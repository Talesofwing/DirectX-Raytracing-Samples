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

class DXRaytracingLibrarySubobjects : public DXSample {
public:
	DXRaytracingLibrarySubobjects (UINT width, UINT height, std::wstring name);

	virtual void OnDeviceLost () override;
	virtual void OnDeviceRestored () override;

	virtual void OnInit ();
	virtual void OnUpdate ();
	virtual void OnRender ();
	virtual void OnSizeChanged (UINT width, UINT height, bool minimized);
	virtual void OnDestroy ();
	virtual IDXGISwapChain* GetSwapChain () { return m_DeviceResources->GetSwapChain (); }

private:
	static const UINT FrameCount = 3;

	static_assert(sizeof (SceneConstantBuffer) < D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "Checking the size here.");

	union AlignedSceneConstantBuffer {
		SceneConstantBuffer constants;
		uint8_t alignmentPadding[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT];
	};
	AlignedSceneConstantBuffer* m_MappedConstantData;
	ComPtr<ID3D12Resource> m_PerFrameConstants;

	ComPtr<ID3D12Device5> m_DxrDevice;
	ID3D12GraphicsCommandList5* m_DxrCommandList;
	ComPtr<ID3D12StateObject> m_DxrStateObject;
	bool m_IsDxrSupported;

	ComPtr<ID3D12RootSignature> m_RaytracingGlobalRootSignature;

	ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;
	UINT m_DescriptorsAllocated = 0;
	UINT m_DescriptorSize;

	SceneConstantBuffer m_SceneCB[FrameCount];
	CubeConstantBuffer m_CubeCB;

	struct D3DBuffer {
		ComPtr<ID3D12Resource> Resource;
		D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptorHandle;
		D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptorHandle;
	};
	D3DBuffer m_IndexBuffer;
	D3DBuffer m_VertexBuffer;

	ComPtr<ID3D12Resource> m_BottomLevelAccelerationStructure;
	ComPtr<ID3D12Resource> m_TopLevelAccelerationStructure;

	ComPtr<ID3D12Resource> m_RaytracingOutput;
	D3D12_GPU_DESCRIPTOR_HANDLE m_RaytracingOutputResourceUAVGpuDescriptor;
	UINT m_RaytracingOutputResourceUAVDescriptorHeapIndex;

	static const wchar_t* c_HitGroupName;
	static const wchar_t* c_RayGenShaderName;
	static const wchar_t* c_ClosestHitShaderName;
	static const wchar_t* c_MissShaderName;
	ComPtr<ID3D12Resource> m_MissShaderTable;
	ComPtr<ID3D12Resource> m_HitGroupShaderTable;
	ComPtr<ID3D12Resource> m_RayGenShaderTable;

	static const wchar_t* c_GlobalRootSignatureName;
	static const wchar_t* c_LocalRootSignatureName;
	static const wchar_t* c_LocalRootSignatureAssociationName;
	static const wchar_t* c_ShaderConfigName;
	static const wchar_t* c_PipelineConfigName;

	StepTimer m_Timer;
	float m_CurRotationAngleRad;
	XMVECTOR m_Eye;
	XMVECTOR m_At;
	XMVECTOR m_Up;

	void EnableDirectXRaytracing (IDXGIAdapter1* adapter);
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
	UINT CreateBufferSRV (D3DBuffer* buffer, UINT numElemtns, UINT elementSize);
};


