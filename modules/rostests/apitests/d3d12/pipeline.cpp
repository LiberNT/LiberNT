/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pipeline state streams with DEPTH_STENCIL2, RASTERIZER1 and RASTERIZER2 subobjects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

struct pipeline_stream
{
    BYTE data[512];
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

static void BaseStream(struct pipeline_stream *stream, ID3D12RootSignature *root_signature, BOOL depth)
{
    D3D12_SHADER_BYTECODE vs = { vs_fullscreen_code, vs_fullscreen_code_size };
    D3D12_SHADER_BYTECODE ps = { ps_green_code, ps_green_code_size };
    D3D12_RT_FORMAT_ARRAY targets = {};

    stream->size = 0;
    targets.NumRenderTargets = 1;
    targets.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    stream->Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, root_signature);
    stream->Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, vs);
    stream->Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, ps);
    stream->Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, targets);
    stream->Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    if (depth)
        stream->Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT_D24_UNORM_S8_UINT);
}

static HRESULT CreateFromStream(ID3D12Device2 *device, struct pipeline_stream *stream)
{
    D3D12_PIPELINE_STATE_STREAM_DESC desc = { stream->size, stream->data };
    ID3D12PipelineState *pipeline = reinterpret_cast<ID3D12PipelineState *>(1);
    HRESULT hr = device->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState),
            reinterpret_cast<void **>(&pipeline));

    if (SUCCEEDED(hr))
    {
        ok(pipeline && pipeline != reinterpret_cast<ID3D12PipelineState *>(1), "no pipeline returned\n");
        if (pipeline && pipeline != reinterpret_cast<ID3D12PipelineState *>(1)) pipeline->Release();
    }
    else
        ok(!pipeline, "failed creation left %p\n", pipeline);
    return hr;
}

static D3D12_RASTERIZER_DESC Raster(void)
{
    D3D12_RASTERIZER_DESC raster = {};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_BACK;
    raster.DepthClipEnable = TRUE;
    return raster;
}

static D3D12_RASTERIZER_DESC1 Raster1(FLOAT bias)
{
    D3D12_RASTERIZER_DESC1 raster = {};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_BACK;
    raster.DepthClipEnable = TRUE;
    raster.DepthBias = bias;
    return raster;
}

static D3D12_RASTERIZER_DESC2 Raster2(D3D12_LINE_RASTERIZATION_MODE mode)
{
    D3D12_RASTERIZER_DESC2 raster = {};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_BACK;
    raster.DepthClipEnable = TRUE;
    raster.LineRasterizationMode = mode;
    return raster;
}

static D3D12_DEPTH_STENCIL_DESC Depth(void)
{
    D3D12_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    return depth;
}

static D3D12_DEPTH_STENCIL_DESC1 Depth1(void)
{
    D3D12_DEPTH_STENCIL_DESC1 depth = {};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    return depth;
}

static D3D12_DEPTH_STENCIL_DESC2 Depth2(UINT8 front, UINT8 back)
{
    D3D12_DEPTH_STENCIL_DESC2 depth = {};
    D3D12_DEPTH_STENCILOP_DESC1 *faces[2] = { &depth.FrontFace, &depth.BackFace };

    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depth.StencilEnable = TRUE;
    for (UINT i = 0; i < 2; ++i)
    {
        faces[i]->StencilFailOp = D3D12_STENCIL_OP_KEEP;
        faces[i]->StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        faces[i]->StencilPassOp = D3D12_STENCIL_OP_REPLACE;
        faces[i]->StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        faces[i]->StencilReadMask = 0xff;
        faces[i]->StencilWriteMask = i ? back : front;
    }
    return depth;
}

static void TestDynamicState(struct d3d12_test_context *context, ID3D12Device2 *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS15 options15 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};
    D3D12_PIPELINE_STATE_FLAGS flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    ID3D12GraphicsCommandList9 *list9 = NULL;
    ID3D12PipelineState *pipeline = NULL;
    struct pipeline_stream stream;
    HRESULT hr;

    context->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS15, &options15, sizeof(options15));
    context->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16));
    if (options16.DynamicDepthBiasSupported) flags |= D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS;
    if (options15.DynamicIndexBufferStripCutSupported) flags |= D3D12_PIPELINE_STATE_FLAG_DYNAMIC_INDEX_BUFFER_STRIP_CUT;
    if (!flags)
    {
        skip("no dynamic pipeline state\n");
        return;
    }
    BaseStream(&stream, context->root_signature, FALSE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, flags);
    D3D12_PIPELINE_STATE_STREAM_DESC desc = { stream.size, stream.data };
    hr = device->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&pipeline));
    ok(hr == S_OK, "dynamic pipeline: %#lx\n", hr);
    context->list->QueryInterface(__uuidof(ID3D12GraphicsCommandList9), reinterpret_cast<void **>(&list9));
    ok(list9 != NULL, "no ID3D12GraphicsCommandList9\n");
    if (pipeline && list9)
    {
        context->list->SetPipelineState(pipeline);
        if (flags & D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS)
            list9->RSSetDepthBias(2.0f, 0.0f, 1.0f);
        if (flags & D3D12_PIPELINE_STATE_FLAG_DYNAMIC_INDEX_BUFFER_STRIP_CUT)
            list9->IASetIndexBufferStripCutValue(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF);
        ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "dynamic state list did not complete\n");
    }
    if (list9) list9->Release();
    if (pipeline) pipeline->Release();
}

static void RunPipeline(void *)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 options14 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS19 options19 = {};
    struct d3d12_test_context context;
    struct pipeline_stream stream;
    ID3D12Device2 *device = NULL;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    context.device->QueryInterface(__uuidof(ID3D12Device2), reinterpret_cast<void **>(&device));
    ok(device != NULL, "no ID3D12Device2\n");
    if (!device)
    {
        d3d12_test_destroy(&context);
        return;
    }
    context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS14, &options14, sizeof(options14));
    context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS19, &options19, sizeof(options19));
    ok(options19.RasterizerDesc2Supported, "D3D12_RASTERIZER_DESC2 is not supported\n");

    BaseStream(&stream, context.root_signature, FALSE);
    ok(CreateFromStream(device, &stream) == S_OK, "base stream failed\n");

    BaseStream(&stream, context.root_signature, FALSE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1, Raster1(0.5f));
    ok(CreateFromStream(device, &stream) == S_OK, "fractional depth bias refused\n");

    for (UINT mode = D3D12_LINE_RASTERIZATION_MODE_ALIASED; mode <= D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_NARROW + 1;
            ++mode)
    {
        HRESULT expected = S_OK;

        if (mode > D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_NARROW
                || (mode == D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_NARROW
                && !options19.NarrowQuadrilateralLinesSupported))
            expected = E_INVALIDARG;
        BaseStream(&stream, context.root_signature, FALSE);
        stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2,
                Raster2(static_cast<D3D12_LINE_RASTERIZATION_MODE>(mode)));
        hr = CreateFromStream(device, &stream);
        ok(hr == expected, "line rasterization mode %u: %#lx\n", mode, hr);
    }

    BaseStream(&stream, context.root_signature, FALSE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, Raster());
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1, Raster1(1.0f));
    hr = CreateFromStream(device, &stream);
    ok(hr == E_INVALIDARG, "RASTERIZER then RASTERIZER1: %#lx\n", hr);
    BaseStream(&stream, context.root_signature, FALSE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, Raster());
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2, Raster2(D3D12_LINE_RASTERIZATION_MODE_ALIASED));
    hr = CreateFromStream(device, &stream);
    ok(hr == S_OK, "RASTERIZER then RASTERIZER2: %#lx\n", hr);
    BaseStream(&stream, context.root_signature, FALSE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1, Raster1(1.0f));
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2, Raster2(D3D12_LINE_RASTERIZATION_MODE_ALIASED));
    hr = CreateFromStream(device, &stream);
    ok(hr == S_OK, "RASTERIZER1 then RASTERIZER2: %#lx\n", hr);
    BaseStream(&stream, context.root_signature, FALSE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1, Raster1(1.0f));
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1, Raster1(1.0f));
    hr = CreateFromStream(device, &stream);
    ok(hr == S_OK, "RASTERIZER1 twice: %#lx\n", hr);

    if (options14.IndependentFrontAndBackStencilRefMaskSupported)
    {
        BaseStream(&stream, context.root_signature, TRUE);
        stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2, Depth2(0xf0, 0x0f));
        hr = CreateFromStream(device, &stream);
        ok(hr == S_OK, "DEPTH_STENCIL2 with front and back masks: %#lx\n", hr);
    }
    BaseStream(&stream, context.root_signature, TRUE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2, Depth2(0xff, 0xff));
    hr = CreateFromStream(device, &stream);
    ok(hr == S_OK, "DEPTH_STENCIL2: %#lx\n", hr);
    BaseStream(&stream, context.root_signature, TRUE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, Depth1());
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2, Depth2(0xff, 0xff));
    hr = CreateFromStream(device, &stream);
    ok(hr == S_OK, "DEPTH_STENCIL1 then DEPTH_STENCIL2: %#lx\n", hr);
    BaseStream(&stream, context.root_signature, TRUE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, Depth());
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2, Depth2(0xff, 0xff));
    hr = CreateFromStream(device, &stream);
    ok(hr == E_INVALIDARG, "DEPTH_STENCIL then DEPTH_STENCIL2: %#lx\n", hr);
    BaseStream(&stream, context.root_signature, TRUE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, Depth());
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, Depth1());
    hr = CreateFromStream(device, &stream);
    ok(hr == E_INVALIDARG, "DEPTH_STENCIL then DEPTH_STENCIL1: %#lx\n", hr);

    BaseStream(&stream, context.root_signature, FALSE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS);
    hr = CreateFromStream(device, &stream);
    ok(hr == S_OK, "dynamic depth bias flag: %#lx\n", hr);
    BaseStream(&stream, context.root_signature, FALSE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAG_DYNAMIC_INDEX_BUFFER_STRIP_CUT);
    hr = CreateFromStream(device, &stream);
    ok(hr == S_OK, "dynamic strip cut flag: %#lx\n", hr);
    BaseStream(&stream, context.root_signature, FALSE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, static_cast<D3D12_PIPELINE_STATE_FLAGS>(0x10));
    hr = CreateFromStream(device, &stream);
    ok(hr == E_INVALIDARG, "unknown flag: %#lx\n", hr);
    BaseStream(&stream, context.root_signature, FALSE);
    stream.Add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID, 0u);
    hr = CreateFromStream(device, &stream);
    ok(hr == E_INVALIDARG, "unknown subobject: %#lx\n", hr);

    TestDynamicState(&context, device);

    device->Release();
    d3d12_test_destroy(&context);
}

START_TEST(pipeline)
{
    d3d12_test_bounded("pipeline", RunPipeline, NULL, D3D12_TEST_BUDGET);
}
