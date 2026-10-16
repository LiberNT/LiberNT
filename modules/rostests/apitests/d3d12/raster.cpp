/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Depth bounds, programmable sample positions and conservative rasterization results
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

#define RASTER_RED 0xff0000ffu
#define RASTER_GREEN 0xff00ff00u
#define RASTER_BLACK 0xff000000u

template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, class T> struct alignas(void *) RasterSubobject
{
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
    T value;
    RasterSubobject() : type(Type), value() {}
};

struct RasterStream
{
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature *> root;
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE> vs;
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> ps;
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC> layout;
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> topology;
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> raster;
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> blend;
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, D3D12_DEPTH_STENCIL_DESC1> depth;
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> depth_format;
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> formats;
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> samples;
    RasterSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> mask;
};

struct raster_test
{
    struct d3d12_test_context context;
    ID3D12Device2 *device2;
    ID3D12GraphicsCommandList1 *list1;
    ID3D12RootSignature *root;
};

static const D3D12_INPUT_ELEMENT_DESC raster_layout[] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

static ID3D12PipelineState *CreateRasterPipeline(struct raster_test *test, bool positions,
        D3D12_CONSERVATIVE_RASTERIZATION_MODE conservative, UINT samples, DXGI_FORMAT depth_format, BOOL depth_bounds)
{
    D3D12_PIPELINE_STATE_STREAM_DESC desc;
    ID3D12PipelineState *state = NULL;
    RasterStream stream;
    HRESULT hr;

    stream.root.value = test->root;
    stream.vs.value.pShaderBytecode = positions ? vs_position_code : vs_fullscreen_code;
    stream.vs.value.BytecodeLength = positions ? vs_position_code_size : vs_fullscreen_code_size;
    stream.ps.value.pShaderBytecode = ps_green_code;
    stream.ps.value.BytecodeLength = ps_green_code_size;
    if (positions)
    {
        stream.layout.value.pInputElementDescs = raster_layout;
        stream.layout.value.NumElements = ARRAYSIZE(raster_layout);
    }
    stream.topology.value = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    stream.raster.value.FillMode = D3D12_FILL_MODE_SOLID;
    stream.raster.value.CullMode = D3D12_CULL_MODE_NONE;
    stream.raster.value.DepthClipEnable = TRUE;
    stream.raster.value.ConservativeRaster = conservative;
    stream.blend.value.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    stream.depth.value.DepthEnable = depth_bounds;
    stream.depth.value.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    stream.depth.value.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    stream.depth.value.DepthBoundsTestEnable = depth_bounds;
    stream.depth_format.value = depth_format;
    stream.formats.value.NumRenderTargets = 1;
    stream.formats.value.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    stream.samples.value.Count = samples;
    stream.mask.value = ~0u;
    desc.SizeInBytes = sizeof(stream);
    desc.pPipelineStateSubobjectStream = &stream;
    hr = test->device2->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&state));
    ok(hr == S_OK, "pipeline (conservative %u, samples %u, depth bounds %d): %#lx\n", conservative, samples,
            depth_bounds, hr);
    return state;
}

static BOOL InitRasterTest(struct raster_test *test)
{
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    ID3DBlob *blob = NULL;
    HRESULT hr;

    ZeroMemory(test, sizeof(*test));
    if (!d3d12_test_init(&test->context)) return FALSE;
    test->context.device->QueryInterface(__uuidof(ID3D12Device2), reinterpret_cast<void **>(&test->device2));
    test->context.list->QueryInterface(__uuidof(ID3D12GraphicsCommandList1), reinterpret_cast<void **>(&test->list1));
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, NULL);
    if (SUCCEEDED(hr))
    {
        hr = test->context.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                __uuidof(ID3D12RootSignature), reinterpret_cast<void **>(&test->root));
        blob->Release();
    }
    ok(hr == S_OK && test->device2 && test->list1, "raster test setup: %#lx\n", hr);
    return test->device2 && test->list1 && test->root;
}

static void DestroyRasterTest(struct raster_test *test)
{
    ok(test->context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            test->context.device->GetDeviceRemovedReason());
    if (test->root) test->root->Release();
    if (test->list1) test->list1->Release();
    if (test->device2) test->device2->Release();
    d3d12_test_destroy(&test->context);
}

static ID3D12Resource *CreateVertexBuffer(ID3D12Device *device, const float *data, UINT size)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *buffer = NULL;
    void *mapped;

    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&buffer))))
        return NULL;
    if (SUCCEEDED(buffer->Map(0, NULL, &mapped)))
    {
        memcpy(mapped, data, size);
        buffer->Unmap(0, NULL);
    }
    return buffer;
}

static float ClipX(float pixel)
{
    return -1.0f + 2.0f * pixel / D3D12_TEST_SIZE;
}

static float ClipY(float pixel)
{
    return 1.0f - 2.0f * pixel / D3D12_TEST_SIZE;
}

static void BeginPass(struct raster_test *test, ID3D12PipelineState *state, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        const D3D12_CPU_DESCRIPTOR_HANDLE *dsv, const float *clear)
{
    ID3D12GraphicsCommandList *list = test->context.list;
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)D3D12_TEST_SIZE, (float)D3D12_TEST_SIZE, 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, (LONG)D3D12_TEST_SIZE, (LONG)D3D12_TEST_SIZE };

    test->context.allocator->Reset();
    list->Reset(test->context.allocator, NULL);
    list->ClearRenderTargetView(rtv, clear, 0, NULL);
    list->OMSetRenderTargets(1, &rtv, FALSE, dsv);
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetGraphicsRootSignature(test->root);
    list->SetPipelineState(state);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

static void TestDepthBounds(struct raster_test *test)
{
    static const float red[] = { 1.0f, 0.0f, 0.0f, 1.0f };
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    D3D12_CLEAR_VALUE clear = {};
    ID3D12DescriptorHeap *dsv_heap = NULL;
    ID3D12Resource *target, *depth = NULL;
    ID3D12PipelineState *state;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(&test->context, 0), dsv;
    DWORD texel = 0;
    const float bounds[2][2] = { { 0.6f, 1.0f }, { 0.0f, 1.0f } };
    const DWORD expected[2] = { RASTER_RED, RASTER_GREEN };
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = D3D12_TEST_SIZE;
    desc.Height = D3D12_TEST_SIZE;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 0.5f;
    hr = test->context.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&depth));
    ok(hr == S_OK, "depth buffer: %#lx\n", hr);
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap_desc.NumDescriptors = 1;
    hr = test->context.device->CreateDescriptorHeap(&heap_desc, __uuidof(ID3D12DescriptorHeap),
            reinterpret_cast<void **>(&dsv_heap));
    ok(hr == S_OK, "DSV heap: %#lx\n", hr);
    target = d3d12_test_create_target(test->context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
    state = CreateRasterPipeline(test, false, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF, 1, DXGI_FORMAT_D32_FLOAT, TRUE);
    if (depth && dsv_heap && target && state)
    {
        dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
        test->context.device->CreateDepthStencilView(depth, NULL, dsv);
        test->context.device->CreateRenderTargetView(target, NULL, rtv);
        for (UINT pass = 0; pass < 2; ++pass)
        {
            BeginPass(test, state, rtv, &dsv, red);
            test->context.list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);
            test->list1->OMSetDepthBounds(bounds[pass][0], bounds[pass][1]);
            test->context.list->DrawInstanced(3, 1, 0, 0);
            ok(d3d12_test_execute(&test->context, test->context.list, D3D12_TEST_WAIT), "depth bounds pass %u\n", pass);
            ok(d3d12_test_read_texel(&test->context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 7, 9, &texel),
                    "readback failed\n");
            ok(texel == expected[pass], "depth bounds %.1f..%.1f over depth 0.5: texel %#lx, expected %#lx\n",
                    bounds[pass][0], bounds[pass][1], texel, expected[pass]);
        }
    }
    if (state) state->Release();
    if (target) target->Release();
    if (dsv_heap) dsv_heap->Release();
    if (depth) depth->Release();
}

static void TestSamplePositions(struct raster_test *test)
{
    static const float black[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const float x0 = ClipX(0.5f), x1 = ClipX((float)D3D12_TEST_SIZE), y0 = ClipY(0.0f), y1 = ClipY((float)D3D12_TEST_SIZE);
    const float quad[] = { x0, y0, x1, y0, x0, y1, x1, y0, x1, y1, x0, y1 };
    const INT8 offsets[2] = { 7, -8 };
    const DWORD expected[2] = { RASTER_GREEN, RASTER_BLACK };
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    D3D12_CLEAR_VALUE clear = {};
    D3D12_VERTEX_BUFFER_VIEW view = {};
    D3D12_SAMPLE_POSITION positions[4];
    ID3D12Resource *msaa = NULL, *target, *vertices;
    ID3D12PipelineState *state;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(&test->context, 1);
    DWORD texel = 0;
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = D3D12_TEST_SIZE;
    desc.Height = D3D12_TEST_SIZE;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 4;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    clear.Format = desc.Format;
    hr = test->context.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&msaa));
    ok(hr == S_OK, "4x render target: %#lx\n", hr);
    target = d3d12_test_create_target(test->context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
    vertices = CreateVertexBuffer(test->context.device, quad, sizeof(quad));
    state = CreateRasterPipeline(test, true, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF, 4, DXGI_FORMAT_UNKNOWN, FALSE);
    if (msaa && target && vertices && state)
    {
        test->context.device->CreateRenderTargetView(msaa, NULL, rtv);
        view.BufferLocation = vertices->GetGPUVirtualAddress();
        view.SizeInBytes = sizeof(quad);
        view.StrideInBytes = 2 * sizeof(float);
        for (UINT pass = 0; pass < 2; ++pass)
        {
            for (UINT i = 0; i < ARRAYSIZE(positions); ++i)
            {
                positions[i].X = offsets[pass];
                positions[i].Y = static_cast<INT8>(i * 4 - 6);
            }
            BeginPass(test, state, rtv, NULL, black);
            test->context.list->IASetVertexBuffers(0, 1, &view);
            test->list1->SetSamplePositions(4, 1, positions);
            test->context.list->DrawInstanced(6, 1, 0, 0);
            test->list1->SetSamplePositions(0, 0, NULL);
            d3d12_test_transition(test->context.list, msaa, D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            d3d12_test_transition(test->context.list, target, D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RESOLVE_DEST);
            test->context.list->ResolveSubresource(target, 0, msaa, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
            d3d12_test_transition(test->context.list, target, D3D12_RESOURCE_STATE_RESOLVE_DEST,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
            d3d12_test_transition(test->context.list, msaa, D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
            ok(d3d12_test_execute(&test->context, test->context.list, D3D12_TEST_WAIT), "sample positions pass %u\n",
                    pass);
            ok(d3d12_test_read_texel(&test->context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, 9, &texel),
                    "readback failed\n");
            ok(texel == expected[pass], "samples at x offset %d/16 against an edge at the pixel center: texel %#lx, "
                    "expected %#lx\n", offsets[pass], texel, expected[pass]);
        }
    }
    if (state) state->Release();
    if (vertices) vertices->Release();
    if (target) target->Release();
    if (msaa) msaa->Release();
}

static void TestConservative(struct raster_test *test)
{
    static const float red[] = { 1.0f, 0.0f, 0.0f, 1.0f };
    const float triangle[] = { ClipX(4.6f), ClipY(4.1f), ClipX(4.9f), ClipY(4.1f), ClipX(4.6f), ClipY(4.9f) };
    const D3D12_CONSERVATIVE_RASTERIZATION_MODE modes[2] =
            { D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF, D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON };
    const DWORD expected[2] = { RASTER_RED, RASTER_GREEN };
    D3D12_VERTEX_BUFFER_VIEW view = {};
    ID3D12Resource *target, *vertices;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(&test->context, 2);
    DWORD texel = 0, far_texel = 0;

    target = d3d12_test_create_target(test->context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
    vertices = CreateVertexBuffer(test->context.device, triangle, sizeof(triangle));
    if (target && vertices)
    {
        test->context.device->CreateRenderTargetView(target, NULL, rtv);
        view.BufferLocation = vertices->GetGPUVirtualAddress();
        view.SizeInBytes = sizeof(triangle);
        view.StrideInBytes = 2 * sizeof(float);
        for (UINT pass = 0; pass < 2; ++pass)
        {
            ID3D12PipelineState *state = CreateRasterPipeline(test, true, modes[pass], 1, DXGI_FORMAT_UNKNOWN, FALSE);

            if (!state) continue;
            BeginPass(test, state, rtv, NULL, red);
            test->context.list->IASetVertexBuffers(0, 1, &view);
            test->context.list->DrawInstanced(3, 1, 0, 0);
            ok(d3d12_test_execute(&test->context, test->context.list, D3D12_TEST_WAIT), "conservative pass %u\n", pass);
            ok(d3d12_test_read_texel(&test->context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel),
                    "readback failed\n");
            ok(d3d12_test_read_texel(&test->context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 12, 12, &far_texel),
                    "readback failed\n");
            ok(texel == expected[pass], "triangle inside pixel (4,4) away from its center, conservative %u: texel %#lx, "
                    "expected %#lx\n", modes[pass], texel, expected[pass]);
            ok(far_texel == RASTER_RED, "far pixel %#lx\n", far_texel);
            state->Release();
        }
    }
    if (vertices) vertices->Release();
    if (target) target->Release();
}

static void RunRaster(void *)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2 = {};
    struct raster_test test;

    if (!InitRasterTest(&test))
    {
        if (test.context.device) DestroyRasterTest(&test);
        return;
    }
    test.context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    test.context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &options2, sizeof(options2));
    trace("depth bounds %d, sample positions tier %u, conservative tier %u\n", options2.DepthBoundsTestSupported,
            options2.ProgrammableSamplePositionsTier, options.ConservativeRasterizationTier);
    if (options2.DepthBoundsTestSupported) TestDepthBounds(&test);
    else skip("no depth bounds test\n");
    if (options2.ProgrammableSamplePositionsTier) TestSamplePositions(&test);
    else skip("no programmable sample positions\n");
    if (options.ConservativeRasterizationTier) TestConservative(&test);
    else skip("no conservative rasterization\n");
    DestroyRasterTest(&test);
}

START_TEST(raster)
{
    d3d12_test_bounded("raster", RunRaster, NULL, D3D12_TEST_BUDGET);
}
