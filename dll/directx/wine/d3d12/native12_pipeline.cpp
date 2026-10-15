/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 root signatures and pipeline state objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "native12.h"
#include "native12_device.h"
#include "native12_objects.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d12);

static_assert(sizeof(D3D12_ROOT_PARAMETER1) == sizeof(D3D12DDI_ROOT_PARAMETER_0013), "root parameter layout");
static_assert(sizeof(D3D12_DESCRIPTOR_RANGE1) == sizeof(D3D12DDI_DESCRIPTOR_RANGE_0013), "descriptor range layout");
static_assert(offsetof(D3D12_ROOT_PARAMETER1, ShaderVisibility)
        == offsetof(D3D12DDI_ROOT_PARAMETER_0013, ShaderVisibility), "root parameter visibility layout");

struct Native12RegisterRange
{
    UINT type, space, base, visibility;
    UINT64 count;
};

static HRESULT Native12AddRegisterRange(Native12RegisterRange *ranges, UINT capacity, UINT *count, UINT type,
        UINT space, UINT base, UINT64 registers, D3D12_SHADER_VISIBILITY visibility)
{
    if (*count >= capacity) return E_INVALIDARG;
    Native12RegisterRange &range = ranges[(*count)++];
    range.type = type;
    range.space = space;
    range.base = base;
    range.count = registers;
    range.visibility = visibility;
    return S_OK;
}

static HRESULT Native12ValidateRootSignature(const D3D12_ROOT_SIGNATURE_DESC2 *desc)
{
    UINT capacity = desc->NumStaticSamplers, count = 0, cost = 0;
    HRESULT hr = S_OK;

    for (UINT i = 0; i < desc->NumParameters; ++i)
        capacity += desc->pParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE
                ? desc->pParameters[i].DescriptorTable.NumDescriptorRanges : 1;
    if (!capacity) return S_OK;
    Native12RegisterRange *ranges = static_cast<Native12RegisterRange *>(
            HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*ranges)));
    if (!ranges) return E_OUTOFMEMORY;

    for (UINT i = 0; i < desc->NumParameters && SUCCEEDED(hr); ++i)
    {
        const D3D12_ROOT_PARAMETER1 &parameter = desc->pParameters[i];
        switch (parameter.ParameterType)
        {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            {
                bool unbounded = false;
                UINT64 table_offset = 0;
                cost += 1;
                for (UINT j = 0; j < parameter.DescriptorTable.NumDescriptorRanges && SUCCEEDED(hr); ++j)
                {
                    const D3D12_DESCRIPTOR_RANGE1 &range = parameter.DescriptorTable.pDescriptorRanges[j];
                    UINT64 start = range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                            ? table_offset : range.OffsetInDescriptorsFromTableStart;
                    if (!range.NumDescriptors
                            || (range.NumDescriptors != UINT_MAX
                            && (range.NumDescriptors > UINT_MAX - range.BaseShaderRegister
                            || start + range.NumDescriptors > UINT_MAX))
                            || (unbounded && range.OffsetInDescriptorsFromTableStart
                            == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND))
                    {
                        hr = E_INVALIDARG;
                        break;
                    }
                    table_offset = start + range.NumDescriptors;
                    UINT64 registers = range.NumDescriptors == UINT_MAX
                            ? 0x100000000ull - range.BaseShaderRegister : range.NumDescriptors;
                    if (range.NumDescriptors == UINT_MAX) unbounded = true;
                    hr = Native12AddRegisterRange(ranges, capacity, &count, range.RangeType, range.RegisterSpace,
                            range.BaseShaderRegister, registers, parameter.ShaderVisibility);
                }
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                cost += parameter.Constants.Num32BitValues;
                hr = Native12AddRegisterRange(ranges, capacity, &count, D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
                        parameter.Constants.RegisterSpace, parameter.Constants.ShaderRegister, 1,
                        parameter.ShaderVisibility);
                break;
            case D3D12_ROOT_PARAMETER_TYPE_CBV:
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
                cost += 2;
                hr = Native12AddRegisterRange(ranges, capacity, &count,
                        parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV ? D3D12_DESCRIPTOR_RANGE_TYPE_CBV
                        : parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
                        : D3D12_DESCRIPTOR_RANGE_TYPE_UAV, parameter.Descriptor.RegisterSpace,
                        parameter.Descriptor.ShaderRegister, 1, parameter.ShaderVisibility);
                break;
            default:
                hr = E_INVALIDARG;
                break;
        }
    }
    if (SUCCEEDED(hr) && cost > D3D12_MAX_ROOT_COST) hr = E_INVALIDARG;
    for (UINT i = 0; i < desc->NumStaticSamplers && SUCCEEDED(hr); ++i)
        hr = Native12AddRegisterRange(ranges, capacity, &count, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
                desc->pStaticSamplers[i].RegisterSpace, desc->pStaticSamplers[i].ShaderRegister, 1,
                desc->pStaticSamplers[i].ShaderVisibility);

    for (UINT i = 0; i < count && SUCCEEDED(hr); ++i)
    {
        for (UINT j = i + 1; j < count; ++j)
        {
            const Native12RegisterRange &a = ranges[i], &b = ranges[j];
            if (a.type != b.type || a.space != b.space) continue;
            if (a.visibility != D3D12_SHADER_VISIBILITY_ALL && b.visibility != D3D12_SHADER_VISIBILITY_ALL
                    && a.visibility != b.visibility)
                continue;
            if (a.base < b.base + b.count && b.base < a.base + a.count)
            {
                hr = E_INVALIDARG;
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, ranges);
    return hr;
}

HRESULT Native12RootSignature::Initialize(const void *bytecode, SIZE_T length)
{
    ID3D12VersionedRootSignatureDeserializer *deserializer = NULL;
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *versioned = NULL;
    HRESULT hr = D3D12CreateVersionedRootSignatureDeserializer(bytecode, length,
            IID_ID3D12VersionedRootSignatureDeserializer, reinterpret_cast<void **>(&deserializer));
    if (FAILED(hr)) return hr;
    hr = deserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_2, &versioned);
    if (FAILED(hr) || !versioned)
    {
        deserializer->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    const D3D12_ROOT_SIGNATURE_DESC2 *source = &versioned->Desc_1_2;
    if (FAILED(hr = Native12ValidateRootSignature(source)))
    {
        deserializer->Release();
        return hr;
    }
    flags = source->Flags;
    UINT constant_offset = 0;
    parameter_count = source->NumParameters < D3D12_MAX_ROOT_COST ? source->NumParameters : D3D12_MAX_ROOT_COST;
    for (UINT i = 0; i < parameter_count; ++i)
    {
        const D3D12_ROOT_PARAMETER1 &parameter = source->pParameters[i];
        UINT64 key = 0xcbf29ce484222325ull;
        auto mix = [&key](UINT value) { key = (key ^ value) * 0x100000001b3ull; };
        mix(parameter.ParameterType);
        mix(parameter.ShaderVisibility);
        parameter_types[i] = static_cast<BYTE>(parameter.ParameterType);
        switch (parameter.ParameterType)
        {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                mix(parameter.DescriptorTable.NumDescriptorRanges);
                for (UINT j = 0; j < parameter.DescriptorTable.NumDescriptorRanges; ++j)
                {
                    const D3D12_DESCRIPTOR_RANGE1 &range = parameter.DescriptorTable.pDescriptorRanges[j];
                    mix(range.RangeType);
                    mix(range.NumDescriptors);
                    mix(range.BaseShaderRegister);
                    mix(range.RegisterSpace);
                    mix(range.Flags);
                    mix(range.OffsetInDescriptorsFromTableStart);
                }
                break;
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                mix(parameter.Constants.ShaderRegister);
                mix(parameter.Constants.RegisterSpace);
                mix(parameter.Constants.Num32BitValues);
                parameter_constants[i] = static_cast<BYTE>(parameter.Constants.Num32BitValues);
                constant_offsets[i] = static_cast<BYTE>(constant_offset);
                constant_offset += parameter.Constants.Num32BitValues;
                break;
            default:
                mix(parameter.Descriptor.ShaderRegister);
                mix(parameter.Descriptor.RegisterSpace);
                mix(parameter.Descriptor.Flags);
                break;
        }
        parameter_keys[i] = key;
    }
    D3D12DDI_STATIC_SAMPLER_0100 *samplers = NULL;
    if (source->NumStaticSamplers)
    {
        samplers = static_cast<D3D12DDI_STATIC_SAMPLER_0100 *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                source->NumStaticSamplers * sizeof(*samplers)));
        if (!samplers)
        {
            deserializer->Release();
            return E_OUTOFMEMORY;
        }
        for (UINT i = 0; i < source->NumStaticSamplers; ++i)
        {
            const D3D12_STATIC_SAMPLER_DESC1 &input = source->pStaticSamplers[i];
            samplers[i].Filter = static_cast<D3D12DDI_FILTER>(input.Filter);
            samplers[i].AddressU = static_cast<D3D12DDI_TEXTURE_ADDRESS_MODE>(input.AddressU);
            samplers[i].AddressV = static_cast<D3D12DDI_TEXTURE_ADDRESS_MODE>(input.AddressV);
            samplers[i].AddressW = static_cast<D3D12DDI_TEXTURE_ADDRESS_MODE>(input.AddressW);
            samplers[i].MipLODBias = input.MipLODBias;
            samplers[i].MaxAnisotropy = input.MaxAnisotropy;
            samplers[i].ComparisonFunc = static_cast<D3D12DDI_COMPARISON_FUNC>(input.ComparisonFunc);
            samplers[i].BorderColor = static_cast<D3D12DDI_STATIC_BORDER_COLOR>(input.BorderColor);
            samplers[i].MinLOD = input.MinLOD;
            samplers[i].MaxLOD = input.MaxLOD;
            samplers[i].ShaderRegister = input.ShaderRegister;
            samplers[i].RegisterSpace = input.RegisterSpace;
            samplers[i].ShaderVisibility = static_cast<D3D12DDI_SHADER_VISIBILITY>(input.ShaderVisibility);
            samplers[i].Flags = static_cast<D3D12DDI_SAMPLER_FLAGS_0096>(input.Flags);
        }
    }

    D3D12DDI_ROOT_SIGNATURE_0100 signature = {};
    signature.NumParameters = source->NumParameters;
    signature.pRootParameters = reinterpret_cast<const D3D12DDI_ROOT_PARAMETER_0013 *>(source->pParameters);
    signature.NumStaticSamplers = source->NumStaticSamplers;
    signature.pStaticSamplers = samplers;
    signature.Flags = static_cast<D3D12DDI_ROOT_SIGNATURE_FLAGS>(source->Flags);
    D3D12DDIARG_CREATE_ROOT_SIGNATURE_0100 args = {};
    args.Version = D3D12DDI_ROOT_SIGNATURE_VERSION_1_2;
    args.pRootSignature_1_2 = &signature;
    args.NodeMask = 1;
    SIZE_T size = device->functions.pfnCalcPrivateRootSignatureSize(device->driver_device, &args);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!driver.pDrvPrivate) hr = E_OUTOFMEMORY;
    else hr = device->functions.pfnCreateRootSignature(device->driver_device, &args, driver);
    if (SUCCEEDED(hr)) driver_created = true;
    else WARN("The driver failed to create a root signature with %u parameters, hr %#lx.\n",
            source->NumParameters, hr);
    if (samplers) HeapFree(GetProcessHeap(), 0, samplers);
    deserializer->Release();
    return hr;
}

Native12RootSignature::~Native12RootSignature()
{
    if (cached_bytecode)
    {
        AcquireSRWLockExclusive(&device->root_signature_lock);
        for (Native12RootSignature **link = &device->root_signatures; *link; link = &(*link)->next_cached)
        {
            if (*link == this)
            {
                *link = next_cached;
                break;
            }
        }
        ReleaseSRWLockExclusive(&device->root_signature_lock);
        HeapFree(GetProcessHeap(), 0, cached_bytecode);
    }
    if (driver_created) device->functions.pfnDestroyRootSignature(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
}

HRESULT STDMETHODCALLTYPE Native12RootSignature::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12RootSignature) || IsEqualGUID(iid, IID_ID3D12DeviceChild)
            || IsEqualGUID(iid, IID_ID3D12Object) || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12RootSignature *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateRootSignature(UINT, const void *bytecode, SIZE_T length,
        REFIID iid, void **out)
{
    if (!bytecode || !length || !out) return E_INVALIDARG;
    *out = NULL;
    AcquireSRWLockExclusive(&root_signature_lock);
    for (Native12RootSignature *cached = root_signatures; cached; cached = cached->next_cached)
    {
        if (cached->cached_size != length || memcmp(cached->cached_bytecode, bytecode, length)) continue;
        LONG count = cached->references;
        while (count > 0)
        {
            LONG previous = InterlockedCompareExchange(&cached->references, count + 1, count);
            if (previous == count) break;
            count = previous;
        }
        if (count <= 0) continue;
        ReleaseSRWLockExclusive(&root_signature_lock);
        HRESULT hr = cached->QueryInterface(iid, out);
        cached->Release();
        return hr;
    }
    ReleaseSRWLockExclusive(&root_signature_lock);

    Native12RootSignature *signature = new Native12RootSignature(this);
    if (!signature) return E_OUTOFMEMORY;
    HRESULT hr = signature->Initialize(bytecode, length);
    if (SUCCEEDED(hr) && (signature->cached_bytecode = static_cast<BYTE *>(HeapAlloc(GetProcessHeap(), 0, length))))
    {
        memcpy(signature->cached_bytecode, bytecode, length);
        signature->cached_size = length;
        AcquireSRWLockExclusive(&root_signature_lock);
        signature->next_cached = root_signatures;
        root_signatures = signature;
        ReleaseSRWLockExclusive(&root_signature_lock);
    }
    if (SUCCEEDED(hr)) hr = signature->QueryInterface(iid, out);
    signature->Release();
    return hr;
}

static UINT Native12ReadDword(const BYTE *data)
{
    UINT value;
    memcpy(&value, data, sizeof(value));
    return value;
}

static UINT Native12SystemValue(UINT name, UINT index)
{
    switch (name)
    {
        case D3D_NAME_FINAL_QUAD_EDGE_TESSFACTOR: return D3D11_SB_NAME_FINAL_QUAD_U_EQ_0_EDGE_TESSFACTOR + index;
        case D3D_NAME_FINAL_QUAD_INSIDE_TESSFACTOR: return D3D11_SB_NAME_FINAL_QUAD_U_INSIDE_TESSFACTOR + index;
        case D3D_NAME_FINAL_TRI_EDGE_TESSFACTOR: return D3D11_SB_NAME_FINAL_TRI_U_EQ_0_EDGE_TESSFACTOR + index;
        case D3D_NAME_FINAL_TRI_INSIDE_TESSFACTOR: return D3D11_SB_NAME_FINAL_TRI_INSIDE_TESSFACTOR;
        case D3D_NAME_FINAL_LINE_DETAIL_TESSFACTOR: return D3D11_SB_NAME_FINAL_LINE_DETAIL_TESSFACTOR;
        case D3D_NAME_FINAL_LINE_DENSITY_TESSFACTOR: return D3D11_SB_NAME_FINAL_LINE_DENSITY_TESSFACTOR;
        default: return name;
    }
}

struct Native12Signature
{
    D3D12DDIARG_SIGNATURE_ENTRY_0012 *entries = NULL;
    const char **names = NULL;
    UINT *indices = NULL;
    UINT count = 0;

    ~Native12Signature()
    {
        HeapFree(GetProcessHeap(), 0, entries);
        HeapFree(GetProcessHeap(), 0, names);
        HeapFree(GetProcessHeap(), 0, indices);
    }

    HRESULT Read(const BYTE *data, UINT size, UINT tag)
    {
        if (size < 8) return E_INVALIDARG;
        UINT elements = Native12ReadDword(data);
        bool stream = tag == 0x31475349 || tag == 0x3147534f || tag == 0x3547534f || tag == 0x31475350;
        UINT stride = stream ? (tag == 0x3547534f ? 28 : 32) : 24;
        if (elements > (size - 8) / stride) return E_INVALIDARG;
        HeapFree(GetProcessHeap(), 0, entries);
        HeapFree(GetProcessHeap(), 0, names);
        HeapFree(GetProcessHeap(), 0, indices);
        entries = static_cast<D3D12DDIARG_SIGNATURE_ENTRY_0012 *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                elements ? elements * sizeof(*entries) : 1));
        names = static_cast<const char **>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                elements ? elements * sizeof(*names) : 1));
        indices = static_cast<UINT *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                elements ? elements * sizeof(*indices) : 1));
        if (!entries || !names || !indices) return E_OUTOFMEMORY;
        for (UINT i = 0; i < elements; ++i)
        {
            const BYTE *entry = data + 8 + i * stride + (stream ? 4 : 0);
            UINT name_offset = Native12ReadDword(entry);
            if (name_offset >= size) return E_INVALIDARG;
            names[i] = reinterpret_cast<const char *>(data + name_offset);
            indices[i] = Native12ReadDword(entry + 4);
            entries[i].SystemValue = static_cast<D3D10_SB_NAME>(
                    Native12SystemValue(Native12ReadDword(entry + 8), indices[i]));
            entries[i].RegisterComponentType =
                    static_cast<D3D10_SB_REGISTER_COMPONENT_TYPE>(Native12ReadDword(entry + 12));
            entries[i].Register = Native12ReadDword(entry + 16);
            entries[i].Mask = entry[20] & 0x0f;
            entries[i].Stream = stream ? static_cast<BYTE>(Native12ReadDword(data + 8 + i * stride)) : 0;
            entries[i].MinPrecision = static_cast<D3D11_SB_OPERAND_MIN_PRECISION>(
                    stride == 32 ? Native12ReadDword(entry + 24) : 0);
        }
        count = elements;
        return S_OK;
    }
};

struct Native12ShaderCode
{
    const UINT *code = NULL;
    D3D12DDI_SHADERCACHE_HASH hash = {};
    Native12Signature input, output, patch;

    HRESULT Parse(const D3D12_SHADER_BYTECODE &bytecode)
    {
        const BYTE *data = static_cast<const BYTE *>(bytecode.pShaderBytecode);
        if (!data || bytecode.BytecodeLength < 32 || Native12ReadDword(data) != 0x43425844) return E_INVALIDARG;
        UINT size = Native12ReadDword(data + 24), chunks = Native12ReadDword(data + 28);
        if (size > bytecode.BytecodeLength || size < 32 || chunks > (size - 32) / 4) return E_INVALIDARG;
        memcpy(hash.Hash, data + 4, sizeof(hash.Hash));
        for (UINT i = 0; i < chunks; ++i)
        {
            UINT offset = Native12ReadDword(data + 32 + i * 4);
            if (offset > size - 8) return E_INVALIDARG;
            UINT tag = Native12ReadDword(data + offset), chunk_size = Native12ReadDword(data + offset + 4);
            if (chunk_size > size - offset - 8) return E_INVALIDARG;
            const BYTE *chunk = data + offset + 8;
            HRESULT hr = S_OK;
            if (tag == 0x52444853 || tag == 0x58454853 || tag == 0x4c495844)
            {
                if (chunk_size < 8) return E_INVALIDARG;
                code = reinterpret_cast<const UINT *>(chunk);
            }
            else if (tag == 0x4e475349 || tag == 0x31475349) hr = input.Read(chunk, chunk_size, tag);
            else if (tag == 0x4e47534f || tag == 0x3147534f || tag == 0x3547534f)
                hr = output.Read(chunk, chunk_size, tag);
            else if (tag == 0x47534350 || tag == 0x31475350) hr = patch.Read(chunk, chunk_size, tag);
            if (FAILED(hr)) return hr;
        }
        return code ? S_OK : E_INVALIDARG;
    }
};

enum Native12Stage
{
    Native12StageVertex,
    Native12StagePixel,
    Native12StageGeometry,
    Native12StageHull,
    Native12StageDomain,
    Native12StageCompute,
};

static HRESULT Native12CreateShader(Native12PipelineState *pipeline, Native12Stage stage,
        const Native12ShaderCode &shader)
{
    Native12Device *device = pipeline->device;
    D3D12DDIARG_STAGE_IO_SIGNATURES standard = {};
    D3D12DDIARG_TESSELLATION_IO_SIGNATURES tessellation = {};
    D3D12DDIARG_CREATE_SHADER_0026 args = {};
    bool tess = stage == Native12StageHull || stage == Native12StageDomain;

    if (pipeline->root_signature) args.hRootSignature = pipeline->root_signature->driver;
    args.pShaderCode = shader.code;
    args.ShaderCodeHash = shader.hash;
    if (tess)
    {
        tessellation.pInputSignature = shader.input.entries;
        tessellation.NumInputSignatureEntries = shader.input.count;
        tessellation.pOutputSignature = shader.output.entries;
        tessellation.NumOutputSignatureEntries = shader.output.count;
        tessellation.pPatchConstantSignature = shader.patch.entries;
        tessellation.NumPatchConstantSignatureEntries = shader.patch.count;
        args.IOSignatures.Tessellation = &tessellation;
    }
    else
    {
        standard.pInputSignature = shader.input.entries;
        standard.NumInputSignatureEntries = shader.input.count;
        standard.pOutputSignature = shader.output.entries;
        standard.NumOutputSignatureEntries = shader.output.count;
        args.IOSignatures.Standard = &standard;
    }

    SIZE_T size = tess ? device->functions.pfnCalcPrivateTessellationShaderSize(device->driver_device, &args)
            : device->functions.pfnCalcPrivateShaderSize(device->driver_device, &args);
    pipeline->shaders[stage].pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!pipeline->shaders[stage].pDrvPrivate) return E_OUTOFMEMORY;
    switch (stage)
    {
        case Native12StageVertex:
            device->functions.pfnCreateVertexShader(device->driver_device, &args, pipeline->shaders[stage]);
            break;
        case Native12StagePixel:
            device->functions.pfnCreatePixelShader(device->driver_device, &args, pipeline->shaders[stage]);
            break;
        case Native12StageGeometry:
            device->functions.pfnCreateGeometryShader(device->driver_device, &args, pipeline->shaders[stage]);
            break;
        case Native12StageHull:
            device->functions.pfnCreateHullShader(device->driver_device, &args, pipeline->shaders[stage]);
            break;
        case Native12StageDomain:
            device->functions.pfnCreateDomainShader(device->driver_device, &args, pipeline->shaders[stage]);
            break;
        default:
            device->functions.pfnCreateComputeShader(device->driver_device, &args, pipeline->shaders[stage]);
            break;
    }
    pipeline->shader_created[stage] = true;
    return device->removed_reason;
}

static HRESULT Native12CreateStreamOutputShader(Native12PipelineState *pipeline, const Native12ShaderCode &shader,
        bool passthrough, const D3D12_STREAM_OUTPUT_DESC &output)
{
    Native12Device *device = pipeline->device;
    D3D12DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *declaration = NULL;
    UINT buffer_bytes[D3D12_SO_BUFFER_SLOT_COUNT] = {}, buffer_stream[D3D12_SO_BUFFER_SLOT_COUNT];
    UINT strides[D3D12_SO_BUFFER_SLOT_COUNT] = {}, used_buffers = 0, expanded = 0;
    BYTE used_components[D3D12_SO_STREAM_COUNT][32] = {};

    if (!output.pSODeclaration || output.NumEntries > D3D12_SO_STREAM_COUNT * D3D12_SO_OUTPUT_COMPONENT_COUNT
            || output.NumStrides > D3D12_SO_BUFFER_SLOT_COUNT || (output.NumStrides && !output.pBufferStrides)
            || (output.RasterizedStream != D3D12_SO_NO_RASTERIZED_STREAM
                && output.RasterizedStream >= D3D12_SO_STREAM_COUNT)) return E_INVALIDARG;
    if (!device->functions.pfnCalcPrivateGeometryShaderWithStreamOutput
            || !device->functions.pfnCreateGeometryShaderWithStreamOutput) return E_NOTIMPL;
    for (UINT i = 0; i < D3D12_SO_BUFFER_SLOT_COUNT; ++i) buffer_stream[i] = ~0u;
    for (UINT i = 0; i < output.NumEntries; ++i)
    {
        const D3D12_SO_DECLARATION_ENTRY &entry = output.pSODeclaration[i];
        if (entry.Stream >= D3D12_SO_STREAM_COUNT || entry.OutputSlot >= D3D12_SO_BUFFER_SLOT_COUNT
                || !entry.ComponentCount) return E_INVALIDARG;
        if (buffer_stream[entry.OutputSlot] != ~0u && buffer_stream[entry.OutputSlot] != entry.Stream)
            return E_INVALIDARG;
        buffer_stream[entry.OutputSlot] = entry.Stream;
        if (used_buffers <= entry.OutputSlot) used_buffers = entry.OutputSlot + 1u;
        buffer_bytes[entry.OutputSlot] += 4 * entry.ComponentCount;
        if (buffer_bytes[entry.OutputSlot] > D3D12_SO_BUFFER_MAX_STRIDE_IN_BYTES) return E_INVALIDARG;
        expanded += entry.SemanticName ? 1 : (entry.ComponentCount + 3) / 4;
    }
    declaration = static_cast<D3D12DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY *>(HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, expanded ? expanded * sizeof(*declaration) : 1));
    if (!declaration) return E_OUTOFMEMORY;
    UINT cursor = 0;
    for (UINT i = 0; i < output.NumEntries; ++i)
    {
        const D3D12_SO_DECLARATION_ENTRY &entry = output.pSODeclaration[i];
        if (!entry.SemanticName)
        {
            UINT remaining = entry.ComponentCount;
            while (remaining)
            {
                UINT components = remaining > 4 ? 4 : remaining;
                D3D12DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY &gap = declaration[cursor++];
                gap.Stream = entry.Stream;
                gap.OutputSlot = entry.OutputSlot;
                gap.RegisterIndex = ~0u;
                gap.RegisterMask = static_cast<BYTE>((1u << components) - 1);
                remaining -= components;
            }
            continue;
        }
        const D3D12DDIARG_SIGNATURE_ENTRY_0012 *signature = NULL;
        for (UINT j = 0; j < shader.output.count && !signature; ++j)
            if (shader.output.indices[j] == entry.SemanticIndex && shader.output.entries[j].Stream == entry.Stream
                    && !_stricmp(shader.output.names[j], entry.SemanticName)) signature = &shader.output.entries[j];
        if (!signature || signature->Register >= 32 || !signature->Mask || entry.StartComponent > 3
                || entry.ComponentCount > 4 || entry.StartComponent + entry.ComponentCount > 4)
        {
            HeapFree(GetProcessHeap(), 0, declaration);
            return E_INVALIDARG;
        }
        UINT first = 0;
        while (!(signature->Mask & (1u << first))) ++first;
        UINT mask = ((1u << entry.ComponentCount) - 1) << (first + entry.StartComponent);
        if ((mask & signature->Mask) != mask || (used_components[entry.Stream][signature->Register] & mask))
        {
            HeapFree(GetProcessHeap(), 0, declaration);
            return E_INVALIDARG;
        }
        used_components[entry.Stream][signature->Register] |= mask;
        D3D12DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY &target = declaration[cursor++];
        target.Stream = entry.Stream;
        target.OutputSlot = entry.OutputSlot;
        target.RegisterIndex = signature->Register;
        target.RegisterMask = static_cast<BYTE>(mask);
    }
    for (UINT slot = 0; slot < used_buffers; ++slot)
    {
        strides[slot] = buffer_bytes[slot];
        if (slot < output.NumStrides)
        {
            if (output.pBufferStrides[slot] < buffer_bytes[slot] || (output.pBufferStrides[slot] & 3)
                    || output.pBufferStrides[slot] > D3D12_SO_BUFFER_MAX_STRIDE_IN_BYTES)
            {
                HeapFree(GetProcessHeap(), 0, declaration);
                return E_INVALIDARG;
            }
            strides[slot] = output.pBufferStrides[slot];
        }
    }

    D3D12DDIARG_STAGE_IO_SIGNATURES signatures = {};
    signatures.pInputSignature = passthrough ? shader.output.entries : shader.input.entries;
    signatures.NumInputSignatureEntries = passthrough ? shader.output.count : shader.input.count;
    signatures.pOutputSignature = shader.output.entries;
    signatures.NumOutputSignatureEntries = shader.output.count;
    D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0026 args = {};
    if (pipeline->root_signature) args.CreateShader.hRootSignature = pipeline->root_signature->driver;
    args.CreateShader.pShaderCode = passthrough ? NULL : shader.code;
    if (!passthrough) args.CreateShader.ShaderCodeHash = shader.hash;
    args.CreateShader.IOSignatures.Standard = &signatures;
    args.pOutputStreamDecl = declaration;
    args.NumEntries = expanded;
    args.BufferStridesInBytes = strides;
    args.NumStrides = used_buffers;
    args.RasterizedStream = output.RasterizedStream;
    SIZE_T size = device->functions.pfnCalcPrivateGeometryShaderWithStreamOutput(device->driver_device, &args);
    D3D12DDI_HSHADER &handle = pipeline->shaders[Native12StageGeometry];
    handle.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!handle.pDrvPrivate)
    {
        HeapFree(GetProcessHeap(), 0, declaration);
        return E_OUTOFMEMORY;
    }
    device->functions.pfnCreateGeometryShaderWithStreamOutput(device->driver_device, &args, handle);
    pipeline->shader_created[Native12StageGeometry] = true;
    HeapFree(GetProcessHeap(), 0, declaration);
    return device->removed_reason;
}

static UINT Native12FormatBytes(DXGI_FORMAT format)
{
    Native12FormatInfo info = {};
    return Native12GetFormatInfo(format, &info) ? info.block_bytes : 0;
}

static Native12RootSignature *Native12EmbeddedRootSignature(Native12Device *device, const D3D12_SHADER_BYTECODE &code)
{
    if (!code.pShaderBytecode || !code.BytecodeLength) return NULL;
    Native12RootSignature *signature = new Native12RootSignature(device);
    if (!signature) return NULL;
    if (FAILED(signature->Initialize(code.pShaderBytecode, code.BytecodeLength)))
    {
        signature->Release();
        return NULL;
    }
    signature->AddInternal();
    signature->Release();
    return signature;
}

static bool Native12DualSourceBlend(D3D12_BLEND blend)
{
    return blend == D3D12_BLEND_SRC1_COLOR || blend == D3D12_BLEND_INV_SRC1_COLOR
            || blend == D3D12_BLEND_SRC1_ALPHA || blend == D3D12_BLEND_INV_SRC1_ALPHA;
}

HRESULT Native12PipelineState::InitializeGraphics(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *input)
{
    static const struct
    {
        Native12Stage stage;
        SIZE_T offset;
    } stages[] =
    {
        { Native12StageVertex, offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, VS) },
        { Native12StagePixel, offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, PS) },
        { Native12StageGeometry, offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, GS) },
        { Native12StageHull, offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, HS) },
        { Native12StageDomain, offsetof(D3D12_GRAPHICS_PIPELINE_STATE_DESC, DS) },
    };
    Native12ShaderCode parsed[ARRAYSIZE(stages)];
    Native12ShaderCode &vertex = parsed[0];
    bool stream_output = input->StreamOutput.NumEntries != 0;
    const D3D12_BLEND_DESC &blend_input = input->BlendState;
    HRESULT hr;

    if (input->NumRenderTargets > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) return E_INVALIDARG;
    if (input->Flags & ~(D3D12_PIPELINE_STATE_FLAG_TOOL_DEBUG | D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS
            | D3D12_PIPELINE_STATE_FLAG_DYNAMIC_INDEX_BUFFER_STRIP_CUT))
        return E_INVALIDARG;
    flags = input->Flags;
    if (!input->VS.pShaderBytecode || !input->VS.BytecodeLength) return E_INVALIDARG;
    if ((input->HS.pShaderBytecode || input->DS.pShaderBytecode)
            && input->PrimitiveTopologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH)
        return E_INVALIDARG;
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        const D3D12_RENDER_TARGET_BLEND_DESC &target = blend_input.RenderTarget[blend_input.IndependentBlendEnable ? i : 0];
        if (target.LogicOpEnable && (target.BlendEnable || blend_input.IndependentBlendEnable)) return E_INVALIDARG;
        if (i >= input->NumRenderTargets && input->RTVFormats[i] != DXGI_FORMAT_UNKNOWN) return E_INVALIDARG;
    }
    const D3D12_RENDER_TARGET_BLEND_DESC &first_target = blend_input.RenderTarget[0];
    if (first_target.BlendEnable && (Native12DualSourceBlend(first_target.SrcBlend)
            || Native12DualSourceBlend(first_target.DestBlend) || Native12DualSourceBlend(first_target.SrcBlendAlpha)
            || Native12DualSourceBlend(first_target.DestBlendAlpha)))
    {
        if (input->NumRenderTargets > 1) return E_INVALIDARG;
        for (UINT i = 1; blend_input.IndependentBlendEnable && i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            if (blend_input.RenderTarget[i].BlendEnable) return E_INVALIDARG;
    }

    if (input->pRootSignature)
    {
        root_signature = static_cast<Native12RootSignature *>(input->pRootSignature);
        root_signature->AddInternal();
    }
    else
    {
        for (UINT i = 0; i < ARRAYSIZE(stages) && !root_signature; ++i)
            root_signature = Native12EmbeddedRootSignature(device,
                    *reinterpret_cast<const D3D12_SHADER_BYTECODE *>(reinterpret_cast<const BYTE *>(input)
                    + stages[i].offset));
        if (!root_signature) return E_INVALIDARG;
    }
    if (stream_output && !(root_signature->flags & D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT)) return E_INVALIDARG;

    for (UINT i = 0; i < ARRAYSIZE(stages); ++i)
    {
        const D3D12_SHADER_BYTECODE &bytecode = *reinterpret_cast<const D3D12_SHADER_BYTECODE *>(
                reinterpret_cast<const BYTE *>(input) + stages[i].offset);
        if (!bytecode.pShaderBytecode || !bytecode.BytecodeLength) continue;
        Native12ShaderCode &shader = parsed[i];
        if (FAILED(hr = shader.Parse(bytecode))) return hr;
        if (stream_output && stages[i].stage == Native12StageGeometry)
            hr = Native12CreateStreamOutputShader(this, shader, false, input->StreamOutput);
        else
            hr = Native12CreateShader(this, stages[i].stage, shader);
        if (FAILED(hr)) return hr;
    }
    if (stream_output && !shader_created[Native12StageGeometry])
    {
        const Native12ShaderCode &last = parsed[4].code ? parsed[4] : parsed[0];
        if (!last.code) return E_INVALIDARG;
        if (FAILED(hr = Native12CreateStreamOutputShader(this, last, true, input->StreamOutput))) return hr;
    }

    D3D12DDI_BLEND_DESC_0010 blend_desc = {};
    blend_desc.AlphaToCoverageEnable = input->BlendState.AlphaToCoverageEnable;
    blend_desc.IndependentBlendEnable = input->BlendState.IndependentBlendEnable;
    for (UINT i = 0; i < 8; ++i)
    {
        const D3D12_RENDER_TARGET_BLEND_DESC &source =
                input->BlendState.RenderTarget[input->BlendState.IndependentBlendEnable ? i : 0];
        D3D12DDI_RENDER_TARGET_BLEND_DESC &target = blend_desc.RenderTarget[i];
        target.BlendEnable = source.BlendEnable;
        target.LogicOpEnable = source.LogicOpEnable;
        target.SrcBlend = static_cast<D3D12DDI_BLEND>(source.SrcBlend);
        target.DestBlend = static_cast<D3D12DDI_BLEND>(source.DestBlend);
        target.BlendOp = static_cast<D3D12DDI_BLEND_OP>(source.BlendOp);
        target.SrcBlendAlpha = static_cast<D3D12DDI_BLEND>(source.SrcBlendAlpha);
        target.DestBlendAlpha = static_cast<D3D12DDI_BLEND>(source.DestBlendAlpha);
        target.BlendOpAlpha = static_cast<D3D12DDI_BLEND_OP>(source.BlendOpAlpha);
        target.LogicOp = static_cast<D3D12DDI_LOGIC_OP>(source.LogicOp);
        target.RenderTargetWriteMask = source.RenderTargetWriteMask;
    }
    SIZE_T size = device->functions.pfnCalcPrivateBlendStateSize(device->driver_device, &blend_desc);
    blend.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!blend.pDrvPrivate) return E_OUTOFMEMORY;
    device->functions.pfnCreateBlendState(device->driver_device, &blend_desc, blend);
    blend_created = true;

    D3D12DDI_RASTERIZER_DESC_0102 raster_desc = {};
    raster_desc.FillMode = static_cast<D3D12DDI_FILL_MODE>(input->RasterizerState.FillMode);
    raster_desc.CullMode = static_cast<D3D12DDI_CULL_MODE>(input->RasterizerState.CullMode);
    raster_desc.FrontCounterClockwise = input->RasterizerState.FrontCounterClockwise;
    raster_desc.DepthBias = float_depth_bias ? depth_bias : static_cast<FLOAT>(input->RasterizerState.DepthBias);
    raster_desc.DepthBiasClamp = input->RasterizerState.DepthBiasClamp;
    raster_desc.SlopeScaledDepthBias = input->RasterizerState.SlopeScaledDepthBias;
    raster_desc.DepthClipEnable = input->RasterizerState.DepthClipEnable;
    raster_desc.ScissorEnable = TRUE;
    raster_desc.LineRasterizationMode = line_mode_set ? static_cast<D3D12DDI_LINE_RASTERIZATION_MODE>(line_mode)
            : input->RasterizerState.MultisampleEnable ? D3D12DDI_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE
            : input->RasterizerState.AntialiasedLineEnable ? D3D12DDI_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED
            : D3D12DDI_LINE_RASTERIZATION_MODE_ALIASED;
    raster_desc.ForcedSampleCount = input->RasterizerState.ForcedSampleCount;
    raster_desc.ConservativeRasterizationMode =
            static_cast<D3D12DDI_CONSERVATIVE_RASTERIZATION_MODE>(input->RasterizerState.ConservativeRaster);
    size = device->functions.pfnCalcPrivateRasterizerStateSize(device->driver_device, &raster_desc);
    rasterizer.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!rasterizer.pDrvPrivate) return E_OUTOFMEMORY;
    device->functions.pfnCreateRasterizerState(device->driver_device, &raster_desc, rasterizer);
    rasterizer_created = true;

    const D3D12_DEPTH_STENCIL_DESC &depth_source = input->DepthStencilState;
    D3D12DDI_DEPTH_STENCIL_DESC_0095 depth_desc = {};
    depth_desc.DepthEnable = depth_source.DepthEnable;
    depth_desc.DepthWriteMask = static_cast<D3D12DDI_DEPTH_WRITE_MASK>(depth_source.DepthWriteMask);
    depth_desc.DepthFunc = static_cast<D3D12DDI_COMPARISON_FUNC>(
            depth_source.DepthFunc ? depth_source.DepthFunc : D3D12_COMPARISON_FUNC_ALWAYS);
    depth_desc.StencilEnable = depth_source.StencilEnable;
    depth_desc.FrontEnable = depth_source.StencilEnable;
    depth_desc.BackEnable = depth_source.StencilEnable;
    depth_desc.FrontFaceStencilReadMask = depth_source.StencilReadMask;
    depth_desc.FrontFaceStencilWriteMask = depth_source.StencilWriteMask;
    depth_desc.BackFaceStencilReadMask = face_stencil_masks ? back_stencil_read_mask : depth_source.StencilReadMask;
    depth_desc.BackFaceStencilWriteMask = face_stencil_masks ? back_stencil_write_mask : depth_source.StencilWriteMask;
    depth_desc.DepthBoundsTestEnable = depth_bounds_test;
    const D3D12_DEPTH_STENCILOP_DESC *faces[2] = { &depth_source.FrontFace, &depth_source.BackFace };
    D3D12DDI_DEPTH_STENCILOP_DESC *targets[2] = { &depth_desc.FrontFace, &depth_desc.BackFace };
    for (UINT i = 0; i < 2; ++i)
    {
        targets[i]->StencilFailOp = static_cast<D3D12DDI_STENCIL_OP>(
                faces[i]->StencilFailOp ? faces[i]->StencilFailOp : D3D12_STENCIL_OP_KEEP);
        targets[i]->StencilDepthFailOp = static_cast<D3D12DDI_STENCIL_OP>(
                faces[i]->StencilDepthFailOp ? faces[i]->StencilDepthFailOp : D3D12_STENCIL_OP_KEEP);
        targets[i]->StencilPassOp = static_cast<D3D12DDI_STENCIL_OP>(
                faces[i]->StencilPassOp ? faces[i]->StencilPassOp : D3D12_STENCIL_OP_KEEP);
        targets[i]->StencilFunc = static_cast<D3D12DDI_COMPARISON_FUNC>(
                faces[i]->StencilFunc ? faces[i]->StencilFunc : D3D12_COMPARISON_FUNC_ALWAYS);
    }
    size = device->functions.pfnCalcPrivateDepthStencilStateSize(device->driver_device, &depth_desc);
    depth_stencil.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!depth_stencil.pDrvPrivate) return E_OUTOFMEMORY;
    device->functions.pfnCreateDepthStencilState(device->driver_device, &depth_desc, depth_stencil);
    depth_created = true;

    UINT element_count = 0;
    D3D12DDIARG_INPUT_ELEMENT_DESC *elements = NULL;
    if (input->InputLayout.NumElements)
    {
        UINT next_offset[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        elements = static_cast<D3D12DDIARG_INPUT_ELEMENT_DESC *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                input->InputLayout.NumElements * sizeof(*elements)));
        if (!elements) return E_OUTOFMEMORY;
        for (UINT i = 0; i < input->InputLayout.NumElements; ++i)
        {
            const D3D12_INPUT_ELEMENT_DESC &source = input->InputLayout.pInputElementDescs[i];
            if (source.InputSlot >= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
            {
                HeapFree(GetProcessHeap(), 0, elements);
                return E_INVALIDARG;
            }
            UINT offset = source.AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT
                    ? next_offset[source.InputSlot] : source.AlignedByteOffset;
            next_offset[source.InputSlot] = offset + Native12FormatBytes(source.Format);
            for (UINT j = 0; j < vertex.input.count; ++j)
            {
                if (vertex.input.indices[j] != source.SemanticIndex
                        || _stricmp(vertex.input.names[j], source.SemanticName)) continue;
                D3D12DDIARG_INPUT_ELEMENT_DESC &target = elements[element_count++];
                target.InputSlot = source.InputSlot;
                target.AlignedByteOffset = offset;
                target.Format = source.Format;
                target.InputSlotClass = static_cast<D3D12DDI_INPUT_CLASSIFICATION>(source.InputSlotClass);
                target.InstanceDataStepRate = source.InstanceDataStepRate;
                target.InputRegister = vertex.input.entries[j].Register;
                break;
            }
        }
    }
    D3D12DDIARG_CREATEELEMENTLAYOUT_0010 layout_args = {};
    layout_args.pVertexElements = elements;
    layout_args.NumElements = element_count;
    size = device->functions.pfnCalcPrivateElementLayoutSize(device->driver_device, &layout_args);
    layout.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!layout.pDrvPrivate)
    {
        if (elements) HeapFree(GetProcessHeap(), 0, elements);
        return E_OUTOFMEMORY;
    }
    device->functions.pfnCreateElementLayout(device->driver_device, &layout_args, layout);
    layout_created = true;
    if (elements) HeapFree(GetProcessHeap(), 0, elements);

    D3D12DDIARG_CREATE_PIPELINE_STATE_0099 args = {};
    args.hVertexShader = shaders[Native12StageVertex];
    args.hPixelShader = shaders[Native12StagePixel];
    args.hDomainShader = shaders[Native12StageDomain];
    args.hHullShader = shaders[Native12StageHull];
    args.hGeometryShader = shaders[Native12StageGeometry];
    if (root_signature) args.hRootSignature = root_signature->driver;
    args.hBlendState = blend;
    args.SampleMask = input->SampleMask;
    args.hRasterizerState = rasterizer;
    args.hDepthStencilState = depth_stencil;
    args.hElementLayout = layout;
    args.IBStripCutValue = static_cast<D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE>(input->IBStripCutValue);
    args.PrimitiveTopologyType = static_cast<D3D12DDI_PRIMITIVE_TOPOLOGY_TYPE>(input->PrimitiveTopologyType);
    args.NumRenderTargets = input->NumRenderTargets;
    memcpy(args.RTVFormats, input->RTVFormats, sizeof(args.RTVFormats));
    args.DSVFormat = input->DSVFormat;
    args.SampleDesc = input->SampleDesc;
    if (!args.SampleDesc.Count) args.SampleDesc.Count = 1;
    args.NodeMask = input->NodeMask ? input->NodeMask : 1;
    args.Flags = static_cast<D3D12DDI_PIPELINE_STATE_FLAGS>(input->Flags
            & (D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS | D3D12_PIPELINE_STATE_FLAG_DYNAMIC_INDEX_BUFFER_STRIP_CUT));
    size = device->functions.pfnCalcPrivatePipelineStateSize(device->driver_device, &args);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!driver.pDrvPrivate) return E_OUTOFMEMORY;
    D3D12DDI_HRTPIPELINESTATE runtime = {};
    runtime.handle = this;
    if (FAILED(hr = device->functions.pfnCreatePipelineState(device->driver_device, &args, driver, runtime)))
    {
        WARN("The driver failed to create a graphics pipeline state, hr %#lx.\n", hr);
        return hr;
    }
    driver_created = true;
    return S_OK;
}

HRESULT Native12PipelineState::InitializeCompute(const D3D12_COMPUTE_PIPELINE_STATE_DESC *input)
{
    Native12ShaderCode shader;
    HRESULT hr;

    compute = true;
    if (input->pRootSignature)
    {
        root_signature = static_cast<Native12RootSignature *>(input->pRootSignature);
        root_signature->AddInternal();
    }
    else if (!(root_signature = Native12EmbeddedRootSignature(device, input->CS)))
    {
        return E_INVALIDARG;
    }
    if (FAILED(hr = shader.Parse(input->CS))) return hr;
    if (FAILED(hr = Native12CreateShader(this, Native12StageCompute, shader))) return hr;

    D3D12DDIARG_CREATE_PIPELINE_STATE_0099 args = {};
    args.hComputeShader = shaders[Native12StageCompute];
    if (root_signature) args.hRootSignature = root_signature->driver;
    args.NodeMask = input->NodeMask ? input->NodeMask : 1;
    SIZE_T size = device->functions.pfnCalcPrivatePipelineStateSize(device->driver_device, &args);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!driver.pDrvPrivate) return E_OUTOFMEMORY;
    D3D12DDI_HRTPIPELINESTATE runtime = {};
    runtime.handle = this;
    if (FAILED(hr = device->functions.pfnCreatePipelineState(device->driver_device, &args, driver, runtime)))
    {
        WARN("The driver failed to create a compute pipeline state, hr %#lx.\n", hr);
        return hr;
    }
    driver_created = true;
    return S_OK;
}

Native12PipelineState::~Native12PipelineState()
{
    if (driver_created) device->functions.pfnDestroyPipelineState(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
    if (layout_created) device->functions.pfnDestroyElementLayout(device->driver_device, layout);
    if (layout.pDrvPrivate) HeapFree(GetProcessHeap(), 0, layout.pDrvPrivate);
    if (depth_created) device->functions.pfnDestroyDepthStencilState(device->driver_device, depth_stencil);
    if (depth_stencil.pDrvPrivate) HeapFree(GetProcessHeap(), 0, depth_stencil.pDrvPrivate);
    if (rasterizer_created) device->functions.pfnDestroyRasterizerState(device->driver_device, rasterizer);
    if (rasterizer.pDrvPrivate) HeapFree(GetProcessHeap(), 0, rasterizer.pDrvPrivate);
    if (blend_created) device->functions.pfnDestroyBlendState(device->driver_device, blend);
    if (blend.pDrvPrivate) HeapFree(GetProcessHeap(), 0, blend.pDrvPrivate);
    for (UINT i = 0; i < ShaderCount; ++i)
    {
        if (shader_created[i]) device->functions.pfnDestroyShader(device->driver_device, shaders[i]);
        if (shaders[i].pDrvPrivate) HeapFree(GetProcessHeap(), 0, shaders[i].pDrvPrivate);
    }
    if (root_signature) root_signature->ReleaseInternal();
}

HRESULT STDMETHODCALLTYPE Native12PipelineState::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12PipelineState) || IsEqualGUID(iid, IID_ID3D12Pageable)
            || IsEqualGUID(iid, IID_ID3D12DeviceChild) || IsEqualGUID(iid, IID_ID3D12Object)
            || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12PipelineState *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Native12PipelineState::GetCachedBlob(ID3DBlob **blob)
{
    if (blob) *blob = NULL;
    return E_NOTIMPL;
}

HRESULT Native12CreateDefaultPipelineState(Native12Device *device, bool compute, ID3D12PipelineState **out)
{
    static const UINT vs_code[] =
    {
        0x43425844, 0x12121eb1, 0x1183504a, 0x42959518, 0x8a83ca8b, 0x00000001, 0x000001c0, 0x00000005,
        0x00000034, 0x00000044, 0x00000078, 0x000000dc, 0x00000124, 0x4e475349, 0x00000008, 0x00000000,
        0x00000008, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001,
        0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69, 0x46454452, 0x0000005c,
        0x00000000, 0x0000003c, 0x00000000, 0x0000003c, 0xfffe0500, 0x00000000, 0x0000003c, 0x31314452,
        0x0000003c, 0x00000018, 0x00000020, 0x00000028, 0x00000024, 0x0000000c, 0x00000000, 0x33646b76,
        0x68732d64, 0x72656461, 0x312e3220, 0x69672820, 0x65362074, 0x62643365, 0xab002932, 0x52444853,
        0x00000040, 0x00010050, 0x00000010, 0x0100086a, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x08000036, 0x001020f2, 0x00000000, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x3f800000,
        0x0100003e, 0x54415453, 0x00000094, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    };
    static const UINT cs_code[] =
    {
        0x43425844, 0x63b00360, 0x2d1fa0ba, 0xb6f0df1c, 0x34931766, 0x00000001, 0x0000017c, 0x00000005,
        0x00000034, 0x00000044, 0x00000054, 0x000000b8, 0x000000e0, 0x4e475349, 0x00000008, 0x00000000,
        0x00000008, 0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x46454452, 0x0000005c, 0x00000000,
        0x0000003c, 0x00000000, 0x0000003c, 0x43530500, 0x00000000, 0x0000003c, 0x31314452, 0x0000003c,
        0x00000018, 0x00000020, 0x00000028, 0x00000024, 0x0000000c, 0x00000000, 0x33646b76, 0x68732d64,
        0x72656461, 0x312e3220, 0x69672820, 0x65362074, 0x62643365, 0xab002932, 0x52444853, 0x00000020,
        0x00050050, 0x00000008, 0x0100086a, 0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x0100003e,
        0x54415453, 0x00000094, 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000
    };
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    Native12RootSignature *root_signature;
    ID3DBlob *blob = NULL;
    HRESULT hr;

    *out = NULL;
    if (FAILED(hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, NULL))) return hr;
    if (!(root_signature = new Native12RootSignature(device)))
    {
        blob->Release();
        return E_OUTOFMEMORY;
    }
    hr = root_signature->Initialize(blob->GetBufferPointer(), blob->GetBufferSize());
    blob->Release();
    if (FAILED(hr))
    {
        root_signature->Release();
        return hr;
    }
    if (compute)
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root_signature;
        desc.CS.pShaderBytecode = cs_code;
        desc.CS.BytecodeLength = sizeof(cs_code);
        hr = device->CreateComputePipelineState(&desc, IID_ID3D12PipelineState, reinterpret_cast<void **>(out));
    }
    else
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = root_signature;
        desc.VS.pShaderBytecode = vs_code;
        desc.VS.BytecodeLength = sizeof(vs_code);
        desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.SampleMask = ~0u;
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        desc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        desc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleDesc.Count = 1;
        hr = device->CreateGraphicsPipelineState(&desc, IID_ID3D12PipelineState, reinterpret_cast<void **>(out));
    }
    root_signature->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateGraphicsPipelineState(
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID iid, void **out)
{
    if (!desc || !out) return E_INVALIDARG;
    *out = NULL;
    Native12PipelineState *pipeline = new Native12PipelineState(this);
    if (!pipeline) return E_OUTOFMEMORY;
    HRESULT hr = pipeline->InitializeGraphics(desc);
    if (SUCCEEDED(hr)) hr = pipeline->QueryInterface(iid, out);
    pipeline->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
        REFIID iid, void **out)
{
    if (!desc || !out) return E_INVALIDARG;
    *out = NULL;
    Native12PipelineState *pipeline = new Native12PipelineState(this);
    if (!pipeline) return E_OUTOFMEMORY;
    HRESULT hr = pipeline->InitializeCompute(desc);
    if (SUCCEEDED(hr)) hr = pipeline->QueryInterface(iid, out);
    pipeline->Release();
    return hr;
}
