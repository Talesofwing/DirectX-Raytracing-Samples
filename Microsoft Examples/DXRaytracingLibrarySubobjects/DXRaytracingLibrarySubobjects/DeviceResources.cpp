#include "stdafx.h"
#include "DeviceResources.h"
#include "Win32Application.h"

using namespace DX;
using namespace std;

using Microsoft::WRL::ComPtr;

namespace {
    inline DXGI_FORMAT NoSRGB (DXGI_FORMAT fmt) {
        switch (fmt) {
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8X8_UNORM;
            default:                                return fmt;
        }
    }
};

DeviceResources::DeviceResources (DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthBufferFormat, UINT backBufferCount, D3D_FEATURE_LEVEL minFeatureLevel, UINT flags, UINT adapterIDoverride) :
    m_BackBufferIndex (0),
    m_FenceValues {},
    m_RtvDescriptorSize (0),
    m_ScreenViewport {},
    m_ScissorRect {},
    m_BackBufferFormat (backBufferFormat),
    m_DepthBufferFormat (depthBufferFormat),
    m_BackBufferCount (backBufferCount),
    m_MinFeatureLevel (minFeatureLevel),
    m_Window (nullptr),
    m_FeatureLevel (D3D_FEATURE_LEVEL_11_0),
    m_OutputSize {0, 0, 1, 1},
    m_Options (flags),
    m_DeviceNotify (nullptr),
    m_IsWindowVisible (true),
    m_AdapterIDoverride (adapterIDoverride),
    m_AdapterID (UINT_MAX)
{
    if (backBufferCount > MAX_BACK_BUFFER_COUNT) {
        throw out_of_range ("backBufferCount too large");
    }

    if (minFeatureLevel < D3D_FEATURE_LEVEL_11_0) {
        throw out_of_range ("minFeatureLevel too low");
    }

    if (m_Options & c_RequireTearingSupport) {
        m_Options |= c_AllowTearing;
    }
}

DeviceResources::~DeviceResources () {
    WaitForGpu ();
}

void DeviceResources::InitializeDXGIAdapter () {
    bool debugDXGI = false;
#if defined (_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED (D3D12GetDebugInterface (IID_PPV_ARGS (&debugController)))) {
            debugController->EnableDebugLayer ();
        } else {
            OutputDebugStringA ("WARNING: Direct3D Debug Device is not available\n");
        }

        ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
        if (SUCCEEDED (DXGIGetDebugInterface1 (0, IID_PPV_ARGS (&dxgiInfoQueue)))) {
            debugDXGI = true;

            ThrowIfFailed (CreateDXGIFactory2 (DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS (&m_DxgiFactory)));

            dxgiInfoQueue->SetBreakOnSeverity (DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
            dxgiInfoQueue->SetBreakOnSeverity (DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
        }
    }
#endif

    if (!debugDXGI) {
        ThrowIfFailed (CreateDXGIFactory1 (IID_PPV_ARGS (&m_DxgiFactory)));
    }

    if (m_Options & (c_AllowTearing | c_RequireTearingSupport)) {
        BOOL allowTearing = FALSE;

        ComPtr<IDXGIFactory5> factory5;
        HRESULT hr = m_DxgiFactory.As (&factory5);
        if (SUCCEEDED (hr)) {
            hr = factory5->CheckFeatureSupport (DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof (allowTearing));
        }

        if (FAILED (hr) || !allowTearing) {
            OutputDebugStringA ("WARNING: Variable refresh rate displays are not supported.\n");
            if (m_Options & c_RequireTearingSupport) {
                ThrowIfFailed (false, L"Error: Sample must be run on an OS with tearing support.\n");
            }
            m_Options &= ~c_AllowTearing;
        }
    }

    InitializeAdapter (&m_Adapter);
}

void DeviceResources::CreateDeviceResources () {
    ThrowIfFailed (D3D12CreateDevice (m_Adapter.Get (), m_MinFeatureLevel, IID_PPV_ARGS (&m_Device)));

#ifndef NDEBUG
    // Configure debug device (if active).
    ComPtr<ID3D12InfoQueue> d3dInfoQueue;
    if (SUCCEEDED (m_Device.As (&d3dInfoQueue))) {
#ifdef _DEBUG
#endif
        d3dInfoQueue->SetBreakOnSeverity (D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        d3dInfoQueue->SetBreakOnSeverity (D3D12_MESSAGE_SEVERITY_ERROR, true);
#endif
        D3D12_MESSAGE_ID hide[] =
        {
            D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
            D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE
        };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumIDs = _countof (hide);
        filter.DenyList.pIDList = hide;
        d3dInfoQueue->AddStorageFilterEntries (&filter);
    }

    static const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D12_FEATURE_DATA_FEATURE_LEVELS featLevels = {
        _countof (featureLevels), featureLevels, D3D_FEATURE_LEVEL_11_0
    };

    HRESULT hr = m_Device->CheckFeatureSupport (D3D12_FEATURE_FEATURE_LEVELS, &featLevels, sizeof (featLevels));
    if (SUCCEEDED (hr)) {
        m_FeatureLevel = featLevels.MaxSupportedFeatureLevel;
    } else {
        m_FeatureLevel = m_MinFeatureLevel;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed (m_Device->CreateCommandQueue (&queueDesc, IID_PPV_ARGS (&m_CommandQueue)));

    D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
    rtvDescriptorHeapDesc.NumDescriptors = m_BackBufferCount;
    rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    ThrowIfFailed (m_Device->CreateDescriptorHeap (&rtvDescriptorHeapDesc, IID_PPV_ARGS (&m_RtvDescriptorHeap)));

    if (m_DepthBufferFormat != DXGI_FORMAT_UNKNOWN) {
        D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc = {};
        dsvDescriptorHeapDesc.NumDescriptors = 1;
        dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

        ThrowIfFailed (m_Device->CreateDescriptorHeap (&dsvDescriptorHeapDesc, IID_PPV_ARGS (&m_DsvDescriptorHeap)));
    }

    for (UINT n = 0; n < m_BackBufferCount; n++) {
        ThrowIfFailed (m_Device->CreateCommandAllocator (D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS (&m_CommandAllocators[n])));
    }

    ThrowIfFailed (m_Device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocators[0].Get (), nullptr, IID_PPV_ARGS (&m_CommandList)));
    ThrowIfFailed (m_CommandList->Close ());

    ThrowIfFailed (m_Device->CreateFence (m_FenceValues[m_BackBufferIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS (&m_Fence)));
    m_FenceValues[m_BackBufferIndex]++;

    m_FenceEvent.Attach (CreateEvent (nullptr, FALSE, FALSE, nullptr));
    if (!m_FenceEvent.IsValid ()) {
        ThrowIfFailed (E_FAIL, L"CreateEvent failed.\n");
    }
}

void DeviceResources::CreateWindowSizeDependentResources () {
    if (!m_Window) {
        ThrowIfFailed (E_HANDLE, L"Call SetWindow with a vaild Win32 window handle.\n");
    }

    WaitForGpu ();

    for (UINT n = 0; n < m_BackBufferCount; n++) {
        m_RenderTargets[n].Reset ();
        m_FenceValues[n] = m_FenceValues[m_BackBufferIndex];
    }

    UINT backBufferWidth = max (m_OutputSize.right - m_OutputSize.left, 1);
    UINT backBufferHeight = max (m_OutputSize.bottom - m_OutputSize.top, 1);
    DXGI_FORMAT backBufferFormat = NoSRGB (m_BackBufferFormat);
    
    if (m_SwapChain) {
        HRESULT hr = m_SwapChain->ResizeBuffers (
            m_BackBufferCount,
            backBufferWidth,
            backBufferHeight,
            backBufferFormat,
            (m_Options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0
        );

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        #ifdef _DEBUG
            char buff[64] = {};
            sprintf_s (buff, "Device Lost on ResizeBuffers: Reason code 0x%08X\n", (hr == DXGI_ERROR_DEVICE_REMOVED) ? m_Device->GetDeviceRemovedReason () : hr);
            OutputDebugStringA (buff);
        #endif

            HandleDeviceLost ();

            return;
        } else {
            ThrowIfFailed (hr);
        }
    } else {
        // Create a descriptor for the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = backBufferWidth;
        swapChainDesc.Height = backBufferHeight;
        swapChainDesc.Format = backBufferFormat;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = m_BackBufferCount;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        swapChainDesc.Flags = (m_Options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {0};
        fsSwapChainDesc.Windowed = TRUE;

        // Create a swap chain for the window.
        ComPtr<IDXGISwapChain1> swapChain;

        bool prevIsFullscreen = Win32Application::IsFullscreen ();
        if (prevIsFullscreen) {
            Win32Application::SetWindowZorderToTopMost (false);
        }
    
        ThrowIfFailed (m_DxgiFactory->CreateSwapChainForHwnd (m_CommandQueue.Get (), m_Window, &swapChainDesc, &fsSwapChainDesc, nullptr, &swapChain));

        if (prevIsFullscreen) {
            Win32Application::SetWindowZorderToTopMost (true);
        }

        ThrowIfFailed (swapChain.As (&m_SwapChain));
    
        if (IsTearingSupported ()) {
            m_DxgiFactory->MakeWindowAssociation (m_Window, DXGI_MWA_NO_ALT_ENTER);
        }
    }   

    for (UINT n = 0; n < m_BackBufferCount; n++) {
        ThrowIfFailed (m_SwapChain->GetBuffer (n, IID_PPV_ARGS (&m_RenderTargets[n])));

        wchar_t name[25] = {};
        swprintf_s (name, L"Render target %u", n);
        m_RenderTargets[n]->SetName (name);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = m_BackBufferFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor (m_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart (), n, m_RtvDescriptorSize);
        m_Device->CreateRenderTargetView (m_RenderTargets[n].Get (), &rtvDesc, rtvDescriptor);
    }
    
    m_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex ();

    if (m_DepthBufferFormat != DXGI_FORMAT_UNKNOWN) {
        CD3DX12_HEAP_PROPERTIES depthHeapProperties (D3D12_HEAP_TYPE_DEFAULT);

        D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D (
            m_DepthBufferFormat,
            backBufferWidth,
            backBufferHeight,
            1,
            1
        );
        depthStencilDesc.Flags != D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
        depthOptimizedClearValue.Format = m_DepthBufferFormat;
        depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
        depthOptimizedClearValue.DepthStencil.Stencil = 0.0f;

        ThrowIfFailed (m_Device->CreateCommittedResource (&depthHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &depthStencilDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &depthOptimizedClearValue,
            IID_PPV_ARGS (&m_DepthStencil)
        ));
    
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = m_DepthBufferFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        m_Device->CreateDepthStencilView (m_DepthStencil.Get (), &dsvDesc, m_DsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart ());
    }

    m_ScreenViewport.TopLeftX = m_ScreenViewport.TopLeftY = 0.0f;
    m_ScreenViewport.Width = static_cast<float> (backBufferWidth);
    m_ScreenViewport.Height = static_cast<float> (backBufferHeight);
    m_ScreenViewport.MinDepth = D3D12_MIN_DEPTH;
    m_ScreenViewport.MaxDepth = D3D12_MAX_DEPTH;

    m_ScissorRect.left = m_ScissorRect.top = 0;
    m_ScissorRect.right = backBufferWidth;
    m_ScissorRect.bottom = backBufferHeight;
}

void DeviceResources::SetWindow (HWND window, int width, int height) {
    m_Window = window;

    m_OutputSize.left = m_OutputSize.top = 0;
    m_OutputSize.right = width;
    m_OutputSize.bottom = height;
}

bool DeviceResources::WindowSizeChanged (int width, int height, bool minimized) {
    m_IsWindowVisible = !minimized;

    if (minimized || width == 0 || height == 0) {
        return false;
    }

    RECT newRc;
    newRc.left = newRc.top = 0;
    newRc.right = width;
    newRc.bottom = height;
    if (newRc.left == m_OutputSize.left &&
        newRc.top == m_OutputSize.top &&
        newRc.right == m_OutputSize.right &&
        newRc.bottom == m_OutputSize.bottom) {
        return false;
    }

    m_OutputSize = newRc;
    CreateWindowSizeDependentResources ();
    return true;
}

void DeviceResources::HandleDeviceLost () {
    if (m_DeviceNotify) {
        m_DeviceNotify->OnDeviceLost ();
    }

    for (UINT n = 0; n < m_BackBufferCount; n++) {
        m_CommandAllocators[n].Reset ();
        m_RenderTargets[n].Reset ();
    }

    m_DepthStencil.Reset ();
    m_CommandQueue.Reset ();
    m_CommandList.Reset ();
    m_Fence.Reset ();
    m_RtvDescriptorHeap.Reset ();
    m_DsvDescriptorHeap.Reset ();
    m_SwapChain.Reset ();
    m_Device.Reset ();
    m_DxgiFactory.Reset ();
    m_Adapter.Reset ();

#ifdef _DEBUG
    {
        ComPtr<IDXGIDebug1> dxgiDebug;
        if (SUCCEEDED (DXGIGetDebugInterface1 (0, IID_PPV_ARGS (&dxgiDebug)))) {
            dxgiDebug->ReportLiveObjects (DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS (DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
        }
    }
#endif
    InitializeDXGIAdapter ();
    CreateDeviceResources ();
    CreateWindowSizeDependentResources ();

    if (m_DeviceNotify) {
        m_DeviceNotify->OnDeviceRestored ();
    }
}

void DeviceResources::Prepare (D3D12_RESOURCE_STATES beforeState) {
    ThrowIfFailed (m_CommandAllocators[m_BackBufferIndex]->Reset ());
    ThrowIfFailed (m_CommandList->Reset (m_CommandAllocators[m_BackBufferIndex].Get (), nullptr));

    if (beforeState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition (m_RenderTargets[m_BackBufferIndex].Get (), beforeState, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_CommandList->ResourceBarrier (1, &barrier);
    }
}

void DeviceResources::Present (D3D12_RESOURCE_STATES beforeState) {
    if (beforeState != D3D12_RESOURCE_STATE_PRESENT) {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition (m_RenderTargets[m_BackBufferIndex].Get (), beforeState, D3D12_RESOURCE_STATE_PRESENT);
        m_CommandList->ResourceBarrier (1, &barrier);
    }

    ExecuteCommandList ();

    HRESULT hr;
    if (m_Options & c_AllowTearing) {
        hr = m_SwapChain->Present (0, DXGI_PRESENT_ALLOW_TEARING);
    } else {
        hr = m_SwapChain->Present (1, 0);
    }

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    #ifdef _DEBUG
        char buff[64] = {};
        sprintf_s (buff, "Device Lost on Present: Reason code 0x%08x\n", (hr == DXGI_ERROR_DEVICE_REMOVED) ? m_Device->GetDeviceRemovedReason () : hr);
        OutputDebugStringA (buff);
    #endif
        HandleDeviceLost ();
    } else {
        ThrowIfFailed (hr);

        MoveToNextFrame ();
    }
}

void DeviceResources::ExecuteCommandList () {
    ThrowIfFailed (m_CommandList->Close ());
    ID3D12CommandList* commandLists[] = {m_CommandList.Get ()};
    m_CommandQueue->ExecuteCommandLists (ARRAYSIZE (commandLists), commandLists);
}

void DeviceResources::WaitForGpu () noexcept {
    if (m_CommandQueue && m_Fence && m_FenceEvent.IsValid ()) {
        UINT64 fenceValue = m_FenceValues[m_BackBufferIndex];
        if (SUCCEEDED (m_CommandQueue->Signal (m_Fence.Get (), fenceValue))) {
            if (SUCCEEDED (m_Fence->SetEventOnCompletion (fenceValue, m_FenceEvent.Get ()))) {
                WaitForSingleObjectEx (m_FenceEvent.Get (), INFINITE, FALSE);

                m_FenceValues[m_BackBufferIndex]++;
            }
        }
    }
}

void DeviceResources::MoveToNextFrame () {
    const UINT64 currentFenceValue = m_FenceValues[m_BackBufferIndex];
    ThrowIfFailed (m_CommandQueue->Signal (m_Fence.Get (), currentFenceValue));

    m_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex ();

    if (m_Fence->GetCompletedValue () < m_FenceValues[m_BackBufferIndex]) {
        ThrowIfFailed (m_Fence->SetEventOnCompletion (m_FenceValues[m_BackBufferIndex], m_FenceEvent.Get ()));
        WaitForSingleObjectEx (m_FenceEvent.Get (), INFINITE, FALSE);
    }

    m_FenceValues[m_BackBufferIndex] = currentFenceValue + 1;
}

void DeviceResources::InitializeAdapter (IDXGIAdapter1** ppAdapter) {
    *ppAdapter = nullptr;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT adapterID = 0; DXGI_ERROR_NOT_FOUND != m_DxgiFactory->EnumAdapters1 (adapterID, &adapter); ++adapterID) {
        if (m_AdapterIDoverride != UINT_MAX && adapterID != m_AdapterIDoverride) {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc;
        ThrowIfFailed (adapter->GetDesc1 (&desc));

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            // Don't select the Basic Render Driver adapter.
            continue;
        }

        if (SUCCEEDED (D3D12CreateDevice (adapter.Get (), m_MinFeatureLevel, _uuidof (ID3D12Device), nullptr))) {
            m_AdapterID = adapterID;
            m_AdapterDescription = desc.Description;
        #ifdef _DEBUG
            wchar_t buff[256] = {};
            swprintf_s (buff, L"Direct3D Adapter (%u): VID:%04X, PID:%04X - %ls\n", adapterID, desc.VendorId, desc.DeviceId, desc.Description);
            OutputDebugStringW (buff);
        #endif
            break;
        }
    }

#if !defined (NDEBUG)
    if (!adapter && m_AdapterIDoverride == UINT_MAX) {
        // Try WARP12 instead
        if (FAILED (m_DxgiFactory->EnumWarpAdapter (IID_PPV_ARGS (&adapter)))) {
            throw exception ("WARP12 not available. Enable the 'Graphics Tools' optional feature");
        }

        OutputDebugStringA ("Direct3D Adapter - WARP12\n");
    }
#endif

    if (!adapter) {
        if (m_AdapterIDoverride != UINT_MAX) {
            throw exception ("Unavailable adapter requested.");
        } else {
            throw exception ("Unavailable adapter.");
        }
    }

    *ppAdapter = adapter.Detach ();
}