/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Unordered access views of multisampled textures
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static ID3D12Resource *CreateMultisampled(ID3D12Device *device, UINT layers)
{
    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *resource = NULL;
    HRESULT hr;

    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = D3D12_TEST_SIZE;
    desc.Height = D3D12_TEST_SIZE;
    desc.DepthOrArraySize = layers;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 4;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            NULL, __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    ok(hr == S_OK, "multisampled UAV texture with %u layers: %#lx\n", layers, hr);
    return resource;
}

static ID3D12DescriptorHeap *CreateViewHeap(ID3D12Device *device, BOOL shader_visible)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    ID3D12DescriptorHeap *heap = NULL;

    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 3;
    desc.Flags = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void **>(&heap));
    return heap;
}

static void RunUav(void *)
{
    static const FLOAT colours[3][4] = { { 0.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } };
    static const DWORD expected[3] = { 0xffff0000u, 0xff0000ffu, 0xff00ff00u };
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 options14 = {};
    D3D12_UNORDERED_ACCESS_VIEW_DESC views[3] = {};
    ID3D12DescriptorHeap *cpu_heap = NULL, *gpu_heap = NULL;
    ID3D12Resource *single = NULL, *array = NULL, *targets[3] = {};
    struct d3d12_test_context context;
    UINT increment;
    DWORD texel;

    if (!d3d12_test_init(&context)) return;
    context.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS14, &options14, sizeof(options14));
    if (!options14.WriteableMSAATexturesSupported)
    {
        skip("writeable multisampled textures are not supported\n");
        d3d12_test_destroy(&context);
        return;
    }
    single = CreateMultisampled(context.device, 1);
    array = CreateMultisampled(context.device, 2);
    cpu_heap = CreateViewHeap(context.device, FALSE);
    gpu_heap = CreateViewHeap(context.device, TRUE);
    ok(cpu_heap && gpu_heap, "descriptor heap creation failed\n");
    for (UINT i = 0; i < 3; ++i)
    {
        targets[i] = d3d12_test_create_target(context.device, DXGI_FORMAT_R8G8B8A8_UNORM);
        if (!targets[i]) break;
    }
    if (single && array && cpu_heap && gpu_heap && targets[0] && targets[1] && targets[2])
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE visible = gpu_heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = gpu_heap->GetGPUDescriptorHandleForHeapStart();
        ID3D12Resource *sources[3] = { single, array, array };
        UINT source_slices[3] = { 0, 0, 1 };

        increment = context.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (UINT i = 0; i < 3; ++i) views[i].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        views[0].ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DMS;
        views[1].ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY;
        views[1].Texture2DMSArray.FirstArraySlice = 0;
        views[1].Texture2DMSArray.ArraySize = 2;
        views[2].ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY;
        views[2].Texture2DMSArray.FirstArraySlice = 1;
        views[2].Texture2DMSArray.ArraySize = 1;
        for (UINT i = 0; i < 3; ++i)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE c = { cpu.ptr + i * increment }, v = { visible.ptr + i * increment };

            context.device->CreateUnorderedAccessView(sources[i], NULL, &views[i], c);
            context.device->CreateUnorderedAccessView(sources[i], NULL, &views[i], v);
        }
        context.list->SetDescriptorHeaps(1, &gpu_heap);
        for (UINT i = 0; i < 3; ++i)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE c = { cpu.ptr + i * increment };
            D3D12_GPU_DESCRIPTOR_HANDLE g = { gpu.ptr + i * increment };
            D3D12_RESOURCE_BARRIER barrier = {};

            context.list->ClearUnorderedAccessViewFloat(g, c, sources[i], colours[i], 0, NULL);
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = sources[i];
            context.list->ResourceBarrier(1, &barrier);
        }
        d3d12_test_transition(context.list, single, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        d3d12_test_transition(context.list, array, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        for (UINT i = 0; i < 3; ++i)
        {
            d3d12_test_transition(context.list, targets[i], D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RESOLVE_DEST);
            context.list->ResolveSubresource(targets[i], 0, sources[i], source_slices[i], DXGI_FORMAT_R8G8B8A8_UNORM);
            d3d12_test_transition(context.list, targets[i], D3D12_RESOURCE_STATE_RESOLVE_DEST,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        ok(d3d12_test_execute(&context, context.list, D3D12_TEST_WAIT), "clear and resolve did not complete\n");
        for (UINT i = 0; i < 3; ++i)
        {
            texel = 0;
            ok(d3d12_test_read_texel(&context, targets[i], D3D12_RESOURCE_STATE_RENDER_TARGET, 4, 4, &texel),
                    "readback %u failed\n", i);
            ok(texel == expected[i], "view %u: got %08lx, expected %08lx\n", i, texel, expected[i]);
        }
    }
    ok(context.device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n",
            context.device->GetDeviceRemovedReason());
    for (UINT i = 0; i < 3; ++i) if (targets[i]) targets[i]->Release();
    if (gpu_heap) gpu_heap->Release();
    if (cpu_heap) cpu_heap->Release();
    if (array) array->Release();
    if (single) single->Release();
    d3d12_test_destroy(&context);
}

START_TEST(uav)
{
    d3d12_test_bounded("uav", RunUav, NULL, D3D12_TEST_BUDGET);
}
