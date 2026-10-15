/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3D12GetInterface, D3D12GetDebugInterface and device DRED interfaces
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "d3d12_apitest.h"

typedef HRESULT (WINAPI *PFN_D3D12_GET_INTERFACE_TEST)(REFCLSID, REFIID, void **);

static const GUID unknown_clsid = { 0x11111111, 0x2222, 0x3333, { 0x44, 0x44, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 } };
static const GUID clsid_debug = { 0xf2352aeb, 0xdd84, 0x49fe, { 0xb9, 0x7b, 0xa9, 0xdc, 0xfd, 0xcc, 0x1b, 0x4f } };
static const GUID clsid_dred = { 0x4a75bbc4, 0x9ff4, 0x4ad8, { 0x9f, 0x18, 0xab, 0xae, 0x84, 0xdc, 0x5f, 0xf2 } };
static const GUID clsid_sdk_configuration = { 0x7cda6aca, 0xa03e, 0x49c8, { 0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce } };

static void TestGetInterface(PFN_D3D12_GET_INTERFACE_TEST get_interface)
{
    ID3D12SDKConfiguration *configuration = NULL;
    ID3D12DeviceRemovedExtendedDataSettings *settings = NULL;
    ID3D12DeviceRemovedExtendedDataSettings1 *settings1 = NULL;
    IUnknown *object;
    HRESULT hr;

    hr = get_interface(clsid_sdk_configuration, __uuidof(ID3D12SDKConfiguration),
            reinterpret_cast<void **>(&configuration));
    ok(hr == S_OK, "SDK configuration: %#lx\n", hr);
    if (configuration)
    {
        hr = configuration->SetSDKVersion(1, ".\\D3D12\\");
        ok(hr == DXGI_ERROR_INVALID_CALL, "SetSDKVersion outside developer mode: %#lx\n", hr);
        configuration->Release();
    }

    object = reinterpret_cast<IUnknown *>(0x1);
    hr = get_interface(clsid_sdk_configuration, __uuidof(ID3D12Device), reinterpret_cast<void **>(&object));
    ok(hr == E_NOINTERFACE && !object, "SDK configuration with a foreign interface: %#lx, %p\n", hr, object);

    hr = get_interface(clsid_dred, __uuidof(ID3D12DeviceRemovedExtendedDataSettings),
            reinterpret_cast<void **>(&settings));
    ok(hr == S_OK, "DRED settings: %#lx\n", hr);
    if (settings)
    {
        hr = settings->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedDataSettings1),
                reinterpret_cast<void **>(&settings1));
        ok(hr == S_OK, "DRED settings 1: %#lx\n", hr);
        if (settings1) settings1->Release();
        settings->Release();
    }

    object = reinterpret_cast<IUnknown *>(0x1);
    hr = get_interface(unknown_clsid, __uuidof(IUnknown), reinterpret_cast<void **>(&object));
    ok(hr == E_NOINTERFACE && object == reinterpret_cast<IUnknown *>(0x1), "unknown CLSID: %#lx, %p\n", hr, object);

    object = NULL;
    hr = get_interface(clsid_debug, __uuidof(ID3D12Debug), reinterpret_cast<void **>(&object));
    ok(hr == S_OK || hr == DXGI_ERROR_SDK_COMPONENT_MISSING, "debug layer: %#lx\n", hr);
    if (object) object->Release();
}

static void TestDeviceDred(BOOL enabled)
{
    ID3D12DeviceRemovedExtendedData *dred = NULL;
    ID3D12DeviceRemovedExtendedData1 *dred1 = NULL;
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs1 = {};
    D3D12_DRED_PAGE_FAULT_OUTPUT page_fault = {};
    ID3D12Device *device;
    HRESULT hr;

    if (!(device = d3d12_test_create_device())) return;
    hr = device->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedData), reinterpret_cast<void **>(&dred));
    ok(hr == S_OK, "device DRED: %#lx\n", hr);
    if (dred)
    {
        HRESULT expected = enabled ? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE : DXGI_ERROR_UNSUPPORTED;

        hr = dred->GetAutoBreadcrumbsOutput(&breadcrumbs);
        ok(hr == expected, "enabled %d: breadcrumbs of a live device: %#lx\n", enabled, hr);
        hr = dred->GetPageFaultAllocationOutput(&page_fault);
        ok(hr == expected, "enabled %d: page fault of a live device: %#lx\n", enabled, hr);
        hr = dred->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedData1), reinterpret_cast<void **>(&dred1));
        ok(hr == S_OK, "device DRED 1: %#lx\n", hr);
        if (dred1)
        {
            hr = dred1->GetAutoBreadcrumbsOutput1(&breadcrumbs1);
            ok(hr == expected, "enabled %d: breadcrumbs 1 of a live device: %#lx\n", enabled, hr);
            dred1->Release();
        }
        dred->Release();
    }
    ok(device->GetDeviceRemovedReason() == S_OK, "device removed reason %#lx\n", device->GetDeviceRemovedReason());
    device->Release();
}

static void RunInterfaces(void *)
{
    ID3D12DeviceRemovedExtendedDataSettings *settings = NULL;
    PFN_D3D12_GET_INTERFACE_TEST get_interface;
    HRESULT hr;

    get_interface = reinterpret_cast<PFN_D3D12_GET_INTERFACE_TEST>(
            GetProcAddress(GetModuleHandleA("d3d12.dll"), "D3D12GetInterface"));
    ok(get_interface != NULL, "D3D12GetInterface is not exported\n");
    if (get_interface) TestGetInterface(get_interface);

    hr = D3D12GetDebugInterface(__uuidof(ID3D12DeviceRemovedExtendedDataSettings),
            reinterpret_cast<void **>(&settings));
    ok(hr == S_OK, "D3D12GetDebugInterface(DRED settings): %#lx\n", hr);
    if (settings)
    {
        TestDeviceDred(FALSE);
        settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        TestDeviceDred(TRUE);
        settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED);
        settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED);
        settings->Release();
    }
}

START_TEST(interfaces)
{
    d3d12_test_bounded("interfaces", RunInterfaces, NULL, D3D12_TEST_BUDGET);
}
