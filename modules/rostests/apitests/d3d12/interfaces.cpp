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
    ID3D12DeviceRemovedExtendedDataSettings2 *settings2 = NULL;
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
        hr = settings->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedDataSettings2),
                reinterpret_cast<void **>(&settings2));
        ok(hr == S_OK, "DRED settings 2: %#lx\n", hr);
        if (settings2)
        {
            settings2->UseMarkersOnlyAutoBreadcrumbs(FALSE);
            settings2->Release();
        }
        settings->Release();
    }
    settings2 = NULL;
    hr = get_interface(clsid_dred, __uuidof(ID3D12DeviceRemovedExtendedDataSettings2),
            reinterpret_cast<void **>(&settings2));
    ok(hr == S_OK && settings2, "DRED settings 2 from D3D12GetInterface: %#lx\n", hr);
    if (settings2) settings2->Release();

    object = reinterpret_cast<IUnknown *>(0x1);
    hr = get_interface(unknown_clsid, __uuidof(IUnknown), reinterpret_cast<void **>(&object));
    ok(hr == E_NOINTERFACE && object == reinterpret_cast<IUnknown *>(0x1), "unknown CLSID: %#lx, %p\n", hr, object);

    object = NULL;
    hr = get_interface(clsid_debug, __uuidof(ID3D12Debug), reinterpret_cast<void **>(&object));
    ok(hr == S_OK || hr == DXGI_ERROR_SDK_COMPONENT_MISSING, "debug layer: %#lx\n", hr);
    if (object) object->Release();
}

static void TestDeviceConfiguration(ID3D12Device *device, BOOL enabled)
{
    const UINT dred_flags = D3D12_DEVICE_FLAG_DRED_AUTO_BREADCRUMBS_ENABLED
            | D3D12_DEVICE_FLAG_DRED_PAGE_FAULT_REPORTING_ENABLED;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC root = {};
    ID3D12DeviceConfiguration1 *configuration1 = NULL;
    ID3D12DeviceConfiguration *configuration = NULL;
    ID3D12VersionedRootSignatureDeserializer *deserializer = NULL;
    D3D12_DEVICE_CONFIGURATION_DESC desc;
    ID3DBlob *blob = NULL, *error = NULL;
    HRESULT hr;

    hr = device->QueryInterface(__uuidof(ID3D12DeviceConfiguration), reinterpret_cast<void **>(&configuration));
    ok(hr == S_OK && configuration, "device configuration: %#lx\n", hr);
    if (!configuration) return;
    desc = configuration->GetDesc();
    trace("enabled %d: configuration flags %#x, SDK version %u\n", enabled, desc.Flags, desc.SDKVersion);
    ok((desc.Flags & dred_flags) == (enabled ? dred_flags : 0), "enabled %d: flags %#x\n", enabled, desc.Flags);
    ok(!(desc.Flags & (D3D12_DEVICE_FLAG_DEBUG_LAYER_ENABLED | D3D12_DEVICE_FLAG_GPU_BASED_VALIDATION_ENABLED)),
            "debug flags %#x\n", desc.Flags);
    ok(desc.GpuBasedValidationFlags == D3D12_GPU_BASED_VALIDATION_FLAGS_NONE, "GBV flags %#x\n",
            desc.GpuBasedValidationFlags);
    ok(desc.SDKVersion >= 600, "SDK version %u\n", desc.SDKVersion);
    ok(!desc.NumEnabledExperimentalFeatures, "experimental features %u\n", desc.NumEnabledExperimentalFeatures);
    hr = configuration->GetEnabledExperimentalFeatures(NULL, 0);
    ok(hr == S_OK, "GetEnabledExperimentalFeatures: %#lx\n", hr);

    root.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    root.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    hr = configuration->SerializeVersionedRootSignature(&root, &blob, &error);
    ok(hr == S_OK && blob, "SerializeVersionedRootSignature: %#lx\n", hr);
    if (blob)
    {
        hr = configuration->CreateVersionedRootSignatureDeserializer(blob->GetBufferPointer(), blob->GetBufferSize(),
                __uuidof(ID3D12VersionedRootSignatureDeserializer), reinterpret_cast<void **>(&deserializer));
        ok(hr == S_OK && deserializer, "CreateVersionedRootSignatureDeserializer: %#lx\n", hr);
        if (deserializer) deserializer->Release();
        deserializer = NULL;
        hr = configuration->QueryInterface(__uuidof(ID3D12DeviceConfiguration1),
                reinterpret_cast<void **>(&configuration1));
        ok(hr == S_OK && configuration1, "device configuration 1: %#lx\n", hr);
        if (configuration1)
        {
            hr = configuration1->CreateVersionedRootSignatureDeserializerFromSubobjectInLibrary(
                    blob->GetBufferPointer(), blob->GetBufferSize(), L"root",
                    __uuidof(ID3D12VersionedRootSignatureDeserializer), reinterpret_cast<void **>(&deserializer));
            ok(hr == E_INVALIDARG && !deserializer, "deserializer from a blob that is no library: %#lx\n", hr);
            if (deserializer) deserializer->Release();
            configuration1->Release();
        }
        blob->Release();
    }
    if (error) error->Release();
    configuration->Release();
}

static void TestDeviceDred(BOOL enabled)
{
    ID3D12DeviceRemovedExtendedData *dred = NULL;
    ID3D12DeviceRemovedExtendedData1 *dred1 = NULL;
    ID3D12DeviceRemovedExtendedData2 *dred2 = NULL;
    D3D12_DRED_PAGE_FAULT_OUTPUT2 page_fault2;
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
        hr = dred->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedData2), reinterpret_cast<void **>(&dred2));
        ok(hr == S_OK, "device DRED 2: %#lx\n", hr);
        if (dred2)
        {
            memset(&page_fault2, 0xcc, sizeof(page_fault2));
            hr = dred2->GetPageFaultAllocationOutput2(&page_fault2);
            ok(hr == expected && page_fault2.PageFaultFlags == 0xccccccccu,
                    "enabled %d: page fault 2 of a live device: %#lx, flags %#x\n", enabled, hr,
                    page_fault2.PageFaultFlags);
            ok(dred2->GetDeviceState() == D3D12_DRED_DEVICE_STATE_UNKNOWN, "enabled %d: device state %u\n", enabled,
                    dred2->GetDeviceState());
            dred2->Release();
        }
        dred->Release();
    }
    TestDeviceConfiguration(device, enabled);
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
    {
        ID3D12DeviceRemovedExtendedDataSettings2 *settings2 = NULL;

        hr = D3D12GetDebugInterface(__uuidof(ID3D12DeviceRemovedExtendedDataSettings2),
                reinterpret_cast<void **>(&settings2));
        ok(hr == S_OK && settings2, "D3D12GetDebugInterface(DRED settings 2): %#lx\n", hr);
        if (settings2) settings2->Release();
    }
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
