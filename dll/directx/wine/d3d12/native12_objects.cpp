/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 12 command lists on a user-mode display driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "native12.h"
#include "native12_device.h"
#include "native12_objects.h"
#include <winternl.h>

WINE_DEFAULT_DEBUG_CHANNEL(d3d12);

static_assert(sizeof(D3D12_VIEWPORT) == sizeof(D3D12DDI_VIEWPORT), "viewport layout");
static_assert(sizeof(D3D12_VERTEX_BUFFER_VIEW) == sizeof(D3D12DDI_VERTEX_BUFFER_VIEW), "vertex buffer view layout");
static_assert(sizeof(D3D12_INDEX_BUFFER_VIEW) == sizeof(D3D12DDI_INDEX_BUFFER_VIEW), "index buffer view layout");
static_assert(sizeof(D3D12_STREAM_OUTPUT_BUFFER_VIEW) == sizeof(D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW),
        "stream output view layout");
static_assert(sizeof(D3D12_BOX) == sizeof(D3D12DDI_BOX), "box layout");

void Native12SetCommandListErrorObject(HANDLE handle, HRESULT hr)
{
    Native12CommandList *list = static_cast<Native12CommandList *>(handle);
    if (!list || Native12VideoListError(handle, hr)) return;
    WARN("Driver reported error %#lx on command list %p.\n", hr, list);
    if (SUCCEEDED(list->error)) list->error = hr;
}

void Native12SetCommandListTableObject(HANDLE handle, HANDLE table)
{
    Native12CommandList *list = static_cast<Native12CommandList *>(handle);
    const NATIVE12_LIST_FUNCS *functions;

    if (!list || Native12VideoListError(handle, S_OK)) return;
    if (!(functions = list->device->FindListTable(table)))
    {
        WARN("Ignoring unknown command list table %p.\n", table);
        return;
    }
    list->table = functions;
}

static D3D12DDI_CPU_DESCRIPTOR_HANDLE Native12CpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    D3D12DDI_CPU_DESCRIPTOR_HANDLE result;
    result.ptr = handle.ptr;
    return result;
}

static D3D12DDI_GPU_DESCRIPTOR_HANDLE Native12GpuHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle)
{
    D3D12DDI_GPU_DESCRIPTOR_HANDLE result;
    result.ptr = handle.ptr;
    return result;
}

static D3D12DDIARG_BUFFER_PLACEMENT Native12Placement(ID3D12Resource *resource, UINT64 offset)
{
    D3D12DDIARG_BUFFER_PLACEMENT placement = {};
    if (resource) placement.BaseAddress.UMD.hResource = Native12UnwrapResource(resource)->driver;
    placement.BaseAddress.UMD.Offset = offset;
    return placement;
}

HRESULT Native12CommandList::Initialize(D3D12_COMMAND_LIST_TYPE list_type, Native12CommandAllocator *pool,
        ID3D12PipelineState *initial_state)
{
    type = list_type;
    id = static_cast<UINT64>(InterlockedIncrement64(&device->list_sequence));
    table = &device->list_functions;

    D3D12DDIARG_CREATE_COMMAND_LIST_0040 args = {};
    args.Type = type == D3D12_COMMAND_LIST_TYPE_BUNDLE ? D3D12DDI_COMMAND_LIST_TYPE_BUNDLE
            : D3D12DDI_COMMAND_LIST_TYPE_DIRECT;
    args.QueueFlags = Native12QueueFlags(type);
    args.ID = id;
    args.NodeMask = 1;
    SIZE_T size = device->functions.pfnCalcPrivateCommandListSize(device->driver_device, &args);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!driver.pDrvPrivate) return E_OUTOFMEMORY;
    D3D12DDI_HRTCOMMANDLIST runtime = {};
    runtime.handle = this;
    HRESULT hr = device->functions.pfnCreateCommandList(device->driver_device, &args, driver, runtime);
    if (FAILED(hr))
    {
        WARN("The driver failed to create a command list of type %u, hr %#lx.\n", type, hr);
        return hr;
    }
    driver_created = true;

    D3D12DDIARG_CREATE_COMMAND_RECORDER_0040 recorder_args = {};
    recorder_args.QueueFlags = args.QueueFlags;
    size = device->functions.pfnCalcPrivateCommandRecorderSize(device->driver_device, &recorder_args);
    recorder.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!recorder.pDrvPrivate) return E_OUTOFMEMORY;
    if (FAILED(hr = device->functions.pfnCreateCommandRecorder(device->driver_device, &recorder_args, recorder)))
    {
        WARN("The driver failed to create a command recorder, hr %#lx.\n", hr);
        return hr;
    }
    recorder_created = true;
    return pool ? Reset(pool, initial_state) : S_OK;
}

Native12CommandList::~Native12CommandList()
{
    if (recording && allocator)
        InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&allocator->recording_list), NULL, this);
    if (driver_created) device->functions.pfnDestroyCommandList(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
    if (recorder_created) device->functions.pfnDestroyCommandRecorder(device->driver_device, recorder);
    if (recorder.pDrvPrivate) HeapFree(GetProcessHeap(), 0, recorder.pDrvPrivate);
    if (allocator) allocator->ReleaseInternal();
    ClearPassResolves();
    for (UINT i = 0; i < ARRAYSIZE(default_states); ++i)
    {
        if (!default_states[i]) continue;
        device->AddRef();
        device->AddRef();
        default_states[i]->Release();
    }
}

bool Native12CommandList::RejectInBundle()
{
    if (type != D3D12_COMMAND_LIST_TYPE_BUNDLE) return false;
    if (SUCCEEDED(error)) error = E_FAIL;
    return true;
}

void Native12CommandList::SetDefaultPipelineState()
{
    if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_BUNDLE
            && type != D3D12_COMMAND_LIST_TYPE_COMPUTE)
        return;
    for (UINT i = type == D3D12_COMMAND_LIST_TYPE_COMPUTE ? 1 : 0; i < ARRAYSIZE(default_states); ++i)
    {
        if (!default_states[i])
        {
            if (FAILED(Native12CreateDefaultPipelineState(device, i == 1, &default_states[i]))) continue;
            device->Release();
            device->Release();
        }
        table->pfnSetPipelineState(driver, static_cast<Native12PipelineState *>(default_states[i])->driver);
    }
}

HRESULT STDMETHODCALLTYPE Native12CommandList::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12GraphicsCommandList) || IsEqualGUID(iid, IID_ID3D12GraphicsCommandList1)
            || IsEqualGUID(iid, IID_ID3D12GraphicsCommandList2) || IsEqualGUID(iid, IID_ID3D12GraphicsCommandList3)
            || IsEqualGUID(iid, IID_ID3D12GraphicsCommandList4) || IsEqualGUID(iid, IID_ID3D12GraphicsCommandList5)
            || IsEqualGUID(iid, IID_ID3D12GraphicsCommandList6) || IsEqualGUID(iid, IID_ID3D12GraphicsCommandList7)
            || IsEqualGUID(iid, IID_ID3D12GraphicsCommandList8) || IsEqualGUID(iid, IID_ID3D12GraphicsCommandList9)
            || IsEqualGUID(iid, IID_ID3D12GraphicsCommandList10) || IsEqualGUID(iid, IID_ID3D12CommandList)
            || IsEqualGUID(iid, IID_ID3D12DeviceChild) || IsEqualGUID(iid, IID_ID3D12Object)
            || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12GraphicsCommandList10 *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE Native12CommandList::GetType() { return type; }

HRESULT STDMETHODCALLTYPE Native12CommandList::Close()
{
    if (!recording) return E_FAIL;
    table->pfnCloseCommandList(driver);
    recording = false;
    if (allocator) InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&allocator->recording_list),
            NULL, this);
    return error;
}

HRESULT STDMETHODCALLTYPE Native12CommandList::Reset(ID3D12CommandAllocator *pool,
        ID3D12PipelineState *initial_state)
{
    if (recording || !pool) return E_FAIL;
    Native12CommandAllocator *target = static_cast<Native12CommandAllocator *>(pool);
    if (target->type != type) return E_INVALIDARG;
    if (InterlockedCompareExchangePointer(reinterpret_cast<void *volatile *>(&target->recording_list), this, NULL))
        return E_INVALIDARG;
    Native12CommandAllocator *previous = allocator;
    allocator = target;
    allocator->AddInternal();
    error = S_OK;
    device->functions.pfnCommandRecorderSetCommandPoolAsTarget(device->driver_device, recorder, target->driver);
    D3D12DDIARG_RESETCOMMANDLIST_0040 args = {};
    args.hDrvCommandRecorder = recorder;
    args.ID = id;
    table->pfnResetCommandList(driver, &args);
    recording = true;
    if (previous) previous->ReleaseInternal();
    if (type != D3D12_COMMAND_LIST_TYPE_BUNDLE)
    {
        D3D12DDI_HRESOURCE no_buffer = {};
        table->pfnSetPredication(driver, no_buffer, 0, D3D12DDI_PREDICATION_OP_EQUAL_ZERO);
    }
    graphics_bound = compute_bound = pipeline_set = false;
    pipeline_flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ZeroMemory(root_arguments, sizeof(root_arguments));
    if (initial_state) SetPipelineState(initial_state);
    else SetDefaultPipelineState();
    return error;
}

void STDMETHODCALLTYPE Native12CommandList::ClearState(ID3D12PipelineState *pipeline_state)
{
    if (RejectInBundle()) return;
    table->pfnClearRootArguments(driver);
    graphics_bound = compute_bound = false;
    ZeroMemory(root_arguments, sizeof(root_arguments));
    if (pipeline_state) SetPipelineState(pipeline_state);
    else SetDefaultPipelineState();
}

void STDMETHODCALLTYPE Native12CommandList::DrawInstanced(UINT vertex_count, UINT instance_count,
        UINT start_vertex, UINT start_instance)
{
    if (type != D3D12_COMMAND_LIST_TYPE_BUNDLE && !graphics_bound) return;
    table->pfnDrawInstanced(driver, vertex_count, instance_count, start_vertex, start_instance);
}

void STDMETHODCALLTYPE Native12CommandList::DrawIndexedInstanced(UINT index_count, UINT instance_count,
        UINT start_index, INT base_vertex, UINT start_instance)
{
    if (type != D3D12_COMMAND_LIST_TYPE_BUNDLE && !graphics_bound) return;
    table->pfnDrawIndexedInstanced(driver, index_count, instance_count, start_index, base_vertex, start_instance);
}

void STDMETHODCALLTYPE Native12CommandList::Dispatch(UINT x, UINT y, UINT z)
{
    if (type != D3D12_COMMAND_LIST_TYPE_BUNDLE && !compute_bound) return;
    table->pfnDispatch(driver, x, y, z);
}

void STDMETHODCALLTYPE Native12CommandList::CopyBufferRegion(ID3D12Resource *dst, UINT64 dst_offset,
        ID3D12Resource *src, UINT64 src_offset, UINT64 byte_count)
{
    if (RejectInBundle()) return;
    table->pfnCopyBufferRegion(driver, Native12Placement(dst, dst_offset), Native12Placement(src, src_offset),
            byte_count);
}

static void Native12CopyLocation(const D3D12_TEXTURE_COPY_LOCATION *location,
        D3D12DDIARG_BUFFER_PLACEMENT *placement, D3D12DDIARG_PLACED_RESOURCE *placed,
        D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT *layout)
{
    ZeroMemory(placed, sizeof(*placed));
    if (location->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT)
    {
        const D3D12_SUBRESOURCE_FOOTPRINT &footprint = location->PlacedFootprint.Footprint;
        Native12FormatInfo format = {};
        Native12GetFormatInfo(footprint.Format, &format);
        if (!format.block_width) format.block_width = 1;
        if (!format.block_height) format.block_height = 1;
        UINT rows = (footprint.Height + format.block_height - 1) / format.block_height;
        *placement = Native12Placement(location->pResource, location->PlacedFootprint.Offset);
        layout->Format = footprint.Format;
        if (layout->Format == DXGI_FORMAT_D32_FLOAT) layout->Format = DXGI_FORMAT_R32_FLOAT;
        else if (layout->Format == DXGI_FORMAT_D16_UNORM) layout->Format = DXGI_FORMAT_R16_UNORM;
        layout->PhysicalWidth = (footprint.Width + format.block_width - 1) / format.block_width * format.block_width;
        layout->PhysicalHeight = rows * format.block_height;
        layout->PhysicalDepth = footprint.Depth;
        layout->Pitch = footprint.RowPitch;
        layout->SlicePitch = footprint.RowPitch * rows;
        placed->Layout = D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED;
        placed->pLayout = layout;
    }
    else
    {
        *placement = Native12Placement(location->pResource, location->SubresourceIndex);
        placed->Layout = D3D12DDI_RL_SELECT_SUBRESOURCE;
    }
}

void STDMETHODCALLTYPE Native12CommandList::CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION *dst,
        UINT dst_x, UINT dst_y, UINT dst_z, const D3D12_TEXTURE_COPY_LOCATION *src, const D3D12_BOX *src_box)
{
    if (RejectInBundle()) return;
    D3D12DDIARG_BUFFER_PLACEMENT dst_placement, src_placement;
    D3D12DDIARG_PLACED_RESOURCE dst_placed, src_placed;
    D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT dst_layout = {}, src_layout = {};

    if (!dst || !src) return;
    if (src_box && (src_box->left >= src_box->right || src_box->top >= src_box->bottom
            || src_box->front >= src_box->back))
        return;
    Native12CopyLocation(dst, &dst_placement, &dst_placed, &dst_layout);
    Native12CopyLocation(src, &src_placement, &src_placed, &src_layout);
    table->pfnCopyTextureRegion(driver, &dst_placement, dst_placed, dst_x, dst_y, dst_z, &src_placement,
            src_placed, reinterpret_cast<const D3D12DDI_BOX *>(src_box));
}

void STDMETHODCALLTYPE Native12CommandList::CopyResource(ID3D12Resource *dst, ID3D12Resource *src)
{
    if (RejectInBundle()) return;
    if (!dst || !src) return;
    table->pfnResourceCopy(driver, Native12UnwrapResource(dst)->driver, Native12UnwrapResource(src)->driver);
}

void STDMETHODCALLTYPE Native12CommandList::CopyTiles(ID3D12Resource *tiled,
        const D3D12_TILED_RESOURCE_COORDINATE *start, const D3D12_TILE_REGION_SIZE *size, ID3D12Resource *buffer,
        UINT64 buffer_offset, D3D12_TILE_COPY_FLAGS flags)
{
    if (RejectInBundle()) return;
    if (!tiled || !buffer || !start || !size) return;
    table->pfnCopyTiles(driver, Native12UnwrapResource(tiled)->driver,
            reinterpret_cast<const D3D12DDI_TILED_RESOURCE_COORDINATE *>(start),
            reinterpret_cast<const D3D12DDI_TILE_REGION_SIZE *>(size), Native12UnwrapResource(buffer)->driver,
            buffer_offset, static_cast<D3D12DDI_TILE_COPY_FLAGS>(flags));
}

void STDMETHODCALLTYPE Native12CommandList::ResolveSubresource(ID3D12Resource *dst, UINT dst_sub_resource,
        ID3D12Resource *src, UINT src_sub_resource, DXGI_FORMAT format)
{
    if (RejectInBundle()) return;
    if (!dst || !src) return;
    table->pfnResourceResolveSubresource(driver, Native12UnwrapResource(dst)->driver, dst_sub_resource,
            Native12UnwrapResource(src)->driver, src_sub_resource, format);
}

void STDMETHODCALLTYPE Native12CommandList::IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology)
{
    table->pfnIaSetTopology(driver, static_cast<D3D12DDI_PRIMITIVE_TOPOLOGY>(topology));
}

void STDMETHODCALLTYPE Native12CommandList::RSSetViewports(UINT count, const D3D12_VIEWPORT *viewports)
{
    if (RejectInBundle()) return;
    table->pfnRsSetViewports(driver, count, reinterpret_cast<const D3D12DDI_VIEWPORT *>(viewports));
}

void STDMETHODCALLTYPE Native12CommandList::RSSetScissorRects(UINT count, const D3D12_RECT *rects)
{
    if (RejectInBundle()) return;
    table->pfnRsSetScissorRects(driver, count, rects);
}

void STDMETHODCALLTYPE Native12CommandList::OMSetBlendFactor(const FLOAT blend_factor[4])
{
    static const FLOAT ones[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    table->pfnOmSetBlendFactor(driver, blend_factor ? blend_factor : ones);
}

void STDMETHODCALLTYPE Native12CommandList::OMSetStencilRef(UINT stencil_ref)
{
    table->pfnOmSetStencilRef(driver, stencil_ref);
}

void STDMETHODCALLTYPE Native12CommandList::SetPipelineState(ID3D12PipelineState *pipeline_state)
{
    if (!pipeline_state) return;
    Native12PipelineState *pipeline = static_cast<Native12PipelineState *>(pipeline_state);
    compute_bound = pipeline->compute;
    graphics_bound = !pipeline->compute;
    pipeline_set = true;
    pipeline_flags = pipeline->flags;
    table->pfnSetPipelineState(driver, pipeline->driver);
}

UINT Native12ConvertResourceBarriers(UINT count, const D3D12_RESOURCE_BARRIER *barriers,
        D3D12DDIARG_RESOURCE_BARRIER_0022 *converted)
{
    UINT used = 0;

    for (UINT i = 0; i < count; ++i)
    {
        D3D12DDIARG_RESOURCE_BARRIER_0022 &target = converted[used];
        ZeroMemory(&target, sizeof(target));
        target.Flags = static_cast<D3D12DDI_RESOURCE_BARRIER_FLAGS>(barriers[i].Flags);
        switch (barriers[i].Type)
        {
            case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
                if (!barriers[i].Transition.pResource) continue;
                target.Type = D3D12DDI_RESOURCE_BARRIER_TYPE_TRANSITION;
                target.Transition.hResource = Native12UnwrapResource(barriers[i].Transition.pResource)->driver;
                target.Transition.Subresource = barriers[i].Transition.Subresource;
                target.Transition.StateBefore =
                        static_cast<D3D12DDI_RESOURCE_STATES>(barriers[i].Transition.StateBefore);
                target.Transition.StateAfter =
                        static_cast<D3D12DDI_RESOURCE_STATES>(barriers[i].Transition.StateAfter);
                break;
            case D3D12_RESOURCE_BARRIER_TYPE_UAV:
                target.Type = D3D12DDI_RESOURCE_BARRIER_TYPE_UAV;
                if (barriers[i].UAV.pResource)
                    target.UAV.hResource = Native12UnwrapResource(barriers[i].UAV.pResource)->driver;
                break;
            default:
                FIXME("Barrier type %u is not implemented.\n", barriers[i].Type);
                continue;
        }
        ++used;
    }
    return used;
}

void STDMETHODCALLTYPE Native12CommandList::ResourceBarrier(UINT count, const D3D12_RESOURCE_BARRIER *barriers)
{
    if (RejectInBundle()) return;
    D3D12DDIARG_RESOURCE_BARRIER_0022 local[16];
    D3D12DDIARG_RESOURCE_BARRIER_0022 *converted = local;
    UINT used;

    if (!count || !barriers) return;
    if (count > ARRAYSIZE(local))
    {
        converted = static_cast<D3D12DDIARG_RESOURCE_BARRIER_0022 *>(
                HeapAlloc(GetProcessHeap(), 0, count * sizeof(*converted)));
        if (!converted) return;
    }
    used = Native12ConvertResourceBarriers(count, barriers, converted);
    if (used) table->pfnResourceBarrier(driver, used, converted);
    if (converted != local) HeapFree(GetProcessHeap(), 0, converted);
}

void STDMETHODCALLTYPE Native12CommandList::ExecuteBundle(ID3D12GraphicsCommandList *command_list)
{
    if (RejectInBundle()) return;
    if (!command_list) return;
    Native12CommandList *bundle = static_cast<Native12CommandList *>(command_list);
    table->pfnExecuteBundle(driver, bundle->driver);
    ZeroMemory(root_arguments, sizeof(root_arguments));
    if (bundle->pipeline_set)
    {
        graphics_bound = bundle->graphics_bound;
        compute_bound = bundle->compute_bound;
        pipeline_flags = bundle->pipeline_flags;
    }
}

void STDMETHODCALLTYPE Native12CommandList::SetDescriptorHeaps(UINT count, ID3D12DescriptorHeap *const *heaps)
{
    D3D12DDI_HDESCRIPTORHEAP handles[D3D12DDI_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

    if (count > ARRAYSIZE(handles)) return;
    for (UINT i = 0; i < count; ++i)
        handles[i] = static_cast<Native12DescriptorHeap *>(heaps[i])->driver;
    table->pfnSetDescriptorHeaps(driver, count, handles);
}

void Native12CommandList::ApplyRootArgument(UINT bind_point, UINT index)
{
    Native12RootArguments &arguments = root_arguments[bind_point];
    bool compute = bind_point == 1;
    D3D12_GPU_DESCRIPTOR_HANDLE table_handle;

    switch (arguments.types[index])
    {
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            table_handle.ptr = arguments.values[index];
            if (compute) table->pfnSetComputeRootDescriptorTable(driver, index, Native12GpuHandle(table_handle));
            else table->pfnSetGraphicsRootDescriptorTable(driver, index, Native12GpuHandle(table_handle));
            break;
        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            if (compute) table->pfnSetComputeRoot32BitConstants(driver, index, arguments.constant_counts[index],
                    &arguments.constants[arguments.constant_offsets[index]], 0);
            else table->pfnSetGraphicsRoot32BitConstants(driver, index, arguments.constant_counts[index],
                    &arguments.constants[arguments.constant_offsets[index]], 0);
            break;
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
            if (compute) table->pfnSetComputeRootConstantBufferView(driver, index, arguments.values[index]);
            else table->pfnSetGraphicsRootConstantBufferView(driver, index, arguments.values[index]);
            break;
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
            if (compute) table->pfnSetComputeRootShaderResourceView(driver, index, arguments.values[index]);
            else table->pfnSetGraphicsRootShaderResourceView(driver, index, arguments.values[index]);
            break;
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            if (compute) table->pfnSetComputeRootUnorderedAccessView(driver, index, arguments.values[index]);
            else table->pfnSetGraphicsRootUnorderedAccessView(driver, index, arguments.values[index]);
            break;
    }
}

void Native12CommandList::BindRootSignature(UINT bind_point, Native12RootSignature *signature)
{
    Native12RootArguments &arguments = root_arguments[bind_point];
    D3D12DDI_HROOTSIGNATURE handle = {};
    UINT kept = 0;

    if (signature && signature->driver.pDrvPrivate == arguments.bound) return;
    if (signature)
    {
        handle = signature->driver;
        while (kept < arguments.count && kept < signature->parameter_count
                && arguments.keys[kept] == signature->parameter_keys[kept])
            ++kept;
    }
    if (bind_point == 1) table->pfnSetComputeRootSignature(driver, handle);
    else table->pfnSetGraphicsRootSignature(driver, handle);

    arguments.set &= kept >= D3D12_MAX_ROOT_COST ? ~0ull : (1ull << kept) - 1;
    arguments.bound = handle.pDrvPrivate;
    arguments.count = signature ? signature->parameter_count : 0;
    for (UINT i = 0; i < arguments.count; ++i)
    {
        arguments.keys[i] = signature->parameter_keys[i];
        arguments.types[i] = signature->parameter_types[i];
        arguments.constant_counts[i] = signature->parameter_constants[i];
        arguments.constant_offsets[i] = signature->constant_offsets[i];
    }
    for (UINT i = 0; i < kept; ++i)
        if (arguments.set & (1ull << i)) ApplyRootArgument(bind_point, i);
}

void Native12CommandList::RecordRootValue(UINT bind_point, UINT index, UINT64 value)
{
    Native12RootArguments &arguments = root_arguments[bind_point];
    if (index >= arguments.count) return;
    arguments.values[index] = value;
    arguments.set |= 1ull << index;
}

void Native12CommandList::RecordRootConstants(UINT bind_point, UINT index, UINT count, const void *data, UINT offset)
{
    Native12RootArguments &arguments = root_arguments[bind_point];
    if (index >= arguments.count || arguments.types[index] != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS
            || offset > arguments.constant_counts[index] || count > arguments.constant_counts[index] - offset)
        return;
    memcpy(&arguments.constants[arguments.constant_offsets[index] + offset], data, count * sizeof(UINT));
    arguments.set |= 1ull << index;
}

void STDMETHODCALLTYPE Native12CommandList::SetComputeRootSignature(ID3D12RootSignature *root_signature)
{
    BindRootSignature(1, static_cast<Native12RootSignature *>(root_signature));
}

void STDMETHODCALLTYPE Native12CommandList::SetGraphicsRootSignature(ID3D12RootSignature *root_signature)
{
    BindRootSignature(0, static_cast<Native12RootSignature *>(root_signature));
}

void STDMETHODCALLTYPE Native12CommandList::SetComputeRootDescriptorTable(UINT index,
        D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    RecordRootValue(1, index, base_descriptor.ptr);
    table->pfnSetComputeRootDescriptorTable(driver, index, Native12GpuHandle(base_descriptor));
}

void STDMETHODCALLTYPE Native12CommandList::SetGraphicsRootDescriptorTable(UINT index,
        D3D12_GPU_DESCRIPTOR_HANDLE base_descriptor)
{
    RecordRootValue(0, index, base_descriptor.ptr);
    table->pfnSetGraphicsRootDescriptorTable(driver, index, Native12GpuHandle(base_descriptor));
}

void STDMETHODCALLTYPE Native12CommandList::SetComputeRoot32BitConstant(UINT index, UINT data, UINT dst_offset)
{
    RecordRootConstants(1, index, 1, &data, dst_offset);
    table->pfnSetComputeRoot32BitConstant(driver, index, data, dst_offset);
}

void STDMETHODCALLTYPE Native12CommandList::SetGraphicsRoot32BitConstant(UINT index, UINT data, UINT dst_offset)
{
    RecordRootConstants(0, index, 1, &data, dst_offset);
    table->pfnSetGraphicsRoot32BitConstant(driver, index, data, dst_offset);
}

void STDMETHODCALLTYPE Native12CommandList::SetComputeRoot32BitConstants(UINT index, UINT count,
        const void *data, UINT dst_offset)
{
    RecordRootConstants(1, index, count, data, dst_offset);
    table->pfnSetComputeRoot32BitConstants(driver, index, count, data, dst_offset);
}

void STDMETHODCALLTYPE Native12CommandList::SetGraphicsRoot32BitConstants(UINT index, UINT count,
        const void *data, UINT dst_offset)
{
    RecordRootConstants(0, index, count, data, dst_offset);
    table->pfnSetGraphicsRoot32BitConstants(driver, index, count, data, dst_offset);
}

void STDMETHODCALLTYPE Native12CommandList::SetComputeRootConstantBufferView(UINT index,
        D3D12_GPU_VIRTUAL_ADDRESS address)
{
    RecordRootValue(1, index, address);
    table->pfnSetComputeRootConstantBufferView(driver, index, address);
}

void STDMETHODCALLTYPE Native12CommandList::SetGraphicsRootConstantBufferView(UINT index,
        D3D12_GPU_VIRTUAL_ADDRESS address)
{
    RecordRootValue(0, index, address);
    table->pfnSetGraphicsRootConstantBufferView(driver, index, address);
}

void STDMETHODCALLTYPE Native12CommandList::SetComputeRootShaderResourceView(UINT index,
        D3D12_GPU_VIRTUAL_ADDRESS address)
{
    RecordRootValue(1, index, address);
    table->pfnSetComputeRootShaderResourceView(driver, index, address);
}

void STDMETHODCALLTYPE Native12CommandList::SetGraphicsRootShaderResourceView(UINT index,
        D3D12_GPU_VIRTUAL_ADDRESS address)
{
    RecordRootValue(0, index, address);
    table->pfnSetGraphicsRootShaderResourceView(driver, index, address);
}

void STDMETHODCALLTYPE Native12CommandList::SetComputeRootUnorderedAccessView(UINT index,
        D3D12_GPU_VIRTUAL_ADDRESS address)
{
    RecordRootValue(1, index, address);
    table->pfnSetComputeRootUnorderedAccessView(driver, index, address);
}

void STDMETHODCALLTYPE Native12CommandList::SetGraphicsRootUnorderedAccessView(UINT index,
        D3D12_GPU_VIRTUAL_ADDRESS address)
{
    RecordRootValue(0, index, address);
    table->pfnSetGraphicsRootUnorderedAccessView(driver, index, address);
}

void STDMETHODCALLTYPE Native12CommandList::IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW *view)
{
    table->pfnIASetIndexBuffer(driver, reinterpret_cast<const D3D12DDI_INDEX_BUFFER_VIEW *>(view));
}

void STDMETHODCALLTYPE Native12CommandList::IASetVertexBuffers(UINT start_slot, UINT count,
        const D3D12_VERTEX_BUFFER_VIEW *views)
{
    table->pfnIASetVertexBuffers(driver, start_slot, count,
            reinterpret_cast<const D3D12DDI_VERTEX_BUFFER_VIEW *>(views));
}

void STDMETHODCALLTYPE Native12CommandList::SOSetTargets(UINT start_slot, UINT count,
        const D3D12_STREAM_OUTPUT_BUFFER_VIEW *views)
{
    if (RejectInBundle()) return;
    table->pfnSOSetTargets(driver, start_slot, count,
            reinterpret_cast<const D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW *>(views));
}

void STDMETHODCALLTYPE Native12CommandList::OMSetRenderTargets(UINT count,
        const D3D12_CPU_DESCRIPTOR_HANDLE *render_targets, BOOL single_handle,
        const D3D12_CPU_DESCRIPTOR_HANDLE *depth_stencil)
{
    if (RejectInBundle()) return;
    static_assert(sizeof(D3D12_CPU_DESCRIPTOR_HANDLE) == sizeof(D3D12DDI_CPU_DESCRIPTOR_HANDLE), "descriptor");
    table->pfnOMSetRenderTargets(driver, count,
            reinterpret_cast<const D3D12DDI_CPU_DESCRIPTOR_HANDLE *>(render_targets), single_handle,
            reinterpret_cast<const D3D12DDI_CPU_DESCRIPTOR_HANDLE *>(depth_stencil));
}

void STDMETHODCALLTYPE Native12CommandList::ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE dsv,
        D3D12_CLEAR_FLAGS flags, FLOAT depth, UINT8 stencil, UINT rect_count, const D3D12_RECT *rects)
{
    if (RejectInBundle()) return;
    DXGI_FORMAT format = device->GetDsvFormat(dsv.ptr);
    if (format == DXGI_FORMAT_D16_UNORM || format == DXGI_FORMAT_D32_FLOAT)
        flags = static_cast<D3D12_CLEAR_FLAGS>(flags & ~D3D12_CLEAR_FLAG_STENCIL);
    if (!(flags & (D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL))) return;
    table->pfnClearDepthStencilView(driver, Native12CpuHandle(dsv), flags, depth, stencil, rect_count, rects);
}

void STDMETHODCALLTYPE Native12CommandList::ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        const FLOAT color[4], UINT rect_count, const D3D12_RECT *rects)
{
    if (RejectInBundle()) return;
    table->pfnClearRenderTargetView(driver, Native12CpuHandle(rtv), color, rect_count, rects);
}

void STDMETHODCALLTYPE Native12CommandList::ClearUnorderedAccessViewUint(D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource, const UINT values[4], UINT rect_count,
        const D3D12_RECT *rects)
{
    if (RejectInBundle()) return;
    if (!resource) return;
    table->pfnClearUnorderedAccessViewUint(driver, Native12GpuHandle(gpu_handle), Native12CpuHandle(cpu_handle),
            Native12UnwrapResource(resource)->driver, values, rect_count, rects);
}

void STDMETHODCALLTYPE Native12CommandList::ClearUnorderedAccessViewFloat(D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource *resource, const float values[4], UINT rect_count,
        const D3D12_RECT *rects)
{
    if (RejectInBundle()) return;
    if (!resource) return;
    table->pfnClearUnorderedAccessViewFloat(driver, Native12GpuHandle(gpu_handle), Native12CpuHandle(cpu_handle),
            Native12UnwrapResource(resource)->driver, values, rect_count, rects);
}

void STDMETHODCALLTYPE Native12CommandList::DiscardResource(ID3D12Resource *resource,
        const D3D12_DISCARD_REGION *region)
{
    if (RejectInBundle()) return;
    D3D12DDIARG_DISCARD_RESOURCE_0003 args = {};

    if (!resource) return;
    Native12Resource *target = Native12UnwrapResource(resource);
    if (region)
    {
        args.NumRects = region->NumRects;
        args.pRects = region->pRects;
        args.FirstSubresource = region->FirstSubresource;
        args.NumSubresources = region->NumSubresources;
    }
    else args.NumSubresources = target->SubresourceCount();
    table->pfnDiscardResource(driver, target->driver, &args);
}

void STDMETHODCALLTYPE Native12CommandList::BeginQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE query_type,
        UINT index)
{
    if (RejectInBundle()) return;
    if (!heap) return;
    table->pfnBeginQuery(driver, static_cast<Native12QueryHeap *>(heap)->driver,
            static_cast<D3D12DDI_QUERY_TYPE>(query_type), index);
}

void STDMETHODCALLTYPE Native12CommandList::EndQuery(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE query_type,
        UINT index)
{
    if (RejectInBundle()) return;
    if (!heap) return;
    table->pfnEndQuery(driver, static_cast<Native12QueryHeap *>(heap)->driver,
            static_cast<D3D12DDI_QUERY_TYPE>(query_type), index);
}

void STDMETHODCALLTYPE Native12CommandList::ResolveQueryData(ID3D12QueryHeap *heap, D3D12_QUERY_TYPE query_type,
        UINT start, UINT count, ID3D12Resource *buffer, UINT64 offset)
{
    if (!heap || !buffer) return;
    table->pfnResolveQueryData(driver, static_cast<Native12QueryHeap *>(heap)->driver,
            static_cast<D3D12DDI_QUERY_TYPE>(query_type), start, count, Native12UnwrapResource(buffer)->driver,
            offset);
}

void STDMETHODCALLTYPE Native12CommandList::SetPredication(ID3D12Resource *buffer, UINT64 offset,
        D3D12_PREDICATION_OP operation)
{
    if (RejectInBundle()) return;
    D3D12DDI_HRESOURCE handle = {};
    if (buffer) handle = Native12UnwrapResource(buffer)->driver;
    table->pfnSetPredication(driver, handle, offset, static_cast<D3D12DDI_PREDICATION_OP>(operation));
}

void STDMETHODCALLTYPE Native12CommandList::SetMarker(UINT, const void *, UINT) {}
void STDMETHODCALLTYPE Native12CommandList::BeginEvent(UINT, const void *, UINT) {}
void STDMETHODCALLTYPE Native12CommandList::EndEvent() {}

void STDMETHODCALLTYPE Native12CommandList::ExecuteIndirect(ID3D12CommandSignature *signature, UINT max_count,
        ID3D12Resource *arguments, UINT64 argument_offset, ID3D12Resource *count, UINT64 count_offset)
{
    if (!signature || !arguments) return;
    table->pfnExecuteIndirect(driver, static_cast<Native12CommandSignature *>(signature)->driver, max_count,
            Native12Placement(arguments, argument_offset), Native12Placement(count, count_offset));
}

static_assert(sizeof(D3D12_SAMPLE_POSITION) == sizeof(D3D12DDI_SAMPLE_POSITION), "sample position layout");
static_assert(sizeof(D3D12_WRITEBUFFERIMMEDIATE_PARAMETER) == sizeof(D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032)
        && offsetof(D3D12_WRITEBUFFERIMMEDIATE_PARAMETER, Value)
        == offsetof(D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032, Value), "write immediate layout");
static_assert(sizeof(D3D12_WRITEBUFFERIMMEDIATE_MODE) == sizeof(D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_0032),
        "write immediate mode");

void STDMETHODCALLTYPE Native12CommandList::AtomicCopyBufferUINT(ID3D12Resource *dst, UINT64 dst_offset,
        ID3D12Resource *src, UINT64 src_offset, UINT, ID3D12Resource *const *, const D3D12_SUBRESOURCE_RANGE_UINT64 *)
{
    if (!dst || !src || !table->pfnAtomicCopyBufferRegion) return;
    table->pfnAtomicCopyBufferRegion(driver, Native12Placement(dst, dst_offset), Native12Placement(src, src_offset),
            sizeof(UINT));
}

void STDMETHODCALLTYPE Native12CommandList::AtomicCopyBufferUINT64(ID3D12Resource *dst, UINT64 dst_offset,
        ID3D12Resource *src, UINT64 src_offset, UINT, ID3D12Resource *const *, const D3D12_SUBRESOURCE_RANGE_UINT64 *)
{
    if (!dst || !src || !table->pfnAtomicCopyBufferRegion) return;
    table->pfnAtomicCopyBufferRegion(driver, Native12Placement(dst, dst_offset), Native12Placement(src, src_offset),
            sizeof(UINT64));
}

void STDMETHODCALLTYPE Native12CommandList::OMSetDepthBounds(FLOAT min, FLOAT max)
{
    if (table->pfnOMSetDepthBounds) table->pfnOMSetDepthBounds(driver, min, max);
}

void STDMETHODCALLTYPE Native12CommandList::SetSamplePositions(UINT sample_count, UINT pixel_count,
        D3D12_SAMPLE_POSITION *positions)
{
    if (RejectInBundle()) return;
    if (table->pfnSetSamplePositions)
        table->pfnSetSamplePositions(driver, sample_count, pixel_count,
                reinterpret_cast<D3D12DDI_SAMPLE_POSITION *>(positions));
}

void STDMETHODCALLTYPE Native12CommandList::ResolveSubresourceRegion(ID3D12Resource *dst, UINT dst_sub_resource,
        UINT dst_x, UINT dst_y, ID3D12Resource *src, UINT src_sub_resource, D3D12_RECT *src_rect, DXGI_FORMAT format,
        D3D12_RESOLVE_MODE mode)
{
    if (RejectInBundle()) return;
    if (!dst || !src || !table->pfnResourceResolveSubresourceRegion) return;
    table->pfnResourceResolveSubresourceRegion(driver, Native12UnwrapResource(dst)->driver, dst_sub_resource, dst_x,
            dst_y, Native12UnwrapResource(src)->driver, src_sub_resource, src_rect, format,
            static_cast<D3D12DDI_RESOLVE_MODE>(mode));
}

void STDMETHODCALLTYPE Native12CommandList::SetViewInstanceMask(UINT mask)
{
    if (table->pfnSetViewInstanceMask) table->pfnSetViewInstanceMask(driver, mask);
}

void STDMETHODCALLTYPE Native12CommandList::WriteBufferImmediate(UINT count,
        const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER *parameters, const D3D12_WRITEBUFFERIMMEDIATE_MODE *modes)
{
    if (!count || !parameters || !table->pfnWriteBufferImmediate) return;
    for (UINT i = 0; modes && i < count; ++i)
    {
        if (modes[i] != D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT && modes[i] != D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN
                && modes[i] != D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT)
        {
            if (SUCCEEDED(error)) error = E_INVALIDARG;
            return;
        }
    }
    table->pfnWriteBufferImmediate(driver, count,
            reinterpret_cast<const D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032 *>(parameters),
            reinterpret_cast<const D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_0032 *>(modes));
}

void STDMETHODCALLTYPE Native12CommandList::SetProtectedResourceSession(ID3D12ProtectedResourceSession *session)
{
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 none = {};
    if (!session && table->pfnSetProtectedResourceSession) table->pfnSetProtectedResourceSession(driver, none);
}

void Native12CommandList::ClearPassResolves()
{
    for (UINT i = 0; i < pass_resolve_count; ++i)
    {
        Native12PassResolve &entry = pass_resolves[i];
        entry.source->Release();
        entry.destination->Release();
        HeapFree(GetProcessHeap(), 0, entry.parameters);
    }
    ZeroMemory(pass_resolves, sizeof(pass_resolves));
    pass_resolve_count = 0;
}

void Native12CommandList::AddPassResolve(const D3D12_RENDER_PASS_ENDING_ACCESS &access)
{
    if (access.Type != D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE || pass_resolve_count == ARRAYSIZE(pass_resolves))
        return;
    const D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_PARAMETERS &resolve = access.Resolve;
    if (!resolve.pSrcResource || !resolve.pDstResource || !resolve.SubresourceCount
            || !resolve.pSubresourceParameters) return;
    Native12PassResolve &entry = pass_resolves[pass_resolve_count];
    SIZE_T bytes = resolve.SubresourceCount * sizeof(*resolve.pSubresourceParameters);
    entry.parameters = static_cast<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS *>(
            HeapAlloc(GetProcessHeap(), 0, bytes));
    if (!entry.parameters)
    {
        if (SUCCEEDED(error)) error = E_OUTOFMEMORY;
        return;
    }
    memcpy(entry.parameters, resolve.pSubresourceParameters, bytes);
    entry.source = resolve.pSrcResource;
    entry.source->AddRef();
    entry.destination = resolve.pDstResource;
    entry.destination->AddRef();
    entry.count = resolve.SubresourceCount;
    entry.format = resolve.Format;
    entry.mode = resolve.ResolveMode;
    ++pass_resolve_count;
}

static bool Native12PassBindsRaster(D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE type, bool read_only)
{
    switch (type)
    {
        case D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD:
        case D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE:
        case D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR:
        case D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_RENDER:
            return true;
        case D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV:
            return read_only;
        default:
            return false;
    }
}

void STDMETHODCALLTYPE Native12CommandList::BeginRenderPass(UINT count,
        const D3D12_RENDER_PASS_RENDER_TARGET_DESC *targets, const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *depth,
        D3D12_RENDER_PASS_FLAGS flags)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handles[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];

    if (count > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT || (count && !targets)) return;
    ClearPassResolves();
    pass_flags = flags;
    UINT bound = 0;
    for (UINT i = 0; i < count; ++i)
        if (Native12PassBindsRaster(targets[i].BeginningAccess.Type, false)) handles[bound++] = targets[i].cpuDescriptor;
    bool bind_depth = depth && (Native12PassBindsRaster(depth->DepthBeginningAccess.Type,
            !!(flags & D3D12_RENDER_PASS_FLAG_BIND_READ_ONLY_DEPTH))
            || Native12PassBindsRaster(depth->StencilBeginningAccess.Type,
            !!(flags & D3D12_RENDER_PASS_FLAG_BIND_READ_ONLY_STENCIL)));
    OMSetRenderTargets(bound, bound ? handles : NULL, FALSE, bind_depth ? &depth->cpuDescriptor : NULL);

    if (!(flags & D3D12_RENDER_PASS_FLAG_RESUMING_PASS))
    {
        for (UINT i = 0; i < count; ++i)
            if (targets[i].BeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
                ClearRenderTargetView(targets[i].cpuDescriptor, targets[i].BeginningAccess.Clear.ClearValue.Color,
                        0, NULL);
        if (bind_depth)
        {
            UINT clear = 0;
            if (depth->DepthBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
                clear |= D3D12_CLEAR_FLAG_DEPTH;
            if (depth->StencilBeginningAccess.Type == D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
                clear |= D3D12_CLEAR_FLAG_STENCIL;
            if (clear)
                ClearDepthStencilView(depth->cpuDescriptor, static_cast<D3D12_CLEAR_FLAGS>(clear),
                        depth->DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth,
                        depth->StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil, 0, NULL);
        }
    }

    if (!(flags & D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS))
    {
        for (UINT i = 0; i < count; ++i) AddPassResolve(targets[i].EndingAccess);
        if (depth)
        {
            AddPassResolve(depth->DepthEndingAccess);
            AddPassResolve(depth->StencilEndingAccess);
        }
    }
}

void STDMETHODCALLTYPE Native12CommandList::EndRenderPass()
{
    for (UINT i = 0; i < pass_resolve_count; ++i)
    {
        const Native12PassResolve &entry = pass_resolves[i];
        for (UINT j = 0; j < entry.count; ++j)
        {
            D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS &parameters = entry.parameters[j];
            ResolveSubresourceRegion(entry.destination, parameters.DstSubresource, parameters.DstX, parameters.DstY,
                    entry.source, parameters.SrcSubresource, &parameters.SrcRect, entry.format, entry.mode);
        }
    }
    ClearPassResolves();
    pass_flags = D3D12_RENDER_PASS_FLAG_NONE;
}

void STDMETHODCALLTYPE Native12CommandList::InitializeMetaCommand(ID3D12MetaCommand *, const void *, SIZE_T) {}
void STDMETHODCALLTYPE Native12CommandList::ExecuteMetaCommand(ID3D12MetaCommand *, const void *, SIZE_T) {}
void STDMETHODCALLTYPE Native12CommandList::BuildRaytracingAccelerationStructure(
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *, UINT,
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *) {}
void STDMETHODCALLTYPE Native12CommandList::EmitRaytracingAccelerationStructurePostbuildInfo(
        const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *, UINT, const D3D12_GPU_VIRTUAL_ADDRESS *) {}
void STDMETHODCALLTYPE Native12CommandList::CopyRaytracingAccelerationStructure(D3D12_GPU_VIRTUAL_ADDRESS,
        D3D12_GPU_VIRTUAL_ADDRESS, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE) {}
void STDMETHODCALLTYPE Native12CommandList::SetPipelineState1(ID3D12StateObject *) {}
void STDMETHODCALLTYPE Native12CommandList::DispatchRays(const D3D12_DISPATCH_RAYS_DESC *) {}

static bool Native12ShadingRateValid(D3D12_SHADING_RATE rate, bool additional)
{
    switch (rate)
    {
        case D3D12_SHADING_RATE_1X1:
        case D3D12_SHADING_RATE_1X2:
        case D3D12_SHADING_RATE_2X1:
        case D3D12_SHADING_RATE_2X2:
            return true;
        case D3D12_SHADING_RATE_2X4:
        case D3D12_SHADING_RATE_4X2:
        case D3D12_SHADING_RATE_4X4:
            return additional;
        default:
            return false;
    }
}

void STDMETHODCALLTYPE Native12CommandList::RSSetShadingRate(D3D12_SHADING_RATE rate,
        const D3D12_SHADING_RATE_COMBINER *combiners)
{
    bool additional = device->options.VariableShadingRateTier != D3D12DDI_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED
            && device->options.AdditionalShadingRatesSupported;
    if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_BUNDLE)
    {
        if (SUCCEEDED(error)) error = E_FAIL;
        return;
    }
    if (!Native12ShadingRateValid(rate, additional) || (combiners && (combiners[0] > D3D12_SHADING_RATE_COMBINER_SUM
            || combiners[1] > D3D12_SHADING_RATE_COMBINER_SUM)))
    {
        if (SUCCEEDED(error)) error = E_INVALIDARG;
        return;
    }
    if (table->pfnRSSetShadingRate)
        table->pfnRSSetShadingRate(driver, static_cast<D3D12DDI_SHADING_RATE_0062>(rate),
                reinterpret_cast<const D3D12DDI_SHADING_RATE_COMBINER_0062 *>(combiners));
}

void STDMETHODCALLTYPE Native12CommandList::RSSetShadingRateImage(ID3D12Resource *)
{
    if (SUCCEEDED(error)) error = E_FAIL;
}

void STDMETHODCALLTYPE Native12CommandList::DispatchMesh(UINT, UINT, UINT) {}

void STDMETHODCALLTYPE Native12CommandList::Barrier(UINT32 group_count, const D3D12_BARRIER_GROUP *groups)
{
    if (RejectInBundle()) return;
    for (UINT32 i = 0; i < group_count; ++i)
    {
        if (groups[i].NumBarriers)
        {
            if (SUCCEEDED(error)) error = E_FAIL;
            return;
        }
    }
}

void STDMETHODCALLTYPE Native12CommandList::OMSetFrontAndBackStencilRef(UINT front, UINT back)
{
    if (table->pfnOmSetFrontAndBackStencilRef) table->pfnOmSetFrontAndBackStencilRef(driver, front, back);
}

void STDMETHODCALLTYPE Native12CommandList::RSSetDepthBias(FLOAT bias, FLOAT clamp, FLOAT slope_scaled_bias)
{
    if (table->pfnRSSetDepthBias && (pipeline_flags & D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS))
        table->pfnRSSetDepthBias(driver, bias, clamp, slope_scaled_bias);
}

void STDMETHODCALLTYPE Native12CommandList::IASetIndexBufferStripCutValue(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE value)
{
    if (table->pfnIASetIndexBufferStripCutValue
            && (pipeline_flags & D3D12_PIPELINE_STATE_FLAG_DYNAMIC_INDEX_BUFFER_STRIP_CUT))
        table->pfnIASetIndexBufferStripCutValue(driver, static_cast<D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE>(value));
}

void STDMETHODCALLTYPE Native12CommandList::SetProgram(const D3D12_SET_PROGRAM_DESC *) {}
void STDMETHODCALLTYPE Native12CommandList::DispatchGraph(const D3D12_DISPATCH_GRAPH_DESC *) {}

HRESULT STDMETHODCALLTYPE Native12Device::CreateCommandList(UINT, D3D12_COMMAND_LIST_TYPE type,
        ID3D12CommandAllocator *allocator, ID3D12PipelineState *initial_state, REFIID iid, void **out)
{
    if (!allocator || !out) return E_INVALIDARG;
    *out = NULL;
    if (static_cast<Native12CommandAllocator *>(allocator)->type != type) return E_INVALIDARG;
    if (type == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE)
    {
        Native12VideoDecodeCommandList *video = new Native12VideoDecodeCommandList(this);
        if (!video) return E_OUTOFMEMORY;
        HRESULT hr = video->Initialize(static_cast<Native12CommandAllocator *>(allocator));
        if (SUCCEEDED(hr)) hr = video->QueryInterface(iid, out);
        video->Release();
        return hr;
    }
    if (type == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS)
    {
        Native12VideoProcessCommandList *video = new Native12VideoProcessCommandList(this);
        if (!video) return E_OUTOFMEMORY;
        HRESULT hr = video->Initialize(static_cast<Native12CommandAllocator *>(allocator));
        if (SUCCEEDED(hr)) hr = video->QueryInterface(iid, out);
        video->Release();
        return hr;
    }
    Native12CommandList *list = new Native12CommandList(this);
    if (!list) return E_OUTOFMEMORY;
    HRESULT hr = list->Initialize(type, static_cast<Native12CommandAllocator *>(allocator), initial_state);
    if (SUCCEEDED(hr)) hr = list->QueryInterface(iid, out);
    list->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateHeap(const D3D12_HEAP_DESC *desc, REFIID iid, void **out)
{
    if (out) *out = NULL;
    if (!desc || !desc->SizeInBytes || (desc->Flags & D3D12_HEAP_FLAG_ALLOW_DISPLAY)) return E_INVALIDARG;
    if (FAILED(ValidateHeapProperties(&desc->Properties, desc->Flags))) return E_INVALIDARG;
    if (desc->Alignment && desc->Alignment != D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
            && desc->Alignment != D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT)
        return E_INVALIDARG;
    Native12Heap *heap = new Native12Heap(this);
    if (!heap) return E_OUTOFMEMORY;
    HRESULT hr = heap->Initialize(desc);
    if (SUCCEEDED(hr)) hr = out ? heap->QueryInterface(iid, out) : S_FALSE;
    heap->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreatePlacedResource(ID3D12Heap *heap, UINT64 offset,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *clear_value, REFIID iid, void **out)
{
    if (out) *out = NULL;
    if (!heap || !desc) return E_INVALIDARG;
    HRESULT hr = Native12ValidateResource(static_cast<Native12Heap *>(heap)->desc.Properties.Type, desc,
            initial_state, clear_value);
    if (FAILED(hr)) return hr;
    Native12Resource *resource = new Native12Resource(this);
    if (!resource) return E_OUTOFMEMORY;
    hr = resource->InitializePlaced(static_cast<Native12Heap *>(heap), offset, desc, initial_state,
            clear_value);
    if (SUCCEEDED(hr)) hr = out ? resource->QueryInterface(iid, out) : S_FALSE;
    resource->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateReservedResource(const D3D12_RESOURCE_DESC *desc,
        D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *clear_value, REFIID iid, void **out)
{
    if (out) *out = NULL;
    if (!desc) return E_INVALIDARG;
    if (desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER && desc->Layout != D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE)
        return E_INVALIDARG;
    HRESULT hr = Native12ValidateResource(D3D12_HEAP_TYPE_DEFAULT, desc, initial_state, clear_value);
    if (FAILED(hr)) return hr;
    Native12Resource *resource = new Native12Resource(this);
    if (!resource) return E_OUTOFMEMORY;
    hr = resource->InitializeReserved(desc, initial_state, clear_value);
    if (SUCCEEDED(hr)) hr = out ? resource->QueryInterface(iid, out) : S_FALSE;
    resource->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateSharedHandle(ID3D12DeviceChild *object,
        const SECURITY_ATTRIBUTES *attributes, DWORD access, const WCHAR *name, HANDLE *out)
{
    if (!object || !out) return E_INVALIDARG;
    *out = NULL;
    if (name)
    {
        FIXME("Named shared handles are not supported.\n");
        return E_NOTIMPL;
    }
    ID3D12Resource *resource = NULL;
    if (FAILED(object->QueryInterface(IID_ID3D12Resource, reinterpret_cast<void **>(&resource))))
    {
        FIXME("Only resources can be shared.\n");
        return E_NOTIMPL;
    }
    Native12Resource *impl = Native12UnwrapResource(resource);
    D3DKMT_HANDLE km_resource = 0;
    HRESULT hr = E_INVALIDARG;
    if (impl->nt_sharing && get_resource_handles)
        hr = get_resource_handles(runtime_device, impl->identity, &km_resource, NULL);
    resource->Release();
    if (FAILED(hr)) return hr;
    OBJECT_ATTRIBUTES attributes_nt = {};
    attributes_nt.Length = sizeof(attributes_nt);
    attributes_nt.Attributes = attributes && attributes->bInheritHandle ? OBJ_INHERIT : 0;
    attributes_nt.SecurityDescriptor = attributes ? attributes->lpSecurityDescriptor : NULL;
    HANDLE handle = NULL;
    hr = Native12StatusToHresult(D3DKMTShareObjects(1, &km_resource, &attributes_nt, access, &handle));
    if (SUCCEEDED(hr)) *out = handle;
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::OpenSharedHandle(HANDLE handle, REFIID iid, void **out)
{
    if (out) *out = NULL;
    if (!handle) return E_INVALIDARG;
    if (!IsEqualGUID(iid, IID_ID3D12Resource) && !IsEqualGUID(iid, IID_ID3D12Resource1)
            && !IsEqualGUID(iid, IID_ID3D12Pageable) && !IsEqualGUID(iid, IID_ID3D12DeviceChild)
            && !IsEqualGUID(iid, IID_ID3D12Object) && !IsEqualGUID(iid, IID_IUnknown))
    {
        FIXME("Opening %s from a shared handle is not supported.\n", debugstr_guid(&iid));
        return E_NOTIMPL;
    }
    D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE query = {};
    query.hDevice = km_device;
    query.hNtHandle = handle;
    HRESULT hr = Native12StatusToHresult(D3DKMTQueryResourceInfoFromNtHandle(&query));
    if (FAILED(hr)) return hr;
    if (query.PrivateRuntimeDataSize != sizeof(Native12SharedTextureData) || !query.NumAllocations
            || query.NumAllocations > ~0u / sizeof(D3DDDI_OPENALLOCATIONINFO2)) return E_INVALIDARG;

    Native12SharedTextureData data = {};
    D3DKMT_OPENRESOURCEFROMNTHANDLE open = {};
    open.hDevice = km_device;
    open.hNtHandle = handle;
    open.NumAllocations = query.NumAllocations;
    open.pOpenAllocationInfo2 = static_cast<D3DDDI_OPENALLOCATIONINFO2 *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            query.NumAllocations * sizeof(*open.pOpenAllocationInfo2)));
    open.PrivateRuntimeDataSize = sizeof(data);
    open.pPrivateRuntimeData = &data;
    open.ResourcePrivateDriverDataSize = query.ResourcePrivateDriverDataSize;
    open.pResourcePrivateDriverData = HeapAlloc(GetProcessHeap(), 0,
            query.ResourcePrivateDriverDataSize ? query.ResourcePrivateDriverDataSize : 1);
    open.TotalPrivateDriverDataBufferSize = query.TotalPrivateDriverDataSize;
    open.pTotalPrivateDriverDataBuffer = HeapAlloc(GetProcessHeap(), 0,
            query.TotalPrivateDriverDataSize ? query.TotalPrivateDriverDataSize : 1);
    D3DDDI_OPENALLOCATIONINFO *allocations = static_cast<D3DDDI_OPENALLOCATIONINFO *>(HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, query.NumAllocations * sizeof(*allocations)));
    Native12Resource *resource = NULL;
    if (!open.pOpenAllocationInfo2 || !open.pResourcePrivateDriverData || !open.pTotalPrivateDriverDataBuffer || !allocations)
        hr = E_OUTOFMEMORY;
    else
        hr = Native12StatusToHresult(D3DKMTOpenResourceFromNtHandle(&open));
    if (SUCCEEDED(hr) && (data.signature != 0x54313144 || data.version != 1 || !data.desc.Width || !data.desc.Height
            || !data.desc.MipLevels || !data.desc.ArraySize || !data.desc.SampleDesc.Count))
        hr = E_INVALIDARG;
    if (SUCCEEDED(hr))
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = data.desc.Width;
        desc.Height = data.desc.Height;
        desc.DepthOrArraySize = static_cast<UINT16>(data.desc.ArraySize);
        desc.MipLevels = static_cast<UINT16>(data.desc.MipLevels);
        desc.Format = data.desc.Format;
        desc.SampleDesc = data.desc.SampleDesc;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (data.desc.BindFlags & D3D11_BIND_RENDER_TARGET) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (data.desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (data.desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (!(data.desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) && (data.desc.BindFlags & D3D11_BIND_DEPTH_STENCIL))
            desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        for (UINT i = 0; i < open.NumAllocations; ++i)
        {
            allocations[i].hAllocation = open.pOpenAllocationInfo2[i].hAllocation;
            allocations[i].pPrivateDriverData = open.pOpenAllocationInfo2[i].pPrivateDriverData;
            allocations[i].PrivateDriverDataSize = open.pOpenAllocationInfo2[i].PrivateDriverDataSize;
        }
        resource = new Native12Resource(this);
        bool adopted = false;
        if (!resource)
            hr = E_OUTOFMEMORY;
        else
        {
            hr = resource->InitializeOpened(&desc, open.hResource, open.NumAllocations, allocations,
                    open.pResourcePrivateDriverData, open.ResourcePrivateDriverDataSize, &adopted);
            if (adopted) open.hResource = 0;
            if (SUCCEEDED(hr)) hr = out ? resource->QueryInterface(iid, out) : S_FALSE;
        }
    }
    if (resource) resource->Release();
    if (open.hResource)
    {
        D3DKMT_DESTROYALLOCATION destroy = {};
        destroy.hDevice = km_device;
        destroy.hResource = open.hResource;
        D3DKMTDestroyAllocation(&destroy);
    }
    HeapFree(GetProcessHeap(), 0, allocations);
    HeapFree(GetProcessHeap(), 0, open.pTotalPrivateDriverDataBuffer);
    HeapFree(GetProcessHeap(), 0, open.pResourcePrivateDriverData);
    HeapFree(GetProcessHeap(), 0, open.pOpenAllocationInfo2);
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::OpenSharedHandleByName(const WCHAR *, DWORD, HANDLE *)
{
    FIXME("OpenSharedHandleByName is not implemented.\n");
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Native12Device::MakeResident(UINT, ID3D12Pageable *const *)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Native12Device::Evict(UINT, ID3D12Pageable *const *)
{
    return S_OK;
}

HRESULT Native12QueryHeap::Initialize(const D3D12_QUERY_HEAP_DESC *input)
{
    desc = *input;
    D3D12DDIARG_CREATE_QUERY_HEAP_0001 args = {};
    args.Type = static_cast<D3D12DDI_QUERY_HEAP_TYPE>(desc.Type);
    args.Count = desc.Count;
    args.NodeMask = desc.NodeMask ? desc.NodeMask : 1;
    SIZE_T size = device->functions.pfnCalcPrivateQueryHeapSize(device->driver_device, &args);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!driver.pDrvPrivate) return E_OUTOFMEMORY;
    HRESULT hr = device->functions.pfnCreateQueryHeap(device->driver_device, &args, driver);
    if (FAILED(hr))
    {
        WARN("The driver failed to create a query heap of type %u with %u queries, hr %#lx.\n", desc.Type,
                desc.Count, hr);
        return hr;
    }
    driver_created = true;
    return S_OK;
}

Native12QueryHeap::~Native12QueryHeap()
{
    if (driver_created) device->functions.pfnDestroyQueryHeap(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
}

HRESULT STDMETHODCALLTYPE Native12QueryHeap::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12QueryHeap) || IsEqualGUID(iid, IID_ID3D12Pageable)
            || IsEqualGUID(iid, IID_ID3D12DeviceChild) || IsEqualGUID(iid, IID_ID3D12Object)
            || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12QueryHeap *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateQueryHeap(const D3D12_QUERY_HEAP_DESC *desc, REFIID iid,
        void **out)
{
    if (out) *out = NULL;
    if (!desc || !desc->Count) return E_INVALIDARG;
    Native12QueryHeap *heap = new Native12QueryHeap(this);
    if (!heap) return E_OUTOFMEMORY;
    HRESULT hr = heap->Initialize(desc);
    if (SUCCEEDED(hr)) hr = out ? heap->QueryInterface(iid, out) : S_FALSE;
    heap->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::SetStablePowerState(BOOL)
{
    return S_OK;
}

static_assert(sizeof(D3D12DDI_INDIRECT_ARGUMENT_DESC) == sizeof(D3D12_INDIRECT_ARGUMENT_DESC),
        "indirect argument layout");

HRESULT Native12CommandSignature::Initialize(const D3D12_COMMAND_SIGNATURE_DESC *input,
        ID3D12RootSignature *signature)
{
    for (UINT i = 0; i < input->NumArgumentDescs; ++i)
    {
        D3D12_INDIRECT_ARGUMENT_TYPE type = input->pArgumentDescs[i].Type;
        if ((type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW || type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED
                || type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH) && i != input->NumArgumentDescs - 1)
            return E_INVALIDARG;
    }
    D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 args = {};
    args.ByteStride = input->ByteStride;
    args.NumArgumentDescs = input->NumArgumentDescs;
    args.pArgumentDescs = reinterpret_cast<const D3D12DDI_INDIRECT_ARGUMENT_DESC *>(input->pArgumentDescs);
    args.NodeMask = input->NodeMask ? input->NodeMask : 1;
    if (signature)
    {
        root_signature = static_cast<Native12RootSignature *>(signature);
        root_signature->AddRef();
        args.hRootSignature = root_signature->driver;
    }
    SIZE_T size = device->functions.pfnCalcPrivateCommandSignatureSize(device->driver_device, &args);
    driver.pDrvPrivate = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!driver.pDrvPrivate) return E_OUTOFMEMORY;
    HRESULT hr = device->functions.pfnCreateCommandSignature(device->driver_device, &args, driver);
    if (FAILED(hr))
    {
        WARN("The driver failed to create a command signature with %u arguments, hr %#lx.\n",
                input->NumArgumentDescs, hr);
        return hr;
    }
    driver_created = true;
    return S_OK;
}

Native12CommandSignature::~Native12CommandSignature()
{
    if (driver_created) device->functions.pfnDestroyCommandSignature(device->driver_device, driver);
    if (driver.pDrvPrivate) HeapFree(GetProcessHeap(), 0, driver.pDrvPrivate);
    if (root_signature) root_signature->Release();
}

HRESULT STDMETHODCALLTYPE Native12CommandSignature::QueryInterface(REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, IID_ID3D12CommandSignature) || IsEqualGUID(iid, IID_ID3D12Pageable)
            || IsEqualGUID(iid, IID_ID3D12DeviceChild) || IsEqualGUID(iid, IID_ID3D12Object)
            || IsEqualGUID(iid, IID_IUnknown))
    {
        *out = static_cast<ID3D12CommandSignature *>(this);
        AddRef();
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC *desc,
        ID3D12RootSignature *root_signature, REFIID iid, void **out)
{
    if (out) *out = NULL;
    if (!desc || !desc->NumArgumentDescs || !desc->pArgumentDescs) return E_INVALIDARG;
    Native12CommandSignature *signature = new Native12CommandSignature(this);
    if (!signature) return E_OUTOFMEMORY;
    HRESULT hr = signature->Initialize(desc, root_signature);
    if (SUCCEEDED(hr)) hr = out ? signature->QueryInterface(iid, out) : S_FALSE;
    signature->Release();
    return hr;
}

static void Native12TileShape(const D3D12_RESOURCE_DESC *desc, D3D12_TILE_SHAPE *shape)
{
    Native12FormatInfo format = {};
    shape->WidthInTexels = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    shape->HeightInTexels = 1;
    shape->DepthInTexels = 1;
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) return;
    Native12GetFormatInfo(desc->Format, &format);
    UINT block_bits = format.block_bytes * 8;
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        if (format.block_width == 4)
        {
            shape->WidthInTexels = format.block_bytes == 8 ? 128 : 64;
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
    UINT width = 64, height = 64;
    if (format.block_width == 4)
    {
        width = format.block_bytes == 8 ? 512 : 256;
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
    switch (desc->SampleDesc.Count)
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

void STDMETHODCALLTYPE Native12Device::GetResourceTiling(ID3D12Resource *resource, UINT *total_tiles,
        D3D12_PACKED_MIP_INFO *packed, D3D12_TILE_SHAPE *shape, UINT *tiling_count, UINT first_tiling,
        D3D12_SUBRESOURCE_TILING *tilings)
{
    if (!resource) return;
    Native12Resource *target = Native12UnwrapResource(resource);
    const D3D12_RESOURCE_DESC &desc = target->desc;
    D3D12_TILE_SHAPE standard = {};
    Native12TileShape(&desc, &standard);
    if (shape) *shape = standard;

    UINT packed_mips = 0, packed_tiles = 0;
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
        functions.pfnGetMipPacking(driver_device, target->driver, &packed_mips, &packed_tiles);
    UINT layers = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc.DepthOrArraySize;
    UINT standard_mips = desc.MipLevels > packed_mips ? desc.MipLevels - packed_mips : 0;
    UINT requested = tiling_count ? *tiling_count : 0;
    UINT written = 0, tile = 0;

    for (UINT layer = 0; layer < layers; ++layer)
    {
        for (UINT mip = 0; mip < desc.MipLevels; ++mip)
        {
            UINT subresource = layer * desc.MipLevels + mip;
            D3D12_SUBRESOURCE_TILING tiling = {};
            if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                tiling.WidthInTiles = static_cast<UINT>((desc.Width + standard.WidthInTexels - 1)
                        / standard.WidthInTexels);
                tiling.HeightInTiles = 1;
                tiling.DepthInTiles = 1;
                tiling.StartTileIndexInOverallResource = tile;
                tile += tiling.WidthInTiles;
            }
            else if (mip < standard_mips)
            {
                UINT width = max(1u, static_cast<UINT>(desc.Width >> mip));
                UINT height = max(1u, desc.Height >> mip);
                UINT depth = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                        ? max(1u, static_cast<UINT>(desc.DepthOrArraySize) >> mip) : 1;
                tiling.WidthInTiles = (width + standard.WidthInTexels - 1) / standard.WidthInTexels;
                tiling.HeightInTiles = static_cast<UINT16>((height + standard.HeightInTexels - 1)
                        / standard.HeightInTexels);
                tiling.DepthInTiles = static_cast<UINT16>((depth + standard.DepthInTexels - 1)
                        / standard.DepthInTexels);
                tiling.StartTileIndexInOverallResource = tile;
                tile += tiling.WidthInTiles * tiling.HeightInTiles * tiling.DepthInTiles;
            }
            else tiling.StartTileIndexInOverallResource = D3D12_PACKED_TILE;
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
        packed->NumStandardMips = static_cast<UINT8>(desc.MipLevels);
        packed->NumPackedMips = 0;
        packed->NumTilesForPackedMips = 0;
        packed->StartTileIndexInOverallResource = 0;
    }
    if (total_tiles) *total_tiles = tile;
    if (tiling_count) *tiling_count = written;
}
