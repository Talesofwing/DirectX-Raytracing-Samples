#pragma once

namespace DX {

    interface IDeviceNotify {
        virtual void OnDeviceLost () = 0;
        virtual void OnDeviceRestored () = 0;
    };

    class DeviceResources {
    private:
        DeviceResources () {}
    public:
        static const unsigned int c_AllowTearing = 0x1;
        static const unsigned int c_RequireTearingSupport = 0x2;

        DeviceResources (DXGI_FORMAT backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D32_FLOAT,
            UINT backBufferCount = 2,
            D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_11_0,
            UINT flags = 0,
            UINT adapterIDoverride = UINT_MAX);
        ~DeviceResources ();

        void InitializeDXGIAdapter ();
        void SetAdapterOverride (UINT adapterID) { m_AdapterIDoverride = adapterID; }
        void CreateDeviceResources ();
        void CreateWindowSizeDependentResources ();
        void SetWindow (HWND window, int width, int height);
        bool WindowSizeChanged (int width, int height, bool minimized);
        void HandleDeviceLost ();
        void RegisterDeviceNotify (IDeviceNotify* deviceNotify) {
            m_DeviceNotify = deviceNotify;

            __if_exists(DXGIDeclareAdapterRemovalSupport) {
                if (deviceNotify) {
                    if (FAILED (DXGIDeclareAdapterRemovalSupport ())) {
                        OutputDebugString (L"Warning: application failed to declare adapter removal support.\n");
                    }
                }
            }
        }

        void Prepare (D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_PRESENT);
        void Present (D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_RENDER_TARGET);
        void ExecuteCommandList ();
        void WaitForGpu () noexcept;

        // Device Accessors.
        RECT GetOutputSize () const { return m_OutputSize; }
        bool IsWindowVisible () const { return m_IsWindowVisible; }
        bool IsTearingSupported () const { return m_Options & c_AllowTearing; }

        // Direct3D Accessors.
        IDXGIAdapter1* GetAdapter () const { return m_Adapter.Get (); }
        ID3D12Device* GetD3DDevice () const { return m_Device.Get (); }
        IDXGIFactory4* GetDXGIFactory () const { return m_DxgiFactory.Get (); }
        IDXGISwapChain3* GetSwapChain () const { return m_SwapChain.Get (); }
        D3D_FEATURE_LEVEL GetDeviceFeatureLevel () const { return m_FeatureLevel; }
        ID3D12Resource* GetRenderTarget () const { return m_RenderTargets[m_BackBufferIndex].Get (); }
        ID3D12Resource* GetDepthStencil () const { return m_DepthStencil.Get (); }
        ID3D12CommandQueue* GetCommandQueue () const { return m_CommandQueue.Get (); }
        ID3D12CommandAllocator* GetCommandAllocator () const { return m_CommandAllocators[m_BackBufferIndex].Get (); }
        ID3D12GraphicsCommandList* GetCommandList () const { return m_CommandList.Get (); }
        DXGI_FORMAT                 GetBackBufferFormat () const { return m_BackBufferFormat; }
        DXGI_FORMAT                 GetDepthBufferFormat () const { return m_DepthBufferFormat; }
        D3D12_VIEWPORT              GetScreenViewport () const { return m_ScreenViewport; }
        D3D12_RECT                  GetScissorRect () const { return m_ScissorRect; }
        UINT                        GetCurrentFrameIndex () const { return m_BackBufferIndex; }
        UINT                        GetPreviousFrameIndex () const { return m_BackBufferIndex == 0 ? m_BackBufferCount - 1 : m_BackBufferIndex - 1; }
        UINT                        GetBackBufferCount () const { return m_BackBufferCount; }
        unsigned int                GetDeviceOptions () const { return m_Options; }
        LPCWSTR                     GetAdapterDescription () const { return m_AdapterDescription.c_str (); }
        UINT                        GetAdapterID () const { return m_AdapterID; }

        CD3DX12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView () const {
            return CD3DX12_CPU_DESCRIPTOR_HANDLE (m_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart (), m_BackBufferIndex, m_RtvDescriptorSize);
        }
        CD3DX12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView () const {
            return CD3DX12_CPU_DESCRIPTOR_HANDLE (m_DsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart ());
        }

    private:
        void MoveToNextFrame ();
        void InitializeAdapter (IDXGIAdapter1** ppAdapter);

        const static size_t MAX_BACK_BUFFER_COUNT = 3;

        UINT                                                m_AdapterIDoverride;
        UINT                                                m_BackBufferIndex;
        Microsoft::WRL::ComPtr<IDXGIAdapter1>               m_Adapter;
        UINT                                                m_AdapterID;
        std::wstring                                        m_AdapterDescription;

        // Direct3D objects.
        Microsoft::WRL::ComPtr<ID3D12Device>                m_Device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue>          m_CommandQueue;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>   m_CommandList;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator>      m_CommandAllocators[MAX_BACK_BUFFER_COUNT];

        // Swap chain objects.
        Microsoft::WRL::ComPtr<IDXGIFactory4>               m_DxgiFactory;
        Microsoft::WRL::ComPtr<IDXGISwapChain3>             m_SwapChain;
        Microsoft::WRL::ComPtr<ID3D12Resource>              m_RenderTargets[MAX_BACK_BUFFER_COUNT];
        Microsoft::WRL::ComPtr<ID3D12Resource>              m_DepthStencil;

        // Presentation fence objects.
        Microsoft::WRL::ComPtr<ID3D12Fence>                 m_Fence;
        UINT64                                              m_FenceValues[MAX_BACK_BUFFER_COUNT];
        Microsoft::WRL::Wrappers::Event                     m_FenceEvent;

        // Direct3D rendering objects.
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>        m_RtvDescriptorHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>        m_DsvDescriptorHeap;
        UINT                                                m_RtvDescriptorSize;
        D3D12_VIEWPORT                                      m_ScreenViewport;
        D3D12_RECT                                          m_ScissorRect;

        // Direct3D properties.
        DXGI_FORMAT                                         m_BackBufferFormat;
        DXGI_FORMAT                                         m_DepthBufferFormat;
        UINT                                                m_BackBufferCount;
        D3D_FEATURE_LEVEL                                   m_MinFeatureLevel;

        // Cached device properties.
        HWND                                                m_Window;
        D3D_FEATURE_LEVEL                                   m_FeatureLevel;
        RECT                                                m_OutputSize;
        bool                                                m_IsWindowVisible;

        // DeviceResources options (see flags above)
        unsigned int                                        m_Options;

        // The IDeviceNotify can be held directly as it owns the DeviceResources.
        IDeviceNotify* m_DeviceNotify;
    };

}