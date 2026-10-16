/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ID3D12Device6 to ID3D12Device14, command lists 5 to 10, fences 1 and shader cache sessions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static const GUID cache_memory = { 0x6f1b2c3d, 0x1a2b, 0x4c5d, { 0x8e, 0x9f, 0x01, 0x23, 0x45, 0x67, 0x89, 0xa1 } };
static const GUID cache_small = { 0x6f1b2c3d, 0x1a2b, 0x4c5d, { 0x8e, 0x9f, 0x01, 0x23, 0x45, 0x67, 0x89, 0xa2 } };
static const GUID cache_disk = { 0x6f1b2c3d, 0x1a2b, 0x4c5d, { 0x8e, 0x9f, 0x01, 0x23, 0x45, 0x67, 0x89, 0xa3 } };
static const GUID cache_other = { 0x6f1b2c3d, 0x1a2b, 0x4c5d, { 0x8e, 0x9f, 0x01, 0x23, 0x45, 0x67, 0x89, 0xa4 } };
static const GUID queue_creator = { 0x6f1b2c3d, 0x1a2b, 0x4c5d, { 0x8e, 0x9f, 0x01, 0x23, 0x45, 0x67, 0x89, 0xa5 } };
static const GUID hardware_protected = { 0x62b0084e, 0xc70e, 0x4daa, { 0xa1, 0x09, 0x30, 0xff, 0x8d, 0x5a, 0x04, 0x82 } };
static const char cache_key[] = "key-one";

static BOOL DeveloperModeResult(HRESULT hr)
{
    return hr == DXGI_ERROR_SDK_COMPONENT_MISSING || hr == S_OK;
}

static D3D12_RESOURCE_DESC1 TextureDesc1(UINT width, UINT height, DXGI_FORMAT format)
{
    D3D12_RESOURCE_DESC1 desc = {};

    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    return desc;
}

static D3D12_RESOURCE_DESC1 BufferDesc1(UINT64 size)
{
    D3D12_RESOURCE_DESC1 desc = {};

    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

static D3D12_RESOURCE_DESC ResourceDesc(const D3D12_RESOURCE_DESC1 &desc1)
{
    D3D12_RESOURCE_DESC desc;

    desc.Dimension = desc1.Dimension;
    desc.Alignment = desc1.Alignment;
    desc.Width = desc1.Width;
    desc.Height = desc1.Height;
    desc.DepthOrArraySize = desc1.DepthOrArraySize;
    desc.MipLevels = desc1.MipLevels;
    desc.Format = desc1.Format;
    desc.SampleDesc = desc1.SampleDesc;
    desc.Layout = desc1.Layout;
    desc.Flags = desc1.Flags;
    return desc;
}

static void TestInterfaces(ID3D12Device *device)
{
    const struct { const GUID *iid; const char *name; } devices[] =
    {
        { &__uuidof(ID3D12Device1), "ID3D12Device1" }, { &__uuidof(ID3D12Device2), "ID3D12Device2" },
        { &__uuidof(ID3D12Device3), "ID3D12Device3" }, { &__uuidof(ID3D12Device4), "ID3D12Device4" },
        { &__uuidof(ID3D12Device5), "ID3D12Device5" }, { &__uuidof(ID3D12Device6), "ID3D12Device6" },
        { &__uuidof(ID3D12Device7), "ID3D12Device7" }, { &__uuidof(ID3D12Device8), "ID3D12Device8" },
        { &__uuidof(ID3D12Device9), "ID3D12Device9" }, { &__uuidof(ID3D12Device10), "ID3D12Device10" },
        { &__uuidof(ID3D12Device11), "ID3D12Device11" }, { &__uuidof(ID3D12Device12), "ID3D12Device12" },
        { &__uuidof(ID3D12Device13), "ID3D12Device13" }, { &__uuidof(ID3D12Device14), "ID3D12Device14" },
    };
    const struct { const GUID *iid; const char *name; } lists[] =
    {
        { &__uuidof(ID3D12GraphicsCommandList1), "ID3D12GraphicsCommandList1" },
        { &__uuidof(ID3D12GraphicsCommandList2), "ID3D12GraphicsCommandList2" },
        { &__uuidof(ID3D12GraphicsCommandList3), "ID3D12GraphicsCommandList3" },
        { &__uuidof(ID3D12GraphicsCommandList4), "ID3D12GraphicsCommandList4" },
        { &__uuidof(ID3D12GraphicsCommandList5), "ID3D12GraphicsCommandList5" },
        { &__uuidof(ID3D12GraphicsCommandList6), "ID3D12GraphicsCommandList6" },
        { &__uuidof(ID3D12GraphicsCommandList7), "ID3D12GraphicsCommandList7" },
        { &__uuidof(ID3D12GraphicsCommandList8), "ID3D12GraphicsCommandList8" },
        { &__uuidof(ID3D12GraphicsCommandList9), "ID3D12GraphicsCommandList9" },
        { &__uuidof(ID3D12GraphicsCommandList10), "ID3D12GraphicsCommandList10" },
    };
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    IUnknown *object;
    HRESULT hr;

    for (UINT i = 0; i < ARRAYSIZE(devices); ++i)
    {
        object = NULL;
        hr = device->QueryInterface(*devices[i].iid, reinterpret_cast<void **>(&object));
        ok(hr == S_OK, "%s: %#lx\n", devices[i].name, hr);
        if (object) object->Release();
    }
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
            reinterpret_cast<void **>(&allocator));
    if (allocator)
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL,
                __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void **>(&list));
    ok(list != NULL, "CreateCommandList failed\n");
    for (UINT i = 0; list && i < ARRAYSIZE(lists); ++i)
    {
        object = NULL;
        hr = list->QueryInterface(*lists[i].iid, reinterpret_cast<void **>(&object));
        ok(hr == S_OK, "%s: %#lx\n", lists[i].name, hr);
        if (object) object->Release();
    }
    if (list) list->Release();
    if (allocator) allocator->Release();
}

static void TestFences(ID3D12Device *device)
{
    static const D3D12_FENCE_FLAGS flags[] = { D3D12_FENCE_FLAG_NONE, D3D12_FENCE_FLAG_SHARED };

    for (UINT i = 0; i < ARRAYSIZE(flags); ++i)
    {
        ID3D12Fence1 *fence = NULL;
        HRESULT hr = device->CreateFence(0, flags[i], __uuidof(ID3D12Fence1), reinterpret_cast<void **>(&fence));
        ok(hr == S_OK, "CreateFence(%#x) as ID3D12Fence1: %#lx\n", flags[i], hr);
        if (!fence) continue;
        ok(fence->GetCreationFlags() == flags[i], "GetCreationFlags: %#x, expected %#x\n",
                fence->GetCreationFlags(), flags[i]);
        fence->Release();
    }
}

static void TestBackgroundProcessing(ID3D12Device *device)
{
    ID3D12Device6 *device6 = NULL;
    HANDLE event;
    BOOL further;
    HRESULT hr;

    if (FAILED(device->QueryInterface(__uuidof(ID3D12Device6), reinterpret_cast<void **>(&device6)))) return;
    event = CreateEventW(NULL, TRUE, FALSE, NULL);

    further = 0x55;
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
            D3D12_MEASUREMENTS_ACTION_KEEP_ALL, NULL, &further);
    ok(hr == S_OK, "ALLOWED, KEEP_ALL: %#lx\n", hr);
    ok(further == 0x55, "KEEP_ALL wrote further measurements %d\n", further);
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
            D3D12_MEASUREMENTS_ACTION_KEEP_ALL, event, NULL);
    ok(hr == E_INVALIDARG, "KEEP_ALL with an event: %#lx\n", hr);
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
            D3D12_MEASUREMENTS_ACTION_DISCARD_PREVIOUS, event, NULL);
    ok(hr == E_INVALIDARG, "DISCARD_PREVIOUS with an event: %#lx\n", hr);
    ok(WaitForSingleObject(event, 0) == WAIT_TIMEOUT, "rejected calls signaled the event\n");

    further = 0x55;
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOW_INTRUSIVE_MEASUREMENTS,
            D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS, event, &further);
    ok(hr == S_OK, "COMMIT_RESULTS with an event: %#lx\n", hr);
    ok(further == FALSE, "COMMIT_RESULTS further measurements %d\n", further);
    ok(WaitForSingleObject(event, 0) == WAIT_OBJECT_0, "COMMIT_RESULTS did not signal the event\n");
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
            D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS, NULL, NULL);
    ok(hr == S_OK, "COMMIT_RESULTS without an event: %#lx\n", hr);

    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_BACKGROUND_WORK,
            D3D12_MEASUREMENTS_ACTION_KEEP_ALL, NULL, NULL);
    ok(DeveloperModeResult(hr), "DISABLE_BACKGROUND_WORK: %#lx\n", hr);
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_DISABLE_PROFILING_BY_SYSTEM,
            D3D12_MEASUREMENTS_ACTION_KEEP_ALL, NULL, NULL);
    ok(DeveloperModeResult(hr), "DISABLE_PROFILING_BY_SYSTEM: %#lx\n", hr);
    hr = device6->SetBackgroundProcessingMode(static_cast<D3D12_BACKGROUND_PROCESSING_MODE>(7),
            D3D12_MEASUREMENTS_ACTION_KEEP_ALL, NULL, NULL);
    ok(hr == E_INVALIDARG, "invalid mode: %#lx\n", hr);
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
            static_cast<D3D12_MEASUREMENTS_ACTION>(9), NULL, NULL);
    ok(hr == E_INVALIDARG, "invalid action: %#lx\n", hr);
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
            D3D12_MEASUREMENTS_ACTION_KEEP_ALL, NULL, NULL);
    ok(hr == S_OK, "restoring ALLOWED: %#lx\n", hr);

    CloseHandle(event);
    device6->Release();
}

static void TestProtectedSessions(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT support = {};
    D3D12_PROTECTED_RESOURCE_SESSION_DESC1 desc1 = {};
    D3D12_PROTECTED_RESOURCE_SESSION_DESC desc = {};
    ID3D12Device7 *device7 = NULL;
    IUnknown *session;
    HRESULT hr;

    hr = device->CheckFeatureSupport(D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT, &support, sizeof(support));
    ok(hr == S_OK, "PROTECTED_RESOURCE_SESSION_SUPPORT: %#lx\n", hr);
    if (support.Support != D3D12_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_NONE)
    {
        trace("Protected resource sessions are supported, skipping the unsupported case\n");
        return;
    }
    if (FAILED(device->QueryInterface(__uuidof(ID3D12Device7), reinterpret_cast<void **>(&device7)))) return;
    session = reinterpret_cast<IUnknown *>(0x1);
    hr = device7->CreateProtectedResourceSession(&desc, __uuidof(ID3D12ProtectedResourceSession),
            reinterpret_cast<void **>(&session));
    ok(hr == E_INVALIDARG, "CreateProtectedResourceSession: %#lx\n", hr);
    ok(session == reinterpret_cast<IUnknown *>(0x1), "CreateProtectedResourceSession wrote %p\n", session);
    desc1.ProtectionType = hardware_protected;
    hr = device7->CreateProtectedResourceSession1(&desc1, __uuidof(ID3D12ProtectedResourceSession1),
            reinterpret_cast<void **>(&session));
    ok(hr == E_INVALIDARG, "CreateProtectedResourceSession1: %#lx\n", hr);
    ok(session == reinterpret_cast<IUnknown *>(0x1), "CreateProtectedResourceSession1 wrote %p\n", session);
    device7->Release();
}

static void TestResourceDesc1(ID3D12Device *device)
{
    D3D12_HEAP_PROPERTIES heap_properties = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC1 texture = TextureDesc1(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D12_RESOURCE_DESC1 descs1[2] = { texture, texture };
    D3D12_RESOURCE_DESC descs[2] = { ResourceDesc(texture), ResourceDesc(texture) };
    D3D12_RESOURCE_ALLOCATION_INFO1 infos[2] = {}, infos1[2] = {};
    D3D12_RESOURCE_ALLOCATION_INFO info, info1;
    ID3D12Device4 *device4 = NULL;
    ID3D12Device8 *device8 = NULL;
    ID3D12Resource2 *resource2;
    ID3D12Resource *resource;
    ID3D12Heap *heap = NULL;
    D3D12_HEAP_DESC heap_desc = {};
    HRESULT hr;

    if (FAILED(device->QueryInterface(__uuidof(ID3D12Device8), reinterpret_cast<void **>(&device8)))) return;
    if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D12Device4), reinterpret_cast<void **>(&device4))))
    {
        info = device4->GetResourceAllocationInfo1(0, 2, descs, infos);
        info1 = device8->GetResourceAllocationInfo2(0, 2, descs1, infos1);
        ok(info1.SizeInBytes == info.SizeInBytes && info1.Alignment == info.Alignment,
                "GetResourceAllocationInfo2 %I64u/%I64u, GetResourceAllocationInfo1 %I64u/%I64u\n",
                info1.SizeInBytes, info1.Alignment, info.SizeInBytes, info.Alignment);
        {
            ID3D12Device12 *device12 = NULL;
            if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D12Device12), reinterpret_cast<void **>(&device12))))
            {
                D3D12_RESOURCE_ALLOCATION_INFO info3 = device12->GetResourceAllocationInfo3(0, 2, descs1, NULL, NULL, NULL);
                ok(info3.SizeInBytes == info.SizeInBytes && info3.Alignment == info.Alignment,
                        "GetResourceAllocationInfo3 %I64u/%I64u\n", info3.SizeInBytes, info3.Alignment);
                device12->Release();
            }
        }
        for (UINT i = 0; i < 2; ++i)
        {
            ok(infos1[i].Offset == infos[i].Offset && infos1[i].SizeInBytes == infos[i].SizeInBytes
                    && infos1[i].Alignment == infos[i].Alignment, "resource %u: offset %I64u/%I64u size %I64u/%I64u\n",
                    i, infos1[i].Offset, infos[i].Offset, infos1[i].SizeInBytes, infos[i].SizeInBytes);
        }
        device4->Release();
    }
    descs1[1].SamplerFeedbackMipRegion.Width = 4;
    descs1[1].SamplerFeedbackMipRegion.Height = 4;
    descs1[1].SamplerFeedbackMipRegion.Depth = 1;
    info1 = device8->GetResourceAllocationInfo2(0, 2, descs1, NULL);
    ok(info1.SizeInBytes == ~(UINT64)0, "mip region on a colour format: size %#I64x\n", info1.SizeInBytes);

    resource = NULL;
    hr = device8->CreateCommittedResource2(&heap_properties, D3D12_HEAP_FLAG_NONE, &texture,
            D3D12_RESOURCE_STATE_COMMON, NULL, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    ok(hr == S_OK, "CreateCommittedResource2: %#lx\n", hr);
    if (resource)
    {
        resource2 = NULL;
        hr = resource->QueryInterface(__uuidof(ID3D12Resource2), reinterpret_cast<void **>(&resource2));
        ok(hr == S_OK, "ID3D12Resource2: %#lx\n", hr);
        if (resource2)
        {
            D3D12_RESOURCE_DESC1 desc = resource2->GetDesc1();
            ok(desc.Dimension == texture.Dimension && desc.Width == texture.Width && desc.Height == texture.Height
                    && desc.Format == texture.Format && !desc.SamplerFeedbackMipRegion.Width,
                    "GetDesc1: dimension %u, %I64ux%u, format %u, mip region width %u\n", desc.Dimension, desc.Width,
                    desc.Height, desc.Format, desc.SamplerFeedbackMipRegion.Width);
            resource2->Release();
        }
        resource->Release();
    }
    {
        D3D12_RESOURCE_DESC1 buffer = BufferDesc1(65536);
        resource = NULL;
        hr = device8->CreateCommittedResource2(&heap_properties, D3D12_HEAP_FLAG_NONE, &buffer,
                D3D12_RESOURCE_STATE_COMMON, NULL, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
        ok(hr == S_OK, "CreateCommittedResource2 buffer: %#lx\n", hr);
        if (resource) resource->Release();
    }
    resource = reinterpret_cast<ID3D12Resource *>(0x1);
    hr = device8->CreateCommittedResource2(&heap_properties, D3D12_HEAP_FLAG_NONE, &descs1[1],
            D3D12_RESOURCE_STATE_COMMON, NULL, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    ok(hr == E_INVALIDARG, "CreateCommittedResource2 with a mip region: %#lx\n", hr);
    ok(!resource, "CreateCommittedResource2 with a mip region returned %p\n", resource);
    if (SUCCEEDED(hr) && resource) resource->Release();

    heap_desc.SizeInBytes = 1 << 20;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    hr = device->CreateHeap(&heap_desc, __uuidof(ID3D12Heap), reinterpret_cast<void **>(&heap));
    ok(hr == S_OK, "CreateHeap: %#lx\n", hr);
    if (heap)
    {
        resource = NULL;
        hr = device8->CreatePlacedResource1(heap, 65536, &texture, D3D12_RESOURCE_STATE_COMMON, NULL,
                __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
        ok(hr == S_OK, "CreatePlacedResource1: %#lx\n", hr);
        if (resource) resource->Release();
        heap->Release();
    }
    {
        D3D12_RESOURCE_DESC1 block = TextureDesc1(36, 20, DXGI_FORMAT_BC1_UNORM);
        D3D12_RESOURCE_DESC block0 = ResourceDesc(block);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {}, layout1 = {};
        UINT rows = 0, rows1 = 0;
        UINT64 row_size = 0, row_size1 = 0, total = 0, total1 = 0;

        device->GetCopyableFootprints(&block0, 0, 1, 256, &layout, &rows, &row_size, &total);
        device8->GetCopyableFootprints1(&block, 0, 1, 256, &layout1, &rows1, &row_size1, &total1);
        ok(layout1.Offset == layout.Offset && layout1.Footprint.RowPitch == layout.Footprint.RowPitch
                && layout1.Footprint.Width == layout.Footprint.Width && rows1 == rows && row_size1 == row_size
                && total1 == total, "GetCopyableFootprints1: pitch %u/%u, rows %u/%u, total %I64u/%I64u\n",
                layout1.Footprint.RowPitch, layout.Footprint.RowPitch, rows1, rows, total1, total);
    }
    device8->Release();
}

static void TestCommandQueue1(ID3D12Device *device)
{
    D3D12_COMMAND_QUEUE_DESC desc = {};
    ID3D12Device9 *device9 = NULL;
    ID3D12CommandQueue *queue;
    HRESULT hr;

    if (FAILED(device->QueryInterface(__uuidof(ID3D12Device9), reinterpret_cast<void **>(&device9)))) return;
    queue = NULL;
    hr = device9->CreateCommandQueue1(&desc, queue_creator, __uuidof(ID3D12CommandQueue),
            reinterpret_cast<void **>(&queue));
    ok(hr == S_OK, "CreateCommandQueue1: %#lx\n", hr);
    if (queue)
    {
        ok(queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT, "queue type %u\n", queue->GetDesc().Type);
        queue->Release();
    }
    desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    queue = NULL;
    hr = device9->CreateCommandQueue1(&desc, GUID_NULL, __uuidof(ID3D12CommandQueue), reinterpret_cast<void **>(&queue));
    ok(hr == S_OK, "CreateCommandQueue1 without a creator: %#lx\n", hr);
    if (queue)
    {
        ok(queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_COMPUTE, "queue type %u\n", queue->GetDesc().Type);
        queue->Release();
    }
    device9->Release();
}

static ID3D12ShaderCacheSession *CreateSession(ID3D12Device9 *device9, const GUID &id, D3D12_SHADER_CACHE_MODE mode,
        D3D12_SHADER_CACHE_FLAGS flags, UINT entries, UINT memory, UINT64 version, HRESULT *result)
{
    D3D12_SHADER_CACHE_SESSION_DESC desc = {};
    ID3D12ShaderCacheSession *session = NULL;

    desc.Identifier = id;
    desc.Mode = mode;
    desc.Flags = flags;
    desc.MaximumInMemoryCacheEntries = entries;
    desc.MaximumInMemoryCacheSizeBytes = memory;
    desc.Version = version;
    *result = device9->CreateShaderCacheSession(&desc, __uuidof(ID3D12ShaderCacheSession),
            reinterpret_cast<void **>(&session));
    return session;
}

static HRESULT FindValue(ID3D12ShaderCacheSession *session, const void *key, UINT key_size, UINT *size)
{
    BYTE value[256];

    *size = sizeof(value);
    return session->FindValue(key, key_size, value, size);
}

static void TestMemoryCache(ID3D12Device *device, ID3D12Device9 *device9)
{
    ID3D12ShaderCacheSession *session, *second;
    D3D12_SHADER_CACHE_SESSION_DESC desc = {};
    BYTE value[16], out[64], big[2048];
    ID3D12Device *owner;
    IUnknown *object;
    UINT size;
    HRESULT hr;

    for (UINT i = 0; i < sizeof(value); ++i) value[i] = static_cast<BYTE>(0x40 + i);
    memset(big, 0x5a, sizeof(big));

    session = CreateSession(device9, cache_memory, D3D12_SHADER_CACHE_MODE_MEMORY, D3D12_SHADER_CACHE_FLAG_NONE, 0, 0,
            0, &hr);
    ok(hr == S_OK, "memory session: %#lx\n", hr);
    if (!session) return;
    desc = session->GetDesc();
    ok(desc.MaximumInMemoryCacheSizeBytes == 1048576 && desc.MaximumInMemoryCacheEntries == 128
            && desc.MaximumValueFileSizeBytes == 134217728, "default limits %u, %u, %u\n",
            desc.MaximumInMemoryCacheSizeBytes, desc.MaximumInMemoryCacheEntries, desc.MaximumValueFileSizeBytes);
    owner = NULL;
    hr = session->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void **>(&owner));
    ok(hr == S_OK && owner == device, "GetDevice: %#lx, %p\n", hr, owner);
    if (owner) owner->Release();

    size = 0x77;
    hr = session->FindValue(cache_key, sizeof(cache_key), NULL, &size);
    ok(hr == DXGI_ERROR_NOT_FOUND && size == 0x77, "missing key: %#lx, size %u\n", hr, size);
    hr = session->StoreValue(cache_key, sizeof(cache_key), value, sizeof(value));
    ok(hr == S_OK, "StoreValue: %#lx\n", hr);
    size = 0;
    hr = session->FindValue(cache_key, sizeof(cache_key), NULL, &size);
    ok(hr == S_OK && size == sizeof(value), "size query: %#lx, size %u\n", hr, size);
    size = sizeof(out);
    memset(out, 0, sizeof(out));
    hr = session->FindValue(cache_key, sizeof(cache_key), out, &size);
    ok(hr == S_OK && size == sizeof(value) && !memcmp(out, value, sizeof(value)), "FindValue: %#lx, size %u\n", hr, size);
    size = 4;
    memset(out, 0, sizeof(out));
    hr = session->FindValue(cache_key, sizeof(cache_key), out, &size);
    ok(hr == DXGI_ERROR_MORE_DATA && size == sizeof(value) && !out[0], "small buffer: %#lx, size %u\n", hr, size);
    hr = session->StoreValue(cache_key, sizeof(cache_key), value, 8);
    ok(hr == DXGI_ERROR_ALREADY_EXISTS, "duplicate key: %#lx\n", hr);
    hr = session->StoreValue(cache_key, 0, value, sizeof(value));
    ok(hr == E_INVALIDARG, "empty key: %#lx\n", hr);
    hr = session->StoreValue("k2", 3, value, 0);
    ok(hr == E_INVALIDARG, "empty value: %#lx\n", hr);
    hr = session->StoreValue(NULL, 1, value, 8);
    ok(hr == E_INVALIDARG, "NULL key: %#lx\n", hr);
    hr = session->StoreValue("k2", 3, NULL, 8);
    ok(hr == E_INVALIDARG, "NULL value: %#lx\n", hr);
    size = 8;
    hr = session->FindValue(NULL, 1, out, &size);
    ok(hr == E_INVALIDARG, "FindValue with a NULL key: %#lx\n", hr);
    hr = session->FindValue(cache_key, sizeof(cache_key), out, NULL);
    ok(hr == E_INVALIDARG, "FindValue with a NULL size: %#lx\n", hr);
    hr = session->FindValue(cache_key, 0, out, &size);
    ok(hr == E_INVALIDARG, "FindValue with an empty key: %#lx\n", hr);
    hr = session->StoreValue("k3", 3, big, sizeof(big));
    ok(hr == S_OK, "2048-byte value: %#lx\n", hr);

    second = CreateSession(device9, cache_memory, D3D12_SHADER_CACHE_MODE_MEMORY, D3D12_SHADER_CACHE_FLAG_NONE, 7, 0, 0,
            &hr);
    ok(hr == S_OK && second && second != session, "second session: %#lx, %p\n", hr, second);
    if (second)
    {
        ok(second->GetDesc().MaximumInMemoryCacheEntries == 128, "second session entries %u\n",
                second->GetDesc().MaximumInMemoryCacheEntries);
        hr = FindValue(second, cache_key, sizeof(cache_key), &size);
        ok(hr == S_OK && size == sizeof(value), "shared value: %#lx, size %u\n", hr, size);
        second->Release();
    }
    second = CreateSession(device9, cache_memory, D3D12_SHADER_CACHE_MODE_DISK, D3D12_SHADER_CACHE_FLAG_NONE, 0, 0, 0,
            &hr);
    ok(hr == S_OK && second, "disk session on an open memory cache: %#lx\n", hr);
    if (second)
    {
        ok(second->GetDesc().Mode == D3D12_SHADER_CACHE_MODE_MEMORY, "mode %u\n", second->GetDesc().Mode);
        second->Release();
    }
    second = CreateSession(device9, cache_memory, D3D12_SHADER_CACHE_MODE_MEMORY, D3D12_SHADER_CACHE_FLAG_NONE, 0, 0, 1,
            &hr);
    ok(hr == DXGI_ERROR_ALREADY_EXISTS && !second, "other version while open: %#lx, %p\n", hr, second);
    ok(session->Release() == 0, "session still referenced\n");

    session = CreateSession(device9, cache_memory, D3D12_SHADER_CACHE_MODE_MEMORY, D3D12_SHADER_CACHE_FLAG_NONE, 0, 0,
            1, &hr);
    ok(hr == S_OK, "recreated memory session: %#lx\n", hr);
    if (session)
    {
        hr = FindValue(session, cache_key, sizeof(cache_key), &size);
        ok(hr == DXGI_ERROR_NOT_FOUND, "memory cache kept a value after its last session: %#lx\n", hr);
        session->Release();
    }

    session = CreateSession(device9, cache_small, D3D12_SHADER_CACHE_MODE_MEMORY, D3D12_SHADER_CACHE_FLAG_NONE, 1, 64,
            0, &hr);
    ok(hr == S_OK, "one-entry session: %#lx\n", hr);
    if (session)
    {
        desc = session->GetDesc();
        ok(desc.MaximumInMemoryCacheEntries == 1 && desc.MaximumInMemoryCacheSizeBytes == 64, "limits %u, %u\n",
                desc.MaximumInMemoryCacheEntries, desc.MaximumInMemoryCacheSizeBytes);
        ok(session->StoreValue("a", 1, value, sizeof(value)) == S_OK, "first entry not stored\n");
        ok(session->StoreValue("b", 1, value, sizeof(value)) == S_OK, "second entry not stored\n");
        ok(FindValue(session, "a", 1, &size) == DXGI_ERROR_NOT_FOUND, "first entry was not evicted\n");
        ok(FindValue(session, "b", 1, &size) == S_OK, "second entry was evicted\n");
        ok(session->StoreValue("c", 1, big, 128) == S_OK, "oversized entry not stored\n");
        hr = FindValue(session, "c", 1, &size);
        ok(hr == S_OK && size == 128, "oversized entry: %#lx, size %u\n", hr, size);
        session->Release();
    }

    desc = {};
    desc.Identifier = cache_other;
    desc.Mode = static_cast<D3D12_SHADER_CACHE_MODE>(5);
    object = reinterpret_cast<IUnknown *>(0x1);
    hr = device9->CreateShaderCacheSession(&desc, __uuidof(ID3D12ShaderCacheSession), reinterpret_cast<void **>(&object));
    ok(hr == E_INVALIDARG && object == reinterpret_cast<IUnknown *>(0x1), "invalid mode: %#lx, %p\n", hr, object);
    desc.Mode = D3D12_SHADER_CACHE_MODE_MEMORY;
    desc.Flags = static_cast<D3D12_SHADER_CACHE_FLAGS>(0x10);
    hr = device9->CreateShaderCacheSession(&desc, __uuidof(ID3D12ShaderCacheSession), reinterpret_cast<void **>(&object));
    ok(hr == E_INVALIDARG && object == reinterpret_cast<IUnknown *>(0x1), "invalid flags: %#lx, %p\n", hr, object);
    hr = device9->CreateShaderCacheSession(NULL, __uuidof(ID3D12ShaderCacheSession), reinterpret_cast<void **>(&object));
    ok(hr == E_INVALIDARG && object == reinterpret_cast<IUnknown *>(0x1), "NULL desc: %#lx, %p\n", hr, object);
    desc.Flags = D3D12_SHADER_CACHE_FLAG_USE_WORKING_DIR;
    object = NULL;
    hr = device9->CreateShaderCacheSession(&desc, __uuidof(ID3D12ShaderCacheSession), reinterpret_cast<void **>(&object));
    ok(hr == S_OK, "memory session with USE_WORKING_DIR: %#lx\n", hr);
    if (object) object->Release();
    desc.Flags = D3D12_SHADER_CACHE_FLAG_NONE;
    object = reinterpret_cast<IUnknown *>(0x1);
    hr = device9->CreateShaderCacheSession(&desc, __uuidof(ID3D12Device), reinterpret_cast<void **>(&object));
    ok(hr == E_NOINTERFACE && !object, "foreign interface: %#lx, %p\n", hr, object);
    hr = device9->CreateShaderCacheSession(&desc, __uuidof(ID3D12ShaderCacheSession), NULL);
    ok(hr == S_FALSE, "NULL output: %#lx\n", hr);
}

static UINT CountCacheFiles(void)
{
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA("*", &data);
    UINT count = 0;

    if (find == INVALID_HANDLE_VALUE) return 0;
    do
    {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++count;
    } while (FindNextFileA(find, &data));
    FindClose(find);
    return count;
}

static void TestDiskCache(ID3D12Device9 *device9)
{
    char previous[MAX_PATH], directory[MAX_PATH];
    ID3D12ShaderCacheSession *session;
    BYTE value[16] = { 7, 7, 7 };
    UINT size;
    HRESULT hr;

    if (!GetCurrentDirectoryA(sizeof(previous), previous) || !GetTempPathA(sizeof(directory) - 32, directory)) return;
    strcat(directory, "d3d12_apitest_cache");
    CreateDirectoryA(directory, NULL);
    if (!SetCurrentDirectoryA(directory))
    {
        skip("Cannot enter %s\n", directory);
        return;
    }
    ok(CountCacheFiles() == 0, "the cache directory is not empty\n");

    session = CreateSession(device9, cache_disk, D3D12_SHADER_CACHE_MODE_DISK, D3D12_SHADER_CACHE_FLAG_USE_WORKING_DIR,
            0, 0, 0, &hr);
    ok(hr == S_OK, "disk session: %#lx\n", hr);
    if (session)
    {
        ok(FindValue(session, cache_key, sizeof(cache_key), &size) == DXGI_ERROR_NOT_FOUND, "new disk cache has data\n");
        ok(session->StoreValue(cache_key, sizeof(cache_key), value, sizeof(value)) == S_OK, "disk StoreValue failed\n");
        session->Release();
    }
    ok(CountCacheFiles() != 0, "the disk cache wrote no file to the working directory\n");
    session = CreateSession(device9, cache_disk, D3D12_SHADER_CACHE_MODE_DISK, D3D12_SHADER_CACHE_FLAG_USE_WORKING_DIR,
            0, 0, 0, &hr);
    ok(hr == S_OK, "reopened disk session: %#lx\n", hr);
    if (session)
    {
        hr = FindValue(session, cache_key, sizeof(cache_key), &size);
        ok(hr == S_OK && size == sizeof(value), "disk value after reopening: %#lx, size %u\n", hr, size);
        session->Release();
    }
    session = CreateSession(device9, cache_disk, D3D12_SHADER_CACHE_MODE_DISK, D3D12_SHADER_CACHE_FLAG_USE_WORKING_DIR,
            0, 0, 2, &hr);
    ok(hr == S_OK, "disk session with a new version: %#lx\n", hr);
    if (session)
    {
        ok(FindValue(session, cache_key, sizeof(cache_key), &size) == DXGI_ERROR_NOT_FOUND,
                "a new version kept the old data\n");
        session->SetDeleteOnDestroy();
        session->Release();
    }
    ok(CountCacheFiles() == 0, "SetDeleteOnDestroy left %u files\n", CountCacheFiles());

    SetCurrentDirectoryA(previous);
    RemoveDirectoryA(directory);
}

static void TestShaderCache(ID3D12Device *device)
{
    ID3D12Device9 *device9 = NULL;
    HRESULT hr;

    if (FAILED(device->QueryInterface(__uuidof(ID3D12Device9), reinterpret_cast<void **>(&device9)))) return;
    TestMemoryCache(device, device9);
    TestDiskCache(device9);

    hr = device9->ShaderCacheControl(D3D12_SHADER_CACHE_KIND_FLAG_APPLICATION_MANAGED, D3D12_SHADER_CACHE_CONTROL_FLAG_CLEAR);
    ok(DeveloperModeResult(hr), "ShaderCacheControl CLEAR: %#lx\n", hr);
    hr = device9->ShaderCacheControl(D3D12_SHADER_CACHE_KIND_FLAG_IMPLICIT_DRIVER_MANAGED,
            D3D12_SHADER_CACHE_CONTROL_FLAG_DISABLE);
    ok(DeveloperModeResult(hr), "ShaderCacheControl DISABLE: %#lx\n", hr);
    hr = device9->ShaderCacheControl(D3D12_SHADER_CACHE_KIND_FLAG_APPLICATION_MANAGED,
            static_cast<D3D12_SHADER_CACHE_CONTROL_FLAGS>(D3D12_SHADER_CACHE_CONTROL_FLAG_DISABLE
            | D3D12_SHADER_CACHE_CONTROL_FLAG_ENABLE));
    ok(hr == E_INVALIDARG, "DISABLE with ENABLE: %#lx\n", hr);
    hr = device9->ShaderCacheControl(static_cast<D3D12_SHADER_CACHE_KIND_FLAGS>(0), D3D12_SHADER_CACHE_CONTROL_FLAG_CLEAR);
    ok(hr == E_INVALIDARG, "no kind: %#lx\n", hr);
    hr = device9->ShaderCacheControl(D3D12_SHADER_CACHE_KIND_FLAG_APPLICATION_MANAGED,
            static_cast<D3D12_SHADER_CACHE_CONTROL_FLAGS>(0));
    ok(hr == E_INVALIDARG, "no control: %#lx\n", hr);
    hr = device9->ShaderCacheControl(static_cast<D3D12_SHADER_CACHE_KIND_FLAGS>(0x10), D3D12_SHADER_CACHE_CONTROL_FLAG_CLEAR);
    ok(hr == E_INVALIDARG, "unknown kind: %#lx\n", hr);
    device9->Release();
}

static void TestInitialLayouts(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options = {};
    D3D12_HEAP_PROPERTIES heap_properties = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC1 texture = TextureDesc1(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D12_RESOURCE_DESC1 buffer = BufferDesc1(65536);
    ID3D12Device10 *device10 = NULL;
    ID3D12Resource *resource;
    HRESULT hr;

    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options));
    ok(hr == S_OK, "OPTIONS12: %#lx\n", hr);
    if (!options.EnhancedBarriersSupported)
    {
        trace("Enhanced barriers are not supported, skipping the initial layout cases\n");
        return;
    }
    if (FAILED(device->QueryInterface(__uuidof(ID3D12Device10), reinterpret_cast<void **>(&device10)))) return;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    resource = NULL;
    hr = device10->CreateCommittedResource3(&heap_properties, D3D12_HEAP_FLAG_NONE, &texture, D3D12_BARRIER_LAYOUT_COMMON,
            NULL, NULL, 0, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    ok(hr == S_OK, "texture in COMMON: %#lx\n", hr);
    if (resource) resource->Release();
    resource = NULL;
    hr = device10->CreateCommittedResource3(&heap_properties, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_BARRIER_LAYOUT_UNDEFINED, NULL, NULL, 0, NULL, __uuidof(ID3D12Resource),
            reinterpret_cast<void **>(&resource));
    ok(hr == S_OK, "buffer in UNDEFINED: %#lx\n", hr);
    if (resource) resource->Release();
    resource = reinterpret_cast<ID3D12Resource *>(0x1);
    hr = device10->CreateCommittedResource3(&heap_properties, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_BARRIER_LAYOUT_COMMON,
            NULL, NULL, 0, NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    ok(hr == E_INVALIDARG && !resource, "buffer in COMMON: %#lx, %p\n", hr, resource);
    device10->Release();
}

enum list_case
{
    CASE_RATE_1X1,
    CASE_RATE_2X2_COMBINERS,
    CASE_RATE_INVALID,
    CASE_RATE_4X4,
    CASE_COMBINER_INVALID,
    CASE_RATE_IMAGE,
    CASE_DISPATCH_MESH,
    CASE_BARRIER_EMPTY,
    CASE_BARRIER_GLOBAL,
    CASE_STENCIL_REFS,
    CASE_DEPTH_BIAS,
    CASE_STRIP_CUT,
};

static HRESULT CloseAfter(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type, enum list_case which)
{
    D3D12_SHADING_RATE_COMBINER combiners[2] = { D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
            D3D12_SHADING_RATE_COMBINER_PASSTHROUGH };
    ID3D12CommandAllocator *allocator = NULL;
    ID3D12GraphicsCommandList *list = NULL;
    ID3D12GraphicsCommandList10 *list7 = NULL;
    D3D12_GLOBAL_BARRIER global = {};
    D3D12_BARRIER_GROUP group = {};
    HRESULT hr;

    device->CreateCommandAllocator(type, __uuidof(ID3D12CommandAllocator), reinterpret_cast<void **>(&allocator));
    if (!allocator) return E_UNEXPECTED;
    device->CreateCommandList(0, type, allocator, NULL, __uuidof(ID3D12GraphicsCommandList),
            reinterpret_cast<void **>(&list));
    if (list) list->QueryInterface(__uuidof(ID3D12GraphicsCommandList10), reinterpret_cast<void **>(&list7));
    if (!list7)
    {
        if (list) list->Release();
        allocator->Release();
        return E_UNEXPECTED;
    }
    global.SyncBefore = D3D12_BARRIER_SYNC_ALL;
    global.SyncAfter = D3D12_BARRIER_SYNC_ALL;
    group.Type = D3D12_BARRIER_TYPE_GLOBAL;
    group.NumBarriers = 1;
    group.pGlobalBarriers = &global;
    switch (which)
    {
        case CASE_RATE_1X1: list7->RSSetShadingRate(D3D12_SHADING_RATE_1X1, NULL); break;
        case CASE_RATE_2X2_COMBINERS: list7->RSSetShadingRate(D3D12_SHADING_RATE_2X2, combiners); break;
        case CASE_RATE_INVALID: list7->RSSetShadingRate(static_cast<D3D12_SHADING_RATE>(3), NULL); break;
        case CASE_RATE_4X4: list7->RSSetShadingRate(D3D12_SHADING_RATE_4X4, NULL); break;
        case CASE_COMBINER_INVALID:
            combiners[0] = static_cast<D3D12_SHADING_RATE_COMBINER>(9);
            list7->RSSetShadingRate(D3D12_SHADING_RATE_1X1, combiners);
            break;
        case CASE_RATE_IMAGE: list7->RSSetShadingRateImage(NULL); break;
        case CASE_DISPATCH_MESH: list7->DispatchMesh(1, 1, 1); break;
        case CASE_BARRIER_EMPTY: list7->Barrier(0, NULL); break;
        case CASE_BARRIER_GLOBAL: list7->Barrier(1, &group); break;
        case CASE_STENCIL_REFS: list7->OMSetFrontAndBackStencilRef(1, 2); break;
        case CASE_DEPTH_BIAS: list7->RSSetDepthBias(1.0f, 0.0f, 1.0f); break;
        case CASE_STRIP_CUT: list7->IASetIndexBufferStripCutValue(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF); break;
    }
    hr = list->Close();
    list7->Release();
    list->Release();
    allocator->Release();
    return hr;
}

static void TestListMethods(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 options14 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS15 options15 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};
    HRESULT hr;

    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS14, &options14, sizeof(options14));
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS15, &options15, sizeof(options15));
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16));
    if (options14.IndependentFrontAndBackStencilRefMaskSupported)
    {
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_STENCIL_REFS);
        ok(hr == S_OK, "OMSetFrontAndBackStencilRef: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_BUNDLE, CASE_STENCIL_REFS);
        ok(hr == S_OK, "OMSetFrontAndBackStencilRef in a bundle: %#lx\n", hr);
    }
    if (options16.DynamicDepthBiasSupported)
    {
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_DEPTH_BIAS);
        ok(hr == S_OK, "RSSetDepthBias: %#lx\n", hr);
    }
    if (options15.DynamicIndexBufferStripCutSupported)
    {
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_STRIP_CUT);
        ok(hr == S_OK, "IASetIndexBufferStripCutValue: %#lx\n", hr);
    }

    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6));
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12));

    if (options6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_1)
    {
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_RATE_1X1);
        ok(hr == S_OK, "RSSetShadingRate 1X1: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_RATE_2X2_COMBINERS);
        ok(hr == S_OK, "RSSetShadingRate 2X2 with combiners: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_BUNDLE, CASE_RATE_1X1);
        ok(hr == S_OK, "RSSetShadingRate in a bundle: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_RATE_INVALID);
        ok(hr == E_INVALIDARG, "invalid shading rate: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_COMBINER_INVALID);
        ok(hr == E_INVALIDARG, "invalid combiner: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_RATE_4X4);
        ok(hr == (options6.AdditionalShadingRatesSupported ? S_OK : E_INVALIDARG),
                "RSSetShadingRate 4X4 with additional rates %d: %#lx\n", options6.AdditionalShadingRatesSupported, hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_COMPUTE, CASE_RATE_1X1);
        ok(hr == E_FAIL, "RSSetShadingRate on a compute list: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_COPY, CASE_RATE_1X1);
        ok(hr == E_FAIL, "RSSetShadingRate on a copy list: %#lx\n", hr);
        if (options6.VariableShadingRateTier < D3D12_VARIABLE_SHADING_RATE_TIER_2)
        {
            hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_RATE_IMAGE);
            ok(hr == E_FAIL, "RSSetShadingRateImage below tier 2: %#lx\n", hr);
        }
    }
    else trace("Variable rate shading is not supported, skipping its cases\n");

    if (options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1)
    {
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_DISPATCH_MESH);
        ok(hr == S_OK, "DispatchMesh: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_BUNDLE, CASE_DISPATCH_MESH);
        ok(hr == S_OK, "DispatchMesh in a bundle: %#lx\n", hr);
    }
    else trace("Mesh shaders are not supported, skipping their cases\n");

    if (options12.EnhancedBarriersSupported)
    {
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_BARRIER_EMPTY);
        ok(hr == S_OK, "Barrier without groups: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_DIRECT, CASE_BARRIER_GLOBAL);
        ok(hr == S_OK, "global Barrier: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_COMPUTE, CASE_BARRIER_GLOBAL);
        ok(hr == S_OK, "global Barrier on a compute list: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_COPY, CASE_BARRIER_GLOBAL);
        ok(hr == S_OK, "global Barrier on a copy list: %#lx\n", hr);
        hr = CloseAfter(device, D3D12_COMMAND_LIST_TYPE_BUNDLE, CASE_BARRIER_EMPTY);
        ok(hr == E_FAIL, "Barrier in a bundle: %#lx\n", hr);
    }
    else trace("Enhanced barriers are not supported, skipping their cases\n");
}

static void TestLibraryRootSignature(ID3D12Device *device)
{
    static const BYTE garbage[16] = {};
    ID3D12Device14 *device14 = NULL;
    IUnknown *signature;
    HRESULT hr;

    if (FAILED(device->QueryInterface(__uuidof(ID3D12Device14), reinterpret_cast<void **>(&device14)))) return;
    signature = reinterpret_cast<IUnknown *>(0x1);
    hr = device14->CreateRootSignatureFromSubobjectInLibrary(0, garbage, sizeof(garbage), NULL,
            __uuidof(ID3D12RootSignature), reinterpret_cast<void **>(&signature));
    ok(hr == E_INVALIDARG && !signature, "CreateRootSignatureFromSubobjectInLibrary on garbage: %#lx, %p\n", hr,
            signature);
    device14->Release();
}

static void RunVersions(void *)
{
    ID3D12Device *device;

    if (!(device = d3d12_test_create_device())) return;
    TestInterfaces(device);
    TestFences(device);
    TestBackgroundProcessing(device);
    TestProtectedSessions(device);
    TestResourceDesc1(device);
    TestCommandQueue1(device);
    TestShaderCache(device);
    TestInitialLayouts(device);
    TestListMethods(device);
    TestLibraryRootSignature(device);
    ok(device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n", device->GetDeviceRemovedReason());
    device->Release();
}

START_TEST(versions)
{
    d3d12_test_bounded("versions", RunVersions, NULL, D3D12_TEST_BUDGET);
}
