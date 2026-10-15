/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IDXGIAdapter3::QueryVideoMemoryInfo budgets of the adapter a device runs on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

#define BUDGET_BUFFER_SIZE (16u * 1024u * 1024u)

static ID3D12Resource *CreateBuffer(ID3D12Device *device, UINT64 size)
{
    D3D12_HEAP_PROPERTIES heap = {};
    D3D12_RESOURCE_DESC desc = {};
    ID3D12Resource *resource = NULL;

    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, NULL,
            __uuidof(ID3D12Resource), reinterpret_cast<void **>(&resource));
    return resource;
}

static void RunBudget(void *)
{
    D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
    DXGI_QUERY_VIDEO_MEMORY_INFO local = {}, nonlocal = {}, after = {};
    DXGI_ADAPTER_DESC1 desc = {};
    IDXGIFactory4 *factory = NULL;
    IDXGIAdapter3 *adapter = NULL;
    ID3D12Resource *buffer;
    ID3D12Device *device;
    HRESULT hr;

    if (!(device = d3d12_test_create_device())) return;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void **>(&factory));
    ok(hr == S_OK, "CreateDXGIFactory1: %#lx\n", hr);
    if (factory)
    {
        hr = factory->EnumAdapterByLuid(device->GetAdapterLuid(), __uuidof(IDXGIAdapter3),
                reinterpret_cast<void **>(&adapter));
        ok(hr == S_OK, "EnumAdapterByLuid: %#lx\n", hr);
    }
    if (!adapter)
    {
        if (factory) factory->Release();
        device->Release();
        return;
    }
    adapter->GetDesc1(&desc);
    device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));

    hr = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
    ok(hr == S_OK, "local: %#lx\n", hr);
    ok(local.Budget >= local.AvailableForReservation, "local budget %I64u, available for reservation %I64u\n",
            local.Budget, local.AvailableForReservation);
    ok(local.Budget != 0, "local budget is 0\n");
    hr = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal);
    ok(hr == S_OK, "non-local: %#lx\n", hr);
    ok(nonlocal.Budget >= nonlocal.AvailableForReservation,
            "non-local budget %I64u, available for reservation %I64u\n", nonlocal.Budget,
            nonlocal.AvailableForReservation);
    if (!architecture.UMA)
        ok(nonlocal.Budget != 0, "a discrete adapter has no non-local budget\n");
    hr = adapter->QueryVideoMemoryInfo(0, static_cast<DXGI_MEMORY_SEGMENT_GROUP>(DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL + 1),
            &after);
    ok(hr == E_INVALIDARG, "invalid group: %#lx\n", hr);
    trace("adapter '%ls' uma %d: local budget %I64u usage %I64u, non-local budget %I64u usage %I64u\n",
            desc.Description, architecture.UMA, local.Budget, local.CurrentUsage, nonlocal.Budget,
            nonlocal.CurrentUsage);

    if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
    {
        buffer = CreateBuffer(device, BUDGET_BUFFER_SIZE);
        ok(buffer != NULL, "buffer creation failed\n");
        hr = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &after);
        ok(hr == S_OK && after.CurrentUsage >= local.CurrentUsage + BUDGET_BUFFER_SIZE,
                "local usage %I64u after a %u byte buffer, %I64u before, hr %#lx\n", after.CurrentUsage,
                BUDGET_BUFFER_SIZE, local.CurrentUsage, hr);
        if (buffer) buffer->Release();
    }

    adapter->Release();
    factory->Release();
    device->Release();
}

START_TEST(budget)
{
    d3d12_test_bounded("budget", RunBudget, NULL, D3D12_TEST_BUDGET);
}
