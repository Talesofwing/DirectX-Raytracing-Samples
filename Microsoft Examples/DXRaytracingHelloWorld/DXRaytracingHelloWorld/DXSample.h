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

	// Samples override the event handlers to handle specific messages.
	virtual void OnKeyDown (UINT8 /*key*/) {}
	virtual void OnKeyUp (UINT8 /*key*/) {}
	virtual void OnWindowMoved (int /*x*/, int /*y*/) {}
	virtual void OnMouseMove (UINT /*x*/, UINT /*y*/) {}
	virtual void OnLeftButtonDown (UINT /*x*/, UINT /*y*/) {}
	virtual void OnLeftButtonUp (UINT /*x*/, UINT /*y*/) {}
	virtual void OnDisplayChanged () {}

	// Overridable members.
	virtual void ParseCommandLineArgs (_In_reads_ (argc) WCHAR* argv[], int argc);
	
	// Accessors.
	UINT GetWidth () const { return m_Width; }
	UINT GetHeight () const { return m_Height; }
	const WCHAR* GetTitle () const { return m_Title.c_str (); }
	RECT GetWindowBounds () const { return m_WindowBounds; }
	virtual IDXGISwapChain* GetSwapChain () { return nullptr; }
	DX::DeviceResources* GetDeviceResources () const { return m_DeviceResources.get (); }

	void UpdateForSizeChange (UINT clientWidth, UINT clientHeight);
	void SetWindowBounds (int left, int top, int right, int bottom);
	std::wstring GetAssetFullPath (LPCWSTR assetName);

protected:
	void SetCustomWindowText (LPCWSTR text);

	// Viewport dimensions.
	UINT m_Width;
	UINT m_Height;
	float m_AspectRatio;

	// Window bounds
	RECT m_WindowBounds;

	// Override to be able to start without Dx11on12 UI for PIX. PIX doesn't support 11 on 12.
	bool m_EnableUI;

	// D3D device resources
	UINT m_AdapterIDoverride;
	std::unique_ptr<DX::DeviceResources> m_DeviceResources;

private:
	// Root assets path.
	std::wstring m_AssetsPath;

	// Window title.
	std::wstring m_Title;
};

