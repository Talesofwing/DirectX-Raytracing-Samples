#include "stdafx.h"
#include "DXRaytracingSimpleLighting.h"
#include "DirectXRaytracingHelper.h"
#include "CompiledShaders\Raytracing.hlsl.h"

using namespace std;
using namespace DX;

const wchar_t* DXRaytracingSimpleLighting::c_HitGroupName = L"MyHitGroup";
const wchar_t* DXRaytracingSimpleLighting::c_RaygenShaderName = L"MyRaygenShader";
const wchar_t* DXRaytracingSimpleLighting::c_ClosestHitShaderName = L"MyClosestHitShader";
const wchar_t* DXRaytracingSimpleLighting::c_MissShaderName = L"MyMissShader";

DXRaytracingSimpleLighting::DXRaytracingSimpleLighting (UINT width, UINT height, std::wstring name) :
	DXSample (width, height, name),
	m_RaytracingOutputResourceUAVDescriptorHeapIndex (UINT_MAX),
	m_CurRotationAngleRad (0.0f) {

	UpdateForSizeChange (width, height);
}

void DXRaytracingSimpleLighting::OnInit () {
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

	ThrowIfFalse (IsDirectXRaytracingSupported (m_DeviceResources->GetAdapter ()),
		L"ERROR: DirectX Raytracing is not supported by your OS, GPU and/or driver.\n\n");

	m_DeviceResources->CreateDeviceResources ();
	m_DeviceResources->CreateWindowSizeDependentResources ();

	InitializeScene ();

	CreateDeviceDependentResources ();
	CreateWindowSizeDependentResources ();
}

void DXRaytracingSimpleLighting::UpdateCameraMatrices () {
	auto frameIndex = m_DeviceResources->GetCurrentFrameIndex ();

	m_SceneCB[frameIndex].cameraPosition = m_Eye;
	float fovAngleY = 45.0f;
	XMMATRIX view = XMMatrixLookAtLH (m_Eye, m_At, m_Up);
	XMMATRIX proj = XMMatrixPerspectiveFovLH (XMConvertToRadians (fovAngleY), m_AspectRatio, 1.0f, 125.0f);
	XMMATRIX viewProj = view * proj;

	m_SceneCB[frameIndex].projectionToWorld = XMMatrixInverse (nullptr, viewProj);
}

void DXRaytracingSimpleLighting::InitializeScene () {
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

		lightAmbientColor = XMFLOAT4 (0.35f, 0.2f, 0.3f, 1.0f);
		m_SceneCB[frameIndex].lightAmbientColor = XMLoadFloat4 (&lightAmbientColor);

		lightDiffuseColor = XMFLOAT4 (0.5f, 0.0f, 0.0f, 1.0f);
		m_SceneCB[frameIndex].lightDiffuseColor = XMLoadFloat4 (&lightDiffuseColor);
	}

	// Apply the initial values to all frames' buffer instances.
	for (auto& sceneCB : m_SceneCB) {
		sceneCB = m_SceneCB[frameIndex];
	}
}

void DXRaytracingSimpleLighting::CreateConstantBuffers () {
	auto device = m_DeviceResources->GetD3DDevice ();
	auto frameCount = m_DeviceResources->GetBackBufferCount ();

	// Create the constant buffer memory and map the CPU and GPU addresses
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
		IID_PPV_ARGS (&m_PerFrameConstants)));

	// Map the constant buffer and cache its heap pointers.
	// We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
	CD3DX12_RANGE readRange (0, 0);        // We do not intend to read from this resource on the CPU.
	ThrowIfFailed (m_PerFrameConstants->Map (0, nullptr, reinterpret_cast<void**>(&m_MappedConstantData)));
}

void DXRaytracingSimpleLighting::CreateDeviceDependentResources () {
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

void DXRaytracingSimpleLighting::SerializeAndCreateRaytracingRootSignature (D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig) {
	auto device = m_DeviceResources->GetD3DDevice ();
	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> error;

	ThrowIfFailed (D3D12SerializeRootSignature (&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer ()) : nullptr);
	ThrowIfFailed (device->CreateRootSignature (1, blob->GetBufferPointer (), blob->GetBufferSize (), IID_PPV_ARGS (&(*rootSig))));
}

void DXRaytracingSimpleLighting::CreateRootSignatures () {
	auto device = m_DeviceResources->GetD3DDevice ();

	// Global Root Signature
	// This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
	{
		CD3DX12_DESCRIPTOR_RANGE ranges[2]; // Perfomance TIP: Order from most frequent to least frequent.
		ranges[0].Init (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);  // 1 output texture
		ranges[1].Init (D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1);  // 2 static index and vertex buffers.

		CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
		rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable (1, &ranges[0]);
		rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView (0);
		rootParameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView (0);
		rootParameters[GlobalRootSignatureParams::VertexBuffersSlot].InitAsDescriptorTable (1, &ranges[1]);
		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc (ARRAYSIZE (rootParameters), rootParameters);
		SerializeAndCreateRaytracingRootSignature (globalRootSignatureDesc, &m_RaytracingGlobalRootSignature);
	}

	// Local Root Signature
	// This is a root signature that enables a shader to have unique arguments that come from shader tables.
	{
		CD3DX12_ROOT_PARAMETER rootParameters[LocalRootSignatureParams::Count];
		rootParameters[LocalRootSignatureParams::CubeConstantSlot].InitAsConstants (SizeOfInUint32 (m_CubeCB), 1);
		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc (ARRAYSIZE (rootParameters), rootParameters);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		SerializeAndCreateRaytracingRootSignature (localRootSignatureDesc, &m_RaytracingLocalRootSignature);
	}
}

void DXRaytracingSimpleLighting::CreateRaytracingInterfaces () {
	auto device = m_DeviceResources->GetD3DDevice ();
	auto commandList = m_DeviceResources->GetCommandList ();

	ThrowIfFailed (device->QueryInterface (IID_PPV_ARGS (&m_DxrDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");
	ThrowIfFailed (commandList->QueryInterface (IID_PPV_ARGS (&m_DxrCommandList)), L"Couldn't get DirectX Raytracing interface for the command list.\n");
}

void DXRaytracingSimpleLighting::CreateLocalRootSignatureSubobjects (CD3DX12_STATE_OBJECT_DESC* raytracingPipeline) {
	auto localRootSignature = raytracingPipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT> ();
	localRootSignature->SetRootSignature (m_RaytracingLocalRootSignature.Get ());

	{
		auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT> ();
		rootSignatureAssociation->SetSubobjectToAssociate (*localRootSignature);
		rootSignatureAssociation->AddExport (c_HitGroupName);
	}
}

void DXRaytracingSimpleLighting::CreateRaytracingPipelineStateObject () {
	CD3DX12_STATE_OBJECT_DESC raytracingPipeline {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

	auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT> ();
	D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE ((void*)g_pRaytracing, ARRAYSIZE (g_pRaytracing));
	lib->SetDXILLibrary (&libdxil);
	{
		lib->DefineExport (c_RaygenShaderName);
		lib->DefineExport (c_ClosestHitShaderName);
		lib->DefineExport (c_MissShaderName);
	}

	auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT> ();
	hitGroup->SetClosestHitShaderImport (c_ClosestHitShaderName);
	hitGroup->SetHitGroupExport (c_HitGroupName);
	hitGroup->SetHitGroupType (D3D12_HIT_GROUP_TYPE_TRIANGLES);

	auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT> ();
	UINT payloadSize = sizeof (XMFLOAT4);    // float4 pixelColor
	UINT attributeSize = sizeof (XMFLOAT2);  // float2 barycentrics
	shaderConfig->Config (payloadSize, attributeSize);

	CreateLocalRootSignatureSubobjects (&raytracingPipeline);

	auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT> ();
	globalRootSignature->SetRootSignature (m_RaytracingGlobalRootSignature.Get ());

	auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT> ();
	UINT maxRecursionDepth = 1; // ~ primary rays only. 
	pipelineConfig->Config (maxRecursionDepth);

#if _DEBUG
	PrintStateObjectDesc (raytracingPipeline);
#endif

	ThrowIfFailed (m_DxrDevice->CreateStateObject (raytracingPipeline, IID_PPV_ARGS (&m_DxrStateObject)), L"Couldn't create DirectX Raytracing state object.\n");
}

void DXRaytracingSimpleLighting::CreateRaytracingOutputResource () {
	auto device = m_DeviceResources->GetD3DDevice ();
	auto backbufferFormat = m_DeviceResources->GetBackBufferFormat ();

	auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D (backbufferFormat, m_Width, m_Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

	auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed (device->CreateCommittedResource (
		&defaultHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&uavDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS (&m_RaytracingOutput)
	));
	NAME_D3D12_OBJECT (m_RaytracingOutput);

	D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
	m_RaytracingOutputResourceUAVDescriptorHeapIndex = AllocateDescriptor (&uavDescriptorHandle, m_RaytracingOutputResourceUAVDescriptorHeapIndex);
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	device->CreateUnorderedAccessView (m_RaytracingOutput.Get (), nullptr, &UAVDesc, uavDescriptorHandle);
	m_RaytracingOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE (m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart (), m_RaytracingOutputResourceUAVDescriptorHeapIndex, m_DescriptorSize);
}

void DXRaytracingSimpleLighting::CreateDescriptorHeap () {
	auto device = m_DeviceResources->GetD3DDevice ();

	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
	// Allocate a heap for 3 descriptors:
	// 2 - vertex and index buffer SRVs
	// 1 - raytracing output texture SRV
	descriptorHeapDesc.NumDescriptors = 3;
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descriptorHeapDesc.NodeMask = 0;
	device->CreateDescriptorHeap (&descriptorHeapDesc, IID_PPV_ARGS (&m_DescriptorHeap));
	NAME_D3D12_OBJECT (m_DescriptorHeap);

	m_DescriptorSize = device->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void DXRaytracingSimpleLighting::BuildGeometry () {
	auto device = m_DeviceResources->GetD3DDevice ();

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
	ThrowIfFalse (descriptorIndexVB == descriptorIndexIB + 1, L"Vertex Buffer descriptor index must follow that of Index Buffer descriptor index!");
}

void DXRaytracingSimpleLighting::BuildAccelerationStructures () {
	auto device = m_DeviceResources->GetD3DDevice ();
	auto commandList = m_DeviceResources->GetCommandList ();
	auto commandQueue = m_DeviceResources->GetCommandQueue ();
	auto commandAllocator = m_DeviceResources->GetCommandAllocator ();

	commandList->Reset (commandAllocator, nullptr);

	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
	geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc.Triangles.IndexBuffer = m_IndexBuffer.Resource->GetGPUVirtualAddress ();
	geometryDesc.Triangles.IndexCount = static_cast<UINT>(m_IndexBuffer.Resource->GetDesc ().Width) / sizeof (Index);
	geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
	geometryDesc.Triangles.Transform3x4 = 0;
	geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometryDesc.Triangles.VertexCount = static_cast<UINT>(m_VertexBuffer.Resource->GetDesc ().Width) / sizeof (Vertex);
	geometryDesc.Triangles.VertexBuffer.StartAddress = m_VertexBuffer.Resource->GetGPUVirtualAddress ();
	geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof (Vertex);
	geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& bottomLevelInputs = bottomLevelBuildDesc.Inputs;
	bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	bottomLevelInputs.Flags = buildFlags;
	bottomLevelInputs.NumDescs = 1;
	bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	bottomLevelInputs.pGeometryDescs = &geometryDesc;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& topLevelInputs = topLevelBuildDesc.Inputs;
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	topLevelInputs.Flags = buildFlags;
	topLevelInputs.NumDescs = 1;
	topLevelInputs.pGeometryDescs = nullptr;
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
	m_DxrDevice->GetRaytracingAccelerationStructurePrebuildInfo (&topLevelInputs, &topLevelPrebuildInfo);
	ThrowIfFalse (topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
	m_DxrDevice->GetRaytracingAccelerationStructurePrebuildInfo (&bottomLevelInputs, &bottomLevelPrebuildInfo);
	ThrowIfFalse (bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

	ComPtr<ID3D12Resource> scratchResource;
	AllocateUAVBuffer (device, max (topLevelPrebuildInfo.ScratchDataSizeInBytes, bottomLevelPrebuildInfo.ScratchDataSizeInBytes), &scratchResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");

	{
		D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

		AllocateUAVBuffer (device, bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_BottomLevelAccelerationStructure, initialResourceState, L"BottomLevelAccelerationStructure");
		AllocateUAVBuffer (device, topLevelPrebuildInfo.ResultDataMaxSizeInBytes, &m_TopLevelAccelerationStructure, initialResourceState, L"TopLevelAccelerationStructure");
	}

	// Create an instance desc for the bottom-level acceleration structure.
	ComPtr<ID3D12Resource> instanceDescs;
	D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
	instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1;
	instanceDesc.InstanceMask = 1;
	instanceDesc.AccelerationStructure = m_BottomLevelAccelerationStructure->GetGPUVirtualAddress ();
	AllocateUploadBuffer (device, &instanceDesc, sizeof (instanceDesc), &instanceDescs, L"InstanceDescs");

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

	// Build acceleration structure.
	BuildAccelerationStructure (m_DxrCommandList.Get ());

	// Kick off acceleration structure construction.
	m_DeviceResources->ExecuteCommandList ();

	// Wait for GPU to finish as the locally created temporary GPU resources will get released once we go out of scope.
	m_DeviceResources->WaitForGpu ();
}

void DXRaytracingSimpleLighting::BuildShaderTables () {
	auto device = m_DeviceResources->GetD3DDevice ();

	void* rayGenShaderIdentifier;
	void* missShaderIdentifier;
	void* hitGroupShaderIdentifier;

	auto GetShaderIdentifiers = [&](auto* stateObjectProperties) {
		rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier (c_RaygenShaderName);
		missShaderIdentifier = stateObjectProperties->GetShaderIdentifier (c_MissShaderName);
		hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier (c_HitGroupName);
	};

	// Get shader identifiers.
	UINT shaderIdentifierSize;
	{
		ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
		ThrowIfFailed (m_DxrStateObject.As (&stateObjectProperties));
		GetShaderIdentifiers (stateObjectProperties.Get ());
		shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	}

	// Ray gen shader table
	{
		struct RootArguments {
			CubeConstantBuffer cb;
		} rootArguments;
		rootArguments.cb = m_CubeCB;

		UINT numShaderRecords = 1;
		UINT shaderRecordSize = shaderIdentifierSize;
		ShaderTable rayGenShaderTable (device, numShaderRecords, shaderRecordSize, L"RayGenShaderTable");
		rayGenShaderTable.Push (ShaderRecord (rayGenShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof (rootArguments)));
		m_RayGenShaderTable = rayGenShaderTable.GetResource ();
	}

	// Miss shader table
	{
		UINT numShaderRecords = 1;
		UINT shaderRecordSize = shaderIdentifierSize;
		ShaderTable missShaderTable (device, numShaderRecords, shaderRecordSize, L"MissShaderTable");
		missShaderTable.Push (ShaderRecord (missShaderIdentifier, shaderIdentifierSize));
		m_MissShaderTable = missShaderTable.GetResource ();
	}

	// Hit group shader table
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

void DXRaytracingSimpleLighting::OnUpdate () {
	m_Timer.Tick ();
	CalculateFrameStats ();
	float elapsedTime = static_cast<float>(m_Timer.GetElapsedSeconds ());
	auto frameIndex = m_DeviceResources->GetCurrentFrameIndex ();
	auto prevFrameIndex = m_DeviceResources->GetPreviousFrameIndex ();

	// Rotate the camera around Y axis.
	{
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

void DXRaytracingSimpleLighting::DoRaytracing () {
	auto commandList = m_DeviceResources->GetCommandList ();
	auto frameIndex = m_DeviceResources->GetCurrentFrameIndex ();

	auto DispatchRays = [&](auto* commandList, auto* stateObject, auto* dispatchDesc) {
		// Since each shader table has only one shader record, the stride is same as the size.
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
		// Set index and successive vertex buffer decriptor tables
		commandList->SetComputeRootDescriptorTable (GlobalRootSignatureParams::VertexBuffersSlot, m_IndexBuffer.GpuDescriptorHandle);
		commandList->SetComputeRootDescriptorTable (GlobalRootSignatureParams::OutputViewSlot, m_RaytracingOutputResourceUAVGpuDescriptor);
	};

	commandList->SetComputeRootSignature (m_RaytracingGlobalRootSignature.Get ());

	// Copy the updated scene constant buffer to GPU.
	memcpy (&m_MappedConstantData[frameIndex].Constants, &m_SceneCB[frameIndex], sizeof (m_SceneCB[frameIndex]));
	auto cbGpuAddress = m_PerFrameConstants->GetGPUVirtualAddress () + frameIndex * sizeof (m_MappedConstantData[0]);
	commandList->SetComputeRootConstantBufferView (GlobalRootSignatureParams::SceneConstantSlot, cbGpuAddress);

	// Bind the heaps, acceleration structure and dispatch rays.
	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
	SetCommonPipelineState (commandList);
	commandList->SetComputeRootShaderResourceView (GlobalRootSignatureParams::AccelerationStructureSlot, m_TopLevelAccelerationStructure->GetGPUVirtualAddress ());
	DispatchRays (m_DxrCommandList.Get (), m_DxrStateObject.Get (), &dispatchDesc);
}

void DXRaytracingSimpleLighting::UpdateForSizeChange (UINT width, UINT height) {
	DXSample::UpdateForSizeChange (width, height);
}

void DXRaytracingSimpleLighting::CopyRaytracingOutputToBackBuffer () {
	auto commandList = m_DeviceResources->GetCommandList ();
	auto renderTarget = m_DeviceResources->GetRenderTarget ();

	D3D12_RESOURCE_BARRIER preCopyBarriers[2];
	preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition (renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
	preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition (m_RaytracingOutput.Get (), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	commandList->ResourceBarrier (ARRAYSIZE (preCopyBarriers), preCopyBarriers);

	commandList->CopyResource (renderTarget, m_RaytracingOutput.Get ());

	D3D12_RESOURCE_BARRIER postCopyBarriers[2];
	postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition (renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
	postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition (m_RaytracingOutput.Get (), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	commandList->ResourceBarrier (ARRAYSIZE (postCopyBarriers), postCopyBarriers);
}

void DXRaytracingSimpleLighting::CreateWindowSizeDependentResources () {
	CreateRaytracingOutputResource ();
	UpdateCameraMatrices ();
}

void DXRaytracingSimpleLighting::ReleaseDeviceDependentResources () {
	m_RaytracingOutput.Reset ();
}

void DXRaytracingSimpleLighting::ReleaseWindowSizeDependentResources () {
    m_RaytracingGlobalRootSignature.Reset ();
	m_RaytracingLocalRootSignature.Reset ();

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

void DXRaytracingSimpleLighting::RecreateD3D () {
	try {
		m_DeviceResources->WaitForGpu ();
	} catch (HrException&) {

	}

	m_DeviceResources->HandleDeviceLost ();
}

void DXRaytracingSimpleLighting::OnRender () {
	if (!m_DeviceResources->IsWindowVisible ()) {
		return;
	}

	m_DeviceResources->Prepare ();
	DoRaytracing ();
	CopyRaytracingOutputToBackBuffer ();

	m_DeviceResources->Present (D3D12_RESOURCE_STATE_PRESENT);
}

void DXRaytracingSimpleLighting::OnDestroy () {
	m_DeviceResources->WaitForGpu ();
	OnDeviceLost ();
}

void DXRaytracingSimpleLighting::OnDeviceLost () {
	ReleaseWindowSizeDependentResources ();
	ReleaseDeviceDependentResources ();
}

void DXRaytracingSimpleLighting::OnDeviceRestored () {
	CreateDeviceDependentResources ();
	CreateWindowSizeDependentResources ();
}

void DXRaytracingSimpleLighting::CalculateFrameStats () {
	static int frameCnt = 0;
	static double elapsedTime = 0.0f;
	double totalTime = m_Timer.GetTotalSeconds ();
	frameCnt++;

	if ((totalTime - elapsedTime) >= 1.0f) {
		float diff = static_cast<float> (totalTime - elapsedTime);
		float fps = static_cast<float> (frameCnt) / diff;

		frameCnt = 0;
		elapsedTime = totalTime;

		float mRaysPerSecond = (m_Width * m_Height * fps) / static_cast<float> (1e6);

		wstringstream windowText;

		windowText << setprecision (2) << fixed
			<< L"    fps: " << fps << L"     ~Million Primary Rays/s: " << mRaysPerSecond
			<< L"    GPU[" << m_DeviceResources->GetAdapterID () << L"]: " << m_DeviceResources->GetAdapterDescription ();

		SetCustomWindowText (windowText.str ().c_str ());
	}
}

void DXRaytracingSimpleLighting::OnSizeChanged (UINT width, UINT height, bool minimized) {
	if (!m_DeviceResources->WindowSizeChanged (width, height, minimized)) {
		return;
	}

	UpdateForSizeChange (width, height);

	ReleaseWindowSizeDependentResources ();
	CreateWindowSizeDependentResources ();
}

UINT DXRaytracingSimpleLighting::AllocateDescriptor (D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse) {
	auto descriptorHeapCpuBase = m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart ();
	if (descriptorIndexToUse >= m_DescriptorHeap->GetDesc ().NumDescriptors) {
		descriptorIndexToUse = m_DescriptorsAllocated++;
	}
	*cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE (descriptorHeapCpuBase, descriptorIndexToUse, m_DescriptorSize);
	return descriptorIndexToUse;
}

UINT DXRaytracingSimpleLighting::CreateBufferSRV (D3DBuffer* buffer, UINT numElements, UINT elementSize) {
	auto device = m_DeviceResources->GetD3DDevice ();

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