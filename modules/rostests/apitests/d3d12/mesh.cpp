/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mesh and amplification pipelines, DispatchMesh, indirect mesh and mesh statistics
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "d3d12_apitest.h"
#include "mesh_shaders.h"

#define MESH_RED 0xff0000ffu
#define MESH_GREEN 0xff00ff00u
#define MESH_BLUE 0xffff0000u
#define MESH_YELLOW 0xff00ffffu

template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename Value>
struct alignas(void *) MeshSubobject
{
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
    Value value = {};
};

struct MeshStream
{
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature *> root;
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE> ms;
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> ps;
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> raster;
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> blend;
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC> depth;
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> formats;
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC> samples;
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT> mask;
};

struct AmplificationStream
{
    MeshStream mesh;
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE> as;
};

struct VertexMeshStream
{
    MeshStream mesh;
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE> vs;
};

struct InputLayoutMeshStream
{
    MeshStream mesh;
    MeshSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC> layout;
};

static void FillStream(MeshStream *stream, ID3D12RootSignature *root, const void *ms, SIZE_T ms_size, const void *ps,
        SIZE_T ps_size)
{
    stream->root.value = root;
    stream->ms.value.pShaderBytecode = ms;
    stream->ms.value.BytecodeLength = ms_size;
    stream->ps.value.pShaderBytecode = ps;
    stream->ps.value.BytecodeLength = ps_size;
    stream->raster.value.FillMode = D3D12_FILL_MODE_SOLID;
    stream->raster.value.CullMode = D3D12_CULL_MODE_NONE;
    stream->raster.value.DepthClipEnable = TRUE;
    stream->blend.value.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    stream->depth.value.DepthEnable = FALSE;
    stream->depth.value.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    stream->formats.value.NumRenderTargets = 1;
    stream->formats.value.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    stream->samples.value.Count = 1;
    stream->mask.value = ~0u;
}

static ID3D12PipelineState *CreateFromStream(ID3D12Device2 *device, void *stream, SIZE_T size, HRESULT *result)
{
    D3D12_PIPELINE_STATE_STREAM_DESC desc = {};
    ID3D12PipelineState *state = NULL;

    desc.SizeInBytes = size;
    desc.pPipelineStateSubobjectStream = stream;
    *result = device->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&state));
    return state;
}

static void ResetList(struct d3d12_test_context *context)
{
    HRESULT hr = context->allocator->Reset();

    ok(hr == S_OK, "allocator Reset: %#lx\n", hr);
    hr = context->list->Reset(context->allocator, NULL);
    ok(hr == S_OK, "list Reset: %#lx\n", hr);
}

static void DrawAndCheck(struct d3d12_test_context *context, ID3D12GraphicsCommandList6 *list, ID3D12PipelineState *state,
        ID3D12Resource *target, UINT groups, DWORD expected, const char *what)
{
    static const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    DWORD texel = 0;

    context->list->ClearRenderTargetView(rtv, red, 0, NULL);
    d3d12_test_bind_target(context, context->list, 1, &rtv);
    context->list->SetGraphicsRootSignature(context->root_signature);
    context->list->SetPipelineState(state);
    list->DispatchMesh(groups, 1, 1);
    ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "%s: draw did not complete\n", what);
    ok(d3d12_test_read_texel(context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel), "%s: readback failed\n", what);
    ok(texel == expected, "%s: got %08lx, expected %08lx\n", what, texel, expected);
    ResetList(context);
}

static void TestIndirect(struct d3d12_test_context *context, ID3D12PipelineState *state, ID3D12Resource *target)
{
    static const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    D3D12_INDIRECT_ARGUMENT_DESC argument = {};
    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC buffer = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    ID3D12CommandSignature *signature = NULL;
    ID3D12Resource *arguments = NULL;
    UINT *mapped = NULL;
    DWORD texel = 0;
    HRESULT hr;

    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
    desc.ByteStride = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs = &argument;
    hr = context->device->CreateCommandSignature(&desc, NULL, __uuidof(ID3D12CommandSignature), reinterpret_cast<void **>(&signature));
    ok(hr == S_OK, "indirect mesh signature: %#lx\n", hr);
    if (!signature) return;

    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = context->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
            __uuidof(ID3D12Resource), reinterpret_cast<void **>(&arguments));
    ok(hr == S_OK, "indirect buffer: %#lx\n", hr);
    if (arguments && SUCCEEDED(arguments->Map(0, NULL, reinterpret_cast<void **>(&mapped))))
    {
        mapped[0] = mapped[1] = mapped[2] = 1;
        arguments->Unmap(0, NULL);
        context->list->ClearRenderTargetView(rtv, red, 0, NULL);
        d3d12_test_bind_target(context, context->list, 1, &rtv);
        context->list->SetGraphicsRootSignature(context->root_signature);
        context->list->SetPipelineState(state);
        context->list->ExecuteIndirect(signature, 1, arguments, 0, NULL, 0);
        ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "indirect mesh did not complete\n");
        ok(d3d12_test_read_texel(context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel), "indirect readback failed\n");
        ok(texel == MESH_GREEN, "indirect mesh: got %08lx\n", texel);
        ResetList(context);
    }
    if (arguments) arguments->Release();
    signature->Release();
}

static void TestStatistics(struct d3d12_test_context *context, ID3D12GraphicsCommandList6 *list, ID3D12PipelineState *state)
{
    D3D12_QUERY_DATA_PIPELINE_STATISTICS1 *data = NULL;
    D3D12_QUERY_HEAP_DESC heap_desc = {};
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC buffer = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    ID3D12QueryHeap *queries = NULL;
    ID3D12Resource *results = NULL;
    HRESULT hr;

    heap_desc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1;
    heap_desc.Count = 1;
    hr = context->device->CreateQueryHeap(&heap_desc, __uuidof(ID3D12QueryHeap), reinterpret_cast<void **>(&queries));
    ok(hr == S_OK, "statistics heap: %#lx\n", hr);
    if (!queries) return;

    heap.Type = D3D12_HEAP_TYPE_READBACK;
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = sizeof(*data);
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = context->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            __uuidof(ID3D12Resource), reinterpret_cast<void **>(&results));
    ok(hr == S_OK, "statistics buffer: %#lx\n", hr);
    if (results)
    {
        d3d12_test_bind_target(context, context->list, 1, &rtv);
        context->list->SetGraphicsRootSignature(context->root_signature);
        context->list->SetPipelineState(state);
        context->list->BeginQuery(queries, D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0);
        list->DispatchMesh(1, 1, 1);
        context->list->EndQuery(queries, D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0);
        context->list->ResolveQueryData(queries, D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0, 1, results, 0);
        ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "statistics did not complete\n");
        if (SUCCEEDED(results->Map(0, NULL, reinterpret_cast<void **>(&data))))
        {
            trace("amplification %I64u, mesh %I64u, primitives %I64u\n", data->ASInvocations, data->MSInvocations, data->MSPrimitives);
            results->Unmap(0, NULL);
        }
        results->Release();
        ResetList(context);
    }
    queries->Release();
}

static void RunMesh(void *)
{
    struct d3d12_test_context context;
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9 = {};
    ID3D12GraphicsCommandList6 *list = NULL;
    ID3D12PipelineState *state = NULL;
    ID3D12Resource *target = NULL;
    ID3D12Device2 *device = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    hr = context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
    ok(hr == S_OK, "OPTIONS7: %#lx\n", hr);
    hr = context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &options9, sizeof(options9));
    ok(hr == S_OK, "OPTIONS9: %#lx\n", hr);
    ok(!!options9.MeshShaderPipelineStatsSupported == (options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED),
       "mesh statistics %d with tier %u\n", options9.MeshShaderPipelineStatsSupported, options7.MeshShaderTier);
    if (options7.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
    {
        skip("Mesh shaders are not supported\n");
        d3d12_test_destroy(&context);
        return;
    }

    context.device->QueryInterface(__uuidof(ID3D12Device2), reinterpret_cast<void **>(&device));
    hr = context.list->QueryInterface(__uuidof(ID3D12GraphicsCommandList6), reinterpret_cast<void **>(&list));
    ok(hr == S_OK, "ID3D12GraphicsCommandList6: %#lx\n", hr);
    target = d3d12_test_create_target(context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
    rtv = d3d12_test_rtv(&context, 0);
    if (target) context.device->CreateRenderTargetView(target, NULL, rtv);

    if (device && list && target)
    {
        MeshStream stream;
        AmplificationStream amplified;
        VertexMeshStream vertex;
        InputLayoutMeshStream layout;
        static const D3D12_INPUT_ELEMENT_DESC elements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        FillStream(&stream, context.root_signature, mesh_simple_code, sizeof(mesh_simple_code), pixel_code, sizeof(pixel_code));
        state = CreateFromStream(device, &stream, sizeof(stream), &hr);
        ok(hr == S_OK, "mesh pipeline: %#lx\n", hr);
        if (state)
        {
            DrawAndCheck(&context, list, state, target, 1, MESH_GREEN, "mesh");
            DrawAndCheck(&context, list, state, target, 0, MESH_RED, "no groups");
            TestIndirect(&context, state, target);
            TestStatistics(&context, list, state);
            state->Release();
        }

        FillStream(&stream, context.root_signature, mesh_prim_code, sizeof(mesh_prim_code), pixel_prim_code, sizeof(pixel_prim_code));
        state = CreateFromStream(device, &stream, sizeof(stream), &hr);
        ok(hr == S_OK, "per-primitive pipeline: %#lx\n", hr);
        if (state)
        {
            DrawAndCheck(&context, list, state, target, 1, MESH_YELLOW, "per-primitive attribute");
            state->Release();
        }

        FillStream(&amplified.mesh, context.root_signature, mesh_payload_code, sizeof(mesh_payload_code), pixel_code, sizeof(pixel_code));
        amplified.as.value.pShaderBytecode = amp_code;
        amplified.as.value.BytecodeLength = sizeof(amp_code);
        state = CreateFromStream(device, &amplified, sizeof(amplified), &hr);
        ok(hr == S_OK, "amplification pipeline: %#lx\n", hr);
        if (state)
        {
            DrawAndCheck(&context, list, state, target, 1, MESH_BLUE, "amplification");
            state->Release();
        }

        FillStream(&vertex.mesh, context.root_signature, mesh_simple_code, sizeof(mesh_simple_code), pixel_code, sizeof(pixel_code));
        vertex.vs.value.pShaderBytecode = vs_fullscreen_code;
        vertex.vs.value.BytecodeLength = vs_fullscreen_code_size;
        state = CreateFromStream(device, &vertex, sizeof(vertex), &hr);
        ok(hr == E_INVALIDARG && !state, "mesh beside a vertex shader: %#lx\n", hr);
        if (state) state->Release();

        FillStream(&layout.mesh, context.root_signature, mesh_simple_code, sizeof(mesh_simple_code), pixel_code, sizeof(pixel_code));
        layout.layout.value.pInputElementDescs = elements;
        layout.layout.value.NumElements = ARRAYSIZE(elements);
        state = CreateFromStream(device, &layout, sizeof(layout), &hr);
        ok(hr == E_INVALIDARG && !state, "mesh beside an input layout: %#lx\n", hr);
        if (state) state->Release();

        FillStream(&amplified.mesh, context.root_signature, NULL, 0, pixel_code, sizeof(pixel_code));
        amplified.as.value.pShaderBytecode = amp_code;
        amplified.as.value.BytecodeLength = sizeof(amp_code);
        state = CreateFromStream(device, &amplified, sizeof(amplified), &hr);
        ok(hr == E_INVALIDARG && !state, "amplification without mesh: %#lx\n", hr);
        if (state) state->Release();
    }

    if (target) target->Release();
    if (list) list->Release();
    if (device) device->Release();
    d3d12_test_destroy(&context);
}

START_TEST(mesh)
{
    d3d12_test_bounded("mesh", RunMesh, NULL, D3D12_TEST_BUDGET);
}
