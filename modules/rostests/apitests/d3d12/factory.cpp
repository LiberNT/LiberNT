/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device factories and independent devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

typedef HRESULT (WINAPI *PFN_D3D12_GET_INTERFACE_FACTORY)(REFCLSID, REFIID, void **);

static const GUID clsid_device_factory = { 0x114863bf, 0xc386, 0x4aee, { 0xb3, 0x9d, 0x8f, 0x0b, 0xbb, 0x06, 0x29, 0x55 } };
static const GUID clsid_debug = { 0xf2352aeb, 0xdd84, 0x49fe, { 0xb9, 0x7b, 0xa9, 0xdc, 0xfd, 0xcc, 0x1b, 0x4f } };
static const GUID clsid_dred = { 0x4a75bbc4, 0x9ff4, 0x4ad8, { 0x9f, 0x18, 0xab, 0xae, 0x84, 0xdc, 0x5f, 0xf2 } };
static const GUID clsid_sdk_configuration = { 0x7cda6aca, 0xa03e, 0x49c8, { 0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce } };

static UINT DeviceFlags(IUnknown *object)
{
    ID3D12DeviceConfiguration *configuration = NULL;
    UINT flags = ~0u;

    object->QueryInterface(__uuidof(ID3D12DeviceConfiguration), reinterpret_cast<void **>(&configuration));
    if (configuration)
    {
        flags = configuration->GetDesc().Flags;
        configuration->Release();
    }
    return flags;
}

static void TestSdkConfiguration(PFN_D3D12_GET_INTERFACE_FACTORY get_interface)
{
    ID3D12SDKConfiguration1 *configuration = NULL;
    ID3D12DeviceFactory *factory = NULL;
    HRESULT hr;

    hr = get_interface(clsid_sdk_configuration, __uuidof(ID3D12SDKConfiguration1),
            reinterpret_cast<void **>(&configuration));
    ok(hr == S_OK && configuration, "SDK configuration 1: %#lx\n", hr);
    if (!configuration) return;
    hr = configuration->CreateDeviceFactory(0, ".\\", __uuidof(ID3D12DeviceFactory),
            reinterpret_cast<void **>(&factory));
    ok(hr == S_OK && factory, "CreateDeviceFactory with the global SDK: %#lx\n", hr);
    if (factory) factory->Release();
    factory = NULL;
    hr = configuration->CreateDeviceFactory(614, ".\\D3D12\\", __uuidof(ID3D12DeviceFactory),
            reinterpret_cast<void **>(&factory));
    ok(hr == D3D12_ERROR_INVALID_REDIST && !factory, "CreateDeviceFactory without a redistributable: %#lx\n", hr);
    if (factory) factory->Release();
    configuration->Release();
}

static void RunFactory(void *)
{
    const UINT dred_flags = D3D12_DEVICE_FLAG_DRED_AUTO_BREADCRUMBS_ENABLED
            | D3D12_DEVICE_FLAG_DRED_PAGE_FAULT_REPORTING_ENABLED;
    ID3D12DeviceRemovedExtendedDataSettings *settings = NULL;
    ID3D12Device *global = NULL, *independent = NULL, *again = NULL;
    PFN_D3D12_GET_INTERFACE_FACTORY get_interface;
    IDXGIFactory4 *dxgi = NULL;
    IUnknown *adapter = NULL;
    ID3D12DeviceFactory *factory = NULL;
    void *unused = NULL;
    HRESULT hr;

    get_interface = reinterpret_cast<PFN_D3D12_GET_INTERFACE_FACTORY>(
            GetProcAddress(GetModuleHandleA("d3d12.dll"), "D3D12GetInterface"));
    ok(get_interface != NULL, "D3D12GetInterface is not exported\n");
    if (!get_interface) return;
    TestSdkConfiguration(get_interface);

    hr = get_interface(clsid_device_factory, __uuidof(ID3D12DeviceFactory), reinterpret_cast<void **>(&factory));
    ok(hr == S_OK && factory, "device factory: %#lx\n", hr);
    if (!factory) return;

    ok(factory->GetFlags() == D3D12_DEVICE_FACTORY_FLAG_NONE, "initial flags %#x\n", factory->GetFlags());
    hr = factory->SetFlags(static_cast<D3D12_DEVICE_FACTORY_FLAGS>(0x8));
    ok(hr == E_INVALIDARG, "unknown flag: %#lx\n", hr);
    hr = factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_INCOMPATIBLE_EXISTING_DEVICE);
    ok(hr == E_INVALIDARG, "incompatible without existing: %#lx\n", hr);
    hr = factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE
            | D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_INCOMPATIBLE_EXISTING_DEVICE);
    ok(hr == S_OK, "existing and incompatible: %#lx\n", hr);
    ok(factory->GetFlags() == 3, "flags %#x\n", factory->GetFlags());
    hr = factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_NONE);
    ok(hr == S_OK, "no flags: %#lx\n", hr);

    hr = factory->GetConfigurationInterface(clsid_debug, __uuidof(ID3D12Debug), &unused);
    ok(hr == DXGI_ERROR_SDK_COMPONENT_MISSING && !unused, "debug configuration: %#lx\n", hr);
    hr = factory->InitializeFromGlobalState();
    ok(hr == S_OK, "InitializeFromGlobalState: %#lx\n", hr);
    hr = factory->GetConfigurationInterface(clsid_dred, __uuidof(ID3D12DeviceRemovedExtendedDataSettings),
            reinterpret_cast<void **>(&settings));
    ok(hr == S_OK && settings, "DRED configuration: %#lx\n", hr);

    global = d3d12_test_create_device();
    if (global && SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void **>(&dxgi))))
    {
        dxgi->EnumAdapterByLuid(global->GetAdapterLuid(), __uuidof(IUnknown), reinterpret_cast<void **>(&adapter));
        dxgi->Release();
    }
    ok(adapter != NULL, "adapter of the global device\n");
    if (global && settings && adapter)
    {
        settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        ok((DeviceFlags(factory) & dred_flags) == dred_flags, "factory flags %#x\n", DeviceFlags(factory));
        hr = factory->CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                reinterpret_cast<void **>(&independent));
        ok(hr == S_OK && independent && independent != global, "CreateDevice: %#lx, %p, global %p\n", hr,
                independent, global);
        if (independent)
        {
            ok((DeviceFlags(independent) & dred_flags) == dred_flags, "independent device flags %#x\n",
                    DeviceFlags(independent));
            ok(!(DeviceFlags(global) & dred_flags), "global device flags %#x\n", DeviceFlags(global));
            hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                    reinterpret_cast<void **>(&again));
            ok(hr == S_OK && again == global, "global device after an independent one: %#lx, %p, %p\n", hr, again,
                    global);
            if (again) again->Release();
            independent->Release();
            independent = NULL;
        }
        hr = factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE);
        ok(hr == S_OK, "existing: %#lx\n", hr);
        hr = factory->CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                reinterpret_cast<void **>(&independent));
        ok(hr == S_OK && independent && independent != global,
                "CreateDevice with the existing flag and another configuration: %#lx\n", hr);
        if (independent) independent->Release();
        hr = factory->CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), NULL);
        ok(hr == S_FALSE, "CreateDevice without an output: %#lx\n", hr);
    }
    if (settings) settings->Release();
    if (adapter) adapter->Release();
    if (global)
    {
        ok(global->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n", global->GetDeviceRemovedReason());
        global->Release();
    }
    factory->Release();
}

START_TEST(factory)
{
    d3d12_test_bounded("factory", RunFactory, NULL, D3D12_TEST_BUDGET);
}
