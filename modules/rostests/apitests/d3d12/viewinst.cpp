/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     View instancing declarations and the view instance mask
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

struct view_stream
{
    BYTE data[1024];
    SIZE_T size;

    template <typename T> void Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type, const T &value)
    {
        SIZE_T at = (size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
        SIZE_T inner = (at + sizeof(type) + alignof(T) - 1) & ~(alignof(T) - 1);
        memcpy(data + at, &type, sizeof(type));
        memcpy(data + inner, &value, sizeof(T));
        size = (inner + sizeof(T) + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
    }
};

static HRESULT CreateShaderPipeline(struct d3d12_test_context *context, ID3D12Device2 *device,
        const D3D12_SHADER_BYTECODE &vs, const D3D12_SHADER_BYTECODE &ps, UINT count,
        const D3D12_VIEW_INSTANCE_LOCATION *locations, D3D12_VIEW_INSTANCING_FLAGS flags,
        ID3D12PipelineState **pipeline)
{
    D3D12_VIEW_INSTANCING_DESC views = { count, locations, flags };
    D3D12_RT_FORMAT_ARRAY targets = {};
    D3D12_RASTERIZER_DESC raster = {};
    D3D12_BLEND_DESC blend = {};
    DXGI_SAMPLE_DESC samples = { 1, 0 };
    struct view_stream stream = {};

    targets.NumRenderTargets = 1;
    targets.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_NONE;
    raster.DepthClipEnable = TRUE;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, context->root_signature);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, vs);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, ps);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, blend);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, ~0u);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, raster);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, targets);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, samples);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING, views);
    D3D12_PIPELINE_STATE_STREAM_DESC desc = { stream.size, stream.data };
    *pipeline = NULL;
    return device->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(pipeline));
}

static HRESULT CreateViewPipeline(struct d3d12_test_context *context, ID3D12Device2 *device, UINT count,
        const D3D12_VIEW_INSTANCE_LOCATION *locations, D3D12_VIEW_INSTANCING_FLAGS flags,
        ID3D12PipelineState **pipeline)
{
    D3D12_SHADER_BYTECODE vs = { vs_viewid_code, vs_viewid_code_size };
    D3D12_SHADER_BYTECODE ps = { ps_viewid_code, ps_viewid_code_size };

    return CreateShaderPipeline(context, device, vs, ps, count, locations, flags, pipeline);
}

static void TestVertexSize(struct d3d12_test_context *context, ID3D12Device2 *device)
{
    static const D3D12_VIEW_INSTANCE_LOCATION locations[4] = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 } };
    D3D12_SHADER_BYTECODE dependent = { vs_size_dep_code, vs_size_dep_code_size };
    D3D12_SHADER_BYTECODE flat = { vs_size_flat_code, vs_size_flat_code_size };
    D3D12_SHADER_BYTECODE ps = { ps_flat_code, ps_flat_code_size };
    ID3D12PipelineState *pipeline = NULL;
    HRESULT hr;

    hr = CreateShaderPipeline(context, device, dependent, ps, 2, locations, D3D12_VIEW_INSTANCING_FLAG_NONE, &pipeline);
    ok(hr == S_OK, "120 outputs, 4 view dependent, 2 views: %#lx\n", hr);
    if (pipeline) pipeline->Release();
    hr = CreateShaderPipeline(context, device, dependent, ps, 4, locations, D3D12_VIEW_INSTANCING_FLAG_NONE, &pipeline);
    ok(hr == E_INVALIDARG, "120 outputs, 4 view dependent, 4 views: %#lx\n", hr);
    if (pipeline) pipeline->Release();
    hr = CreateShaderPipeline(context, device, flat, ps, 4, locations, D3D12_VIEW_INSTANCING_FLAG_NONE, &pipeline);
    ok(hr == S_OK, "120 outputs, none view dependent, 4 views: %#lx\n", hr);
    if (pipeline) pipeline->Release();
}

static ID3D12Resource *CreateTargetArray(ID3D12Device *device)
{
    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *resource = NULL;

    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = D3D12_TEST_SIZE;
    desc.Height = D3D12_TEST_SIZE;
    desc.DepthOrArraySize = 2;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL,
            __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    return resource;
}

static BOOL ReadSlice(struct d3d12_test_context *context, ID3D12Resource *resource, UINT slice, DWORD *value)
{
    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_READBACK };
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *readback = NULL;
    D3D12_RANGE range = {};
    BYTE *data = NULL;
    BOOL done = FALSE;

    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * D3D12_TEST_SIZE;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(context->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&readback))))
        return FALSE;
    context->allocator->Reset();
    context->list->Reset(context->allocator, NULL);
    d3d12_test_transition(context->list, resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    dst.pResource = readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = D3D12_TEST_SIZE;
    dst.PlacedFootprint.Footprint.Height = D3D12_TEST_SIZE;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    src.pResource = resource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = slice;
    context->list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
    d3d12_test_transition(context->list, resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (d3d12_test_execute(context, context->list, D3D12_TEST_WAIT))
    {
        range.End = static_cast<SIZE_T>(desc.Width);
        if (SUCCEEDED(readback->Map(0, &range, reinterpret_cast<void **>(&data))))
        {
            *value = *reinterpret_cast<const DWORD *>(data + 4 * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT + 16);
            range.End = 0;
            readback->Unmap(0, &range);
            done = TRUE;
        }
    }
    readback->Release();
    return done;
}

static void DrawViews(struct d3d12_test_context *context, ID3D12PipelineState *pipeline, ID3D12Resource *target,
        BOOL set_mask, UINT mask, const DWORD expected[2], const char *name)
{
    static const FLOAT black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    D3D12_RENDER_TARGET_VIEW_DESC view = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    ID3D12GraphicsCommandList1 *list1 = NULL;
    DWORD texels[2] = {};

    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    view.Texture2DArray.ArraySize = 2;
    context->device->CreateRenderTargetView(target, &view, rtv);
    context->allocator->Reset();
    context->list->Reset(context->allocator, NULL);
    context->list->ClearRenderTargetView(rtv, black, 0, NULL);
    d3d12_test_bind_target(context, context->list, 1, &rtv);
    context->list->SetGraphicsRootSignature(context->root_signature);
    context->list->SetPipelineState(pipeline);
    if (set_mask)
    {
        context->list->QueryInterface(__uuidof(ID3D12GraphicsCommandList1), reinterpret_cast<void **>(&list1));
        ok(list1 != NULL, "%s: no ID3D12GraphicsCommandList1\n", name);
        if (list1)
        {
            list1->SetViewInstanceMask(mask);
            list1->Release();
        }
    }
    context->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->list->DrawInstanced(3, 1, 0, 0);
    ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "%s: draw did not complete\n", name);
    for (UINT i = 0; i < 2; ++i)
    {
        ok(ReadSlice(context, target, i, &texels[i]), "%s: readback of slice %u failed\n", name, i);
        ok(texels[i] == expected[i], "%s: slice %u got %08lx, expected %08lx\n", name, i, texels[i], expected[i]);
    }
}

static void RunViewInstancing(void *)
{
    static const D3D12_VIEW_INSTANCE_LOCATION locations[2] = { { 0, 0 }, { 0, 1 } };
    static const DWORD both[2] = { 0xff00ff00u, 0xffff0000u }, second[2] = { 0, 0xffff0000u };
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3 = {};
    ID3D12PipelineState *pipeline = NULL;
    struct d3d12_test_context context;
    ID3D12Device2 *device = NULL;
    ID3D12Resource *target;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "empty list did not complete\n");
    context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3));
    trace("view instancing tier %u\n", options3.ViewInstancingTier);
    context.device->QueryInterface(__uuidof(ID3D12Device2), reinterpret_cast<void **>(&device));
    ok(device != NULL, "no ID3D12Device2\n");
    if (!device)
    {
        d3d12_test_destroy(&context);
        return;
    }
    hr = CreateViewPipeline(&context, device, 0, NULL, static_cast<D3D12_VIEW_INSTANCING_FLAGS>(2), &pipeline);
    ok(hr == E_INVALIDARG, "no views with an unknown flag: %#lx\n", hr);
    if (pipeline) pipeline->Release();
    if (options3.ViewInstancingTier == D3D12_VIEW_INSTANCING_TIER_NOT_SUPPORTED)
    {
        hr = CreateViewPipeline(&context, device, 2, locations, D3D12_VIEW_INSTANCING_FLAG_NONE, &pipeline);
        ok(hr == E_INVALIDARG, "two views without view instancing: %#lx\n", hr);
        if (pipeline) pipeline->Release();
        hr = CreateViewPipeline(&context, device, 1, locations, D3D12_VIEW_INSTANCING_FLAG_ENABLE_VIEW_INSTANCE_MASKING,
                &pipeline);
        ok(hr == E_INVALIDARG, "masking without view instancing: %#lx\n", hr);
        if (pipeline) pipeline->Release();
        skip("view instancing is not supported\n");
        device->Release();
        d3d12_test_destroy(&context);
        return;
    }

    hr = CreateViewPipeline(&context, device, 2, locations, static_cast<D3D12_VIEW_INSTANCING_FLAGS>(2), &pipeline);
    ok(hr == E_INVALIDARG, "unknown view instancing flag: %#lx\n", hr);
    if (pipeline) pipeline->Release();

    TestVertexSize(&context, device);

    target = CreateTargetArray(context.device);
    ok(target != NULL, "render target array creation failed\n");
    hr = CreateViewPipeline(&context, device, 2, locations, D3D12_VIEW_INSTANCING_FLAG_NONE, &pipeline);
    ok(hr == S_OK, "two views: %#lx\n", hr);
    if (pipeline && target) DrawViews(&context, pipeline, target, FALSE, 0, both, "two views");
    if (pipeline) pipeline->Release();
    hr = CreateViewPipeline(&context, device, 2, locations, D3D12_VIEW_INSTANCING_FLAG_ENABLE_VIEW_INSTANCE_MASKING,
            &pipeline);
    ok(hr == S_OK, "masked views: %#lx\n", hr);
    if (pipeline && target) DrawViews(&context, pipeline, target, TRUE, 0x2, second, "mask 0x2");
    if (pipeline) pipeline->Release();
    if (target) target->Release();
    ok(context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            context.device->GetDeviceRemovedReason());
    device->Release();
    d3d12_test_destroy(&context);
}

START_TEST(viewinst)
{
    d3d12_test_bounded("viewinst", RunViewInstancing, NULL, D3D12_TEST_BUDGET);
}
