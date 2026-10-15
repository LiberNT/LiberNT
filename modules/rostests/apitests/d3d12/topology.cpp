/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN draws a fan around the first vertex
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static ID3D12Resource *CreateIndexBuffer(ID3D12Device *device, const UINT16 *indices, UINT count)
{
    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *buffer = NULL;
    D3D12_RANGE range = {};
    void *data = NULL;

    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = count * sizeof(*indices);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&buffer))))
        return NULL;
    if (FAILED(buffer->Map(0, &range, &data)))
    {
        buffer->Release();
        return NULL;
    }
    memcpy(data, indices, count * sizeof(*indices));
    buffer->Unmap(0, NULL);
    return buffer;
}

static void RunTopology(void *)
{
    static const FLOAT black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const UINT16 indices[4] = { 0, 1, 3, 2 };
    static const UINT points[4][2] = { { 1, 16 }, { 30, 16 }, { 16, 1 }, { 16, 30 } };
    D3D12_FEATURE_DATA_D3D12_OPTIONS15 options15 = {};
    struct d3d12_test_context context;
    D3D12_INDEX_BUFFER_VIEW view = {};
    ID3D12Resource *target = NULL, *index_buffer = NULL;
    ID3D12PipelineState *pipeline = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    HRESULT hr;
    DWORD texel;

    if (!d3d12_test_init(&context)) return;
    hr = context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS15, &options15, sizeof(options15));
    ok(hr == S_OK, "OPTIONS15: %#lx\n", hr);
    if (!options15.TriangleFanSupported)
    {
        skip("triangle fans are not supported\n");
        d3d12_test_destroy(&context);
        return;
    }
    pipeline = d3d12_test_create_pipeline(&context, ps_green_code, ps_green_code_size, 1, NULL);
    target = d3d12_test_create_target(context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
    index_buffer = CreateIndexBuffer(context.device, indices, 4);
    ok(index_buffer != NULL, "index buffer creation failed\n");
    rtv = d3d12_test_rtv(&context, 0);
    if (pipeline && target && index_buffer)
    {
        context.device->CreateRenderTargetView(target, NULL, rtv);
        view.BufferLocation = index_buffer->GetGPUVirtualAddress();
        view.SizeInBytes = sizeof(indices);
        view.Format = DXGI_FORMAT_R16_UINT;
        context.list->ClearRenderTargetView(rtv, black, 0, NULL);
        d3d12_test_bind_target(&context, context.list, 1, &rtv);
        context.list->SetGraphicsRootSignature(context.root_signature);
        context.list->SetPipelineState(pipeline);
        context.list->IASetIndexBuffer(&view);
        context.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN);
        context.list->DrawIndexedInstanced(4, 1, 0, 0, 0);
        ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "draw did not complete\n");
        for (UINT i = 0; i < 4; ++i)
        {
            texel = 0;
            ok(d3d12_test_read_texel(&context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, points[i][0], points[i][1],
                    &texel), "readback %u failed\n", i);
            ok(texel == 0xff00ff00u, "texel %u,%u: got %08lx\n", points[i][0], points[i][1], texel);
        }
    }
    if (index_buffer) index_buffer->Release();
    if (target) target->Release();
    if (pipeline) pipeline->Release();
    d3d12_test_destroy(&context);
}

START_TEST(topology)
{
    d3d12_test_bounded("topology", RunTopology, NULL, D3D12_TEST_BUDGET);
}
