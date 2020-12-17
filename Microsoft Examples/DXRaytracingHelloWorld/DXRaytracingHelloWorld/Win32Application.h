#pragma once

#include "DXSample.h"

class DXSample;

class Win32Application {
public:
	static int Run (DXSample* pSample, HINSTANCE hInstance, int nCmdShow);
	static void ToggleFullscreenWindow (IDXGISwapChain* pOutput = nullptr);
	static void SetWindowZorderToTopMost (bool setToTopMost);
	static HWND GetHwnd () { return m_Hwnd; }
	static bool IsFullscreen () { return m_FullscreenMode; }

protected:
	static LRESULT CALLBACK WindowProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
	static HWND m_Hwnd;
	static bool m_FullscreenMode;
	static const UINT m_WindowStyle = WS_OVERLAPPEDWINDOW;
	static RECT m_WindowRect;
};

