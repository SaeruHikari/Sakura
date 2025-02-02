#include "SDxResourceManager.h"
#include "Framework/GraphicTypes/D3D12/D3DCommon.h"
#include "Framework/GraphicTypes/D3D12/SDx12RenderTarget.hpp"
#include "Framework/GraphicTypes/D3D12/FrameResource.h"
#include "Framework/GraphicTypes/D3D12/SDx12Pass.hpp"
#include <d3d12.h>

using namespace Microsoft::WRL;
using namespace HikaD3DUtils;

SGraphics::SDxResourceManager::SDxResourceManager(
	Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory,
	Microsoft::WRL::ComPtr<ID3D12Device> d3dDevice,
	std::shared_ptr<SFence> fence,
	std::shared_ptr<SDx12DeviceInformation> deviceInformation,
	std::shared_ptr<SDx12GraphicsStates> graphicsConfs)
	:SakuraGraphicsResourceManagerBase(),
	mdxgiFactory(dxgiFactory), md3dDevice(d3dDevice),
	mFence(fence), mDeviceInformation(deviceInformation),
	mGraphicsConfs(graphicsConfs)
{

};

SGraphics::SDxResourceManager::~SDxResourceManager()
{

}

bool SGraphics::SDxResourceManager::Initialize()
{
	InitD3D12Device();
	CreateCommandObjects();
	return true;
}

void SGraphics::SDxResourceManager::Finalize()
{

}

void SGraphics::SDxResourceManager::Tick(double deltaTime)
{

}

SGraphics::SDescriptorHeap* SGraphics::SDxResourceManager::GetOrAllocDescriptorHeap(std::string name,
	UINT descriptorSize, D3D12_DESCRIPTOR_HEAP_DESC desc)
{
	if (auto descPtr = mDescriptorHeaps[name].get())
	{
		return descPtr;
	}
	else if(descriptorSize > 0)
	{
		desc.NumDescriptors = 1000;
		mDescriptorHeaps[name] = std::make_unique<SDescriptorHeap>(md3dDevice.Get(), descriptorSize, desc);
		return mDescriptorHeaps[name].get();
	}
	return nullptr;
}

SGraphics::ISRenderTarget* SGraphics::SDxResourceManager::CreateNamedRenderTarget(std::string registName,
	ISRenderTargetProperties rtProp, SRHIResource* resource, SResourceHandle srvHandle, SResourceHandle rtvHandle)
{
	switch (rtProp.rtType)
	{
	case ERenderTargetTypes::E_RT2D:
	{
		auto rt2d = std::make_unique<SDx12RenderTarget2D>(rtProp.mWidth, rtProp.mHeight,
			rtProp, rtProp.bScaleWithViewport);
		DescriptorHandleCouple srv;
		srv.hCpu = srvHandle.hCpu;
		srv.hGpu = srvHandle.hGpu;
		DescriptorHandleCouple rtv;
		rtv.hCpu = rtvHandle.hCpu;
		rtv.hGpu = rtvHandle.hGpu;
		rt2d->mSSrv = srv;
		rt2d->mSRtv = rtv;
		mResources[registName] = std::move(rt2d);
		return (ISRenderTarget*)mResources[registName].get();
	}
	case ERenderTargetTypes::E_RT3D:
	{
		return nullptr;
	}
	default:
		return nullptr;
	}
	return nullptr;
}

Dx12MeshGeometry* SGraphics::SDxResourceManager::RegistGeometry(const std::string& GeoName,
	std::string FilePath, HikaD3DUtils::ESupportFileForm FileForm /*= ESupportFileForm::ASSIMP_SUPPORTFILE*/)
{
	ThrowIfFailed(mDirectCmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	mGeometries[GeoName]
		= std::move(MeshImporter::ImportMesh(md3dDevice.Get(), mCommandList.Get(), FilePath, ESupportFileForm::ASSIMP_SUPPORTFILE));
	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsList0[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsList0), cmdsList0);
	FlushCommandQueue();
}

SGraphics::ISRenderTarget* SGraphics::SDxResourceManager::CreateNamedRenderTarget(std::string registName,
	ISRenderTargetProperties rtProp, 
	std::string targetRtvHeap, std::string targetSrvHeap, SRHIResource* resource)
{
	D3D12_DESCRIPTOR_HEAP_DESC rtDesc, srDesc;
	rtDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	srDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	switch (rtProp.rtType)
	{
	case ERenderTargetTypes::E_RT2D:
	{
		auto rt2d = std::make_unique<SDx12RenderTarget2D>(rtProp.mWidth, rtProp.mHeight,
			rtProp, rtProp.bScaleWithViewport);
		rt2d->BuildDescriptors(md3dDevice.Get(),
			GetOrAllocDescriptorHeap(targetRtvHeap, mDeviceInformation->rtvDescriptorSize, rtDesc),
			GetOrAllocDescriptorHeap(targetSrvHeap, mDeviceInformation->cbvSrvUavDescriptorSize, srDesc),
			resource);
		mResources[registName] = std::move(rt2d);
		return (ISRenderTarget*)mResources[registName].get();
	}
	case ERenderTargetTypes::E_RT3D:
	{
		auto rt3d = std::make_unique<SDx12RenderTargetCube>(
			rtProp.mWidth, rtProp.mHeight, rtProp.mRtvFormat);
		rt3d->BuildDescriptors(md3dDevice.Get(),
			GetOrAllocDescriptorHeap(targetRtvHeap, mDeviceInformation->rtvDescriptorSize, rtDesc),
			GetOrAllocDescriptorHeap(targetSrvHeap, mDeviceInformation->cbvSrvUavDescriptorSize, srDesc),
			resource);
		mResources[registName] = std::move(rt3d);
		return (ISRenderTarget*)mResources[registName].get();
	}
	default:
		return nullptr;
	}
	return nullptr;
}

bool SGraphics::SDxResourceManager::InitD3D12Device()
{
#if defined(DEBUG) || defined(_DEBUG)
	// Enable the D3D12 debug layer
	{
		ComPtr<ID3D12Debug> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();
	}
#endif
	// Create DXGI(DirectX Graphics Interface) Factory.
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));
}

void SGraphics::SDxResourceManager::CreateCommandObjects()
{
	// Create command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

	// Create command allocator.
	ThrowIfFailed(md3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));

	// Create command list.
	ThrowIfFailed(md3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		mDirectCmdListAlloc.Get(),
		nullptr,
		IID_PPV_ARGS(mCommandList.GetAddressOf())));

	// Start off in a closed state. This is because the first time we refer
	// to the command list we will reset it, and it needs to be closed before
	// calling rest.
	mCommandList->Close();
}

SGraphics::ISTexture* SGraphics::SDxResourceManager::LoadTexture(std::wstring Filename, std::string texName)
{
	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mDirectCmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	if (Filename.find(L".dds") != std::string::npos)
	{
		auto texMap = std::make_unique<SD3DTexture>();
		texMap->Name = texName;
		texMap->Filename = Filename;
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), texMap->Filename.c_str(),
			texMap->Resource, texMap->UploadHeap));

		// Execute the initialization commands.
		ThrowIfFailed(mCommandList->Close());
		ID3D12CommandList* cmdsList0[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsList0), cmdsList0);
		FlushCommandQueue();
		mResources[texName] = std::move(texMap);
		return (ISTexture*)mResources[texName].get();
	}
	else if (Filename.find(L".hdr") != std::string::npos)
	{
		auto texMap = std::unique_ptr<SD3DTexture>(d3dUtil::LoadHDRTexture(md3dDevice.Get(),
			mCommandList.Get(), texName, Filename));
		// Execute the initialization commands.
		ThrowIfFailed(mCommandList->Close());
		ID3D12CommandList* cmdsList0[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsList0), cmdsList0);
		FlushCommandQueue();
		mResources[texName] = std::move(texMap);
		return (ISTexture*)mResources[texName].get();
	}
	return nullptr;
}

void SGraphics::SDxResourceManager::FlushCommandQueue()
{
	mFence->currentFence++;
	// Add an instruction to the command queue to set a new fence point. Because we 
	// are on the GPU time line, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signare()
	ThrowIfFailed(mCommandQueue->Signal(mFence->fence.Get(), mFence->currentFence));

	// Wait until the GPU has completed commands up to this fence point.
	if (mFence->fence->GetCompletedValue() < mFence->currentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, NULL, false, EVENT_ALL_ACCESS);

		// Fire event when GPU hits current Fence.
		ThrowIfFailed(mFence->fence->SetEventOnCompletion(mFence->currentFence, eventHandle));

		// Wait until the GPU hits current fence event is fired.
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

