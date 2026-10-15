/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Alpha blend factors, inverted viewports and unrestricted copy pitches
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static BOOL Near(DWORD texel, DWORD expected)
{
    for (UINT shift = 0; shift < 32; shift += 8)
    {
        int a = (texel >> shift) & 0xff, b = (expected >> shift) & 0xff;
        if (a - b > 1 || b - a > 1) return FALSE;
    }
    return TRUE;
}

static void Reset(struct d3d12_test_context *context)
{
    HRESULT hr;

    hr = context->allocator->Reset();
    ok(hr == S_OK, "allocator Reset: %#lx\n", hr);
    hr = context->list->Reset(context->allocator, NULL);
    ok(hr == S_OK, "list Reset: %#lx\n", hr);
}

static void TestAlphaBlendFactor(struct d3d12_test_context *context)
{
    static const FLOAT black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const FLOAT factor[4] = { 0.2f, 0.4f, 0.6f, 0.8f };
    static const D3D12_BLEND sources[2] = { D3D12_BLEND_ALPHA_FACTOR, D3D12_BLEND_INV_ALPHA_FACTOR };
    static const DWORD expected[2] = { 0xff00cc00u, 0xff003300u };

    for (UINT i = 0; i < 2; ++i)
    {
        D3D12_BLEND_DESC blend = {};
        ID3D12PipelineState *pipeline;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
        ID3D12Resource *target = d3d12_test_create_target(context->device, DXGI_FORMAT_R8G8B8A8_UNORM);
        DWORD texel = 0;

        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = sources[i];
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline = d3d12_test_create_pipeline(context, ps_green_code, ps_green_code_size, 1, &blend);
        if (pipeline && target)
        {
            Reset(context);
            context->device->CreateRenderTargetView(target, NULL, rtv);
            context->list->ClearRenderTargetView(rtv, black, 0, NULL);
            d3d12_test_bind_target(context, context->list, 1, &rtv);
            context->list->SetGraphicsRootSignature(context->root_signature);
            context->list->SetPipelineState(pipeline);
            context->list->OMSetBlendFactor(factor);
            context->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->list->DrawInstanced(3, 1, 0, 0);
            ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "blend %u did not complete\n", i);
            ok(d3d12_test_read_texel(context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel),
                    "blend %u readback failed\n", i);
            ok(Near(texel, expected[i]), "blend %u: got %08lx, expected %08lx\n", i, texel, expected[i]);
        }
        if (target) target->Release();
        if (pipeline) pipeline->Release();
    }
}

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

static void TestInvertedViewport(struct d3d12_test_context *context)
{
    static const FLOAT black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const UINT16 indices[3] = { 0, 2, 3 };
    D3D12_VIEWPORT viewport = { 0.0f, (FLOAT)D3D12_TEST_SIZE, (FLOAT)D3D12_TEST_SIZE, -(FLOAT)D3D12_TEST_SIZE, 0.0f, 1.0f };
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    ID3D12PipelineState *pipeline = d3d12_test_create_pipeline(context, ps_green_code, ps_green_code_size, 1, NULL);
    ID3D12Resource *target = d3d12_test_create_target(context->device, DXGI_FORMAT_R8G8B8A8_UNORM);
    ID3D12Resource *index_buffer = CreateIndexBuffer(context->device, indices, 3);
    D3D12_INDEX_BUFFER_VIEW view = {};
    DWORD below = 0, above = 0;

    ok(index_buffer != NULL, "index buffer creation failed\n");
    if (pipeline && target && index_buffer)
    {
        Reset(context);
        context->device->CreateRenderTargetView(target, NULL, rtv);
        view.BufferLocation = index_buffer->GetGPUVirtualAddress();
        view.SizeInBytes = sizeof(indices);
        view.Format = DXGI_FORMAT_R16_UINT;
        context->list->ClearRenderTargetView(rtv, black, 0, NULL);
        d3d12_test_bind_target(context, context->list, 1, &rtv);
        context->list->RSSetViewports(1, &viewport);
        context->list->SetGraphicsRootSignature(context->root_signature);
        context->list->SetPipelineState(pipeline);
        context->list->IASetIndexBuffer(&view);
        context->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->list->DrawIndexedInstanced(3, 1, 0, 0, 0);
        ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "inverted viewport draw did not complete\n");
        ok(d3d12_test_read_texel(context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 20, 28, &below),
                "readback failed\n");
        ok(d3d12_test_read_texel(context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 20, 4, &above),
                "readback failed\n");
        ok(above == 0xff00ff00u && below == 0, "inverted viewport: upper texel %08lx, lower texel %08lx\n", above, below);
    }
    if (index_buffer) index_buffer->Release();
    if (target) target->Release();
    if (pipeline) pipeline->Release();
}

static void TestUnrestrictedCopyPitch(struct d3d12_test_context *context)
{
    static const FLOAT colour[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    const UINT offset = 4, pitch = D3D12_TEST_SIZE * 4;
    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_READBACK };
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    ID3D12Resource *target = d3d12_test_create_target(context->device, DXGI_FORMAT_R8G8B8A8_UNORM);
    ID3D12Resource *readback = NULL;
    D3D12_RESOURCE_DESC desc = {};
    D3D12_RANGE range = {};
    BYTE *data = NULL;
    HRESULT hr;

    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = offset + pitch * D3D12_TEST_SIZE;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = context->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&readback));
    ok(hr == S_OK, "readback buffer: %#lx\n", hr);
    if (target && readback)
    {
        Reset(context);
        context->device->CreateRenderTargetView(target, NULL, rtv);
        context->list->ClearRenderTargetView(rtv, colour, 0, NULL);
        d3d12_test_transition(context->list, target, D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        dst.pResource = readback;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = offset;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = D3D12_TEST_SIZE;
        dst.PlacedFootprint.Footprint.Height = D3D12_TEST_SIZE;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = pitch;
        src.pResource = target;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        context->list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
        d3d12_test_transition(context->list, target, D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
        ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "copy did not complete\n");
        range.End = static_cast<SIZE_T>(desc.Width);
        hr = readback->Map(0, &range, reinterpret_cast<void **>(&data));
        ok(hr == S_OK, "Map: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            const DWORD *first = reinterpret_cast<const DWORD *>(data + offset);
            const DWORD *last = reinterpret_cast<const DWORD *>(data + offset + pitch * (D3D12_TEST_SIZE - 1)
                    + (D3D12_TEST_SIZE - 1) * 4);
            ok(*first == 0xffff0000u && *last == 0xffff0000u, "copied texels %08lx %08lx\n", *first, *last);
            range.End = 0;
            readback->Unmap(0, &range);
        }
    }
    if (readback) readback->Release();
    if (target) target->Release();
}

static void RunRelaxed(void *)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS13 options13 = {};
    struct d3d12_test_context context;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "empty list did not complete\n");
    hr = context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS13, &options13, sizeof(options13));
    ok(hr == S_OK, "OPTIONS13: %#lx\n", hr);
    trace("copy pitch %d, vertex alignment %d, flip Y %d, flip Z %d, copy between dimensions %d, alpha factor %d\n",
            options13.UnrestrictedBufferTextureCopyPitchSupported, options13.UnrestrictedVertexElementAlignmentSupported,
            options13.InvertedViewportHeightFlipsYSupported, options13.InvertedViewportDepthFlipsZSupported,
            options13.TextureCopyBetweenDimensionsSupported, options13.AlphaBlendFactorSupported);
    if (options13.AlphaBlendFactorSupported) TestAlphaBlendFactor(&context);
    else skip("alpha blend factors are not supported\n");
    if (options13.InvertedViewportHeightFlipsYSupported) TestInvertedViewport(&context);
    else skip("inverted viewports are not supported\n");
    if (options13.UnrestrictedBufferTextureCopyPitchSupported) TestUnrestrictedCopyPitch(&context);
    else skip("unrestricted copy pitches are not supported\n");
    ok(context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            context.device->GetDeviceRemovedReason());
    d3d12_test_destroy(&context);
}

START_TEST(relaxed)
{
    d3d12_test_bounded("relaxed", RunRelaxed, NULL, D3D12_TEST_BUDGET);
}
