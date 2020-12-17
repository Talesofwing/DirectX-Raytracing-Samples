#pragma once

#include "DXSampleHelper.h"
#include "Win32Application.h"
#include "DeviceResources.h"

class DXSample : public DX::IDeviceNotify {
public:
	DXSample (UINT width, UINT height, std::wstring name);
	virtual ~DXSample ();

	virtual void OnInit () = 0;
	virtual void OnUpdate () = 0;
	virtual void OnRender () = 0;
	virtual void OnSizeChanged (UINT width, UINT height, bool minimized) = 0;
	virtual void OnDestroy () = 0;

	virtual void OnKeyDown (UINT8) {}
	virtual void OnKeyUp (UINT8) {}
	virtual void OnWindowMoved (int, int) {}
	virtual void OnMouseMove (UINT, UINT) {}
	virtual void OnLeftButtonDown (UINT, UINT) {}
	virtual void OnLeftButtonUp (UINT, UINT) {}
	virtual void OnDisplayChanged () {}

	virtual void ParseCommandLineArgs (_In_reads_ (argc) WCHAR* argv[], int argc);

	UINT GetWidth () const { return m_Width; }
	UINT GetHeight () const { return m_Height; }
	const WCHAR* GetTitle () const { return m_Title.c_str (); }
	RECT GetWindowsBounds () const { return m_WindowBounds; }
	virtual IDXGISwapChain* GetSwapChain () { return nullptr; }
	DX::DeviceResources* GetDeviceResources () const { return m_DeviceResources.get (); }

	void UpdateForSizeChange (UINT clientWidth, UINT clientHeight);
	void SetWindowBounds (int left, int top, int right, int bottom);
	std::wstring GetAssetFullPath (LPCWSTR assetName);

protected:
	void SetCustomWindowText (LPCWSTR text);

	UINT m_Width;
	UINT m_Height;
	float m_AspectRatio;

	RECT m_WindowBounds;

	bool m_EnableUI;

	UINT m_AdapterIDoverride;
	std::unique_ptr<DX::DeviceResources> m_DeviceResources;

private:
	std::wstring m_AssetsPath;
	std::wstring m_Title;
};

