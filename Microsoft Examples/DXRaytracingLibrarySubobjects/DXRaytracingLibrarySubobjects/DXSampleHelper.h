#pragma once

using Microsoft::WRL::ComPtr;

class HrException : public std::runtime_error {
	inline std::string HrToString (HRESULT hr) {
		char s_str[64] = {};
		sprintf_s (s_str, "HRESULT of 0x%08x", static_cast<UINT> (hr));
		return std::string (s_str);
	}
public:
	HrException (HRESULT hr) : std::runtime_error (HrToString (hr)), m_Hr (hr) {}
	HRESULT Error () const { return m_Hr; }
private:
	const HRESULT m_Hr;
};

#define SAFE_RELEASE(p) if (p) (p)->Release ()

inline void ThrowIfFailed (HRESULT hr) {
	if (FAILED (hr)) {
		throw HrException (hr);
	}
}

inline void ThrowIfFailed (HRESULT hr, const wchar_t* msg) {
	if (FAILED (hr)) {
		OutputDebugString (msg);
		throw HrException (hr);
	}
}

inline void ThrowIfFalse (bool value) {
	ThrowIfFailed (value ? S_OK : E_FAIL);
}

inline void ThrowIfFalse (bool value, const wchar_t* msg) {
	ThrowIfFailed (value ? S_OK : E_FAIL, msg);
}

inline void GetAssetsPath (_Out_writes_ (pathSize) WCHAR* path, UINT pathSize) {
	if (path == nullptr) {
		throw std::exception ();
	}

	DWORD size = GetModuleFileName (nullptr, path, pathSize);
	if (size == 0 || size == pathSize) {
		throw std::exception ();
	}

	WCHAR* lastSlash = wcsrchr (path, L'\\');
	if (lastSlash) {
		*(lastSlash + 1) = L'\0';
	}
}

#if defined (_DEBUG) || defined (DBG)
inline void SetName (ID3D12Object* pObject, LPCWSTR name) {
	pObject->SetName (name);
}

inline void SetNameIndexed (ID3D12Object* pObject, LPCWSTR name, UINT index) {
	WCHAR fullName[50];
	if (swprintf_s (fullName, L"%s[%u]", name, index) > 0) {
		pObject->SetName (fullName);
	}
}
#else
inline void SetName (ID3D12Object* pObject, LPCWSTR) {}
inline void SetNameIndexed (ID3D12Object*, LPCWSTR, UINT) {}
#endif

#define NAME_D3D12_OBJECT(x) SetName((x).Get (), L#x)
#define NAME_D3D12_OBJECT_INDEXED(x, n) SetNameIndexed((x)[n].Get (), L#x, n)

inline UINT Align (UINT size, UINT alignment) {
	return (size + (alignment - 1)) & ~(alignment - 1);
}

class GpuUploadBuffer {
public:
	ComPtr<ID3D12Resource> GetResource () { return m_Resource; }
	
protected:
	ComPtr<ID3D12Resource> m_Resource;

	GpuUploadBuffer () {}
	~GpuUploadBuffer () {
		if (m_Resource.Get ()) {
			m_Resource->Unmap (0, nullptr);
		}
	}

	void Allocate (ID3D12Device* device, UINT bufferSize, LPCWSTR resourceName = nullptr) {
		auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer (bufferSize);
		ThrowIfFailed (device->CreateCommittedResource (
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS (&m_Resource)
		));
		m_Resource->SetName (resourceName);
	} 

	uint8_t* MapCpuWriteOnly () {
		uint8_t* mappedData;
		CD3DX12_RANGE readRange (0, 0);
		ThrowIfFailed (m_Resource->Map (0, &readRange, reinterpret_cast<void**> (&mappedData)));
		return mappedData;
	}
};