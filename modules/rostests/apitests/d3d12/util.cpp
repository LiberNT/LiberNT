/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device, queue, render target and readback helpers for the Direct3D 12 tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

#include <stdlib.h>

struct d3d12_bounded_call
{
    void (*function)(void *);
    void *argument;
};

static DWORD WINAPI d3d12_bounded_thread(void *parameter)
{
    struct d3d12_bounded_call *call = static_cast<struct d3d12_bounded_call *>(parameter);
    call->function(call->argument);
    return 0;
}

BOOL d3d12_test_bounded(const char *name, void (*function)(void *), void *argument, DWORD timeout)
{
    struct d3d12_bounded_call *call = static_cast<struct d3d12_bounded_call *>(
            HeapAlloc(GetProcessHeap(), 0, sizeof(*call)));
    HANDLE thread;
    DWORD wait;

    if (!call) return FALSE;
    call->function = function;
    call->argument = argument;
    thread = CreateThread(NULL, 0, d3d12_bounded_thread, call, 0, NULL);
    ok(thread != NULL, "%s: CreateThread failed\n", name);
    if (!thread)
    {
        HeapFree(GetProcessHeap(), 0, call);
        return FALSE;
    }
    wait = WaitForSingleObject(thread, timeout);
    CloseHandle(thread);
    ok(wait == WAIT_OBJECT_0, "%s did not finish within %lu ms\n", name, timeout);
    if (wait != WAIT_OBJECT_0) return FALSE;
    HeapFree(GetProcessHeap(), 0, call);
    return TRUE;
}

ID3D12Device *d3d12_test_create_device(void)
{
    ID3D12Device *device = NULL;
    IDXGIFactory4 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    char value[16];
    HRESULT hr;

    if (GetEnvironmentVariableA("D3D12_APITEST_ADAPTER", value, sizeof(value))
            && SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void **>(&factory))))
    {
        if (FAILED(factory->EnumAdapters1(atoi(value), &adapter))) adapter = NULL;
        factory->Release();
    }
    hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void **>(&device));
    if (adapter) adapter->Release();
    if (FAILED(hr))
    {
        skip("D3D12CreateDevice failed, hr %#lx.\n", hr);
        return NULL;
    }
    return device;
}

BOOL d3d12_test_init(struct d3d12_test_context *context)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    ID3DBlob *blob = NULL;
    HRESULT hr;

    ZeroMemory(context, sizeof(*context));
    if (!(context->device = d3d12_test_create_device())) return FALSE;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = context->device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue),
            reinterpret_cast<void **>(&context->queue));
    ok(hr == S_OK, "CreateCommandQueue: %#lx\n", hr);
    hr = context->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&context->allocator));
    ok(hr == S_OK, "CreateCommandAllocator: %#lx\n", hr);
    hr = context->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context->allocator, NULL,
            __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void **>(&context->list));
    ok(hr == S_OK, "CreateCommandList: %#lx\n", hr);
    hr = context->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
            reinterpret_cast<void **>(&context->fence));
    ok(hr == S_OK, "CreateFence: %#lx\n", hr);
    context->event = CreateEventA(NULL, FALSE, FALSE, NULL);
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.NumDescriptors = 8;
    hr = context->device->CreateDescriptorHeap(&heap_desc, __uuidof(ID3D12DescriptorHeap),
            reinterpret_cast<void **>(&context->rtv_heap));
    ok(hr == S_OK, "CreateDescriptorHeap: %#lx\n", hr);
    context->rtv_increment = context->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, NULL);
    ok(hr == S_OK, "D3D12SerializeRootSignature: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = context->device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                __uuidof(ID3D12RootSignature), reinterpret_cast<void **>(&context->root_signature));
        ok(hr == S_OK, "CreateRootSignature: %#lx\n", hr);
        blob->Release();
    }
    if (!context->queue || !context->allocator || !context->list || !context->fence || !context->rtv_heap
            || !context->root_signature)
    {
        d3d12_test_destroy(context);
        return FALSE;
    }
    return TRUE;
}

void d3d12_test_destroy(struct d3d12_test_context *context)
{
    if (context->root_signature) context->root_signature->Release();
    if (context->rtv_heap) context->rtv_heap->Release();
    if (context->event) CloseHandle(context->event);
    if (context->fence) context->fence->Release();
    if (context->list) context->list->Release();
    if (context->allocator) context->allocator->Release();
    if (context->queue) context->queue->Release();
    if (context->device) context->device->Release();
    ZeroMemory(context, sizeof(*context));
}

BOOL d3d12_test_wait_queue(ID3D12Device *device, ID3D12CommandQueue *queue, DWORD timeout)
{
    ID3D12Fence *fence = NULL;
    HANDLE event;
    BOOL done = FALSE;

    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void **>(&fence))))
        return FALSE;
    event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (SUCCEEDED(queue->Signal(fence, 1)) && SUCCEEDED(fence->SetEventOnCompletion(1, event)))
        done = WaitForSingleObject(event, timeout) == WAIT_OBJECT_0;
    CloseHandle(event);
    fence->Release();
    return done;
}

BOOL d3d12_test_execute(struct d3d12_test_context *context, ID3D12GraphicsCommandList *list, DWORD timeout)
{
    ID3D12CommandList *lists[1] = { list };
    HRESULT hr = list->Close();

    ok(hr == S_OK, "Close: %#lx\n", hr);
    if (FAILED(hr)) return FALSE;
    context->queue->ExecuteCommandLists(1, lists);
    hr = context->queue->Signal(context->fence, ++context->fence_value);
    ok(hr == S_OK, "Signal: %#lx\n", hr);
    if (FAILED(hr)) return FALSE;
    if (context->fence->GetCompletedValue() >= context->fence_value) return TRUE;
    hr = context->fence->SetEventOnCompletion(context->fence_value, context->event);
    ok(hr == S_OK, "SetEventOnCompletion: %#lx\n", hr);
    return SUCCEEDED(hr) && WaitForSingleObject(context->event, timeout) == WAIT_OBJECT_0;
}

ID3D12Resource *d3d12_test_create_target(ID3D12Device *device, DXGI_FORMAT format)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    D3D12_CLEAR_VALUE clear = {};
    ID3D12Resource *resource = NULL;
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = D3D12_TEST_SIZE;
    desc.Height = D3D12_TEST_SIZE;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    clear.Format = format;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clear, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    ok(hr == S_OK, "CreateCommittedResource(render target): %#lx\n", hr);
    return resource;
}

D3D12_CPU_DESCRIPTOR_HANDLE d3d12_test_rtv(struct d3d12_test_context *context, UINT index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = context->rtv_heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * context->rtv_increment;
    return handle;
}

ID3D12PipelineState *d3d12_test_create_pipeline(struct d3d12_test_context *context, const DWORD *ps_code,
        SIZE_T ps_size, UINT target_count, const D3D12_BLEND_DESC *blend)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    ID3D12PipelineState *state = NULL;
    HRESULT hr;

    desc.pRootSignature = context->root_signature;
    desc.VS.pShaderBytecode = vs_fullscreen_code;
    desc.VS.BytecodeLength = vs_fullscreen_code_size;
    desc.PS.pShaderBytecode = ps_code;
    desc.PS.BytecodeLength = ps_size;
    if (blend) desc.BlendState = *blend;
    else desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = ~0u;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = target_count;
    for (UINT i = 0; i < target_count; ++i) desc.RTVFormats[i] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    hr = context->device->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void **>(&state));
    ok(hr == S_OK, "CreateGraphicsPipelineState: %#lx\n", hr);
    return state;
}

void d3d12_test_transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    list->ResourceBarrier(1, &barrier);
}

void d3d12_test_bind_target(struct d3d12_test_context *context, ID3D12GraphicsCommandList *list, UINT count,
        const D3D12_CPU_DESCRIPTOR_HANDLE *rtvs)
{
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (FLOAT)D3D12_TEST_SIZE, (FLOAT)D3D12_TEST_SIZE, 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, D3D12_TEST_SIZE, D3D12_TEST_SIZE };

    list->OMSetRenderTargets(count, rtvs, FALSE, NULL);
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
}

BOOL d3d12_test_read_texel(struct d3d12_test_context *context, ID3D12Resource *resource,
        D3D12_RESOURCE_STATES state, UINT x, UINT y, DWORD *value)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    ID3D12Resource *readback = NULL;
    D3D12_RANGE range = {};
    BYTE *data = NULL;
    BOOL done;
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_READBACK;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * D3D12_TEST_SIZE;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = context->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&readback));
    ok(hr == S_OK, "CreateCommittedResource(readback): %#lx\n", hr);
    if (FAILED(hr)) return FALSE;

    hr = context->allocator->Reset();
    ok(hr == S_OK, "allocator Reset: %#lx\n", hr);
    hr = context->list->Reset(context->allocator, NULL);
    ok(hr == S_OK, "list Reset: %#lx\n", hr);
    d3d12_test_transition(context->list, resource, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    dst.pResource = readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = D3D12_TEST_SIZE;
    dst.PlacedFootprint.Footprint.Height = D3D12_TEST_SIZE;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    src.pResource = resource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    context->list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
    d3d12_test_transition(context->list, resource, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
    done = d3d12_test_execute(context, context->list, D3D12_TEST_WAIT);
    ok(done, "readback did not complete\n");
    if (done)
    {
        range.End = static_cast<SIZE_T>(desc.Width);
        hr = readback->Map(0, &range, reinterpret_cast<void **>(&data));
        ok(hr == S_OK, "Map: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            *value = *reinterpret_cast<const DWORD *>(data + y * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT + x * 4);
            range.End = 0;
            readback->Unmap(0, &range);
        }
        else done = FALSE;
    }
    readback->Release();
    return done;
}
