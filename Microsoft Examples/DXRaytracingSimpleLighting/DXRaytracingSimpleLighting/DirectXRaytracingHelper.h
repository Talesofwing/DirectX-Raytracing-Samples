#pragma once

#define SizeOfInUint32(obj) ((sizeof (obj) - 1) / sizeof (UINT32) + 1)

class ShaderRecord {
public:
    ShaderRecord (void* pShaderIdentifier, UINT shaderIdentifierSize) :
        ShaderIdentifier (pShaderIdentifier, shaderIdentifierSize) {}

    ShaderRecord (void* pShaderIdentifier, UINT shaderIdentifierSize, void* pLocalRootArguments, UINT localRootArgumentsSize) :
        ShaderIdentifier (pShaderIdentifier, shaderIdentifierSize),
        LocalRootArguments (pLocalRootArguments, localRootArgumentsSize) {}

    void CopyTo (void* dest) const {
        uint8_t* byteDest = static_cast<uint8_t*> (dest);
        memcpy (byteDest, ShaderIdentifier.ptr, ShaderIdentifier.size);
        if (LocalRootArguments.ptr) {
            memcpy (byteDest + ShaderIdentifier.size, LocalRootArguments.ptr, LocalRootArguments.size);
        }
    }

    struct PointerWithSize {
        void* ptr;
        UINT size;

        PointerWithSize () : ptr (nullptr), size (0) {}
        PointerWithSize (void* _ptr, UINT _size) : ptr (_ptr), size (_size) {};
    };

    PointerWithSize ShaderIdentifier;
    PointerWithSize LocalRootArguments;
};

class ShaderTable : public GpuUploadBuffer {
    uint8_t* m_MappedShaderRecords;
    UINT m_ShaderRecordSize;

    // Debug support
    std::wstring m_Name;
    std::vector<ShaderRecord> m_ShaderRecords;

    ShaderTable () {}

public:
    ShaderTable (ID3D12Device* device, UINT numShaderRecords, UINT shaderRecordSize, LPCWSTR resourceName = nullptr) : m_Name (resourceName) {
        m_ShaderRecordSize = Align (shaderRecordSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_ShaderRecords.reserve (numShaderRecords);
        UINT bufferSize = numShaderRecords * m_ShaderRecordSize;
        Allocate (device, bufferSize, resourceName);
        m_MappedShaderRecords = MapCpuWriteOnly ();
    }

    void Push (const ShaderRecord& shaderRecord) {
        ThrowIfFalse (m_ShaderRecords.size () < m_ShaderRecords.capacity ());
        m_ShaderRecords.push_back (shaderRecord);
        shaderRecord.CopyTo (m_MappedShaderRecords);
        m_MappedShaderRecords += m_ShaderRecordSize;
    }

    UINT GetShaderRecordSize () { return m_ShaderRecordSize; }

    void DebugPrint (std::unordered_map<void*, std::wstring> shaderIdToStringMap) {
        std::wstringstream wstr;
        wstr << L"|--------------------------------------------------------------------\n";
        wstr << L"|Shader table - " << m_Name.c_str () << L": "
            << m_ShaderRecordSize << L" | "
            << m_ShaderRecords.size () * m_ShaderRecordSize << L" bytes\n";

        for (UINT i = 0; i < m_ShaderRecords.size (); i++) {
            wstr << L"| [" << i << L"]: ";
            wstr << shaderIdToStringMap[m_ShaderRecords[i].ShaderIdentifier.ptr] << L", ";
            wstr << m_ShaderRecords[i].ShaderIdentifier.size << L" + " << m_ShaderRecords[i].LocalRootArguments.size << L" bytes \n";
        }
        wstr << L"|--------------------------------------------------------------------\n";
        wstr << L"\n";
        OutputDebugStringW (wstr.str ().c_str ());
    }

};

inline void AllocateUAVBuffer (ID3D12Device* pDevice, UINT64 bufferSize, ID3D12Resource** ppResource, D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_COMMON, const wchar_t* resourceName = nullptr) {
    auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer (bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed (pDevice->CreateCommittedResource (
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        initialResourceState,
        nullptr,
        IID_PPV_ARGS (ppResource)
    ));

    if (resourceName) {
        (*ppResource)->SetName (resourceName);
    }
}

inline void AllocateUploadBuffer (ID3D12Device* pDevice, void* pData, UINT64 datasize, ID3D12Resource** ppResource, const wchar_t* resourceName = nullptr) {
    auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer (datasize);
    ThrowIfFailed (pDevice->CreateCommittedResource (
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS (ppResource)
    ));

    if (resourceName) {
        (*ppResource)->SetName (resourceName);
    }

    void* pMappedData;
    (*ppResource)->Map (0, nullptr, &pMappedData);
    memcpy (pMappedData, pData, datasize);
    (*ppResource)->Unmap (0, nullptr);
}

inline void PrintStateObjectDesc (const D3D12_STATE_OBJECT_DESC* desc) {
    std::wstringstream wstr;
    wstr << L"\n";
    wstr << L"--------------------------------------------------------------------\n";
    wstr << L"| D3D12 State Object 0x" << static_cast<const void*>(desc) << L": ";
    if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) wstr << L"Collection\n";
    if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) wstr << L"Raytracing Pipeline\n";

    auto ExportTree = [](UINT depth, UINT numExports, const D3D12_EXPORT_DESC* exports) {
        std::wostringstream woss;
        for (UINT i = 0; i < numExports; i++) {
            woss << L"|";
            if (depth > 0) {
                for (UINT j = 0; j < 2 * depth - 1; j++) woss << L" ";
            }
            woss << L" [" << i << L"]: ";
            if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
            woss << exports[i].Name << L"\n";
        }
        return woss.str ();
    };

    for (UINT i = 0; i < desc->NumSubobjects; i++) {
        wstr << L"| [" << i << L"]: ";
        switch (desc->pSubobjects[i].Type) {
            case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
                wstr << L"Global Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
                break;
            case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
                wstr << L"Local Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
                break;
            case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
                wstr << L"Node Mask: 0x" << std::hex << std::setfill (L'0') << std::setw (8) << *static_cast<const UINT*>(desc->pSubobjects[i].pDesc) << std::setw (0) << std::dec << L"\n";
                break;
            case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
            {
                wstr << L"DXIL Library 0x";
                auto lib = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
                wstr << lib->DXILLibrary.pShaderBytecode << L", " << lib->DXILLibrary.BytecodeLength << L" bytes\n";
                wstr << ExportTree (1, lib->NumExports, lib->pExports);
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
            {
                wstr << L"Existing Library 0x";
                auto collection = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(desc->pSubobjects[i].pDesc);
                wstr << collection->pExistingCollection << L"\n";
                wstr << ExportTree (1, collection->NumExports, collection->pExports);
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            {
                wstr << L"Subobject to Exports Association (Subobject [";
                auto association = static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
                UINT index = static_cast<UINT>(association->pSubobjectToAssociate - desc->pSubobjects);
                wstr << index << L"])\n";
                for (UINT j = 0; j < association->NumExports; j++) {
                    wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
                }
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            {
                wstr << L"DXIL Subobjects to Exports Association (";
                auto association = static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
                wstr << association->SubobjectToAssociate << L")\n";
                for (UINT j = 0; j < association->NumExports; j++) {
                    wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
                }
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
            {
                wstr << L"Raytracing Shader Config\n";
                auto config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(desc->pSubobjects[i].pDesc);
                wstr << L"|  [0]: Max Payload Size: " << config->MaxPayloadSizeInBytes << L" bytes\n";
                wstr << L"|  [1]: Max Attribute Size: " << config->MaxAttributeSizeInBytes << L" bytes\n";
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
            {
                wstr << L"Raytracing Pipeline Config\n";
                auto config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(desc->pSubobjects[i].pDesc);
                wstr << L"|  [0]: Max Recursion Depth: " << config->MaxTraceRecursionDepth << L"\n";
                break;
            }
            case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
            {
                wstr << L"Hit Group (";
                auto hitGroup = static_cast<const D3D12_HIT_GROUP_DESC*>(desc->pSubobjects[i].pDesc);
                wstr << (hitGroup->HitGroupExport ? hitGroup->HitGroupExport : L"[none]") << L")\n";
                wstr << L"|  [0]: Any Hit Import: " << (hitGroup->AnyHitShaderImport ? hitGroup->AnyHitShaderImport : L"[none]") << L"\n";
                wstr << L"|  [1]: Closest Hit Import: " << (hitGroup->ClosestHitShaderImport ? hitGroup->ClosestHitShaderImport : L"[none]") << L"\n";
                wstr << L"|  [2]: Intersection Import: " << (hitGroup->IntersectionShaderImport ? hitGroup->IntersectionShaderImport : L"[none]") << L"\n";
                break;
            }
        }
        wstr << L"|--------------------------------------------------------------------\n";
    }
    wstr << L"\n";
    OutputDebugStringW (wstr.str ().c_str ());
}

inline bool IsDirectXRaytracingSupported (IDXGIAdapter1* adapter) {
    ComPtr<ID3D12Device> testDevice;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData = {};

    return SUCCEEDED (D3D12CreateDevice (adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS (&testDevice)))
        && SUCCEEDED (testDevice->CheckFeatureSupport (D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof (featureSupportData)))
        && featureSupportData.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
}