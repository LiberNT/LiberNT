/*
 * PROJECT:     LiberNT Direct3D 11 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 on 12 device that copies wrapped resources when they are acquired and released
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "d3d11_private.h"
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_4.h>

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

static const GUID d3d11_on12_wrap_guid =
    { 0x3c1f6a52, 0x9d47, 0x4b8e, { 0xa2, 0x61, 0x5e, 0x0c, 0x97, 0x14, 0xd8, 0x3b } };

struct d3d11_on12_device
{
    ID3D11On12Device2 ID3D11On12Device2_iface;
    ID3D11Device *device11;
    ID3D12Device *device12;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    ID3D12Fence *fence;
    UINT64 fence_value;
    HANDLE event;
    CRITICAL_SECTION cs;
};

struct d3d11_on12_wrap
{
    IUnknown IUnknown_iface;
    LONG refcount;
    ID3D11Resource *resource11;
    ID3D12Resource *resource12;
    D3D12_RESOURCE_DESC desc;
    D3D12_RESOURCE_STATES input_state;
    D3D12_RESOURCE_STATES output_state;
    BOOL acquired;
    BOOL unwrapped;
};

static struct d3d11_on12_device *impl_from_ID3D11On12Device2(ID3D11On12Device2 *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_on12_device, ID3D11On12Device2_iface);
}

static struct d3d11_on12_wrap *impl_from_IUnknown_wrap(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_on12_wrap, IUnknown_iface);
}

static HRESULT STDMETHODCALLTYPE d3d11_on12_wrap_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d11_on12_wrap_AddRef(IUnknown *iface)
{
    return InterlockedIncrement(&impl_from_IUnknown_wrap(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE d3d11_on12_wrap_Release(IUnknown *iface)
{
    struct d3d11_on12_wrap *wrap = impl_from_IUnknown_wrap(iface);
    ULONG refcount = InterlockedDecrement(&wrap->refcount);

    if (!refcount)
    {
        ID3D12Resource_Release(wrap->resource12);
        free(wrap);
    }
    return refcount;
}

static const IUnknownVtbl d3d11_on12_wrap_vtbl =
{
    d3d11_on12_wrap_QueryInterface,
    d3d11_on12_wrap_AddRef,
    d3d11_on12_wrap_Release,
};

static struct d3d11_on12_wrap *d3d11_on12_get_wrap(ID3D11Resource *resource)
{
    IUnknown *wrap = NULL;
    UINT size = sizeof(wrap);

    if (!resource || FAILED(ID3D11Resource_GetPrivateData(resource, &d3d11_on12_wrap_guid, &size, &wrap)) || !wrap)
        return NULL;
    IUnknown_Release(wrap);
    return impl_from_IUnknown_wrap(wrap);
}

static UINT d3d11_on12_subresource_count(const D3D12_RESOURCE_DESC *desc)
{
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return 1;
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        return desc->MipLevels;
    return desc->MipLevels * desc->DepthOrArraySize;
}

static HRESULT d3d11_on12_wait(struct d3d11_on12_device *device)
{
    UINT64 value = ++device->fence_value;
    HRESULT hr;

    if (FAILED(hr = ID3D12CommandQueue_Signal(device->queue, device->fence, value)))
        return hr;
    if (ID3D12Fence_GetCompletedValue(device->fence) >= value)
        return S_OK;
    if (FAILED(hr = ID3D12Fence_SetEventOnCompletion(device->fence, value, device->event)))
        return hr;
    WaitForSingleObject(device->event, INFINITE);
    return S_OK;
}

static HRESULT d3d11_on12_begin(struct d3d11_on12_device *device)
{
    HRESULT hr;

    if (FAILED(hr = ID3D12CommandAllocator_Reset(device->allocator)))
        return hr;
    return ID3D12GraphicsCommandList_Reset(device->list, device->allocator, NULL);
}

static HRESULT d3d11_on12_submit(struct d3d11_on12_device *device)
{
    ID3D12CommandList *list;
    HRESULT hr;

    if (FAILED(hr = ID3D12GraphicsCommandList_Close(device->list)))
        return hr;
    list = (ID3D12CommandList *)device->list;
    ID3D12CommandQueue_ExecuteCommandLists(device->queue, 1, &list);
    return d3d11_on12_wait(device);
}

static void d3d11_on12_barrier(struct d3d11_on12_device *device, ID3D12Resource *resource,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier;

    if (before == after)
        return;
    memset(&barrier, 0, sizeof(barrier));
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    ID3D12GraphicsCommandList_ResourceBarrier(device->list, 1, &barrier);
}

static HRESULT d3d11_on12_create_buffer(struct d3d11_on12_device *device, D3D12_HEAP_TYPE type, UINT64 size,
        D3D12_RESOURCE_STATES state, ID3D12Resource **buffer)
{
    D3D12_HEAP_PROPERTIES heap;
    D3D12_RESOURCE_DESC desc;

    memset(&heap, 0, sizeof(heap));
    heap.Type = type;
    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size ? size : 1;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return ID3D12Device_CreateCommittedResource(device->device12, &heap, D3D12_HEAP_FLAG_NONE, &desc, state, NULL,
            &IID_ID3D12Resource, (void **)buffer);
}

static HRESULT d3d11_on12_create_staging(struct d3d11_on12_device *device, ID3D11Resource *resource,
        ID3D11Resource **staging)
{
    D3D11_RESOURCE_DIMENSION dimension;
    HRESULT hr = E_INVALIDARG;

    ID3D11Resource_GetType(resource, &dimension);
    if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
    {
        D3D11_BUFFER_DESC desc;
        ID3D11Buffer *buffer;

        ID3D11Buffer_GetDesc((ID3D11Buffer *)resource, &desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        desc.StructureByteStride = 0;
        if (SUCCEEDED(hr = ID3D11Device_CreateBuffer(device->device11, &desc, NULL, &buffer)))
            *staging = (ID3D11Resource *)buffer;
    }
    else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
    {
        D3D11_TEXTURE1D_DESC desc;
        ID3D11Texture1D *texture;

        ID3D11Texture1D_GetDesc((ID3D11Texture1D *)resource, &desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        if (SUCCEEDED(hr = ID3D11Device_CreateTexture1D(device->device11, &desc, NULL, &texture)))
            *staging = (ID3D11Resource *)texture;
    }
    else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        D3D11_TEXTURE2D_DESC desc;
        ID3D11Texture2D *texture;

        ID3D11Texture2D_GetDesc((ID3D11Texture2D *)resource, &desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags &= D3D11_RESOURCE_MISC_TEXTURECUBE;
        if (SUCCEEDED(hr = ID3D11Device_CreateTexture2D(device->device11, &desc, NULL, &texture)))
            *staging = (ID3D11Resource *)texture;
    }
    else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
    {
        D3D11_TEXTURE3D_DESC desc;
        ID3D11Texture3D *texture;

        ID3D11Texture3D_GetDesc((ID3D11Texture3D *)resource, &desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = 0;
        if (SUCCEEDED(hr = ID3D11Device_CreateTexture3D(device->device11, &desc, NULL, &texture)))
            *staging = (ID3D11Resource *)texture;
    }
    return hr;
}

struct d3d11_on12_layout
{
    UINT count;
    UINT64 total;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts;
    UINT *rows;
    UINT64 *row_sizes;
};

static HRESULT d3d11_on12_get_layout(struct d3d11_on12_device *device, const D3D12_RESOURCE_DESC *desc,
        struct d3d11_on12_layout *layout)
{
    layout->count = d3d11_on12_subresource_count(desc);
    layout->layouts = calloc(layout->count, sizeof(*layout->layouts));
    layout->rows = calloc(layout->count, sizeof(*layout->rows));
    layout->row_sizes = calloc(layout->count, sizeof(*layout->row_sizes));
    if (!layout->layouts || !layout->rows || !layout->row_sizes)
        return E_OUTOFMEMORY;
    ID3D12Device_GetCopyableFootprints(device->device12, desc, 0, layout->count, 0, layout->layouts,
            layout->rows, layout->row_sizes, &layout->total);
    return S_OK;
}

static void d3d11_on12_free_layout(struct d3d11_on12_layout *layout)
{
    free(layout->layouts);
    free(layout->rows);
    free(layout->row_sizes);
}

static void d3d11_on12_record_copies(struct d3d11_on12_device *device, struct d3d11_on12_wrap *wrap,
        ID3D12Resource *buffer, const struct d3d11_on12_layout *layout, BOOL to_buffer)
{
    D3D12_TEXTURE_COPY_LOCATION resource_location, buffer_location;
    UINT i;

    if (wrap->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        if (to_buffer)
            ID3D12GraphicsCommandList_CopyBufferRegion(device->list, buffer, 0, wrap->resource12, 0, wrap->desc.Width);
        else
            ID3D12GraphicsCommandList_CopyBufferRegion(device->list, wrap->resource12, 0, buffer, 0, wrap->desc.Width);
        return;
    }
    for (i = 0; i < layout->count; ++i)
    {
        resource_location.pResource = wrap->resource12;
        resource_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        resource_location.SubresourceIndex = i;
        buffer_location.pResource = buffer;
        buffer_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        buffer_location.PlacedFootprint = layout->layouts[i];
        if (to_buffer)
            ID3D12GraphicsCommandList_CopyTextureRegion(device->list, &buffer_location, 0, 0, 0, &resource_location, NULL);
        else
            ID3D12GraphicsCommandList_CopyTextureRegion(device->list, &resource_location, 0, 0, 0, &buffer_location, NULL);
    }
}

static void d3d11_on12_copy_rows(BYTE *dst, UINT dst_row_pitch, UINT dst_depth_pitch, const BYTE *src,
        UINT src_row_pitch, UINT rows, UINT depth, UINT64 row_size)
{
    UINT y, z;

    for (z = 0; z < depth; ++z)
    {
        for (y = 0; y < rows; ++y)
            memcpy(dst + z * dst_depth_pitch + y * dst_row_pitch, src + (z * rows + y) * src_row_pitch, (SIZE_T)row_size);
    }
}

static HRESULT d3d11_on12_copy_to_d3d11(struct d3d11_on12_device *device, struct d3d11_on12_wrap *wrap)
{
    ID3D11DeviceContext *context = NULL;
    ID3D11Resource *staging = NULL;
    ID3D12Resource *readback = NULL;
    struct d3d11_on12_layout layout = { 0 };
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D12_RANGE range;
    BYTE *data;
    HRESULT hr;
    UINT i;

    if (wrap->desc.SampleDesc.Count > 1)
    {
        FIXME("Multisampled wrapped resources are not copied.\n");
        return S_OK;
    }
    if (FAILED(hr = d3d11_on12_get_layout(device, &wrap->desc, &layout)))
        goto done;
    if (FAILED(hr = d3d11_on12_create_buffer(device, D3D12_HEAP_TYPE_READBACK, layout.total,
            D3D12_RESOURCE_STATE_COPY_DEST, &readback)))
        goto done;
    if (FAILED(hr = d3d11_on12_begin(device)))
        goto done;
    d3d11_on12_barrier(device, wrap->resource12, wrap->input_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    d3d11_on12_record_copies(device, wrap, readback, &layout, TRUE);
    d3d11_on12_barrier(device, wrap->resource12, D3D12_RESOURCE_STATE_COPY_SOURCE, wrap->input_state);
    if (FAILED(hr = d3d11_on12_submit(device)))
        goto done;
    if (FAILED(hr = d3d11_on12_create_staging(device, wrap->resource11, &staging)))
        goto done;
    range.Begin = 0;
    range.End = (SIZE_T)layout.total;
    if (FAILED(hr = ID3D12Resource_Map(readback, 0, &range, (void **)&data)))
        goto done;
    ID3D11Device_GetImmediateContext(device->device11, &context);
    for (i = 0; i < layout.count; ++i)
    {
        if (FAILED(hr = ID3D11DeviceContext_Map(context, staging, i, D3D11_MAP_WRITE, 0, &mapped)))
            break;
        if (wrap->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            memcpy(mapped.pData, data, (SIZE_T)wrap->desc.Width);
        else
            d3d11_on12_copy_rows(mapped.pData, mapped.RowPitch, mapped.DepthPitch, data + layout.layouts[i].Offset,
                    layout.layouts[i].Footprint.RowPitch, layout.rows[i], layout.layouts[i].Footprint.Depth,
                    layout.row_sizes[i]);
        ID3D11DeviceContext_Unmap(context, staging, i);
    }
    range.End = 0;
    ID3D12Resource_Unmap(readback, 0, &range);
    if (SUCCEEDED(hr))
        ID3D11DeviceContext_CopyResource(context, wrap->resource11, staging);

done:
    if (context)
        ID3D11DeviceContext_Release(context);
    if (staging)
        ID3D11Resource_Release(staging);
    if (readback)
        ID3D12Resource_Release(readback);
    d3d11_on12_free_layout(&layout);
    if (FAILED(hr))
        WARN("Failed to copy a Direct3D 12 resource to Direct3D 11, hr %#lx.\n", hr);
    return hr;
}

static HRESULT d3d11_on12_copy_to_d3d12(struct d3d11_on12_device *device, struct d3d11_on12_wrap *wrap)
{
    ID3D11DeviceContext *context = NULL;
    ID3D11Resource *staging = NULL;
    ID3D12Resource *upload = NULL;
    struct d3d11_on12_layout layout = { 0 };
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D12_RANGE range = { 0, 0 };
    BYTE *data;
    HRESULT hr;
    UINT i;

    if (wrap->desc.SampleDesc.Count > 1)
    {
        FIXME("Multisampled wrapped resources are not copied.\n");
        return S_OK;
    }
    if (FAILED(hr = d3d11_on12_get_layout(device, &wrap->desc, &layout)))
        goto done;
    if (FAILED(hr = d3d11_on12_create_staging(device, wrap->resource11, &staging)))
        goto done;
    if (FAILED(hr = d3d11_on12_create_buffer(device, D3D12_HEAP_TYPE_UPLOAD, layout.total,
            D3D12_RESOURCE_STATE_GENERIC_READ, &upload)))
        goto done;
    ID3D11Device_GetImmediateContext(device->device11, &context);
    ID3D11DeviceContext_CopyResource(context, staging, wrap->resource11);
    if (FAILED(hr = ID3D12Resource_Map(upload, 0, &range, (void **)&data)))
        goto done;
    for (i = 0; i < layout.count; ++i)
    {
        if (FAILED(hr = ID3D11DeviceContext_Map(context, staging, i, D3D11_MAP_READ, 0, &mapped)))
            break;
        if (wrap->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            memcpy(data, mapped.pData, (SIZE_T)wrap->desc.Width);
        else
            d3d11_on12_copy_rows(data + layout.layouts[i].Offset, layout.layouts[i].Footprint.RowPitch,
                    layout.layouts[i].Footprint.RowPitch * layout.rows[i], mapped.pData, mapped.RowPitch,
                    layout.rows[i], layout.layouts[i].Footprint.Depth, layout.row_sizes[i]);
        ID3D11DeviceContext_Unmap(context, staging, i);
    }
    ID3D12Resource_Unmap(upload, 0, NULL);
    if (FAILED(hr))
        goto done;
    if (FAILED(hr = d3d11_on12_begin(device)))
        goto done;
    d3d11_on12_barrier(device, wrap->resource12, wrap->input_state, D3D12_RESOURCE_STATE_COPY_DEST);
    d3d11_on12_record_copies(device, wrap, upload, &layout, FALSE);
    d3d11_on12_barrier(device, wrap->resource12, D3D12_RESOURCE_STATE_COPY_DEST, wrap->output_state);
    hr = d3d11_on12_submit(device);

done:
    if (context)
        ID3D11DeviceContext_Release(context);
    if (staging)
        ID3D11Resource_Release(staging);
    if (upload)
        ID3D12Resource_Release(upload);
    d3d11_on12_free_layout(&layout);
    if (FAILED(hr))
        WARN("Failed to copy a Direct3D 11 resource to Direct3D 12, hr %#lx.\n", hr);
    return hr;
}

static void d3d11_on12_acquire(struct d3d11_on12_device *device, struct d3d11_on12_wrap *wrap)
{
    if (wrap->acquired || wrap->unwrapped)
        return;
    d3d11_on12_copy_to_d3d11(device, wrap);
    wrap->acquired = TRUE;
}

static void d3d11_on12_release(struct d3d11_on12_device *device, struct d3d11_on12_wrap *wrap)
{
    if (!wrap->acquired || wrap->unwrapped)
        return;
    d3d11_on12_copy_to_d3d12(device, wrap);
    wrap->acquired = FALSE;
}

static HRESULT STDMETHODCALLTYPE d3d11_on12_device_QueryInterface(ID3D11On12Device2 *iface, REFIID iid, void **out)
{
    struct d3d11_on12_device *device = impl_from_ID3D11On12Device2(iface);

    if (IsEqualGUID(iid, &IID_ID3D11On12Device2)
            || IsEqualGUID(iid, &IID_ID3D11On12Device1)
            || IsEqualGUID(iid, &IID_ID3D11On12Device))
    {
        ID3D11On12Device2_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    return ID3D11Device_QueryInterface(device->device11, iid, out);
}

static ULONG STDMETHODCALLTYPE d3d11_on12_device_AddRef(ID3D11On12Device2 *iface)
{
    return ID3D11Device_AddRef(impl_from_ID3D11On12Device2(iface)->device11);
}

static ULONG STDMETHODCALLTYPE d3d11_on12_device_Release(ID3D11On12Device2 *iface)
{
    return ID3D11Device_Release(impl_from_ID3D11On12Device2(iface)->device11);
}

static HRESULT d3d11_on12_create_d3d11_resource(struct d3d11_on12_device *device, const D3D12_RESOURCE_DESC *desc,
        const D3D11_RESOURCE_FLAGS *flags, ID3D11Resource **resource)
{
    HRESULT hr = E_INVALIDARG;

    switch (desc->Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
        {
            D3D11_BUFFER_DESC desc11;
            ID3D11Buffer *buffer;

            if (desc->Width > UINT_MAX)
                return E_INVALIDARG;
            desc11.ByteWidth = (UINT)desc->Width;
            desc11.Usage = D3D11_USAGE_DEFAULT;
            desc11.BindFlags = flags->BindFlags;
            desc11.CPUAccessFlags = flags->CPUAccessFlags;
            desc11.MiscFlags = flags->MiscFlags;
            desc11.StructureByteStride = flags->StructureByteStride;
            if (SUCCEEDED(hr = ID3D11Device_CreateBuffer(device->device11, &desc11, NULL, &buffer)))
                *resource = (ID3D11Resource *)buffer;
            break;
        }
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        {
            D3D11_TEXTURE1D_DESC desc11;
            ID3D11Texture1D *texture;

            desc11.Width = (UINT)desc->Width;
            desc11.MipLevels = desc->MipLevels;
            desc11.ArraySize = desc->DepthOrArraySize;
            desc11.Format = desc->Format;
            desc11.Usage = D3D11_USAGE_DEFAULT;
            desc11.BindFlags = flags->BindFlags;
            desc11.CPUAccessFlags = flags->CPUAccessFlags;
            desc11.MiscFlags = flags->MiscFlags;
            if (SUCCEEDED(hr = ID3D11Device_CreateTexture1D(device->device11, &desc11, NULL, &texture)))
                *resource = (ID3D11Resource *)texture;
            break;
        }
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        {
            D3D11_TEXTURE2D_DESC desc11;
            ID3D11Texture2D *texture;

            desc11.Width = (UINT)desc->Width;
            desc11.Height = desc->Height;
            desc11.MipLevels = desc->MipLevels;
            desc11.ArraySize = desc->DepthOrArraySize;
            desc11.Format = desc->Format;
            desc11.SampleDesc.Count = desc->SampleDesc.Count;
            desc11.SampleDesc.Quality = desc->SampleDesc.Quality;
            desc11.Usage = D3D11_USAGE_DEFAULT;
            desc11.BindFlags = flags->BindFlags;
            desc11.CPUAccessFlags = flags->CPUAccessFlags;
            desc11.MiscFlags = flags->MiscFlags;
            if (SUCCEEDED(hr = ID3D11Device_CreateTexture2D(device->device11, &desc11, NULL, &texture)))
                *resource = (ID3D11Resource *)texture;
            break;
        }
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
        {
            D3D11_TEXTURE3D_DESC desc11;
            ID3D11Texture3D *texture;

            desc11.Width = (UINT)desc->Width;
            desc11.Height = desc->Height;
            desc11.Depth = desc->DepthOrArraySize;
            desc11.MipLevels = desc->MipLevels;
            desc11.Format = desc->Format;
            desc11.Usage = D3D11_USAGE_DEFAULT;
            desc11.BindFlags = flags->BindFlags;
            desc11.CPUAccessFlags = flags->CPUAccessFlags;
            desc11.MiscFlags = flags->MiscFlags;
            if (SUCCEEDED(hr = ID3D11Device_CreateTexture3D(device->device11, &desc11, NULL, &texture)))
                *resource = (ID3D11Resource *)texture;
            break;
        }
        default:
            break;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_on12_device_CreateWrappedResource(ID3D11On12Device2 *iface,
        IUnknown *d3d12_resource, const D3D11_RESOURCE_FLAGS *flags, D3D12_RESOURCE_STATES input_state,
        D3D12_RESOURCE_STATES output_state, REFIID iid, void **d3d11_resource)
{
    struct d3d11_on12_device *device = impl_from_ID3D11On12Device2(iface);
    ID3D11Resource *resource11 = NULL;
    ID3D12Resource *resource12;
    struct d3d11_on12_wrap *wrap;
    HRESULT hr;

    TRACE("iface %p, d3d12_resource %p, flags %p, input_state %#x, output_state %#x, iid %s, d3d11_resource %p.\n",
            iface, d3d12_resource, flags, input_state, output_state, debugstr_guid(iid), d3d11_resource);

    if (!d3d11_resource)
        return E_INVALIDARG;
    *d3d11_resource = NULL;
    if (!d3d12_resource || !flags)
        return E_INVALIDARG;
    if (FAILED(IUnknown_QueryInterface(d3d12_resource, &IID_ID3D12Resource, (void **)&resource12)))
        return E_INVALIDARG;
    if (!(wrap = calloc(1, sizeof(*wrap))))
    {
        ID3D12Resource_Release(resource12);
        return E_OUTOFMEMORY;
    }
    wrap->IUnknown_iface.lpVtbl = &d3d11_on12_wrap_vtbl;
    wrap->refcount = 1;
    wrap->resource12 = resource12;
    resource12->lpVtbl->GetDesc(resource12, &wrap->desc);
    wrap->input_state = input_state;
    wrap->output_state = output_state;

    if (FAILED(hr = d3d11_on12_create_d3d11_resource(device, &wrap->desc, flags, &resource11)))
    {
        WARN("Failed to create the Direct3D 11 resource, hr %#lx.\n", hr);
        ID3D12Resource_Release(resource12);
        free(wrap);
        return hr;
    }
    wrap->resource11 = resource11;
    hr = ID3D11Resource_SetPrivateDataInterface(resource11, &d3d11_on12_wrap_guid, &wrap->IUnknown_iface);
    IUnknown_Release(&wrap->IUnknown_iface);
    if (SUCCEEDED(hr))
    {
        EnterCriticalSection(&device->cs);
        d3d11_on12_acquire(device, wrap);
        LeaveCriticalSection(&device->cs);
        hr = ID3D11Resource_QueryInterface(resource11, iid, d3d11_resource);
    }
    ID3D11Resource_Release(resource11);
    return hr;
}

static void STDMETHODCALLTYPE d3d11_on12_device_ReleaseWrappedResources(ID3D11On12Device2 *iface,
        ID3D11Resource * const *resources, UINT count)
{
    struct d3d11_on12_device *device = impl_from_ID3D11On12Device2(iface);
    struct d3d11_on12_wrap *wrap;
    UINT i;

    TRACE("iface %p, resources %p, count %u.\n", iface, resources, count);

    EnterCriticalSection(&device->cs);
    for (i = 0; i < count; ++i)
    {
        if ((wrap = d3d11_on12_get_wrap(resources[i])))
            d3d11_on12_release(device, wrap);
    }
    LeaveCriticalSection(&device->cs);
}

static void STDMETHODCALLTYPE d3d11_on12_device_AcquireWrappedResources(ID3D11On12Device2 *iface,
        ID3D11Resource * const *resources, UINT count)
{
    struct d3d11_on12_device *device = impl_from_ID3D11On12Device2(iface);
    struct d3d11_on12_wrap *wrap;
    UINT i;

    TRACE("iface %p, resources %p, count %u.\n", iface, resources, count);

    EnterCriticalSection(&device->cs);
    for (i = 0; i < count; ++i)
    {
        if ((wrap = d3d11_on12_get_wrap(resources[i])))
            d3d11_on12_acquire(device, wrap);
    }
    LeaveCriticalSection(&device->cs);
}

static HRESULT STDMETHODCALLTYPE d3d11_on12_device_GetD3D12Device(ID3D11On12Device2 *iface, REFIID iid,
        ID3D12Device **d3d12_device)
{
    struct d3d11_on12_device *device = impl_from_ID3D11On12Device2(iface);

    TRACE("iface %p, iid %s, d3d12_device %p.\n", iface, debugstr_guid(iid), d3d12_device);

    if (!d3d12_device)
        return E_INVALIDARG;
    return ID3D12Device_QueryInterface(device->device12, iid, (void **)d3d12_device);
}

static HRESULT STDMETHODCALLTYPE d3d11_on12_device_UnwrapUnderlyingResource(ID3D11On12Device2 *iface,
        ID3D11Resource *d3d11_resource, ID3D12CommandQueue *queue, REFIID iid, void **d3d12_resource)
{
    struct d3d11_on12_device *device = impl_from_ID3D11On12Device2(iface);
    struct d3d11_on12_wrap *wrap;
    HRESULT hr;

    TRACE("iface %p, d3d11_resource %p, queue %p, iid %s, d3d12_resource %p.\n",
            iface, d3d11_resource, queue, debugstr_guid(iid), d3d12_resource);

    if (!d3d12_resource)
        return E_INVALIDARG;
    *d3d12_resource = NULL;
    if (!queue || !(wrap = d3d11_on12_get_wrap(d3d11_resource)))
        return E_INVALIDARG;
    EnterCriticalSection(&device->cs);
    d3d11_on12_release(device, wrap);
    wrap->unwrapped = TRUE;
    if (FAILED(hr = d3d11_on12_wait(device)) || FAILED(hr = ID3D12CommandQueue_Wait(queue, device->fence, device->fence_value)))
    {
        LeaveCriticalSection(&device->cs);
        return hr;
    }
    LeaveCriticalSection(&device->cs);
    return ID3D12Resource_QueryInterface(wrap->resource12, iid, d3d12_resource);
}

static HRESULT STDMETHODCALLTYPE d3d11_on12_device_ReturnUnderlyingResource(ID3D11On12Device2 *iface,
        ID3D11Resource *d3d11_resource, UINT num_sync, UINT64 *signal_values, ID3D12Fence **fences)
{
    struct d3d11_on12_device *device = impl_from_ID3D11On12Device2(iface);
    struct d3d11_on12_wrap *wrap;
    HRESULT hr = S_OK;
    UINT i;

    TRACE("iface %p, d3d11_resource %p, num_sync %u, signal_values %p, fences %p.\n",
            iface, d3d11_resource, num_sync, signal_values, fences);

    if (!(wrap = d3d11_on12_get_wrap(d3d11_resource)) || (num_sync && (!signal_values || !fences)))
        return E_INVALIDARG;
    EnterCriticalSection(&device->cs);
    for (i = 0; i < num_sync && SUCCEEDED(hr); ++i)
        hr = ID3D12CommandQueue_Wait(device->queue, fences[i], signal_values[i]);
    if (SUCCEEDED(hr))
    {
        wrap->unwrapped = FALSE;
        wrap->acquired = FALSE;
        d3d11_on12_acquire(device, wrap);
    }
    LeaveCriticalSection(&device->cs);
    return hr;
}

static const ID3D11On12Device2Vtbl d3d11_on12_device_vtbl =
{
    d3d11_on12_device_QueryInterface,
    d3d11_on12_device_AddRef,
    d3d11_on12_device_Release,
    d3d11_on12_device_CreateWrappedResource,
    d3d11_on12_device_ReleaseWrappedResources,
    d3d11_on12_device_AcquireWrappedResources,
    d3d11_on12_device_GetD3D12Device,
    d3d11_on12_device_UnwrapUnderlyingResource,
    d3d11_on12_device_ReturnUnderlyingResource,
};

void d3d11_on12_destroy(IUnknown *iface)
{
    struct d3d11_on12_device *device = impl_from_ID3D11On12Device2((ID3D11On12Device2 *)iface);

    if (device->list)
        ID3D12GraphicsCommandList_Release(device->list);
    if (device->allocator)
        ID3D12CommandAllocator_Release(device->allocator);
    if (device->fence)
        ID3D12Fence_Release(device->fence);
    if (device->queue)
        ID3D12CommandQueue_Release(device->queue);
    if (device->device12)
        ID3D12Device_Release(device->device12);
    if (device->event)
        CloseHandle(device->event);
    DeleteCriticalSection(&device->cs);
    free(device);
}

HRESULT d3d11_on12_create_device(IUnknown *device, UINT flags, const D3D_FEATURE_LEVEL *feature_levels,
        UINT feature_level_count, IUnknown * const *queues, UINT queue_count, UINT node_mask,
        ID3D11Device **d3d11_device, ID3D11DeviceContext **d3d11_context, D3D_FEATURE_LEVEL *obtained_feature_level)
{
    struct d3d11_on12_device *on12 = NULL;
    ID3D11DeviceContext *context11 = NULL;
    ID3D11Device *device11 = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory4 *factory = NULL;
    D3D_FEATURE_LEVEL level;
    ID3D12Device *device12;
    LUID luid;
    HRESULT hr;

    if (d3d11_device)
        *d3d11_device = NULL;
    if (d3d11_context)
        *d3d11_context = NULL;
    if (!device || !queues || !queue_count || !queues[0] || node_mask > 1)
        return E_INVALIDARG;
    if (FAILED(IUnknown_QueryInterface(device, &IID_ID3D12Device, (void **)&device12)))
        return E_INVALIDARG;
    if (!(on12 = calloc(1, sizeof(*on12))))
    {
        ID3D12Device_Release(device12);
        return E_OUTOFMEMORY;
    }
    on12->ID3D11On12Device2_iface.lpVtbl = &d3d11_on12_device_vtbl;
    on12->device12 = device12;
    InitializeCriticalSection(&on12->cs);

    if (FAILED(hr = IUnknown_QueryInterface(queues[0], &IID_ID3D12CommandQueue, (void **)&on12->queue)))
        goto fail;
    if (FAILED(hr = ID3D12Device_CreateCommandAllocator(device12, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void **)&on12->allocator)))
        goto fail;
    if (FAILED(hr = ID3D12Device_CreateCommandList(device12, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, on12->allocator,
            NULL, &IID_ID3D12GraphicsCommandList, (void **)&on12->list)))
        goto fail;
    if (FAILED(hr = ID3D12GraphicsCommandList_Close(on12->list)))
        goto fail;
    if (FAILED(hr = ID3D12Device_CreateFence(device12, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence,
            (void **)&on12->fence)))
        goto fail;
    if (!(on12->event = CreateEventW(NULL, FALSE, FALSE, NULL)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto fail;
    }

    device12->lpVtbl->GetAdapterLuid(device12, &luid);
    if (FAILED(hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory)))
        goto fail;
    if (FAILED(hr = IDXGIFactory4_EnumAdapterByLuid(factory, luid, &IID_IDXGIAdapter, (void **)&adapter)))
        goto fail;
    if (FAILED(hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, flags, feature_levels,
            feature_level_count, D3D11_SDK_VERSION, &device11, &level, &context11)))
        goto fail;
    if (FAILED(hr = d3d11_device_attach_on12(device11, (IUnknown *)&on12->ID3D11On12Device2_iface)))
        goto fail;
    on12->device11 = device11;
    IDXGIAdapter_Release(adapter);
    IDXGIFactory4_Release(factory);

    if (obtained_feature_level)
        *obtained_feature_level = level;
    if (d3d11_context)
        *d3d11_context = context11;
    else
        ID3D11DeviceContext_Release(context11);
    if (d3d11_device)
        *d3d11_device = device11;
    else
        ID3D11Device_Release(device11);
    return S_OK;

fail:
    WARN("Failed to create a Direct3D 11 on 12 device, hr %#lx.\n", hr);
    if (context11)
        ID3D11DeviceContext_Release(context11);
    if (device11)
        ID3D11Device_Release(device11);
    if (adapter)
        IDXGIAdapter_Release(adapter);
    if (factory)
        IDXGIFactory4_Release(factory);
    d3d11_on12_destroy((IUnknown *)&on12->ID3D11On12Device2_iface);
    return hr;
}
