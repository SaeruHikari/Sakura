#pragma once
#include "Framework\GraphicTypes\D3D12\SDx12Pass.hpp"

using namespace Microsoft::WRL;

namespace SGraphics
{
	class SDeferredPass : public SDx12Pass
	{
	public:
		bool bWriteDepth = true;
		SDeferredPass(ID3D12Device* device, bool bwriteDpeth = false)
			:SDx12Pass(device), bWriteDepth(bwriteDpeth)
		{
		}
		REFLECTION_ENABLE(SDx12Pass)
	public:
		virtual bool Initialize(std::vector<ID3D12Resource*> srvResources) override
		{
			if (PS == nullptr)
				PS = d3dUtil::CompileShader(L"Shaders\\PBR\\Pipeline\\DisneyPBRShaderDeferred.hlsl", nullptr, "PS", "ps_5_1");
			if (VS == nullptr)
				VS = d3dUtil::CompileShader(L"Shaders\\PBR\\Pipeline\\ScreenQuadVS.hlsl", nullptr, "VS", "vs_5_1");
			mInputLayout =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
			};
			return __dx12Pass::Initialize(srvResources);
		}

		int DebugNumSrvResource = 100;
		// bind resource to srv heap ?
		void BuildDescriptorHeaps(std::vector<ID3D12Resource*> mSrvResources)
		{
			D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
			// !
			srvHeapDesc.NumDescriptors = DebugNumSrvResource;
			srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeaps[0])));

			// Fill out the heap with actual descriptors:
			CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeaps[0]->GetCPUDescriptorHandleForHeapStart());

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			// LOD Clamp 0, ban nega val
			srvDesc.Texture2D.ResourceMinLODClamp = 0.f;

			// Iterate
			for (int i = 0; i < 5; i++)
			{
				auto resource = mSrvResources[i];
				srvDesc.Format = resource->GetDesc().Format;
				srvDesc.Texture2D.MipLevels = resource->GetDesc().MipLevels;
				mDevice->CreateShaderResourceView(resource, &srvDesc, hDescriptor);
				hDescriptor.Offset(1, mCbvSrvDescriptorSize);
			}
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDescCube = {};
			srvDescCube.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDescCube.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDescCube.TextureCube.MostDetailedMip = 0;
			// LOD Clamp 0, ban nega val
			srvDescCube.TextureCube.ResourceMinLODClamp = 0.f;
			for (int i = 5; i < mSrvResources.size(); i++)
			{
				auto resource = mSrvResources[i];
				srvDescCube.Format = resource->GetDesc().Format;
				srvDescCube.TextureCube.MipLevels = resource->GetDesc().MipLevels;
				mDevice->CreateShaderResourceView(resource, &srvDescCube, hDescriptor);
				hDescriptor.Offset(1, mCbvSrvDescriptorSize);
			}
		}

		void BuildPSO()
		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC deferredPsoDesc;
			// PSO for opaque objects.
			ZeroMemory(&deferredPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
			deferredPsoDesc.InputLayout = { mInputLayout.data(), ((UINT)mInputLayout.size()) };
			deferredPsoDesc.pRootSignature = mRootSignature.Get();
			deferredPsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(VS->GetBufferPointer()),
				VS->GetBufferSize()
			};
			deferredPsoDesc.PS =
			{
				reinterpret_cast<BYTE*>(PS->GetBufferPointer()),
				PS->GetBufferSize()
			};
			deferredPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			deferredPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			deferredPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			deferredPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			deferredPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			deferredPsoDesc.SampleMask = UINT_MAX;
			deferredPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			deferredPsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			deferredPsoDesc.pRootSignature = mRootSignature.Get();
			deferredPsoDesc.NumRenderTargets = 1;
			deferredPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			deferredPsoDesc.SampleDesc.Count = 1;
			deferredPsoDesc.SampleDesc.Quality = 0;
			ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&deferredPsoDesc, IID_PPV_ARGS(&mPSO)));
		}

		void BuildRootSignature()
		{
			CD3DX12_DESCRIPTOR_RANGE texTable, texTable1, texTable2, texTable3, texTable4, texTable5;
			texTable.Init(
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				1,  // number of descriptors
				0); // register t0
			texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				1,
				1);
			texTable2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				1,
				2);
			texTable3.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				1,
				3);
			texTable4.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				1,  // number of descriptors
				4); // register t4
			texTable5.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				6,
				5);

			// Root parameter can be a table, root descriptor or root constants.
			CD3DX12_ROOT_PARAMETER slotRootParameter[9];

			// Create root CBV
			slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
			slotRootParameter[1].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);
			slotRootParameter[2].InitAsDescriptorTable(1, &texTable2, D3D12_SHADER_VISIBILITY_PIXEL);
			slotRootParameter[3].InitAsDescriptorTable(1, &texTable3, D3D12_SHADER_VISIBILITY_PIXEL);
			slotRootParameter[4].InitAsConstantBufferView(0);
			slotRootParameter[5].InitAsConstantBufferView(1);
			slotRootParameter[6].InitAsConstantBufferView(2);
			slotRootParameter[7].InitAsDescriptorTable(1, &texTable4, D3D12_SHADER_VISIBILITY_PIXEL);
			slotRootParameter[8].InitAsDescriptorTable(1, &texTable5, D3D12_SHADER_VISIBILITY_PIXEL);

			auto staticSamplers = HikaD3DUtils::GetStaticSamplers();

			// A root signature is an array of root parameters.
			CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(9, slotRootParameter,
				(UINT)staticSamplers.size(), staticSamplers.data(),
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			// Create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer.
			Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
			Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
			HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
				serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

			if (errorBlob != nullptr)
			{
				::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
			}
			ThrowIfFailed(hr);

			ThrowIfFailed(mDevice->CreateRootSignature(
				0,
				serializedRootSig->GetBufferPointer(),
				serializedRootSig->GetBufferSize(),
				IID_PPV_ARGS(mRootSignature.GetAddressOf())));
		}

		void BindPerPassResource(ID3D12GraphicsCommandList* cmdList, SFrameResource* frameResource, size_t passSrvNum)
		{
			auto passCB = frameResource->PassCB->Resource();
			// Injected tex array
			auto hDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeaps[0]->GetGPUDescriptorHandleForHeapStart());
			cmdList->SetGraphicsRootDescriptorTable(0, hDescriptor);
			hDescriptor.Offset(1, mCbvSrvDescriptorSize);
			cmdList->SetGraphicsRootDescriptorTable(1, hDescriptor);
			hDescriptor.Offset(1, mCbvSrvDescriptorSize);
			cmdList->SetGraphicsRootDescriptorTable(2, hDescriptor);
			hDescriptor.Offset(1, mCbvSrvDescriptorSize);
			cmdList->SetGraphicsRootDescriptorTable(3, hDescriptor);
			hDescriptor.Offset(1, mCbvSrvDescriptorSize);
			cmdList->SetGraphicsRootDescriptorTable(7, hDescriptor);
			hDescriptor.Offset(1, mCbvSrvDescriptorSize);
			cmdList->SetGraphicsRootDescriptorTable(8, hDescriptor);

			cmdList->SetGraphicsRootConstantBufferView(5, passCB->GetGPUVirtualAddress());

			objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(SRenderMeshConstants));
			matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

			objectCB = frameResource->ObjectCB->Resource();
			matCB = frameResource->MaterialCB->Resource();
		}

		void BindPerRenderItemResource(ID3D12GraphicsCommandList* cmdList, SFrameResource* frameResource, SDxRenderItem* ri)
		{
			D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
			D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

			cmdList->SetGraphicsRootConstantBufferView(4, objCBAddress);
			cmdList->SetGraphicsRootConstantBufferView(6, matCBAddress);
		}

	private:
		UINT objCBByteSize;
		UINT matCBByteSize;
		ID3D12Resource* objectCB;
		ID3D12Resource* matCB;

	};
}