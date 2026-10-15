/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3D12_BLEND_DESC with IndependentBlendEnable FALSE applies RenderTarget[0] to every target
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static void RunBlend(void *)
{
    static const FLOAT black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const DWORD expected[3] = { 0xff0000ffu, 0xff00ff00u, 0xffff0000u };
    struct d3d12_test_context context;
    ID3D12Resource *targets[3] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3];
    D3D12_BLEND_DESC blend = {};
    ID3D12PipelineState *pipeline;
    DWORD texel;

    if (!d3d12_test_init(&context)) return;
    blend.IndependentBlendEnable = FALSE;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline = d3d12_test_create_pipeline(&context, ps_mrt_code, ps_mrt_code_size, 3, &blend);
    for (UINT i = 0; i < 3; ++i)
    {
        targets[i] = d3d12_test_create_target(context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
        rtvs[i] = d3d12_test_rtv(&context, i);
        if (targets[i]) context.device->CreateRenderTargetView(targets[i], NULL, rtvs[i]);
    }
    if (pipeline && targets[0] && targets[1] && targets[2])
    {
        for (UINT i = 0; i < 3; ++i) context.list->ClearRenderTargetView(rtvs[i], black, 0, NULL);
        d3d12_test_bind_target(&context, context.list, 3, rtvs);
        context.list->SetGraphicsRootSignature(context.root_signature);
        context.list->SetPipelineState(pipeline);
        context.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context.list->DrawInstanced(3, 1, 0, 0);
        ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "draw did not complete\n");
        for (UINT i = 0; i < 3; ++i)
        {
            texel = 0;
            ok(d3d12_test_read_texel(&context, targets[i], D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel),
                    "readback %u failed\n", i);
            ok(texel == expected[i], "target %u: got %08lx, expected %08lx\n", i, texel, expected[i]);
        }
    }
    for (UINT i = 0; i < 3; ++i) if (targets[i]) targets[i]->Release();
    if (pipeline) pipeline->Release();
    d3d12_test_destroy(&context);
}

START_TEST(blend)
{
    d3d12_test_bounded("blend", RunBlend, NULL, D3D12_TEST_BUDGET);
}
