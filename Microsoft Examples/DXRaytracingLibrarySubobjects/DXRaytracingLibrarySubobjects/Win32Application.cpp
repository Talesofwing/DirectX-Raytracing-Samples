#include "stdafx.h"
#include "DXSampleHelper.h"
#include "Win32Application.h"

HWND Win32Application::m_Hwnd = nullptr;
bool Win32Application::m_FullscreenMode = false;
RECT Win32Application::m_WindowRect;

using Microsoft::WRL::ComPtr;

int Win32Application::Run (DXSample* pSample, HINSTANCE hInstance, int nCmdShow) {
	try {
		int argc;
		LPWSTR* argv = CommandLineToArgvW (GetCommandLineW (), &argc);
		pSample->ParseCommandLineArgs (argv, argc);
		LocalFree (argv);

		WNDCLASSEX windowClass = {0};
		windowClass.cbSize = sizeof (WNDCLASSEX);
		windowClass.style = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc = WindowProc;
		windowClass.hInstance = hInstance;
		windowClass.hCursor = LoadCursor (NULL, IDC_ARROW);
		windowClass.lpszClassName = L"DXSampleClass";
		RegisterClassEx (&windowClass);

		RECT windowRect = {0, 0, static_cast<LONG>(pSample->GetWidth ()), static_cast<LONG> (pSample->GetHeight ())};
		AdjustWindowRect (&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

		m_Hwnd = CreateWindow (
			windowClass.lpszClassName,
			pSample->GetTitle (),
			m_WindowStyle,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			windowRect.right - windowRect.left,
			windowRect.bottom - windowRect.top,
			nullptr,
			nullptr,
			hInstance,
			pSample
		);
	
		pSample->OnInit ();

		ShowWindow (m_Hwnd, nCmdShow);

		MSG msg = {};
		while (msg.message != WM_QUIT) {
			if (PeekMessage (&msg, NULL, 0, 0, PM_REMOVE)) {
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}

		pSample->OnDestroy ();

		// Return this part of the WM_QUIT message to Windows.
		return static_cast<char> (msg.wParam);
	} catch (std::exception& e) {
		OutputDebugString (L"Application hit a problem: ");
		OutputDebugStringA (e.what ());
		OutputDebugString (L"\nTerminating.\n");

		pSample->OnDestroy ();
		return EXIT_FAILURE;
	}
}

void Win32Application::ToggleFullscreenWindow (IDXGISwapChain* pSwapChain) {
	if (m_FullscreenMode) {
		// Restore the window's attributes and size.
		SetWindowLong (m_Hwnd, GWL_STYLE, m_WindowStyle);

		SetWindowPos (
			m_Hwnd,
			HWND_NOTOPMOST,
			m_WindowRect.left,
			m_WindowRect.top,
			m_WindowRect.right - m_WindowRect.left,
			m_WindowRect.bottom - m_WindowRect.top,
			SWP_FRAMECHANGED | SWP_NOACTIVATE
		);

		ShowWindow (m_Hwnd, SW_NORMAL);
	} else {
		// Save the old window rect so we can restore it when exiting fullscreen mode.
		GetWindowRect (m_Hwnd, &m_WindowRect);

		// Make the window borderless so that the client area can fill the screen.
		SetWindowLong (m_Hwnd, GWL_STYLE, m_WindowStyle & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME));

		RECT fullscreenWindowRect;
		try {
			if (pSwapChain) {
				// Get the settings of the display on which the app's window is currently displayed
				ComPtr<IDXGIOutput> pOutput;
				ThrowIfFailed (pSwapChain->GetContainingOutput (&pOutput));
				DXGI_OUTPUT_DESC desc;
				ThrowIfFailed (pOutput->GetDesc (&desc));
				fullscreenWindowRect = desc.DesktopCoordinates;
			} else {
				throw HrException (S_FALSE);
			}
		} catch (HrException& e) {
			UNREFERENCED_PARAMETER (e);

			// Get the settings of the primary display
			DEVMODE devMode = {};
			devMode.dmSize = sizeof (DEVMODE);
			EnumDisplaySettings (nullptr, ENUM_CURRENT_SETTINGS, &devMode);

			fullscreenWindowRect = {
				devMode.dmPosition.x,
				devMode.dmPosition.y,
				devMode.dmPosition.x + static_cast<LONG> (devMode.dmPelsWidth),
				devMode.dmPosition.y + static_cast<LONG> (devMode.dmPelsHeight)
			};
		}

		SetWindowPos (
			m_Hwnd,
			HWND_TOPMOST,
			fullscreenWindowRect.left,
			fullscreenWindowRect.top,
			fullscreenWindowRect.right,
			fullscreenWindowRect.bottom,
			SWP_FRAMECHANGED | SWP_NOACTIVATE
		);

		ShowWindow (m_Hwnd, SW_MAXIMIZE);
	}
		
	m_FullscreenMode = !m_FullscreenMode;
}

void Win32Application::SetWindowZorderToTopMost (bool setToTopMost) {
	RECT windowRect;
	GetWindowRect (m_Hwnd, &windowRect);

	SetWindowPos (
		m_Hwnd,
		(setToTopMost) ? HWND_TOPMOST : HWND_NOTOPMOST,
		windowRect.left,
		windowRect.top,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		SWP_FRAMECHANGED | SWP_NOACTIVATE
	);
}

LRESULT CALLBACK Win32Application::WindowProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	DXSample* pSample = reinterpret_cast<DXSample*> (GetWindowLongPtr (hwnd, GWLP_USERDATA));

	switch (message) {
		case WM_CREATE:
		{
			// Save the DXSample* passed in to CreateWindow.
			LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT> (lParam);
			SetWindowLongPtr (hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (pCreateStruct->lpCreateParams));
		}
			return 0;
		case WM_KEYDOWN:
			if (pSample) {
				pSample->OnKeyDown (static_cast<UINT8> (wParam));
			}
			return 0;
		case WM_KEYUP:
			if (wParam == VK_ESCAPE) {
				PostQuitMessage (0);
			} else {
				if (pSample) {
					pSample->OnKeyUp (static_cast<UINT8> (wParam));
				}
			}
			return 0;
		case WM_SYSKEYDOWN:
			// Handle ALT+ENTER:
			if ((wParam == VK_RETURN) && (lParam & (1 << 29))) {
				if (pSample && pSample->GetDeviceResources ()->IsTearingSupported ()) {
					ToggleFullscreenWindow (pSample->GetSwapChain ());
					return 0;
				}
			}
			break;
		case WM_PAINT:
			if (pSample) {
				pSample->OnUpdate ();
				pSample->OnRender ();
			}
			return 0;
		case WM_SIZE : 
			if (pSample) {
				RECT windowRect = {};
				GetWindowRect (hwnd, &windowRect);
				pSample->SetWindowBounds (windowRect.left, windowRect.top, windowRect.right, windowRect.bottom);

				RECT clientRect = {};
				GetClientRect (hwnd, &clientRect);
				pSample->OnSizeChanged (clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, wParam == SIZE_MINIMIZED);
			}
			return 0;
		case WM_MOVE:
			if (pSample) {
				RECT windowRect = {};
				GetWindowRect (hwnd, &windowRect);
				pSample->SetWindowBounds (windowRect.left, windowRect.top, windowRect.right, windowRect.bottom);

				int xPos = (int)(short)LOWORD (lParam);
				int yPos = (int)(short)HIWORD (lParam);
				pSample->OnWindowMoved (xPos, yPos);
			}
			return 0;
		case WM_DISPLAYCHANGE:
			if (pSample) {
				pSample->OnDisplayChanged ();
			}
			return 0;
		case WM_MOUSEMOVE:
			if (pSample && static_cast<UINT8> (wParam) == MK_LBUTTON) {
				UINT x = LOWORD (lParam);
				UINT y = HIWORD (lParam);
				pSample->OnMouseMove (x, y);
			}
			return 0;
		case WM_LBUTTONDOWN:
		{
			UINT x = LOWORD (lParam);
			UINT y = HIWORD (lParam);
			pSample->OnLeftButtonDown (x, y);
		}
			return 0;
		case WM_LBUTTONUP:
		{
			UINT x = LOWORD (lParam);
			UINT y = HIWORD (lParam);
			pSample->OnLeftButtonUp (x, y);
		}
			return 0;
		case WM_DESTROY:
			PostQuitMessage (0);
			return 0;
	}

	// Handle any messages the switch statement didn't
	return DefWindowProc (hwnd, message, wParam, lParam);
}