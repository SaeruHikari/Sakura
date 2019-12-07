// Saeru Hikari
//#define SAKURA_ENABLE_ANISO
#include "ScreenQuadVertex.hlsl"
//Include structures and functions for lighting
#include "PassCommon.hlsl"
#include "BRDF.hlsl"
#include "Utils.hlsl"
// ----------------------------------------------------------------------------
// http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
// efficient VanDerCorpus calculation.
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}
// ----------------------------------------------------------------------------
float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}
// ----------------------------------------------------------------------------
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
	
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	
	// from spherical coordinates to cartesian coordinates - halfway vector
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
	
	// from tangent-space H vector to world-space sample vector
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
	
    return tangent * H.x + bitangent * H.y + N * H.z;
}
// ----------------------------------------------------------------------------
float GeometrySchlickGGX(float NdotV, float roughness)
{
    // note that we use a different k for IBL
    float a = roughness;
    float k = (a * a) / 2.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float IBL_PBR_Specular_G(float NoL, float NoV, float a)
{
    float a2 = a * a;
    a2 = a2 * a2;
    float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
    float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
    return (2 * NoL) / (GGXL + GGXV);
}

float IBL_PBR_Diffuse(float LoH, float NoL, float NoV, float Roughness)
{
    float F90 = lerp(0, 0.5, Roughness) + (2 * LoH * LoH * Roughness);
    return F_Schlik(1, F90, NoL) * F_Schlik(1, F90, NoV) * lerp(1, 1 / 0.662, Roughness);
}

float3 CosineSampleHemisphere(float u1, float u2)
{
    float r = sqrt(u1);
    float theta = 2 * PI * u2;
 
    float x = r * cos(theta);
    float y = r * sin(theta);
 
    return float3(x, y, sqrt(max(0.0f, 1 - u1)));
}

// ----------------------------------------------------------------------------
float2 IntegrateBRDF(float NdotV, float roughness)
{
    float3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    float3 N = float3(0.0, 0.0, 1.0);
    
    const uint SAMPLE_COUNT = 2048u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        // generates a sample vector that's biased towards the
        // preferred alignment direction (importance sampling).
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0)
        {
            float G = GeometrySmith(N, V, L, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return float2(A, B);
}

float IBL_Default_DiffuseIntegrated(float Roughness, float NoV)
{
    float3 V;
    V.x = sqrt(1 - NoV * NoV);
    V.y = 0;
    V.z = NoV;
    
    float r = 0;
    const uint SAMPLE_COUNT = 64;
    
    for (uint i = 0; i < SAMPLE_COUNT; i++)
    {
        float2 E = Hammersley(i, SAMPLE_COUNT);
        float3 H = CosineSampleHemisphere(E.x, E.y);
        float3 L = 2 * dot(V, H) * H - V;
        
        float NoL = saturate(L.z);
        float LoH = saturate(dot(L, H.xyz));
        
        if(LoH > 0)
        {
            float Diffuse = IBL_PBR_Diffuse(LoH, NoL, NoV, Roughness);
            r += Diffuse;
        }
    }
    return r / (float) SAMPLE_COUNT;
}


float4 PS(VertexOut pin) : SV_Target
{
    // x Nov y Roughness
    float NoV = pin.TexC.x;
    float Roughness = pin.TexC.y;
#if defined(Filament_MS)
    float3 res;
    float3 V;
    V.x = sqrt(1 - NoV * NoV);
    V.y = 0;
    V.z = NoV;
    
    float2 r = 0;
    const uint SAMPLE_COUNT = 2048u;
    float3 N = float3(0.0, 0.0, 1.0);
    
    for (uint i = 0; i < SAMPLE_COUNT; i++)
    {
        float2 E = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(E, N, Roughness);
        float3 L = 2 * dot(V, H) * H.xyz - V;
        
        float VoH = saturate(dot(V, H.xyz));
        float NoL = saturate(L.z);
        float NoH = saturate(H.z);
        
        if(NoL > 0)
        {
            float G = IBL_PBR_Specular_G(NoL, NoV, Roughness);
            float Gv = G * VoH / NoH;
            float Fc = (1 - VoH) * (1 - VoH);
            Fc = Fc * Fc * (1 - VoH);
            r.x = r.x + Gv;
            r.y = r.y + Gv * Fc;
        }
    }
    res.xy = r.xy / (float) SAMPLE_COUNT;
    res.z = IBL_Default_DiffuseIntegrated(Roughness, NoV);
    return float4(res, 1.f);
#else
    float2 integratedBRDF = IntegrateBRDF(NoV, Roughness);
    return float4(integratedBRDF, 0.f, 1.f);
#endif
}

