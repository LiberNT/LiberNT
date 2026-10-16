/*
 * Copyright 2018 Józef Kucia for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <d3d12.h>
#include <dxgi.h>
#include <vkd3d.h>

#ifdef REACTOS_D3D12_NATIVE_UMD
HRESULT d3d12_native_create_device(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID iid,
        void **device);
#endif

static HRESULT
d3d12_signal_event(HANDLE event)
{
    return SetEvent(event) ? S_OK : E_FAIL;
}

static HRESULT
d3d12_get_adapter_luid(IUnknown *adapter, LUID *luid)
{
    IDXGIFactory1 *factory;
    IDXGIAdapter *dxgi_adapter;
    DXGI_ADAPTER_DESC desc;
    HRESULT hr;

    if (adapter)
    {
        hr = IUnknown_QueryInterface(adapter, &IID_IDXGIAdapter, (void **)&dxgi_adapter);
    }
    else
    {
        if (FAILED(hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory)))
            return hr;
        hr = IDXGIFactory1_EnumAdapters(factory, 0, &dxgi_adapter);
        IDXGIFactory1_Release(factory);
    }
    if (FAILED(hr))
        return hr;

    hr = IDXGIAdapter_GetDesc(dxgi_adapter, &desc);
    IDXGIAdapter_Release(dxgi_adapter);
    if (SUCCEEDED(hr))
        *luid = desc.AdapterLuid;
    return hr;
}

static HRESULT
d3d12_create_vkd3d_device(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID iid, void **device)
{
    static const char * const instance_extensions[] =
    {
        VK_KHR_SURFACE_EXTENSION_NAME,
        "VK_KHR_win32_surface",
    };
    static const char * const device_extensions[] =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    struct vkd3d_optional_instance_extensions_info optional_instance_extensions_info;
    struct vkd3d_optional_device_extensions_info optional_device_extensions_info;
    struct vkd3d_instance_create_info instance_create_info;
    struct vkd3d_device_create_info device_create_info;
    struct vkd3d_application_info application_info;
    LUID luid;
    HRESULT hr;

    if (FAILED(hr = d3d12_get_adapter_luid(adapter, &luid)))
        return hr;

    memset(&application_info, 0, sizeof(application_info));
    application_info.type = VKD3D_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.api_version = VKD3D_API_VERSION_1_2;

    memset(&optional_instance_extensions_info, 0, sizeof(optional_instance_extensions_info));
    optional_instance_extensions_info.type = VKD3D_STRUCTURE_TYPE_OPTIONAL_INSTANCE_EXTENSIONS_INFO;
    optional_instance_extensions_info.next = &application_info;
    optional_instance_extensions_info.extensions = instance_extensions;
    optional_instance_extensions_info.extension_count = ARRAYSIZE(instance_extensions);

    memset(&instance_create_info, 0, sizeof(instance_create_info));
    instance_create_info.type = VKD3D_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.next = &optional_instance_extensions_info;
    instance_create_info.pfn_signal_event = d3d12_signal_event;
    instance_create_info.wchar_size = sizeof(WCHAR);

    memset(&optional_device_extensions_info, 0, sizeof(optional_device_extensions_info));
    optional_device_extensions_info.type = VKD3D_STRUCTURE_TYPE_OPTIONAL_DEVICE_EXTENSIONS_INFO;
    optional_device_extensions_info.extensions = device_extensions;
    optional_device_extensions_info.extension_count = ARRAYSIZE(device_extensions);

    memset(&device_create_info, 0, sizeof(device_create_info));
    device_create_info.type = VKD3D_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.next = &optional_device_extensions_info;
    device_create_info.minimum_feature_level = minimum_feature_level;
    device_create_info.instance_create_info = &instance_create_info;
    device_create_info.adapter_luid = luid;

    return vkd3d_create_device(&device_create_info, iid, device);
}

HRESULT WINAPI
d3d12_create_device(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID iid, void **device)
{
#ifdef REACTOS_D3D12_NATIVE_UMD
    HRESULT hr = d3d12_native_create_device(adapter, minimum_feature_level, iid, device);

    if (hr != DXGI_ERROR_UNSUPPORTED)
        return hr;
#endif
    return d3d12_create_vkd3d_device(adapter, minimum_feature_level, iid, device);
}

static LONG d3d12_dred_auto_breadcrumbs;
static LONG d3d12_dred_page_fault;
static LONG d3d12_dred_watson_dump;
static LONG d3d12_dred_breadcrumb_context;
static LONG d3d12_dred_markers_only;

void d3d12_dred_enablement(D3D12_DRED_ENABLEMENT *auto_breadcrumbs, D3D12_DRED_ENABLEMENT *page_fault)
{
    *auto_breadcrumbs = d3d12_dred_auto_breadcrumbs;
    *page_fault = d3d12_dred_page_fault;
}

UINT d3d12_device_configuration_flags(void)
{
    UINT flags = 0;

    if (d3d12_dred_auto_breadcrumbs == D3D12_DRED_ENABLEMENT_FORCED_ON)
        flags |= D3D12_DEVICE_FLAG_DRED_AUTO_BREADCRUMBS_ENABLED;
    if (d3d12_dred_page_fault == D3D12_DRED_ENABLEMENT_FORCED_ON)
        flags |= D3D12_DEVICE_FLAG_DRED_PAGE_FAULT_REPORTING_ENABLED;
    if (d3d12_dred_watson_dump == D3D12_DRED_ENABLEMENT_FORCED_ON)
        flags |= D3D12_DEVICE_FLAG_DRED_WATSON_REPORTING_ENABLED;
    if (d3d12_dred_breadcrumb_context == D3D12_DRED_ENABLEMENT_FORCED_ON)
        flags |= D3D12_DEVICE_FLAG_DRED_BREADCRUMB_CONTEXT_ENABLED;
    if (d3d12_dred_markers_only)
        flags |= D3D12_DEVICE_FLAG_DRED_USE_MARKERS_ONLY_BREADCRUMBS;
    return flags;
}

static void d3d12_dred_update(void)
{
    vkd3d_set_dred_enablement(d3d12_dred_auto_breadcrumbs, d3d12_dred_page_fault);
    vkd3d_set_device_configuration_flags(d3d12_device_configuration_flags());
}

struct d3d12_dred_settings
{
    ID3D12DeviceRemovedExtendedDataSettings2 ID3D12DeviceRemovedExtendedDataSettings2_iface;
    LONG refcount;
};

static struct d3d12_dred_settings *impl_from_ID3D12DeviceRemovedExtendedDataSettings2(
        ID3D12DeviceRemovedExtendedDataSettings2 *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_dred_settings, ID3D12DeviceRemovedExtendedDataSettings2_iface);
}

static HRESULT STDMETHODCALLTYPE
d3d12_dred_settings_QueryInterface(ID3D12DeviceRemovedExtendedDataSettings2 *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_ID3D12DeviceRemovedExtendedDataSettings2)
            || IsEqualGUID(iid, &IID_ID3D12DeviceRemovedExtendedDataSettings1)
            || IsEqualGUID(iid, &IID_ID3D12DeviceRemovedExtendedDataSettings)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12DeviceRemovedExtendedDataSettings2_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d3d12_dred_settings_AddRef(ID3D12DeviceRemovedExtendedDataSettings2 *iface)
{
    return InterlockedIncrement(&impl_from_ID3D12DeviceRemovedExtendedDataSettings2(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE
d3d12_dred_settings_Release(ID3D12DeviceRemovedExtendedDataSettings2 *iface)
{
    struct d3d12_dred_settings *settings = impl_from_ID3D12DeviceRemovedExtendedDataSettings2(iface);
    ULONG refcount = InterlockedDecrement(&settings->refcount);

    if (!refcount)
        HeapFree(GetProcessHeap(), 0, settings);
    return refcount;
}

static void STDMETHODCALLTYPE
d3d12_dred_settings_SetAutoBreadcrumbsEnablement(ID3D12DeviceRemovedExtendedDataSettings2 *iface,
                                                 D3D12_DRED_ENABLEMENT enablement)
{
    InterlockedExchange(&d3d12_dred_auto_breadcrumbs, enablement);
    d3d12_dred_update();
}

static void STDMETHODCALLTYPE
d3d12_dred_settings_SetPageFaultEnablement(ID3D12DeviceRemovedExtendedDataSettings2 *iface,
                                           D3D12_DRED_ENABLEMENT enablement)
{
    InterlockedExchange(&d3d12_dred_page_fault, enablement);
    d3d12_dred_update();
}

static void STDMETHODCALLTYPE
d3d12_dred_settings_SetWatsonDumpEnablement(ID3D12DeviceRemovedExtendedDataSettings2 *iface,
                                            D3D12_DRED_ENABLEMENT enablement)
{
    InterlockedExchange(&d3d12_dred_watson_dump, enablement);
    d3d12_dred_update();
}

static void STDMETHODCALLTYPE
d3d12_dred_settings_SetBreadcrumbContextEnablement(ID3D12DeviceRemovedExtendedDataSettings2 *iface,
                                                   D3D12_DRED_ENABLEMENT enablement)
{
    InterlockedExchange(&d3d12_dred_breadcrumb_context, enablement);
    d3d12_dred_update();
}

static void STDMETHODCALLTYPE
d3d12_dred_settings_UseMarkersOnlyAutoBreadcrumbs(ID3D12DeviceRemovedExtendedDataSettings2 *iface, BOOL markers_only)
{
    InterlockedExchange(&d3d12_dred_markers_only, markers_only);
    d3d12_dred_update();
}

static const ID3D12DeviceRemovedExtendedDataSettings2Vtbl d3d12_dred_settings_vtbl =
{
    d3d12_dred_settings_QueryInterface,
    d3d12_dred_settings_AddRef,
    d3d12_dred_settings_Release,
    d3d12_dred_settings_SetAutoBreadcrumbsEnablement,
    d3d12_dred_settings_SetPageFaultEnablement,
    d3d12_dred_settings_SetWatsonDumpEnablement,
    d3d12_dred_settings_SetBreadcrumbContextEnablement,
    d3d12_dred_settings_UseMarkersOnlyAutoBreadcrumbs,
};

static HRESULT
d3d12_dred_settings_create(REFIID iid, void **out)
{
    struct d3d12_dred_settings *settings;
    HRESULT hr;

    if (!(settings = HeapAlloc(GetProcessHeap(), 0, sizeof(*settings))))
        return E_OUTOFMEMORY;
    settings->ID3D12DeviceRemovedExtendedDataSettings2_iface.lpVtbl = &d3d12_dred_settings_vtbl;
    settings->refcount = 1;
    hr = ID3D12DeviceRemovedExtendedDataSettings2_QueryInterface(
            &settings->ID3D12DeviceRemovedExtendedDataSettings2_iface, iid, out);
    ID3D12DeviceRemovedExtendedDataSettings2_Release(&settings->ID3D12DeviceRemovedExtendedDataSettings2_iface);
    return hr;
}

struct d3d12_sdk_configuration
{
    ID3D12SDKConfiguration ID3D12SDKConfiguration_iface;
    LONG refcount;
};

static struct d3d12_sdk_configuration *impl_from_ID3D12SDKConfiguration(ID3D12SDKConfiguration *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_sdk_configuration, ID3D12SDKConfiguration_iface);
}

static HRESULT STDMETHODCALLTYPE
d3d12_sdk_configuration_QueryInterface(ID3D12SDKConfiguration *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_ID3D12SDKConfiguration) || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12SDKConfiguration_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d3d12_sdk_configuration_AddRef(ID3D12SDKConfiguration *iface)
{
    return InterlockedIncrement(&impl_from_ID3D12SDKConfiguration(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE
d3d12_sdk_configuration_Release(ID3D12SDKConfiguration *iface)
{
    struct d3d12_sdk_configuration *configuration = impl_from_ID3D12SDKConfiguration(iface);
    ULONG refcount = InterlockedDecrement(&configuration->refcount);

    if (!refcount)
        HeapFree(GetProcessHeap(), 0, configuration);
    return refcount;
}

static HRESULT STDMETHODCALLTYPE
d3d12_sdk_configuration_SetSDKVersion(ID3D12SDKConfiguration *iface, UINT version, const char *path)
{
    return DXGI_ERROR_INVALID_CALL;
}

static const ID3D12SDKConfigurationVtbl d3d12_sdk_configuration_vtbl =
{
    d3d12_sdk_configuration_QueryInterface,
    d3d12_sdk_configuration_AddRef,
    d3d12_sdk_configuration_Release,
    d3d12_sdk_configuration_SetSDKVersion,
};

static HRESULT
d3d12_sdk_configuration_create(REFIID iid, void **out)
{
    struct d3d12_sdk_configuration *configuration;
    HRESULT hr;

    if (!(configuration = HeapAlloc(GetProcessHeap(), 0, sizeof(*configuration))))
        return E_OUTOFMEMORY;
    configuration->ID3D12SDKConfiguration_iface.lpVtbl = &d3d12_sdk_configuration_vtbl;
    configuration->refcount = 1;
    hr = ID3D12SDKConfiguration_QueryInterface(&configuration->ID3D12SDKConfiguration_iface, iid, out);
    ID3D12SDKConfiguration_Release(&configuration->ID3D12SDKConfiguration_iface);
    return hr;
}

HRESULT WINAPI
D3D12GetDebugInterface(REFIID iid, void **debug)
{
    if (!debug)
        return DXGI_ERROR_SDK_COMPONENT_MISSING;
    *debug = NULL;

    if (IsEqualGUID(iid, &IID_ID3D12DeviceRemovedExtendedDataSettings)
            || IsEqualGUID(iid, &IID_ID3D12DeviceRemovedExtendedDataSettings1)
            || IsEqualGUID(iid, &IID_ID3D12DeviceRemovedExtendedDataSettings2))
        return d3d12_dred_settings_create(iid, debug);

    return DXGI_ERROR_SDK_COMPONENT_MISSING;
}

HRESULT WINAPI
D3D12CreateRootSignatureDeserializer(const void *data, SIZE_T data_size, REFIID iid, void **deserializer)
{
    return vkd3d_create_root_signature_deserializer(data, data_size, iid, deserializer);
}

HRESULT WINAPI
D3D12CreateVersionedRootSignatureDeserializer(const void *data, SIZE_T data_size, REFIID iid, void **deserializer)
{
    return vkd3d_create_versioned_root_signature_deserializer(data, data_size, iid, deserializer);
}

HRESULT WINAPI
D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC *root_signature_desc, D3D_ROOT_SIGNATURE_VERSION version,
                            ID3DBlob **blob, ID3DBlob **error_blob)
{
    return vkd3d_serialize_root_signature(root_signature_desc, version, blob, error_blob);
}

HRESULT WINAPI
D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, ID3DBlob **blob,
                                     ID3DBlob **error_blob)
{
    return vkd3d_serialize_versioned_root_signature(desc, blob, error_blob);
}

HRESULT WINAPI
D3D12EnableExperimentalFeatures(UINT FeatureCount, const IID *Iids, void *Configurations, UINT *ConfigurationSizes)
{
    UNREFERENCED_PARAMETER(FeatureCount);
    UNREFERENCED_PARAMETER(Iids);
    UNREFERENCED_PARAMETER(Configurations);
    UNREFERENCED_PARAMETER(ConfigurationSizes);

    return E_NOINTERFACE;
}

HRESULT WINAPI
D3D12GetInterface(REFCLSID clsid, REFIID iid, void **debug)
{
    if (IsEqualGUID(clsid, &CLSID_D3D12Debug))
        return D3D12GetDebugInterface(iid, debug);
    if (debug && IsEqualGUID(clsid, &CLSID_D3D12DeviceRemovedExtendedData))
        return d3d12_dred_settings_create(iid, debug);
    if (debug && IsEqualGUID(clsid, &CLSID_D3D12SDKConfiguration))
        return d3d12_sdk_configuration_create(iid, debug);

    return E_NOINTERFACE;
}
