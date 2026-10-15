/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bundle restrictions and state inheritance (DirectX-Specs CPUEfficiency, ResourceBinding)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

#define GREEN 0xff00ff00u
#define WHITE 0xffffffffu

static const FLOAT white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

static BOOL CreateBundle(struct d3d12_test_context *context, ID3D12CommandAllocator **allocator,
        ID3D12GraphicsCommandList **bundle)
{
    HRESULT hr = context->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE,
            __uuidof(ID3D12CommandAllocator), reinterpret_cast<void **>(allocator));
    ok(hr == S_OK, "CreateCommandAllocator(bundle): %#lx\n", hr);
    if (FAILED(hr)) return FALSE;
    hr = context->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, *allocator, NULL,
            __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void **>(bundle));
    ok(hr == S_OK, "CreateCommandList(bundle): %#lx\n", hr);
    if (FAILED(hr))
    {
        (*allocator)->Release();
        return FALSE;
    }
    return TRUE;
}

static void TestRestrictedCall(struct d3d12_test_context *context, ID3D12Resource *target, UINT call)
{
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *bundle;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    HRESULT hr;

    if (!CreateBundle(context, &allocator, &bundle)) return;
    if (call == 0) bundle->ClearRenderTargetView(rtv, white, 0, NULL);
    else if (call == 1) bundle->SetPredication(NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    else d3d12_test_transition(bundle, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    hr = bundle->Close();
    ok(hr == E_FAIL, "call %u: Close of a bundle with a restricted call: %#lx\n", call, hr);
    bundle->Release();
    allocator->Release();
}

static void TestPipelineReturnsToCaller(struct d3d12_test_context *context, ID3D12Resource *target,
        ID3D12PipelineState *pipeline)
{
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *bundle;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    DWORD texel = 0;

    if (!CreateBundle(context, &allocator, &bundle)) return;
    bundle->SetGraphicsRootSignature(context->root_signature);
    bundle->SetPipelineState(pipeline);
    bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ok(bundle->Close() == S_OK, "Close(bundle)\n");

    context->allocator->Reset();
    context->list->Reset(context->allocator, NULL);
    context->list->ClearRenderTargetView(rtv, white, 0, NULL);
    d3d12_test_bind_target(context, context->list, 1, &rtv);
    context->list->ExecuteBundle(bundle);
    context->list->DrawInstanced(3, 1, 0, 0);
    ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "execution did not complete\n");
    ok(d3d12_test_read_texel(context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, 0, &texel), "readback failed\n");
    ok(texel == GREEN, "state set in a bundle did not return to the caller: %08lx\n", texel);
    bundle->Release();
    allocator->Release();
}

static void TestBundleInheritsBindings(struct d3d12_test_context *context, ID3D12Resource *target,
        ID3D12PipelineState *pipeline)
{
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *bundle;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    DWORD texel = 0;

    if (!CreateBundle(context, &allocator, &bundle)) return;
    bundle->SetPipelineState(pipeline);
    bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    bundle->DrawInstanced(3, 1, 0, 0);
    ok(bundle->Close() == S_OK, "Close(bundle)\n");

    context->allocator->Reset();
    context->list->Reset(context->allocator, NULL);
    context->list->ClearRenderTargetView(rtv, white, 0, NULL);
    d3d12_test_bind_target(context, context->list, 1, &rtv);
    context->list->SetGraphicsRootSignature(context->root_signature);
    context->list->ExecuteBundle(bundle);
    ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "execution did not complete\n");
    ok(d3d12_test_read_texel(context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 0, 0, &texel), "readback failed\n");
    ok(texel == GREEN, "bundle did not inherit the caller's bindings: %08lx\n", texel);
    bundle->Release();
    allocator->Release();
}

static void RunBundle(void *)
{
    struct d3d12_test_context context;
    ID3D12PipelineState *pipeline;
    ID3D12Resource *target;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;

    if (!d3d12_test_init(&context)) return;
    target = d3d12_test_create_target(context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
    pipeline = d3d12_test_create_pipeline(&context, ps_green_code, ps_green_code_size, 1, NULL);
    if (target && pipeline)
    {
        rtv = d3d12_test_rtv(&context, 0);
        context.device->CreateRenderTargetView(target, NULL, rtv);
        ok(context.list->Close() == S_OK, "Close(initial list)\n");

        TestBundleInheritsBindings(&context, target, pipeline);
        TestPipelineReturnsToCaller(&context, target, pipeline);
        TestRestrictedCall(&context, target, 0);
        TestRestrictedCall(&context, target, 1);
        TestRestrictedCall(&context, target, 2);
    }
    if (pipeline) pipeline->Release();
    if (target) target->Release();
    d3d12_test_destroy(&context);
}

START_TEST(bundle)
{
    d3d12_test_bounded("bundle", RunBundle, NULL, D3D12_TEST_BUDGET);
}
