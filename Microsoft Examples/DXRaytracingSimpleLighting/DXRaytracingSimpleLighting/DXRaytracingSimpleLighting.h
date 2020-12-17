#pragma once

#include "DXSample.h"
#include "StepTimer.h"
#include "RaytracingHlslCompat.h"

namespace GlobalRootSignatureParams {
    enum Value {
        OutputViewSlot = 0,
        AccelerationStructureSlot,
        SceneConstantSlot,
        VertexBuffersSlot,
        Count
    };
}

namespace LocalRootSignatureParams {
    enum Value {
        CubeConstantSlot = 0,
        Count
    };
}

class DXRaytracingSimpleLighting : public DXSample {
public:
    DXRaytracingSimpleLighting (UINT width, UINT height, std::wstring name);

    // IDeviceNotify
    virtual void OnDeviceLost () override;
    virtual void OnDeviceRestored () override;

    // Messages
    virtual void OnInit ();
    virtual void OnUpdate ();
    virtual void OnRender ();
    virtual void OnSizeChanged (UINT width, UINT height, bool minimized);
    virtual void OnDestroy ();
    virtual IDXGISwapChain* GetSwapChain () { return m_DeviceResources->GetSwapChain (); }

private:
    static const UINT FrameCount = 3;

    // We'll allocate space for several of these and they will need to be padded for alignment.
    static_assert(sizeof (SceneConstantBuffer) < D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "Checking the size here.");

    union AlignedSceneConstantBuffer {
        SceneConstantBuffer Constants;
        uint8_t AlignmentPadding[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT];
    };
    AlignedSceneConstantBuffer* m_MappedConstantData;
    ComPtr<ID3D12Resource>       m_PerFrameConstants;

    // DirectX Raytracing (DXR) attributes
    ComPtr<ID3D12Device5> m_DxrDevice;
    ComPtr<ID3D12GraphicsCommandList5> m_DxrCommandList;
    ComPtr<ID3D12StateObject> m_DxrStateObject;

    // Root signatures
    ComPtr<ID3D12RootSignature> m_RaytracingGlobalRootSignature;
    ComPtr<ID3D12RootSignature> m_RaytracingLocalRootSignature;

    // Descriptors
    ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;
    UINT m_DescriptorsAllocated = 0;
    UINT m_DescriptorSize;

    // Raytracing scene
    SceneConstantBuffer m_SceneCB[FrameCount];
    CubeConstantBuffer m_CubeCB;

    // Geometry
    struct D3DBuffer {
        ComPtr<ID3D12Resource> Resource;
        D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptorHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptorHandle;
    };
    D3DBuffer m_IndexBuffer;
    D3DBuffer m_VertexBuffer;

    // Acceleration structure
    ComPtr<ID3D12Resource> m_BottomLevelAccelerationStructure;
    ComPtr<ID3D12Resource> m_TopLevelAccelerationStructure;

    // Raytracing output
    ComPtr<ID3D12Resource> m_RaytracingOutput;
    D3D12_GPU_DESCRIPTOR_HANDLE m_RaytracingOutputResourceUAVGpuDescriptor;
    UINT m_RaytracingOutputResourceUAVDescriptorHeapIndex;

    // Shader tables
    static const wchar_t* c_HitGroupName;
    static const wchar_t* c_RaygenShaderName;
    static const wchar_t* c_ClosestHitShaderName;
    static const wchar_t* c_MissShaderName;
    ComPtr<ID3D12Resource> m_MissShaderTable;
    ComPtr<ID3D12Resource> m_HitGroupShaderTable;
    ComPtr<ID3D12Resource> m_RayGenShaderTable;

    // Application state
    StepTimer m_Timer;
    float m_CurRotationAngleRad;
    XMVECTOR m_Eye;
    XMVECTOR m_At;
    XMVECTOR m_Up;

    void UpdateCameraMatrices ();
    void InitializeScene ();
    void RecreateD3D ();
    void DoRaytracing ();
    void CreateConstantBuffers ();
    void CreateDeviceDependentResources ();
    void CreateWindowSizeDependentResources ();
    void ReleaseDeviceDependentResources ();
    void ReleaseWindowSizeDependentResources ();
    void CreateRaytracingInterfaces ();
    void SerializeAndCreateRaytracingRootSignature (D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig);
    void CreateRootSignatures ();
    void CreateLocalRootSignatureSubobjects (CD3DX12_STATE_OBJECT_DESC* raytracingPipeline);
    void CreateRaytracingPipelineStateObject ();
    void CreateDescriptorHeap ();
    void CreateRaytracingOutputResource ();
    void BuildGeometry ();
    void BuildAccelerationStructures ();
    void BuildShaderTables ();
    void UpdateForSizeChange (UINT clientWidth, UINT clientHeight);
    void CopyRaytracingOutputToBackBuffer ();
    void CalculateFrameStats ();
    UINT AllocateDescriptor (D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse = UINT_MAX);
    UINT CreateBufferSRV (D3DBuffer* buffer, UINT numElements, UINT elementSize);
};