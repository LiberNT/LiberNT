/*
 * PROJECT:     LiberNT Direct3D 11 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11.2 to 11.4 device and context methods
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

static_assert(sizeof(D3D11_TILED_RESOURCE_COORDINATE) == sizeof(D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE),
        "Tile coordinate ABI");
static_assert(sizeof(D3D11_TILE_REGION_SIZE) == sizeof(D3DWDDM1_3DDI_TILE_REGION_SIZE)
        && offsetof(D3D11_TILE_REGION_SIZE, Width) == offsetof(D3DWDDM1_3DDI_TILE_REGION_SIZE, Width)
        && offsetof(D3D11_TILE_REGION_SIZE, Depth) == offsetof(D3DWDDM1_3DDI_TILE_REGION_SIZE, Depth),
        "Tile region ABI");
static_assert(offsetof(D3D11_TEXTURE2D_DESC1, TextureLayout) == sizeof(D3D11_TEXTURE2D_DESC), "Texture2D desc prefix");
static_assert(offsetof(D3D11_TEXTURE3D_DESC1, TextureLayout) == sizeof(D3D11_TEXTURE3D_DESC), "Texture3D desc prefix");

HRESULT STDMETHODCALLTYPE NativeAnnotation::QueryInterface(REFIID iid, void **out) { return context->QueryInterface(iid, out); }
ULONG STDMETHODCALLTYPE NativeAnnotation::AddRef() { return context->AddRef(); }
ULONG STDMETHODCALLTYPE NativeAnnotation::Release() { return context->Release(); }

void NativeDevice::SetRemovedReason(HRESULT reason)
{
    NativeLock guard(this);
    if (SUCCEEDED(removed_reason)) removed_reason = reason;
    if (FAILED(removed_reason))
        for (NativeRemovedEvent *entry = removed_events; entry; entry = entry->next) SetEvent(entry->event);
}

HRESULT STDMETHODCALLTYPE NativeDevice::RegisterDeviceRemovedEvent(HANDLE event, DWORD *cookie)
{
    if (!event || !cookie) return E_INVALIDARG;
    *cookie = 0;
    HANDLE copy = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return HRESULT_FROM_WIN32(GetLastError());
    NativeRemovedEvent *entry = static_cast<NativeRemovedEvent *>(HeapAlloc(GetProcessHeap(), 0, sizeof(*entry)));
    if (!entry)
    {
        CloseHandle(copy);
        return E_OUTOFMEMORY;
    }
    NativeLock guard(this);
    entry->event = copy;
    entry->cookie = ++removed_cookie;
    entry->next = removed_events;
    removed_events = entry;
    *cookie = entry->cookie;
    if (FAILED(removed_reason)) SetEvent(copy);
    return S_OK;
}

void STDMETHODCALLTYPE NativeDevice::UnregisterDeviceRemoved(DWORD cookie)
{
    NativeLock guard(this);
    for (NativeRemovedEvent **link = &removed_events; *link; link = &(*link)->next)
    {
        NativeRemovedEvent *entry = *link;
        if (entry->cookie != cookie) continue;
        *link = entry->next;
        CloseHandle(entry->event);
        HeapFree(GetProcessHeap(), 0, entry);
        return;
    }
}

void STDMETHODCALLTYPE NativeDevice::GetImmediateContext2(ID3D11DeviceContext2 **out)
{
    if (out) { *out = context; context->AddRef(); }
}

void STDMETHODCALLTYPE NativeDevice::GetImmediateContext3(ID3D11DeviceContext3 **out)
{
    if (out) { *out = context; context->AddRef(); }
}

template<class Interface>
static HRESULT NativeCreateDeferred(NativeDevice *device, UINT flags, REFIID iid, Interface **out)
{
    if (out) *out = NULL;
    ID3D11DeviceContext *base = NULL;
    HRESULT hr = device->CreateDeferredContext(flags, out ? &base : NULL);
    if (SUCCEEDED(hr) && out)
    {
        hr = base->QueryInterface(iid, reinterpret_cast<void **>(out));
        base->Release();
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateDeferredContext2(UINT flags, ID3D11DeviceContext2 **out)
{
    return NativeCreateDeferred(this, flags, IID_ID3D11DeviceContext2, out);
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateDeferredContext3(UINT flags, ID3D11DeviceContext3 **out)
{
    return NativeCreateDeferred(this, flags, IID_ID3D11DeviceContext3, out);
}

HRESULT STDMETHODCALLTYPE NativeDevice::CheckMultisampleQualityLevels1(DXGI_FORMAT format, UINT samples,
        UINT flags, UINT *quality)
{
    if (!quality) return E_INVALIDARG;
    *quality = 0;
    if (flags & ~D3D11_CHECK_MULTISAMPLE_QUALITY_LEVELS_TILED_RESOURCE) return E_INVALIDARG;
    if (!flags) return CheckMultisampleQualityLevels(format, samples, quality);
    if (!samples || samples > D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT) return E_INVALIDARG;
    if (!wddm20 || !wddm20_functions.pfnCheckMultisampleQualityLevels) return S_OK;
    NativeLock guard(this);
    BeginCall();
    wddm20_functions.pfnCheckMultisampleQualityLevels(driver_device, format, samples, flags, quality);
    return operation_error;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateTexture2D1(const D3D11_TEXTURE2D_DESC1 *desc,
        const D3D11_SUBRESOURCE_DATA *initial, ID3D11Texture2D1 **out)
{
    if (out) *out = NULL;
    if (!desc || desc->TextureLayout != D3D11_TEXTURE_LAYOUT_UNDEFINED) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC base;
    memcpy(&base, desc, sizeof(base));
    ID3D11Texture2D *texture = NULL;
    HRESULT hr = CreateTexture(&base, initial, out ? &texture : NULL);
    if (SUCCEEDED(hr) && out) *out = static_cast<NativeTexture2D *>(texture);
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateTexture3D1(const D3D11_TEXTURE3D_DESC1 *desc,
        const D3D11_SUBRESOURCE_DATA *initial, ID3D11Texture3D1 **out)
{
    if (out) *out = NULL;
    if (!desc || desc->TextureLayout != D3D11_TEXTURE_LAYOUT_UNDEFINED) return E_INVALIDARG;
    D3D11_TEXTURE3D_DESC base;
    memcpy(&base, desc, sizeof(base));
    ID3D11Texture3D *texture = NULL;
    HRESULT hr = CreateTexture3D(&base, initial, out ? &texture : NULL);
    if (SUCCEEDED(hr) && out) *out = static_cast<NativeTexture3D *>(texture);
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateRenderTargetView1(ID3D11Resource *resource,
        const D3D11_RENDER_TARGET_VIEW_DESC1 *input, ID3D11RenderTargetView1 **out)
{
    if (out) *out = NULL;
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    UINT plane = 0;
    if (input)
    {
        desc.Format = input->Format;
        desc.ViewDimension = input->ViewDimension;
        switch (input->ViewDimension)
        {
            case D3D11_RTV_DIMENSION_TEXTURE2D:
                desc.Texture2D.MipSlice = input->Texture2D.MipSlice;
                plane = input->Texture2D.PlaneSlice;
                break;
            case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
                desc.Texture2DArray.MipSlice = input->Texture2DArray.MipSlice;
                desc.Texture2DArray.FirstArraySlice = input->Texture2DArray.FirstArraySlice;
                desc.Texture2DArray.ArraySize = input->Texture2DArray.ArraySize;
                plane = input->Texture2DArray.PlaneSlice;
                break;
            default:
                memcpy(&desc, input, sizeof(desc));
                break;
        }
    }
    if (plane && (!wddm20 || ddi11_1)) return E_INVALIDARG;
    NativeLock guard(this);
    ID3D11RenderTargetView *view = NULL;
    plane_slice = plane;
    HRESULT hr = CreateRenderTargetView(resource, input ? &desc : NULL, out ? &view : NULL);
    plane_slice = 0;
    if (FAILED(hr) || !out) return hr;
    NativeRenderTargetView *object = static_cast<NativeRenderTargetView *>(view);
    object->plane_slice = plane;
    *out = object;
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateShaderResourceView1(ID3D11Resource *resource,
        const D3D11_SHADER_RESOURCE_VIEW_DESC1 *input, ID3D11ShaderResourceView1 **out)
{
    if (out) *out = NULL;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
    UINT plane = 0;
    if (input)
    {
        desc.Format = input->Format;
        desc.ViewDimension = input->ViewDimension;
        switch (input->ViewDimension)
        {
            case D3D11_SRV_DIMENSION_TEXTURE2D:
                desc.Texture2D.MostDetailedMip = input->Texture2D.MostDetailedMip;
                desc.Texture2D.MipLevels = input->Texture2D.MipLevels;
                plane = input->Texture2D.PlaneSlice;
                break;
            case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
                desc.Texture2DArray.MostDetailedMip = input->Texture2DArray.MostDetailedMip;
                desc.Texture2DArray.MipLevels = input->Texture2DArray.MipLevels;
                desc.Texture2DArray.FirstArraySlice = input->Texture2DArray.FirstArraySlice;
                desc.Texture2DArray.ArraySize = input->Texture2DArray.ArraySize;
                plane = input->Texture2DArray.PlaneSlice;
                break;
            default:
                memcpy(&desc, input, sizeof(desc));
                break;
        }
    }
    if (plane && (!wddm20 || ddi11_1)) return E_INVALIDARG;
    NativeLock guard(this);
    ID3D11ShaderResourceView *view = NULL;
    plane_slice = plane;
    HRESULT hr = CreateShaderResourceView(resource, input ? &desc : NULL, out ? &view : NULL);
    plane_slice = 0;
    if (FAILED(hr) || !out) return hr;
    NativeShaderResourceView *object = static_cast<NativeShaderResourceView *>(view);
    object->plane_slice = plane;
    *out = object;
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateUnorderedAccessView1(ID3D11Resource *resource,
        const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *input, ID3D11UnorderedAccessView1 **out)
{
    if (out) *out = NULL;
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
    UINT plane = 0;
    if (input)
    {
        desc.Format = input->Format;
        desc.ViewDimension = input->ViewDimension;
        switch (input->ViewDimension)
        {
            case D3D11_UAV_DIMENSION_TEXTURE2D:
                desc.Texture2D.MipSlice = input->Texture2D.MipSlice;
                plane = input->Texture2D.PlaneSlice;
                break;
            case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
                desc.Texture2DArray.MipSlice = input->Texture2DArray.MipSlice;
                desc.Texture2DArray.FirstArraySlice = input->Texture2DArray.FirstArraySlice;
                desc.Texture2DArray.ArraySize = input->Texture2DArray.ArraySize;
                plane = input->Texture2DArray.PlaneSlice;
                break;
            default:
                memcpy(&desc, input, sizeof(desc));
                break;
        }
    }
    if (plane && (!wddm20 || ddi11_1)) return E_INVALIDARG;
    NativeLock guard(this);
    ID3D11UnorderedAccessView *view = NULL;
    plane_slice = plane;
    HRESULT hr = CreateUnorderedAccessView(resource, input ? &desc : NULL, out ? &view : NULL);
    plane_slice = 0;
    if (FAILED(hr) || !out) return hr;
    NativeUnorderedAccessView *object = static_cast<NativeUnorderedAccessView *>(view);
    object->plane_slice = plane;
    *out = object;
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::CreateQuery1(const D3D11_QUERY_DESC1 *input, ID3D11Query1 **out)
{
    if (out) *out = NULL;
    if (!input) return E_INVALIDARG;
    UINT driver_context;
    switch (input->ContextType)
    {
        case D3D11_CONTEXT_TYPE_ALL: driver_context = 0; break;
        case D3D11_CONTEXT_TYPE_3D: driver_context = 1; break;
        case D3D11_CONTEXT_TYPE_COMPUTE: driver_context = 2; break;
        case D3D11_CONTEXT_TYPE_COPY: driver_context = 4; break;
        case D3D11_CONTEXT_TYPE_VIDEO: driver_context = 8; break;
        default: return E_INVALIDARG;
    }
    D3D11_QUERY_DESC desc = {input->Query, input->MiscFlags};
    NativeLock guard(this);
    ID3D11Query *query = NULL;
    UINT previous = query_context;
    query_context = driver_context;
    HRESULT hr = CreateQuery(&desc, out ? &query : NULL);
    query_context = previous;
    if (FAILED(hr) || !out) return hr;
    NativeQuery *object = static_cast<NativeQuery *>(query);
    object->context_type = input->ContextType;
    *out = object;
    return hr;
}

void STDMETHODCALLTYPE NativeDevice::WriteToSubresource(ID3D11Resource *, UINT, const D3D11_BOX *,
        const void *, UINT, UINT)
{
    WARN("Default textures cannot be mapped on this device.\n");
}

void STDMETHODCALLTYPE NativeDevice::ReadFromSubresource(void *, UINT, UINT, ID3D11Resource *, UINT,
        const D3D11_BOX *)
{
    WARN("Default textures cannot be mapped on this device.\n");
}

struct NativeTilingSource
{
    D3D10DDI_HRESOURCE handle;
    D3D10DDIRESOURCE_TYPE dimension;
    DXGI_FORMAT format;
    UINT64 width;
    UINT height, depth, mips, layers, samples, misc;
};

static bool NativeTilingResource(NativeDevice *device, ID3D11Resource *resource, NativeTilingSource *source)
{
    if (NativeBuffer *buffer = GetNativeBuffer(resource, device))
    {
        source->handle = buffer->handle;
        source->dimension = D3D10DDIRESOURCE_BUFFER;
        source->format = DXGI_FORMAT_UNKNOWN;
        source->width = buffer->desc.ByteWidth;
        source->height = source->depth = source->mips = source->layers = source->samples = 1;
        source->misc = buffer->desc.MiscFlags;
        return true;
    }
    NativeTextureInfo info;
    if (!GetNativeTexture(resource, device, &info)) return false;
    source->handle = info.handle;
    source->dimension = info.dimension;
    source->format = info.desc.Format;
    source->width = info.desc.Width;
    source->height = info.desc.Height;
    source->depth = info.depth;
    source->mips = info.desc.MipLevels;
    source->layers = info.dimension == D3D10DDIRESOURCE_TEXTURE3D ? 1 : info.desc.ArraySize;
    source->samples = info.desc.SampleDesc.Count;
    source->misc = info.desc.MiscFlags;
    return true;
}

static void NativeTileShape(const NativeTilingSource &source, D3D11_TILE_SHAPE *shape)
{
    shape->WidthInTexels = D3D11_2_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    shape->HeightInTexels = 1;
    shape->DepthInTexels = 1;
    if (source.dimension == D3D10DDIRESOURCE_BUFFER) return;
    UINT row_bytes = 0, rows = 0;
    if (!NativeUploadFormat(source.format, 4, 4, &row_bytes, &rows)) return;
    bool compressed = rows == 1;
    UINT block_bits = compressed ? row_bytes * 8 : row_bytes * 2;
    if (source.dimension == D3D10DDIRESOURCE_TEXTURE3D)
    {
        if (compressed)
        {
            shape->WidthInTexels = block_bits == 64 ? 128 : 64;
            shape->HeightInTexels = 64;
            shape->DepthInTexels = 16;
            return;
        }
        switch (block_bits)
        {
            case 8: shape->WidthInTexels = 64; shape->HeightInTexels = 32; shape->DepthInTexels = 32; break;
            case 16: shape->WidthInTexels = 32; shape->HeightInTexels = 32; shape->DepthInTexels = 32; break;
            case 32: shape->WidthInTexels = 32; shape->HeightInTexels = 32; shape->DepthInTexels = 16; break;
            case 64: shape->WidthInTexels = 32; shape->HeightInTexels = 16; shape->DepthInTexels = 16; break;
            default: shape->WidthInTexels = 16; shape->HeightInTexels = 16; shape->DepthInTexels = 16; break;
        }
        return;
    }
    UINT width, height;
    if (compressed)
    {
        width = block_bits == 64 ? 512 : 256;
        height = 256;
    }
    else
    {
        switch (block_bits)
        {
            case 8: width = 256; height = 256; break;
            case 16: width = 256; height = 128; break;
            case 32: width = 128; height = 128; break;
            case 64: width = 128; height = 64; break;
            default: width = 64; height = 64; break;
        }
    }
    switch (source.samples)
    {
        case 2: width /= 2; break;
        case 4: width /= 2; height /= 2; break;
        case 8: width /= 4; height /= 2; break;
        case 16: width /= 4; height /= 4; break;
        default: break;
    }
    shape->WidthInTexels = width;
    shape->HeightInTexels = height;
}

void STDMETHODCALLTYPE NativeDevice::GetResourceTiling(ID3D11Resource *resource, UINT *tile_count,
        D3D11_PACKED_MIP_DESC *packed, D3D11_TILE_SHAPE *shape, UINT *tiling_count, UINT first_tiling,
        D3D11_SUBRESOURCE_TILING *tilings)
{
    UINT requested = tiling_count ? *tiling_count : 0;
    if (tile_count) *tile_count = 0;
    if (tiling_count) *tiling_count = 0;
    NativeTilingSource source;
    if (!NativeTilingResource(this, resource, &source) || !(source.misc & D3D11_RESOURCE_MISC_TILED)) return;
    D3D11_TILE_SHAPE standard = {};
    NativeTileShape(source, &standard);
    if (shape) *shape = standard;

    UINT packed_mips = 0, packed_tiles = 0;
    if (source.dimension != D3D10DDIRESOURCE_BUFFER && wddm20 && wddm20_functions.pfnGetMipPacking)
    {
        NativeLock guard(this);
        wddm20_functions.pfnGetMipPacking(driver_device, source.handle, &packed_mips, &packed_tiles);
    }
    if (packed_mips > source.mips) packed_mips = source.mips;
    UINT standard_mips = source.mips - packed_mips;
    UINT written = 0, tile = 0;
    for (UINT layer = 0; layer < source.layers; ++layer)
    {
        for (UINT mip = 0; mip < source.mips; ++mip)
        {
            UINT subresource = layer * source.mips + mip;
            D3D11_SUBRESOURCE_TILING tiling = {};
            if (source.dimension == D3D10DDIRESOURCE_BUFFER)
            {
                tiling.WidthInTiles = static_cast<UINT>((source.width + standard.WidthInTexels - 1)
                        / standard.WidthInTexels);
                tiling.HeightInTiles = 1;
                tiling.DepthInTiles = 1;
                tiling.StartTileIndexInOverallResource = tile;
                tile += tiling.WidthInTiles;
            }
            else if (mip < standard_mips)
            {
                UINT width = max(1u, static_cast<UINT>(source.width) >> mip);
                UINT height = max(1u, source.height >> mip);
                UINT depth = max(1u, source.depth >> mip);
                tiling.WidthInTiles = (width + standard.WidthInTexels - 1) / standard.WidthInTexels;
                tiling.HeightInTiles = static_cast<UINT16>((height + standard.HeightInTexels - 1)
                        / standard.HeightInTexels);
                tiling.DepthInTiles = static_cast<UINT16>((depth + standard.DepthInTexels - 1)
                        / standard.DepthInTexels);
                tiling.StartTileIndexInOverallResource = tile;
                tile += tiling.WidthInTiles * tiling.HeightInTiles * tiling.DepthInTiles;
            }
            else tiling.StartTileIndexInOverallResource = D3D11_PACKED_TILE;
            if (tilings && subresource >= first_tiling && written < requested) tilings[written++] = tiling;
        }
        if (packed_mips)
        {
            if (!layer && packed)
            {
                packed->NumStandardMips = static_cast<UINT8>(standard_mips);
                packed->NumPackedMips = static_cast<UINT8>(packed_mips);
                packed->NumTilesForPackedMips = packed_tiles;
                packed->StartTileIndexInOverallResource = tile;
            }
            tile += packed_tiles;
        }
    }
    if (!packed_mips && packed)
    {
        packed->NumStandardMips = static_cast<UINT8>(source.mips);
        packed->NumPackedMips = 0;
        packed->NumTilesForPackedMips = 0;
        packed->StartTileIndexInOverallResource = 0;
    }
    if (tile_count) *tile_count = tile;
    if (tiling_count) *tiling_count = written;
}

static bool NativeTiledHandle(NativeDevice *device, ID3D11Resource *resource, UINT required,
        D3D10DDI_HRESOURCE *handle)
{
    NativeTilingSource source;
    if (!NativeTilingResource(device, resource, &source) || (source.misc & required) != required) return false;
    *handle = source.handle;
    return true;
}

static ID3D11Resource **NativeTilePoolSlot(ID3D11Resource *resource)
{
    D3D11_RESOURCE_DIMENSION dimension;
    resource->GetType(&dimension);
    if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
        return &static_cast<NativeBuffer *>(static_cast<ID3D11Buffer *>(resource))->tile_pool;
    if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
        return &static_cast<NativeTexture3D *>(static_cast<ID3D11Texture3D *>(resource))->tile_pool;
    return &static_cast<NativeTexture2D *>(static_cast<ID3D11Texture2D *>(resource))->tile_pool;
}

HRESULT STDMETHODCALLTYPE NativeContext::UpdateTileMappings(ID3D11Resource *resource, UINT region_count,
        const D3D11_TILED_RESOURCE_COORDINATE *coordinates, const D3D11_TILE_REGION_SIZE *sizes,
        ID3D11Buffer *pool, UINT range_count, const UINT *range_flags, const UINT *pool_offsets,
        const UINT *range_counts, UINT flags)
{
    if (!resource || !region_count || !range_count || (flags & ~D3D11_TILE_MAPPING_NO_OVERWRITE))
        return E_INVALIDARG;
    if (region_count > 1 && !coordinates) return E_INVALIDARG;
    D3D10DDI_HRESOURCE target = {}, pool_handle = {};
    if (!NativeTiledHandle(device, resource, D3D11_RESOURCE_MISC_TILED, &target)) return E_INVALIDARG;
    if (pool && !NativeTiledHandle(device, pool, D3D11_RESOURCE_MISC_TILE_POOL, &pool_handle)) return E_INVALIDARG;
    if (!device->wddm20 || !device->wddm20_functions.pfnUpdateTileMappings) return E_NOTIMPL;
    NativeLock guard(device);
    if (deferred)
    {
        NativeCommandRef<ID3D11Resource> retained(resource);
        NativeCommandRef<ID3D11Buffer> retained_pool(pool);
        NativeCommandArray<D3D11_TILED_RESOURCE_COORDINATE> coordinate_copy(coordinates ? region_count : 0, coordinates);
        NativeCommandArray<D3D11_TILE_REGION_SIZE> size_copy(sizes ? region_count : 0, sizes);
        NativeCommandArray<UINT> flag_copy(range_flags ? range_count : 0, range_flags);
        NativeCommandArray<UINT> offset_copy(pool_offsets ? range_count : 0, pool_offsets);
        NativeCommandArray<UINT> count_copy(range_counts ? range_count : 0, range_counts);
        if (!Captured(coordinate_copy) || !Captured(size_copy) || !Captured(flag_copy) || !Captured(offset_copy)
                || !Captured(count_copy)) return E_OUTOFMEMORY;
        Record([=](NativeContext *context)
        {
            context->UpdateTileMappings(retained.Get(), region_count, coordinate_copy.Data(), size_copy.Data(),
                    retained_pool.Get(), range_count, flag_copy.Data(), offset_copy.Data(), count_copy.Data(), flags);
        });
        return S_OK;
    }
    D3D11_TILED_RESOURCE_COORDINATE origin = {};
    NativeCommandArray<D3D11_TILE_REGION_SIZE> default_sizes;
    NativeCommandArray<UINT> default_flags, default_counts;
    UINT region_tiles = 0;
    if (!sizes)
    {
        default_sizes = NativeCommandArray<D3D11_TILE_REGION_SIZE>(region_count, NULL);
        if (!default_sizes.Valid()) return E_OUTOFMEMORY;
        UINT whole = 0;
        if (!coordinates) device->GetResourceTiling(resource, &whole, NULL, NULL, NULL, 0, NULL);
        for (UINT i = 0; i < region_count; ++i) default_sizes.Data()[i].NumTiles = coordinates ? 1 : whole;
        sizes = default_sizes.Data();
    }
    if (!coordinates) coordinates = &origin;
    for (UINT i = 0; i < region_count; ++i) region_tiles += sizes[i].NumTiles;
    if (!range_flags)
    {
        default_flags = NativeCommandArray<UINT>(range_count, NULL);
        if (!default_flags.Valid()) return E_OUTOFMEMORY;
        range_flags = default_flags.Data();
    }
    if (!range_counts && range_count == 1) range_counts = &region_tiles;
    else if (!range_counts)
    {
        default_counts = NativeCommandArray<UINT>(range_count, NULL);
        if (!default_counts.Valid()) return E_OUTOFMEMORY;
        for (UINT i = 0; i < range_count; ++i) default_counts.Data()[i] = 1;
        range_counts = default_counts.Data();
    }
    ID3D11Resource **slot = NativeTilePoolSlot(resource);
    if (pool && *slot != pool)
    {
        NativeRetainResource(pool);
        if (*slot) NativeDropResource(*slot);
        *slot = pool;
    }
    else if (!pool && *slot)
        NativeTiledHandle(device, *slot, D3D11_RESOURCE_MISC_TILE_POOL, &pool_handle);
    device->BeginCall();
    device->wddm20_functions.pfnUpdateTileMappings(device->driver_device, target, region_count,
            reinterpret_cast<const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE *>(coordinates),
            reinterpret_cast<const D3DWDDM1_3DDI_TILE_REGION_SIZE *>(sizes), pool_handle, range_count,
            range_flags, pool_offsets, range_counts, flags);
    return device->operation_error;
}

HRESULT STDMETHODCALLTYPE NativeContext::CopyTileMappings(ID3D11Resource *dst,
        const D3D11_TILED_RESOURCE_COORDINATE *dst_coordinate, ID3D11Resource *src,
        const D3D11_TILED_RESOURCE_COORDINATE *src_coordinate, const D3D11_TILE_REGION_SIZE *size, UINT flags)
{
    if (!dst_coordinate || !src_coordinate || !size || (flags & ~D3D11_TILE_MAPPING_NO_OVERWRITE))
        return E_INVALIDARG;
    D3D10DDI_HRESOURCE target = {}, source = {};
    if (!NativeTiledHandle(device, dst, D3D11_RESOURCE_MISC_TILED, &target)
            || !NativeTiledHandle(device, src, D3D11_RESOURCE_MISC_TILED, &source)) return E_INVALIDARG;
    if (!device->wddm20 || !device->wddm20_functions.pfnCopyTileMappings) return E_NOTIMPL;
    NativeLock guard(device);
    if (deferred)
    {
        NativeCommandRef<ID3D11Resource> retained_dst(dst), retained_src(src);
        D3D11_TILED_RESOURCE_COORDINATE dst_copy = *dst_coordinate, src_copy = *src_coordinate;
        D3D11_TILE_REGION_SIZE size_copy = *size;
        Record([=](NativeContext *context)
        {
            context->CopyTileMappings(retained_dst.Get(), &dst_copy, retained_src.Get(), &src_copy, &size_copy, flags);
        });
        return S_OK;
    }
    device->BeginCall();
    device->wddm20_functions.pfnCopyTileMappings(device->driver_device, target,
            reinterpret_cast<const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE *>(dst_coordinate), source,
            reinterpret_cast<const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE *>(src_coordinate),
            reinterpret_cast<const D3DWDDM1_3DDI_TILE_REGION_SIZE *>(size), flags);
    return device->operation_error;
}

void STDMETHODCALLTYPE NativeContext::CopyTiles(ID3D11Resource *resource,
        const D3D11_TILED_RESOURCE_COORDINATE *coordinate, const D3D11_TILE_REGION_SIZE *size,
        ID3D11Buffer *buffer, UINT64 offset, UINT flags)
{
    if (!coordinate || !size) return;
    D3D10DDI_HRESOURCE target = {}, linear = {};
    if (!NativeTiledHandle(device, resource, D3D11_RESOURCE_MISC_TILED, &target)
            || !NativeTiledHandle(device, buffer, 0, &linear)) return;
    if (!device->wddm20 || !device->wddm20_functions.pfnCopyTiles) { Unimplemented("CopyTiles DDI"); return; }
    NativeLock guard(device);
    if (deferred)
    {
        NativeCommandRef<ID3D11Resource> retained(resource);
        NativeCommandRef<ID3D11Buffer> retained_buffer(buffer);
        D3D11_TILED_RESOURCE_COORDINATE coordinate_copy = *coordinate;
        D3D11_TILE_REGION_SIZE size_copy = *size;
        Record([=](NativeContext *context)
        {
            context->CopyTiles(retained.Get(), &coordinate_copy, &size_copy, retained_buffer.Get(), offset, flags);
        });
        return;
    }
    device->wddm20_functions.pfnCopyTiles(device->driver_device, target,
            reinterpret_cast<const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE *>(coordinate),
            reinterpret_cast<const D3DWDDM1_3DDI_TILE_REGION_SIZE *>(size), linear, offset, flags);
}

void STDMETHODCALLTYPE NativeContext::UpdateTiles(ID3D11Resource *dst,
        const D3D11_TILED_RESOURCE_COORDINATE *coordinate, const D3D11_TILE_REGION_SIZE *size,
        const void *data, UINT flags)
{
    if (!coordinate || !size || !data) return;
    D3D10DDI_HRESOURCE target = {};
    if (!NativeTiledHandle(device, dst, D3D11_RESOURCE_MISC_TILED, &target)) return;
    if (!device->wddm20 || !device->wddm20_functions.pfnUpdateTiles) { Unimplemented("UpdateTiles DDI"); return; }
    NativeLock guard(device);
    if (deferred)
    {
        NativeCommandRef<ID3D11Resource> retained(dst);
        D3D11_TILED_RESOURCE_COORDINATE coordinate_copy = *coordinate;
        D3D11_TILE_REGION_SIZE size_copy = *size;
        NativeCommandArray<BYTE> bytes(static_cast<SIZE_T>(size->NumTiles) * D3D11_2_TILED_RESOURCE_TILE_SIZE_IN_BYTES,
                static_cast<const BYTE *>(data));
        if (!Captured(bytes)) return;
        Record([=](NativeContext *context)
        {
            context->UpdateTiles(retained.Get(), &coordinate_copy, &size_copy, bytes.Data(), flags);
        });
        return;
    }
    device->wddm20_functions.pfnUpdateTiles(device->driver_device, target,
            reinterpret_cast<const D3DWDDM1_3DDI_TILED_RESOURCE_COORDINATE *>(coordinate),
            reinterpret_cast<const D3DWDDM1_3DDI_TILE_REGION_SIZE *>(size), data, flags);
}

HRESULT STDMETHODCALLTYPE NativeContext::ResizeTilePool(ID3D11Buffer *pool, UINT64 size)
{
    D3D10DDI_HRESOURCE handle = {};
    if (!size || size % D3D11_2_TILED_RESOURCE_TILE_SIZE_IN_BYTES || size > ~0u
            || !NativeTiledHandle(device, pool, D3D11_RESOURCE_MISC_TILE_POOL, &handle)) return E_INVALIDARG;
    if (deferred) return DXGI_ERROR_INVALID_CALL;
    if (!device->wddm20 || !device->wddm20_functions.pfnResizeTilePool) return E_NOTIMPL;
    NativeLock guard(device);
    device->BeginCall();
    device->wddm20_functions.pfnResizeTilePool(device->driver_device, handle, size);
    if (SUCCEEDED(device->operation_error))
        static_cast<NativeBuffer *>(pool)->desc.ByteWidth = static_cast<UINT>(size);
    return device->operation_error;
}

static bool NativeBarrierHandle(NativeDevice *device, ID3D11DeviceChild *object, D3D11DDI_HANDLETYPE *type,
        void **handle)
{
    *type = D3D10DDI_HT_RESOURCE;
    *handle = NULL;
    if (!object) return true;
    ID3D11Device *owner = NULL;
    object->GetDevice(&owner);
    bool same = owner == static_cast<ID3D11Device *>(device);
    if (owner) owner->Release();
    if (!same) return false;
    ID3D11Resource *resource = NULL;
    if (SUCCEEDED(object->QueryInterface(IID_ID3D11Resource, reinterpret_cast<void **>(&resource))))
    {
        D3D10DDI_HRESOURCE driver = {};
        bool found = NativeTiledHandle(device, resource, 0, &driver);
        resource->Release();
        *handle = driver.pDrvPrivate;
        return found;
    }
    ID3D11ShaderResourceView *shader_view = NULL;
    if (SUCCEEDED(object->QueryInterface(IID_ID3D11ShaderResourceView, reinterpret_cast<void **>(&shader_view))))
    {
        *type = D3D10DDI_HT_SHADERRESOURCEVIEW;
        *handle = static_cast<NativeShaderResourceView *>(shader_view)->handle.pDrvPrivate;
        shader_view->Release();
        return true;
    }
    ID3D11RenderTargetView *target_view = NULL;
    if (SUCCEEDED(object->QueryInterface(IID_ID3D11RenderTargetView, reinterpret_cast<void **>(&target_view))))
    {
        *type = D3D10DDI_HT_RENDERTARGETVIEW;
        *handle = static_cast<NativeRenderTargetView *>(target_view)->handle.pDrvPrivate;
        target_view->Release();
        return true;
    }
    ID3D11DepthStencilView *depth_view = NULL;
    if (SUCCEEDED(object->QueryInterface(IID_ID3D11DepthStencilView, reinterpret_cast<void **>(&depth_view))))
    {
        *type = D3D10DDI_HT_DEPTHSTENCILVIEW;
        *handle = static_cast<NativeDepthView *>(depth_view)->handle.pDrvPrivate;
        depth_view->Release();
        return true;
    }
    ID3D11UnorderedAccessView *access_view = NULL;
    if (SUCCEEDED(object->QueryInterface(IID_ID3D11UnorderedAccessView, reinterpret_cast<void **>(&access_view))))
    {
        *type = D3D11DDI_HT_UNORDEREDACCESSVIEW;
        *handle = static_cast<NativeUnorderedAccessView *>(access_view)->handle.pDrvPrivate;
        access_view->Release();
        return true;
    }
    return false;
}

void STDMETHODCALLTYPE NativeContext::TiledResourceBarrier(ID3D11DeviceChild *before, ID3D11DeviceChild *after)
{
    D3D11DDI_HANDLETYPE before_type, after_type;
    void *before_handle, *after_handle;
    if (!NativeBarrierHandle(device, before, &before_type, &before_handle)
            || !NativeBarrierHandle(device, after, &after_type, &after_handle)) return;
    if (!device->wddm20 || !device->wddm20_functions.pfnTiledResourceBarrier) return;
    NativeLock guard(device);
    if (deferred)
    {
        NativeCommandRef<ID3D11DeviceChild> retained_before(before), retained_after(after);
        Record([=](NativeContext *context)
        {
            context->TiledResourceBarrier(retained_before.Get(), retained_after.Get());
        });
        return;
    }
    device->wddm20_functions.pfnTiledResourceBarrier(device->driver_device, before_type, before_handle,
            after_type, after_handle);
}

void STDMETHODCALLTYPE NativeContext::Flush1(D3D11_CONTEXT_TYPE, HANDLE event)
{
    if (deferred) return;
    Flush();
    if (event) device->EnqueueSetEvent(event);
}

void STDMETHODCALLTYPE NativeContext::SetHardwareProtectionState(BOOL enable)
{
    if (deferred) return;
    NativeLock guard(device);
    hardware_protection = !!enable;
    if (device->wddm20 && device->wddm20_functions.pfnSetHardwareProtectionState)
        device->wddm20_functions.pfnSetHardwareProtectionState(device->driver_device, hardware_protection);
}

void STDMETHODCALLTYPE NativeContext::GetHardwareProtectionState(BOOL *enable)
{
    if (enable) *enable = hardware_protection;
}

class NativeFence final : public NativeChild<ID3D11Fence, &IID_ID3D11Fence>
{
public:
    D3DKMT_HANDLE sync = 0;
    const volatile UINT64 *value = NULL;
    explicit NativeFence(NativeDevice *d) : NativeChild(d) {}
    ~NativeFence()
    {
        if (!sync || !device->kernel_callbacks.pfnDestroySynchronizationObjectCb) return;
        D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT destroy = {};
        destroy.hSyncObject = sync;
        device->kernel_callbacks.pfnDestroySynchronizationObjectCb(device->runtime_device, &destroy);
    }
    HRESULT STDMETHODCALLTYPE CreateSharedHandle(const SECURITY_ATTRIBUTES *, DWORD, const WCHAR *,
            HANDLE *handle) override
    {
        if (handle) *handle = NULL;
        return E_NOTIMPL;
    }
    UINT64 STDMETHODCALLTYPE GetCompletedValue() override { return *value; }
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 fence_value, HANDLE event) override
    {
        if (!device->kernel_callbacks.pfnWaitForSynchronizationObjectFromCpuCb) return E_NOTIMPL;
        D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait = {};
        wait.ObjectCount = 1;
        wait.ObjectHandleArray = &sync;
        wait.FenceValueArray = &fence_value;
        wait.hAsyncEvent = event;
        return device->kernel_callbacks.pfnWaitForSynchronizationObjectFromCpuCb(device->runtime_device, &wait);
    }
};

HRESULT STDMETHODCALLTYPE NativeDevice::CreateFence(UINT64 initial_value, D3D11_FENCE_FLAG flags, REFIID iid,
        void **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (flags & (D3D11_FENCE_FLAG_SHARED | D3D11_FENCE_FLAG_SHARED_CROSS_ADAPTER | D3D11_FENCE_FLAG_NON_MONITORED))
        return E_NOTIMPL;
    if (!kernel_callbacks.pfnCreateSynchronizationObject2Cb || !signal_fence || !wait_fence) return E_NOTIMPL;
    NativeLock guard(this);
    NativeFence *fence = new NativeFence(this);
    if (!fence) return E_OUTOFMEMORY;
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 create = {};
    create.Info.Type = D3DDDI_MONITORED_FENCE;
    create.Info.MonitoredFence.InitialFenceValue = initial_value;
    create.Info.MonitoredFence.EngineAffinity = 1;
    HRESULT hr = kernel_callbacks.pfnCreateSynchronizationObject2Cb(runtime_device, &create);
    if (SUCCEEDED(hr))
    {
        fence->sync = create.hSyncObject;
        fence->value = static_cast<const volatile UINT64 *>(create.Info.MonitoredFence.FenceValueCPUVirtualAddress);
        hr = fence->value ? fence->QueryInterface(iid, out) : E_FAIL;
    }
    fence->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE NativeDevice::OpenSharedFence(HANDLE, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

static NativeFence *NativeOwnedFence(NativeDevice *device, ID3D11Fence *fence)
{
    if (!fence) return NULL;
    ID3D11Device *owner = NULL;
    fence->GetDevice(&owner);
    bool same = owner == static_cast<ID3D11Device *>(device);
    if (owner) owner->Release();
    return same ? static_cast<NativeFence *>(fence) : NULL;
}

HRESULT STDMETHODCALLTYPE NativeContext::Signal(ID3D11Fence *fence, UINT64 value)
{
    NativeFence *object = NativeOwnedFence(device, fence);
    if (!object) return E_INVALIDARG;
    if (deferred) return DXGI_ERROR_INVALID_CALL;
    NativeLock guard(device);
    Flush();
    return device->signal_fence(device->runtime_device, object->sync, value);
}

HRESULT STDMETHODCALLTYPE NativeContext::Wait(ID3D11Fence *fence, UINT64 value)
{
    NativeFence *object = NativeOwnedFence(device, fence);
    if (!object) return E_INVALIDARG;
    if (deferred) return DXGI_ERROR_INVALID_CALL;
    NativeLock guard(device);
    Flush();
    return device->wait_fence(device->runtime_device, object->sync, value);
}
