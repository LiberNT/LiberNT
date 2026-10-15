/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for DXCoreCreateAdapterFactory and the DXCore adapter objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <stdint.h>
#include <initguid.h>
#include <dxgi.h>
#include <dxcore.h>

static const struct
{
    DXCoreAdapterProperty property;
    size_t size;
} fixed_sizes[] =
{
    { DXCoreAdapterProperty::InstanceLuid, 8 },
    { DXCoreAdapterProperty::DriverVersion, 8 },
    { DXCoreAdapterProperty::HardwareID, 16 },
    { DXCoreAdapterProperty::KmdModelVersion, 4 },
    { DXCoreAdapterProperty::ComputePreemptionGranularity, 4 },
    { DXCoreAdapterProperty::GraphicsPreemptionGranularity, 4 },
    { DXCoreAdapterProperty::DedicatedAdapterMemory, 8 },
    { DXCoreAdapterProperty::DedicatedSystemMemory, 8 },
    { DXCoreAdapterProperty::SharedSystemMemory, 8 },
    { DXCoreAdapterProperty::AcgCompatible, 1 },
    { DXCoreAdapterProperty::IsHardware, 1 },
    { DXCoreAdapterProperty::IsIntegrated, 1 },
    { DXCoreAdapterProperty::IsDetachable, 1 },
    { DXCoreAdapterProperty::HardwareIDParts, 20 },
    { DXCoreAdapterProperty::PhysicalAdapterCount, 4 },
    { DXCoreAdapterProperty::AdapterEngineCount, 4 },
    { DXCoreAdapterProperty::AdapterEngineName, 4 },
};

static void test_adapter(IDXCoreAdapterFactory *factory, IDXCoreAdapter *adapter)
{
    DXCoreAdapterMemoryBudgetNodeSegmentGroup budget_in = { 0, DXCoreSegmentGroup::Local };
    DXCoreAdapterMemoryBudget budget;
    IDXCoreAdapter *again = NULL;
    IUnknown *unk = NULL;
    BYTE buffer[1024];
    size_t size;
    LUID luid;
    HRESULT hr;
    UINT i;

    ok(adapter->IsValid(), "adapter not valid\n");
    ok(adapter->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS), "D3D11 graphics not supported\n");
    ok(adapter->IsAttributeSupported(DXCORE_HARDWARE_TYPE_ATTRIBUTE_GPU), "not a GPU\n");
    ok(!adapter->IsAttributeSupported(DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU), "NPU attribute supported\n");

    for (i = 0; i < ARRAYSIZE(fixed_sizes); i++)
    {
        size = 0xdead;
        ok(adapter->IsPropertySupported(fixed_sizes[i].property), "%u: property not supported\n", i);
        hr = adapter->GetPropertySize(fixed_sizes[i].property, &size);
        ok(hr == S_OK, "%u: GetPropertySize returned %#lx\n", i, hr);
        ok(size == fixed_sizes[i].size, "%u: size %Iu, expected %Iu\n", i, size, fixed_sizes[i].size);
    }

    size = 0;
    hr = adapter->GetPropertySize(DXCoreAdapterProperty::DriverDescription, &size);
    ok(hr == S_OK, "GetPropertySize(DriverDescription) returned %#lx\n", hr);
    ok(size > 1 && size <= sizeof(buffer), "description size %Iu\n", size);
    if (size > 1 && size <= sizeof(buffer))
    {
        memset(buffer, 0xcc, sizeof(buffer));
        hr = adapter->GetProperty(DXCoreAdapterProperty::DriverDescription, size, buffer);
        ok(hr == S_OK, "GetProperty(DriverDescription) returned %#lx\n", hr);
        ok(buffer[size - 1] == 0 && strlen((char *)buffer) == size - 1, "description not terminated\n");
        hr = adapter->GetProperty(DXCoreAdapterProperty::DriverDescription, size - 1, buffer);
        ok(hr == E_INVALIDARG, "short description returned %#lx\n", hr);
    }

    hr = adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, sizeof(luid), &luid);
    ok(hr == S_OK, "GetProperty(InstanceLuid) returned %#lx\n", hr);
    hr = adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, sizeof(luid) - 1, buffer);
    ok(hr == E_INVALIDARG, "short LUID returned %#lx\n", hr);
    hr = adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, sizeof(luid) + 8, buffer);
    ok(hr == S_OK, "long LUID returned %#lx\n", hr);
    hr = adapter->GetProperty(DXCoreAdapterProperty::PhysicalAdapterCount, 4, buffer);
    ok(hr == S_OK && *(uint32_t *)buffer >= 1, "physical adapter count %#lx %u\n", hr, *(uint32_t *)buffer);

    hr = adapter->GetProperty(DXCoreAdapterProperty::AdapterEngineCount, 4, buffer);
    ok(hr == E_INVALIDARG, "engine count without input returned %#lx\n", hr);
    hr = adapter->GetProperty(DXCoreAdapterProperty::AdapterEngineName, 4, buffer);
    ok(hr == E_INVALIDARG, "engine name without input returned %#lx\n", hr);

    ok(!adapter->IsPropertySupported((DXCoreAdapterProperty)18), "property 18 supported\n");
    hr = adapter->GetPropertySize((DXCoreAdapterProperty)18, &size);
    ok(hr == DXGI_ERROR_INVALID_CALL, "GetPropertySize(18) returned %#lx\n", hr);
    hr = adapter->GetProperty((DXCoreAdapterProperty)18, sizeof(buffer), buffer);
    ok(hr == DXGI_ERROR_INVALID_CALL, "GetProperty(18) returned %#lx\n", hr);

    ok(adapter->IsQueryStateSupported(DXCoreAdapterState::IsDriverUpdateInProgress), "driver update state\n");
    ok(adapter->IsQueryStateSupported(DXCoreAdapterState::AdapterMemoryBudget), "memory budget state\n");
    ok(!adapter->IsQueryStateSupported((DXCoreAdapterState)11), "state 11 supported\n");
    hr = adapter->QueryState(DXCoreAdapterState::AdapterMemoryBudget, sizeof(budget_in), &budget_in,
                             sizeof(budget), &budget);
    ok(hr == S_OK, "QueryState(AdapterMemoryBudget) returned %#lx\n", hr);

    hr = adapter->GetFactory(IID_IDXCoreAdapterFactory, (void **)&unk);
    ok(hr == S_OK && unk == factory, "GetFactory returned %#lx %p\n", hr, unk);
    if (unk) unk->Release();
    hr = adapter->QueryInterface(IID_IDXCoreAdapter1, (void **)&unk);
    ok(hr == S_OK, "QueryInterface(IDXCoreAdapter1) returned %#lx\n", hr);
    if (unk) unk->Release();

    hr = factory->GetAdapterByLuid(luid, IID_IDXCoreAdapter, (void **)&again);
    ok(hr == S_OK && again == adapter, "GetAdapterByLuid returned %#lx %p, expected %p\n", hr, again, adapter);
    if (again) again->Release();
}

START_TEST(DXCoreCreateAdapterFactory)
{
    IDXCoreAdapterFactory *factory = NULL, *factory2 = NULL;
    IDXCoreAdapterList *list = NULL;
    IDXCoreAdapter *adapter = NULL;
    IUnknown *unk = NULL;
    LUID bogus = { 0x12345, 0x6789 };
    HRESULT hr;
    UINT i;

    hr = DXCoreCreateAdapterFactory(IID_IDXCoreAdapterFactory, NULL);
    ok(hr == E_POINTER, "NULL output returned %#lx\n", hr);
    hr = DXCoreCreateAdapterFactory(IID_IDXCoreAdapterList, (void **)&list);
    ok(hr == E_NOINTERFACE && !list, "wrong interface returned %#lx %p\n", hr, list);

    hr = DXCoreCreateAdapterFactory(IID_IDXCoreAdapterFactory, (void **)&factory);
    ok(hr == S_OK, "DXCoreCreateAdapterFactory returned %#lx\n", hr);
    if (!factory) return;
    hr = DXCoreCreateAdapterFactory(IID_IDXCoreAdapterFactory, (void **)&factory2);
    ok(hr == S_OK && factory2 == factory, "second factory %p, expected %p\n", factory2, factory);
    if (factory2) factory2->Release();
    hr = factory->QueryInterface(IID_IDXCoreAdapterFactory1, (void **)&unk);
    ok(hr == S_OK, "QueryInterface(IDXCoreAdapterFactory1) returned %#lx\n", hr);
    if (unk) unk->Release();

    for (i = 0; i <= (UINT)DXCoreNotificationType::AdapterHardwareContentProtectionTeardown; i++)
        ok(factory->IsNotificationTypeSupported((DXCoreNotificationType)i), "notification %u not supported\n", i);

    hr = factory->CreateAdapterList(0, NULL, IID_IDXCoreAdapterList, (void **)&list);
    ok(hr == E_INVALIDARG, "empty filter returned %#lx\n", hr);

    hr = factory->CreateAdapterList(1, &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, IID_IDXCoreAdapterList,
                                    (void **)&list);
    ok(hr == S_OK, "CreateAdapterList returned %#lx\n", hr);
    if (list)
    {
        ok(list->GetAdapterCount() >= 1, "no D3D11 adapter\n");
        ok(!list->IsStale(), "fresh list is stale\n");
        for (i = 0; i <= (UINT)DXCoreAdapterPreference::HighPerformance; i++)
            ok(list->IsAdapterPreferenceSupported((DXCoreAdapterPreference)i), "preference %u not supported\n", i);
        for (i = 0; i < list->GetAdapterCount(); i++)
        {
            hr = list->GetAdapter(i, IID_IDXCoreAdapter, (void **)&adapter);
            ok(hr == S_OK, "GetAdapter(%u) returned %#lx\n", i, hr);
            if (adapter)
            {
                test_adapter(factory, adapter);
                adapter->Release();
                adapter = NULL;
            }
        }
        hr = list->GetAdapter(list->GetAdapterCount(), IID_IDXCoreAdapter, (void **)&adapter);
        ok(hr == E_INVALIDARG && !adapter, "GetAdapter past the end returned %#lx %p\n", hr, adapter);
        list->Release();
    }

    hr = factory->GetAdapterByLuid(bogus, IID_IDXCoreAdapter, (void **)&adapter);
    ok(hr == E_INVALIDARG && !adapter, "bogus LUID returned %#lx %p\n", hr, adapter);
    factory->Release();
}
