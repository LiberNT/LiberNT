/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pipeline libraries and cached pipeline state blobs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "native12.h"
#include "native12_device.h"
#include "native12_objects.h"

static const GUID Native12LibraryFormat = {0x3f8e2a61, 0x5c4d, 0x4b7e, {0x9a, 0x21, 0x6d, 0x0c, 0x58, 0xe3, 0x47, 0xb9}};

struct Native12LibraryHeader
{
    GUID format;
    UINT vendor_id;
    UINT device_id;
    UINT subsys_id;
    UINT revision;
    UINT64 driver_identity;
    UINT entry_count;
    UINT names_size;
    UINT64 driver_size;
    BYTE hash[16];
};

struct Native12LibraryRecord
{
    UINT name_offset;
    UINT name_length;
    BYTE hash[16];
};

class Native12Hasher
{
public:
    UINT64 high = 0x6c62272e07bb0142ull;
    UINT64 low = 0x62b821756295c58dull;

    void Add(const void *data, SIZE_T size)
    {
        const BYTE *bytes = static_cast<const BYTE *>(data);
        for (SIZE_T i = 0; i < size; ++i)
        {
            low ^= bytes[i];
            UINT64 low_low = low & 0xffffffffu, low_high = low >> 32;
            UINT64 part_low = low_low * 0x13b, part_high = low_high * 0x13b;
            UINT64 result_low = part_low + (part_high << 32);
            UINT64 carry = (part_high >> 32) + (result_low < part_low ? 1 : 0);
            high = high * 0x13b + carry + (low << 24);
            low = result_low;
        }
    }
    template <typename T> void Value(const T &value) { Add(&value, sizeof(value)); }
    void String(const char *text)
    {
        UINT64 length = text ? strlen(text) + 1 : 0;
        Value(length);
        if (text) Add(text, static_cast<SIZE_T>(length));
    }
    void Bytecode(const D3D12_SHADER_BYTECODE &code)
    {
        UINT64 length = code.pShaderBytecode ? code.BytecodeLength : 0;
        Value(length);
        if (length) Add(code.pShaderBytecode, static_cast<SIZE_T>(length));
    }
    void RootSignature(ID3D12RootSignature *root)
    {
        Native12RootSignature *signature = static_cast<Native12RootSignature *>(root);
        UINT64 length = signature && signature->cached_bytecode ? signature->cached_size : 0;
        Value(length);
        if (length) Add(signature->cached_bytecode, static_cast<SIZE_T>(length));
    }
    void Finish(BYTE out[16]) const
    {
        memcpy(out, &low, sizeof(low));
        memcpy(out + 8, &high, sizeof(high));
    }
};

static void Native12HashGraphics(Native12Hasher &hasher, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
        const Native12PipelineState &state)
{
    hasher.Value(static_cast<UINT>(0));
    hasher.RootSignature(desc->pRootSignature);
    hasher.Bytecode(desc->VS);
    hasher.Bytecode(desc->PS);
    hasher.Bytecode(desc->DS);
    hasher.Bytecode(desc->HS);
    hasher.Bytecode(desc->GS);
    hasher.Value(desc->StreamOutput.NumEntries);
    for (UINT i = 0; desc->StreamOutput.pSODeclaration && i < desc->StreamOutput.NumEntries; ++i)
    {
        const D3D12_SO_DECLARATION_ENTRY &entry = desc->StreamOutput.pSODeclaration[i];
        hasher.Value(entry.Stream);
        hasher.String(entry.SemanticName);
        hasher.Value(entry.SemanticIndex);
        hasher.Value(entry.StartComponent);
        hasher.Value(entry.ComponentCount);
        hasher.Value(entry.OutputSlot);
    }
    hasher.Value(desc->StreamOutput.NumStrides);
    for (UINT i = 0; desc->StreamOutput.pBufferStrides && i < desc->StreamOutput.NumStrides; ++i)
        hasher.Value(desc->StreamOutput.pBufferStrides[i]);
    hasher.Value(desc->StreamOutput.RasterizedStream);
    hasher.Value(desc->BlendState.AlphaToCoverageEnable);
    hasher.Value(desc->BlendState.IndependentBlendEnable);
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        const D3D12_RENDER_TARGET_BLEND_DESC &target = desc->BlendState.RenderTarget[i];
        hasher.Value(target.BlendEnable);
        hasher.Value(target.LogicOpEnable);
        hasher.Value(target.SrcBlend);
        hasher.Value(target.DestBlend);
        hasher.Value(target.BlendOp);
        hasher.Value(target.SrcBlendAlpha);
        hasher.Value(target.DestBlendAlpha);
        hasher.Value(target.BlendOpAlpha);
        hasher.Value(target.LogicOp);
        hasher.Value(target.RenderTargetWriteMask);
    }
    hasher.Value(desc->SampleMask);
    hasher.Value(desc->RasterizerState);
    hasher.Value(desc->DepthStencilState.DepthEnable);
    hasher.Value(desc->DepthStencilState.DepthWriteMask);
    hasher.Value(desc->DepthStencilState.DepthFunc);
    hasher.Value(desc->DepthStencilState.StencilEnable);
    hasher.Value(desc->DepthStencilState.StencilReadMask);
    hasher.Value(desc->DepthStencilState.StencilWriteMask);
    hasher.Value(desc->DepthStencilState.FrontFace);
    hasher.Value(desc->DepthStencilState.BackFace);
    hasher.Value(desc->InputLayout.NumElements);
    for (UINT i = 0; desc->InputLayout.pInputElementDescs && i < desc->InputLayout.NumElements; ++i)
    {
        const D3D12_INPUT_ELEMENT_DESC &element = desc->InputLayout.pInputElementDescs[i];
        hasher.String(element.SemanticName);
        hasher.Value(element.SemanticIndex);
        hasher.Value(element.Format);
        hasher.Value(element.InputSlot);
        hasher.Value(element.AlignedByteOffset);
        hasher.Value(element.InputSlotClass);
        hasher.Value(element.InstanceDataStepRate);
    }
    hasher.Value(desc->IBStripCutValue);
    hasher.Value(desc->PrimitiveTopologyType);
    hasher.Value(desc->NumRenderTargets);
    hasher.Value(desc->RTVFormats);
    hasher.Value(desc->DSVFormat);
    hasher.Value(desc->SampleDesc);
    hasher.Value(desc->NodeMask ? desc->NodeMask : 1u);
    hasher.Value(desc->Flags);
    hasher.Value(state.depth_bounds_test);
    hasher.Value(state.face_stencil_masks);
    hasher.Value(state.back_stencil_read_mask);
    hasher.Value(state.back_stencil_write_mask);
    hasher.Value(state.float_depth_bias);
    hasher.Value(state.depth_bias);
    hasher.Value(state.line_mode_set);
    hasher.Value(state.line_mode);
    hasher.Value(state.view_instance_count);
    for (UINT i = 0; i < state.view_instance_count && i < D3D12_MAX_VIEW_INSTANCE_COUNT; ++i)
        hasher.Value(state.view_instance_locations[i]);
    hasher.Value(state.view_instancing_flags);
}

static void Native12HashCompute(Native12Hasher &hasher, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc)
{
    hasher.Value(static_cast<UINT>(1));
    hasher.RootSignature(desc->pRootSignature);
    hasher.Bytecode(desc->CS);
    hasher.Value(desc->NodeMask ? desc->NodeMask : 1u);
    hasher.Value(desc->Flags);
}

HRESULT Native12PipelineState::PrepareDescription(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *graphics,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *compute_desc, const D3D12_CACHED_PIPELINE_STATE &cached)
{
    Native12Hasher hasher;
    if (graphics) Native12HashGraphics(hasher, graphics, *this);
    else Native12HashCompute(hasher, compute_desc);
    hasher.Finish(description_hash);
    if (check_hash) return memcmp(description_hash, expected_hash, sizeof(description_hash)) ? E_INVALIDARG : S_OK;
    if (!cached.pCachedBlob || !cached.CachedBlobSizeInBytes) return S_OK;

    Native12PipelineLibrary *source = new Native12PipelineLibrary(device);
    if (!source) return E_OUTOFMEMORY;
    HRESULT hr = source->Initialize(cached.pCachedBlob, cached.CachedBlobSizeInBytes);
    UINT index = 0;
    if (SUCCEEDED(hr) && (!source->Find(L"", &index)
            || memcmp(source->entries[index].hash, description_hash, sizeof(description_hash))))
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr))
    {
        library = source;
        library->AddInternal();
        library_index = index;
    }
    source->Release();
    return hr;
}

D3D12DDI_LIBRARY_REFERENCE_0010 Native12PipelineState::LibraryReference() const
{
    D3D12DDI_LIBRARY_REFERENCE_0010 reference = {};
    if (library && library->driver_created)
    {
        reference.hLibrary = library->driver;
        reference.PipelineIndex = library_index;
    }
    return reference;
}

class Native12Blob final : public ID3DBlob, public Native12Allocation
{
public:
    LONG references = 1;
    SIZE_T size;
    BYTE *data;

    explicit Native12Blob(SIZE_T length) : size(length)
    {
        data = static_cast<BYTE *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, length ? length : 1));
    }
    ~Native12Blob() { HeapFree(GetProcessHeap(), 0, data); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        if (IsEqualGUID(iid, IID_ID3D10Blob) || IsEqualGUID(iid, IID_IUnknown))
        {
            AddRef();
            *out = static_cast<ID3DBlob *>(this);
            return S_OK;
        }
        *out = NULL;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG count = InterlockedDecrement(&references);
        if (!count) delete this;
        return count;
    }
    LPVOID STDMETHODCALLTYPE GetBufferPointer() override { return data; }
    SIZE_T STDMETHODCALLTYPE GetBufferSize() override { return size; }
};

HRESULT STDMETHODCALLTYPE Native12PipelineState::GetCachedBlob(ID3DBlob **blob)
{
    if (!blob) return E_POINTER;
    *blob = NULL;
    Native12PipelineLibrary *cache = new Native12PipelineLibrary(device);
    if (!cache) return E_OUTOFMEMORY;
    HRESULT hr = cache->Initialize(NULL, 0);
    if (SUCCEEDED(hr)) hr = cache->Store(L"", this);
    if (SUCCEEDED(hr))
    {
        AcquireSRWLockShared(&cache->lock);
        SIZE_T size = cache->SerializedSize();
        Native12Blob *result = new Native12Blob(size);
        if (!result || !result->data) hr = E_OUTOFMEMORY;
        else hr = cache->SerializeInto(result->data, size);
        ReleaseSRWLockShared(&cache->lock);
        if (SUCCEEDED(hr)) *blob = result;
        else if (result) result->Release();
    }
    cache->Release();
    return hr;
}

static UINT64 Native12DriverIdentity(Native12Device *device)
{
    const IMAGE_DOS_HEADER *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(device->umd);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    const IMAGE_NT_HEADERS *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(
            reinterpret_cast<const BYTE *>(dos) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return (static_cast<UINT64>(nt->FileHeader.TimeDateStamp) << 32) | nt->OptionalHeader.SizeOfImage;
}

static HRESULT Native12AdapterIdentity(Native12Device *device, Native12LibraryHeader *header)
{
    DXGI_ADAPTER_DESC desc;
    HRESULT hr = device->adapter->GetDesc(&desc);
    if (FAILED(hr)) return hr;
    header->vendor_id = desc.VendorId;
    header->device_id = desc.DeviceId;
    header->subsys_id = desc.SubSysId;
    header->revision = desc.Revision;
    header->driver_identity = Native12DriverIdentity(device);
    return S_OK;
}

Native12PipelineLibrary::~Native12PipelineLibrary()
{
    for (UINT i = 0; i < count; ++i)
    {
        if (entries[i].pipeline) entries[i].pipeline->ReleaseInternal();
        HeapFree(GetProcessHeap(), 0, entries[i].name);
    }
    HeapFree(GetProcessHeap(), 0, entries);
    if (driver_created) device->functions.pfnDestroyPipelineLibrary(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
    if (initial_data) HeapFree(GetProcessHeap(), 0, initial_data);
}

static WCHAR *Native12CopyName(const WCHAR *name, SIZE_T length)
{
    WCHAR *copy = static_cast<WCHAR *>(HeapAlloc(GetProcessHeap(), 0, (length + 1) * sizeof(WCHAR)));
    if (!copy) return NULL;
    memcpy(copy, name, length * sizeof(WCHAR));
    copy[length] = 0;
    return copy;
}

static bool Native12GrowEntries(Native12PipelineLibrary *library)
{
    if (library->count < library->capacity) return true;
    UINT capacity = library->capacity ? library->capacity * 2 : 8;
    void *grown = library->entries
            ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, library->entries, capacity * sizeof(*library->entries))
            : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(*library->entries));
    if (!grown) return false;
    library->entries = static_cast<Native12PipelineLibrary::Entry *>(grown);
    library->capacity = capacity;
    return true;
}

HRESULT Native12PipelineLibrary::Initialize(const void *blob, SIZE_T size)
{
    const BYTE *bytes = static_cast<const BYTE *>(blob);
    Native12LibraryHeader header = {}, local = {};
    const BYTE *driver_data = NULL;
    SIZE_T driver_size = 0;
    HRESULT hr;

    if (blob && size)
    {
        if (size < sizeof(header)) return E_INVALIDARG;
        memcpy(&header, bytes, sizeof(header));
        if (!IsEqualGUID(header.format, Native12LibraryFormat)) return E_INVALIDARG;
        UINT64 records = static_cast<UINT64>(header.entry_count) * sizeof(Native12LibraryRecord);
        UINT64 total = sizeof(header) + records + header.names_size + header.driver_size;
        if (header.driver_size > size || records > size || total > size) return E_INVALIDARG;
        if (FAILED(hr = Native12AdapterIdentity(device, &local))) return hr;
        if (local.vendor_id != header.vendor_id || local.device_id != header.device_id
                || local.subsys_id != header.subsys_id || local.revision != header.revision)
            return D3D12_ERROR_ADAPTER_NOT_FOUND;
        if (local.driver_identity != header.driver_identity) return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
        Native12Hasher hasher;
        hasher.Add(bytes + sizeof(header), static_cast<SIZE_T>(total - sizeof(header)));
        hasher.Finish(local.hash);
        if (memcmp(local.hash, header.hash, sizeof(local.hash))) return E_INVALIDARG;

        const BYTE *names = bytes + sizeof(header) + records;
        for (UINT i = 0; i < header.entry_count; ++i)
        {
            Native12LibraryRecord record;
            memcpy(&record, bytes + sizeof(header) + i * sizeof(record), sizeof(record));
            UINT64 end = static_cast<UINT64>(record.name_offset) + (static_cast<UINT64>(record.name_length) + 1) * 2;
            if (end > header.names_size) return E_INVALIDARG;
            if (!Native12GrowEntries(this)) return E_OUTOFMEMORY;
            Entry &entry = entries[count];
            if (!(entry.name = Native12CopyName(reinterpret_cast<const WCHAR *>(names + record.name_offset),
                    record.name_length)))
                return E_OUTOFMEMORY;
            memcpy(entry.hash, record.hash, sizeof(entry.hash));
            entry.pipeline = NULL;
            ++count;
        }
        driver_data = names + header.names_size;
        driver_size = static_cast<SIZE_T>(header.driver_size);
    }

    if (!device->functions.pfnCalcPrivatePipelineLibrarySize || !device->functions.pfnCreatePipelineLibrary)
        return S_OK;
    D3D12DDIARG_CREATE_PIPELINE_LIBRARY_0010 args = {};
    if (driver_size)
    {
        if (!(initial_data = static_cast<BYTE *>(HeapAlloc(GetProcessHeap(), 0, driver_size)))) return E_OUTOFMEMORY;
        memcpy(initial_data, driver_data, driver_size);
        args.pInitialData = initial_data;
        args.InitialDataSize = driver_size;
    }
    SIZE_T private_size = device->functions.pfnCalcPrivatePipelineLibrarySize(device->driver_device, &args);
    if (!(driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, private_size ? private_size : 1)))
        return E_OUTOFMEMORY;
    hr = device->functions.pfnCreatePipelineLibrary(device->driver_device, &args, driver);
    if (hr == E_INVALIDARG && driver_size) return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    if (FAILED(hr)) return hr;
    driver_created = true;
    return S_OK;
}

bool Native12PipelineLibrary::Find(const WCHAR *name, UINT *index) const
{
    for (UINT i = 0; i < count; ++i)
    {
        if (!wcscmp(entries[i].name, name))
        {
            *index = i;
            return true;
        }
    }
    return false;
}

HRESULT Native12PipelineLibrary::Store(const WCHAR *name, Native12PipelineState *pipeline)
{
    HRESULT hr = S_OK;
    UINT index;

    if (!name || !pipeline) return E_INVALIDARG;
    AcquireSRWLockExclusive(&lock);
    if (Find(name, &index)) hr = E_INVALIDARG;
    else if (!Native12GrowEntries(this)) hr = E_OUTOFMEMORY;
    else if (driver_created && pipeline->driver_created && device->functions.pfnAddPipelineStateToLibrary)
        hr = device->functions.pfnAddPipelineStateToLibrary(device->driver_device, driver, pipeline->driver, count);
    if (SUCCEEDED(hr))
    {
        Entry &entry = entries[count];
        if (!(entry.name = Native12CopyName(name, wcslen(name)))) hr = E_OUTOFMEMORY;
        else
        {
            memcpy(entry.hash, pipeline->description_hash, sizeof(entry.hash));
            entry.pipeline = pipeline;
            pipeline->AddInternal();
            ++count;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    return hr;
}

SIZE_T Native12PipelineLibrary::SerializedSize()
{
    SIZE_T size = sizeof(Native12LibraryHeader) + count * sizeof(Native12LibraryRecord);
    for (UINT i = 0; i < count; ++i) size += (wcslen(entries[i].name) + 1) * sizeof(WCHAR);
    if (driver_created && device->functions.pfnCalcSerializedLibrarySize)
        size += device->functions.pfnCalcSerializedLibrarySize(device->driver_device, driver);
    return size;
}

HRESULT Native12PipelineLibrary::SerializeInto(BYTE *data, SIZE_T size)
{
    Native12LibraryHeader header = {};
    SIZE_T names_size = 0, driver_size = 0, offset;
    HRESULT hr;

    for (UINT i = 0; i < count; ++i) names_size += (wcslen(entries[i].name) + 1) * sizeof(WCHAR);
    if (driver_created && device->functions.pfnCalcSerializedLibrarySize && device->functions.pfnSerializeLibrary)
        driver_size = device->functions.pfnCalcSerializedLibrarySize(device->driver_device, driver);
    SIZE_T total = sizeof(header) + count * sizeof(Native12LibraryRecord) + names_size + driver_size;
    if (!data || size < total) return E_INVALIDARG;

    header.format = Native12LibraryFormat;
    if (FAILED(hr = Native12AdapterIdentity(device, &header))) return hr;
    header.entry_count = count;
    header.names_size = static_cast<UINT>(names_size);
    header.driver_size = driver_size;
    offset = 0;
    for (UINT i = 0; i < count; ++i)
    {
        Native12LibraryRecord record = {};
        SIZE_T length = wcslen(entries[i].name);
        record.name_offset = static_cast<UINT>(offset);
        record.name_length = static_cast<UINT>(length);
        memcpy(record.hash, entries[i].hash, sizeof(record.hash));
        memcpy(data + sizeof(header) + i * sizeof(record), &record, sizeof(record));
        memcpy(data + sizeof(header) + count * sizeof(record) + offset, entries[i].name, (length + 1) * sizeof(WCHAR));
        offset += (length + 1) * sizeof(WCHAR);
    }
    if (driver_size && FAILED(hr = device->functions.pfnSerializeLibrary(device->driver_device, driver,
            data + sizeof(header) + count * sizeof(Native12LibraryRecord) + names_size)))
        return hr;
    Native12Hasher hasher;
    hasher.Add(data + sizeof(header), total - sizeof(header));
    hasher.Finish(header.hash);
    memcpy(data, &header, sizeof(header));
    return S_OK;
}

template <typename Create>
HRESULT Native12PipelineLibrary::Load(const WCHAR *name, REFIID iid, void **out, Create create)
{
    UINT index;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!name) return E_INVALIDARG;
    AcquireSRWLockExclusive(&lock);
    if (!Find(name, &index))
    {
        ReleaseSRWLockExclusive(&lock);
        return E_INVALIDARG;
    }
    Native12PipelineState *pipeline = new Native12PipelineState(device);
    if (!pipeline)
    {
        ReleaseSRWLockExclusive(&lock);
        return E_OUTOFMEMORY;
    }
    pipeline->check_hash = true;
    memcpy(pipeline->expected_hash, entries[index].hash, sizeof(pipeline->expected_hash));
    pipeline->hash_only = entries[index].pipeline != NULL;
    if (!pipeline->hash_only)
    {
        pipeline->library = this;
        AddInternal();
        pipeline->library_index = index;
    }
    hr = create(pipeline);
    if (SUCCEEDED(hr) && pipeline->hash_only) hr = entries[index].pipeline->QueryInterface(iid, out);
    else if (SUCCEEDED(hr))
    {
        entries[index].pipeline = pipeline;
        pipeline->AddInternal();
        hr = pipeline->QueryInterface(iid, out);
    }
    pipeline->Release();
    ReleaseSRWLockExclusive(&lock);
    return hr;
}

ULONG STDMETHODCALLTYPE Native12PipelineLibrary::Release()
{
    ULONG left = InterlockedDecrement(&references);
    if (!left)
    {
        AcquireSRWLockExclusive(&lock);
        for (UINT i = 0; i < count; ++i)
        {
            if (!entries[i].pipeline) continue;
            entries[i].pipeline->ReleaseInternal();
            entries[i].pipeline = NULL;
        }
        ReleaseSRWLockExclusive(&lock);
        ReleaseInternal();
    }
    return left;
}

HRESULT STDMETHODCALLTYPE Native12PipelineLibrary::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12PipelineLibrary1) || IsEqualGUID(iid, IID_ID3D12PipelineLibrary)
            || IsEqualGUID(iid, IID_ID3D12DeviceChild) || IsEqualGUID(iid, IID_ID3D12Object)
            || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12PipelineLibrary1 *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Native12PipelineLibrary::StorePipeline(LPCWSTR name, ID3D12PipelineState *pipeline)
{
    return Store(name, static_cast<Native12PipelineState *>(pipeline));
}

HRESULT STDMETHODCALLTYPE Native12PipelineLibrary::LoadGraphicsPipeline(LPCWSTR name,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID iid, void **out)
{
    if (!desc) return E_INVALIDARG;
    return Load(name, iid, out, [desc](Native12PipelineState *pipeline) { return pipeline->InitializeGraphics(desc); });
}

HRESULT STDMETHODCALLTYPE Native12PipelineLibrary::LoadComputePipeline(LPCWSTR name,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID iid, void **out)
{
    if (!desc) return E_INVALIDARG;
    return Load(name, iid, out, [desc](Native12PipelineState *pipeline) { return pipeline->InitializeCompute(desc); });
}

HRESULT STDMETHODCALLTYPE Native12PipelineLibrary::LoadPipeline(LPCWSTR name,
        const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID iid, void **out)
{
    if (!desc) return E_INVALIDARG;
    Native12Device *owner = device;
    return Load(name, iid, out, [owner, desc](Native12PipelineState *pipeline)
            { return owner->InitializePipelineState(pipeline, desc); });
}

SIZE_T STDMETHODCALLTYPE Native12PipelineLibrary::GetSerializedSize()
{
    AcquireSRWLockShared(&lock);
    SIZE_T size = SerializedSize();
    ReleaseSRWLockShared(&lock);
    return size;
}

HRESULT STDMETHODCALLTYPE Native12PipelineLibrary::Serialize(void *data, SIZE_T size)
{
    AcquireSRWLockShared(&lock);
    HRESULT hr = SerializeInto(static_cast<BYTE *>(data), size);
    ReleaseSRWLockShared(&lock);
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreatePipelineLibrary(const void *blob, SIZE_T size, REFIID iid, void **out)
{
    if (out) *out = NULL;
    if (size && !blob) return E_INVALIDARG;
    Native12PipelineLibrary *library = new Native12PipelineLibrary(this);
    if (!library) return E_OUTOFMEMORY;
    HRESULT hr = library->Initialize(blob, size);
    if (SUCCEEDED(hr)) hr = out ? library->QueryInterface(iid, out) : S_FALSE;
    library->Release();
    return hr;
}
