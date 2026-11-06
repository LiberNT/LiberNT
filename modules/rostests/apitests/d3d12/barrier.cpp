/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Enhanced barriers on real GPU work
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static ID3D12Resource *CreateReadback(ID3D12Device *device)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *resource = NULL;
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_READBACK;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * D3D12_TEST_SIZE;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL,
            __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    ok(hr == S_OK, "readback buffer: %#lx\n", hr);
    return resource;
}

static ID3D12Resource *CreateTarget(ID3D12Device10 *device, D3D12_BARRIER_LAYOUT layout, D3D12_RESOURCE_FLAGS flags, HRESULT expected)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC1 desc = {};
    ID3D12Resource *resource = NULL;
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = D3D12_TEST_SIZE;
    desc.Height = D3D12_TEST_SIZE;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | flags;
    hr = device->CreateCommittedResource3(&heap, D3D12_HEAP_FLAG_NONE, &desc, layout, NULL, NULL, 0, NULL,
            __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    ok(hr == expected, "texture in layout %#x with flags %#x: %#lx\n", layout, flags, hr);
    return resource;
}

static void TestCreationRules(ID3D12Device10 *device)
{
    ID3D12Resource *resource;

    resource = CreateTarget(device, D3D12_BARRIER_LAYOUT_COMMON, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, S_OK);
    if (resource) resource->Release();
    resource = CreateTarget(device, D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS,
            E_INVALIDARG);
    if (resource) resource->Release();
}

static void TextureBarrier(ID3D12GraphicsCommandList7 *list, ID3D12Resource *resource, D3D12_BARRIER_SYNC sync_before,
        D3D12_BARRIER_SYNC sync_after, D3D12_BARRIER_ACCESS access_before, D3D12_BARRIER_ACCESS access_after,
        D3D12_BARRIER_LAYOUT layout_before, D3D12_BARRIER_LAYOUT layout_after)
{
    D3D12_TEXTURE_BARRIER barrier = {};
    D3D12_BARRIER_GROUP group = {};

    barrier.SyncBefore = sync_before;
    barrier.SyncAfter = sync_after;
    barrier.AccessBefore = access_before;
    barrier.AccessAfter = access_after;
    barrier.LayoutBefore = layout_before;
    barrier.LayoutAfter = layout_after;
    barrier.pResource = resource;
    barrier.Subresources.IndexOrFirstMipLevel = 0xffffffff;
    group.Type = D3D12_BARRIER_TYPE_TEXTURE;
    group.NumBarriers = 1;
    group.pTextureBarriers = &barrier;
    list->Barrier(1, &group);
}

static void TestTextureRoundTrip(struct d3d12_test_context *context, ID3D12Device10 *device, ID3D12GraphicsCommandList7 *list)
{
    static const FLOAT green[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    ID3D12Resource *target = CreateTarget(device, D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_RESOURCE_FLAG_NONE, S_OK);
    ID3D12Resource *readback = CreateReadback(context->device);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d3d12_test_rtv(context, 0);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    D3D12_GLOBAL_BARRIER global = {};
    D3D12_BUFFER_BARRIER buffer = {};
    D3D12_BARRIER_GROUP groups[2] = {};
    D3D12_RANGE range = {};
    BYTE *data = NULL;
    HRESULT hr;

    if (target && readback)
    {
        context->device->CreateRenderTargetView(target, NULL, rtv);
        context->list->ClearRenderTargetView(rtv, green, 0, NULL);
        TextureBarrier(list, target, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_SYNC_COPY,
                D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_ACCESS_COPY_SOURCE,
                D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_LAYOUT_COPY_SOURCE);
        dst.pResource = readback;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = D3D12_TEST_SIZE;
        dst.PlacedFootprint.Footprint.Height = D3D12_TEST_SIZE;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        src.pResource = target;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        context->list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
        buffer.SyncBefore = D3D12_BARRIER_SYNC_COPY;
        buffer.SyncAfter = D3D12_BARRIER_SYNC_ALL;
        buffer.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
        buffer.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
        buffer.pResource = readback;
        buffer.Size = ~static_cast<UINT64>(0);
        groups[0].Type = D3D12_BARRIER_TYPE_BUFFER;
        groups[0].NumBarriers = 1;
        groups[0].pBufferBarriers = &buffer;
        global.SyncBefore = D3D12_BARRIER_SYNC_ALL;
        global.SyncAfter = D3D12_BARRIER_SYNC_ALL;
        global.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
        global.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
        groups[1].Type = D3D12_BARRIER_TYPE_GLOBAL;
        groups[1].NumBarriers = 1;
        groups[1].pGlobalBarriers = &global;
        list->Barrier(ARRAYSIZE(groups), groups);
        ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "barriers did not complete\n");
        range.End = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * D3D12_TEST_SIZE;
        hr = readback->Map(0, &range, reinterpret_cast<void **>(&data));
        ok(hr == S_OK, "Map: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            DWORD texel = *reinterpret_cast<const DWORD *>(data);
            ok(texel == 0xff00ff00, "texel %08lx\n", texel);
            range.End = 0;
            readback->Unmap(0, &range);
        }
    }
    if (readback) readback->Release();
    if (target) target->Release();
}

static void RunBarrier(void *)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options = {};
    struct d3d12_test_context context = {};
    ID3D12GraphicsCommandList7 *list = NULL;
    ID3D12Device10 *device = NULL;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    hr = context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options));
    ok(hr == S_OK, "OPTIONS12: %#lx\n", hr);
    if (!options.EnhancedBarriersSupported)
        skip("Enhanced barriers are not supported\n");
    else if (FAILED(context.device->QueryInterface(__uuidof(ID3D12Device10), reinterpret_cast<void **>(&device)))
            || FAILED(context.list->QueryInterface(__uuidof(ID3D12GraphicsCommandList7), reinterpret_cast<void **>(&list))))
        skip("No interfaces for enhanced barriers\n");
    else
    {
        TestCreationRules(device);
        TestTextureRoundTrip(&context, device, list);
    }
    if (list) list->Release();
    if (device) device->Release();
    d3d12_test_destroy(&context);
}

START_TEST(barrier)
{
    d3d12_test_bounded("barrier", RunBarrier, NULL, D3D12_TEST_BUDGET);
}
