/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What ID3D12Device::CheckFeatureSupport reports as supported works when used
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "d3d12_apitest.h"

static void TestCastingFullyTypedFormat(struct d3d12_test_context *context)
{
    static const FLOAT grey[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options = {};
    D3D12_RENDER_TARGET_VIEW_DESC view = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    ID3D12Resource *target;
    DWORD texel = 0;
    HRESULT hr;

    hr = context->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options, sizeof(options));
    ok(hr == S_OK, "OPTIONS3: %#lx\n", hr);
    ok(options.CastingFullyTypedFormatSupported, "Casting fully typed formats is not supported\n");
    if (!options.CastingFullyTypedFormatSupported) return;
    if (!(target = d3d12_test_create_target(context->device, DXGI_FORMAT_R8G8B8A8_UNORM)))
    {
        ok(0, "Failed to create the target\n");
        return;
    }
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    context->device->CreateRenderTargetView(target, &view, rtv);
    context->list->ClearRenderTargetView(rtv, grey, 0, NULL);
    ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "The clear did not finish\n");
    ok(d3d12_test_read_texel(context, target, D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel),
       "The readback failed\n");
    ok((texel & 0xff) >= 186 && (texel & 0xff) <= 190, "An sRGB view of a UNORM target stored %08lx\n", texel);
    target->Release();
}

static void TestMultisampledPlacement(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options = {};
    D3D12_RESOURCE_ALLOCATION_INFO info;
    D3D12_RESOURCE_DESC desc = {};
    HRESULT hr;

    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &options, sizeof(options));
    ok(hr == S_OK, "OPTIONS4: %#lx\n", hr);
    ok(options.MSAA64KBAlignedTextureSupported, "64KB multisampled textures are not supported\n");
    if (!options.MSAA64KBAlignedTextureSupported) return;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 4;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    info = device->GetResourceAllocationInfo(0, 1, &desc);
    ok(info.SizeInBytes != ~static_cast<UINT64>(0), "A 64KB aligned 4x texture has no allocation size\n");
    ok(info.Alignment >= D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT && !(info.Alignment & (info.Alignment - 1)),
       "Got alignment %#I64x\n", info.Alignment);
}

static void TestBundleWriteImmediate(struct d3d12_test_context *context)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options = {};
    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER parameter = {};
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList2 *bundle = NULL;
    ID3D12Resource *buffer = NULL;
    HRESULT hr;

    hr = context->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options, sizeof(options));
    ok(hr == S_OK, "OPTIONS3: %#lx\n", hr);
    ok(!!(options.WriteBufferImmediateSupportFlags & D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT)
       == !!(options.WriteBufferImmediateSupportFlags & D3D12_COMMAND_LIST_SUPPORT_FLAG_BUNDLE),
       "Got WriteBufferImmediate flags %#x\n", options.WriteBufferImmediateSupportFlags);
    if (!(options.WriteBufferImmediateSupportFlags & D3D12_COMMAND_LIST_SUPPORT_FLAG_BUNDLE)) return;
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 256;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = context->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&buffer));
    ok(hr == S_OK, "CreateCommittedResource: %#lx\n", hr);
    hr = context->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&allocator));
    ok(hr == S_OK, "CreateCommandAllocator: %#lx\n", hr);
    if (allocator)
    {
        hr = context->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, allocator, NULL,
                __uuidof(ID3D12GraphicsCommandList2), reinterpret_cast<void **>(&bundle));
        ok(hr == S_OK, "CreateCommandList: %#lx\n", hr);
    }
    if (bundle && buffer)
    {
        parameter.Dest = buffer->GetGPUVirtualAddress();
        parameter.Value = 0x12345678;
        bundle->WriteBufferImmediate(1, &parameter, NULL);
        hr = bundle->Close();
        ok(hr == S_OK, "A bundle with WriteBufferImmediate failed to close: %#lx\n", hr);
    }
    if (bundle) bundle->Release();
    if (allocator) allocator->Release();
    if (buffer) buffer->Release();
}

static void TestExpandedComputeStates(struct d3d12_test_context *context)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options = {};
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    ID3D12Resource *buffer = NULL;
    HRESULT hr;

    hr = context->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options, sizeof(options));
    ok(hr == S_OK, "OPTIONS1: %#lx\n", hr);
    ok(options.ExpandedComputeResourceStates, "Expanded compute resource states are not supported\n");
    if (!options.ExpandedComputeResourceStates) return;
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 256;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = context->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
            NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&buffer));
    ok(hr == S_OK, "CreateCommittedResource: %#lx\n", hr);
    hr = context->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&allocator));
    ok(hr == S_OK, "CreateCommandAllocator: %#lx\n", hr);
    if (allocator)
    {
        hr = context->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator, NULL,
                __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void **>(&list));
        ok(hr == S_OK, "CreateCommandList: %#lx\n", hr);
    }
    if (list && buffer)
    {
        d3d12_test_transition(list, buffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        d3d12_test_transition(list, buffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        hr = list->Close();
        ok(hr == S_OK, "A compute list with shader resource barriers failed to close: %#lx\n", hr);
    }
    if (list) list->Release();
    if (allocator) allocator->Release();
    if (buffer) buffer->Release();
}

static void TestCrossAdapterRowMajor(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *texture = NULL;
    HRESULT hr;

    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    ok(hr == S_OK, "OPTIONS: %#lx\n", hr);
    ok(options.CrossAdapterRowMajorTextureSupported, "Cross-adapter row major textures are not supported\n");
    if (!options.CrossAdapterRowMajorTextureSupported) return;
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER, &desc,
            D3D12_RESOURCE_STATE_COMMON, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&texture));
    ok(hr == S_OK, "A cross-adapter row major texture failed: %#lx\n", hr);
    if (texture) texture->Release();
}

static void RunCapsUse(void *)
{
    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4 = {};
    struct d3d12_test_context context;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;

    hr = context.device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture));
    ok(hr == S_OK, "ARCHITECTURE1: %#lx\n", hr);
    ok(architecture.IsolatedMMU, "The GPU reports no isolated MMU\n");
    hr = context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &options4, sizeof(options4));
    ok(hr == S_OK, "OPTIONS4: %#lx\n", hr);
    ok(options4.SharedResourceCompatibilityTier == D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER_2,
       "Got shared resource tier %u\n", options4.SharedResourceCompatibilityTier);

    TestCrossAdapterRowMajor(context.device);
    TestMultisampledPlacement(context.device);
    TestBundleWriteImmediate(&context);
    TestExpandedComputeStates(&context);
    TestCastingFullyTypedFormat(&context);
    d3d12_test_destroy(&context);
}

START_TEST(capsuse)
{
    d3d12_test_bounded("capsuse", RunCapsUse, NULL, D3D12_TEST_BUDGET);
}
