#include <minwindef.h>
#include "SDxRendererGM.h"
#include "../../../Core/Nodes/EngineNodes/SStaticMeshNode.hpp"
#include "../../../GraphicTypes/FrameGraph/SakuraFrameGraph.h"
#include "../CJsonObject/CJsonObject.hpp"
#include "../HikaUtils/HikaCommonUtils/HikaCommonUtil.h"
#define Sakura_Full_Effects
//#define Sakura_Debug_PureColor

#define Sakura_MotionVector

#define Sakura_Defferred 
#define Sakura_SSAO 

#define Sakura_IBL
#define Sakura_IBL_HDR_INPUT

#define Sakura_TAA

#define Sakura_GBUFFER_DEBUG 

#ifndef Sakura_MotionVector
#undef Sakura_TAA
#endif
#if defined(Sakura_TAA)
#ifndef Sakura_MotionVector
#define Sakura_MotionVector
#endif
#endif

#include "Pipeline/SsaoPass.hpp"
#include "Pipeline/GBufferPass.hpp"
#include "Pipeline/IBL/SkySpherePass.hpp"
#include "Pipeline/IBL/HDR2CubeMapPass.hpp"
#include "Pipeline/ScreenSpaceEfx/TAAPass.hpp"
#include "Pipeline/IBL/CubeMapConvolutionPass.hpp"
#include "Pipeline/IBL/BRDFLutPass.hpp"
#include "Pipeline/MotionVectorPass.hpp"
#include "Pipeline/DeferredPass.hpp"
#include "Debug/GBufferDebugPass.hpp"

namespace SGraphics
{
	REFLECTION_ENGINE(
	registration::class_<SkySpherePass>("SkySpherePass")
		.constructor<ID3D12Device*>();
	registration::class_<SsaoPass>("SsaoPass")
		.constructor<ID3D12Device*>();
	registration::class_<SMotionVectorPass>("SMotionVectorPass")
		.constructor<ID3D12Device*>();
	registration::class_<SGBufferPass>("SGBufferPass")
		.constructor<ID3D12Device*>();
	registration::class_<SDeferredPass>("SDeferredPass")
		.constructor<ID3D12Device*>();
	registration::class_<STaaPass>("STaaPass")
		.constructor<ID3D12Device*>();
	registration::class_<SGBufferDebugPass>("SGBufferDebugPass")
		.constructor<ID3D12Device*>();
	);
}



#define TINYEXR_IMPLEMENTATION
#include "Includes/tinyexr.h"

namespace SGraphics
{
	bool SDxRendererGM::Initialize()
	{
		if (!SakuraD3D12GraphicsManager::Initialize())
			return false;
		GetFrameGraph()->Initialize();
		BuildGeometry();

		// Reset the command list to prep for initialization commands.
		ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
		// Get the increment size of a descriptor in this heap type. This is hardware specific
		// so we have to query this information
		mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		mCamera.SetPosition(0.0f, 0.0f, 0.0f);
		BuildCubeFaceCamera(0.0f, 2.0f, 0.0f);

		LoadTextures();
		BuildMaterials();
		BuildRenderItems();
		BuildDescriptorHeaps();

		mImGuiDebugger = std::make_unique<SDx12ImGuiDebugger>();
		mImGuiDebugger->Initialize(mhMainWnd, md3dDevice.Get(),
			GetResourceManager()->GetOrAllocDescriptorHeap(SRVs::ImGuiSrvName)->DescriptorHeap());
		// �� Do not have dependency on dx12 resources
		{
			{
#if defined(Sakura_MotionVector)
				ISRenderTargetProperties vprop;
				vprop.mRtvFormat = DXGI_FORMAT_R16G16_UNORM;
				vprop.bScaleWithViewport = true;
				vprop.rtType = ERenderTargetTypes::E_RT2D;
				vprop.mWidth = mGraphicsConfs->clientWidth;
				vprop.mHeight = mGraphicsConfs->clientHeight;
				if (GetFrameGraph()->
					RegistNamedResourceNode<SDx12RenderTarget2D>
					(RT2Ds::MotionVectorRTName, vprop, RTVs::ScreenEfxRtvName, SRVs::ScreenEfxSrvName) != nullptr)
				{
					mMotionVectorRT = GetFrameGraph()->GetNamedRenderResource<SDx12RenderTarget2D>(RT2Ds::MotionVectorRTName);
				}
#endif
			}
#if defined(Sakura_IBL)		
			ISRenderTargetProperties prop;
			prop.mRtvFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
			prop.rtType = ERenderTargetTypes::E_RT2D;
			prop.mWidth = 4096;
			prop.mHeight = 4096;
			if (GetFrameGraph()->
				RegistNamedResourceNode<SDx12RenderTarget2D>
				(RT2Ds::BrdfLutRTName, prop, RTVs::CaptureRtvName, SRVs::CaptureSrvName) != nullptr)
			{
				mBrdfLutRT2D = GetFrameGraph()->GetNamedRenderResource<SDx12RenderTarget2D>(RT2Ds::BrdfLutRTName);
			}
			UINT _sizeS = 2048;
			for (int j = 0; j < SkyCubeMips; j++)
			{
				CD3DX12_CPU_DESCRIPTOR_HANDLE convCubeRtvHandles[6];
				prop.mHeight = _sizeS;
				prop.mWidth = _sizeS;
				prop.mRtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
				prop.rtType = ERenderTargetTypes::E_RT3D;
				_sizeS /= 2;
				prop.bScaleWithViewport = false;
				if (GetFrameGraph()->
					RegistNamedResourceNode<SDx12RenderTargetCube>
					(RT3Ds::SkyCubeRTNames[j], prop, RTVs::DeferredRtvName, SRVs::DeferredSrvName) != nullptr)
				{
					mSkyCubeRT[j] = GetFrameGraph()->GetNamedRenderResource<SDx12RenderTargetCube>(RT3Ds::SkyCubeRTNames[j]);
				}
			}
			UINT size = 1024;
			CD3DX12_CPU_DESCRIPTOR_HANDLE cubeRtvHandles[6];
			for (int i = 0; i < SkyCubeConvFilterNum; i++)
			{
				if (i != 0)
				{
					prop.mHeight = size;
					prop.mWidth = size;
					size = size / 2;
				}
				else
				{
					prop.mHeight = 64;
					prop.mWidth = 64;
				}
				if (GetFrameGraph()->
					RegistNamedResourceNode<SDx12RenderTargetCube>
					(RT3Ds::ConvAndPrefilterNames[i], prop, RTVs::DeferredRtvName, SRVs::DeferredSrvName) != nullptr)
				{
					mConvAndPrefilterCubeRTs[i] =
						GetFrameGraph()->GetNamedRenderResource<SDx12RenderTargetCube>(RT3Ds::ConvAndPrefilterNames[i]);
				}
			}
#endif
			// Velocity and History
#if defined(Sakura_TAA)
			mTaaRTs = new SDx12RenderTarget2D*[TAARtvsNum];
			for (int i = 0; i < TAARtvsNum; i++)
			{
				ISRenderTargetProperties vprop;
				vprop.mRtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
				vprop.bScaleWithViewport = true;
				vprop.rtType = ERenderTargetTypes::E_RT2D;
				vprop.mWidth = mGraphicsConfs->clientWidth;
				vprop.mHeight = mGraphicsConfs->clientHeight;
				if (GetFrameGraph()->
					RegistNamedResourceNode<SDx12RenderTarget2D>
					(RT2Ds::TAARTNames[i], vprop, RTVs::ScreenEfxRtvName, SRVs::ScreenEfxSrvName) != nullptr)
				{
					mTaaRTs[i] =
						GetFrameGraph()->GetNamedRenderResource<SDx12RenderTarget2D>(RT2Ds::TAARTNames[i]);
				}
			}
#endif
			// Create resources depended by passes. 
#if defined(Sakura_Defferred)
			GBufferRTs = new SDx12RenderTarget2D* [GBufferRTNum];
			for (int i = 0; i < GBufferRTNum; i++)
			{
				ISRenderTargetProperties prop(0.f, 0.f, (i == 3) ? 1.f : 0.f, 0.f);
				prop.bScaleWithViewport = true;
				prop.rtType = ERenderTargetTypes::E_RT2D;
				prop.mWidth = mGraphicsConfs->clientWidth;
				prop.mHeight = mGraphicsConfs->clientHeight;
				prop.mRtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
				if (GetFrameGraph()->
					RegistNamedResourceNode<SDx12RenderTarget2D>
					(RT2Ds::GBufferRTNames[i], prop, RTVs::DeferredRtvName, SRVs::DeferredSrvName) != nullptr)
				{
					GBufferRTs[i] =
						GetFrameGraph()->GetNamedRenderResource<SDx12RenderTarget2D>(RT2Ds::GBufferRTNames[i]);
				}
			}
#endif
		}
		//�� Do have dependency on dx12 resources
		BindPassResources();
		BuildFrameResources();
		mCurrFrameResource = GetRenderScene()->GetFrameResource(mCurrFrameResourceIndex);

		std::string DS = "DepthStencil";
		//InitFGFromJson((char*)"Resources/RenderPipeline/StandardPipeline.json");
		
#pragma region GraphInit
		// Pass declares:
		
#if defined(Sakura_Defferred)
#if defined(Sakura_MotionVector)
		auto mGbufferPass = GetFrameGraph()->
			RegistNamedPassNode<SGBufferPass>(ConsistingPasses::GBufferPassName, false, md3dDevice.Get(), false);
		GetFrameGraph()->
			RegistNamedPassNode<SMotionVectorPass>(ConsistingPasses::MotionVectorPassName, false, md3dDevice.Get());
		auto mMotionVectorPassNode = GetFrameGraph()->GetNamedPassNode(ConsistingPasses::MotionVectorPassName);
		mMotionVectorPassNode->ConfirmOutput(RT2Ds::MotionVectorRTName);
#else
		auto mGbufferPass = GetResourceManager()->
			RegistNamedPassNode<SGBufferPass>(ConsistingPasses::GBufferPassName, false, md3dDevice.Get(), true);
#endif
		SFG_PassNode* mGbufferPassNode = GetFrameGraph()->
			GetNamedPassNode(ConsistingPasses::GBufferPassName);
		mGbufferPassNode->GetPass()->StartUp(mCommandList.Get());
		mGbufferPassNode->ConfirmInput(GBufferPassResources, mMotionVectorPassNode);
		GetFrameGraph()->GetNamedPassNode(ConsistingPasses::GBufferPassName)
			->ConfirmOutput(RT2Ds::GBufferRTNames[0], RT2Ds::GBufferRTNames[1],
				RT2Ds::GBufferRTNames[2], RT2Ds::GBufferRTNames[3]);
#if defined(Sakura_SSAO)
		auto mSsaoPassNode = GetFrameGraph()->
			RegistNamedPassNode<SsaoPass>(ConsistingPasses::SsaoPassName, false, md3dDevice.Get());
		mSsaoPassNode->GetPass()->StartUp(mCommandList.Get());
		GetFrameGraph()->GetNamedPassNode(ConsistingPasses::SsaoPassName)
			->ConfirmInput(
				mGbufferPassNode->GetOutput(RT2Ds::GBufferRTNames[1]),
				DS);
		GetFrameGraph()->GetNamedPassNode(ConsistingPasses::SsaoPassName)
			->ConfirmOutput(RT2Ds::GBufferRTNames[1]);
#endif
#endif

#if defined(Sakura_Defferred)
		auto mDeferredPass = GetFrameGraph()->
			RegistNamedPassNode<SDeferredPass>(ConsistingPasses::DeferredPassName, false, md3dDevice.Get());
		GetFrameGraph()->GetNamedPassNode(ConsistingPasses::DeferredPassName)
			->ConfirmInput(
				mGbufferPassNode->GetOutput(RT2Ds::GBufferRTNames[0]),
				mSsaoPassNode->GetOutput(RT2Ds::GBufferRTNames[1]),
				mGbufferPassNode->GetOutput(RT2Ds::GBufferRTNames[2]),
				mGbufferPassNode->GetOutput(RT2Ds::GBufferRTNames[3]),
				RT2Ds::BrdfLutRTName,
				RT3Ds::ConvAndPrefilterNames[0], RT3Ds::ConvAndPrefilterNames[1],
				RT3Ds::ConvAndPrefilterNames[2], RT3Ds::ConvAndPrefilterNames[3],
				RT3Ds::ConvAndPrefilterNames[4], RT3Ds::ConvAndPrefilterNames[5]);

		GetFrameGraph()->GetNamedPassNode(ConsistingPasses::DeferredPassName)
			->ConfirmOutput(RT2Ds::TAARTNames[0]);
#endif

#if defined(Sakura_IBL)
		std::shared_ptr<SBrdfLutPass> brdfPass = nullptr;
		// Draw brdf Lut
		brdfPass = std::make_shared<SBrdfLutPass>(md3dDevice.Get());
		brdfPass->PushRenderItems(GetRenderScene()->GetRenderLayer(ERenderLayer::E_ScreenQuad));
		brdfPass->Initialize();
		//Update the viewport transform to cover the client area
		auto mDrawSkyPassNode = GetFrameGraph()->
			RegistNamedPassNode<SkySpherePass>(ConsistingPasses::SkySpherePassName, false, md3dDevice.Get());
		mMotionVectorPassNode->GetPass()->PushRenderItems(GetRenderScene()->GetRenderLayer(ERenderLayer::E_Opaque));
		GetFrameGraph()->GetNamedPassNode(ConsistingPasses::SkySpherePassName)
			->ConfirmInput(RT3Ds::SkyCubeRTNames[0], RT3Ds::SkyCubeRTNames[1], RT3Ds::SkyCubeRTNames[2],
				RT3Ds::SkyCubeRTNames[3], RT3Ds::SkyCubeRTNames[4], 
				RT3Ds::SkyCubeRTNames[5], RT3Ds::SkyCubeRTNames[6], RT3Ds::SkyCubeRTNames[7], mDeferredPass);
		GetFrameGraph()->GetNamedPassNode(ConsistingPasses::SkySpherePassName)
			->ConfirmOutput(RT2Ds::TAARTNames[0]);
#endif

#if defined(Sakura_TAA)
		auto mTaaPass = GetFrameGraph()->
			RegistNamedPassNode<STaaPass>(ConsistingPasses::TaaPassName, true, md3dDevice.Get());
		GetFrameGraph()->
			GetNamedPassNode(ConsistingPasses::TaaPassName)
			->ConfirmOutput(RT2Ds::TAARTNames[1], RT2Ds::TAARTNames[2]);
		GetFrameGraph()->
			GetNamedPassNode(ConsistingPasses::TaaPassName)
			->ConfirmInput(
				RT2Ds::TAARTNames[1],
				RT2Ds::TAARTNames[2],
				mDrawSkyPassNode->GetOutput(RT2Ds::TAARTNames[0]),
				mMotionVectorPassNode->GetOutput(RT2Ds::MotionVectorRTName), DS);
#endif
#if defined(Sakura_Defferred)
	#if defined(Sakura_GBUFFER_DEBUG)
			auto mGBufferDebugPass = GetFrameGraph()->
				RegistNamedPassNode<SGBufferDebugPass>(ConsistingPasses::GBufferDebugPassName, true, md3dDevice.Get());
			GetFrameGraph()->GetNamedPassNode(ConsistingPasses::GBufferDebugPassName)
				->ConfirmInput(
					mGbufferPassNode->GetOutput(RT2Ds::GBufferRTNames[0]),
					mSsaoPassNode->GetOutput(RT2Ds::GBufferRTNames[1]),
					mGbufferPassNode->GetOutput(RT2Ds::GBufferRTNames[2]),
					mGbufferPassNode->GetOutput(RT2Ds::GBufferRTNames[3]),
					RT2Ds::BrdfLutRTName,
					RT3Ds::ConvAndPrefilterNames[0], RT3Ds::ConvAndPrefilterNames[1],
					RT3Ds::ConvAndPrefilterNames[2], RT3Ds::ConvAndPrefilterNames[3],
					RT3Ds::ConvAndPrefilterNames[4], RT3Ds::ConvAndPrefilterNames[5],
					GetFrameGraph()->GetNamedPassNode(ConsistingPasses::TaaPassName));
	#endif
#endif
#pragma endregion 

#if defined(Sakura_IBL_HDR_INPUT)
		std::shared_ptr<SHDR2CubeMapPass> mHDRUnpackPass = nullptr;
		std::shared_ptr<SCubeMapConvPass> mCubeMapConvPass = nullptr;
		mHDRUnpackPass = std::make_shared<SHDR2CubeMapPass>(md3dDevice.Get());
		mCubeMapConvPass = std::make_shared<SCubeMapConvPass>(md3dDevice.Get());
		mHDRUnpackPass->PushRenderItems(GetRenderScene()->GetRenderLayer(ERenderLayer::E_Cube));
		mCubeMapConvPass->PushRenderItems(GetRenderScene()->GetRenderLayer(ERenderLayer::E_Cube));
		std::vector<ID3D12Resource*> mHDRResource;
		mHDRResource.push_back(((SD3DTexture*)GetFrameGraph()->
			GetNamedRenderResource(Textures::texNames[5]))->Resource.Get());
		mHDRUnpackPass->Initialize(mHDRResource);
#endif
		// Execute the initialization commands.
		ThrowIfFailed(mCommandList->Close());
		ID3D12CommandList* cmdsList[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsList), cmdsList);
		// Wait until initialization is complete
		FlushCommandQueue();

		pFrameGraph->Compile();
		OnResize(mGraphicsConfs->clientWidth, mGraphicsConfs->clientHeight);


		// Pre-Compute RTs
		auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
		// Reuse the memory associated with command recording.
		// We can only reset when the associated command lists have finished execution on the GPU.
		ThrowIfFailed(cmdListAlloc->Reset());
		// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
		// Reusing the command list reuses memory.
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr))
		Tick(1 / 60.f);

		// Initialize Draw
#if defined(Sakura_IBL_HDR_INPUT) && defined(Sakura_IBL)
		static int Init = 0;
		if (Init < 6)
		{
			mCommandList->ResourceBarrier(1,
				&CD3DX12_RESOURCE_BARRIER::Transition(mBrdfLutRT2D->mResource.Get(),
					D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));
			mBrdfLutRT2D->ClearRenderTarget(mCommandList.Get());
			for (size_t i = 0; i < SkyCubeConvFilterNum; i++)
			{
				mCommandList->ResourceBarrier(1,
					&CD3DX12_RESOURCE_BARRIER::Transition(mConvAndPrefilterCubeRTs[i]->Resource(),
						D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));
				mConvAndPrefilterCubeRTs[i]->ClearRenderTarget(mCommandList.Get());
			}
			for (size_t i = 0; i < SkyCubeMips; i++)
			{
				// Prepare to unpack HDRI map to cubemap.
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mSkyCubeRT[i]->Resource(),
					D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));
				mSkyCubeRT[i]->ClearRenderTarget(mCommandList.Get());
			}
			
			D3D12_VIEWPORT screenViewport0;
			D3D12_RECT scissorRect0;
			screenViewport0.TopLeftX = 0;
			screenViewport0.TopLeftY = 0;
			screenViewport0.Width = static_cast<float>(4096);
			screenViewport0.Height = static_cast<float>(4096);
			screenViewport0.MinDepth = 0.0f;
			screenViewport0.MaxDepth = 1.0f;
			scissorRect0 = { 0, 0, 4096, 4096 };
			mCommandList->RSSetViewports(1, &screenViewport0);
			mCommandList->RSSetScissorRects(1, &scissorRect0);
			brdfPass->Draw(mCommandList.Get(), nullptr, mCurrFrameResource,
				&GetResourceManager()->GetOrAllocDescriptorHeap(RTVs::CaptureRtvName)->GetCPUtDescriptorHandle(0), 1);
			mCommandList->ResourceBarrier(1,
				&CD3DX12_RESOURCE_BARRIER::Transition(mBrdfLutRT2D->mResource.Get(),
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));

			//Update the viewport transform to cover the client area
			D3D12_VIEWPORT screenViewport;
			D3D12_RECT scissorRect;
			screenViewport.TopLeftX = 0;
			screenViewport.TopLeftY = 0;
			screenViewport.Width = static_cast<float>(2048);
			screenViewport.Height = static_cast<float>(2048);
			screenViewport.MinDepth = 0.0f;
			screenViewport.MaxDepth = 1.0f;
			scissorRect = { 0, 0, 2048, 2048 };
			mCommandList->RSSetViewports(1, &screenViewport);
			mCommandList->RSSetScissorRects(1, &scissorRect);
			for (size_t i = 0; i < SkyCubeMips; i++)
			{
				SPassConstants cubeFacePassCB = mMainPassCB;
				cubeFacePassCB.RenderTargetSize =
					XMFLOAT2(mSkyCubeRT[i]->mProperties.mWidth,
						mSkyCubeRT[i]->mProperties.mHeight);
				cubeFacePassCB.InvRenderTargetSize =
					XMFLOAT2(1.0f / mSkyCubeRT[i]->mProperties.mWidth,
						1.0f / mSkyCubeRT[i]->mProperties.mHeight);
				screenViewport.Width = static_cast<float>(mSkyCubeRT[i]->mProperties.mWidth);
				screenViewport.Height = static_cast<float>(mSkyCubeRT[i]->mProperties.mHeight);
				screenViewport.MinDepth = 0.0f;
				screenViewport.MaxDepth = 1.0f;
				scissorRect = { 0, 0, (LONG)screenViewport.Width, (LONG)screenViewport.Height };
				mCommandList->RSSetViewports(1, &screenViewport);
				mCommandList->RSSetScissorRects(1, &scissorRect);
				// Cube map pass cbuffers are stored in elements 1-6.	
				D3D12_CPU_DESCRIPTOR_HANDLE skyRtvs;
				while (Init < 6)
				{
					XMMATRIX view = mCubeMapCamera[Init].GetView();
					XMMATRIX proj = mCubeMapCamera[Init].GetProj();

					XMMATRIX viewProj = XMMatrixMultiply(view, proj);
					XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
					XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
					XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

					XMStoreFloat4x4(&cubeFacePassCB.View, XMMatrixTranspose(view));
					XMStoreFloat4x4(&cubeFacePassCB.InvView, XMMatrixTranspose(invView));
					XMStoreFloat4x4(&cubeFacePassCB.Proj, XMMatrixTranspose(proj));
					XMStoreFloat4x4(&cubeFacePassCB.InvProj, XMMatrixTranspose(invProj));
					XMStoreFloat4x4(&cubeFacePassCB.ViewProj, XMMatrixTranspose(viewProj));
					XMStoreFloat4x4(&cubeFacePassCB.InvViewProj, XMMatrixTranspose(invViewProj));
					cubeFacePassCB.EyePosW = mCubeMapCamera[Init].GetPosition3f();
					auto currPassCB = mCurrFrameResource->PassCB.get();
					currPassCB->CopyData(1 + Init, cubeFacePassCB);
					skyRtvs = mSkyCubeRT[i]->GetRenderTargetHandle(Init)->hCpu;
					mHDRUnpackPass->Draw(mCommandList.Get(), nullptr, mCurrFrameResource, &skyRtvs, 1, 1 + Init);
					Init++;
				}
				Init = 0;
			}
			mSkyCubeResource.resize(SkyCubeMips);
			for (size_t i = 0; i < SkyCubeMips; i++)
			{
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mSkyCubeRT[i]->Resource(),
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
				mSkyCubeResource[i] = mSkyCubeRT[i]->Resource();
			}
			// Initialize Convolution Pass
			mCubeMapConvPass->Initialize(mSkyCubeResource);
			for (size_t i = 0; i < SkyCubeConvFilterNum; i++)
			{
				Init = 0;
				screenViewport.TopLeftX = 0;
				screenViewport.TopLeftY = 0;
				screenViewport.Width = static_cast<float>(mConvAndPrefilterCubeRTs[i]->mProperties.mWidth);
				screenViewport.Height = static_cast<float>(mConvAndPrefilterCubeRTs[i]->mProperties.mHeight);
				screenViewport.MinDepth = 0.0f;
				screenViewport.MaxDepth = 1.0f;
				scissorRect = { 0, 0, (LONG)mConvAndPrefilterCubeRTs[i]->mProperties.mWidth,
					(LONG)mConvAndPrefilterCubeRTs[i]->mProperties.mHeight };
				mCommandList->RSSetViewports(1, &screenViewport);
				mCommandList->RSSetScissorRects(1, &scissorRect);
				// Prepare to draw convoluted cube map.
				// Change to RENDER_TARGET.
				SPassConstants convPassCB = mMainPassCB;
				convPassCB.AddOnMsg = i;
				while (Init < 6)
				{
					XMMATRIX view = mCubeMapCamera[Init].GetView();
					XMMATRIX proj = mCubeMapCamera[Init].GetProj();

					XMMATRIX viewProj = XMMatrixMultiply(view, proj);
					XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
					XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
					XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

					XMStoreFloat4x4(&convPassCB.View, XMMatrixTranspose(view));
					XMStoreFloat4x4(&convPassCB.InvView, XMMatrixTranspose(invView));
					XMStoreFloat4x4(&convPassCB.Proj, XMMatrixTranspose(proj));
					XMStoreFloat4x4(&convPassCB.InvProj, XMMatrixTranspose(invProj));
					XMStoreFloat4x4(&convPassCB.ViewProj, XMMatrixTranspose(viewProj));
					XMStoreFloat4x4(&convPassCB.UnjitteredViewProj, XMMatrixTranspose(viewProj));
					XMStoreFloat4x4(&convPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
					convPassCB.EyePosW = mCubeMapCamera[Init].GetPosition3f();
					convPassCB.RenderTargetSize = XMFLOAT2((float)screenViewport.Width,
						(float)screenViewport.Height);
					convPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / screenViewport.Width,
						1.0f / screenViewport.Height);
					auto currPassCB = mCurrFrameResource->PassCB.get();
					// Cube map pass cbuffers are stored in elements 1-6.
					currPassCB->CopyData(1 + Init + 6 + 6 * i, convPassCB);

					D3D12_CPU_DESCRIPTOR_HANDLE iblRtv = mConvAndPrefilterCubeRTs[i]->GetRenderTargetHandle(Init)->hCpu;
					mCubeMapConvPass->Draw(mCommandList.Get(), nullptr, mCurrFrameResource, &iblRtv, 1,
						1 + Init + 6 + 6 * i);
					Init++;
				}
			}
			for (size_t i = 0; i < SkyCubeConvFilterNum; i++)
			{
				mCommandList->ResourceBarrier(1,
					&CD3DX12_RESOURCE_BARRIER::Transition(mConvAndPrefilterCubeRTs[i]->Resource(),
						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
				mConvAndPrefilterSkyCubeResource[i].resize(1);
				mConvAndPrefilterSkyCubeResource[i][0] = mConvAndPrefilterCubeRTs[i]->Resource();
			}

			CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(GetResourceManager()->GetOrAllocDescriptorHeap(SRVs::DeferredSrvName)->GetCPUtDescriptorHandle(0));
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			md3dDevice->CreateShaderResourceView(mBrdfLutRT2D->mResource.Get(), &srvDesc, hDescriptor);
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			for (size_t i = 0; i < SkyCubeMips; i++)
			{
				hDescriptor.Offset(1, mCbvSrvDescriptorSize);
				md3dDevice->CreateShaderResourceView(mSkyCubeRT[i]->Resource(), &srvDesc, hDescriptor);
			}
			srvDesc.Texture2D.MipLevels = 1;
			for (int i = 0; i < SkyCubeConvFilterNum; i++)
			{
				hDescriptor.Offset(1, mCbvSrvDescriptorSize);
				md3dDevice->CreateShaderResourceView(mConvAndPrefilterCubeRTs[i]->Resource(),
					&srvDesc, hDescriptor);
			}
			mDrawSkyPassNode->GetPass()->Initialize(mSkyCubeResource);
		}
#endif

		// Execute the initialization commands.
		ThrowIfFailed(mCommandList->Close());
		ID3D12CommandList* cmdsList0[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsList0), cmdsList0);
		// Wait until initialization is complete
		FlushCommandQueue();
		return true;
	}

	void SDxRendererGM::InitFGFromJson(std::string jsonPath)
	{
		auto jsonstr = HikaCommonUtils::readFileIntoString((char*)jsonPath.c_str());
		neb::CJsonObject oJson(jsonstr);
		for (auto i = 0u; i < oJson["Passes"].GetArraySize(); i++)
		{
			std::string SinglePass;
			if (oJson["Passes"][i].GetKey(SinglePass))
			{
				auto singlePassNode = GetFrameGraph()->
					RegistNamedPassNode(SinglePass, oJson["Passes"][i][SinglePass]["Type"].ToString(), md3dDevice.Get());
			}
		}
		for(auto i = 0u; i < oJson["Passes"].GetArraySize(); i++)
		{
			std::string SinglePass;
			if (oJson["Passes"][i].GetKey(SinglePass))
			{
				auto singlePassNode = GetFrameGraph()->GetNamedPassNode(SinglePass);
				std::vector<SFG_PassNode*> dependencies;
				dependencies.resize(oJson["Passes"][i][SinglePass]["Dependencies"].GetArraySize());
				for (auto j = 0u; j < dependencies.size(); j++)
				{
					dependencies[j] = GetFrameGraph()->GetNamedPassNode(oJson["Passes"][SinglePass]["Dependencies"][j].ToString());
				}
				std::vector<SFG_ResourceHandle> inputs;
				inputs.resize(oJson["Passes"][i][SinglePass]["ResourceIn"].GetArraySize());
				for (auto j = 0u; j < inputs.size(); j++)
				{
					SFG_ResourceHandle handle;
					inputs[j].name = oJson["Passes"][i][SinglePass]["ResourceIn"][j]["Name"].ToString();
					inputs[j].writer = oJson["Passes"][i][SinglePass]["ResourceIn"][j]["Writer"].ToString();
				}

				singlePassNode->ConfirmInput(dependencies, inputs);

				std::vector<std::string> outputs;
				outputs.resize(oJson["Passes"][i][SinglePass]["ResourceOut"].GetArraySize());
				for (auto j = 0u; j < outputs.size(); j++)
				{
					outputs[j] = oJson["Passes"][i][SinglePass]["ResourceOut"][j]["Name"].ToString();
				}
				singlePassNode
					->ConfirmOutput(outputs);

				singlePassNode->GetPass()->StartUp(mCommandList.Get());
			}
		}
	}

	void SDxRendererGM::OnResize(UINT Width, UINT Height)
	{
		SakuraD3D12GraphicsManager::OnResize(Width, Height);
		mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 20000.0f);
		//
#if defined(Sakura_Defferred)
		for (int i = 0; i < GBufferRTNum; i++)
		{
			GBufferRTs[i]->OnResize(md3dDevice.Get(), Width, Height);
		}
#endif
#if defined(Sakura_MotionVector)
		mMotionVectorRT->OnResize(md3dDevice.Get(), Width, Height);
#endif
#if defined(Sakura_TAA)
		for (int i = 0; i < TAARtvsNum; i++)
		{
			mTaaRTs[i]->OnResize(md3dDevice.Get(), Width, Height);
		}
#endif
		GetFrameGraph()->Setup();
	}


	void SDxRendererGM::Draw()
	{
		std::string CurrBufName = "SwapChain" + std::to_string(mCurrBackBuffer);
		auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
		ThrowIfFailed(cmdListAlloc->Reset());
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));
		// Indicate a state transition on the resource usage.
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
		mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::Black, 0, nullptr);
		mCommandList->RSSetViewports(1, &mScreenViewport);
		mCommandList->RSSetScissorRects(1, &mScissorRect);
		GetFrameGraph()->GetNamedRenderPass(ConsistingPasses::GBufferPassName)
			->PushRenderItems(GetRenderScene()->GetRenderLayer(ERenderLayer::E_Opaque));
		GetFrameGraph()->GetNamedRenderPass(ConsistingPasses::SsaoPassName)
			->PushRenderItems(GetRenderScene()->GetRenderLayer(ERenderLayer::E_ScreenQuad));
		GetFrameGraph()->GetNamedRenderPass(ConsistingPasses::DeferredPassName)
			->PushRenderItems(GetRenderScene()->GetRenderLayer(ERenderLayer::E_ScreenQuad));
		GetFrameGraph()->GetNamedRenderPass(ConsistingPasses::SkySpherePassName)
			->PushRenderItems(GetRenderScene()->GetRenderLayer(ERenderLayer::E_SKY));
		GetFrameGraph()->GetNamedRenderPass(ConsistingPasses::TaaPassName)
			->PushRenderItems(GetRenderScene()->GetRenderLayer(ERenderLayer::E_ScreenQuad));
		GetFrameGraph()->GetNamedRenderPass(ConsistingPasses::GBufferDebugPassName)
			->PushRenderItems(GetRenderScene()->GetRenderLayer(ERenderLayer::E_GBufferDebug));
		GetFrameGraph()->GetNamedRenderPass(ConsistingPasses::MotionVectorPassName)
			->PushRenderItems(GetRenderScene()->GetRenderLayer(ERenderLayer::E_Opaque));

		GetFrameGraph()->Execute(&DepthStencilView(), 
			(ISRenderTarget*)GetFrameGraph()->GetNamedRenderResource(CurrBufName), mCurrFrameResource);

#if defined(Sakura_TAA)
		auto mTaaPass = GetFrameGraph()->
			GetNamedRenderPass<STaaPass>(ConsistingPasses::TaaPassName);
		static int mTaaChain = 0;
		mTaaPass->ResourceIndex = mTaaChain;
		mTaaChain = (mTaaChain + 1) % 2;
		GetFrameGraph()->
			GetNamedPassNode(ConsistingPasses::TaaPassName)
			->ConfirmOutput(RT2Ds::TAARTNames[mTaaChain + 1]);
#endif
#if defined(Sakura_Defferred)
#if defined(REVERSE_Z)
		mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.f, 0.f, 0.f, nullptr);
#else
		mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.f, 0.f, 0.f, nullptr);
#endif

#if defined(Sakura_MotionVector)
		GetFrameGraph()->
			GetNamedPassNode(ConsistingPasses::MotionVectorPassName)
			->Execute(mCommandList.Get(), &DepthStencilView(), mCurrFrameResource);
#endif
		GetFrameGraph()->GetNamedPassNode(ConsistingPasses::GBufferPassName)
			->Execute(mCommandList.Get(), &DepthStencilView(), mCurrFrameResource);
	#if defined(Sakura_SSAO)
		GetFrameGraph()->
			GetNamedPassNode(ConsistingPasses::SsaoPassName)
			->Execute(mCommandList.Get(), &DepthStencilView(), mCurrFrameResource);
	#endif
#endif
#if defined(Sakura_Defferred)
		GetFrameGraph()->
			GetNamedPassNode(ConsistingPasses::DeferredPassName)
			->Execute(mCommandList.Get(), &DepthStencilView(), mCurrFrameResource);
#endif
#if defined(Sakura_IBL)
		GetFrameGraph()->GetNamedPassNode(ConsistingPasses::SkySpherePassName)
			->Execute(mCommandList.Get(), &DepthStencilView(), mCurrFrameResource);
#endif
#if defined(Sakura_TAA)
		GetFrameGraph()->
			GetNamedPassNode(ConsistingPasses::TaaPassName)->
			Execute(mCommandList.Get(), &DepthStencilView(), mCurrFrameResource);
		GetFrameGraph()->
			GetNamedPassNode(ConsistingPasses::TaaPassName)->
			Execute(mCommandList.Get(), &DepthStencilView(), mCurrFrameResource,
			(ISRenderTarget*)GetFrameGraph()->GetNamedRenderResource(CurrBufName));
#endif
#if defined(Sakura_GBUFFER_DEBUG) 
		GetFrameGraph()->
			GetNamedPassNode(ConsistingPasses::GBufferDebugPassName)
			->Execute(mCommandList.Get(), &DepthStencilView(), mCurrFrameResource,
			(ISRenderTarget*)GetFrameGraph()->GetNamedRenderResource(CurrBufName));
#endif
		mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, nullptr);
		mImGuiDebugger->Draw(mCommandList.Get(), pFrameGraph.get());
		// Indicate a state transition on the resource usage.
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
		// Done recording commands.

		ThrowIfFailed(mCommandList->Close());
		// Add the command list to the queue for execution.
		ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

		// Advance the fence value to mark commands up to this fence point.
		mCurrFrameResource->Fence = ++mFence->currentFence;

		mCommandQueue->Signal(mFence->fence.Get(), mFence->currentFence);
		ThrowIfFailed(mSwapChain->Present(0, 0));
		mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
	}

	void SDxRendererGM::Finalize()
	{

	}
	void SDxRendererGM::Tick(double deltaTime)
	{
		OnKeyDown(deltaTime);
		//mCamera.UpdateViewMatrix();
		//mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
		// Cycle through the circular frame resource array.
		mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
		mCurrFrameResource = GetRenderScene()->GetFrameResource(mCurrFrameResourceIndex);

		// Has the GPU finished processing the commands of the current frame resource? 
		// If not, wait until the GPU has completed commands up to this fence point
		if (mCurrFrameResource->Fence != 0 && mFence->fence->GetCompletedValue() < mCurrFrameResource->Fence)
		{
			HANDLE eventHandle = CreateEventEx(nullptr, NULL, false, EVENT_ALL_ACCESS);
			ThrowIfFailed(mFence->fence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
			WaitForSingleObject(eventHandle, INFINITE);
			CloseHandle(eventHandle);
		}
		UpdateObjectCBs();
		UpdateMaterialCBs();
		UpdateMainPassCB();
#if defined(Sakura_SSAO)
		UpdateSsaoPassCB();
#endif
	}

	void SDxRendererGM::OnMouseDown(SAKURA_INPUT_MOUSE_TYPES btnState, int x, int y)
	{
		mLastMousePos.x = x;
		mLastMousePos.y = y;
	}

	void SDxRendererGM::OnMouseMove(SAKURA_INPUT_MOUSE_TYPES btnState, int x, int y)
	{
		if ((abs(x - mLastMousePos.x) > 100) | (abs(y - mLastMousePos.y) > 100))
		{
			mLastMousePos.x = x;
			mLastMousePos.y = y;
			return;
		}
		if ((btnState & SAKURA_INPUT_MOUSE_LBUTTON) != 0)
		{
			// Make each pixel correspond to a quarter of a degree.
			float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
			float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

			mCamera.Pitch(dy);
			mCamera.RotateY(dx);
		}
		else if ((btnState & SAKURA_INPUT_MOUSE_RBUTTON) != 0)
		{
			// Make each pixel correspond to 0.2 unit in the scene.
			float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
			float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);


			mCamera.Walk(dx * -10.f);
		}
		mLastMousePos.x = x;
		mLastMousePos.y = y;

	}
	void SDxRendererGM::OnMouseUp(SAKURA_INPUT_MOUSE_TYPES btnState, int x, int y)
	{
	}
	float* SDxRendererGM::CaputureBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
		ID3D12Resource* resourceToRead, size_t outChannels /*= 4*/)
	{
		auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
		FlushCommandQueue();
		ThrowIfFailed(cmdListAlloc->Reset());
		ThrowIfFailed(cmdList->Reset(cmdListAlloc.Get(), nullptr));

		float outputBufferSize = outChannels * sizeof(float) * resourceToRead->GetDesc().Height * resourceToRead->GetDesc().Width;

		// The readback buffer (created below) is on a readback heap, so that the CPU can access it.
		D3D12_HEAP_PROPERTIES readbackHeapProperties{ CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK) };
		D3D12_RESOURCE_DESC readbackBufferDesc{ CD3DX12_RESOURCE_DESC::Buffer(outputBufferSize) };
		ComPtr<::ID3D12Resource> readbackBuffer;
		ThrowIfFailed(device->CreateCommittedResource(
			&readbackHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&readbackBufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr, IID_PPV_ARGS(&readbackBuffer)));

		{
			D3D12_RESOURCE_BARRIER outputBufferResourceBarrier
			{
				CD3DX12_RESOURCE_BARRIER::Transition(
					resourceToRead,
					D3D12_RESOURCE_STATE_GENERIC_READ,
					D3D12_RESOURCE_STATE_COPY_SOURCE)
			};
			cmdList->ResourceBarrier(1, &outputBufferResourceBarrier);
		}

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT  fp;
		UINT nrow;
		UINT64 rowsize, size;
		device->GetCopyableFootprints(&resourceToRead->GetDesc(), 0, 1, 0, &fp, &nrow, &rowsize, &size);

		D3D12_TEXTURE_COPY_LOCATION td;
		td.pResource = readbackBuffer.Get();
		td.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		td.PlacedFootprint = fp;
		
		D3D12_TEXTURE_COPY_LOCATION ts;
		ts.pResource = resourceToRead;
		ts.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		ts.SubresourceIndex = 0;
		cmdList->CopyTextureRegion(&td, 0, 0, 0, &ts, nullptr);

		{
			D3D12_RESOURCE_BARRIER outputBufferResourceBarrier
			{
				CD3DX12_RESOURCE_BARRIER::Transition(
					resourceToRead,
					D3D12_RESOURCE_STATE_COPY_SOURCE,
					D3D12_RESOURCE_STATE_GENERIC_READ)
			};
			cmdList->ResourceBarrier(1, &outputBufferResourceBarrier);
		}

		// Code goes here to close, execute (and optionally reset) the command list, and also
		// to use a fence to wait for the command queue.
		cmdList->Close();
		// Add the command list to the queue for execution.
		ID3D12CommandList* cmdsLists[] = { cmdList };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
		FlushCommandQueue();
		// The code below assumes that the GPU wrote FLOATs to the buffer.
		D3D12_RANGE readbackBufferRange{ 0, outputBufferSize };
		FLOAT* pReadbackBufferData{};
		ThrowIfFailed(
			readbackBuffer->Map
			(
				0,
				&readbackBufferRange,
				reinterpret_cast<void**>(&pReadbackBufferData)
			)
		);
		const char** err = nullptr;
		SaveEXR(pReadbackBufferData, resourceToRead->GetDesc().Width, resourceToRead->GetDesc().Height,
			4, 0, "Capture.exr", err);
		D3D12_RANGE emptyRange{ 0, 0 };
		readbackBuffer->Unmap
		(
			0,
			&emptyRange
		);
		// Reuse the memory associated with command recording.
		// We can only reset when the associated command lists have finished execution on the GPU.
		ThrowIfFailed(cmdListAlloc->Reset());

		// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
		// Reusing the command list reuses memory.
		ThrowIfFailed(cmdList->Reset(cmdListAlloc.Get(), nullptr));
		// Code goes here to close, execute (and optionally reset) the command list, and also
		// to use a fence to wait for the command queue.
		cmdList->Close();
		return pReadbackBufferData;
	}

	void SDxRendererGM::OnKeyDown(double deltaTime)
	{
		auto dt = deltaTime;
		
		if (GetAsyncKeyState('W') & 0x8000)
			mCamera.Walk(100.0f * dt);
		if (GetAsyncKeyState('S') & 0x8000)
			mCamera.Walk(-100.0f * dt);
		if (GetAsyncKeyState('A') & 0x8000)
			mCamera.Strafe(-100.0f * dt);
		if (GetAsyncKeyState('D') & 0x8000)
			mCamera.Strafe(100.0f * dt);
		if (GetAsyncKeyState('F') & 0x8000)
			mCamera.SetPosition(0.0f, 0.0f, -100.0f);
		static bool Cap = false;
		if (GetAsyncKeyState('P') & 0x8000 && Cap)
		{
			//CaputureBuffer(md3dDevice.Get(), mCommandList.Get(),
			//	mBrdfLutRT2D->mResource.Get(),
			//	4);
			//Cap = false;
		}else Cap = true;

		mCamera.UpdateViewMatrix();
	}

	void SDxRendererGM::UpdateObjectCBs()
	{
		auto CurrObjectCB = mCurrFrameResource->ObjectCB.get();
		for (auto& se : GetRenderScene()->mAllRItems)
		{
			auto e = &se->dxRenderItem;
			// Only update the cbuffer data if the constants have changed.
			// This needs to be tracked per frame resource
			if (e->NumFramesDirty > 0)
			{
				XMMATRIX world = XMLoadFloat4x4(&e->World);
				XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);
				XMMATRIX prevWorld = XMLoadFloat4x4(&e->PrevWorld);

				SRenderMeshConstants objConstants; 
				XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
				XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
				XMStoreFloat4x4(&objConstants.PrevWorld, XMMatrixTranspose(prevWorld));

				CurrObjectCB->CopyData(e->ObjCBIndex, objConstants);

				// Next FrameResource need to be updated too.
				e->NumFramesDirty--;
			}
		}
	}

	void SDxRendererGM::UpdateMaterialCBs()
	{
		auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
		for (auto& e : GetRenderScene()->OpaqueMaterials)
		{
			// Only update the cbuffer data if the constants have changed. If the cbuffer
			// data changes, it needs to be updated for each FrameResource.
			OpaqueMaterial* mat = &e.second->data;
			if (mat->NumFramesDirty > 0)
			{
				PBRMaterialConstants matConstants;
				matConstants = mat->MatConstants;

				currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

				// Next FrameResource need to be updated too.
				mat->NumFramesDirty--;
			}
		}
	}
	// 
	void SDxRendererGM::UpdateMainPassCB()
	{
		static int mFrameCount = 0;
		mFrameCount = (mFrameCount + 1) % TAA_SAMPLE_COUNT;
		XMMATRIX proj = mCamera.GetProj();
		XMMATRIX view = mCamera.GetView();
		mMainPassCB.PrevViewProj = mMainPassCB.UnjitteredViewProj;
		XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
		XMMATRIX viewProj = XMMatrixMultiply(view, proj);
#if defined(Sakura_TAA)
		XMStoreFloat4x4(&mMainPassCB.UnjitteredViewProj, XMMatrixTranspose(viewProj));
		XMMATRIX unj_invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
		XMStoreFloat4x4(&mMainPassCB.UnjiterredInvProj, XMMatrixTranspose(unj_invProj));
		double JitterX = SGraphics::Halton_2[mFrameCount] / (double)mGraphicsConfs->clientWidth * (double)TAA_JITTER_DISTANCE;
		double JitterY = SGraphics::Halton_3[mFrameCount] / (double)mGraphicsConfs->clientHeight * (double)TAA_JITTER_DISTANCE;
		proj.r[2].m128_f32[0] += JitterX;
		proj.r[2].m128_f32[1] += JitterY;
		mMainPassCB.AddOnMsg = mFrameCount;
		mMainPassCB.Jitter.x = JitterX / 2;
		mMainPassCB.Jitter.y = -JitterY / 2;
#endif
		viewProj = XMMatrixMultiply(view, proj);
		XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
		XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

		XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
		XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
		XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
		XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
		XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
		XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
		mMainPassCB.EyePosW = mCamera.GetPosition3f();
		mMainPassCB.RenderTargetSize = XMFLOAT2((float)mGraphicsConfs->clientWidth, (float)mGraphicsConfs->clientHeight);
		mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mGraphicsConfs->clientWidth, 1.0f / mGraphicsConfs->clientHeight);

		mMainPassCB.FarZ = mCamera.GetFarZ();
		mMainPassCB.NearZ = mCamera.GetNearZ();

		mMainPassCB.TotalTime = 1;
		mMainPassCB.DeltaTime = 1;

		mMainPassCB.AmbientLight = { 0.f, 0.f, 0.f, 0.f };
		mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
		mMainPassCB.Lights[0].Strength = { 1.000000000f, 1.000000000f, 0.878431439f };
		mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
		mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
		mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
		mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

		auto currPassCB = mCurrFrameResource->PassCB.get();
		currPassCB->CopyData(0, mMainPassCB);
	}

	void SDxRendererGM::UpdateSsaoPassCB()
	{
		XMMATRIX proj = mCamera.GetProj();

		// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
		XMMATRIX T(
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f);

		mSsaoCB.View = mMainPassCB.View;
		mSsaoCB.Proj = mMainPassCB.Proj;
		mSsaoCB.InvProj = mMainPassCB.UnjiterredInvProj;
		XMStoreFloat4x4(&mSsaoCB.ProjTex, XMMatrixTranspose(proj * T));
		
		SsaoPass* mSsaoPas = (SsaoPass*)(GetFrameGraph()->GetNamedRenderPass(ConsistingPasses::SsaoPassName));
		mSsaoPas->GetOffsetVectors(mSsaoCB.OffsetVectors);
		auto blurWeights = mSsaoPas->CalcGaussWeights(2.5f);

		mSsaoCB.InvRenderTargetSize = XMFLOAT2(1.0f / (mGraphicsConfs->clientWidth), 1.0f / (mGraphicsConfs->clientHeight));

		// Coordinates given in view space.
		mSsaoCB.OcclusionRadius = 0.5f;
		mSsaoCB.OcclusionFadeStart = 0.2f;
		mSsaoCB.OcclusionFadeEnd = 1.0f;
		mSsaoCB.SurfaceEpsilon = 0.05f;

		auto currSsaoCB = mCurrFrameResource->SsaoCB.get();
		currSsaoCB->CopyData(0, mSsaoCB);
	}

	void SDxRendererGM::LoadTextures()
	{
		for (int i = 0; i < Textures::texNames.size(); ++i)
		{
			GetFrameGraph()->
				RegistNamedResourceNode<SD3DTexture>(Textures::texFilenames[i], Textures::texNames[i]);
		}
	}

	void SDxRendererGM::BuildDescriptorHeaps()
	{
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = GBufferResourceSrv;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		((SDxResourceManager*)pGraphicsResourceManager.get())
			->GetOrAllocDescriptorHeap(SRVs::GBufferSrvName, mDeviceInformation->cbvSrvUavDescriptorSize, srvHeapDesc);

		srvHeapDesc.NumDescriptors = 1;
		((SDxResourceManager*)pGraphicsResourceManager.get())
			->GetOrAllocDescriptorHeap(SRVs::CaptureSrvName, mDeviceInformation->cbvSrvUavDescriptorSize, srvHeapDesc);

		srvHeapDesc.NumDescriptors = GBufferRTNum + GBufferSrvStartAt + LUTNum;
		((SDxResourceManager*)pGraphicsResourceManager.get())
			->GetOrAllocDescriptorHeap(SRVs::DeferredSrvName, mDeviceInformation->cbvSrvUavDescriptorSize, srvHeapDesc);

		srvHeapDesc.NumDescriptors = 10;
		((SDxResourceManager*)pGraphicsResourceManager.get())
			->GetOrAllocDescriptorHeap(SRVs::ScreenEfxSrvName, mDeviceInformation->cbvSrvUavDescriptorSize, srvHeapDesc);
	}

	void SDxRendererGM::BindPassResources()
	{
		auto SkyTex = ((SD3DTexture*)(GetFrameGraph()->GetNamedRenderResource("SkyCubeMap")))->Resource;
		GBufferPassResources.resize(4);
		GBufferPassResources[0] = "DiffTex";
		GBufferPassResources[1] = "RoughTex";
		GBufferPassResources[2] = "SpecTex";
		GBufferPassResources[3] = "NormalTex";

		mSkyCubeResource.resize(1);
		mSkyCubeResource[0] = (SkyTex.Get());
	}

	void SDxRendererGM::BuildGeneratedMeshes()
	{
		GeometryGenerator geoGen;
		auto quad = geoGen.CreateQuad(0.f, 0.f, 1.f, 1.f, 0.f);

		std::vector<ScreenQuadVertex> vertices0((UINT)quad.Indices32.size());
		for (int i = 0; i < quad.Vertices.size(); ++i)
		{
			vertices0[i].Pos = quad.Vertices[i].Position;
			vertices0[i].TexC = quad.Vertices[i].TexC;
		}
		std::vector<std::uint16_t> indices;
		indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));
		GetResourceManager()->RegistGeometry("ScreenQuad", "ScreenQuad", vertices0, indices);

		SubmeshGeometry quadSubmesh;
		quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
		quadSubmesh.StartIndexLocation = 0;
		quadSubmesh.BaseVertexLocation = 0;
#if defined(Sakura_GBUFFER_DEBUG) 
		{
			for (int j = 0; j < 6; j++)
			{
				std::string Name = "DebugScreenQuad" + std::to_string(j);
				std::vector<ScreenQuadDebugVertex> vertices(quadSubmesh.IndexCount);
				const UINT vbByteSize = (UINT)vertices.size() * sizeof(ScreenQuadDebugVertex);
				const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);
				for (int i = 0; i < quad.Vertices.size(); ++i)
				{
					vertices[i].Pos = quad.Vertices[i].Position;
					vertices[i].TexC = quad.Vertices[i].TexC;
					vertices[i].Type = j;
				}
				std::vector<std::uint16_t> indices;
				indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));
				GetResourceManager()->RegistGeometry(Name, Name, vertices, indices);
			}
		}
#endif
#if defined(Sakura_IBL)
		{
			auto sphere = geoGen.CreateSphere(0.5f, 120, 120);
			std::vector<StandardVertex> vertices((UINT)sphere.Indices32.size());
			for (int i = 0; i < sphere.Vertices.size(); ++i)
			{
				vertices[i].Pos = sphere.Vertices[i].Position;
				vertices[i].TexC = sphere.Vertices[i].TexC;
				vertices[i].Normal = sphere.Vertices[i].Normal;
				vertices[i].TexC = sphere.Vertices[i].TexC;
				vertices[i].Tangent = sphere.Vertices[i].TangentU;
			}
			std::vector<std::uint16_t> indices;
			indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
			GetResourceManager()->RegistGeometry("SkySphere", "SkySphere", vertices, indices);
		}
#endif
#if defined(Sakura_IBL_HDR_INPUT)
		{
			auto cube = geoGen.CreateBox(1.f, 1.f, 1.f, 1);
			std::vector<StandardVertex> vertices((UINT)cube.Indices32.size());
			for (int i = 0; i < cube.Vertices.size(); ++i)
			{
				vertices[i].Pos = cube.Vertices[i].Position;
				vertices[i].TexC = cube.Vertices[i].TexC;
				vertices[i].Normal = cube.Vertices[i].Normal;
				vertices[i].TexC = cube.Vertices[i].TexC;
				vertices[i].Tangent = cube.Vertices[i].TangentU;
			}
			std::vector<std::uint16_t> indices;
			indices.insert(indices.end(), std::begin(cube.GetIndices16()), std::end(cube.GetIndices16()));
			GetResourceManager()->RegistGeometry("HDRCube", "HDRCube", vertices, indices);
		}
#endif
	}

	void SDxRendererGM::BuildGeometry()
	{
		GetResourceManager()->RegistGeometry("skullGeo", Meshes::CurrPath, ESupportFileForm::ASSIMP_SUPPORTFILE);
		BuildGeneratedMeshes();
	}

	void SDxRendererGM::BuildFrameResources()
	{
		for (int i = 0; i < gNumFrameResources; i++)
		{
			GetRenderScene()->mFrameResources.push_back(std::make_unique<SFrameResource>(md3dDevice.Get(),
				1 + 6 + SkyCubeConvFilterNum * 6, 300, 500));
		}
	}

	void SDxRendererGM::BuildMaterials()
	{
		OpaqueMaterial Mat;
		Mat.Name = "DefaultMat";
		Mat.MatConstants.DiffuseSrvHeapIndex = -1;
		Mat.MatConstants.RMOSrvHeapIndex = -1;
		Mat.MatConstants.SpecularSrvHeapIndex = -1;
		Mat.MatConstants.NormalSrvHeapIndex = -1;
		Mat.MatConstants.BaseColor = XMFLOAT3(Colors::Gray);
		Mat.MatConstants.Roughness = 1.f;
		GetRenderScene()->RegistOpaqueMaterial(Mat);

		Mat.Name = "FlameThrower";
		Mat.MatConstants.DiffuseSrvHeapIndex = 0;
		Mat.MatConstants.RMOSrvHeapIndex = 1;
		Mat.MatConstants.SpecularSrvHeapIndex = 2;
		Mat.MatConstants.NormalSrvHeapIndex = 3;
		Mat.MatConstants.BaseColor = XMFLOAT3(Colors::White);
		Mat.MatConstants.Roughness = 1.f;
		GetRenderScene()->RegistOpaqueMaterial("FlameThrower", Mat);
	}
	
	void SDxRendererGM::BuildRenderItems()
	{
#if defined(Sakura_Full_Effects)
		SDxRenderItem opaqueRitem;
		XMStoreFloat4x4(&opaqueRitem.World, XMMatrixScaling(.2f, .2f, .2f) * 
		XMMatrixTranslation(0.f, -6.f, 0.f) * XMMatrixRotationY(155));
		opaqueRitem.PrevWorld = opaqueRitem.World;
		opaqueRitem.TexTransform = MathHelper::Identity4x4();
		opaqueRitem.Mat = &GetRenderScene()->GetMaterial("DefaultMat")->data;
		opaqueRitem.Geo = GetResourceManager()->GetGeometry("skullGeo");
		opaqueRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		opaqueRitem.IndexCount = opaqueRitem.Geo->DrawArgs["mesh"].IndexCount;
		opaqueRitem.StartIndexLocation = opaqueRitem.Geo->DrawArgs["mesh"].StartIndexLocation;
		opaqueRitem.BaseVertexLocation = opaqueRitem.Geo->DrawArgs["mesh"].BaseVertexLocation;
		//GetRenderScene()->RegistRenderItem(opaqueRitem, E_Opaque);
#endif
		SDxRenderItem screenQuad;
		screenQuad.World = MathHelper::Identity4x4();
		screenQuad.TexTransform = MathHelper::Identity4x4();
		screenQuad.Mat = &GetRenderScene()->GetMaterial("FlameThrower")->data;
		screenQuad.Geo = GetResourceManager()->GetGeometry("ScreenQuad");
		screenQuad.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		screenQuad.IndexCount = screenQuad.Geo->DrawArgs["ScreenQuad"].IndexCount;
		screenQuad.StartIndexLocation = screenQuad.Geo->DrawArgs["ScreenQuad"].StartIndexLocation;
		screenQuad.BaseVertexLocation = screenQuad.Geo->DrawArgs["ScreenQuad"].BaseVertexLocation;
		GetRenderScene()->RegistRenderItem(screenQuad, E_ScreenQuad);
#if defined(Sakura_IBL)
		SDxRenderItem skyRitem;
		XMStoreFloat4x4(&skyRitem.World, XMMatrixScaling(5000.f, 5000.f, 5000.f));
		skyRitem.TexTransform = MathHelper::Identity4x4();
		skyRitem.Mat = &GetRenderScene()->GetMaterial("FlameThrower")->data;
		skyRitem.Geo = GetResourceManager()->GetGeometry("SkySphere");
		skyRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		skyRitem.IndexCount = skyRitem.Geo->DrawArgs["SkySphere"].IndexCount;
		skyRitem.StartIndexLocation = skyRitem.Geo->DrawArgs["SkySphere"].StartIndexLocation;
		skyRitem.BaseVertexLocation = skyRitem.Geo->DrawArgs["SkySphere"].BaseVertexLocation;
		GetRenderScene()->RegistRenderItem(skyRitem, E_SKY);
#endif

#if defined(Sakura_IBL_HDR_INPUT)
		SDxRenderItem boxRitem;
		XMStoreFloat4x4(&boxRitem.World, XMMatrixScaling(1000.f, 1000.f, 1000.f));
		boxRitem.TexTransform = MathHelper::Identity4x4();
		boxRitem.Mat = &GetRenderScene()->GetMaterial("FlameThrower")->data;
		boxRitem.Geo = GetResourceManager()->GetGeometry("HDRCube");
		boxRitem.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		boxRitem.IndexCount = boxRitem.Geo->DrawArgs["HDRCube"].IndexCount;
		boxRitem.StartIndexLocation = boxRitem.Geo->DrawArgs["HDRCube"].StartIndexLocation;
		boxRitem.BaseVertexLocation = boxRitem.Geo->DrawArgs["HDRCube"].BaseVertexLocation;
		GetRenderScene()->RegistRenderItem(boxRitem, E_Cube);
#endif

#if defined(Sakura_GBUFFER_DEBUG) 
		for (int i = 0; i < 6; i++)
		{
			std::string Name = "DebugScreenQuad" + std::to_string(i);
			screenQuad.World = MathHelper::Identity4x4();
			screenQuad.TexTransform = MathHelper::Identity4x4();
			screenQuad.Mat = &GetRenderScene()->GetMaterial("FlameThrower")->data;
			screenQuad.Geo = GetResourceManager()->GetGeometry(Name);
			screenQuad.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			screenQuad.IndexCount = screenQuad.Geo->DrawArgs[Name].IndexCount;
			screenQuad.StartIndexLocation = screenQuad.Geo->DrawArgs[Name].StartIndexLocation;
			screenQuad.BaseVertexLocation = screenQuad.Geo->DrawArgs[Name].BaseVertexLocation;
			GetRenderScene()->RegistRenderItem(screenQuad, E_GBufferDebug);
		}
#endif
	}

	void SDxRendererGM::CreateRtvAndDsvDescriptorHeaps()
	{
		//Create render target view descriptor for swap chain buffers
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
		rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		rtvHeapDesc.NodeMask = 0;
		((SDxResourceManager*)(pGraphicsResourceManager.get()))
			->GetOrAllocDescriptorHeap("DefaultRtv", mDeviceInformation->rtvDescriptorSize, rtvHeapDesc);

		//Create render target view descriptor for swap chain buffers
		rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		rtvHeapDesc.NodeMask = 0;
		((SDxResourceManager*)(pGraphicsResourceManager.get()))
			->GetOrAllocDescriptorHeap("DefaultSrv", mDeviceInformation->cbvSrvUavDescriptorSize, rtvHeapDesc);

		//Create render target view descriptor for swap chain buffers
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = 1;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		((SDxResourceManager*)(pGraphicsResourceManager.get()))
			->GetOrAllocDescriptorHeap(SRVs::ImGuiSrvName, mDeviceInformation->cbvSrvUavDescriptorSize, desc);

		rtvHeapDesc.NumDescriptors =  GBufferRTNum + 
			6 * SkyCubeMips + 6 * SkyCubeConvFilterNum + 100;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		((SDxResourceManager*)(pGraphicsResourceManager.get()))
			->GetOrAllocDescriptorHeap(RTVs::DeferredRtvName, mDeviceInformation->rtvDescriptorSize, rtvHeapDesc);

		rtvHeapDesc.NumDescriptors = 1;
		((SDxResourceManager*)(pGraphicsResourceManager.get()))
			->GetOrAllocDescriptorHeap(RTVs::CaptureRtvName, mDeviceInformation->rtvDescriptorSize, rtvHeapDesc);

		rtvHeapDesc.NumDescriptors = 10;
		((SDxResourceManager*)(pGraphicsResourceManager.get()))
			->GetOrAllocDescriptorHeap(RTVs::ScreenEfxRtvName, mDeviceInformation->rtvDescriptorSize, rtvHeapDesc);

		//Create depth/stencil view descriptor 
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dsvHeapDesc.NodeMask = 0;
		ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
			&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
	}

	void SDxRendererGM::BuildCubeFaceCamera(float x, float y, float z)
	{
		// Generate the cube map about the given position.
		XMFLOAT3 center(x, y, z);
		XMFLOAT3 worldUp(0.0f, 1.0f, 0.0f);
		// Look along each coordinate axis.
		XMFLOAT3 targets[6] =
		{
			XMFLOAT3(x + 1.0f, y, z), // +X
			XMFLOAT3(x - 1.0f, y, z), // -X			
			XMFLOAT3(x, y + 1.0f, z), // +Y
			XMFLOAT3(x, y - 1.0f, z), // -Y
			XMFLOAT3(x, y, z + 1.0f), // +Z
			XMFLOAT3(x, y, z - 1.0f),  // -Z
			
		};
		// Use world up vector (0,1,0) for all directions except +Y/-Y.  In these cases, we
		// are looking down +Y or -Y, so we need a different "up" vector.
		XMFLOAT3 ups[6] =
		{
			XMFLOAT3(0.0f, 1.0f, 0.0f),  // +X
			XMFLOAT3(0.0f, 1.0f, 0.0f),  // -X
			XMFLOAT3(0.0f, 0.0f, -1.0f), // +Y		
			XMFLOAT3(0.0f, 0.0f, +1.0f),  // -Y	
			XMFLOAT3(0.0f, 1.0f, 0.0f),	 // +Z
			XMFLOAT3(0.0f, 1.0f, 0.0f),	 // -Z				
			
		};
		for (int i = 0; i < 6; ++i)
		{
			mCubeMapCamera[i].LookAt(center, targets[i], ups[i]);
			mCubeMapCamera[i].SetLens(0.5f * XM_PI, 1.0f, 0.1f, 20000.0f);
			mCubeMapCamera[i].UpdateViewMatrix();
		}
	}
}