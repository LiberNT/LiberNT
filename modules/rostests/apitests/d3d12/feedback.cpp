/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Sampler feedback tier, feedback map creation rules, feedback views and transcoding
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

#define FEEDBACK_SIZE 64u
#define FEEDBACK_REGION 16u
#define FEEDBACK_DECODED (FEEDBACK_SIZE / FEEDBACK_REGION)

static D3D12_RESOURCE_DESC1 FeedbackDesc(DXGI_FORMAT format, UINT region)
{
    D3D12_RESOURCE_DESC1 desc = {};

    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = FEEDBACK_SIZE;
    desc.Height = FEEDBACK_SIZE;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 7;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.SamplerFeedbackMipRegion.Width = region;
    desc.SamplerFeedbackMipRegion.Height = region;
    desc.SamplerFeedbackMipRegion.Depth = region ? 1 : 0;
    return desc;
}

static HRESULT CreateFeedback(ID3D12Device8 *device, const D3D12_RESOURCE_DESC1 *desc, ID3D12Resource **out)
{
    D3D12_HEAP_PROPERTIES heap = {};

    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    *out = NULL;
    return device->CreateCommittedResource2(&heap, D3D12_HEAP_FLAG_NONE, desc, D3D12_RESOURCE_STATE_COMMON, NULL, NULL,
            __uuidof(ID3D12Resource), reinterpret_cast<void **>(out));
}

static ID3D12Resource *CreateDecoded(ID3D12Device *device, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *resource = NULL;
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = FEEDBACK_DECODED;
    desc.Height = FEEDBACK_DECODED;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_UINT;
    desc.SampleDesc.Count = 1;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, NULL, __uuidof(ID3D12Resource),
            reinterpret_cast<void **>(&resource));
    ok(hr == S_OK, "decoded texture: %#lx\n", hr);
    return resource;
}

static ID3D12Resource *CreateBuffer(ID3D12Device *device, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *resource = NULL;
    HRESULT hr;

    heap.Type = type;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * FEEDBACK_DECODED;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, NULL, __uuidof(ID3D12Resource),
            reinterpret_cast<void **>(&resource));
    ok(hr == S_OK, "buffer: %#lx\n", hr);
    return resource;
}

static D3D12_TEXTURE_COPY_LOCATION Footprint(ID3D12Resource *buffer)
{
    D3D12_TEXTURE_COPY_LOCATION location = {};

    location.pResource = buffer;
    location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UINT;
    location.PlacedFootprint.Footprint.Width = FEEDBACK_DECODED;
    location.PlacedFootprint.Footprint.Height = FEEDBACK_DECODED;
    location.PlacedFootprint.Footprint.Depth = 1;
    location.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    return location;
}

static D3D12_TEXTURE_COPY_LOCATION Subresource(ID3D12Resource *texture)
{
    D3D12_TEXTURE_COPY_LOCATION location = {};

    location.pResource = texture;
    location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    return location;
}

static BYTE Pattern(UINT x, UINT y)
{
    return (x * 3 + y) % 2 ? 0xff : 0;
}

static void TestTranscode(ID3D12Resource *feedback)
{
    struct d3d12_test_context context;
    ID3D12GraphicsCommandList1 *list1 = NULL;
    ID3D12Resource *source = NULL, *decoded = NULL, *upload = NULL, *readback = NULL;
    D3D12_TEXTURE_COPY_LOCATION dst, src;
    UINT mismatches = 0;
    BYTE *data;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    hr = context.list->QueryInterface(__uuidof(ID3D12GraphicsCommandList1), reinterpret_cast<void **>(&list1));
    ok(hr == S_OK, "ID3D12GraphicsCommandList1: %#lx\n", hr);
    source = CreateDecoded(context.device, D3D12_RESOURCE_STATE_COPY_DEST);
    decoded = CreateDecoded(context.device, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    upload = CreateBuffer(context.device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    readback = CreateBuffer(context.device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    if (list1 && source && decoded && upload && readback && SUCCEEDED(upload->Map(0, NULL, reinterpret_cast<void **>(&data))))
    {
        for (UINT y = 0; y < FEEDBACK_DECODED; ++y)
            for (UINT x = 0; x < FEEDBACK_DECODED; ++x)
                data[y * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT + x] = Pattern(x, y);
        upload->Unmap(0, NULL);

        dst = Subresource(source);
        src = Footprint(upload);
        context.list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
        d3d12_test_transition(context.list, source, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        d3d12_test_transition(context.list, feedback, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        list1->ResolveSubresourceRegion(feedback, 0, 0, 0, source, 0, NULL, DXGI_FORMAT_R8_UINT,
                D3D12_RESOLVE_MODE_ENCODE_SAMPLER_FEEDBACK);
        d3d12_test_transition(context.list, feedback, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        list1->ResolveSubresourceRegion(decoded, 0, 0, 0, feedback, 0, NULL, DXGI_FORMAT_R8_UINT,
                D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK);
        d3d12_test_transition(context.list, decoded, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dst = Footprint(readback);
        src = Subresource(decoded);
        context.list->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
        d3d12_test_transition(context.list, feedback, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "transcode did not complete\n");
        if (SUCCEEDED(readback->Map(0, NULL, reinterpret_cast<void **>(&data))))
        {
            for (UINT y = 0; y < FEEDBACK_DECODED; ++y)
                for (UINT x = 0; x < FEEDBACK_DECODED; ++x)
                    if (data[y * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT + x] != Pattern(x, y)) ++mismatches;
            trace("decoded row 0: %02x %02x %02x %02x\n", data[0], data[1], data[2], data[3]);
            readback->Unmap(0, NULL);
        }
        ok(!mismatches, "%u of %u decoded MinMip values differ from the encoded ones\n", mismatches,
                FEEDBACK_DECODED * FEEDBACK_DECODED);
    }
    ok(context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            context.device->GetDeviceRemovedReason());
    if (readback) readback->Release();
    if (upload) upload->Release();
    if (decoded) decoded->Release();
    if (source) source->Release();
    if (list1) list1->Release();
    d3d12_test_destroy(&context);
}

static void RunFeedback(void *)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options = {};
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    D3D12_RESOURCE_ALLOCATION_INFO info;
    D3D12_RESOURCE_DESC1 desc;
    ID3D12DescriptorHeap *heap = NULL;
    ID3D12Resource *feedback = NULL, *paired = NULL;
    ID3D12Device8 *device8 = NULL;
    ID3D12Device *device;
    HRESULT hr;

    if (!(device = d3d12_test_create_device())) return;
    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options, sizeof(options));
    ok(hr == S_OK, "OPTIONS7: %#lx\n", hr);
    trace("sampler feedback tier %u, mesh shader tier %u\n", options.SamplerFeedbackTier, options.MeshShaderTier);
    hr = device->QueryInterface(__uuidof(ID3D12Device8), reinterpret_cast<void **>(&device8));
    ok(hr == S_OK && device8, "ID3D12Device8: %#lx\n", hr);
    if (!device8)
    {
        device->Release();
        return;
    }

    desc = FeedbackDesc(DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, FEEDBACK_REGION);
    if (options.SamplerFeedbackTier == D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED)
    {
        hr = CreateFeedback(device8, &desc, &feedback);
        ok(hr == E_INVALIDARG && !feedback, "feedback map without support: %#lx\n", hr);
        if (feedback) feedback->Release();
        skip("no sampler feedback\n");
        device8->Release();
        device->Release();
        return;
    }

    desc.SamplerFeedbackMipRegion.Width = 12;
    hr = CreateFeedback(device8, &desc, &feedback);
    ok(hr == E_INVALIDARG && !feedback, "mip region width 12: %#lx\n", hr);
    desc.SamplerFeedbackMipRegion.Width = FEEDBACK_SIZE;
    hr = CreateFeedback(device8, &desc, &feedback);
    ok(hr == E_INVALIDARG && !feedback, "mip region as wide as the texture: %#lx\n", hr);
    desc.SamplerFeedbackMipRegion.Width = 2;
    hr = CreateFeedback(device8, &desc, &feedback);
    ok(hr == E_INVALIDARG && !feedback, "mip region width 2: %#lx\n", hr);
    desc = FeedbackDesc(DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, FEEDBACK_REGION);
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = CreateFeedback(device8, &desc, &feedback);
    ok(hr == S_OK && feedback, "feedback map without unordered access: %#lx\n", hr);
    if (feedback) feedback->Release();
    feedback = NULL;
    desc = FeedbackDesc(DXGI_FORMAT_R8G8B8A8_UNORM, FEEDBACK_REGION);
    hr = CreateFeedback(device8, &desc, &feedback);
    ok(hr == E_INVALIDARG && !feedback, "mip region on a color texture: %#lx\n", hr);

    desc = FeedbackDesc(DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, FEEDBACK_REGION);
    device8->GetResourceAllocationInfo2(&info, 0, 1, &desc, NULL);
    ok(info.SizeInBytes && info.SizeInBytes != ~0ull, "MinMip allocation size %#I64x\n", info.SizeInBytes);
    hr = CreateFeedback(device8, &desc, &feedback);
    ok(hr == S_OK && feedback, "MinMip feedback map: %#lx\n", hr);
    desc = FeedbackDesc(DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = CreateFeedback(device8, &desc, &paired);
    ok(hr == S_OK && paired, "paired texture: %#lx\n", hr);
    if (feedback)
    {
        ok(feedback->GetDesc().Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE, "feedback format %u\n",
                feedback->GetDesc().Format);
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap_desc.NumDescriptors = 1;
        hr = device->CreateDescriptorHeap(&heap_desc, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void **>(&heap));
        ok(hr == S_OK, "descriptor heap: %#lx\n", hr);
        if (heap && paired)
            device8->CreateSamplerFeedbackUnorderedAccessView(paired, feedback, heap->GetCPUDescriptorHandleForHeapStart());
        TestTranscode(feedback);
    }
    {
        ID3D12Resource *used = NULL;

        desc = FeedbackDesc(DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE, FEEDBACK_REGION);
        hr = CreateFeedback(device8, &desc, &used);
        ok(hr == S_OK && used, "MipRegionUsed feedback map: %#lx\n", hr);
        if (used) used->Release();
    }
    ok(device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n", device->GetDeviceRemovedReason());
    if (heap) heap->Release();
    if (paired) paired->Release();
    if (feedback) feedback->Release();
    device8->Release();
    device->Release();
}

START_TEST(feedback)
{
    d3d12_test_bounded("feedback", RunFeedback, NULL, D3D12_TEST_BUDGET);
}
