#include "SCore.h"
#include <assert.h>
#include <WindowsX.h>
#define DX_MANAGER_VERSION 0
#if (DX_MANAGER_VERSION == 0)
#include "../Managers\Graphics\D3D12\SDxRendererGM.h"
#define ManagerClass SGraphics::SDxRendererGM
#endif
#include <Common/Thirdparty/Imgui/imgui.h>

SakuraCore::SCore* SakuraCore::SCore::mCore = nullptr;
SakuraCore::SCore::SCore(CORE_GRAPHICS_API_CONF gAPI)
{
	CurrSceneMng = std::make_shared<SSceneManager>();
	mTimer = std::make_unique<GameTimer>();
	if (gAPI == CORE_GRAPHICS_API_CONF::SAKURA_DRAW_WITH_D3D12)
	{
		std::unique_ptr<SakuraGraphicsManagerBase> pD3DGraphicsManager = std::make_unique<ManagerClass>();
		mGraphicsManager = std::move(pD3DGraphicsManager);
	}
	else if (gAPI == CORE_GRAPHICS_API_CONF::SAKURA_DRAW_WITH_VULKAN)
	{
		assert(0);
	}
	else
	{
		assert(0);
	}
};


bool SakuraCore::SCore::SakuraInitScene()
{
	CurrScene = std::make_shared<SScene::SakuraScene>();
	CurrScene->Initialize();
	return true;
}

bool SakuraCore::SCore::SakuraInitializeGraphicsCore(HWND hwnd, UINT width, UINT height)
{
	// Debug
	mGraphicsManager->SetHwnd(hwnd);
	if (mGraphicsManager->Initialize())
	{
		mGraphicsManager->OnResize(width, height);
		SakuraInitScene();
	}
	else 
		return false;
	
	return true;
}

int SakuraCore::SCore::Run()
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	try
	{
		MSG msg = { 0 };
		mTimer->Reset();
		bRunning = true;
		while (msg.message != WM_QUIT)
		{
			// If there are Window messages then process them.
			if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				//PeekMessage(&msg, 0, 0, 0, PM_REMOVE);
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			} 
			// Otherwise, do animation/game stuff.
			else
			{
				if (!mAppPaused)
				{
					mTimer->Tick();
					mGraphicsManager->Tick(mTimer->DeltaTime());
					CurrScene->Tick(mTimer->DeltaTime());
					mGraphicsManager->Draw();
				}
				else
				{
					Sleep(100);
				}
			}
		}
	}
	catch (DxException & e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
	}
	return 0;
}

void SakuraCore::SCore::ShutDown()
{
	mAppPaused = true;
	bRunning = false;
	mTimer.reset();
	CurrScene.reset();
	CurrSceneMng.reset();
	::SendMessage(mGraphicsManager->GetHwnd(), WM_CLOSE, 0, 0);
	mGraphicsManager.reset();
	delete this;
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void SakuraCore::SCore::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui::GetCurrentContext() != NULL)
	{
		ImGuiIO& io = ImGui::GetIO();
		ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
	}
	static bool LMDown = false;
	static bool RMDown = false;
	static bool mMinimized = false;
	static bool mMaximized = false;
	static bool mResizing = false;
	switch (msg)
	{
		// WM_ACTIVATE is sent when the window is activated or deactivated.
		// We pause the game when the window is deactivated and unpause it
		// when it becomes active.
	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			mAppPaused = true;
			mTimer->Stop();
		}
		else
		{
			mAppPaused = false;
			mTimer->Start();
		}
		return; 
		// WM_SIZE is sent when the user resizes the window.
	case WM_SIZE:
		if (bRunning)
		{
			if (wParam == SIZE_MINIMIZED)
			{
				mAppPaused = true;
				mMinimized = true;
				mMaximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED)
			{
				mAppPaused = false;
				mMinimized = false;
				mMaximized = true;
				mGraphicsManager->OnResize(LOWORD(lParam), HIWORD(lParam));
			}
			else if (wParam == SIZE_RESTORED)
			{

				// Restoring from minimized state?
				if (mMinimized)
				{
					mAppPaused = false;
					mMinimized = false;
					mGraphicsManager->OnResize(LOWORD(lParam), HIWORD(lParam));
				}

				// Restoring from maximized state?
				else if (mMaximized)
				{
					mAppPaused = false;
					mMaximized = false;
					mGraphicsManager->OnResize(LOWORD(lParam), HIWORD(lParam));
				}
				else if (mResizing)
				{
					// If user is dragging the resize bars, we do not resize
					// the buffers here because as the user continuously
					// drags the resize bars, a stream of WM_SIZE messages are
					// sent to the window, and it would be pointless (and slow)
					// to resize for each WM_SIZE message received from dragging
					// the resize bars.  So instead, we reset after the user is
					// done resizing the window and releases the resize bars, which
					// sends a WM_EXITSIZEMOVE message.
				}
				else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				{
					mGraphicsManager->OnResize(LOWORD(lParam), HIWORD(lParam));
				}
			}
		}
		return; 
		// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
		if (bRunning)
		{
			mAppPaused = true;
			mResizing = true;
			mTimer->Stop();
		}
		return;
		// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
		// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
		if (bRunning)
		{
			mAppPaused = false;
			mResizing = false;
			mTimer->Start();
			mGraphicsManager->OnResize(LOWORD(lParam), HIWORD(lParam));
		}
		return; 
		// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
		PostQuitMessage(0);
		return;
		// The WM_MENUCHAR message is sent when a menu is active and the user presses
		// a key that does not correspond to any mnemonic or accelerator key.
	case WM_MENUCHAR:
		// Don't beep when we alt-enter.
		return; 
		// Catch this message so to prevent the window from becoming too small.
	case WM_GETMINMAXINFO:
		((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
		((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
		return; 
	case WM_LBUTTONDOWN:
		if (bRunning)
		{
			LMDown = true;
		}
		return;
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		if (bRunning)
		{
			mGraphicsManager->OnMouseDown(SAKURA_INPUT_MOUSE_RBUTTON, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			RMDown = true;
		}
		return;
	case WM_LBUTTONUP:
		if (bRunning)
		{
			LMDown = false;
		}
		return;
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		if (bRunning)
		{
			mGraphicsManager->OnMouseUp(SAKURA_INPUT_MOUSE_RBUTTON, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			RMDown = false;
		}
		return; 
	case WM_MOUSEMOVE:
		if (bRunning)
		{
			if((GetAsyncKeyState(VK_RBUTTON) & 0x8000))
				mGraphicsManager->OnMouseMove(SAKURA_INPUT_MOUSE_RBUTTON, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			else if((GetAsyncKeyState(VK_LBUTTON) & 0x8000))
				mGraphicsManager->OnMouseMove(SAKURA_INPUT_MOUSE_LBUTTON, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		}
		return; 
	case WM_KEYUP:
	case WM_PAINT:
	case WM_ERASEBKGND:
		return;
	default:
		return;
	}
}

void SakuraCore::SCore::TickSakuraCore(double deltaTime, UINT mask)
{
	if ((mask & SAKURA_CORE_COMPONENT_MASK_GRAPHICS) != 0)
	{
		CurrScene->Tick(deltaTime);
		mGraphicsManager->Tick(deltaTime);
		// Current simply calls draw.
		mGraphicsManager->Draw();
	}
}

void SakuraCore::SCore::MsgSakuraGraphicsCore(UINT Msg, UINT param0, UINT param1, UINT param2)
{
	if ((Msg & SAKURA_GRAPHICS_CORE_MSG_RESIZE) != 0)
	{
		mGraphicsManager->OnResize(param1, param2);
	}
	if ((Msg & SAKURA_GRAPHICS_CORE_MSG_MOUSEPRESS) != 0)
	{
		mGraphicsManager->OnMouseDown((SAKURA_INPUT_MOUSE_TYPES)param0, param0, param1);
	}
	if ((Msg & SAKURA_GRAPHICS_CORE_MSG_MOUSERELEASE) != 0)
	{
		mGraphicsManager->OnMouseUp((SAKURA_INPUT_MOUSE_TYPES)param0, param1, param2);
	}
	if ((Msg & SAKURA_GRAPHICS_CORE_MSG_MOUSEMOVE) != 0)
	{
		mGraphicsManager->OnMouseMove((SAKURA_INPUT_MOUSE_TYPES)param0, param1, param2);
	}
	if ((Msg & SAKURA_GRAPHICS_CORE_MSG_KEYPRESS) != 0)
	{
		mGraphicsManager->OnKeyDown((SAKURA_INPUT_KEY)param0);
	}
}

SScene::SakuraSceneNode* SakuraCore::SCore::GetNode(SakuraSceneNode* parent, UINT Index)
{
	if (parent == nullptr)
		return CurrScene->GetRoot();
	else
	{
		return parent->GetChild(Index);
	}
}

