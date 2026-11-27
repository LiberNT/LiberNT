/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DXCore adapter state and property calls check their buffers before the state or property
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <stdint.h>
#include <initguid.h>
#include <dxgi.h>
#include <dxcore.h>

static void TestStates(IDXCoreAdapter *adapter)
{
    DXCoreAdapterMemoryBudgetNodeSegmentGroup budget_in = { 0, DXCoreSegmentGroup::Local };
    DXCoreMemoryQueryInput usage_in = { 0, DXCoreMemoryType::Dedicated };
    DXCoreAdapterMemoryBudget budget;
    DXCoreMemoryUsage usage;
    BYTE scratch[64];
    HRESULT hr;
    UINT state;

    for (state = 0; state <= 13; state++)
    {
        hr = adapter->QueryState((DXCoreAdapterState)state, 0, NULL, 8, NULL);
        ok(hr == E_POINTER, "state %u without an output: %#lx\n", state, hr);
    }

    hr = adapter->QueryState(DXCoreAdapterState::AdapterMemoryBudget, sizeof(budget_in), NULL, sizeof(scratch), scratch);
    ok(hr == E_POINTER, "budget without an input: %#lx\n", hr);
    hr = adapter->QueryState(DXCoreAdapterState::AdapterMemoryBudget, sizeof(budget_in), &budget_in, sizeof(budget),
                             &budget);
    ok(hr == S_OK, "local budget: %#lx\n", hr);
    budget_in.segmentGroup = (DXCoreSegmentGroup)2;
    hr = adapter->QueryState(DXCoreAdapterState::AdapterMemoryBudget, sizeof(budget_in), &budget_in, sizeof(budget),
                             &budget);
    ok(hr == E_INVALIDARG, "segment group 2: %#lx\n", hr);

    if (!adapter->IsQueryStateSupported(DXCoreAdapterState::AdapterMemoryUsageBytes))
    {
        hr = adapter->QueryState(DXCoreAdapterState::AdapterMemoryUsageBytes, sizeof(usage_in), &usage_in,
                                 sizeof(usage), &usage);
        ok(hr == DXGI_ERROR_INVALID_CALL, "unsupported state: %#lx\n", hr);
    }
}

static void TestProperties(IDXCoreAdapter *adapter)
{
    const DXCoreAdapterProperty unsupported = (DXCoreAdapterProperty)18;
    BYTE buffer[64];
    HRESULT hr;
    UINT zero, i;

    if (adapter->IsPropertySupported(unsupported))
    {
        skip("property 18 is supported\n");
        return;
    }
    hr = adapter->GetProperty(unsupported, 16, NULL);
    ok(hr == E_POINTER, "unsupported property without a buffer: %#lx\n", hr);
    memset(buffer, 0xcc, sizeof(buffer));
    hr = adapter->GetProperty(unsupported, sizeof(buffer), buffer);
    ok(hr == DXGI_ERROR_INVALID_CALL, "unsupported property: %#lx\n", hr);
    for (zero = 0, i = 0; i < sizeof(buffer); i++)
        zero += buffer[i] == 0;
    ok(zero == sizeof(buffer), "unsupported property: %u of %u bytes zeroed\n", zero, (UINT)sizeof(buffer));
}

START_TEST(Arguments)
{
    IDXCoreAdapterFactory *factory = NULL;
    IDXCoreAdapterList *list = NULL;
    IDXCoreAdapter *adapter = NULL;
    HRESULT hr;

    hr = DXCoreCreateAdapterFactory(IID_IDXCoreAdapterFactory, (void **)&factory);
    ok(hr == S_OK, "DXCoreCreateAdapterFactory returned %#lx\n", hr);
    if (!factory) return;
    hr = factory->CreateAdapterList(1, &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, IID_IDXCoreAdapterList,
                                    (void **)&list);
    ok(hr == S_OK, "CreateAdapterList returned %#lx\n", hr);
    if (list && list->GetAdapterCount())
        list->GetAdapter(0, IID_IDXCoreAdapter, (void **)&adapter);
    ok(adapter != NULL, "no D3D11 adapter\n");
    if (adapter)
    {
        TestStates(adapter);
        TestProperties(adapter);
        adapter->Release();
    }
    if (list) list->Release();
    factory->Release();
}
