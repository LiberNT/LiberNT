/*
 * PROJECT:     LiberNT Direct3D 12 runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ID3D12Device1 to ID3D12Device5 methods
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "native12.h"
#include "native12_device.h"
#include "native12_objects.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d12);

HRESULT STDMETHODCALLTYPE Native12Device::CreatePipelineLibrary(const void *, SIZE_T, REFIID, void **out)
{
    if (out) *out = NULL;
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT STDMETHODCALLTYPE Native12Device::SetEventOnMultipleFenceCompletion(ID3D12Fence *const *fences,
        const UINT64 *values, UINT count, D3D12_MULTIPLE_FENCE_WAIT_FLAGS flags, HANDLE event)
{
    if (flags & ~D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY) return E_INVALIDARG;
    if (!count || !fences || !values) return E_INVALIDARG;
    if (!kernel_callbacks.pfnWaitForSynchronizationObjectFromCpuCb) return E_NOTIMPL;
    D3DKMT_HANDLE *handles = static_cast<D3DKMT_HANDLE *>(HeapAlloc(GetProcessHeap(), 0, count * sizeof(*handles)));
    if (!handles) return E_OUTOFMEMORY;
    for (UINT i = 0; i < count; ++i) handles[i] = static_cast<Native12Fence *>(fences[i])->sync;
    D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU wait = {};
    wait.ObjectCount = count;
    wait.ObjectHandleArray = handles;
    wait.FenceValueArray = values;
    wait.hAsyncEvent = event;
    wait.Flags.WaitAny = !!(flags & D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY);
    HRESULT hr = kernel_callbacks.pfnWaitForSynchronizationObjectFromCpuCb(runtime_device, &wait);
    HeapFree(GetProcessHeap(), 0, handles);
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::SetResidencyPriority(UINT count, ID3D12Pageable *const *objects,
        const D3D12_RESIDENCY_PRIORITY *priorities)
{
    if (count && (!objects || !priorities)) return E_INVALIDARG;
    return S_OK;
}

static SIZE_T Native12AlignStream(SIZE_T value, SIZE_T alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

HRESULT STDMETHODCALLTYPE Native12Device::CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
        REFIID iid, void **out)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics = {};
    D3D12_SHADER_BYTECODE compute_shader = {}, amplification = {}, mesh = {};
    D3D12_DEPTH_STENCIL_DESC1 depth = {};
    D3D12_RT_FORMAT_ARRAY targets = {};
    D3D12_VIEW_INSTANCING_DESC view_instancing = {};
    D3D12_PIPELINE_STATE_FLAGS flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    D3D12_CACHED_PIPELINE_STATE cached = {};
    UINT node_mask = 0;
    UINT64 seen = 0;

    if (!desc || !out || (desc->SizeInBytes && !desc->pPipelineStateSubobjectStream)) return E_INVALIDARG;
    *out = NULL;

    graphics.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    graphics.SampleDesc.Count = 1;
    graphics.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphics.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    graphics.RasterizerState.DepthClipEnable = TRUE;
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        D3D12_RENDER_TARGET_BLEND_DESC &target = graphics.BlendState.RenderTarget[i];
        target.SrcBlend = target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlend = target.DestBlendAlpha = D3D12_BLEND_ZERO;
        target.BlendOp = target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        target.LogicOp = D3D12_LOGIC_OP_NOOP;
        target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depth.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depth.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    D3D12_DEPTH_STENCILOP_DESC *faces[2] = { &depth.FrontFace, &depth.BackFace };
    for (UINT i = 0; i < 2; ++i)
    {
        faces[i]->StencilFailOp = faces[i]->StencilDepthFailOp = faces[i]->StencilPassOp = D3D12_STENCIL_OP_KEEP;
        faces[i]->StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    }

    const BYTE *stream = static_cast<const BYTE *>(desc->pPipelineStateSubobjectStream);
    SIZE_T position = 0;
    while (position < desc->SizeInBytes)
    {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
        if (desc->SizeInBytes - position < sizeof(type)) return E_INVALIDARG;
        memcpy(&type, stream + position, sizeof(type));
        void *target;
        SIZE_T size, alignment;
#define NATIVE12_SUBOBJECT(Type, Field, Payload) \
        case Type: target = &(Field); size = sizeof(Payload); alignment = alignof(Payload); break;
        switch (type)
        {
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, graphics.pRootSignature, ID3D12RootSignature *)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, graphics.VS, D3D12_SHADER_BYTECODE)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, graphics.PS, D3D12_SHADER_BYTECODE)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, graphics.DS, D3D12_SHADER_BYTECODE)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, graphics.HS, D3D12_SHADER_BYTECODE)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, graphics.GS, D3D12_SHADER_BYTECODE)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, compute_shader, D3D12_SHADER_BYTECODE)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT, graphics.StreamOutput, D3D12_STREAM_OUTPUT_DESC)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, graphics.BlendState, D3D12_BLEND_DESC)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, graphics.SampleMask, UINT)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, graphics.RasterizerState, D3D12_RASTERIZER_DESC)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, depth, D3D12_DEPTH_STENCIL_DESC)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, graphics.InputLayout, D3D12_INPUT_LAYOUT_DESC)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE, graphics.IBStripCutValue, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, graphics.PrimitiveTopologyType, D3D12_PRIMITIVE_TOPOLOGY_TYPE)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, targets, D3D12_RT_FORMAT_ARRAY)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, graphics.DSVFormat, DXGI_FORMAT)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, graphics.SampleDesc, DXGI_SAMPLE_DESC)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK, node_mask, UINT)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO, cached, D3D12_CACHED_PIPELINE_STATE)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, flags, D3D12_PIPELINE_STATE_FLAGS)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, depth, D3D12_DEPTH_STENCIL_DESC1)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING, view_instancing, D3D12_VIEW_INSTANCING_DESC)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, amplification, D3D12_SHADER_BYTECODE)
            NATIVE12_SUBOBJECT(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, mesh, D3D12_SHADER_BYTECODE)
            default:
                WARN("Unsupported pipeline state subobject type %#x.\n", type);
                return E_INVALIDARG;
        }
#undef NATIVE12_SUBOBJECT
        if (seen & (1ull << type)) return E_INVALIDARG;
        seen |= 1ull << type;
        SIZE_T offset = Native12AlignStream(position + sizeof(type), alignment);
        if (offset > desc->SizeInBytes || desc->SizeInBytes - offset < size) return E_INVALIDARG;
        memcpy(target, stream + offset, size);
        position = Native12AlignStream(offset + size, sizeof(void *));
    }

    if (view_instancing.ViewInstanceCount || amplification.BytecodeLength || mesh.BytecodeLength)
        return E_INVALIDARG;
    if (targets.NumRenderTargets > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) return E_INVALIDARG;

    Native12PipelineState *pipeline = new Native12PipelineState(this);
    if (!pipeline) return E_OUTOFMEMORY;
    HRESULT hr;
    if (compute_shader.BytecodeLength)
    {
        if (graphics.VS.BytecodeLength)
        {
            pipeline->Release();
            return E_INVALIDARG;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC compute = {};
        compute.pRootSignature = graphics.pRootSignature;
        compute.CS = compute_shader;
        compute.NodeMask = node_mask;
        compute.CachedPSO = cached;
        compute.Flags = flags;
        hr = pipeline->InitializeCompute(&compute);
    }
    else
    {
        graphics.NumRenderTargets = targets.NumRenderTargets;
        memcpy(graphics.RTVFormats, targets.RTFormats, sizeof(graphics.RTVFormats));
        memcpy(&graphics.DepthStencilState, &depth, sizeof(graphics.DepthStencilState));
        graphics.NodeMask = node_mask;
        graphics.CachedPSO = cached;
        graphics.Flags = flags;
        pipeline->depth_bounds_test = depth.DepthBoundsTestEnable;
        hr = pipeline->InitializeGraphics(&graphics);
    }
    if (SUCCEEDED(hr)) hr = pipeline->QueryInterface(iid, out);
    pipeline->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::OpenExistingHeapFromAddress(const void *, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Native12Device::OpenExistingHeapFromFileMapping(HANDLE, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Native12Device::EnqueueMakeResident(D3D12_RESIDENCY_FLAGS flags, UINT count,
        ID3D12Pageable *const *objects, ID3D12Fence *fence, UINT64 value)
{
    if ((flags & ~D3D12_RESIDENCY_FLAG_DENY_OVERBUDGET) || (count && !objects) || !fence) return E_INVALIDARG;
    HRESULT hr = MakeResident(count, objects);
    if (FAILED(hr)) return hr;
    return fence->Signal(value);
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateCommandList1(UINT, D3D12_COMMAND_LIST_TYPE type,
        D3D12_COMMAND_LIST_FLAGS flags, REFIID iid, void **out)
{
    if (!out) return E_INVALIDARG;
    *out = NULL;
    if (flags != D3D12_COMMAND_LIST_FLAG_NONE) return E_INVALIDARG;
    Native12CommandList *list = new Native12CommandList(this);
    if (!list) return E_OUTOFMEMORY;
    HRESULT hr = list->Initialize(type, NULL, NULL);
    if (SUCCEEDED(hr)) hr = list->QueryInterface(iid, out);
    list->Release();
    return hr;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateProtectedResourceSession(const D3D12_PROTECTED_RESOURCE_SESSION_DESC *,
        REFIID, void **)
{
    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateCommittedResource1(const D3D12_HEAP_PROPERTIES *properties,
        D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES state,
        const D3D12_CLEAR_VALUE *clear, ID3D12ProtectedResourceSession *session, REFIID iid, void **out)
{
    if (session)
    {
        if (out) *out = NULL;
        return E_INVALIDARG;
    }
    return CreateCommittedResource(properties, flags, desc, state, clear, iid, out);
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateHeap1(const D3D12_HEAP_DESC *desc,
        ID3D12ProtectedResourceSession *session, REFIID iid, void **out)
{
    if (session)
    {
        if (out) *out = NULL;
        return E_INVALIDARG;
    }
    return CreateHeap(desc, iid, out);
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateReservedResource1(const D3D12_RESOURCE_DESC *desc,
        D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear, ID3D12ProtectedResourceSession *session,
        REFIID iid, void **out)
{
    if (session)
    {
        if (out) *out = NULL;
        return E_INVALIDARG;
    }
    return CreateReservedResource(desc, state, clear, iid, out);
}

D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE Native12Device::GetResourceAllocationInfo1(
        D3D12_RESOURCE_ALLOCATION_INFO *result, UINT visible_mask, UINT count, const D3D12_RESOURCE_DESC *descs,
        D3D12_RESOURCE_ALLOCATION_INFO1 *infos)
{
    UINT64 offset = 0, alignment = 0;
    for (UINT i = 0; i < count; ++i)
    {
        D3D12_RESOURCE_ALLOCATION_INFO info;
        GetResourceAllocationInfo(&info, visible_mask, 1, &descs[i]);
        if (info.SizeInBytes == ~(UINT64)0)
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

HRESULT STDMETHODCALLTYPE Native12Device::CreateLifetimeTracker(ID3D12LifetimeOwner *, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

void STDMETHODCALLTYPE Native12Device::RemoveDevice()
{
    if (SUCCEEDED(removed_reason)) removed_reason = DXGI_ERROR_DEVICE_REMOVED;
}

HRESULT STDMETHODCALLTYPE Native12Device::EnumerateMetaCommands(UINT *count, D3D12_META_COMMAND_DESC *)
{
    if (!count) return E_INVALIDARG;
    *count = 0;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Native12Device::EnumerateMetaCommandParameters(REFGUID, D3D12_META_COMMAND_PARAMETER_STAGE,
        UINT *, UINT *, D3D12_META_COMMAND_PARAMETER_DESC *)
{
    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateMetaCommand(REFGUID, UINT, const void *, SIZE_T, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_INVALIDARG;
}

HRESULT STDMETHODCALLTYPE Native12Device::CreateStateObject(const D3D12_STATE_OBJECT_DESC *, REFIID, void **out)
{
    if (out) *out = NULL;
    return E_NOTIMPL;
}

void STDMETHODCALLTYPE Native12Device::GetRaytracingAccelerationStructurePrebuildInfo(
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO *info)
{
    if (info) ZeroMemory(info, sizeof(*info));
}

D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS STDMETHODCALLTYPE Native12Device::CheckDriverMatchingIdentifier(
        D3D12_SERIALIZED_DATA_TYPE, const D3D12_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER *)
{
    return D3D12_DRIVER_MATCHING_IDENTIFIER_UNSUPPORTED_TYPE;
}
