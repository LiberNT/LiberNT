/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pipeline libraries, cached pipeline state blobs and the shader cache capabilities
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static void FillDesc(struct d3d12_test_context *context, D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, DXGI_FORMAT format)
{
    memset(desc, 0, sizeof(*desc));
    desc->pRootSignature = context->root_signature;
    desc->VS.pShaderBytecode = vs_fullscreen_code;
    desc->VS.BytecodeLength = vs_fullscreen_code_size;
    desc->PS.pShaderBytecode = ps_green_code;
    desc->PS.BytecodeLength = ps_green_code_size;
    desc->BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc->SampleMask = ~0u;
    desc->RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc->RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc->RasterizerState.DepthClipEnable = TRUE;
    desc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc->NumRenderTargets = 1;
    desc->RTVFormats[0] = format;
    desc->SampleDesc.Count = 1;
}

static BOOL DrawsGreen(struct d3d12_test_context *context, ID3D12PipelineState *pipeline, ID3D12Resource *target)
{
    static const FLOAT black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    DWORD texel = 0;

    context->allocator->Reset();
    context->list->Reset(context->allocator, NULL);
    context->device->CreateRenderTargetView(target, NULL, rtv);
    context->list->ClearRenderTargetView(rtv, black, 0, NULL);
    d3d12_test_bind_target(context, context->list, 1, &rtv);
    context->list->SetGraphicsRootSignature(context->root_signature);
    context->list->SetPipelineState(pipeline);
    context->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->list->DrawInstanced(3, 1, 0, 0);
    if (!d3d12_test_execute(context, context->list, D3D12_TEST_WAIT)) return FALSE;
    if (!d3d12_test_read_texel(context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel)) return FALSE;
    return texel == 0xff00ff00u;
}

static void TestCachedBlob(struct d3d12_test_context *context, ID3D12PipelineState *pipeline, ID3D12Resource *target)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
    ID3D12PipelineState *cached = NULL;
    ID3DBlob *blob = NULL;
    BYTE *corrupt;
    HRESULT hr;

    hr = pipeline->GetCachedBlob(&blob);
    ok(hr == S_OK && blob && blob->GetBufferSize(), "GetCachedBlob: %#lx, %p\n", hr, blob);
    if (!blob) return;

    FillDesc(context, &desc, DXGI_FORMAT_R8G8B8A8_UNORM);
    desc.CachedPSO.pCachedBlob = blob->GetBufferPointer();
    desc.CachedPSO.CachedBlobSizeInBytes = blob->GetBufferSize();
    hr = context->device->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void **>(&cached));
    ok(hr == S_OK, "pipeline from its cached blob: %#lx\n", hr);
    if (cached)
    {
        ok(DrawsGreen(context, cached, target), "pipeline from its cached blob does not draw\n");
        cached->Release();
        cached = NULL;
    }

    FillDesc(context, &desc, DXGI_FORMAT_R16G16B16A16_FLOAT);
    desc.CachedPSO.pCachedBlob = blob->GetBufferPointer();
    desc.CachedPSO.CachedBlobSizeInBytes = blob->GetBufferSize();
    hr = context->device->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void **>(&cached));
    ok(hr == E_INVALIDARG, "cached blob of another description: %#lx\n", hr);
    if (cached)
    {
        cached->Release();
        cached = NULL;
    }

    corrupt = static_cast<BYTE *>(HeapAlloc(GetProcessHeap(), 0, blob->GetBufferSize()));
    if (corrupt)
    {
        memcpy(corrupt, blob->GetBufferPointer(), blob->GetBufferSize());
        corrupt[blob->GetBufferSize() - 1] ^= 0x5a;
        FillDesc(context, &desc, DXGI_FORMAT_R8G8B8A8_UNORM);
        desc.CachedPSO.pCachedBlob = corrupt;
        desc.CachedPSO.CachedBlobSizeInBytes = blob->GetBufferSize();
        hr = context->device->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(&cached));
        ok(hr == E_INVALIDARG, "corrupted cached blob: %#lx\n", hr);
        if (cached) cached->Release();
        HeapFree(GetProcessHeap(), 0, corrupt);
    }
    blob->Release();
}

static void TestLibrary(struct d3d12_test_context *context, ID3D12Device1 *device, ID3D12PipelineState *pipeline,
        ID3D12Resource *target)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc, other;
    ID3D12PipelineLibrary *library = NULL, *reloaded = NULL;
    ID3D12PipelineState *loaded = NULL, *again = NULL;
    SIZE_T size;
    BYTE *data;
    HRESULT hr;

    hr = device->CreatePipelineLibrary(NULL, 0, __uuidof(ID3D12PipelineLibrary), NULL);
    ok(hr == S_FALSE, "CreatePipelineLibrary without an output: %#lx\n", hr);
    hr = device->CreatePipelineLibrary(NULL, 0, __uuidof(ID3D12PipelineLibrary), reinterpret_cast<void **>(&library));
    ok(hr == S_OK && library, "CreatePipelineLibrary: %#lx\n", hr);
    if (!library) return;

    FillDesc(context, &desc, DXGI_FORMAT_R8G8B8A8_UNORM);
    FillDesc(context, &other, DXGI_FORMAT_R16G16B16A16_FLOAT);
    hr = library->StorePipeline(L"green", pipeline);
    ok(hr == S_OK, "StorePipeline: %#lx\n", hr);
    hr = library->StorePipeline(L"green", pipeline);
    ok(hr == E_INVALIDARG, "StorePipeline with a used name: %#lx\n", hr);
    hr = library->StorePipeline(NULL, pipeline);
    ok(hr == E_INVALIDARG, "StorePipeline without a name: %#lx\n", hr);

    hr = library->LoadGraphicsPipeline(L"green", &desc, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void **>(&loaded));
    ok(hr == S_OK && loaded == pipeline, "LoadGraphicsPipeline of a stored pipeline: %#lx, %p, %p\n", hr, loaded,
            pipeline);
    if (loaded) loaded->Release();
    loaded = NULL;
    hr = library->LoadGraphicsPipeline(L"blue", &desc, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void **>(&loaded));
    ok(hr == E_INVALIDARG && !loaded, "LoadGraphicsPipeline of an unknown name: %#lx\n", hr);
    hr = library->LoadGraphicsPipeline(L"green", &other, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void **>(&loaded));
    ok(hr == E_INVALIDARG && !loaded, "LoadGraphicsPipeline with another description: %#lx\n", hr);
    if (loaded) loaded->Release();
    loaded = NULL;

    size = library->GetSerializedSize();
    ok(size > 0, "GetSerializedSize: %Iu\n", size);
    data = static_cast<BYTE *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1));
    if (data && size)
    {
        hr = library->Serialize(data, size - 1);
        ok(hr == E_INVALIDARG, "Serialize into a short buffer: %#lx\n", hr);
        hr = library->Serialize(data, size);
        ok(hr == S_OK, "Serialize: %#lx\n", hr);

        hr = device->CreatePipelineLibrary(data, size, __uuidof(ID3D12PipelineLibrary),
                reinterpret_cast<void **>(&reloaded));
        ok(hr == S_OK && reloaded, "CreatePipelineLibrary from a serialized library: %#lx\n", hr);
        if (reloaded)
        {
            hr = reloaded->LoadGraphicsPipeline(L"green", &desc, __uuidof(ID3D12PipelineState),
                    reinterpret_cast<void **>(&loaded));
            ok(hr == S_OK && loaded && loaded != pipeline, "LoadGraphicsPipeline after a reload: %#lx, %p\n", hr,
                    loaded);
            if (loaded)
            {
                ok(DrawsGreen(context, loaded, target), "reloaded pipeline does not draw\n");
                hr = reloaded->LoadGraphicsPipeline(L"green", &desc, __uuidof(ID3D12PipelineState),
                        reinterpret_cast<void **>(&again));
                ok(hr == S_OK && again == loaded, "second load after a reload: %#lx, %p, %p\n", hr, again, loaded);
                if (again) again->Release();
                loaded->Release();
            }
            hr = reloaded->LoadGraphicsPipeline(L"green", &other, __uuidof(ID3D12PipelineState),
                    reinterpret_cast<void **>(&loaded));
            ok(hr == E_INVALIDARG, "LoadGraphicsPipeline after a reload with another description: %#lx\n", hr);
            reloaded->Release();
            reloaded = NULL;
        }

        hr = device->CreatePipelineLibrary(data, 16, __uuidof(ID3D12PipelineLibrary),
                reinterpret_cast<void **>(&reloaded));
        ok(hr == E_INVALIDARG && !reloaded, "CreatePipelineLibrary from a truncated blob: %#lx\n", hr);
        if (reloaded) reloaded->Release();
        reloaded = NULL;
        data[size - 1] ^= 0x5a;
        hr = device->CreatePipelineLibrary(data, size, __uuidof(ID3D12PipelineLibrary),
                reinterpret_cast<void **>(&reloaded));
        ok(hr == E_INVALIDARG && !reloaded, "CreatePipelineLibrary from a corrupted blob: %#lx\n", hr);
        if (reloaded) reloaded->Release();
    }
    HeapFree(GetProcessHeap(), 0, data);
    library->Release();
}

static void RunPipelineLibrary(void *)
{
    D3D12_FEATURE_DATA_SHADER_CACHE caps = {};
    struct d3d12_test_context context;
    ID3D12PipelineState *pipeline;
    ID3D12Device1 *device = NULL;
    ID3D12Resource *target;
    UINT64 large = 0;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "empty list did not complete\n");
    hr = context.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_CACHE, &caps, sizeof(caps));
    ok(hr == S_OK, "SHADER_CACHE: %#lx\n", hr);
    trace("shader cache support %#x\n", caps.SupportFlags);
    ok(caps.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_SINGLE_PSO, "no single pipeline state cache\n");
    hr = context.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_CACHE, &large, sizeof(large));
    ok(hr == E_INVALIDARG, "SHADER_CACHE with a wrong size: %#lx\n", hr);

    pipeline = d3d12_test_create_pipeline(&context, ps_green_code, ps_green_code_size, 1, NULL);
    target = d3d12_test_create_target(context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
    if (pipeline && target)
    {
        TestCachedBlob(&context, pipeline, target);
        context.device->QueryInterface(__uuidof(ID3D12Device1), reinterpret_cast<void **>(&device));
        if (device && (caps.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_LIBRARY))
            TestLibrary(&context, device, pipeline, target);
        else skip("pipeline libraries are not supported\n");
        if (device) device->Release();
    }
    ok(context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            context.device->GetDeviceRemovedReason());
    if (target) target->Release();
    if (pipeline) pipeline->Release();
    d3d12_test_destroy(&context);
}

START_TEST(pipelib)
{
    d3d12_test_bounded("pipelib", RunPipelineLibrary, NULL, D3D12_TEST_BUDGET);
}
