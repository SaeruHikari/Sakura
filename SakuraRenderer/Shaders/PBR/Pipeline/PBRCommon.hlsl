// Copyright SaeruHikari 2019, PBR Common Utils.
#ifndef _PBR_COMMON_
#define _PBR_COMMON_

struct Material
{
	// Basic PBR material attribute sets.
	//
	float3	BaseColor;
	float	Opacity;
	//
	float	Metallic;
	float	Roughness;
	float   SpecularTint;
	float   SpecularStrength;

	float3	SpecularColor;
	float 	Anisotropic; 
	
	float 	Subsurface;
	float3  SubsurfaceColor;
	
	float   Clearcoat;
	float	ClearcoarGloss;
	float   Sheen;
	float 	SheenTint;
};

struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Direction; // directional/spot light only
    float FalloffEnd; // point/spot light only
    float3 Position; // point light only
    float SpotPower; // spot light only
};


#endif
