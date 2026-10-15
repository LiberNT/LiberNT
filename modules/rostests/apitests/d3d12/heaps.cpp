/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Heap types, custom heap properties and GPU upload heaps
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

#define HEAPS_BUFFER_SIZE 65536u

static D3D12_RESOURCE_DESC BufferDesc(D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC desc = {};

    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = HEAPS_BUFFER_SIZE;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

static D3D12_RESOURCE_DESC TextureDesc(D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC desc = {};

    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = flags;
    return desc;
}

static HRESULT CreateCommitted(ID3D12Device *device, D3D12_HEAP_PROPERTIES heap, D3D12_HEAP_FLAGS flags,
        const D3D12_RESOURCE_DESC &desc, D3D12_RESOURCE_STATES state, ID3D12Resource **resource)
{
    ID3D12Resource *object = NULL;
    HRESULT hr;

    hr = device->CreateCommittedResource(&heap, flags, &desc, state, NULL, __uuidof(ID3D12Resource),
            reinterpret_cast<void **>(&object));
    if (resource) *resource = object;
    else if (object) object->Release();
    return hr;
}

static HRESULT CreateHeap(ID3D12Device *device, D3D12_HEAP_PROPERTIES properties, D3D12_HEAP_FLAGS flags,
        ID3D12Heap **heap)
{
    D3D12_HEAP_DESC desc = {};
    ID3D12Heap *object = NULL;
    HRESULT hr;

    desc.SizeInBytes = HEAPS_BUFFER_SIZE;
    desc.Properties = properties;
    desc.Flags = flags;
    hr = device->CreateHeap(&desc, __uuidof(ID3D12Heap), reinterpret_cast<void **>(&object));
    if (heap) *heap = object;
    else if (object) object->Release();
    return hr;
}

static void TestCustomHeapProperties(ID3D12Device *device, const D3D12_FEATURE_DATA_ARCHITECTURE &architecture)
{
    D3D12_CPU_PAGE_PROPERTY upload_page = architecture.CacheCoherentUMA
            ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
    D3D12_MEMORY_POOL local_pool = architecture.UMA ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1;
    const struct
    {
        D3D12_HEAP_TYPE input;
        D3D12_HEAP_TYPE type;
        D3D12_CPU_PAGE_PROPERTY page;
        D3D12_MEMORY_POOL pool;
    }
    tests[] =
    {
        { D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE, local_pool },
        { D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_TYPE_CUSTOM, upload_page, D3D12_MEMORY_POOL_L0 },
        { D3D12_HEAP_TYPE_READBACK, D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0 },
        { D3D12_HEAP_TYPE_CUSTOM, static_cast<D3D12_HEAP_TYPE>(0), D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                D3D12_MEMORY_POOL_UNKNOWN },
        { D3D12_HEAP_TYPE_GPU_UPLOAD, D3D12_HEAP_TYPE_CUSTOM, upload_page, local_pool },
        { static_cast<D3D12_HEAP_TYPE>(6), static_cast<D3D12_HEAP_TYPE>(0), D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                D3D12_MEMORY_POOL_UNKNOWN },
    };

    for (UINT i = 0; i < ARRAYSIZE(tests); ++i)
    {
        D3D12_HEAP_PROPERTIES properties = device->GetCustomHeapProperties(0, tests[i].input);

        ok(properties.Type == tests[i].type && properties.CPUPageProperty == tests[i].page
                && properties.MemoryPoolPreference == tests[i].pool && properties.CreationNodeMask == 1
                && properties.VisibleNodeMask == 1, "heap type %u: type %u, page %u, pool %u, nodes %u %u\n",
                tests[i].input, properties.Type, properties.CPUPageProperty, properties.MemoryPoolPreference,
                properties.CreationNodeMask, properties.VisibleNodeMask);
    }
}

static void TestCommittedFlags(ID3D12Device *device)
{
    const D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
    const struct
    {
        D3D12_RESOURCE_DESC desc;
        D3D12_RESOURCE_STATES state;
        D3D12_HEAP_FLAGS expected;
    }
    tests[] =
    {
        { BufferDesc(D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS },
        { TextureDesc(D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_COMMON,
                D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES },
        { TextureDesc(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET), D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES },
    };

    for (UINT i = 0; i < ARRAYSIZE(tests); ++i)
    {
        D3D12_HEAP_FLAGS flags = static_cast<D3D12_HEAP_FLAGS>(0xdead);
        D3D12_HEAP_PROPERTIES properties = {};
        ID3D12Resource *resource;
        HRESULT hr;

        hr = CreateCommitted(device, heap, D3D12_HEAP_FLAG_NONE, tests[i].desc, tests[i].state, &resource);
        ok(hr == S_OK, "resource %u: %#lx\n", i, hr);
        if (!resource) continue;
        hr = resource->GetHeapProperties(&properties, &flags);
        ok(hr == S_OK && properties.Type == D3D12_HEAP_TYPE_DEFAULT && flags == tests[i].expected,
                "resource %u: %#lx, type %u, flags %#x\n", i, hr, properties.Type, flags);
        resource->Release();
    }
}

static BOOL CopyAndCompare(struct d3d12_test_context *context, ID3D12Resource *source)
{
    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_READBACK };
    ID3D12Resource *readback = NULL;
    D3D12_RANGE range = { 0, HEAPS_BUFFER_SIZE };
    BYTE *data = NULL;
    BOOL match = FALSE;

    if (FAILED(CreateCommitted(context->device, heap, D3D12_HEAP_FLAG_NONE, BufferDesc(D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COPY_DEST, &readback)))
        return FALSE;
    context->list->CopyBufferRegion(readback, 0, source, 0, HEAPS_BUFFER_SIZE);
    if (d3d12_test_execute(context, context->list, D3D12_TEST_WAIT)
            && SUCCEEDED(readback->Map(0, &range, reinterpret_cast<void **>(&data))))
    {
        match = TRUE;
        for (UINT i = 0; i < HEAPS_BUFFER_SIZE && match; ++i)
            if (data[i] != static_cast<BYTE>(i * 7 + 3)) match = FALSE;
        readback->Unmap(0, NULL);
    }
    readback->Release();
    return match;
}

static void TestGpuUploadSupported(struct d3d12_test_context *context)
{
    ID3D12Device *device = context->device;
    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_GPU_UPLOAD }, properties, bad;
    D3D12_HEAP_FLAGS flags;
    ID3D12Resource *resource;
    ID3D12Heap *object;
    BYTE *data = NULL;
    HRESULT hr;

    hr = CreateCommitted(device, heap, D3D12_HEAP_FLAG_NONE, BufferDesc(D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COMMON, &resource);
    ok(hr == S_OK, "GPU upload buffer: %#lx\n", hr);
    if (resource)
    {
        flags = static_cast<D3D12_HEAP_FLAGS>(0xdead);
        hr = resource->GetHeapProperties(&properties, &flags);
        ok(hr == S_OK && properties.Type == D3D12_HEAP_TYPE_GPU_UPLOAD
                && properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_UNKNOWN
                && properties.MemoryPoolPreference == D3D12_MEMORY_POOL_UNKNOWN
                && flags == D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, "GPU upload buffer properties: %#lx, %u %u %u %#x\n",
                hr, properties.Type, properties.CPUPageProperty, properties.MemoryPoolPreference, flags);
        hr = resource->Map(0, NULL, reinterpret_cast<void **>(&data));
        ok(hr == S_OK && data, "GPU upload map: %#lx\n", hr);
        if (SUCCEEDED(hr) && data)
        {
            for (UINT i = 0; i < HEAPS_BUFFER_SIZE; ++i) data[i] = static_cast<BYTE>(i * 7 + 3);
            resource->Unmap(0, NULL);
            ok(CopyAndCompare(context, resource), "the GPU did not read what the CPU wrote\n");
        }
        resource->Release();
    }
    hr = CreateCommitted(device, heap, D3D12_HEAP_FLAG_NONE, BufferDesc(D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COPY_DEST, NULL);
    ok(hr == S_OK, "GPU upload buffer in COPY_DEST: %#lx\n", hr);
    hr = CreateCommitted(device, heap, D3D12_HEAP_FLAG_NONE, BufferDesc(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL);
    ok(hr == S_OK, "GPU upload UAV buffer: %#lx\n", hr);
    hr = CreateCommitted(device, heap, D3D12_HEAP_FLAG_NONE, TextureDesc(D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COMMON, NULL);
    ok(hr == S_OK, "GPU upload texture: %#lx\n", hr);
    hr = CreateCommitted(device, heap, D3D12_HEAP_FLAG_NONE, TextureDesc(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL);
    ok(hr == S_OK, "GPU upload render target: %#lx\n", hr);
    hr = CreateCommitted(device, heap, D3D12_HEAP_FLAG_NONE, TextureDesc(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
            D3D12_RESOURCE_STATE_COMMON, NULL);
    ok(hr == S_OK, "GPU upload depth stencil: %#lx\n", hr);
    hr = CreateCommitted(device, heap, D3D12_HEAP_FLAG_SHARED, BufferDesc(D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COMMON, NULL);
    ok(hr == E_INVALIDARG, "shared GPU upload buffer: %#lx\n", hr);
    hr = CreateCommitted(device, heap, D3D12_HEAP_FLAG_ALLOW_DISPLAY,
            TextureDesc(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET), D3D12_RESOURCE_STATE_COMMON, NULL);
    ok(hr == E_INVALIDARG, "displayable GPU upload texture: %#lx\n", hr);
    bad = heap;
    bad.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
    hr = CreateCommitted(device, bad, D3D12_HEAP_FLAG_NONE, BufferDesc(D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COMMON, NULL);
    ok(hr == E_INVALIDARG, "GPU upload heap with a page property: %#lx\n", hr);
    bad = heap;
    bad.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    hr = CreateCommitted(device, bad, D3D12_HEAP_FLAG_NONE, BufferDesc(D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COMMON, NULL);
    ok(hr == E_INVALIDARG, "GPU upload heap with a memory pool: %#lx\n", hr);
    properties = device->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_GPU_UPLOAD);
    hr = CreateCommitted(device, properties, D3D12_HEAP_FLAG_NONE, BufferDesc(D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COMMON, NULL);
    ok(hr == S_OK, "custom heap with the GPU upload properties: %#lx\n", hr);

    hr = CreateHeap(device, heap, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, &object);
    ok(hr == S_OK, "GPU upload heap: %#lx\n", hr);
    if (object)
    {
        D3D12_HEAP_DESC desc = object->GetDesc();

        ok(desc.Properties.Type == D3D12_HEAP_TYPE_GPU_UPLOAD && desc.Flags == D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
                "GPU upload heap type %u, flags %#x\n", desc.Properties.Type, desc.Flags);
        D3D12_RESOURCE_DESC buffer = BufferDesc(D3D12_RESOURCE_FLAG_NONE);
        resource = NULL;
        hr = device->CreatePlacedResource(object, 0, &buffer, D3D12_RESOURCE_STATE_COMMON, NULL,
                __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
        ok(hr == S_OK, "placed GPU upload buffer: %#lx\n", hr);
        if (resource) resource->Release();
        object->Release();
    }
    hr = CreateHeap(device, heap, D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, NULL);
    ok(hr == E_INVALIDARG, "shared GPU upload heap: %#lx\n", hr);
}

static void TestGpuUploadUnsupported(ID3D12Device *device)
{
    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_GPU_UPLOAD };
    HRESULT hr;

    hr = CreateCommitted(device, heap, D3D12_HEAP_FLAG_NONE, BufferDesc(D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COMMON, NULL);
    ok(hr == E_INVALIDARG, "unsupported GPU upload buffer: %#lx\n", hr);
    hr = CreateHeap(device, heap, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS, NULL);
    ok(hr == E_INVALIDARG, "unsupported GPU upload heap: %#lx\n", hr);
}

static void RunHeaps(void *)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS17 options17 = {};
    D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
    const D3D12_HEAP_PROPERTIES upload = { D3D12_HEAP_TYPE_UPLOAD };
    struct d3d12_test_context context;
    D3D12_HEAP_PROPERTIES custom = {};
    HRESULT hr;

    if (!d3d12_test_init(&context)) return;
    ID3D12Device *device = context.device;
    device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));
    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16));
    ok(hr == S_OK, "OPTIONS16: %#lx\n", hr);
    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS17, &options17, sizeof(options17));
    ok(hr == S_OK && !options17.ManualWriteTrackingResourceSupported, "OPTIONS17: %#lx, manual write tracking %d\n",
            hr, options17.ManualWriteTrackingResourceSupported);
    trace("UMA %d, cache coherent %d, GPU upload heaps %d\n", architecture.UMA, architecture.CacheCoherentUMA,
            options16.GPUUploadHeapSupported);
    if (architecture.UMA)
        ok(options16.GPUUploadHeapSupported, "a UMA adapter does not support GPU upload heaps\n");

    TestCustomHeapProperties(device, architecture);
    TestCommittedFlags(device);

    hr = CreateCommitted(device, upload, D3D12_HEAP_FLAG_TOOLS_USE_MANUAL_WRITE_TRACKING,
            BufferDesc(D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_GENERIC_READ, NULL);
    ok(hr == E_INVALIDARG, "manual write tracking resource: %#lx\n", hr);
    hr = CreateHeap(device, upload, D3D12_HEAP_FLAG_TOOLS_USE_MANUAL_WRITE_TRACKING | D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
            NULL);
    ok(hr == E_INVALIDARG, "manual write tracking heap: %#lx\n", hr);
    hr = CreateCommitted(device, { static_cast<D3D12_HEAP_TYPE>(6) }, D3D12_HEAP_FLAG_NONE,
            BufferDesc(D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_COMMON, NULL);
    ok(hr == E_INVALIDARG, "heap type 6: %#lx\n", hr);

    custom.Type = D3D12_HEAP_TYPE_CUSTOM;
    custom.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
    custom.MemoryPoolPreference = D3D12_MEMORY_POOL_L1;
    hr = CreateCommitted(device, custom, D3D12_HEAP_FLAG_NONE, BufferDesc(D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COMMON, NULL);
    ok(hr == (!architecture.UMA && options16.GPUUploadHeapSupported ? S_OK : E_INVALIDARG),
            "write-combined L1 custom heap: %#lx\n", hr);
    custom.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    hr = CreateCommitted(device, custom, D3D12_HEAP_FLAG_NONE, BufferDesc(D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COMMON, NULL);
    ok(hr == E_INVALIDARG, "write-back L1 custom heap: %#lx\n", hr);

    if (options16.GPUUploadHeapSupported) TestGpuUploadSupported(&context);
    else TestGpuUploadUnsupported(device);

    ok(device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n", device->GetDeviceRemovedReason());
    d3d12_test_destroy(&context);
}

START_TEST(heaps)
{
    d3d12_test_bounded("heaps", RunHeaps, NULL, D3D12_TEST_BUDGET);
}
