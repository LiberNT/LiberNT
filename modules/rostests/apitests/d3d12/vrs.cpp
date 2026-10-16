/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Per-draw variable rate shading
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static void DrawAtRate(struct d3d12_test_context *context, ID3D12GraphicsCommandList5 *list5,
        ID3D12PipelineState *pipeline, ID3D12Resource *target, D3D12_SHADING_RATE rate, DWORD texels[4])
{
    static const FLOAT black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const UINT points[4][2] = { { 4, 4 }, { 5, 4 }, { 4, 5 }, { 5, 5 } };
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    HRESULT hr;

    hr = context->allocator->Reset();
    ok(hr == S_OK, "allocator Reset: %#lx\n", hr);
    hr = context->list->Reset(context->allocator, NULL);
    ok(hr == S_OK, "list Reset: %#lx\n", hr);
    context->device->CreateRenderTargetView(target, NULL, rtv);
    context->list->ClearRenderTargetView(rtv, black, 0, NULL);
    d3d12_test_bind_target(context, context->list, 1, &rtv);
    context->list->SetGraphicsRootSignature(context->root_signature);
    context->list->SetPipelineState(pipeline);
    list5->RSSetShadingRate(rate, NULL);
    context->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->list->DrawInstanced(3, 1, 0, 0);
    ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "rate %#x: draw did not complete\n", rate);
    for (UINT i = 0; i < 4; ++i)
    {
        texels[i] = 0;
        ok(d3d12_test_read_texel(context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, points[i][0], points[i][1],
                &texels[i]), "rate %#x: readback %u failed\n", rate, i);
    }
}

static void RunVrs(void *)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
    ID3D12GraphicsCommandList5 *list5 = NULL;
    ID3D12PipelineState *pipeline = NULL;
    struct d3d12_test_context context;
    ID3D12Resource *target = NULL;
    DWORD fine[4], coarse[4];
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "empty list did not complete\n");
    hr = context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6));
    ok(hr == S_OK, "OPTIONS6: %#lx\n", hr);
    trace("shading rate tier %u, additional rates %d, tile size %u\n", options6.VariableShadingRateTier,
            options6.AdditionalShadingRatesSupported, options6.ShadingRateImageTileSize);
    if (options6.VariableShadingRateTier == D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED)
    {
        ok(!options6.AdditionalShadingRatesSupported && !options6.ShadingRateImageTileSize,
                "capabilities reported without a shading rate tier\n");
        skip("variable rate shading is not supported\n");
        d3d12_test_destroy(&context);
        return;
    }
    if (options6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2)
        ok(options6.ShadingRateImageTileSize >= 8 && options6.ShadingRateImageTileSize <= 32,
                "tile size %u\n", options6.ShadingRateImageTileSize);
    context.list->QueryInterface(__uuidof(ID3D12GraphicsCommandList5), reinterpret_cast<void **>(&list5));
    ok(list5 != NULL, "no ID3D12GraphicsCommandList5\n");
    pipeline = d3d12_test_create_pipeline(&context, ps_parity_code, ps_parity_code_size, 1, NULL);
    target = d3d12_test_create_target(context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
    if (list5 && pipeline && target)
    {
        DrawAtRate(&context, list5, pipeline, target, D3D12_SHADING_RATE_1X1, fine);
        ok(fine[0] == 0xff000000u && fine[1] == 0xff0000ffu && fine[2] == 0xff00ff00u && fine[3] == 0xff00ffffu,
                "1x1: %08lx %08lx %08lx %08lx\n", fine[0], fine[1], fine[2], fine[3]);
        DrawAtRate(&context, list5, pipeline, target, D3D12_SHADING_RATE_2X2, coarse);
        ok(coarse[0] == coarse[1] && coarse[0] == coarse[2] && coarse[0] == coarse[3],
                "2x2: %08lx %08lx %08lx %08lx\n", coarse[0], coarse[1], coarse[2], coarse[3]);
        DrawAtRate(&context, list5, pipeline, target, D3D12_SHADING_RATE_2X1, coarse);
        ok(coarse[0] == coarse[1] && coarse[2] == coarse[3] && coarse[0] != coarse[2],
                "2x1: %08lx %08lx %08lx %08lx\n", coarse[0], coarse[1], coarse[2], coarse[3]);
    }
    ok(context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            context.device->GetDeviceRemovedReason());
    if (target) target->Release();
    if (pipeline) pipeline->Release();
    if (list5) list5->Release();
    d3d12_test_destroy(&context);
}

START_TEST(vrs)
{
    d3d12_test_bounded("vrs", RunVrs, NULL, D3D12_TEST_BUDGET);
}
