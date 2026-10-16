/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Render pass access types and raster binding (DirectX-Specs RenderPasses)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

#define RED 0xff0000ffu
#define GREEN 0xff00ff00u
#define BLUE 0xffff0000u

static D3D12_RENDER_PASS_RENDER_TARGET_DESC PassTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE begin, D3D12_RENDER_PASS_ENDING_ACCESS_TYPE end)
{
    D3D12_RENDER_PASS_RENDER_TARGET_DESC desc = {};

    desc.cpuDescriptor = rtv;
    desc.BeginningAccess.Type = begin;
    desc.BeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BeginningAccess.Clear.ClearValue.Color[2] = 1.0f;
    desc.BeginningAccess.Clear.ClearValue.Color[3] = 1.0f;
    desc.EndingAccess.Type = end;
    return desc;
}

static void RunRenderPass(void *)
{
    static const FLOAT red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    struct d3d12_test_context context;
    ID3D12GraphicsCommandList4 *list4 = NULL;
    ID3D12PipelineState *pipeline = NULL;
    ID3D12Resource *targets[3] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3];
    D3D12_RENDER_PASS_RENDER_TARGET_DESC pass[3];
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (FLOAT)D3D12_TEST_SIZE, (FLOAT)D3D12_TEST_SIZE, 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, D3D12_TEST_SIZE, D3D12_TEST_SIZE };
    DWORD texel;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    hr = context.list->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), reinterpret_cast<void **>(&list4));
    ok(hr == S_OK, "QueryInterface(ID3D12GraphicsCommandList4): %#lx\n", hr);
    pipeline = d3d12_test_create_pipeline(&context, ps_green_code, ps_green_code_size, 1, NULL);
    for (UINT i = 0; i < 3; ++i)
    {
        targets[i] = d3d12_test_create_target(context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
        rtvs[i] = d3d12_test_rtv(&context, i);
        if (targets[i]) context.device->CreateRenderTargetView(targets[i], NULL, rtvs[i]);
    }
    if (!list4 || !pipeline || !targets[0] || !targets[1] || !targets[2]) goto done;

    context.list->ClearRenderTargetView(rtvs[0], red, 0, NULL);
    pass[0] = PassTarget(rtvs[0], D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR,
            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE);
    list4->BeginRenderPass(1, pass, NULL, D3D12_RENDER_PASS_FLAG_NONE);
    list4->EndRenderPass();
    ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "clear pass did not complete\n");
    texel = 0;
    ok(d3d12_test_read_texel(&context, targets[0], D3D12_RESOURCE_STATE_RENDER_TARGET, 1, 1, &texel), "readback failed\n");
    ok(texel == BLUE, "CLEAR beginning access: got %08lx\n", texel);

    context.allocator->Reset();
    context.list->Reset(context.allocator, NULL);
    context.list->ClearRenderTargetView(rtvs[0], red, 0, NULL);
    context.list->ClearRenderTargetView(rtvs[1], red, 0, NULL);
    pass[0] = PassTarget(rtvs[0], D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS,
            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS);
    pass[1] = PassTarget(rtvs[1], D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE,
            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE);
    list4->BeginRenderPass(2, pass, NULL, D3D12_RENDER_PASS_FLAG_NONE);
    context.list->RSSetViewports(1, &viewport);
    context.list->RSSetScissorRects(1, &scissor);
    context.list->SetGraphicsRootSignature(context.root_signature);
    context.list->SetPipelineState(pipeline);
    context.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.list->DrawInstanced(3, 1, 0, 0);
    list4->EndRenderPass();
    ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "draw pass did not complete\n");
    texel = 0;
    ok(d3d12_test_read_texel(&context, targets[1], D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel), "readback failed\n");
    ok(texel == GREEN, "first raster-bound target: got %08lx\n", texel);
    texel = 0;
    ok(d3d12_test_read_texel(&context, targets[0], D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel), "readback failed\n");
    ok(texel == RED, "NO_ACCESS target: got %08lx\n", texel);

    context.allocator->Reset();
    context.list->Reset(context.allocator, NULL);
    for (UINT i = 0; i < 3; ++i) context.list->ClearRenderTargetView(rtvs[i], red, 0, NULL);
    pass[0] = PassTarget(rtvs[0], D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE,
            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_SRV);
    pass[1] = PassTarget(rtvs[1], D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE,
            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_RENDER);
    pass[2] = PassTarget(rtvs[2], D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE,
            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_UAV);
    list4->BeginRenderPass(3, pass, NULL, D3D12_RENDER_PASS_FLAG_NONE);
    list4->EndRenderPass();
    pass[0] = PassTarget(rtvs[0], D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV,
            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE);
    pass[1] = PassTarget(rtvs[1], D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_RENDER,
            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE);
    pass[2] = PassTarget(rtvs[2], D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_UAV,
            D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE);
    list4->BeginRenderPass(3, pass, NULL, D3D12_RENDER_PASS_FLAG_NONE);
    context.list->RSSetViewports(1, &viewport);
    context.list->RSSetScissorRects(1, &scissor);
    context.list->SetGraphicsRootSignature(context.root_signature);
    context.list->SetPipelineState(pipeline);
    context.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context.list->DrawInstanced(3, 1, 0, 0);
    list4->EndRenderPass();
    ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "local pass did not complete\n");
    for (UINT i = 0; i < 3; ++i)
    {
        DWORD expected = i == 1 ? GREEN : RED;

        texel = 0;
        ok(d3d12_test_read_texel(&context, targets[i], D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel),
                "readback failed\n");
        ok(texel == expected, "PRESERVE_LOCAL target %u: got %08lx, expected %08lx\n", i, texel, expected);
    }

done:
    for (UINT i = 0; i < 3; ++i) if (targets[i]) targets[i]->Release();
    if (pipeline) pipeline->Release();
    if (list4) list4->Release();
    d3d12_test_destroy(&context);
}

START_TEST(renderpass)
{
    d3d12_test_bounded("renderpass", RunRenderPass, NULL, D3D12_TEST_BUDGET);
}
