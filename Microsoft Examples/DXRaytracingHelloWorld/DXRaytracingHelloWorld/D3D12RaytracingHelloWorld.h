#pragma once

#include "DXSample.h"
#include "StepTimer.h"
#include "RaytracingHlslCompat.h"

namespace GlobalRootSignatureParams {
	enum Value {
		OutputViewSlot = 0,
		AccelerationStructureSlot,
		Count
	};
}

namespace LocalRootSignatureParams {
	enum Value {
		ViewportConstantSlot = 0,
		Count
	};
}

class D3D12RaytracingHelloWorld : public DXSample {
public:
	D3D12RaytracingHelloWorld (UINT width, UINT height, std::wstring name);

	// IDeviceNotify
	virtual void OnDeviceLost () override;
	virtual void OnDeviceRestored () override;

	// Message
	virtual void OnInit ();
	virtual void OnUpdate ();
	virtual void OnRender ();
	virtual void OnSizeChanged (UINT width, UINT height, bool minimized);
	virtual void OnDestroy ();
	virtual IDXGISwapChain* GetSwapChain () { return m_DeviceResources->GetSwapChain (); }

private:
	static const UINT FrameCount = 3;

	// DirectX Raytracing (DXR) attributes
	ComPtr<ID3D12Device5> m_DxrDevice;
	ComPtr<ID3D12GraphicsCommandList4> m_DxrCommandList;
	ComPtr<ID3D12StateObject> m_DxrStateObject;

	// Root signatures
	ComPtr<ID3D12RootSignature> m_RaytracingGlobalRootSignature;
	ComPtr<ID3D12RootSignature> m_RaytracingLocalRootSignature;

	// Descriptors
	ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;
	UINT m_DescriptorsAllocated = 0;
	UINT m_DescriptorSize;

	// Raytracing scene
	RayGenConstantBuffer m_RayGenCB;

	// Geometry
	typedef UINT16 Index;
	struct Vertex { float v1, v2, v3; };
	ComPtr<ID3D12Resource> m_IndexBuffer;
	ComPtr<ID3D12Resource> m_VertexBuffer;

	// Acceleration structure
	ComPtr<ID3D12Resource> m_AccelerationStructure;
	ComPtr<ID3D12Resource> m_BottomLevelAccelerationStructure;
	ComPtr<ID3D12Resource> m_TopLevelAccelerationStructure;

	// Raytracing output
	ComPtr<ID3D12Resource> m_RaytracingOutput;
	D3D12_GPU_DESCRIPTOR_HANDLE m_RaytracingOutputResourceUAVGpuDescriptor;
	UINT m_RaytracingOutputResourceUAVDescriptorHeapIndex;

	// Shader tables
	static const wchar_t* c_HitGroupName;
	static const wchar_t* c_RayGenShaderName;
	static const wchar_t* c_ClosestHitShaderName;
	static const wchar_t* c_MissShaderName;
	ComPtr<ID3D12Resource> m_MissShaderTable;
	ComPtr<ID3D12Resource> m_HitGroupShaderTable;
	ComPtr<ID3D12Resource> m_RayGenShaderTable;

	// Application state
	StepTimer m_Timer;

	void RecreateD3D ();
	void DoRaytracing ();
	void CreateDeviceDependentResources ();
	void CreateWindowSizeDependentResources ();
	void ReleaseDeviceDependentResources ();
	void ReleaseWindowSizeDependentResources ();
	void CreateRaytracingInterfaces ();
	void SerializeAndCreateRaytracingRootSignature (D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig);
	void CreateRootSignatures ();
	void CreateLocalRootSignaturesSubobjects (CD3DX12_STATE_OBJECT_DESC* raytracingPipeline);
	void CreateRaytracingPipelineStateObject ();
	void CreateDescriptorHeap ();
	void CreateRaytracingOutputResource ();
	void BuildGeometry ();
	void BuildAccelerationStructures ();
	void BuildShaderTables ();
	void UpdateForSizeChange (UINT clientWidth, UINT clientHeight);
	void CopyRayTracingOutputToBackBuffer ();
	void CalculateFrameStats ();
	UINT AllocateDescriptor (D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse = UINT_MAX);
};

