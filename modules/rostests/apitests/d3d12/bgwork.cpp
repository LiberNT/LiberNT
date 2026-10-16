/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Background processing mode and shader cache capabilities
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

static void RunBackgroundWork(void *)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options = {};
    D3D12_FEATURE_DATA_SHADER_CACHE cache = {};
    ID3D12Device6 *device6 = NULL;
    ID3D12Device *device;
    BOOL further = 2;
    HANDLE event;
    HRESULT hr;

    if (!(device = d3d12_test_create_device())) return;
    hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options, sizeof(options));
    ok(hr == S_OK, "OPTIONS6: %#lx\n", hr);
    trace("background processing supported %d\n", options.BackgroundProcessingSupported);

    hr = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_CACHE, &cache, sizeof(cache));
    ok(hr == S_OK, "shader cache caps: %#lx\n", hr);
    trace("shader cache support flags %#x\n", cache.SupportFlags);
    ok(cache.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_SINGLE_PSO, "SINGLE_PSO missing: %#x\n", cache.SupportFlags);
    ok(!(cache.SupportFlags & ~0x7fu), "unknown shader cache flags %#x\n", cache.SupportFlags);

    hr = device->QueryInterface(__uuidof(ID3D12Device6), reinterpret_cast<void **>(&device6));
    ok(hr == S_OK && device6, "ID3D12Device6: %#lx\n", hr);
    if (!device6)
    {
        device->Release();
        return;
    }
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
            D3D12_MEASUREMENTS_ACTION_KEEP_ALL, NULL, NULL);
    ok(hr == S_OK, "ALLOWED, KEEP_ALL: %#lx\n", hr);
    hr = device6->SetBackgroundProcessingMode(static_cast<D3D12_BACKGROUND_PROCESSING_MODE>(4),
            D3D12_MEASUREMENTS_ACTION_KEEP_ALL, NULL, NULL);
    ok(hr == E_INVALIDARG, "unknown mode: %#lx\n", hr);

    event = CreateEventW(NULL, TRUE, FALSE, NULL);
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
            D3D12_MEASUREMENTS_ACTION_KEEP_ALL, event, NULL);
    ok(hr == E_INVALIDARG, "event with KEEP_ALL: %#lx\n", hr);
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOW_INTRUSIVE_MEASUREMENTS,
            D3D12_MEASUREMENTS_ACTION_COMMIT_RESULTS, event, &further);
    ok(hr == S_OK, "ALLOW_INTRUSIVE_MEASUREMENTS, COMMIT_RESULTS: %#lx\n", hr);
    ok(WaitForSingleObject(event, D3D12_TEST_WAIT) == WAIT_OBJECT_0, "the completion event was not signaled\n");
    if (options.BackgroundProcessingSupported) trace("further measurements desired %d\n", further);
    else ok(further == FALSE, "further measurements desired %d\n", further);
    hr = device6->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
            D3D12_MEASUREMENTS_ACTION_DISCARD_PREVIOUS, NULL, NULL);
    ok(hr == S_OK, "ALLOWED, DISCARD_PREVIOUS: %#lx\n", hr);
    CloseHandle(event);
    ok(device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n", device->GetDeviceRemovedReason());
    device6->Release();
    device->Release();
}

START_TEST(bgwork)
{
    d3d12_test_bounded("bgwork", RunBackgroundWork, NULL, D3D12_TEST_BUDGET);
}
