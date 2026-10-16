/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ID3D12Device6 to ID3D12Device14 methods
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "native12.h"
#include "native12_device.h"
#include "native12_objects.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d12);

HRESULT STDMETHODCALLTYPE Native12Device::SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE mode,
        D3D12_MEASUREMENTS_ACTION action, HANDLE event, BOOL *further_measurements_desired)
{
    if (mode > D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_PROFILING_BY_SYSTEM
            || action > D3D12_MEASUREMENTS_ACTION_DISCARD_PREVIOUS)
        return E_INVALIDARG;
    if (mode == D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_BACKGROUND_WORK
            || mode == D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_PROFILING_BY_SYSTEM
            || action == D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS_HIGH_PRIORITY)
        return DXGI_ERROR_SDK_COMPONENT_MISSING;
    if (event && action != D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS) return E_INVALIDARG;
    if (action == D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS)
    {
        if (further_measurements_desired) *further_measurements_desired = FALSE;
        if (event) SetEvent(event);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Native12Device::AddToStateObject(const D3D12_STATE_OBJECT_DESC *, ID3D12StateObject *,
        REFIID, void **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateProtectedResourceSession1(const D3D12_PROTECTED_RESOURCE_SESSION_DESC1 *,
        REFIID, void **)
{
    return E_INVALIDARG;
}

static bool Native12ResourceDesc(const D3D12_RESOURCE_DESC1 *in, D3D12_RESOURCE_DESC *out)
{
    out->Dimension = in->Dimension;
    out->Alignment = in->Alignment;
    out->Width = in->Width;
    out->Height = in->Height;
    out->DepthOrArraySize = in->DepthOrArraySize;
    out->MipLevels = in->MipLevels;
    out->Format = in->Format;
    out->SampleDesc = in->SampleDesc;
    out->Layout = in->Layout;
    out->Flags = in->Flags;
    return !in->SamplerFeedbackMipRegion.Width && !in->SamplerFeedbackMipRegion.Height
            && !in->SamplerFeedbackMipRegion.Depth;
}

D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE Native12Device::GetResourceAllocationInfo2(
        D3D12_RESOURCE_ALLOCATION_INFO *result, UINT visible_mask, UINT count, const D3D12_RESOURCE_DESC1 *descs,
        D3D12_RESOURCE_ALLOCATION_INFO1 *infos)
{
    UINT64 offset = 0, alignment = 0;
    for (UINT i = 0; i < count; ++i)
    {
        D3D12_RESOURCE_ALLOCATION_INFO info;
        D3D12_RESOURCE_DESC desc;
        bool valid = Native12ResourceDesc(&descs[i], &desc);
        GetResourceAllocationInfo(&info, visible_mask, 1, &desc);
        if (!valid || info.SizeInBytes == ~(UINT64)0)
        {
            result->SizeInBytes = ~(UINT64)0;
            result->Alignment = info.Alignment;
            return result;
        }
        offset = (offset + info.Alignment - 1) & ~(info.Alignment - 1);
        if (infos)
        {
            infos[i].Offset = offset;
            infos[i].Alignment = info.Alignment;
            infos[i].SizeInBytes = info.SizeInBytes;
        }
        offset += info.SizeInBytes;
        if (info.Alignment > alignment) alignment = info.Alignment;
    }
    result->SizeInBytes = offset;
    result->Alignment = alignment;
    return result;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateCommittedResource2(const D3D12_HEAP_PROPERTIES *properties,
        D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES state,
        const D3D12_CLEAR_VALUE *clear, ID3D12ProtectedResourceSession *session, REFIID iid, void **out)
{
    D3D12_RESOURCE_DESC desc0;
    if (!desc) return CreateCommittedResource1(properties, flags, NULL, state, clear, session, iid, out);
    if (!Native12ResourceDesc(desc, &desc0))
    {
        if (out) *out = NULL;
        return E_INVALIDARG;
    }
    return CreateCommittedResource1(properties, flags, &desc0, state, clear, session, iid, out);
}

HRESULT STDMETHODCALLTYPE Native12Device::CreatePlacedResource1(ID3D12Heap *heap, UINT64 offset,
        const D3D12_RESOURCE_DESC1 *desc, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear, REFIID iid,
        void **out)
{
    D3D12_RESOURCE_DESC desc0;
    if (!desc) return CreatePlacedResource(heap, offset, NULL, state, clear, iid, out);
    if (!Native12ResourceDesc(desc, &desc0))
    {
        if (out) *out = NULL;
        return E_INVALIDARG;
    }
    return CreatePlacedResource(heap, offset, &desc0, state, clear, iid, out);
}

void STDMETHODCALLTYPE Native12Device::CreateSamplerFeedbackUnorderedAccessView(ID3D12Resource *, ID3D12Resource *,
        D3D12_CPU_DESCRIPTOR_HANDLE)
{
    WARN("Sampler feedback is not supported.\n");
}

void STDMETHODCALLTYPE Native12Device::GetCopyableFootprints1(const D3D12_RESOURCE_DESC1 *desc, UINT first,
        UINT count, UINT64 base_offset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts, UINT *rows, UINT64 *row_sizes,
        UINT64 *total_bytes)
{
    D3D12_RESOURCE_DESC desc0;
    if (desc) Native12ResourceDesc(desc, &desc0);
    GetCopyableFootprints(desc ? &desc0 : NULL, first, count, base_offset, layouts, rows, row_sizes, total_bytes);
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateShaderCacheSession(const D3D12_SHADER_CACHE_SESSION_DESC *desc,
        REFIID iid, void **out)
{
    return vkd3d_create_shader_cache_session(static_cast<ID3D12Device10 *>(this), desc, &iid, out);
}

HRESULT STDMETHODCALLTYPE Native12Device::ShaderCacheControl(D3D12_SHADER_CACHE_KIND_FLAGS kinds,
        D3D12_SHADER_CACHE_CONTROL_FLAGS control)
{
    const UINT all_kinds = D3D12_SHADER_CACHE_KIND_FLAG_IMPLICIT_D3D_CACHE_FOR_DRIVER
            | D3D12_SHADER_CACHE_KIND_FLAG_IMPLICIT_D3D_CONVERSIONS | D3D12_SHADER_CACHE_KIND_FLAG_IMPLICIT_DRIVER_MANAGED
            | D3D12_SHADER_CACHE_KIND_FLAG_APPLICATION_MANAGED;
    const UINT all_controls = D3D12_SHADER_CACHE_CONTROL_FLAG_DISABLE | D3D12_SHADER_CACHE_CONTROL_FLAG_ENABLE
            | D3D12_SHADER_CACHE_CONTROL_FLAG_CLEAR;

    if (!kinds || (kinds & ~all_kinds) || !control || (control & ~all_controls)
            || ((control & D3D12_SHADER_CACHE_CONTROL_FLAG_DISABLE) && (control & D3D12_SHADER_CACHE_CONTROL_FLAG_ENABLE)))
        return E_INVALIDARG;
    return DXGI_ERROR_SDK_COMPONENT_MISSING;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateCommandQueue1(const D3D12_COMMAND_QUEUE_DESC *desc, REFIID,
        REFIID iid, void **out)
{
    return CreateCommandQueue(desc, iid, out);
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateCommittedResource3(const D3D12_HEAP_PROPERTIES *, D3D12_HEAP_FLAGS,
        const D3D12_RESOURCE_DESC1 *, D3D12_BARRIER_LAYOUT, const D3D12_CLEAR_VALUE *, ID3D12ProtectedResourceSession *,
        UINT32, DXGI_FORMAT *, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreatePlacedResource2(ID3D12Heap *, UINT64, const D3D12_RESOURCE_DESC1 *,
        D3D12_BARRIER_LAYOUT, const D3D12_CLEAR_VALUE *, UINT32, DXGI_FORMAT *, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateReservedResource2(const D3D12_RESOURCE_DESC *, D3D12_BARRIER_LAYOUT,
        const D3D12_CLEAR_VALUE *, ID3D12ProtectedResourceSession *, UINT32, DXGI_FORMAT *, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_INVALIDARG;
}

D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE Native12Device::GetResourceAllocationInfo3(
        D3D12_RESOURCE_ALLOCATION_INFO *result, UINT visible_mask, UINT count, const D3D12_RESOURCE_DESC1 *descs,
        const UINT32 *, const DXGI_FORMAT *const *, D3D12_RESOURCE_ALLOCATION_INFO1 *infos)
{
    return GetResourceAllocationInfo2(result, visible_mask, count, descs, infos);
}

HRESULT STDMETHODCALLTYPE Native12Device::OpenExistingHeapFromAddress1(const void *address, SIZE_T, REFIID iid,
        void **out)
{
    return OpenExistingHeapFromAddress(address, iid, out);
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateRootSignatureFromSubobjectInLibrary(UINT, const void *, SIZE_T,
        LPCWSTR, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_INVALIDARG;
}

D3D12_DEVICE_CONFIGURATION_DESC *STDMETHODCALLTYPE Native12Device::GetDesc(D3D12_DEVICE_CONFIGURATION_DESC *desc)
{
    desc->Flags = static_cast<D3D12_DEVICE_FLAGS>(configuration_flags);
    desc->GpuBasedValidationFlags = D3D12_GPU_BASED_VALIDATION_FLAGS_NONE;
    desc->SDKVersion = 612;
    desc->NumEnabledExperimentalFeatures = 0;
    return desc;
}

HRESULT STDMETHODCALLTYPE Native12Device::GetEnabledExperimentalFeatures(GUID *, UINT)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Native12Device::SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3DBlob **blob, ID3DBlob **error)
{
    return D3D12SerializeVersionedRootSignature(desc, blob, error);
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateVersionedRootSignatureDeserializer(const void *data, SIZE_T size,
        REFIID iid, void **out)
{
    return D3D12CreateVersionedRootSignatureDeserializer(data, size, iid, out);
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateVersionedRootSignatureDeserializerFromSubobjectInLibrary(const void *,
        SIZE_T, LPCWSTR, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_INVALIDARG;
}
