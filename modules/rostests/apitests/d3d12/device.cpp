/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3D12CreateDevice returns the existing device of an adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static IDXGIAdapter1 *AdapterOf(ID3D12Device *device)
{
    IDXGIFactory4 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;

    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void **>(&factory)))) return NULL;
    if (FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(), __uuidof(IDXGIAdapter1),
            reinterpret_cast<void **>(&adapter))))
        adapter = NULL;
    factory->Release();
    return adapter;
}

static void RunDevice(void *)
{
    ID3D12Device *first, *second = NULL, *third = NULL, *higher = NULL;
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {};
    static const D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_12_1 };
    IDXGIAdapter1 *adapter;
    HRESULT hr;

    if (!(first = d3d12_test_create_device())) return;
    adapter = AdapterOf(first);
    ok(adapter != NULL, "no DXGI adapter for the device LUID\n");
    if (!adapter)
    {
        first->Release();
        return;
    }

    hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void **>(&second));
    ok(hr == S_OK && second == first, "second device: %#lx, %p, first %p\n", hr, second, first);
    hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), NULL);
    ok(hr == S_FALSE, "capability check: %#lx\n", hr);

    levels.NumFeatureLevels = ARRAYSIZE(requested);
    levels.pFeatureLevelsRequested = requested;
    first->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels));
    hr = D3D12CreateDevice(adapter, levels.MaxSupportedFeatureLevel, __uuidof(ID3D12Device),
            reinterpret_cast<void **>(&higher));
    ok(hr == S_OK && higher == first, "device at feature level %#x: %#lx, %p, first %p\n",
            levels.MaxSupportedFeatureLevel, hr, higher, first);
    if (higher) higher->Release();
    if (second) second->Release();
    first->Release();

    hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void **>(&third));
    ok(hr == S_OK && third, "device after release: %#lx\n", hr);
    if (third)
    {
        third->AddRef();
        ok(third->Release() == 1, "the new device has other references\n");
        third->Release();
    }
    adapter->Release();
}

START_TEST(device)
{
    d3d12_test_bounded("device", RunDevice, NULL, D3D12_TEST_BUDGET);
}
