/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 resources, descriptor heaps and views
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "native12.h"
#include "native12_device.h"
#include "native12_objects.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d12);

bool Native12GetFormatInfo(DXGI_FORMAT format, Native12FormatInfo *info)
{
    UINT value = format;

    info->block_width = 1;
    info->block_height = 1;
    info->planes = 1;
    if (value >= DXGI_FORMAT_R32G32B32A32_TYPELESS && value <= DXGI_FORMAT_R32G32B32A32_SINT) info->block_bytes = 16;
    else if (value >= DXGI_FORMAT_R32G32B32_TYPELESS && value <= DXGI_FORMAT_R32G32B32_SINT) info->block_bytes = 12;
    else if (value >= DXGI_FORMAT_R16G16B16A16_TYPELESS && value <= DXGI_FORMAT_R32G32_SINT) info->block_bytes = 8;
    else if (value >= DXGI_FORMAT_R32G8X24_TYPELESS && value <= DXGI_FORMAT_X32_TYPELESS_G8X24_UINT)
    {
        info->block_bytes = 8;
        info->planes = 2;
    }
    else if (value >= DXGI_FORMAT_R10G10B10A2_TYPELESS && value <= DXGI_FORMAT_R32_SINT) info->block_bytes = 4;
    else if (value >= DXGI_FORMAT_R24G8_TYPELESS && value <= DXGI_FORMAT_X24_TYPELESS_G8_UINT)
    {
        info->block_bytes = 4;
        info->planes = 2;
    }
    else if (value >= DXGI_FORMAT_R8G8_TYPELESS && value <= DXGI_FORMAT_R16_SINT) info->block_bytes = 2;
    else if (value >= DXGI_FORMAT_R8_TYPELESS && value <= DXGI_FORMAT_A8_UNORM) info->block_bytes = 1;
    else if (value == DXGI_FORMAT_R9G9B9E5_SHAREDEXP) info->block_bytes = 4;
    else if (value == DXGI_FORMAT_R8G8_B8G8_UNORM || value == DXGI_FORMAT_G8R8_G8B8_UNORM)
    {
        info->block_width = 2;
        info->block_bytes = 4;
    }
    else if ((value >= DXGI_FORMAT_BC1_TYPELESS && value <= DXGI_FORMAT_BC1_UNORM_SRGB)
            || (value >= DXGI_FORMAT_BC4_TYPELESS && value <= DXGI_FORMAT_BC4_SNORM))
    {
        info->block_width = 4;
        info->block_height = 4;
        info->block_bytes = 8;
    }
    else if ((value >= DXGI_FORMAT_BC2_TYPELESS && value <= DXGI_FORMAT_BC3_UNORM_SRGB)
            || (value >= DXGI_FORMAT_BC5_TYPELESS && value <= DXGI_FORMAT_BC5_SNORM)
            || (value >= DXGI_FORMAT_BC6H_TYPELESS && value <= DXGI_FORMAT_BC7_UNORM_SRGB))
    {
        info->block_width = 4;
        info->block_height = 4;
        info->block_bytes = 16;
    }
    else if (value == DXGI_FORMAT_B5G6R5_UNORM || value == DXGI_FORMAT_B5G5R5A1_UNORM
            || value == DXGI_FORMAT_B4G4R4A4_UNORM) info->block_bytes = 2;
    else if (value >= DXGI_FORMAT_B8G8R8A8_UNORM && value <= DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) info->block_bytes = 4;
    else
    {
        info->block_bytes = 0;
        return false;
    }
    return true;
}

static bool Native12IntegerFormat(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT:
        case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R32G32B32_SINT:
        case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: case DXGI_FORMAT_R10G10B10A2_UINT:
        case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_R16G16_UINT: case DXGI_FORMAT_R16G16_SINT:
        case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT: case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        case DXGI_FORMAT_R8G8_UINT: case DXGI_FORMAT_R8G8_SINT:
        case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_R16_SINT:
        case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SINT:
            return true;
        default:
            return false;
    }
}

static bool Native12DepthFormat(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT || format == DXGI_FORMAT_D32_FLOAT
            || format == DXGI_FORMAT_D24_UNORM_S8_UINT || format == DXGI_FORMAT_D16_UNORM;
}

static bool Native12DisplayFormat(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_R8G8B8A8_UNORM || format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            || format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
            || format == DXGI_FORMAT_R10G10B10A2_UNORM || format == DXGI_FORMAT_R16G16B16A16_FLOAT;
}

void Native12FormatSupport(DXGI_FORMAT format, UINT ddi_support, D3D12_FORMAT_SUPPORT1 *support1,
        D3D12_FORMAT_SUPPORT2 *support2)
{
    UINT first = 0, second = 0;
    Native12FormatInfo info = {};
    bool known = Native12GetFormatInfo(format, &info);
    bool integer = Native12IntegerFormat(format);
    bool depth = Native12DepthFormat(format);
    bool block_compressed = info.block_width == 4 && info.block_height == 4;
    bool unsupported = !!(ddi_support & D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED);

    if (!unsupported)
    {
        if ((ddi_support & D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE) && !integer)
            first |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
        if (ddi_support & D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET) first |= D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
        if ((ddi_support & D3D10_DDI_FORMAT_SUPPORT_BLENDABLE) && !integer) first |= D3D12_FORMAT_SUPPORT1_BLENDABLE;
        if (ddi_support & D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET)
            first |= D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE;
        if (ddi_support & D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD) first |= D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD;
        if (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_SHADER_GATHER) first |= D3D12_FORMAT_SUPPORT1_SHADER_GATHER;
        if (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_VERTEX_BUFFER) first |= D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER;
        if (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_BUFFER)
            first |= D3D12_FORMAT_SUPPORT1_BUFFER | D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
        if (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_DECODER_OUTPUT) first |= D3D12_FORMAT_SUPPORT1_DECODER_OUTPUT;
        if (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT)
            first |= D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_OUTPUT;
        if (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT)
            first |= D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_INPUT;
        if (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_VIDEO_ENCODER) first |= D3D12_FORMAT_SUPPORT1_VIDEO_ENCODER;
        if (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_UAV_WRITES)
        {
            first |= D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
            second |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
        }
        if (ddi_support & D3DWDDM2_0DDI_FORMAT_SUPPORT_UAV_READS) second |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
        if (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_OUTPUT_MERGER_LOGIC_OP)
            second |= D3D12_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP;
        if (ddi_support & D3D11_1DDI_FORMAT_SUPPORT_MULTIPLANE_OVERLAY)
            second |= D3D12_FORMAT_SUPPORT2_MULTIPLANE_OVERLAY;

        if (ddi_support & (D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE | D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET
                | D3D10_DDI_FORMAT_SUPPORT_BLENDABLE))
        {
            first |= D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D
                    | D3D12_FORMAT_SUPPORT1_TEXTURECUBE | D3D12_FORMAT_SUPPORT1_MIP
                    | D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
            if (block_compressed) first &= ~D3D12_FORMAT_SUPPORT1_TEXTURE1D;
            else if (!depth) first |= D3D12_FORMAT_SUPPORT1_TEXTURE3D;
        }
        if (depth)
            first |= D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL | D3D12_FORMAT_SUPPORT1_TEXTURE2D
                    | D3D12_FORMAT_SUPPORT1_MIP;
        if ((format == DXGI_FORMAT_R32_UINT || format == DXGI_FORMAT_R32_SINT)
                && (second & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
            second |= D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS
                    | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE
                    | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX
                    | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX;
        if (Native12DisplayFormat(format) && (first & D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
            first |= D3D12_FORMAT_SUPPORT1_DISPLAY;
    }
    if (format == DXGI_FORMAT_R16_UINT || format == DXGI_FORMAT_R32_UINT)
        first |= D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER;
    if (known && info.block_bytes && !block_compressed && !depth && info.planes == 1)
        first |= D3D12_FORMAT_SUPPORT1_BUFFER | D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER;
    *support1 = static_cast<D3D12_FORMAT_SUPPORT1>(first);
    *support2 = static_cast<D3D12_FORMAT_SUPPORT2>(second);
}

static D3D12DDI_RESOURCE_FLAGS_0003 Native12ResourceFlags(const D3D12_RESOURCE_DESC *desc)
{
    UINT flags = D3D12DDI_RESOURCE_FLAG_0003_NONE;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) flags |= D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) flags |= D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        flags |= D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS;
    if (!(desc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
        flags |= D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER) flags |= D3D12DDI_RESOURCE_FLAG_0003_CROSS_ADAPTER;
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)
        flags |= D3D12DDI_RESOURCE_FLAG_0003_SIMULTANEOUS_ACCESS;
    return static_cast<D3D12DDI_RESOURCE_FLAGS_0003>(flags);
}

static D3D12DDI_BARRIER_LAYOUT Native12InitialLayout(const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES state)
{
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) return D3D12DDI_BARRIER_LAYOUT_UNDEFINED;
    switch (state)
    {
        case D3D12_RESOURCE_STATE_COMMON: return D3D12DDI_BARRIER_LAYOUT_COMMON;
        case D3D12_RESOURCE_STATE_RENDER_TARGET: return D3D12DDI_BARRIER_LAYOUT_RENDER_TARGET;
        case D3D12_RESOURCE_STATE_UNORDERED_ACCESS: return D3D12DDI_BARRIER_LAYOUT_UNORDERED_ACCESS;
        case D3D12_RESOURCE_STATE_DEPTH_WRITE: return D3D12DDI_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
        case D3D12_RESOURCE_STATE_DEPTH_READ: return D3D12DDI_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
        case D3D12_RESOURCE_STATE_COPY_DEST: return D3D12DDI_BARRIER_LAYOUT_LEGACY_COPY_DEST;
        case D3D12_RESOURCE_STATE_COPY_SOURCE: return D3D12DDI_BARRIER_LAYOUT_LEGACY_COPY_SOURCE;
        case D3D12_RESOURCE_STATE_RESOLVE_DEST: return D3D12DDI_BARRIER_LAYOUT_RESOLVE_DEST;
        case D3D12_RESOURCE_STATE_RESOLVE_SOURCE: return D3D12DDI_BARRIER_LAYOUT_RESOLVE_SOURCE;
        case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE: return D3D12DDI_BARRIER_LAYOUT_LEGACY_PIXEL_SHADER_RESOURCE;
        default:
            if (state & (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return (state & D3D12_RESOURCE_STATE_COPY_SOURCE)
                        ? D3D12DDI_BARRIER_LAYOUT_LEGACY_DIRECT_QUEUE_GENERIC_READ_COMPUTE_QUEUE_ACCESSIBLE
                        : D3D12DDI_BARRIER_LAYOUT_LEGACY_SHADER_RESOURCE;
            return D3D12DDI_BARRIER_LAYOUT_COMMON;
    }
}

static void Native12HeapPlacement(Native12Device *device, const D3D12_HEAP_PROPERTIES *properties,
        D3D12DDIARG_CREATEHEAP_0001 *heap)
{
    switch (properties->Type)
    {
        case D3D12_HEAP_TYPE_UPLOAD:
            heap->MemoryPool = D3D12DDI_MEMORY_POOL_L0;
            heap->CPUPageProperty = D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE;
            break;
        case D3D12_HEAP_TYPE_READBACK:
            heap->MemoryPool = D3D12DDI_MEMORY_POOL_L0;
            heap->CPUPageProperty = D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK;
            break;
        case D3D12_HEAP_TYPE_GPU_UPLOAD:
            heap->MemoryPool = device->memory.UMA ? D3D12DDI_MEMORY_POOL_L0 : D3D12DDI_MEMORY_POOL_L1;
            heap->CPUPageProperty = device->memory.CacheCoherent ? D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK
                    : D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE;
            break;
        case D3D12_HEAP_TYPE_CUSTOM:
            heap->MemoryPool = properties->MemoryPoolPreference == D3D12_MEMORY_POOL_L1
                    ? D3D12DDI_MEMORY_POOL_L1 : D3D12DDI_MEMORY_POOL_L0;
            heap->CPUPageProperty = properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE
                    ? D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE
                    : properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK
                    ? D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK : D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
            break;
        default:
            heap->MemoryPool = device->memory.UMA ? D3D12DDI_MEMORY_POOL_L0 : D3D12DDI_MEMORY_POOL_L1;
            heap->CPUPageProperty = D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
            break;
    }
    heap->CreationNodeMask = properties->CreationNodeMask ? properties->CreationNodeMask : 1;
    heap->VisibleNodeMask = properties->VisibleNodeMask ? properties->VisibleNodeMask : 1;
}

static UINT64 Native12DefaultAlignment(const D3D12_RESOURCE_DESC *desc)
{
    return desc->SampleDesc.Count > 1 && desc->Layout == D3D12_TEXTURE_LAYOUT_UNKNOWN
            ? D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
}

static UINT16 Native12MipLevels(const D3D12_RESOURCE_DESC *desc)
{
    if (desc->MipLevels) return desc->MipLevels;
    UINT64 size = desc->Width;
    if (desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE1D && desc->Height > size) size = desc->Height;
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D && desc->DepthOrArraySize > size)
        size = desc->DepthOrArraySize;
    UINT16 levels = 1;
    while (size > 1)
    {
        size >>= 1;
        ++levels;
    }
    return levels;
}

static void Native12ResourceArguments(const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        D3D12DDIARG_CREATERESOURCE_0109 *args)
{
    ZeroMemory(args, sizeof(*args));
    args->ResourceType = static_cast<D3D12DDI_RESOURCE_TYPE>(desc->Dimension);
    args->Width = desc->Width;
    args->Height = desc->Height;
    args->DepthOrArraySize = desc->DepthOrArraySize;
    args->MipLevels = Native12MipLevels(desc);
    args->Format = desc->Format;
    args->SampleDesc = desc->SampleDesc;
    args->Layout = static_cast<D3D12DDI_TEXTURE_LAYOUT>(desc->Layout);
    args->Flags = Native12ResourceFlags(desc);
    args->InitialBarrierLayout = Native12InitialLayout(desc, initial_state);
}

UINT Native12Resource::SubresourceCount() const
{
    Native12FormatInfo format = {};
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) return 1;
    Native12GetFormatInfo(desc.Format, &format);
    UINT layers = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc.DepthOrArraySize;
    return desc.MipLevels * layers * (format.planes ? format.planes : 1);
}

HRESULT Native12Resource::Initialize(const D3D12_HEAP_PROPERTIES *properties, D3D12_HEAP_FLAGS flags,
        const D3D12_RESOURCE_DESC *input, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *clear_value)
{
    desc = *input;
    desc.MipLevels = Native12MipLevels(input);
    if (!desc.SampleDesc.Count) desc.SampleDesc.Count = 1;
    if (!desc.Alignment) desc.Alignment = Native12DefaultAlignment(&desc);
    heap_properties = *properties;
    if (!heap_properties.CreationNodeMask) heap_properties.CreationNodeMask = 1;
    if (!heap_properties.VisibleNodeMask) heap_properties.VisibleNodeMask = 1;
    heap_flags = flags;
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) heap_flags |= D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    else if (desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        heap_flags |= D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    else heap_flags |= D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    identity = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(InterlockedIncrement64(&device->resource_sequence)));

    D3D12DDIARG_CREATERESOURCE_0109 resource_args;
    Native12ResourceArguments(&desc, initial_state, &resource_args);

    UINT optimization = D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_NONE;
    D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 allocation = {};
    device->functions.pfnCheckResourceAllocationInfo(device->driver_device, &resource_args,
            static_cast<D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS>(optimization), static_cast<UINT32>(desc.Alignment),
            1, &allocation);
    if (!allocation.ResourceDataSize || allocation.ResourceDataSize == ~(UINT64)0)
    {
        WARN("The driver rejected a %u x %u resource of format %#x.\n", static_cast<UINT>(desc.Width),
                desc.Height, desc.Format);
        return E_INVALIDARG;
    }
    resource_args.Layout = allocation.Layout;
    TRACE("data %#I64x/%#x, header %#I64x/%#x, additional %#I64x/%#x, layout %#x.\n",
            allocation.ResourceDataSize, allocation.ResourceDataAlignment, allocation.AdditionalDataHeaderSize,
            allocation.AdditionalDataHeaderAlignment, allocation.AdditionalDataSize,
            allocation.AdditionalDataAlignment, allocation.Layout);

    D3D12DDIARG_CREATEHEAP_0001 heap_args = {};
    heap_args.ByteSize = allocation.ResourceDataSize;
    heap_args.Alignment = allocation.ResourceDataAlignment;
    if (allocation.AdditionalDataHeaderSize)
    {
        UINT64 alignment = allocation.AdditionalDataHeaderAlignment ? allocation.AdditionalDataHeaderAlignment : 1;
        heap_args.ByteSize = ((heap_args.ByteSize + alignment - 1) & ~(alignment - 1))
                + allocation.AdditionalDataHeaderSize;
        if (alignment > heap_args.Alignment) heap_args.Alignment = alignment;
    }
    if (allocation.AdditionalDataSize)
    {
        UINT64 alignment = allocation.AdditionalDataAlignment ? allocation.AdditionalDataAlignment : 1;
        heap_args.ByteSize = ((heap_args.ByteSize + alignment - 1) & ~(alignment - 1))
                + allocation.AdditionalDataSize;
        if (alignment > heap_args.Alignment) heap_args.Alignment = alignment;
    }
    Native12HeapPlacement(device, properties, &heap_args);
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) heap_args.Flags = D3D12DDI_HEAP_FLAG_BUFFERS;
    else if (desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
        heap_args.Flags = D3D12DDI_HEAP_FLAG_RT_DS_TEXTURES;
    else heap_args.Flags = D3D12DDI_HEAP_FLAG_NON_RT_DS_TEXTURES;

    if (heap_args.Alignment)
        heap_args.ByteSize = (heap_args.ByteSize + heap_args.Alignment - 1) & ~(heap_args.Alignment - 1);

    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 session = {};
    D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes = device->functions.pfnCalcPrivateHeapAndResourceSizes(
            device->driver_device, &heap_args, &resource_args, session);
    driver_heap.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Heap ? sizes.Heap : 1);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Resource ? sizes.Resource : 1);
    if (!driver_heap.pDrvPrivate || !driver.pDrvPrivate) return E_OUTOFMEMORY;

    D3D12DDI_CLEAR_VALUES clear = {};
    if (clear_value)
    {
        clear.Format = clear_value->Format;
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        {
            clear.DepthStencil.Depth = clear_value->DepthStencil.Depth;
            clear.DepthStencil.Stencil = clear_value->DepthStencil.Stencil;
        }
        else memcpy(clear.Color, clear_value->Color, sizeof(clear.Color));
    }
    D3D12DDI_HRTRESOURCE runtime = {};
    runtime.handle = identity;
    Native12SharedTextureData nt_data = {};
    if (!shared_data && (flags & D3D12_HEAP_FLAG_SHARED) && desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        nt_data.signature = 0x54313144;
        nt_data.version = 1;
        nt_data.desc.Width = static_cast<UINT>(desc.Width);
        nt_data.desc.Height = desc.Height;
        nt_data.desc.MipLevels = desc.MipLevels;
        nt_data.desc.ArraySize = desc.DepthOrArraySize;
        nt_data.desc.Format = desc.Format;
        nt_data.desc.SampleDesc = desc.SampleDesc;
        nt_data.desc.Usage = D3D11_USAGE_DEFAULT;
        if (!(desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) nt_data.desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) nt_data.desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) nt_data.desc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) nt_data.desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        nt_data.desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        shared_data = &nt_data;
        shared_data_size = sizeof(nt_data);
        nt_sharing = true;
    }
    else if (!shared_data && (flags & D3D12_HEAP_FLAG_SHARED))
        FIXME("Shared heaps are only supported for 2D textures.\n");
    if (shared_data)
    {
        D3DKMT_CREATEALLOCATIONFLAGS create_flags = {};
        create_flags.CreateResource = 1;
        create_flags.CreateShared = 1;
        create_flags.NtSecuritySharing = nt_sharing;
        if (!device->register_resource) return E_NOINTERFACE;
        HRESULT registered = device->register_resource(device->runtime_device, identity, create_flags,
                shared_data, shared_data_size);
        if (nt_sharing) shared_data = NULL;
        if (FAILED(registered)) return registered;
    }
    HRESULT hr = device->functions.pfnCreateHeapAndResource(device->driver_device, &heap_args, driver_heap,
            runtime, &resource_args, clear_value ? &clear : NULL, session, driver);
    if (FAILED(hr))
    {
        WARN("The driver failed to create a %u x %u resource of format %#x, size %#I64x, hr %#lx.\n",
                static_cast<UINT>(desc.Width), desc.Height, desc.Format, heap_args.ByteSize, hr);
        return hr;
    }
    driver_created = true;

    D3D12DDI_HRTPAGINGQUEUE paging = {};
    paging.handle = &device->paging_queue;
    D3D12DDI_HANDLE_AND_TYPE object = {};
    object.Handle = driver_heap.pDrvPrivate;
    object.Type = D3D12DDI_HT_HEAP;
    UINT64 paging_fence = 0;
    D3D12DDIARG_MAKERESIDENT_0001 resident = {};
    resident.NumAdapters = 1;
    resident.pRTPagingQueue = &paging;
    resident.NumObjects = 1;
    resident.pObjects = &object;
    resident.pPagingFenceValue = &paging_fence;
    resident.WaitMask = 1;
    hr = device->functions.pfnMakeResident(device->driver_device, &resident);
    if (FAILED(hr) && hr != E_PENDING)
    {
        WARN("The driver failed to make a new heap resident, hr %#lx.\n", hr);
        return hr;
    }
    return device->WaitForPaging(paging_fence);
}

Native12Resource::~Native12Resource()
{
    if (driver_created) device->functions.pfnDestroyHeapAndResource(device->driver_device, driver_heap, driver);
    if (identity && device->release_resource) device->release_resource(device->runtime_device, identity);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
    if (driver_heap.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver_heap.pDrvPrivate);
    if (heap) heap->ReleaseInternal();
}

static HRESULT Native12MakeHeapResident(Native12Device *device, D3D12DDI_HHEAP heap);

HRESULT Native12Resource::InitializeOpened(const D3D12_RESOURCE_DESC *input, D3DKMT_HANDLE km_resource, UINT count,
        D3DDDI_OPENALLOCATIONINFO *allocations, void *driver_data, UINT driver_data_size, bool *adopted)
{
    *adopted = false;
    desc = *input;
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_flags = D3D12_HEAP_FLAG_SHARED;
    nt_sharing = true;
    identity = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(InterlockedIncrement64(&device->resource_sequence)));
    if (!device->functions.pfnCalcPrivateOpenedHeapAndResourceSizes || !device->functions.pfnOpenHeapAndResource
            || !device->adopt_resource)
        return E_NOTIMPL;

    D3D12DDIARG_OPENHEAP_0003 args = {};
    args.NumAllocations = count;
    args.pOpenAllocationInfo = allocations;
    args.hKMResource.handle = km_resource;
    args.pPrivateDriverData = driver_data;
    args.PrivateDriverDataSize = driver_data_size;
    args.InitialResourceState = D3D12DDI_RESOURCE_STATE_COMMON;
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 session = {};
    D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes = device->functions.pfnCalcPrivateOpenedHeapAndResourceSizes(
            device->driver_device, &args, session);
    driver_heap.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Heap ? sizes.Heap : 1);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Resource ? sizes.Resource : 1);
    if (!driver_heap.pDrvPrivate || !driver.pDrvPrivate) return E_OUTOFMEMORY;

    HRESULT hr = device->adopt_resource(device->runtime_device, identity, km_resource, 0,
            count == 1 ? allocations[0].hAllocation : 0);
    if (FAILED(hr)) return hr;
    *adopted = true;
    D3D12DDI_HRTRESOURCE runtime = {};
    runtime.handle = identity;
    hr = device->functions.pfnOpenHeapAndResource(device->driver_device, &args, driver_heap, runtime, session, driver);
    if (FAILED(hr))
    {
        WARN("The driver failed to open a shared resource, hr %#lx.\n", hr);
        return hr;
    }
    driver_created = true;
    return Native12MakeHeapResident(device, driver_heap);
}

static HRESULT Native12MakeHeapResident(Native12Device *device, D3D12DDI_HHEAP heap)
{
    D3D12DDI_HRTPAGINGQUEUE paging = {};
    paging.handle = &device->paging_queue;
    D3D12DDI_HANDLE_AND_TYPE object = {};
    object.Handle = heap.pDrvPrivate;
    object.Type = D3D12DDI_HT_HEAP;
    UINT64 paging_fence = 0;
    D3D12DDIARG_MAKERESIDENT_0001 resident = {};
    resident.NumAdapters = 1;
    resident.pRTPagingQueue = &paging;
    resident.NumObjects = 1;
    resident.pObjects = &object;
    resident.pPagingFenceValue = &paging_fence;
    HRESULT hr = device->functions.pfnMakeResident(device->driver_device, &resident);
    if (FAILED(hr) && hr != E_PENDING) return hr;
    return device->WaitForPaging(paging_fence);
}

HRESULT Native12Heap::Initialize(const D3D12_HEAP_DESC *input)
{
    desc = *input;
    if (!desc.Properties.CreationNodeMask) desc.Properties.CreationNodeMask = 1;
    if (!desc.Properties.VisibleNodeMask) desc.Properties.VisibleNodeMask = 1;
    if (!desc.Alignment) desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.SizeInBytes = (desc.SizeInBytes + desc.Alignment - 1) & ~(desc.Alignment - 1);

    D3D12DDIARG_CREATEHEAP_0001 args = {};
    args.ByteSize = desc.SizeInBytes;
    args.Alignment = desc.Alignment;
    Native12HeapPlacement(device, &desc.Properties, &args);
    UINT flags = 0;
    if (!(desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS)) flags |= D3D12DDI_HEAP_FLAG_BUFFERS;
    if (!(desc.Flags & D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES)) flags |= D3D12DDI_HEAP_FLAG_RT_DS_TEXTURES;
    if (!(desc.Flags & D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES)) flags |= D3D12DDI_HEAP_FLAG_NON_RT_DS_TEXTURES;
    args.Flags = static_cast<D3D12DDI_HEAP_FLAGS>(flags);

    D3D12_RESOURCE_DESC whole = {};
    whole.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    whole.Width = desc.SizeInBytes;
    whole.Height = 1;
    whole.DepthOrArraySize = 1;
    whole.MipLevels = 1;
    whole.SampleDesc.Count = 1;
    whole.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12DDIARG_CREATERESOURCE_0109 resource_args;
    Native12ResourceArguments(&whole, D3D12_RESOURCE_STATE_COMMON, &resource_args);

    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 session = {};
    D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes = device->functions.pfnCalcPrivateHeapAndResourceSizes(
            device->driver_device, &args, &resource_args, session);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Heap ? sizes.Heap : 1);
    buffer.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Resource ? sizes.Resource : 1);
    if (!driver.pDrvPrivate || !buffer.pDrvPrivate) return E_OUTOFMEMORY;
    identity = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(InterlockedIncrement64(&device->resource_sequence)));
    D3D12DDI_HRTRESOURCE runtime = {};
    runtime.handle = identity;
    HRESULT hr = device->functions.pfnCreateHeapAndResource(device->driver_device, &args, driver, runtime,
            &resource_args, NULL, session, buffer);
    if (FAILED(hr))
    {
        WARN("The driver failed to create a heap of %#I64x bytes, flags %#x, hr %#lx.\n", desc.SizeInBytes,
                flags, hr);
        return hr;
    }
    driver_created = true;
    return Native12MakeHeapResident(device, driver);
}

Native12Heap::~Native12Heap()
{
    if (driver_created) device->functions.pfnDestroyHeapAndResource(device->driver_device, driver, buffer);
    if (identity && device->release_resource) device->release_resource(device->runtime_device, identity);
    if (buffer.pDrvPrivate) HeapFree(GetProcessHeap(), 0, buffer.pDrvPrivate);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
}

HRESULT Native12Heap::MapBase(void **base)
{
    if (!mapping)
    {
        void *address = NULL;
        HRESULT hr = device->functions.pfnMapHeap(device->driver_device, driver, &address);
        if (FAILED(hr) || !address)
        {
            WARN("The driver failed to map a heap, hr %#lx.\n", hr);
            return FAILED(hr) ? hr : E_FAIL;
        }
        if (InterlockedCompareExchangePointer(&mapping, address, NULL))
            device->functions.pfnUnmapHeap(device->driver_device, driver);
    }
    *base = mapping;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Native12Heap::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12Heap1) || IsEqualGUID(iid, IID_ID3D12Heap) || IsEqualGUID(iid, IID_ID3D12Pageable)
            || IsEqualGUID(iid, IID_ID3D12DeviceChild) || IsEqualGUID(iid, IID_ID3D12Object)
            || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12Heap1 *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Native12Heap::GetProtectedResourceSession(REFIID, void **session)
{
    if (session) *session = NULL;
    return DXGI_ERROR_NOT_FOUND;
}

D3D12_HEAP_DESC *STDMETHODCALLTYPE Native12Heap::GetDesc(D3D12_HEAP_DESC *out)
{
    *out = desc;
    return out;
}

HRESULT Native12Resource::InitializeReserved(const D3D12_RESOURCE_DESC *input, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *clear_value)
{
    desc = *input;
    desc.MipLevels = Native12MipLevels(input);
    if (!desc.SampleDesc.Count) desc.SampleDesc.Count = 1;
    if (!desc.Alignment) desc.Alignment = Native12DefaultAlignment(&desc);
    reserved = true;
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    identity = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(InterlockedIncrement64(&device->resource_sequence)));

    D3D12DDIARG_CREATERESOURCE_0109 resource_args;
    Native12ResourceArguments(&desc, initial_state, &resource_args);
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 session = {};
    D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes = device->functions.pfnCalcPrivateHeapAndResourceSizes(
            device->driver_device, NULL, &resource_args, session);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Resource ? sizes.Resource : 1);
    if (!driver.pDrvPrivate) return E_OUTOFMEMORY;

    D3D12DDI_CLEAR_VALUES clear = {};
    if (clear_value)
    {
        clear.Format = clear_value->Format;
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        {
            clear.DepthStencil.Depth = clear_value->DepthStencil.Depth;
            clear.DepthStencil.Stencil = clear_value->DepthStencil.Stencil;
        }
        else memcpy(clear.Color, clear_value->Color, sizeof(clear.Color));
    }
    D3D12DDI_HRTRESOURCE runtime = {};
    runtime.handle = identity;
    D3D12DDI_HHEAP no_heap = {};
    HRESULT hr = device->functions.pfnCreateHeapAndResource(device->driver_device, NULL, no_heap, runtime,
            &resource_args, clear_value ? &clear : NULL, session, driver);
    if (FAILED(hr))
    {
        WARN("The driver failed to reserve a %u x %u resource of format %#x, hr %#lx.\n",
                static_cast<UINT>(desc.Width), desc.Height, desc.Format, hr);
        return hr;
    }
    driver_created = true;
    TRACE("Reserved resource %p, address %#I64x.\n", this,
            device->functions.pfnCheckResourceVirtualAddress(device->driver_device, driver));
    return S_OK;
}

HRESULT Native12Resource::InitializePlaced(Native12Heap *parent, UINT64 offset, const D3D12_RESOURCE_DESC *input,
        D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *clear_value)
{
    desc = *input;
    desc.MipLevels = Native12MipLevels(input);
    if (!desc.SampleDesc.Count) desc.SampleDesc.Count = 1;
    if (!desc.Alignment) desc.Alignment = Native12DefaultAlignment(&desc);
    heap = parent;
    heap->AddInternal();
    heap_offset = offset;
    heap_properties = parent->desc.Properties;
    heap_flags = parent->desc.Flags;
    identity = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(InterlockedIncrement64(&device->resource_sequence)));

    D3D12DDIARG_CREATERESOURCE_0109 resource_args;
    Native12ResourceArguments(&desc, initial_state, &resource_args);
    D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 allocation = {};
    device->functions.pfnCheckResourceAllocationInfo(device->driver_device, &resource_args,
            D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_NONE, static_cast<UINT32>(desc.Alignment), 1, &allocation);
    if (!allocation.ResourceDataSize || allocation.ResourceDataSize == ~(UINT64)0) return E_INVALIDARG;
    if (offset + allocation.ResourceDataSize > parent->desc.SizeInBytes) return E_INVALIDARG;
    resource_args.Layout = allocation.Layout;
    resource_args.ReuseBufferGPUVA.BaseAddress.UMD.hResource = parent->buffer;
    resource_args.ReuseBufferGPUVA.BaseAddress.UMD.Offset = offset;

    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 session = {};
    D3D12DDI_HEAP_AND_RESOURCE_SIZES sizes = device->functions.pfnCalcPrivateHeapAndResourceSizes(
            device->driver_device, NULL, &resource_args, session);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizes.Resource ? sizes.Resource : 1);
    if (!driver.pDrvPrivate) return E_OUTOFMEMORY;

    D3D12DDI_CLEAR_VALUES clear = {};
    if (clear_value)
    {
        clear.Format = clear_value->Format;
        if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        {
            clear.DepthStencil.Depth = clear_value->DepthStencil.Depth;
            clear.DepthStencil.Stencil = clear_value->DepthStencil.Stencil;
        }
        else memcpy(clear.Color, clear_value->Color, sizeof(clear.Color));
    }
    D3D12DDI_HRTRESOURCE runtime = {};
    runtime.handle = identity;
    HRESULT hr = device->functions.pfnCreateHeapAndResource(device->driver_device, NULL, parent->driver, runtime,
            &resource_args, clear_value ? &clear : NULL, session, driver);
    if (FAILED(hr))
    {
        WARN("The driver failed to place a %u x %u resource of format %#x at heap offset %#I64x, hr %#lx.\n",
                static_cast<UINT>(desc.Width), desc.Height, desc.Format, offset, hr);
        return hr;
    }
    driver_created = true;
    TRACE("Placed resource %p in heap %p at %#I64x, address %#I64x.\n", this, parent, offset,
            device->functions.pfnCheckResourceVirtualAddress(device->driver_device, driver));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Native12Resource::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12Resource2) || IsEqualGUID(iid, IID_ID3D12Resource1)
            || IsEqualGUID(iid, IID_ID3D12Resource) || IsEqualGUID(iid, IID_ID3D12Pageable)
            || IsEqualGUID(iid, IID_ID3D12DeviceChild) || IsEqualGUID(iid, IID_ID3D12Object)
            || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12Resource2 *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Native12Resource::GetProtectedResourceSession(REFIID, void **session)
{
    if (session) *session = NULL;
    return DXGI_ERROR_NOT_FOUND;
}

D3D12_RESOURCE_DESC1 *STDMETHODCALLTYPE Native12Resource::GetDesc1(D3D12_RESOURCE_DESC1 *out)
{
    ZeroMemory(out, sizeof(*out));
    out->Dimension = desc.Dimension;
    out->Alignment = desc.Alignment;
    out->Width = desc.Width;
    out->Height = desc.Height;
    out->DepthOrArraySize = desc.DepthOrArraySize;
    out->MipLevels = desc.MipLevels;
    out->Format = desc.Format;
    out->SampleDesc = desc.SampleDesc;
    out->Layout = desc.Layout;
    out->Flags = desc.Flags;
    return out;
}

HRESULT STDMETHODCALLTYPE Native12Resource::Map(UINT sub_resource, const D3D12_RANGE *, void **data)
{
    if (sub_resource >= SubresourceCount()) return E_INVALIDARG;
    if (heap_properties.Type == D3D12_HEAP_TYPE_DEFAULT) return E_INVALIDARG;
    if (data && desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER && desc.Layout == D3D12_TEXTURE_LAYOUT_UNKNOWN)
        return E_INVALIDARG;
    if (!mapping && heap)
    {
        void *base = NULL;
        HRESULT hr = heap->MapBase(&base);
        if (FAILED(hr)) return hr;
        InterlockedCompareExchangePointer(&mapping, static_cast<BYTE *>(base) + heap_offset, NULL);
    }
    if (!mapping)
    {
        void *base = NULL;
        HRESULT hr = device->functions.pfnMapHeap(device->driver_device, driver_heap, &base);
        if (FAILED(hr) || !base)
        {
            WARN("The driver failed to map a heap, hr %#lx.\n", hr);
            return FAILED(hr) ? hr : E_FAIL;
        }
        if (InterlockedCompareExchangePointer(&mapping, base, NULL))
            device->functions.pfnUnmapHeap(device->driver_device, driver_heap);
    }
    InterlockedIncrement(&map_count);
    if (data)
    {
        UINT64 offset = 0;
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            D3D12DDI_SUBRESOURCE_INFO info = {};
            device->functions.pfnCheckSubresourceInfo(device->driver_device, driver, sub_resource, &info);
            offset = info.Offset;
        }
        *data = static_cast<BYTE *>(mapping) + offset;
    }
    return S_OK;
}

void STDMETHODCALLTYPE Native12Resource::Unmap(UINT, const D3D12_RANGE *)
{
    if (map_count > 0) InterlockedDecrement(&map_count);
}

D3D12_RESOURCE_DESC *STDMETHODCALLTYPE Native12Resource::GetDesc(D3D12_RESOURCE_DESC *out)
{
    *out = desc;
    return out;
}

D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE Native12Resource::GetGPUVirtualAddress()
{
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) return 0;
    if (!address_valid)
    {
        address = device->functions.pfnCheckResourceVirtualAddress(device->driver_device, driver);
        address_valid = true;
    }
    return address;
}

static HRESULT Native12SubresourceBox(const Native12Resource *resource, UINT sub_resource, const D3D12_BOX *input,
        D3D12_BOX *box, Native12FormatInfo *format)
{
    const D3D12_RESOURCE_DESC &desc = resource->desc;
    if (resource->reserved || desc.SampleDesc.Count > 1 || sub_resource >= resource->SubresourceCount())
        return E_INVALIDARG;
    if (resource->heap_properties.Type == D3D12_HEAP_TYPE_DEFAULT
            || (resource->heap_properties.Type == D3D12_HEAP_TYPE_CUSTOM
                && resource->heap_properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE))
        return E_INVALIDARG;
    UINT64 width = desc.Width;
    UINT height = 1, depth = 1;
    format->block_width = format->block_height = format->block_bytes = format->planes = 1;
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        if (!Native12GetFormatInfo(desc.Format, format)) return E_INVALIDARG;
        UINT layers = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc.DepthOrArraySize;
        UINT mip = sub_resource % desc.MipLevels;
        if (format->planes == 2) format->block_bytes = sub_resource / (desc.MipLevels * layers) ? 1 : 4;
        width = desc.Width >> mip;
        height = desc.Height >> mip;
        depth = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? desc.DepthOrArraySize >> mip : 1;
        if (!width) width = 1;
        if (!height) height = 1;
        if (!depth) depth = 1;
    }
    if (!input)
    {
        box->left = box->top = box->front = 0;
        box->right = static_cast<UINT>(width);
        box->bottom = height;
        box->back = depth;
        return width > ~0u ? E_INVALIDARG : S_OK;
    }
    if (input->right > width || input->bottom > height || input->back > depth) return E_INVALIDARG;
    if (input->left >= input->right || input->top >= input->bottom || input->front >= input->back) return S_FALSE;
    if (input->left % format->block_width || input->top % format->block_height
            || (input->right % format->block_width && input->right != width)
            || (input->bottom % format->block_height && input->bottom != height))
        return E_INVALIDARG;
    *box = *input;
    return S_OK;
}

static void Native12CopyRows(BYTE *dst, UINT64 dst_row_pitch, UINT64 dst_slice_pitch, const BYTE *src,
        UINT64 src_row_pitch, UINT64 src_slice_pitch, UINT64 row_bytes, UINT rows, UINT slices)
{
    for (UINT z = 0; z < slices; ++z)
        for (UINT y = 0; y < rows; ++y)
            memcpy(dst + z * dst_slice_pitch + y * dst_row_pitch, src + z * src_slice_pitch + y * src_row_pitch,
                    static_cast<SIZE_T>(row_bytes));
}

static HRESULT Native12CopyThroughQueue(Native12Resource *resource, UINT sub_resource, const D3D12_BOX &box,
        UINT64 row_bytes, UINT rows, UINT slices, BYTE *data, UINT64 row_pitch, UINT64 slice_pitch, bool write)
{
    Native12Device *device = resource->device;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    device->GetCopyableFootprints(&resource->desc, sub_resource, 1, 0, &footprint, NULL, NULL, NULL);
    footprint.Footprint.Width = box.right - box.left;
    footprint.Footprint.Height = box.bottom - box.top;
    footprint.Footprint.Depth = slices;
    footprint.Footprint.RowPitch = static_cast<UINT>((row_bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
            & ~static_cast<UINT64>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1));
    UINT64 staging_slice = static_cast<UINT64>(footprint.Footprint.RowPitch) * rows;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = write ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = staging_slice * slices;
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ID3D12Resource *staging = NULL;
    ID3D12CommandQueue *queue = NULL;
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    ID3D12Fence *fence = NULL;
    BYTE *mapped = NULL;
    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            write ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            IID_ID3D12Resource, reinterpret_cast<void **>(&staging));
    if (SUCCEEDED(hr)) hr = staging->Map(0, NULL, reinterpret_cast<void **>(&mapped));
    if (SUCCEEDED(hr) && write)
        Native12CopyRows(mapped, footprint.Footprint.RowPitch, staging_slice, data, row_pitch, slice_pitch,
                row_bytes, rows, slices);
    if (SUCCEEDED(hr)) hr = device->CreateCommandQueue(&queue_desc, IID_ID3D12CommandQueue,
            reinterpret_cast<void **>(&queue));
    if (SUCCEEDED(hr)) hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_ID3D12CommandAllocator, reinterpret_cast<void **>(&allocator));
    if (SUCCEEDED(hr)) hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
            IID_ID3D12GraphicsCommandList, reinterpret_cast<void **>(&list));
    if (SUCCEEDED(hr)) hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence,
            reinterpret_cast<void **>(&fence));
    if (SUCCEEDED(hr))
    {
        D3D12_TEXTURE_COPY_LOCATION texture = {};
        texture.pResource = resource;
        texture.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        texture.SubresourceIndex = sub_resource;
        D3D12_TEXTURE_COPY_LOCATION placed = {};
        placed.pResource = staging;
        placed.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        placed.PlacedFootprint = footprint;
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = sub_resource;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrier.Transition.StateAfter = write ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &barrier);
        if (write) list->CopyTextureRegion(&texture, box.left, box.top, box.front, &placed, NULL);
        else list->CopyTextureRegion(&placed, 0, 0, 0, &texture, &box);
        barrier.Transition.StateBefore = barrier.Transition.StateAfter;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        list->ResourceBarrier(1, &barrier);
        hr = list->Close();
    }
    if (SUCCEEDED(hr))
    {
        ID3D12CommandList *lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        hr = queue->Signal(fence, 1);
    }
    if (SUCCEEDED(hr)) hr = fence->SetEventOnCompletion(1, NULL);
    if (SUCCEEDED(hr) && !write)
        Native12CopyRows(data, row_pitch, slice_pitch, mapped, footprint.Footprint.RowPitch, staging_slice,
                row_bytes, rows, slices);
    if (fence) fence->Release();
    if (list) list->Release();
    if (allocator) allocator->Release();
    if (queue) queue->Release();
    if (mapped) staging->Unmap(0, NULL);
    if (staging) staging->Release();
    if (FAILED(hr)) WARN("Subresource %u transfer failed, hr %#lx.\n", sub_resource, hr);
    return hr;
}

static HRESULT Native12TransferSubresource(Native12Resource *resource, UINT sub_resource, const D3D12_BOX *input,
        BYTE *data, UINT64 row_pitch, UINT64 slice_pitch, bool write)
{
    D3D12_BOX box;
    Native12FormatInfo format = {};
    HRESULT hr = Native12SubresourceBox(resource, sub_resource, input, &box, &format);
    if (hr != S_OK) return SUCCEEDED(hr) ? S_OK : hr;
    UINT64 row_bytes = (box.right - box.left + format.block_width - 1) / format.block_width
            * static_cast<UINT64>(format.block_bytes);
    UINT rows = (box.bottom - box.top + format.block_height - 1) / format.block_height;
    UINT slices = box.back - box.front;
    if (resource->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER
            && resource->desc.Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR)
        return Native12CopyThroughQueue(resource, sub_resource, box, row_bytes, rows, slices, data, row_pitch,
                slice_pitch, write);

    BYTE *base = NULL;
    if (FAILED(hr = resource->Map(sub_resource, NULL, reinterpret_cast<void **>(&base)))) return hr;
    UINT64 resource_row = row_bytes, resource_slice = row_bytes;
    if (resource->desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        D3D12DDI_SUBRESOURCE_INFO info = {};
        resource->device->functions.pfnCheckSubresourceInfo(resource->device->driver_device, resource->driver,
                sub_resource, &info);
        resource_row = info.RowStride;
        resource_slice = info.DepthStride;
    }
    base += box.front * resource_slice + box.top / format.block_height * resource_row
            + box.left / format.block_width * static_cast<UINT64>(format.block_bytes);
    if (write) Native12CopyRows(base, resource_row, resource_slice, data, row_pitch, slice_pitch, row_bytes, rows, slices);
    else Native12CopyRows(data, row_pitch, slice_pitch, base, resource_row, resource_slice, row_bytes, rows, slices);
    resource->Unmap(sub_resource, NULL);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Native12Resource::WriteToSubresource(UINT dst_sub_resource, const D3D12_BOX *dst_box,
        const void *src_data, UINT src_row_pitch, UINT src_slice_pitch)
{
    TRACE("sub-resource %u, box %p, data %p, pitch %u/%u.\n", dst_sub_resource, dst_box, src_data, src_row_pitch,
            src_slice_pitch);
    if (!src_data || desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) return E_INVALIDARG;
    return Native12TransferSubresource(this, dst_sub_resource, dst_box,
            static_cast<BYTE *>(const_cast<void *>(src_data)), src_row_pitch, src_slice_pitch, true);
}

HRESULT STDMETHODCALLTYPE Native12Resource::ReadFromSubresource(void *dst_data, UINT dst_row_pitch,
        UINT dst_slice_pitch, UINT src_sub_resource, const D3D12_BOX *src_box)
{
    TRACE("data %p, pitch %u/%u, sub-resource %u, box %p.\n", dst_data, dst_row_pitch, dst_slice_pitch,
            src_sub_resource, src_box);
    if (!dst_data || desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) return E_INVALIDARG;
    return Native12TransferSubresource(this, src_sub_resource, src_box, static_cast<BYTE *>(dst_data), dst_row_pitch,
            dst_slice_pitch, false);
}

HRESULT STDMETHODCALLTYPE Native12Resource::GetHeapProperties(D3D12_HEAP_PROPERTIES *properties,
        D3D12_HEAP_FLAGS *flags)
{
    if (reserved) return E_INVALIDARG;
    if (properties) *properties = heap_properties;
    if (flags) *flags = heap_flags;
    return S_OK;
}

HRESULT Native12Device::WaitForPaging(UINT64 fence_value)
{
    if (!fence_value || !paging_queue.hSyncObject) return S_OK;
    const volatile UINT64 *current = static_cast<const volatile UINT64 *>(paging_queue.FenceValueCPUVirtualAddress);
    if (current && *current >= fence_value) return S_OK;
    if (!kernel_callbacks.pfnWaitForSynchronizationObjectFromCpuCb) return E_NOINTERFACE;
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait = {};
    wait.ObjectCount = 1;
    wait.ObjectHandleArray = &paging_queue.hSyncObject;
    wait.FenceValueArray = &fence_value;
    return kernel_callbacks.pfnWaitForSynchronizationObjectFromCpuCb(runtime_device, &wait);
}

static bool Native12ValidResourceState(D3D12_RESOURCE_STATES state)
{
    const UINT valid = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER
            | D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_UNORDERED_ACCESS
            | D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_DEPTH_READ
            | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
            | D3D12_RESOURCE_STATE_STREAM_OUT | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT
            | D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE
            | D3D12_RESOURCE_STATE_RESOLVE_DEST | D3D12_RESOURCE_STATE_RESOLVE_SOURCE
            | D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE
            | D3D12_RESOURCE_STATE_VIDEO_DECODE_READ | D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE
            | D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ | D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE
            | D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ | D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE;
    const UINT writes = D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_UNORDERED_ACCESS
            | D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_STREAM_OUT | D3D12_RESOURCE_STATE_COPY_DEST
            | D3D12_RESOURCE_STATE_RESOLVE_DEST | D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE
            | D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE | D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE;
    UINT value = state;
    if (value & ~valid) return false;
    if ((value & writes) && (value & (value - 1))) return false;
    return true;
}

static bool Native12SmallTexture(const D3D12_RESOURCE_DESC *desc, const Native12FormatInfo &format)
{
    UINT64 tile_width, tile_height, tile_depth = 1, tiles;
    UINT tile_texels = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT / format.block_bytes;

    if (desc->Layout != D3D12_TEXTURE_LAYOUT_UNKNOWN
            || (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)))
        return false;
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
    {
        tile_width = tile_texels;
        tile_height = 1;
    }
    else
    {
        UINT log_size = 0, log_depth, log_height;
        while ((2u << log_size) <= tile_texels) ++log_size;
        log_depth = desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? log_size / 3 : 0;
        log_height = (log_size - log_depth) / 2;
        tile_width = (1ull << (log_size - log_depth - log_height)) * format.block_width;
        tile_height = (1ull << log_height) * format.block_height;
        tile_depth = 1ull << log_depth;
    }
    tiles = ((desc->Width + tile_width - 1) / tile_width) * ((desc->Height + tile_height - 1) / tile_height);
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        tiles *= (desc->DepthOrArraySize + tile_depth - 1) / tile_depth;
    return tiles <= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT / D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
}

HRESULT Native12ValidateResourceDesc(const D3D12_RESOURCE_DESC *desc)
{
    Native12FormatInfo format = {};
    switch (desc->Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            if (desc->MipLevels != 1 || desc->Format != DXGI_FORMAT_UNKNOWN
                    || desc->Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR || desc->Height != 1
                    || desc->DepthOrArraySize != 1 || desc->SampleDesc.Count != 1 || desc->SampleDesc.Quality
                    || (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)
                    || (desc->Alignment && desc->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT))
                return E_INVALIDARG;
            return S_OK;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            if (desc->Height != 1) return E_INVALIDARG;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            break;
        default:
            return E_INVALIDARG;
    }
    if (!desc->SampleDesc.Count) return E_INVALIDARG;
    if (desc->SampleDesc.Count > 1
            && !(desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)))
        return E_INVALIDARG;
    if (desc->Format == DXGI_FORMAT_UNKNOWN || !Native12GetFormatInfo(desc->Format, &format) || !format.block_bytes)
        return E_INVALIDARG;
    if (desc->Layout == D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE && format.planes > 1) return E_INVALIDARG;
    if (desc->Layout == D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE) return E_INVALIDARG;
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D && format.block_height > 1) return E_INVALIDARG;
    if (desc->Alignment)
    {
        if (desc->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
                && desc->Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT
                && (desc->SampleDesc.Count == 1 || desc->Alignment != D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT))
            return E_INVALIDARG;
        if (desc->Layout == D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE
                && desc->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)
            return E_INVALIDARG;
        if (desc->Alignment == D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT
                && (desc->SampleDesc.Count > 1 || !Native12SmallTexture(desc, format)))
            return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT Native12ValidateResource(D3D12_HEAP_TYPE heap_type, const D3D12_RESOURCE_DESC *desc,
        D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear_value)
{
    bool buffer = desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    if (heap_type == D3D12_HEAP_TYPE_UPLOAD || heap_type == D3D12_HEAP_TYPE_READBACK)
    {
        if (!buffer) return E_INVALIDARG;
        if (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
            return E_INVALIDARG;
    }
    if (heap_type == D3D12_HEAP_TYPE_UPLOAD && state != D3D12_RESOURCE_STATE_GENERIC_READ) return E_INVALIDARG;
    if (heap_type == D3D12_HEAP_TYPE_READBACK && state != D3D12_RESOURCE_STATE_COPY_DEST) return E_INVALIDARG;
    if (!Native12ValidResourceState(state)) return E_INVALIDARG;
    if (state == D3D12_RESOURCE_STATE_RENDER_TARGET && !(desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
        return E_INVALIDARG;
    if (clear_value && buffer) return E_INVALIDARG;
    return Native12ValidateResourceDesc(desc);
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateCommittedResource(const D3D12_HEAP_PROPERTIES *properties,
        D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *clear_value, REFIID iid, void **out)
{
    if (!properties || !desc) return E_INVALIDARG;
    if (out) *out = NULL;
    HRESULT hr = ValidateHeapProperties(properties, flags);
    if (FAILED(hr)) return hr;
    hr = Native12ValidateResource(properties->Type, desc, initial_state, clear_value);
    if (FAILED(hr))
    {
        WARN("Invalid committed resource, dimension %u, format %#x, state %#x.\n", desc->Dimension, desc->Format,
                initial_state);
        return hr;
    }
    Native12Resource *resource = new Native12Resource(this);
    if (!resource) return E_OUTOFMEMORY;
    hr = resource->Initialize(properties, flags, desc, initial_state, clear_value);
    if (SUCCEEDED(hr)) hr = out ? resource->QueryInterface(iid, out) : S_FALSE;
    resource->Release();
    return hr;
}

D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE Native12Device::GetResourceAllocationInfo(
        D3D12_RESOURCE_ALLOCATION_INFO *out, UINT, UINT count, const D3D12_RESOURCE_DESC *descs)
{
    out->SizeInBytes = 0;
    out->Alignment = 0;
    for (UINT i = 0; i < count; ++i)
    {
        D3D12DDIARG_CREATERESOURCE_0109 args;
        D3D12_RESOURCE_DESC desc = descs[i];
        desc.MipLevels = Native12MipLevels(&descs[i]);
        if (!desc.Alignment) desc.Alignment = Native12DefaultAlignment(&desc);
        if (FAILED(Native12ValidateResourceDesc(&desc)))
        {
            out->SizeInBytes = ~(UINT64)0;
            out->Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            return out;
        }
        Native12ResourceArguments(&desc, D3D12_RESOURCE_STATE_COMMON, &args);
        D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 info = {};
        functions.pfnCheckResourceAllocationInfo(driver_device, &args, D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_NONE,
                static_cast<UINT32>(desc.Alignment), 1, &info);
        if (!info.ResourceDataSize || info.ResourceDataSize == ~(UINT64)0)
        {
            out->SizeInBytes = ~(UINT64)0;
            out->Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            return out;
        }
        if (info.ResourceDataAlignment < desc.Alignment) info.ResourceDataAlignment = static_cast<UINT32>(desc.Alignment);
        if (info.ResourceDataAlignment > out->Alignment) out->Alignment = info.ResourceDataAlignment;
        out->SizeInBytes = (out->SizeInBytes + info.ResourceDataAlignment - 1)
                & ~static_cast<UINT64>(info.ResourceDataAlignment - 1);
        out->SizeInBytes += info.ResourceDataSize;
    }
    if (out->Alignment)
        out->SizeInBytes = (out->SizeInBytes + out->Alignment - 1) & ~(out->Alignment - 1);
    return out;
}

HRESULT Native12Device::ValidateHeapProperties(const D3D12_HEAP_PROPERTIES *properties, D3D12_HEAP_FLAGS flags)
{
    if (flags & D3D12_HEAP_FLAG_TOOLS_USE_MANUAL_WRITE_TRACKING) return E_INVALIDARG;
    switch (properties->Type)
    {
        case D3D12_HEAP_TYPE_GPU_UPLOAD:
            if (!gpu_upload.L1MemoryFullyCpuAccessible
                    || (flags & (D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER
                    | D3D12_HEAP_FLAG_ALLOW_DISPLAY)))
                return E_INVALIDARG;
            return properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_UNKNOWN
                    && properties->MemoryPoolPreference == D3D12_MEMORY_POOL_UNKNOWN ? S_OK : E_INVALIDARG;
        case D3D12_HEAP_TYPE_DEFAULT:
        case D3D12_HEAP_TYPE_UPLOAD:
        case D3D12_HEAP_TYPE_READBACK:
            return properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_UNKNOWN
                    && properties->MemoryPoolPreference == D3D12_MEMORY_POOL_UNKNOWN ? S_OK : E_INVALIDARG;
        case D3D12_HEAP_TYPE_CUSTOM:
            if (properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_UNKNOWN
                    || properties->CPUPageProperty > D3D12_CPU_PAGE_PROPERTY_WRITE_BACK)
                return E_INVALIDARG;
            if (properties->MemoryPoolPreference == D3D12_MEMORY_POOL_L0) return S_OK;
            if (properties->MemoryPoolPreference != D3D12_MEMORY_POOL_L1 || memory.UMA) return E_INVALIDARG;
            return properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE
                    || (properties->CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE
                    && gpu_upload.L1MemoryFullyCpuAccessible) ? S_OK : E_INVALIDARG;
        default:
            return E_INVALIDARG;
    }
}

D3D12_HEAP_PROPERTIES *STDMETHODCALLTYPE Native12Device::GetCustomHeapProperties(D3D12_HEAP_PROPERTIES *out,
        UINT, D3D12_HEAP_TYPE type)
{
    ZeroMemory(out, sizeof(*out));
    out->Type = D3D12_HEAP_TYPE_CUSTOM;
    out->CreationNodeMask = 1;
    out->VisibleNodeMask = 1;
    switch (type)
    {
        case D3D12_HEAP_TYPE_UPLOAD:
            out->CPUPageProperty = memory.CacheCoherent ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK
                    : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
            out->MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
            break;
        case D3D12_HEAP_TYPE_READBACK:
            out->CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
            out->MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
            break;
        case D3D12_HEAP_TYPE_GPU_UPLOAD:
            out->CPUPageProperty = memory.CacheCoherent ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK
                    : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
            out->MemoryPoolPreference = memory.UMA ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1;
            break;
        case D3D12_HEAP_TYPE_DEFAULT:
            out->CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
            out->MemoryPoolPreference = memory.UMA ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1;
            break;
        default:
            out->Type = static_cast<D3D12_HEAP_TYPE>(0);
            break;
    }
    return out;
}

static void Native12InvalidFootprints(UINT count, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts, UINT *rows,
        UINT64 *row_sizes)
{
    for (UINT i = 0; i < count; ++i)
    {
        if (layouts) memset(&layouts[i], 0xff, sizeof(layouts[i]));
        if (rows) rows[i] = ~0u;
        if (row_sizes) row_sizes[i] = ~(UINT64)0;
    }
}

void STDMETHODCALLTYPE Native12Device::GetCopyableFootprints(const D3D12_RESOURCE_DESC *desc, UINT first,
        UINT count, UINT64 base_offset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts, UINT *rows,
        UINT64 *row_sizes, UINT64 *total)
{
    Native12FormatInfo format = {};
    UINT64 offset = base_offset;

    if (total) *total = ~(UINT64)0;
    if (!desc) return;
    if (FAILED(Native12ValidateResourceDesc(desc)))
    {
        Native12InvalidFootprints(count, layouts, rows, row_sizes);
        return;
    }
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        if (first || count > 1)
        {
            Native12InvalidFootprints(count, layouts, rows, row_sizes);
            return;
        }
        if (count)
        {
            if (layouts)
            {
                layouts[0].Offset = base_offset;
                layouts[0].Footprint.Format = DXGI_FORMAT_UNKNOWN;
                layouts[0].Footprint.Width = static_cast<UINT>(desc->Width);
                layouts[0].Footprint.Height = 1;
                layouts[0].Footprint.Depth = 1;
                layouts[0].Footprint.RowPitch = (static_cast<UINT>(desc->Width) + 255) & ~255u;
            }
            if (rows) rows[0] = 1;
            if (row_sizes) row_sizes[0] = desc->Width;
        }
        if (total) *total = count ? desc->Width : 0;
        return;
    }
    if (!Native12GetFormatInfo(desc->Format, &format)) return;

    UINT mips = Native12MipLevels(desc);
    UINT layers = desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc->DepthOrArraySize;
    UINT planes = format.planes ? format.planes : 1;
    UINT pitch_alignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * planes;
    UINT64 total_bytes = 0;
    if (first >= mips * layers * planes || count > mips * layers * planes - first)
    {
        Native12InvalidFootprints(count, layouts, rows, row_sizes);
        return;
    }
    offset = 0;
    for (UINT i = 0; i < count; ++i)
    {
        UINT subresource = first + i;
        UINT mip = subresource % mips;
        UINT plane = subresource / (mips * layers);
        UINT64 width = desc->Width >> mip;
        UINT height = desc->Height >> mip;
        UINT depth = desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? desc->DepthOrArraySize >> mip : 1;
        UINT block_bytes = format.block_bytes;
        DXGI_FORMAT plane_format = desc->Format;
        if (!width) width = 1;
        if (!height) height = 1;
        if (!depth) depth = 1;
        if (planes == 2)
        {
            block_bytes = plane ? 1 : 4;
            plane_format = plane ? DXGI_FORMAT_R8_TYPELESS : DXGI_FORMAT_R32_TYPELESS;
        }
        width = (width + format.block_width - 1) / format.block_width * format.block_width;
        height = (height + format.block_height - 1) / format.block_height * format.block_height;
        UINT blocks_high = height / format.block_height;
        UINT64 row_size = width / format.block_width * block_bytes;
        UINT row_pitch = (static_cast<UINT>(row_size) + pitch_alignment - 1) & ~(pitch_alignment - 1);
        UINT64 size = static_cast<UINT64>(row_pitch) * (blocks_high - 1) + row_size;
        size += static_cast<UINT64>(depth - 1) * ((size + pitch_alignment - 1) & ~static_cast<UINT64>(pitch_alignment - 1));
        if (layouts)
        {
            layouts[i].Offset = base_offset + offset;
            layouts[i].Footprint.Format = plane_format;
            layouts[i].Footprint.Width = static_cast<UINT>(width);
            layouts[i].Footprint.Height = height;
            layouts[i].Footprint.Depth = depth;
            layouts[i].Footprint.RowPitch = row_pitch;
        }
        if (rows) rows[i] = blocks_high;
        if (row_sizes) row_sizes[i] = row_size;
        total_bytes = offset + size;
        offset = (total_bytes + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1)
                & ~static_cast<UINT64>(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
    }
    if (total) *total = total_bytes;
}

HRESULT Native12DescriptorHeap::Initialize(const D3D12_DESCRIPTOR_HEAP_DESC *input)
{
    desc = *input;
    D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 args = {};
    args.Type = static_cast<D3D12DDI_DESCRIPTOR_HEAP_TYPE>(desc.Type);
    args.NumDescriptors = desc.NumDescriptors;
    UINT flags = D3D12DDI_DESCRIPTOR_HEAP_FLAG_CPU_VISIBLE;
    if (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) flags |= D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    args.Flags = static_cast<D3D12DDI_DESCRIPTOR_HEAP_FLAGS>(flags);
    args.NodeMask = desc.NodeMask ? desc.NodeMask : 1;
    SIZE_T size = device->functions.pfnCalcPrivateDescriptorHeapSize(device->driver_device, &args);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!driver.pDrvPrivate) return E_OUTOFMEMORY;
    HRESULT hr = device->functions.pfnCreateDescriptorHeap(device->driver_device, &args, driver);
    if (FAILED(hr))
    {
        WARN("The driver failed to create a descriptor heap of type %u with %u entries, hr %#lx.\n",
                desc.Type, desc.NumDescriptors, hr);
        return hr;
    }
    driver_created = true;
    cpu_start.ptr = device->functions.pfnGetCPUDescriptorHandleForHeapStart(device->driver_device, driver).ptr;
    if (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        gpu_start.ptr = device->functions.pfnGetGPUDescriptorHandleForHeapStart(device->driver_device, driver).ptr;
    if (desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV && desc.NumDescriptors)
    {
        increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        dsv_formats = static_cast<DXGI_FORMAT *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                desc.NumDescriptors * sizeof(*dsv_formats)));
        if (!dsv_formats) return E_OUTOFMEMORY;
        AcquireSRWLockExclusive(&device->dsv_lock);
        next_dsv = device->dsv_heaps;
        device->dsv_heaps = this;
        ReleaseSRWLockExclusive(&device->dsv_lock);
    }
    return S_OK;
}

void Native12Device::SetDsvFormat(SIZE_T descriptor, DXGI_FORMAT format)
{
    AcquireSRWLockShared(&dsv_lock);
    for (Native12DescriptorHeap *heap = dsv_heaps; heap; heap = heap->next_dsv)
    {
        SIZE_T offset = descriptor - heap->cpu_start.ptr;
        if (descriptor >= heap->cpu_start.ptr && heap->increment && offset / heap->increment < heap->desc.NumDescriptors)
        {
            heap->dsv_formats[offset / heap->increment] = format;
            break;
        }
    }
    ReleaseSRWLockShared(&dsv_lock);
}

DXGI_FORMAT Native12Device::GetDsvFormat(SIZE_T descriptor)
{
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    AcquireSRWLockShared(&dsv_lock);
    for (Native12DescriptorHeap *heap = dsv_heaps; heap; heap = heap->next_dsv)
    {
        SIZE_T offset = descriptor - heap->cpu_start.ptr;
        if (descriptor >= heap->cpu_start.ptr && heap->increment && offset / heap->increment < heap->desc.NumDescriptors)
        {
            format = heap->dsv_formats[offset / heap->increment];
            break;
        }
    }
    ReleaseSRWLockShared(&dsv_lock);
    return format;
}

Native12DescriptorHeap::~Native12DescriptorHeap()
{
    if (dsv_formats)
    {
        AcquireSRWLockExclusive(&device->dsv_lock);
        for (Native12DescriptorHeap **link = &device->dsv_heaps; *link; link = &(*link)->next_dsv)
        {
            if (*link == this)
            {
                *link = next_dsv;
                break;
            }
        }
        ReleaseSRWLockExclusive(&device->dsv_lock);
        HeapFree(GetProcessHeap(), 0, dsv_formats);
    }
    if (driver_created) device->functions.pfnDestroyDescriptorHeap(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
}

HRESULT STDMETHODCALLTYPE Native12DescriptorHeap::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12DescriptorHeap) || IsEqualGUID(iid, IID_ID3D12Pageable)
            || IsEqualGUID(iid, IID_ID3D12DeviceChild) || IsEqualGUID(iid, IID_ID3D12Object)
            || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12DescriptorHeap *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

D3D12_DESCRIPTOR_HEAP_DESC *STDMETHODCALLTYPE Native12DescriptorHeap::GetDesc(D3D12_DESCRIPTOR_HEAP_DESC *out)
{
    *out = desc;
    return out;
}

D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE Native12DescriptorHeap::GetCPUDescriptorHandleForHeapStart(
        D3D12_CPU_DESCRIPTOR_HANDLE *out)
{
    *out = cpu_start;
    return out;
}

D3D12_GPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE Native12DescriptorHeap::GetGPUDescriptorHandleForHeapStart(
        D3D12_GPU_DESCRIPTOR_HANDLE *out)
{
    *out = gpu_start;
    return out;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID iid,
        void **out)
{
    if (!desc || !out) return E_INVALIDARG;
    *out = NULL;
    if ((desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
            && (desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || desc->Type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV))
        return E_INVALIDARG;
    Native12DescriptorHeap *heap = new Native12DescriptorHeap(this);
    if (!heap) return E_OUTOFMEMORY;
    HRESULT hr = heap->Initialize(desc);
    if (SUCCEEDED(hr)) hr = heap->QueryInterface(iid, out);
    heap->Release();
    return hr;
}

UINT STDMETHODCALLTYPE Native12Device::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    return functions.pfnGetDescriptorSizeInBytes(driver_device, static_cast<D3D12DDI_DESCRIPTOR_HEAP_TYPE>(type));
}

static D3D12DDI_CPU_DESCRIPTOR_HANDLE Native12Descriptor(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    D3D12DDI_CPU_DESCRIPTOR_HANDLE result;
    result.ptr = handle.ptr;
    return result;
}

void STDMETHODCALLTYPE Native12Device::CreateRenderTargetView(ID3D12Resource *input,
        const D3D12_RENDER_TARGET_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    Native12Resource *resource = Native12UnwrapResource(input);
    D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002 args = {};

    if (resource) args.hDrvResource = resource->driver;
    args.Format = desc && desc->Format ? desc->Format : resource ? resource->desc.Format : DXGI_FORMAT_UNKNOWN;
    D3D12_RTV_DIMENSION dimension = desc ? desc->ViewDimension : D3D12_RTV_DIMENSION_UNKNOWN;
    if (!desc && resource)
    {
        switch (resource->desc.Dimension)
        {
            case D3D12_RESOURCE_DIMENSION_BUFFER: dimension = D3D12_RTV_DIMENSION_BUFFER; break;
            case D3D12_RESOURCE_DIMENSION_TEXTURE1D: dimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY; break;
            case D3D12_RESOURCE_DIMENSION_TEXTURE3D: dimension = D3D12_RTV_DIMENSION_TEXTURE3D; break;
            default: dimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY; break;
        }
    }
    UINT layers = resource ? resource->desc.DepthOrArraySize : 1;
    switch (dimension)
    {
        case D3D12_RTV_DIMENSION_BUFFER:
            args.ResourceDimension = D3D12DDI_RD_BUFFER;
            if (desc)
            {
                args.Buffer.FirstElement = desc->Buffer.FirstElement;
                args.Buffer.NumElements = desc->Buffer.NumElements;
            }
            break;
        case D3D12_RTV_DIMENSION_TEXTURE1D:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            args.Tex1D.MipSlice = desc->Texture1D.MipSlice;
            args.Tex1D.ArraySize = 1;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            args.Tex1D.MipSlice = desc ? desc->Texture1DArray.MipSlice : 0;
            args.Tex1D.FirstArraySlice = desc ? desc->Texture1DArray.FirstArraySlice : 0;
            args.Tex1D.ArraySize = desc ? desc->Texture1DArray.ArraySize : layers;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2D:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.MipSlice = desc->Texture2D.MipSlice;
            args.Tex2D.ArraySize = 1;
            args.Tex2D.PlaneSlice = desc->Texture2D.PlaneSlice;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.MipSlice = desc ? desc->Texture2DArray.MipSlice : 0;
            args.Tex2D.FirstArraySlice = desc ? desc->Texture2DArray.FirstArraySlice : 0;
            args.Tex2D.ArraySize = desc ? desc->Texture2DArray.ArraySize : layers;
            args.Tex2D.PlaneSlice = desc ? desc->Texture2DArray.PlaneSlice : 0;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2DMS:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.ArraySize = 1;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.FirstArraySlice = desc->Texture2DMSArray.FirstArraySlice;
            args.Tex2D.ArraySize = desc->Texture2DMSArray.ArraySize;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE3D:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE3D;
            args.Tex3D.MipSlice = desc ? desc->Texture3D.MipSlice : 0;
            args.Tex3D.FirstW = desc ? desc->Texture3D.FirstWSlice : 0;
            args.Tex3D.WSize = desc ? desc->Texture3D.WSize : ~0u;
            break;
        default:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            break;
    }
    if (args.ResourceDimension == D3D12DDI_RD_TEXTURE1D && args.Tex1D.ArraySize == UINT_MAX)
        args.Tex1D.ArraySize = layers > args.Tex1D.FirstArraySlice ? layers - args.Tex1D.FirstArraySlice : 0;
    if (args.ResourceDimension == D3D12DDI_RD_TEXTURE2D && args.Tex2D.ArraySize == UINT_MAX)
        args.Tex2D.ArraySize = layers > args.Tex2D.FirstArraySlice ? layers - args.Tex2D.FirstArraySlice : 0;
    if (args.ResourceDimension == D3D12DDI_RD_TEXTURE3D && args.Tex3D.WSize == UINT_MAX)
    {
        UINT depth = max(1u, layers >> args.Tex3D.MipSlice);
        args.Tex3D.WSize = depth > args.Tex3D.FirstW ? depth - args.Tex3D.FirstW : 0;
    }
    functions.pfnCreateRenderTargetView(driver_device, &args, Native12Descriptor(descriptor));
}

void STDMETHODCALLTYPE Native12Device::CreateDepthStencilView(ID3D12Resource *input,
        const D3D12_DEPTH_STENCIL_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    Native12Resource *resource = Native12UnwrapResource(input);
    D3D12DDIARG_CREATE_DEPTH_STENCIL_VIEW args = {};

    if (resource) args.hDrvResource = resource->driver;
    args.Format = desc && desc->Format ? desc->Format : resource ? resource->desc.Format : DXGI_FORMAT_UNKNOWN;
    args.Flags = static_cast<D3D12DDI_CREATE_DEPTH_STENCIL_VIEW_FLAGS>(desc ? desc->Flags : 0);
    UINT layers = resource ? resource->desc.DepthOrArraySize : 1;
    D3D12_DSV_DIMENSION dimension = desc ? desc->ViewDimension : D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    switch (dimension)
    {
        case D3D12_DSV_DIMENSION_TEXTURE1D:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            args.Tex1D.MipSlice = desc->Texture1D.MipSlice;
            args.Tex1D.ArraySize = 1;
            break;
        case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            args.Tex1D.MipSlice = desc->Texture1DArray.MipSlice;
            args.Tex1D.FirstArraySlice = desc->Texture1DArray.FirstArraySlice;
            args.Tex1D.ArraySize = desc->Texture1DArray.ArraySize;
            break;
        case D3D12_DSV_DIMENSION_TEXTURE2D:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.MipSlice = desc->Texture2D.MipSlice;
            args.Tex2D.ArraySize = 1;
            break;
        case D3D12_DSV_DIMENSION_TEXTURE2DMS:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.ArraySize = 1;
            break;
        case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.FirstArraySlice = desc->Texture2DMSArray.FirstArraySlice;
            args.Tex2D.ArraySize = desc->Texture2DMSArray.ArraySize;
            break;
        default:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.MipSlice = desc ? desc->Texture2DArray.MipSlice : 0;
            args.Tex2D.FirstArraySlice = desc ? desc->Texture2DArray.FirstArraySlice : 0;
            args.Tex2D.ArraySize = desc ? desc->Texture2DArray.ArraySize : layers;
            break;
    }
    if (args.ResourceDimension == D3D12DDI_RD_TEXTURE1D && args.Tex1D.ArraySize == UINT_MAX)
        args.Tex1D.ArraySize = layers > args.Tex1D.FirstArraySlice ? layers - args.Tex1D.FirstArraySlice : 0;
    if (args.ResourceDimension == D3D12DDI_RD_TEXTURE2D && args.Tex2D.ArraySize == UINT_MAX)
        args.Tex2D.ArraySize = layers > args.Tex2D.FirstArraySlice ? layers - args.Tex2D.FirstArraySlice : 0;
    SetDsvFormat(descriptor.ptr, args.Format);
    functions.pfnCreateDepthStencilView(driver_device, &args, Native12Descriptor(descriptor));
}

void STDMETHODCALLTYPE Native12Device::CreateShaderResourceView(ID3D12Resource *input,
        const D3D12_SHADER_RESOURCE_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    Native12Resource *resource = Native12UnwrapResource(input);
    D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 args = {};

    if (resource) args.hDrvResource = resource->driver;
    args.Format = desc && desc->Format ? desc->Format : resource ? resource->desc.Format : DXGI_FORMAT_UNKNOWN;
    args.Shader4ComponentMapping = desc ? desc->Shader4ComponentMapping : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    D3D12_SRV_DIMENSION dimension = desc ? desc->ViewDimension : D3D12_SRV_DIMENSION_UNKNOWN;
    if (!desc && resource)
    {
        switch (resource->desc.Dimension)
        {
            case D3D12_RESOURCE_DIMENSION_BUFFER: dimension = D3D12_SRV_DIMENSION_BUFFER; break;
            case D3D12_RESOURCE_DIMENSION_TEXTURE1D: dimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY; break;
            case D3D12_RESOURCE_DIMENSION_TEXTURE3D: dimension = D3D12_SRV_DIMENSION_TEXTURE3D; break;
            default: dimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; break;
        }
    }
    UINT layers = resource ? resource->desc.DepthOrArraySize : 1;
    switch (dimension)
    {
        case D3D12_SRV_DIMENSION_BUFFER:
            args.ResourceDimension = D3D12DDI_RD_BUFFER;
            if (desc)
            {
                args.Buffer.FirstElement = desc->Buffer.FirstElement;
                args.Buffer.NumElements = desc->Buffer.NumElements;
                args.Buffer.StructureByteStride = desc->Buffer.StructureByteStride;
                args.Buffer.Flags = static_cast<D3D12DDI_BUFFER_SRV_FLAGS>(desc->Buffer.Flags);
            }
            break;
        case D3D12_SRV_DIMENSION_TEXTURE1D:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            args.Tex1D.MostDetailedMip = desc->Texture1D.MostDetailedMip;
            args.Tex1D.MipLevels = desc->Texture1D.MipLevels;
            args.Tex1D.ArraySize = 1;
            args.Tex1D.ResourceMinLODClamp = desc->Texture1D.ResourceMinLODClamp;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            args.Tex1D.MostDetailedMip = desc ? desc->Texture1DArray.MostDetailedMip : 0;
            args.Tex1D.MipLevels = desc ? desc->Texture1DArray.MipLevels : ~0u;
            args.Tex1D.FirstArraySlice = desc ? desc->Texture1DArray.FirstArraySlice : 0;
            args.Tex1D.ArraySize = desc ? desc->Texture1DArray.ArraySize : layers;
            args.Tex1D.ResourceMinLODClamp = desc ? desc->Texture1DArray.ResourceMinLODClamp : 0.0f;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2D:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.MostDetailedMip = desc->Texture2D.MostDetailedMip;
            args.Tex2D.MipLevels = desc->Texture2D.MipLevels;
            args.Tex2D.ArraySize = 1;
            args.Tex2D.PlaneSlice = desc->Texture2D.PlaneSlice;
            args.Tex2D.ResourceMinLODClamp = desc->Texture2D.ResourceMinLODClamp;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.MostDetailedMip = desc ? desc->Texture2DArray.MostDetailedMip : 0;
            args.Tex2D.MipLevels = desc ? desc->Texture2DArray.MipLevels : ~0u;
            args.Tex2D.FirstArraySlice = desc ? desc->Texture2DArray.FirstArraySlice : 0;
            args.Tex2D.ArraySize = desc ? desc->Texture2DArray.ArraySize : layers;
            args.Tex2D.PlaneSlice = desc ? desc->Texture2DArray.PlaneSlice : 0;
            args.Tex2D.ResourceMinLODClamp = desc ? desc->Texture2DArray.ResourceMinLODClamp : 0.0f;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DMS:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.MipLevels = 1;
            args.Tex2D.ArraySize = 1;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.MipLevels = 1;
            args.Tex2D.FirstArraySlice = desc->Texture2DMSArray.FirstArraySlice;
            args.Tex2D.ArraySize = desc->Texture2DMSArray.ArraySize;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE3D:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE3D;
            args.Tex3D.MostDetailedMip = desc ? desc->Texture3D.MostDetailedMip : 0;
            args.Tex3D.MipLevels = desc ? desc->Texture3D.MipLevels : ~0u;
            args.Tex3D.ResourceMinLODClamp = desc ? desc->Texture3D.ResourceMinLODClamp : 0.0f;
            break;
        case D3D12_SRV_DIMENSION_TEXTURECUBE:
            args.ResourceDimension = D3D12DDI_RD_TEXTURECUBE;
            args.TexCube.MostDetailedMip = desc->TextureCube.MostDetailedMip;
            args.TexCube.MipLevels = desc->TextureCube.MipLevels;
            args.TexCube.NumCubes = 1;
            args.TexCube.ResourceMinLODClamp = desc->TextureCube.ResourceMinLODClamp;
            break;
        case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
            args.ResourceDimension = D3D12DDI_RD_TEXTURECUBE;
            args.TexCube.MostDetailedMip = desc->TextureCubeArray.MostDetailedMip;
            args.TexCube.MipLevels = desc->TextureCubeArray.MipLevels;
            args.TexCube.First2DArrayFace = desc->TextureCubeArray.First2DArrayFace;
            args.TexCube.NumCubes = desc->TextureCubeArray.NumCubes;
            args.TexCube.ResourceMinLODClamp = desc->TextureCubeArray.ResourceMinLODClamp;
            break;
        default:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            break;
    }
    if (resource)
    {
        UINT mips = Native12MipLevels(&resource->desc);
        switch (args.ResourceDimension)
        {
            case D3D12DDI_RD_TEXTURE1D:
                if (args.Tex1D.MipLevels == UINT_MAX)
                    args.Tex1D.MipLevels = mips > args.Tex1D.MostDetailedMip ? mips - args.Tex1D.MostDetailedMip : 0;
                if (args.Tex1D.ArraySize == UINT_MAX)
                    args.Tex1D.ArraySize = layers > args.Tex1D.FirstArraySlice ? layers - args.Tex1D.FirstArraySlice : 0;
                break;
            case D3D12DDI_RD_TEXTURE2D:
                if (args.Tex2D.MipLevels == UINT_MAX)
                    args.Tex2D.MipLevels = mips > args.Tex2D.MostDetailedMip ? mips - args.Tex2D.MostDetailedMip : 0;
                if (args.Tex2D.ArraySize == UINT_MAX)
                    args.Tex2D.ArraySize = layers > args.Tex2D.FirstArraySlice ? layers - args.Tex2D.FirstArraySlice : 0;
                break;
            case D3D12DDI_RD_TEXTURE3D:
                if (args.Tex3D.MipLevels == UINT_MAX)
                    args.Tex3D.MipLevels = mips > args.Tex3D.MostDetailedMip ? mips - args.Tex3D.MostDetailedMip : 0;
                break;
            case D3D12DDI_RD_TEXTURECUBE:
                if (args.TexCube.MipLevels == UINT_MAX)
                    args.TexCube.MipLevels = mips > args.TexCube.MostDetailedMip
                            ? mips - args.TexCube.MostDetailedMip : 0;
                if (args.TexCube.NumCubes == UINT_MAX)
                    args.TexCube.NumCubes = layers > args.TexCube.First2DArrayFace
                            ? (layers - args.TexCube.First2DArrayFace) / 6 : 0;
                break;
            default:
                break;
        }
    }
    functions.pfnCreateShaderResourceView(driver_device, &args, Native12Descriptor(descriptor));
}

void STDMETHODCALLTYPE Native12Device::CreateUnorderedAccessView(ID3D12Resource *input,
        ID3D12Resource *counter_input, const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    Native12Resource *resource = Native12UnwrapResource(input);
    Native12Resource *counter = Native12UnwrapResource(counter_input);
    D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 args = {};

    if (resource) args.hDrvResource = resource->driver;
    args.Format = desc && desc->Format ? desc->Format : resource ? resource->desc.Format : DXGI_FORMAT_UNKNOWN;
    D3D12_UAV_DIMENSION dimension = desc ? desc->ViewDimension : D3D12_UAV_DIMENSION_UNKNOWN;
    if (!desc && resource)
    {
        switch (resource->desc.Dimension)
        {
            case D3D12_RESOURCE_DIMENSION_BUFFER: dimension = D3D12_UAV_DIMENSION_BUFFER; break;
            case D3D12_RESOURCE_DIMENSION_TEXTURE1D: dimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY; break;
            case D3D12_RESOURCE_DIMENSION_TEXTURE3D: dimension = D3D12_UAV_DIMENSION_TEXTURE3D; break;
            default: dimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; break;
        }
    }
    UINT layers = resource ? resource->desc.DepthOrArraySize : 1;
    switch (dimension)
    {
        case D3D12_UAV_DIMENSION_BUFFER:
            args.ResourceDimension = D3D12DDI_RD_BUFFER;
            if (counter) args.Buffer.hDrvCounterResource = counter->driver;
            if (desc)
            {
                args.Buffer.FirstElement = desc->Buffer.FirstElement;
                args.Buffer.NumElements = desc->Buffer.NumElements;
                args.Buffer.StructureByteStride = desc->Buffer.StructureByteStride;
                args.Buffer.CounterOffsetInBytes = desc->Buffer.CounterOffsetInBytes;
                args.Buffer.Flags = static_cast<D3D12DDI_BUFFER_UAV_FLAGS>(desc->Buffer.Flags);
            }
            break;
        case D3D12_UAV_DIMENSION_TEXTURE1D:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            args.Tex1D.MipSlice = desc->Texture1D.MipSlice;
            args.Tex1D.ArraySize = 1;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            args.Tex1D.MipSlice = desc ? desc->Texture1DArray.MipSlice : 0;
            args.Tex1D.FirstArraySlice = desc ? desc->Texture1DArray.FirstArraySlice : 0;
            args.Tex1D.ArraySize = desc ? desc->Texture1DArray.ArraySize : layers;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2D:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.MipSlice = desc->Texture2D.MipSlice;
            args.Tex2D.ArraySize = 1;
            args.Tex2D.PlaneSlice = desc->Texture2D.PlaneSlice;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2DMS:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.ArraySize = 1;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.FirstArraySlice = desc->Texture2DMSArray.FirstArraySlice;
            args.Tex2D.ArraySize = desc->Texture2DMSArray.ArraySize;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE3D:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE3D;
            args.Tex3D.MipSlice = desc ? desc->Texture3D.MipSlice : 0;
            args.Tex3D.FirstW = desc ? desc->Texture3D.FirstWSlice : 0;
            args.Tex3D.WSize = desc ? desc->Texture3D.WSize : ~0u;
            break;
        default:
            args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            args.Tex2D.MipSlice = desc ? desc->Texture2DArray.MipSlice : 0;
            args.Tex2D.FirstArraySlice = desc ? desc->Texture2DArray.FirstArraySlice : 0;
            args.Tex2D.ArraySize = desc ? desc->Texture2DArray.ArraySize : layers;
            args.Tex2D.PlaneSlice = desc ? desc->Texture2DArray.PlaneSlice : 0;
            break;
    }
    if (args.ResourceDimension == D3D12DDI_RD_TEXTURE1D && args.Tex1D.ArraySize == UINT_MAX)
        args.Tex1D.ArraySize = layers > args.Tex1D.FirstArraySlice ? layers - args.Tex1D.FirstArraySlice : 0;
    if (args.ResourceDimension == D3D12DDI_RD_TEXTURE2D && args.Tex2D.ArraySize == UINT_MAX)
        args.Tex2D.ArraySize = layers > args.Tex2D.FirstArraySlice ? layers - args.Tex2D.FirstArraySlice : 0;
    if (args.ResourceDimension == D3D12DDI_RD_TEXTURE3D && args.Tex3D.WSize == UINT_MAX)
    {
        UINT depth = max(1u, layers >> args.Tex3D.MipSlice);
        args.Tex3D.WSize = depth > args.Tex3D.FirstW ? depth - args.Tex3D.FirstW : 0;
    }
    functions.pfnCreateUnorderedAccessView(driver_device, &args, Native12Descriptor(descriptor));
}

void STDMETHODCALLTYPE Native12Device::CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    D3D12DDI_CONSTANT_BUFFER_VIEW_DESC args = {};
    if (desc)
    {
        args.BufferLocation = desc->BufferLocation;
        args.SizeInBytes = desc->SizeInBytes;
    }
    functions.pfnCreateConstantBufferView(driver_device, &args, Native12Descriptor(descriptor));
}

void STDMETHODCALLTYPE Native12Device::CreateSampler(const D3D12_SAMPLER_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    D3D12DDI_SAMPLER_DESC_0096 sampler = {};
    D3D12DDIARG_CREATE_SAMPLER_0096 args = {};

    if (!desc) return;
    sampler.Filter = static_cast<D3D12DDI_FILTER>(desc->Filter);
    sampler.AddressU = static_cast<D3D12DDI_TEXTURE_ADDRESS_MODE>(desc->AddressU);
    sampler.AddressV = static_cast<D3D12DDI_TEXTURE_ADDRESS_MODE>(desc->AddressV);
    sampler.AddressW = static_cast<D3D12DDI_TEXTURE_ADDRESS_MODE>(desc->AddressW);
    sampler.MipLODBias = desc->MipLODBias;
    sampler.MaxAnisotropy = desc->MaxAnisotropy;
    sampler.ComparisonFunc = static_cast<D3D12DDI_COMPARISON_FUNC>(desc->ComparisonFunc);
    memcpy(sampler.FloatBorderColor, desc->BorderColor, sizeof(sampler.FloatBorderColor));
    sampler.MinLOD = desc->MinLOD;
    sampler.MaxLOD = desc->MaxLOD;
    args.pSamplerDesc = &sampler;
    functions.pfnCreateSampler(driver_device, &args, Native12Descriptor(descriptor));
}

void STDMETHODCALLTYPE Native12Device::CreateSampler2(const D3D12_SAMPLER_DESC2 *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    D3D12DDI_SAMPLER_DESC_0096 sampler = {};
    D3D12DDIARG_CREATE_SAMPLER_0096 args = {};

    if (!desc) return;
    sampler.Filter = static_cast<D3D12DDI_FILTER>(desc->Filter);
    sampler.AddressU = static_cast<D3D12DDI_TEXTURE_ADDRESS_MODE>(desc->AddressU);
    sampler.AddressV = static_cast<D3D12DDI_TEXTURE_ADDRESS_MODE>(desc->AddressV);
    sampler.AddressW = static_cast<D3D12DDI_TEXTURE_ADDRESS_MODE>(desc->AddressW);
    sampler.MipLODBias = desc->MipLODBias;
    sampler.MaxAnisotropy = desc->MaxAnisotropy;
    sampler.ComparisonFunc = static_cast<D3D12DDI_COMPARISON_FUNC>(desc->ComparisonFunc);
    memcpy(sampler.UintBorderColor, desc->UintBorderColor, sizeof(sampler.UintBorderColor));
    sampler.MinLOD = desc->MinLOD;
    sampler.MaxLOD = desc->MaxLOD;
    sampler.Flags = static_cast<D3D12DDI_SAMPLER_FLAGS_0096>(desc->Flags);
    args.pSamplerDesc = &sampler;
    functions.pfnCreateSampler(driver_device, &args, Native12Descriptor(descriptor));
}

void STDMETHODCALLTYPE Native12Device::CopyDescriptors(UINT dst_count,
        const D3D12_CPU_DESCRIPTOR_HANDLE *dst_starts, const UINT *dst_sizes, UINT src_count,
        const D3D12_CPU_DESCRIPTOR_HANDLE *src_starts, const UINT *src_sizes, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    static_assert(sizeof(D3D12_CPU_DESCRIPTOR_HANDLE) == sizeof(D3D12DDI_CPU_DESCRIPTOR_HANDLE), "descriptor");
    functions.pfnCopyDescriptors(driver_device, dst_count,
            reinterpret_cast<const D3D12DDI_CPU_DESCRIPTOR_HANDLE *>(dst_starts), dst_sizes, src_count,
            reinterpret_cast<const D3D12DDI_CPU_DESCRIPTOR_HANDLE *>(src_starts), src_sizes,
            static_cast<D3D12DDI_DESCRIPTOR_HEAP_TYPE>(type));
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV || !dst_starts || !src_starts) return;
    UINT increment = GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    UINT src_index = 0, src_offset = 0;
    for (UINT dst_index = 0; dst_index < dst_count && src_index < src_count; ++dst_index)
    {
        UINT dst_size = dst_sizes ? dst_sizes[dst_index] : 1;
        for (UINT i = 0; i < dst_size && src_index < src_count; ++i)
        {
            SetDsvFormat(dst_starts[dst_index].ptr + i * increment,
                    GetDsvFormat(src_starts[src_index].ptr + src_offset * increment));
            if (++src_offset == (src_sizes ? src_sizes[src_index] : 1))
            {
                src_offset = 0;
                ++src_index;
            }
        }
    }
}

void STDMETHODCALLTYPE Native12Device::CopyDescriptorsSimple(UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE dst,
        const D3D12_CPU_DESCRIPTOR_HANDLE src, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    functions.pfnCopyDescriptorsSimple(driver_device, count, Native12Descriptor(dst), Native12Descriptor(src),
            static_cast<D3D12DDI_DESCRIPTOR_HEAP_TYPE>(type));
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV) return;
    UINT increment = GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    for (UINT i = 0; i < count; ++i)
        SetDsvFormat(dst.ptr + i * increment, GetDsvFormat(src.ptr + i * increment));
}
