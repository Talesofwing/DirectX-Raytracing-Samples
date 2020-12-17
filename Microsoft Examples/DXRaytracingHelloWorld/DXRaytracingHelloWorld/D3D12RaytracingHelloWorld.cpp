#include "stdafx.h"
#include "D3D12RaytracingHelloWorld.h"
#include "DirectXRaytracingHelper.h"
#include "CompiledShaders\Raytracing.hlsl.h"

using namespace std;
using namespace DX;

const wchar_t* D3D12RaytracingHelloWorld::c_HitGroupName = L"MyHitGroup";
const wchar_t* D3D12RaytracingHelloWorld::c_RayGenShaderName = L"MyRaygenShader";
const wchar_t* D3D12RaytracingHelloWorld::c_ClosestHitShaderName = L"MyClosestHitShader";
const wchar_t* D3D12RaytracingHelloWorld::c_MissShaderName = L"MyMissShader";

D3D12RaytracingHelloWorld::D3D12RaytracingHelloWorld (UINT width, UINT height, std::wstring name) :
	DXSample (width, height, name), m_RaytracingOutputResourceUAVDescriptorHeapIndex (UINT_MAX) {

	m_RayGenCB.viewport = {-1.0f, -1.0f, 1.0f, 1.0f};
	UpdateForSizeChange (width, height);
}

void D3D12RaytracingHelloWorld::OnInit () {
	m_DeviceResources = std::make_unique<DeviceResources> (
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_UNKNOWN,
		FrameCount,
		D3D_FEATURE_LEVEL_11_0,
		// Sample shows handling of use cases with tearing support, which is OS dependent and has been supported since TH2.
		// Since the sample requires build 1809 (RS5) or higher, we don't need to handle non-tearing cases.
		DeviceResources::c_RequireTearingSupport,
		m_AdapterIDoverride
		);
	m_DeviceResources->RegisterDeviceNotify (this);
	m_DeviceResources->SetWindow (Win32Application::GetHwnd (), m_Width, m_Height);
	m_DeviceResources->InitializeDXGIAdapater ();

	ThrowIfFalse (IsDirectXRaytracingSupported (m_DeviceResources->GetAdapter ()),
		L"ERROR: DirectX Raytracing is not supported by your OS, GPU and/or driver.\n\n");

	m_DeviceResources->CreateDeviceResources ();
	m_DeviceResources->CreateWindowSizeDependentResources ();

	CreateDeviceDependentResources ();
	CreateWindowSizeDependentResources ();
}

// Create resources that depend on the device.
void D3D12RaytracingHelloWorld::CreateDeviceDependentResources () {
	// Initialize raytracing pipeline.

	// Create raytracing interfaces: raytracing device and commandList.
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

	// Build shader tables, which define shaders and their local orot arguments.
	//BuildShaderTables ();

	// Create an output 2D texture to store the raytracing result to.
	//CreateRaytracingOutputResource ();
}

void D3D12RaytracingHelloWorld::SerializeAndCreateRaytracingRootSignature (D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig) {
	auto device = m_DeviceResources->GetDevice ();
	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> error;

	ThrowIfFailed (D3D12SerializeRootSignature (&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*> (error->GetBufferPointer ()) : nullptr);
	ThrowIfFailed (device->CreateRootSignature (1, blob->GetBufferPointer (), blob->GetBufferSize (), IID_PPV_ARGS (&(*rootSig))));
}

void D3D12RaytracingHelloWorld::CreateRootSignatures () {
	// Global Root signature
	// This is a root signature that is shared across all raytracing shaders invoked durting a DispatchRays() call.
	{
		CD3DX12_DESCRIPTOR_RANGE UAVDescriptor;
		UAVDescriptor.Init (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count];
		rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable (1, &UAVDescriptor);
		rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView (0);
		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc (ARRAYSIZE (rootParameters), rootParameters);
		SerializeAndCreateRaytracingRootSignature (globalRootSignatureDesc, &m_RaytracingGlobalRootSignature);
	}

	// Local Root Signature
	// This is a root signature that enables a shader to have unique arguments that come from shader tables.
	{
		CD3DX12_ROOT_PARAMETER rootParameters[LocalRootSignatureParams::Count];
		rootParameters[LocalRootSignatureParams::ViewportConstantSlot].InitAsConstants (SizeOfInUint32 (m_RayGenCB), 0, 0);
		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc (ARRAYSIZE (rootParameters), rootParameters);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		SerializeAndCreateRaytracingRootSignature (localRootSignatureDesc, &m_RaytracingLocalRootSignature);
	}
}

// Create raytracing device and command list.
void D3D12RaytracingHelloWorld::CreateRaytracingInterfaces () {
	auto device = m_DeviceResources->GetDevice ();
	auto commandList = m_DeviceResources->GetCommandList ();

	ThrowIfFailed (device->QueryInterface (IID_PPV_ARGS (&m_DxrDevice)), L"Couldn't get DirectX Raytracing interface for the device.");
	ThrowIfFailed (commandList->QueryInterface (IID_PPV_ARGS (&m_DxrCommandList)), L"Couldn't get DirectX Raytracing interface for the command list.");
}

// Local root signature and shader association
// This is a root signature that enables a shader to have unique arguments that come from shader tables.
void D3D12RaytracingHelloWorld::CreateLocalRootSignaturesSubobjects (CD3DX12_STATE_OBJECT_DESC* raytracingPipeline) {
	// Hit group and miss shaders in this sample are not using a local root signature and thus one is not associated with them.
	
	// Local root signature to be used in a ray gen shader.
	{
		auto localRootSignature = raytracingPipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT> ();
		localRootSignature->SetRootSignature (m_RaytracingLocalRootSignature.Get ());
		// Shader association
		auto rootSignatureAssociation = raytracingPipeline->CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT> ();
		rootSignatureAssociation->SetSubobjectToAssociate (*localRootSignature);
		rootSignatureAssociation->AddExport (c_RayGenShaderName);
	}
}

// Create a raytracing pipeline state object (RTPSO).
// An RTPSO represents a full set of shaders reachable by a DispatchRays () call,
// with all configuration options resolved, such as local signatures and other state.
void D3D12RaytracingHelloWorld::CreateRaytracingPipelineStateObject () {
	// Create 7 subobjects that combine into a RTPSO:
	// Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
	// Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
	// This simple sample utilizes default shader association except for local root signature subobject
	// which has an explicit association specified purely for demonstration purposes.
	// 1 - DXIL library
	// 1 - Triangle hit group
	// 1 - Shader config
	// 2 - Local root signature and association
	// 1 - Global root signature
	// 1 - Pipeline config
	CD3DX12_STATE_OBJECT_DESC raytracingPipeline {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};
	
	// DXIL library
	// This contains the shaders and their entrypoints for the state object.
	// Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
	auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT> ();
	D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE ((void*)g_pRaytracing, ARRAYSIZE (g_pRaytracing));
	lib->SetDXILLibrary (&libdxil);
	// Define which shader exports to surface from the library.
	// If no shader exports are defined for a DXIL library subobject, all shaders will be surfaces.
	// In this sample, this could be omitted for convenience since the sample uses all shaders in the library.
	{
		lib->DefineExport (c_RayGenShaderName);
		lib->DefineExport (c_ClosestHitShaderName);
		lib->DefineExport (c_MissShaderName);
	}

	// Triangle hit group
	// A hit group specifies closest hit, any hit and intersection shaders to be executed when a ray intersects the geometry's triangle/AABB.
	// In this sample, we only use triangle geometry with a closest hit shader, so others are not set.
	auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT> ();
	hitGroup->SetClosestHitShaderImport (c_ClosestHitShaderName);
	hitGroup->SetHitGroupExport (c_HitGroupName);
	hitGroup->SetHitGroupType (D3D12_HIT_GROUP_TYPE_TRIANGLES);

	// Shader config
	// Defines the maximun sizes in bytes for the ray payload and attribute structure.
	auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT> ();
	UINT payloadSize = 4 * sizeof (float);		// float4 color
	UINT attributeSize = 2 * sizeof (float);	// float2 barycentrics
	shaderConfig->Config (payloadSize, attributeSize);

	// Local root signature and shader association
	CreateLocalRootSignaturesSubobjects (&raytracingPipeline);
	// This is a root signature that enables a shader to have unique arguments that come from shader tables.

	// Global root signature
	// This is a root signature that is shared across all raytracing shaders invoked durting a DispatchRays () call.
	auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT> ();
	globalRootSignature->SetRootSignature (m_RaytracingGlobalRootSignature.Get ());

	// Pipeline config
	// Defines the maximnu TraceRay() recursion depth.
	auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT> ();
	// PERFOMANCE TIP: Set max recursion depth as low as needed
	// as drivers may apply optimization strategies for low recursion depths.
	UINT maxRecursionDepth = 1;	// ~primary rays only.
	pipelineConfig->Config (maxRecursionDepth);

#if _DEBUG
	PrintStateObjectDesc (raytracingPipeline);
#endif

	// Create the state object.
	ThrowIfFailed (m_DxrDevice->CreateStateObject (raytracingPipeline, IID_PPV_ARGS (&m_DxrStateObject)), L"Couldn't create DirectX Raytracing state object.\n");
}

void D3D12RaytracingHelloWorld::CreateRaytracingOutputResource () {
	auto device = m_DeviceResources->GetDevice ();
	auto backBufferFormat = m_DeviceResources->GetBackBufferFormat ();

	// Create the output resource. The dimensions and format sould match the swap-chain.
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
	NAME_D3D12_OBJECT (m_RaytracingOutput);

	D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
	m_RaytracingOutputResourceUAVDescriptorHeapIndex = AllocateDescriptor (&uavDescriptorHandle, m_RaytracingOutputResourceUAVDescriptorHeapIndex);
	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	device->CreateUnorderedAccessView (m_RaytracingOutput.Get (), nullptr, &UAVDesc, uavDescriptorHandle);
	m_RaytracingOutputResourceUAVGpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE (m_DescriptorHeap->GetGPUDescriptorHandleForHeapStart (),
		m_RaytracingOutputResourceUAVDescriptorHeapIndex, m_DescriptorSize);
}

void D3D12RaytracingHelloWorld::CreateDescriptorHeap () {
	auto device = m_DeviceResources->GetDevice ();

	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
	// Allocate a heap for a single descriptor:
	// 1 - raytracing output texture UAV
	descriptorHeapDesc.NumDescriptors = 1;
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descriptorHeapDesc.NodeMask = 0;
	device->CreateDescriptorHeap (&descriptorHeapDesc, IID_PPV_ARGS (&m_DescriptorHeap));
	NAME_D3D12_OBJECT (m_DescriptorHeap);

	m_DescriptorSize = device->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

// Build geometry used in the sample.
void D3D12RaytracingHelloWorld::BuildGeometry () {
	auto device = m_DeviceResources->GetDevice ();
	Index indices[] = {
		0, 1, 2
	};

	float depthValue = 1.0;
	float offset = 0.7f;
	Vertex vertices[] = {
		// The sample raytraces in screen space coordinates.
		// Since DirectX screen space coordinates are right handed (i.e. Y axis points down).
		// Define the vertices in counter clockwise order ~ clockwise in left handed.
		{0, -offset, depthValue},
		{-offset, offset, depthValue},
		{offset, offset, depthValue}
	};

	AllocateUploadBuffer (device, vertices, sizeof (vertices), &m_VertexBuffer);
	AllocateUploadBuffer (device, indices, sizeof (indices), &m_IndexBuffer);
}

// Build acceleration structures needed for raytracing.
void D3D12RaytracingHelloWorld::BuildAccelerationStructures () {
	auto device = m_DeviceResources->GetDevice ();
	auto commandList = m_DeviceResources->GetCommandList ();
	auto commandQueue = m_DeviceResources->GetCommandQueue ();
	auto commandAllocator = m_DeviceResources->GetCommandAllocator ();

	// Reset the command list for the acceleration structure construction.
	commandList->Reset (commandAllocator, nullptr);

	D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
	geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometryDesc.Triangles.IndexBuffer = m_IndexBuffer->GetGPUVirtualAddress ();
	geometryDesc.Triangles.IndexCount = static_cast<UINT> (m_IndexBuffer->GetDesc ().Width) / sizeof (Index);
	geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
	geometryDesc.Triangles.Transform3x4 = 0;
	geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geometryDesc.Triangles.VertexCount = static_cast<UINT> (m_VertexBuffer->GetDesc ().Width) / sizeof (Vertex);
	geometryDesc.Triangles.VertexBuffer.StartAddress = m_VertexBuffer->GetGPUVirtualAddress ();
	geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof (Vertex);

	// Make the geometry as opaque.
	// PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
	// Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
	geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	// Get required sizes for an acceleration structure.
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	topLevelInputs.Flags = buildFlags;
	topLevelInputs.NumDescs = 1;
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};
	m_DxrDevice->GetRaytracingAccelerationStructurePrebuildInfo (&topLevelInputs, &topLevelPrebuildInfo);
	ThrowIfFalse (topLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelPrebuildInfo = {};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs = topLevelInputs;
	bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	bottomLevelInputs.pGeometryDescs = &geometryDesc;
	m_DxrDevice->GetRaytracingAccelerationStructurePrebuildInfo (&bottomLevelInputs, &bottomLevelPrebuildInfo);
	ThrowIfFalse (bottomLevelPrebuildInfo.ResultDataMaxSizeInBytes > 0);

	ComPtr<ID3D12Resource> scratchResource;
	AllocateUAVBuffer (device, max (topLevelPrebuildInfo.ScratchDataSizeInBytes, bottomLevelPrebuildInfo.ScratchDataSizeInBytes), &scratchResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"ScratchResource");
	
	// Allocate resources for acceleration structures.
	// Acceleration structures can only be placed in resources that are created in the default heap (or custom heap equivalent). 
	// Default heap is OK since the application doesn’t need CPU read/write access to them. 
	// The resources that will contain acceleration structures must be created in the state D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, 
	// and must have resource flag D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS. The ALLOW_UNORDERED_ACCESS requirement simply acknowledges both: 
	//  - the system will be doing this type of access in its implementation of acceleration structure builds behind the scenes.
	//  - from the app point of view, synchronization of writes/reads to acceleration structures is accomplished using UAV barriers.
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
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
	{
		bottomLevelBuildDesc.Inputs = bottomLevelInputs;
		bottomLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress ();
		bottomLevelBuildDesc.DestAccelerationStructureData = m_BottomLevelAccelerationStructure->GetGPUVirtualAddress ();
	}

	// Top Level Acceleration Structure desc
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
	{
		topLevelInputs.InstanceDescs = instanceDescs->GetGPUVirtualAddress ();
		topLevelBuildDesc.Inputs = topLevelInputs;
		topLevelBuildDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress ();
		topLevelBuildDesc.DestAccelerationStructureData = m_TopLevelAccelerationStructure->GetGPUVirtualAddress ();
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

// Build shader tables.
// This encapsulates all shader records - shaders and the arguments for their local root signatures.
void D3D12RaytracingHelloWorld::BuildShaderTables () {
	auto device = m_DeviceResources->GetDevice ();

	void* rayGenShaderIdentifier;
	void* missShaderIdentifier;
	void* hitGroupShaderIdentifier;

	auto GetShaderIdentifiers = [&](auto* stateObjectProperties) {
		rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier (c_RayGenShaderName);
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
			RayGenConstantBuffer cb;
		} rootArguments;
		rootArguments.cb = m_RayGenCB;

		UINT numShaderRecords = 1;
		UINT shaderRecordSize = shaderIdentifierSize + sizeof (rootArguments);
		ShaderTable rayGenShaderTable (device, numShaderRecords, shaderRecordSize, L"RayGenShaderTable");
		rayGenShaderTable.push_back (ShaderRecord (rayGenShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof (rootArguments)));
		m_RayGenShaderTable = rayGenShaderTable.GetResource ();
	}

	// Miss shader table
	{
		UINT numShaderRecords = 1;
		UINT shaderRecordSize = shaderIdentifierSize;
		ShaderTable missShaderTable (device, numShaderRecords, shaderRecordSize, L"MissShaderTable");
		missShaderTable.push_back (ShaderRecord (missShaderIdentifier, shaderIdentifierSize));
		m_MissShaderTable = missShaderTable.GetResource ();
	}

	// Hit group shader table
	{
		UINT numShaderRecords = 1;
		UINT shaderRecordSize = shaderIdentifierSize;
		ShaderTable hitGroupShaderTable (device, numShaderRecords, shaderRecordSize, L"HitGroupShaderTable");
		hitGroupShaderTable.push_back (ShaderRecord (hitGroupShaderIdentifier, shaderIdentifierSize));
		m_HitGroupShaderTable = hitGroupShaderTable.GetResource ();
	}
}

// Update frame-based values.
void D3D12RaytracingHelloWorld::OnUpdate () {
	m_Timer.Tick ();
	CalculateFrameStats ();
}

void D3D12RaytracingHelloWorld::DoRaytracing () {
	auto commandList = m_DeviceResources->GetCommandList ();

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

	commandList->SetComputeRootSignature (m_RaytracingGlobalRootSignature.Get ());

	// Bind the heaps, acceleration structrure and dispatch rays.
	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
	commandList->SetDescriptorHeaps (1, m_DescriptorHeap.GetAddressOf ());
	commandList->SetComputeRootDescriptorTable (GlobalRootSignatureParams::OutputViewSlot, m_RaytracingOutputResourceUAVGpuDescriptor);
	commandList->SetComputeRootShaderResourceView (GlobalRootSignatureParams::AccelerationStructureSlot, m_TopLevelAccelerationStructure->GetGPUVirtualAddress ());
	DispatchRays (m_DxrCommandList.Get (), m_DxrStateObject.Get (), &dispatchDesc);
}

// Update the application state with the new resolution.
void D3D12RaytracingHelloWorld::UpdateForSizeChange (UINT width, UINT height) {
	DXSample::UpdateForSizeChange (width, height);
	float border = 0.1f;
	if (m_Width <= m_Height) {
		m_RayGenCB.stencil = {
			-1 + border, -1 + border * m_AspectRatio,
			1.0f - border, 1 - border * m_AspectRatio
		};
	} else {
		m_RayGenCB.stencil = {
			-1 + border / m_AspectRatio, -1 + border,
			 1 - border / m_AspectRatio, 1.0f - border
		};
	}
}

// Copy the ratracing output to the backbuffer.
void D3D12RaytracingHelloWorld::CopyRayTracingOutputToBackBuffer () {
	auto commandList = m_DeviceResources->GetCommandList ();
	auto renderTarget = m_DeviceResources->GetRenderTarget ();

	D3D12_RESOURCE_BARRIER preCopyBarriers[2];
	preCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition (renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COPY_DEST);
	preCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition (m_RaytracingOutput.Get (), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	commandList->ResourceBarrier (ARRAYSIZE (preCopyBarriers), preCopyBarriers);

	commandList->CopyResource (renderTarget, m_RaytracingOutput.Get ());

	D3D12_RESOURCE_BARRIER postCopyBarriers[2];
	postCopyBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition (renderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
	postCopyBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition (m_RaytracingOutput.Get (), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	commandList->ResourceBarrier (ARRAYSIZE (postCopyBarriers), postCopyBarriers);
}

// Create resources that are dependent on the size of the main window.
void D3D12RaytracingHelloWorld::CreateWindowSizeDependentResources () {
	CreateRaytracingOutputResource ();

	// For simplicity, we will rebuild the shader tables.
	BuildShaderTables ();
}

// Release resources that are dependent on the size of the main window
void D3D12RaytracingHelloWorld::ReleaseWindowSizeDependentResources () {
	m_RayGenShaderTable.Reset ();
	m_MissShaderTable.Reset ();
	m_HitGroupShaderTable.Reset ();
	m_RaytracingOutput.Reset ();
}

// Release all resources that depend on the device.
void D3D12RaytracingHelloWorld::ReleaseDeviceDependentResources () {
	m_RaytracingGlobalRootSignature.Reset ();
	m_RaytracingLocalRootSignature.Reset ();

	m_DxrDevice.Reset ();
	m_DxrCommandList.Reset ();
	m_DxrStateObject.Reset ();

	m_DescriptorHeap.Reset ();
	m_DescriptorsAllocated = 0;
	m_RaytracingOutputResourceUAVDescriptorHeapIndex = UINT_MAX;
	m_IndexBuffer.Reset ();
	m_VertexBuffer.Reset ();

	m_AccelerationStructure.Reset ();
	m_BottomLevelAccelerationStructure.Reset ();
	m_TopLevelAccelerationStructure.Reset ();
}

void D3D12RaytracingHelloWorld::RecreateD3D () {
	// Give GPU a chance to finish its execution in progress.
	try {
		m_DeviceResources->WaitForGpu ();
	} catch (HrException&) {
		// Do nothing, currently attached adapter is unresponsive.
	}
	m_DeviceResources->HandleDeviceLost ();
}

// Render the scene.
void D3D12RaytracingHelloWorld::OnRender () {
	if (!m_DeviceResources->IsWindowVisible ()) {
		return;
	}

	m_DeviceResources->Prepare ();
	DoRaytracing ();
	CopyRayTracingOutputToBackBuffer ();

	m_DeviceResources->Present (D3D12_RESOURCE_STATE_PRESENT);
}

void D3D12RaytracingHelloWorld::OnDestroy () {
	// Let GPU finish before releasing D3D resources.
	m_DeviceResources->WaitForGpu ();
	OnDeviceLost ();
}

// Release all device dependent resources when a device is lost
void D3D12RaytracingHelloWorld::OnDeviceLost () {
	ReleaseWindowSizeDependentResources ();
	ReleaseDeviceDependentResources ();
}

// Create all device dependent resources when a device is restored.
void D3D12RaytracingHelloWorld::OnDeviceRestored () {
	CreateDeviceDependentResources ();
	CreateWindowSizeDependentResources ();
}

// Compute the average frames per second and million rays per second.
void D3D12RaytracingHelloWorld::CalculateFrameStats () {
	static int frameCnt = 0;
	static double elapsedTime = 0.0f;
	double totalTime = m_Timer.GetTotalSeconds ();
	frameCnt++;

	// Compute averages over one second period.
	if ((totalTime - elapsedTime) >= 1.0f) {
		float diff = static_cast<float> (totalTime - elapsedTime); 
		float fps = static_cast<float> (frameCnt) / diff;	// Normalize to an exact second.

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

// Handle OnSizeChanged message event.
void D3D12RaytracingHelloWorld::OnSizeChanged (UINT width, UINT height, bool minimized) {
	if (!m_DeviceResources->WindowSizeChagned (width, height, minimized)) {
		return;
	}

	UpdateForSizeChange (width, height);

	ReleaseWindowSizeDependentResources ();
	CreateWindowSizeDependentResources ();
}

// Allocate a descriptor and return its index.
// If the passed descriptorIndexToUse is valid, it will be used instead of allocating a new one.
UINT D3D12RaytracingHelloWorld::AllocateDescriptor (D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse) {
	auto descriptorHeapCpuBase = m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart ();
	if (descriptorIndexToUse >= m_DescriptorHeap->GetDesc ().NumDescriptors) {
		descriptorIndexToUse = m_DescriptorsAllocated++;
	}
	*cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE (descriptorHeapCpuBase, descriptorIndexToUse, m_DescriptorSize);
	return descriptorIndexToUse;
}