#include "stdafx.h"
#include "DXSample.h"

using namespace Microsoft::WRL;
using namespace std;

DXSample::DXSample (UINT width, UINT height, std::wstring name) : 
    m_Width (width),
    m_Height (height),
    m_WindowBounds {0, 0, 0, 0},
    m_Title (name),
    m_AspectRatio (0.0f),
    m_EnableUI (true),
    m_AdapterIDoverride (UINT_MAX)
{
    WCHAR assetsPath[512];
    GetAssetsPath (assetsPath, _countof (assetsPath));
    m_AssetsPath = assetsPath;

    UpdateForSizeChange (width, height);
}
DXSample::~DXSample () {}

void DXSample::UpdateForSizeChange (UINT clientWidth, UINT clientHeight) {
    m_Width = clientWidth;
    m_Height = clientHeight;
    m_AspectRatio = static_cast<float> (clientWidth) / static_cast<float> (clientHeight);
}

std::wstring DXSample::GetAssetFullPath (LPCWSTR aseetName) {
    return m_AssetsPath + aseetName;
}

void DXSample::SetCustomWindowText (LPCWSTR text) {
    std::wstring windowText = m_Title + L": " + text;
    SetWindowText (Win32Application::GetHwnd (), windowText.c_str ());
}

_Use_decl_annotations_
void DXSample::ParseCommandLineArgs (WCHAR* argv[], int argc) {
    for (int i = 1; i < argc; ++i) {
    // -disableUI
        if (_wcsnicmp (argv[i], L"-disableUI", wcslen (argv[i])) == 0 ||
            _wcsnicmp (argv[i], L"/disableUI", wcslen (argv[i])) == 0) {
            m_EnableUI = false;
        }
        // -forceAdapter [id]
        else if (_wcsnicmp (argv[i], L"-forceAdapter", wcslen (argv[i])) == 0 ||
            _wcsnicmp (argv[i], L"/forceAdapter", wcslen (argv[i])) == 0) {
            ThrowIfFalse (i + 1 < argc, L"Incorrect argument format passed in.");

            m_AdapterIDoverride = _wtoi (argv[i + 1]);
            i++;
        }
    }
}

void DXSample::SetWindowBounds (int left, int top, int right, int bottom) {
    m_WindowBounds.left = static_cast<LONG>(left);
    m_WindowBounds.top = static_cast<LONG>(top);
    m_WindowBounds.right = static_cast<LONG>(right);
    m_WindowBounds.bottom = static_cast<LONG>(bottom);
}