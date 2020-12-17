#include "stdafx.h"
#include "DXRaytracingLibrarySubobjects.h"
#include "DirectXRaytracingHelper.h"
#include "CompiledShaders\Raytracing.hlsl.h"

using namespace std;
using namespace DX;

const wchar_t* DXRaytracingLibrarySubobjects::c_HitGroupName = L"MyHitGroup";
const wchar_t* DXRaytracingLibrarySubobjects::c_RayGenShaderName = L"MyRaygenShader";
const wchar_t* DXRaytracingLibrarySubobjects::c_ClosestHitShaderName = L"MyClosestHitShader";
const wchar_t* DXRaytracingLibrarySubobjects::c_MissShaderName = L"MyMissShader";

// Library subobject names
const wchar_t* DXRaytracingLibrarySubobjects::c_GlobalRootSignatureName = L"MyGlobalRootSignature";
const wchar_t* DXRaytracingLibrarySubobjects::c_LocalRootSignatureName = L"MyLocalRootSignature";
const wchar_t* DXRaytracingLibrarySubobjects::c_LocalRootSignatureAssociationName = L"MyLocalRootSignatureAssociation";
const wchar_t* DXRaytracingLibrarySubobjects::c_ShaderConfigName = L"MyShaderConfig";
const wchar_t* DXRaytracingLibrarySubobjects::c_PipelineConfigName = L"MyPipelineConfig";

DXRaytracingLibrarySubobjects::DXRaytracingLibrarySubobjects (UINT width, UINT height, std::wstring name) :
    DXSample (width, height, name),
    m_RaytracingOutputResourceUAVDescriptorHeapIndex (UINT_MAX),
    m_CurRotationAngleRad (0.0f),
    m_IsDxrSupported (false) 
{
    UpdateForSizeChange (width, height);
}

void DXRaytracingLibrarySubobjects::EnableDirectXRaytracing (IDXGIAdapter1* adapter) {
    m_IsDxrSupported = IsDirectXRaytracingSupported (adapter);
    ThrowIfFalse (m_IsDxrSupported, L"DirectX Raytracing is not supported by your GPU and driver.\n\n");
}

void DXRaytracingLibrarySubobjects::OnInit () {
    m_DeviceResources = std::make_unique<DeviceResources> (
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_UNKNOWN,
        FrameCount,
        D3D_FEATURE_LEVEL_11_0,
        DeviceResources::c_RequireTearingSupport,
        m_AdapterIDoverride
        );

    m_DeviceResources->RegisterDeviceNotify (this);
    m_DeviceResources->SetWindow (Win32Application::GetHwnd (), m_Width, m_Height);
    m_DeviceResources->InitializeDXGIAdapter ();
    EnableDirectXRaytracing (m_DeviceResources->GetAdapter ());

    m_DeviceResources->CreateDeviceResources ();
    m_DeviceResources->CreateWindowSizeDependentResources ();

    InitializeScene ();

    CreateDeviceDependentResources ();
    CreateWindowSizeDependentResources ();
}

// Update camera matrices passed into the shader.
void DXRaytracingLibrarySubobjects::UpdateCameraMatrices () {
    auto frameIndex = m_DeviceResources->GetCurrentFrameIndex ();

    m_SceneCB[frameIndex].cameraPosition = m_Eye;
    float fovAngleY = 45.0f;
    XMMATRIX view = XMMatrixLookAtLH (m_Eye, m_At, m_Up);
    XMMATRIX proj = XMMatrixPerspectiveFovLH (XMConvertToRadians (fovAngleY), m_AspectRatio, 1.00f, 125.0f);
    XMMATRIX viewProj = view * proj;

    m_SceneCB[frameIndex].projectionToWorld = XMMatrixInverse (nullptr, viewProj);
}

// Initialize scene rendering parameters.
void DXRaytracingLibrarySubobjects::InitializeScene () {
    auto frameIndex = m_DeviceResources->GetCurrentFrameIndex ();
    
    // Setup materials.
    {
        m_CubeCB.albedo = XMFLOAT4 (1.0f, 1.0f, 1.0f, 1.0f);
    }

    // Setup camera.
    {
        // Initialize the view and projection inverse matrices.
        m_Eye = {0.0f, 2.0f, -5.0f, 1.0f};
        m_At = {0.0f, 0.0f, 0.0f, 1.0f};
        XMVECTOR right = {1.0f, 0.0f, 0.0f, 0.0f};

        XMVECTOR direction = XMVector4Normalize (m_At - m_Eye);
        m_Up = XMVector3Normalize (XMVector3Cross (direction, right));

        // Rotate camera around Y axis.
        XMMATRIX rotate = XMMatrixRotationY (XMConvertToRadians (45.0f));
        m_Eye = XMVector3Transform (m_Eye, rotate);
        m_Up = XMVector3Transform (m_Up, rotate);

        UpdateCameraMatrices ();
    }

    // Setup lights.
    {
        // Initialize the lighting parameters.
        XMFLOAT4 lightPosition;
        XMFLOAT4 lightAmbientColor;
        XMFLOAT4 lightDiffuseColor;

        lightPosition = XMFLOAT4 (0.0f, 1.8f, -3.0f, 0.0f);
        m_SceneCB[frameIndex].lightPosition = XMLoadFloat4 (&lightPosition);

        lightAmbientColor = XMFLOAT4 (0.5f, 0.5f, 0.5f, 1.0f);
        m_SceneCB[frameIndex].lightAmbientColor = XMLoadFloat4 (&lightAmbientColor);

        lightDiffuseColor = XMFLOAT4 (0.5f, 0.5f, 0.0f, 1.0f);
        m_SceneCB[frameIndex].lightDiffuseColor = XMLoadFloat4 (&lightDiffuseColor);
    }

    // Apply the initial values to all frames' buffer instances.
    for (auto& sceneCB : m_SceneCB) {
        sceneCB = m_SceneCB[frameIndex];
    }
}

// Create constant buffers.
void DXRaytracingLibrarySubobjects::CreateConstantBuffers () {
    auto device = m_DeviceResources->GetDevice ();
    auto frameCount = m_DeviceResources->GetBackBufferCount ();

    // Create the constant buffer memory and map the CPU and GPU address
    const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_UPLOAD);

    // Allocate one constant buffer per frame, since it gets updated every frame.
    size_t cbSize = frameCount * sizeof (AlignedSceneConstantBuffer);
    const D3D12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer (cbSize);

    ThrowIfFailed (device->CreateCommittedResource (
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &constantBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS (&m_PerFrameConstants)
    ));

    // Map the constant buffer and cache its heap pointers.
    // We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
    CD3DX12_RANGE readRange (0, 0);
    ThrowIfFailed (m_PerFrameConstants->Map (0, nullptr, reinterpret_cast<void**> (&m_MappedConstantData)));
}

void DXRaytracingLibrarySubobjects::CreateDeviceDependentResources () {
    // Initialize raytracing pipeline.

    // Create raytracing interfaces: raytracing device and commandlist.
    CreateRaytracingInterfaces ();

       // Create root signatures for the shaders.
    CreateRootSignatures ();

    // Create a raytracing pipeline state object which defines the binding of shaders, state and resources to be used during raytracing.
    CreateRaytracingPipelineStateObject ();

    // Create a heap for descriptors.
    CreateDescriptorHeap ();

    // Build geometry to be used in the sample.
    BuildGeometry ();

    // Build raytracing acceleration structures from the generated geometry.
    BuildAccelerationStructures ();

    // Create constant buffers for the geometry and the scene.
    CreateConstantBuffers ();

    // Build shader tables, which define shaders and their local root arguments.
    BuildShaderTables ();

    // Create an output 2D texture to store the raytracing result to.
    CreateRaytracingOutputResource ();
}

//void DXRaytracingLibrarySubobjects::SerializeAndCreateRaytracingRootSignature (D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig) {
//    auto device = m_DeviceResources->GetDevice ();
//    ComPtr<ID3DBlob> blob;
//    ComPtr<ID3DBlob> error;
//
//    ThrowIfFailed (D3D12SerializeRootSignature (&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*> (error->GetBufferPointer ()) : nullptr);
//    ThrowIfFailed (device->CreateRootSignature (1, blob->GetBufferPointer (), blob->GetBufferSize (), IID_PPV_ARGS (&(*rootSig))));
//}

void DXRaytracingLibrarySubobjects::CreateRootSignatures () {
    auto device = m_DeviceResources->GetDevice ();

    ThrowIfFailed (device->CreateRootSignature (1, g_pRaytracing, ARRAYSIZE (g_pRaytracing), IID_PPV_ARGS (&m_RaytracingGlobalRootSignature)));
}

// Create raytracing device and command list.
void DXRaytracingLibrarySubobjects::CreateRaytracingInterfaces () {
    auto device = m_DeviceResources->GetDevice ();
    auto commandList = m_DeviceResources->GetCommandList ();

    ThrowIfFailed (device->QueryInterface (IID_PPV_ARGS (&m_DxrDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");

    m_DxrCommandList = reinterpret_cast<ID3D12GraphicsCommandList5*> (commandList);
}

void DXRaytracingLibrarySubobjects::CreateRaytracingPipelineStateObject () {
    // 1 - Triangle hit group
    // 1 - Shader config
    // 2 - Local root signature
    // 1 - Global root signature
    // 1 - Pipeline config
    // 1 - Subobject to export association
    CD3DX12_STATE_OBJECT_DESC raytracingPipeline {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

    // DXIL library
    // This contains the shaders and thier entrypoints for the state object.
    // Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
    auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT> ();
    D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE ((void*)g_pRaytracing, ARRAYSIZE (g_pRaytracing));
    lib->SetDXILLibrary (&libdxil);
    // Define which shader exports to surface from the library.
    // If no shader exports are defined for a DXIL library subobject, all shaders will be surfaed.
    // In this sample, this could be ommited for convenience since the sample uses all shaders in the library.
    {
        lib->DefineExport (c_RayGenShaderName);
        lib->DefineExport (c_ClosestHitShaderName);
        lib->DefineExport (c_MissShaderName);
    }

    // Define which subobjects exports to use from the library.
    // If no exports are defined all subobjects are used.
    {
        lib->DefineExport (c_GlobalRootSignatureName);
        lib->DefineExport (c_LocalRootSignatureName);
        lib->DefineExport (c_LocalRootSignatureAssociationName);
        lib->DefineExport (c_ShaderConfigName);
        lib->DefineExport (c_PipelineConfigName);
        lib->DefineExport (c_HitGroupName);
    }

#if _DEBUG
    PrintStateObjectDesc (raytracingPipeline);
#endif

    ThrowIfFailed (m_DxrDevice->CreateStateObject (raytracingPipeline, IID_PPV_ARGS (&m_DxrStateObject)), L"Couldn't create DirectX Raytracing state object.\n");
}

void DXRaytracingLibrarySubobjects::CreateRaytracingOutputResource () {
    auto device = m_DeviceResources->GetDevice ();
    auto backBufferFormat = m_DeviceResources->GetBackBufferFormat ();

    // Create the output resource. The dimensions and format should match the swap-chain.
    auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D (backBufferFormat, m_Width, m_Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed (device->CreateCommittedResource (
        &defaultHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &uavDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS (&m_RaytracingOutput)
    ));

    D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
    m_RaytracingOutputResourceUAVDescriptorHeapIndex = AllocateDescriptor (&uavDescriptorHandle, m_RaytracingOutputResourceUAVDescriptorHeapIndex);
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
    UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView (m_RaytracingOutput.Get (), nullptr, &UAVDesc, uavDescriptorHandle);
    m_RaytracingOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE (m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart (), m_RaytracingOutputResourceUAVDescriptorHeapIndex, m_DescriptorSize);
}

void DXRaytracingLibrarySubobjects::CreateDescriptorHeap () {
    auto device = m_DeviceResources->GetDevice ();

    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
    // Allocate a heap for 3 descriptors:
    // 2 - Vertex and index buffer SRVs
    // 1 - raytracing output texture SRV
    descriptorHeapDesc.NumDescriptors = 3;
    descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    descriptorHeapDesc.NodeMask = 0;
    device->CreateDescriptorHeap (&descriptorHeapDesc, IID_PPV_ARGS (&m_DescriptorHeap));
    NAME_D3D12_OBJECT (m_DescriptorHeap);

    m_DescriptorSize = device->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void DXRaytracingLibrarySubobjects::BuildGeometry () {
    auto device = m_DeviceResources->GetDevice ();

    // Cube indices.
    Index indices[] = {
        3, 1, 0,
        2, 1, 3,

        6, 4, 5,
        7, 4, 6,

        11, 9, 8,
        10, 9, 11,

        14, 12, 13,
        15, 12, 14,

        19, 17, 16,
        18, 17, 19,

        22, 20, 21,
        23, 20, 22
    };

    // Cube vertices positions and corresponding triangle normals.
    Vertex vertices[] = {
        {XMFLOAT3 (-1.0f, 1.0f, -1.0f), XMFLOAT3 (0.0f, 1.0f, 0.0f)},
        {XMFLOAT3 (1.0f, 1.0f, -1.0f), XMFLOAT3 (0.0f, 1.0f, 0.0f)},
        {XMFLOAT3 (1.0f, 1.0f, 1.0f), XMFLOAT3 (0.0f, 1.0f, 0.0f)},
        {XMFLOAT3 (-1.0f, 1.0f, 1.0f), XMFLOAT3 (0.0f, 1.0f, 0.0f)},

        {XMFLOAT3 (-1.0f, -1.0f, -1.0f), XMFLOAT3 (0.0f, -1.0f, 0.0f)},
        {XMFLOAT3 (1.0f, -1.0f, -1.0f), XMFLOAT3 (0.0f, -1.0f, 0.0f)},
        {XMFLOAT3 (1.0f, -1.0f, 1.0f), XMFLOAT3 (0.0f, -1.0f, 0.0f)},
        {XMFLOAT3 (-1.0f, -1.0f, 1.0f), XMFLOAT3 (0.0f, -1.0f, 0.0f)},

        {XMFLOAT3 (-1.0f, -1.0f, 1.0f), XMFLOAT3 (-1.0f, 0.0f, 0.0f)},
        {XMFLOAT3 (-1.0f, -1.0f, -1.0f), XMFLOAT3 (-1.0f, 0.0f, 0.0f)},
        {XMFLOAT3 (-1.0f, 1.0f, -1.0f), XMFLOAT3 (-1.0f, 0.0f, 0.0f)},
        {XMFLOAT3 (-1.0f, 1.0f, 1.0f), XMFLOAT3 (-1.0f, 0.0f, 0.0f)},

        {XMFLOAT3 (1.0f, -1.0f, 1.0f), XMFLOAT3 (1.0f, 0.0f, 0.0f)},
        {XMFLOAT3 (1.0f, -1.0f, -1.0f), XMFLOAT3 (1.0f, 0.0f, 0.0f)},
        {XMFLOAT3 (1.0f, 1.0f, -1.0f), XMFLOAT3 (1.0f, 0.0f, 0.0f)},
        {XMFLOAT3 (1.0f, 1.0f, 1.0f), XMFLOAT3 (1.0f, 0.0f, 0.0f)},

        {XMFLOAT3 (-1.0f, -1.0f, -1.0f), XMFLOAT3 (0.0f, 0.0f, -1.0f)},
        {XMFLOAT3 (1.0f, -1.0f, -1.0f), XMFLOAT3 (0.0f, 0.0f, -1.0f)},
        {XMFLOAT3 (1.0f, 1.0f, -1.0f), XMFLOAT3 (0.0f, 0.0f, -1.0f)},
        {XMFLOAT3 (-1.0f, 1.0f, -1.0f), XMFLOAT3 (0.0f, 0.0f, -1.0f)},

        {XMFLOAT3 (-1.0f, -1.0f, 1.0f), XMFLOAT3 (0.0f, 0.0f, 1.0f)},
        {XMFLOAT3 (1.0f, -1.0f, 1.0f), XMFLOAT3 (0.0f, 0.0f, 1.0f)},
        {XMFLOAT3 (1.0f, 1.0f, 1.0f), XMFLOAT3 (0.0f, 0.0f, 1.0f)},
        {XMFLOAT3 (-1.0f, 1.0f, 1.0f), XMFLOAT3 (0.0f, 0.0f, 1.0f)},
    };

    AllocateUploadBuffer (device, indices, sizeof (indices), &m_IndexBuffer.Resource);
    AllocateUploadBuffer (device, vertices, sizeof (vertices), &m_VertexBuffer.Resource);

    // Vertex buffer is passed to the shader along with index buffer as a descriptor table.
    // Vertex buffer descriptor must follow index buffer descriptor in the descriptor heap.
    UINT descriptorIndexIB = CreateBufferSRV (&m_IndexBuffer, sizeof (indices) / 4, 0);
    UINT descriptorIndexVB = CreateBufferSRV (&m_VertexBuffer, ARRAYSIZE (vertices), sizeof (vertices[0]));
    ThrowIfFalse (descriptorIndexVB == descriptorIndexIB + 1, L"Vertex Buffer descriptor index must follow that of Index Buffer descriptor Index!");
}

void DXRaytracingLibrarySubobjects::BuildAccelerationStructures () {
    auto device = m_DeviceResources->GetDevice ();
    auto commandList = m_DeviceResources->GetCommandList ();
    auto commandQueue = m_DeviceResources->GetCommandQueue ();
    auto commandAllocator = m_DeviceResources->GetCommandAllocator ();

    commandList->Reset (commandAllocator, nullptr);

    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
    geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometryDesc.Triangles.IndexBuffer = m_IndexBuffer.Resource->GetGPUVirtualAddress ();
    geometryDesc.Triangles.IndexCount = static_cast<UINT> (m_IndexBuffer.Resource->GetDesc ().Width) / sizeof (Index);
    geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
    geometryDesc.Triangles.Transform3x4 = 0;
    geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometryDesc.Triangles.VertexCount = static_cast<UINT> (m_VertexBuffer.Resource->GetDesc ().Width) / sizeof (Vertex);
    geometryDesc.Triangles.VertexBuffer.StartAddress = m_VertexBuffer.Resource->GetGPUVirtualAddress ();
    geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof (Vertex);
    geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC  bottomLevelBuildDesc = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& bottomLevelInputs = bottomLevelBuildDesc.Inputs;
    bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottomLevelInputs.Flags = buildFlags;
    bottomLevelInputs.NumDescs = 1;
    bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottomLevelInputs.pGeometryDescs = &geometryDesc;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC  topLevelBuildDesc = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& topLevelInputs = topLevelBuildDesc.Inputs;
    topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    topLevelInputs.Flags = buildFlags;
    topLevelInputs.NumDescs = 1;
    topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    topLevelInputs.pGeometryDescs = nullptr;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo;
    m_DxrDevice->GetRaytracingAccelerationStructurePrebuildInfo (&topLevelInputs, &topLevelPrebuildInfo);
    ThrowIfFalse (topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo;
    m_DxrDevice->GetRaytracingAccelerationStructurePrebuildInfo (&bottomLevelInputs, &bottomLevelPrebuildInfo);
    ThrowIfFalse (bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

    ComPtr<ID3D12Resource> scratchResource;
    AllocateUAVBuffer (device, max (topLevelPrebuildInfo.ScratchDataSizeInBytes, bottomLevelPrebuildInfo.ScratchDataSizeInBytes), &scratchResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

    {
        D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

        AllocateUAVBuffer (device, bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_BottomLevelAccelerationStructure, initialResourceState, L"BottomLevelAccelerationStructure");
        AllocateUAVBuffer (device, topLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_TopLevelAccelerationStructure, initialResourceState, L"TopLevelAccelerationStructure");
    }

    ComPtr<ID3D12Resource> instanceDescs;
    {
        D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
        instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;
        instanceDesc.InstanceMask = 1;
        instanceDesc.AccelerationStructure = m_BottomLevelAccelerationStructure->GetGPUVirtualAddress ();
        AllocateUploadBuffer (device, &instanceDesc, sizeof (instanceDesc), &instanceDescs, L"InstanceDescs");
    }

    // Bottom Level Acceleration Structure desc
    {
        bottomLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress ();
        bottomLevelBuildDesc.DestAccelerationStructureData = m_BottomLevelAccelerationStructure->GetGPUVirtualAddress ();
    }

    // Top Level Acceleration Structure desc
    {
        topLevelBuildDesc.DestAccelerationStructureData = m_TopLevelAccelerationStructure->GetGPUVirtualAddress ();
        topLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress ();
        topLevelBuildDesc.Inputs.InstanceDescs = instanceDescs->GetGPUVirtualAddress ();
    }

    auto BuildAccelerationStructure = [&](auto* raytracingCommandList) {
        raytracingCommandList->BuildRaytracingAccelerationStructure (&bottomLevelBuildDesc, 0, nullptr);
        commandList->ResourceBarrier (1, &CD3DX12_RESOURCE_BARRIER::UAV (m_BottomLevelAccelerationStructure.Get ()));
        raytracingCommandList->BuildRaytracingAccelerationStructure (&topLevelBuildDesc, 0, nullptr);
    };

    BuildAccelerationStructure (m_DxrCommandList);

    // Kick off acceleration structure construction.
    m_DeviceResources->ExecuteCommandList ();

    // Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
    m_DeviceResources->WaitForGpu ();
}

void DXRaytracingLibrarySubobjects::BuildShaderTables () {
    auto device = m_DeviceResources->GetDevice ();
    
    void* rayGenShaderIdentifier;
    void* missShaderIdentifier;
    void* hitGroupShaderIdentifier;

    auto GetShaderIdentifiers = [&](auto* stateObjectProperties) {
        rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier (c_RayGenShaderName);
        missShaderIdentifier = stateObjectProperties->GetShaderIdentifier (c_MissShaderName);
        hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier (c_HitGroupName);
    };

    UINT shaderIdentifierSize;
    {
        ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
        ThrowIfFailed (m_DxrStateObject.As (&stateObjectProperties));
        GetShaderIdentifiers (stateObjectProperties.Get ());
        shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    }

    {
        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIdentifierSize;
        ShaderTable rayGenShaderTable (device, numShaderRecords, shaderRecordSize, L"RayGenShaderTable");
        rayGenShaderTable.Push (ShaderRecord (rayGenShaderIdentifier, shaderIdentifierSize));
        m_RayGenShaderTable = rayGenShaderTable.GetResource ();
    }

    {
        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIdentifierSize;
        ShaderTable missShaderTable (device, numShaderRecords, shaderRecordSize, L"MissShaderTable");
        missShaderTable.Push (ShaderRecord (missShaderIdentifier, shaderIdentifierSize));
        m_MissShaderTable = missShaderTable.GetResource ();
    }

    {
        struct RootArguments {
            CubeConstantBuffer cb;
        } rootArguments;
        rootArguments.cb = m_CubeCB;

        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIdentifierSize + sizeof (rootArguments);
        ShaderTable hitGroupShaderTable (device, numShaderRecords, shaderRecordSize, L"HitGroupShaderTable");
        hitGroupShaderTable.Push (ShaderRecord (hitGroupShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof (rootArguments)));
        m_HitGroupShaderTable = hitGroupShaderTable.GetResource ();
    }
}

void DXRaytracingLibrarySubobjects::OnUpdate () {
    m_Timer.Tick ();
    CalculateFrameStats ();
    float elapsedTime = static_cast<float> (m_Timer.GetElapsedSeconds ());
    auto frameIndex = m_DeviceResources->GetCurrentFrameIndex ();
    auto prevFrameIndex = m_DeviceResources->GetPreviousFrameIndex ();

    // Rotate the camera around Y axis.
    {
        // 360 angle per 24 seconds
        float secondsToRotateAround = 24.0f;
        float angleToRotateBy = 360.0f * (elapsedTime / secondsToRotateAround);
        XMMATRIX rotate = XMMatrixRotationY (XMConvertToRadians (angleToRotateBy));
        m_Eye = XMVector3Transform (m_Eye, rotate);
        m_Up = XMVector3Transform (m_Up, rotate);
        m_At = XMVector3Transform (m_At, rotate);
        UpdateCameraMatrices ();
    }

    // Rotate the second light around Y axis.
    {
        float secondsToRotateAround = 8.0f;
        float angleToRotateBy = -360.0f * (elapsedTime / secondsToRotateAround);
        XMMATRIX rotate = XMMatrixRotationY (XMConvertToRadians (angleToRotateBy));
        const XMVECTOR& prevLightPosition = m_SceneCB[prevFrameIndex].lightPosition;
        m_SceneCB[frameIndex].lightPosition = XMVector3Transform (prevLightPosition, rotate);
    }
}

void DXRaytracingLibrarySubobjects::DoRaytracing () {
    auto commandList = m_DeviceResources->GetCommandList ();
    auto frameIndex = m_DeviceResources->GetCurrentFrameIndex ();

    auto DispatchRays = [&](auto* commandList, auto* stateObject, auto* dispatchDesc) {
        dispatchDesc->HitGroupTable.StartAddress = m_HitGroupShaderTable->GetGPUVirtualAddress ();
        dispatchDesc->HitGroupTable.SizeInBytes = m_HitGroupShaderTable->GetDesc ().Width;
        dispatchDesc->HitGroupTable.StrideInBytes = dispatchDesc->HitGroupTable.SizeInBytes;
        dispatchDesc->MissShaderTable.StartAddress = m_MissShaderTable->GetGPUVirtualAddress ();
        dispatchDesc->MissShaderTable.SizeInBytes = m_MissShaderTable->GetDesc ().Width;
        dispatchDesc->MissShaderTable.StrideInBytes = dispatchDesc->MissShaderTable.SizeInBytes;
        dispatchDesc->RayGenerationShaderRecord.StartAddress = m_RayGenShaderTable->GetGPUVirtualAddress ();
        dispatchDesc->RayGenerationShaderRecord.SizeInBytes = m_RayGenShaderTable->GetDesc ().Width;
        dispatchDesc->Width = m_Width;
        dispatchDesc->Height = m_Height;
        dispatchDesc->Depth = 1;
        commandList->SetPipelineState1 (stateObject);
        commandList->DispatchRays (dispatchDesc);
    };

    auto SetCommonPipelineState = [&](auto* descriptorSetCommandList) {
        descriptorSetCommandList->SetDescriptorHeaps (1, m_DescriptorHeap.GetAddressOf ());
        commandList->SetComputeRootDescriptorTable (GlobalRootSignatureParams::VertexBuffersSlot, m_IndexBuffer.GpuDescriptorHandle);
        commandList->SetComputeRootDescriptorTable (GlobalRootSignatureParams::OutputViewSlot, m_RaytracingOutputResourceUAVGpuDescriptor);
    };

    commandList->SetComputeRootSignature (m_RaytracingGlobalRootSignature.Get ());

    memcpy (&m_MappedConstantData[frameIndex].constants, &m_SceneCB[frameIndex], sizeof (m_SceneCB[frameIndex]));
    auto cbGpuAddress = m_PerFrameConstants->GetGPUVirtualAddress () + frameIndex * sizeof (m_MappedConstantData[0]);
    commandList->SetComputeRootConstantBufferView (GlobalRootSignatureParams::SceneConstantSlot, cbGpuAddress);

    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    {
        SetCommonPipelineState (commandList);
        commandList->SetComputeRootShaderResourceView (GlobalRootSignatureParams::AccelerationStructureSlot, m_TopLevelAccelerationStructure->GetGPUVirtualAddress ());
        DispatchRays (m_DxrCommandList, m_DxrStateObject.Get (), &dispatchDesc);
    }
}

void DXRaytracingLibrarySubobjects::UpdateForSizeChange (UINT width, UINT height) {
	DXSample::UpdateForSizeChange (width, height);
}

void DXRaytracingLibrarySubobjects::CopyRaytracingOutputToBackBuffer () {
    auto commandList = m_DeviceResources->GetCommandList ();
    auto renderTarget = m_DeviceResources->GetRenderTarget ();

    D3D12_RESOURCE_BARRIER preCopyBarrier[2];
    preCopyBarrier[0] = CD3DX12_RESOURCE_BARRIER::Transition (renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    preCopyBarrier[1] = CD3DX12_RESOURCE_BARRIER::Transition (m_RaytracingOutput.Get (), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier (ARRAYSIZE (preCopyBarrier), preCopyBarrier);

    commandList->CopyResource (renderTarget, m_RaytracingOutput.Get ());

    D3D12_RESOURCE_BARRIER postCopyBarriers[2];
    postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition (renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition (m_RaytracingOutput.Get (), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResourceBarrier (ARRAYSIZE (postCopyBarriers), postCopyBarriers);
}

void DXRaytracingLibrarySubobjects::CreateWindowSizeDependentResources () {
    CreateRaytracingOutputResource ();
    UpdateCameraMatrices ();
}

void DXRaytracingLibrarySubobjects::ReleaseWindowSizeDependentResources () {
	m_RaytracingOutput.Reset ();
}

void DXRaytracingLibrarySubobjects::ReleaseDeviceDependentResources () {
    m_RaytracingGlobalRootSignature.Reset ();

    m_DxrDevice.Reset ();
    m_DxrStateObject.Reset ();

    m_DescriptorHeap.Reset ();
    m_DescriptorsAllocated = 0;
    m_RaytracingOutputResourceUAVDescriptorHeapIndex = UINT_MAX;
    m_IndexBuffer.Resource.Reset ();
    m_VertexBuffer.Resource.Reset ();
    m_PerFrameConstants.Reset ();
    m_RayGenShaderTable.Reset ();
    m_MissShaderTable.Reset ();
    m_HitGroupShaderTable.Reset ();

    m_BottomLevelAccelerationStructure.Reset ();
    m_TopLevelAccelerationStructure.Reset ();
}

void DXRaytracingLibrarySubobjects::RecreateD3D () {
    try {
        m_DeviceResources->WaitForGpu ();
    } catch (HrException&) {

    }

    m_DeviceResources->HandleDeviceLost ();
}

void DXRaytracingLibrarySubobjects::OnRender () {
    if (!m_DeviceResources->IsWindowVisible ()) {
        return;
    }

    m_DeviceResources->Prepare ();
    DoRaytracing ();
    CopyRaytracingOutputToBackBuffer ();

    m_DeviceResources->Present (D3D12_RESOURCE_STATE_PRESENT);
}

void DXRaytracingLibrarySubobjects::OnDestroy () {
    m_DeviceResources->WaitForGpu ();
    OnDeviceLost ();
}

void DXRaytracingLibrarySubobjects::OnDeviceLost () {
    ReleaseWindowSizeDependentResources ();
    ReleaseDeviceDependentResources ();
}

void DXRaytracingLibrarySubobjects::OnDeviceRestored () {
    CreateDeviceDependentResources ();
    CreateWindowSizeDependentResources ();
}

void DXRaytracingLibrarySubobjects::CalculateFrameStats () {
    static int frameCnt = 0;
    static double elapsedTime = 0.0f;
    double totalTime = m_Timer.GetTotalSeconds ();
    frameCnt++;

    // Compute averages over one second period.
    if ((totalTime - elapsedTime) >= 1.0f) {
        float diff = static_cast<float> (totalTime - elapsedTime);
        float fps = static_cast<float> (frameCnt) / diff;       // Normalize to an exact second.

        frameCnt = 0;
        elapsedTime = totalTime;

        float MRaysPerSecond = (m_Width * m_Height * fps) / static_cast<float> (1e6);

        wstringstream windowText;

        windowText << setprecision (2) << fixed
            << L"    fps: " << fps << L"     ~Million Primary Rays/s: " << MRaysPerSecond
            << L"    GPU[" << m_DeviceResources->GetAdapterID () << L"]: " << m_DeviceResources->GetAdapterDescription ();

        SetCustomWindowText (windowText.str ().c_str ());
    }
}

void DXRaytracingLibrarySubobjects::OnSizeChanged (UINT width, UINT height, bool minimized) {
    if (!m_DeviceResources->WindowSizeChanged (width, height, minimized)) {
        return;
    }

    UpdateForSizeChange (width, height);

    ReleaseWindowSizeDependentResources ();
    CreateWindowSizeDependentResources ();
}

// Allocate a descriptor and return its index.
// If the passed descriptorIndexToUse is valid, it will be used instead of allocating a new one.
UINT DXRaytracingLibrarySubobjects::AllocateDescriptor (D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse) {
    auto descriptorHeapCpuBase = m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart ();
    if (descriptorIndexToUse >= m_DescriptorHeap->GetDesc ().NumDescriptors) {
        descriptorIndexToUse = m_DescriptorsAllocated++;
    }
    *cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE (descriptorHeapCpuBase, descriptorIndexToUse, m_DescriptorSize);
    return descriptorIndexToUse;
}

// Create SRV for a buffer.
UINT DXRaytracingLibrarySubobjects::CreateBufferSRV (D3DBuffer* buffer, UINT numElements, UINT elementSize) {
    auto device = m_DeviceResources->GetDevice ();

    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.NumElements = numElements;
    if (elementSize == 0) {
        srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        srvDesc.Buffer.StructureByteStride = 0;
    } else {
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.StructureByteStride = elementSize;
    }
    UINT descriptorIndex = AllocateDescriptor (&buffer->CpuDescriptorHandle);
    device->CreateShaderResourceView (buffer->Resource.Get (), &srvDesc, buffer->CpuDescriptorHandle);
    buffer->GpuDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE (m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart (), descriptorIndex, m_DescriptorSize);
    return descriptorIndex;
}