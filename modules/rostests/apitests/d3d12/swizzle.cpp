/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Textures with the 64KB standard swizzle layout
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

#define SWIZZLE_SIZE 64u
#define SWIZZLE_PITCH (SWIZZLE_SIZE * 4u)

static D3D12_RESOURCE_DESC SwizzleDesc(void)
{
    D3D12_RESOURCE_DESC desc = {};

    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = SWIZZLE_SIZE;
    desc.Height = SWIZZLE_SIZE;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE;
    return desc;
}

static HRESULT CreateTexture(ID3D12Device *device, const D3D12_HEAP_PROPERTIES *heap, const D3D12_RESOURCE_DESC *desc,
        D3D12_RESOURCE_STATES state, ID3D12Resource **resource)
{
    *resource = NULL;
    return device->CreateCommittedResource(heap, D3D12_HEAP_FLAG_NONE, desc, state, NULL,
            __uuidof(ID3D12Resource), reinterpret_cast<void **>(resource));
}

static ID3D12Resource *CreateBuffer(ID3D12Device *device, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *resource = NULL;

    heap.Type = type;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = SWIZZLE_PITCH * SWIZZLE_SIZE;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, NULL,
            __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    return resource;
}

static DWORD Texel(UINT x, UINT y)
{
    return 0xff000000u | (y << 8) | x;
}

static void CheckCreate(ID3D12Device *device, const char *name, const D3D12_RESOURCE_DESC *desc, HRESULT expected)
{
    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
    ID3D12Resource *resource;
    HRESULT hr;

    hr = CreateTexture(device, &heap, desc, D3D12_RESOURCE_STATE_COMMON, &resource);
    ok(hr == expected, "%s: got %#lx, expected %#lx\n", name, hr, expected);
    if (resource) resource->Release();
}

static UINT Deposit(UINT value, UINT mask)
{
    UINT result = 0, bit;

    for (bit = 1; mask; bit <<= 1)
    {
        UINT lowest = mask & (0u - mask);

        if (value & bit) result |= lowest;
        mask &= mask - 1;
    }
    return result;
}

static void CheckGpuRoundTrip(struct d3d12_test_context *context, ID3D12Resource *texture)
{
    ID3D12Resource *upload = CreateBuffer(context->device, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource *readback = CreateBuffer(context->device, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION buffer_location = {}, texture_location = {};
    UINT x, y, mismatches = 0;
    DWORD *data = NULL;

    if (!upload || !readback || FAILED(upload->Map(0, NULL, reinterpret_cast<void **>(&data))))
    {
        ok(0, "GPU round trip buffers failed\n");
        if (upload) upload->Release();
        if (readback) readback->Release();
        return;
    }
    for (y = 0; y < SWIZZLE_SIZE; ++y)
        for (x = 0; x < SWIZZLE_SIZE; ++x)
            data[y * SWIZZLE_SIZE + x] = Texel(x, y);
    upload->Unmap(0, NULL);

    buffer_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    buffer_location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    buffer_location.PlacedFootprint.Footprint.Width = SWIZZLE_SIZE;
    buffer_location.PlacedFootprint.Footprint.Height = SWIZZLE_SIZE;
    buffer_location.PlacedFootprint.Footprint.Depth = 1;
    buffer_location.PlacedFootprint.Footprint.RowPitch = SWIZZLE_PITCH;
    texture_location.pResource = texture;
    texture_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    buffer_location.pResource = upload;
    context->list->CopyTextureRegion(&texture_location, 0, 0, 0, &buffer_location, NULL);
    d3d12_test_transition(context->list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    buffer_location.pResource = readback;
    context->list->CopyTextureRegion(&buffer_location, 0, 0, 0, &texture_location, NULL);
    ok(d3d12_test_execute(context, context->list, D3D12_TEST_WAIT), "GPU round trip did not complete\n");
    context->allocator->Reset();
    context->list->Reset(context->allocator, NULL);

    if (SUCCEEDED(readback->Map(0, NULL, reinterpret_cast<void **>(&data))))
    {
        for (y = 0; y < SWIZZLE_SIZE; ++y)
            for (x = 0; x < SWIZZLE_SIZE; ++x)
                if (data[y * SWIZZLE_SIZE + x] != Texel(x, y)) ++mismatches;
        readback->Unmap(0, NULL);
    }
    ok(mismatches == 0, "GPU round trip: %u texels differ\n", mismatches);
    upload->Release();
    readback->Release();
}

static void CheckCpuAccess(ID3D12Device *device)
{
    static DWORD source[SWIZZLE_SIZE * SWIZZLE_SIZE], result[SWIZZLE_SIZE * SWIZZLE_SIZE];
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = SwizzleDesc();
    ID3D12Resource *texture;
    UINT i, x, y, mismatches = 0;
    DWORD *pointer = NULL;
    HRESULT hr;

    heap.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    hr = CreateTexture(device, &heap, &desc, D3D12_RESOURCE_STATE_COMMON, &texture);
    ok(hr == S_OK, "CPU-visible texture: %#lx\n", hr);
    if (!texture) return;

    hr = texture->Map(0, NULL, NULL);
    ok(hr == S_OK, "Map(NULL): %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        for (y = 0; y < SWIZZLE_SIZE; ++y)
            for (x = 0; x < SWIZZLE_SIZE; ++x)
                source[y * SWIZZLE_SIZE + x] = Texel(x, y);
        hr = texture->WriteToSubresource(0, NULL, source, SWIZZLE_PITCH, SWIZZLE_PITCH * SWIZZLE_SIZE);
        ok(hr == S_OK, "WriteToSubresource: %#lx\n", hr);
        hr = texture->ReadFromSubresource(result, SWIZZLE_PITCH, SWIZZLE_PITCH * SWIZZLE_SIZE, 0, NULL);
        ok(hr == S_OK, "ReadFromSubresource: %#lx\n", hr);
        for (i = 0; i < SWIZZLE_SIZE * SWIZZLE_SIZE; ++i)
            if (result[i] != source[i]) ++mismatches;
        ok(mismatches == 0, "CPU round trip: %u texels differ\n", mismatches);
        texture->Unmap(0, NULL);
    }

    hr = texture->Map(0, NULL, reinterpret_cast<void **>(&pointer));
    ok(hr == S_OK && pointer, "Map(pointer): %#lx %p\n", hr, pointer);
    if (SUCCEEDED(hr) && pointer)
    {
        mismatches = 0;
        for (y = 0; y < SWIZZLE_SIZE; ++y)
            for (x = 0; x < SWIZZLE_SIZE; ++x)
                if (pointer[(Deposit(x * 4, 0xaa8f) + Deposit(y, 0x5570)) / 4] != Texel(x, y)) ++mismatches;
        ok(mismatches == 0, "standard swizzle layout through Map: %u texels differ\n", mismatches);
        texture->Unmap(0, NULL);
    }
    texture->Release();
}

static void RunSwizzle(void *)
{
    D3D12_HEAP_PROPERTIES default_heap = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    D3D12_RESOURCE_ALLOCATION_INFO info;
    struct d3d12_test_context context;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *texture;
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    context.device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));
    trace("StandardSwizzle64KBSupported %d, TiledResourcesTier %d, UMA %d, CacheCoherentUMA %d\n",
            options.StandardSwizzle64KBSupported, options.TiledResourcesTier, architecture.UMA,
            architecture.CacheCoherentUMA);
    if (!options.StandardSwizzle64KBSupported)
    {
        skip("64KB standard swizzle is not supported\n");
        d3d12_test_destroy(&context);
        return;
    }

    desc = SwizzleDesc();
    info = context.device->GetResourceAllocationInfo(0, 1, &desc);
    ok(info.SizeInBytes == 0x10000 && info.Alignment == 0x10000, "allocation info: size %#I64x, alignment %#I64x\n",
            info.SizeInBytes, info.Alignment);

    desc = SwizzleDesc();
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    desc.Height = 1;
    CheckCreate(context.device, "1D", &desc, E_INVALIDARG);
    desc = SwizzleDesc();
    desc.SampleDesc.Count = 4;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    CheckCreate(context.device, "MSAA", &desc, E_INVALIDARG);
    desc = SwizzleDesc();
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    CheckCreate(context.device, "depth", &desc, E_INVALIDARG);
    desc = SwizzleDesc();
    desc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
    CheckCreate(context.device, "R32G32B32", &desc, E_INVALIDARG);
    desc = SwizzleDesc();
    desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
    CheckCreate(context.device, "4KB alignment", &desc, E_INVALIDARG);
    desc = SwizzleDesc();
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = desc.Height = 16;
    desc.DepthOrArraySize = 16;
    CheckCreate(context.device, "3D", &desc, S_OK);
    desc = SwizzleDesc();
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    CheckCreate(context.device, "RT+UAV", &desc, S_OK);
    desc = SwizzleDesc();
    desc.MipLevels = 7;
    desc.DepthOrArraySize = 3;
    CheckCreate(context.device, "mips+array", &desc, S_OK);

    desc = SwizzleDesc();
    hr = CreateTexture(context.device, &default_heap, &desc, D3D12_RESOURCE_STATE_COPY_DEST, &texture);
    ok(hr == S_OK, "default heap texture: %#lx\n", hr);
    if (texture)
    {
        ok(texture->GetDesc().Alignment == 0x10000, "desc alignment %#I64x\n", texture->GetDesc().Alignment);
        CheckGpuRoundTrip(&context, texture);
        texture->Release();
    }
    CheckCpuAccess(context.device);

    ok(context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            context.device->GetDeviceRemovedReason());
    d3d12_test_destroy(&context);
}

START_TEST(swizzle)
{
    d3d12_test_bounded("swizzle", RunSwizzle, NULL, D3D12_TEST_BUDGET);
}
