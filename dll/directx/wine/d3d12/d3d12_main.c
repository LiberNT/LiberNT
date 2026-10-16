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
HRESULT d3d12_native_create_device(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level, BOOL independent,
        D3D12_DRED_ENABLEMENT auto_breadcrumbs, D3D12_DRED_ENABLEMENT page_fault, UINT configuration, REFIID iid,
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
d3d12_create_vkd3d_device(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level,
        const struct vkd3d_device_configuration_info *configuration, REFIID iid, void **device)
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
    struct vkd3d_device_configuration_info device_configuration;
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
    if (configuration)
    {
        device_configuration = *configuration;
        device_configuration.next = device_create_info.next;
        device_create_info.next = &device_configuration;
    }

    return vkd3d_create_device(&device_create_info, iid, device);
}

static HRESULT
d3d12_create_configured_device(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level,
        const struct vkd3d_device_configuration_info *configuration, REFIID iid, void **device)
{
#ifdef REACTOS_D3D12_NATIVE_UMD
    HRESULT hr = configuration
            ? d3d12_native_create_device(adapter, minimum_feature_level, TRUE, configuration->dred_auto_breadcrumbs,
                    configuration->dred_page_fault, configuration->configuration_flags, iid, device)
            : d3d12_native_create_device(adapter, minimum_feature_level, FALSE, D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED,
                    D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED, 0, iid, device);

    if (hr != DXGI_ERROR_UNSUPPORTED)
        return hr;
#endif
    return d3d12_create_vkd3d_device(adapter, minimum_feature_level, configuration, iid, device);
}

HRESULT WINAPI
d3d12_create_device(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID iid, void **device)
{
    return d3d12_create_configured_device(adapter, minimum_feature_level, NULL, iid, device);
}

struct d3d12_dred_state
{
    LONG auto_breadcrumbs;
    LONG page_fault;
    LONG watson_dump;
    LONG breadcrumb_context;
    LONG markers_only;
};

static struct d3d12_dred_state d3d12_dred;

void d3d12_dred_enablement(D3D12_DRED_ENABLEMENT *auto_breadcrumbs, D3D12_DRED_ENABLEMENT *page_fault)
{
    *auto_breadcrumbs = d3d12_dred.auto_breadcrumbs;
    *page_fault = d3d12_dred.page_fault;
}

static UINT d3d12_dred_flags(const struct d3d12_dred_state *state)
{
    UINT flags = 0;

    if (state->auto_breadcrumbs == D3D12_DRED_ENABLEMENT_FORCED_ON)
        flags |= D3D12_DEVICE_FLAG_DRED_AUTO_BREADCRUMBS_ENABLED;
    if (state->page_fault == D3D12_DRED_ENABLEMENT_FORCED_ON)
        flags |= D3D12_DEVICE_FLAG_DRED_PAGE_FAULT_REPORTING_ENABLED;
    if (state->watson_dump == D3D12_DRED_ENABLEMENT_FORCED_ON)
        flags |= D3D12_DEVICE_FLAG_DRED_WATSON_REPORTING_ENABLED;
    if (state->breadcrumb_context == D3D12_DRED_ENABLEMENT_FORCED_ON)
        flags |= D3D12_DEVICE_FLAG_DRED_BREADCRUMB_CONTEXT_ENABLED;
    if (state->markers_only)
        flags |= D3D12_DEVICE_FLAG_DRED_USE_MARKERS_ONLY_BREADCRUMBS;
    return flags;
}

UINT d3d12_device_configuration_flags(void)
{
    return d3d12_dred_flags(&d3d12_dred);
}

static void d3d12_dred_update(void)
{
    vkd3d_set_dred_enablement(d3d12_dred.auto_breadcrumbs, d3d12_dred.page_fault);
    vkd3d_set_device_configuration_flags(d3d12_device_configuration_flags());
}

struct d3d12_dred_settings
{
    ID3D12DeviceRemovedExtendedDataSettings2 ID3D12DeviceRemovedExtendedDataSettings2_iface;
    LONG refcount;
    struct d3d12_dred_state *state;
    IUnknown *owner;
};

static void d3d12_dred_settings_changed(struct d3d12_dred_settings *settings)
{
    if (settings->state == &d3d12_dred)
        d3d12_dred_update();
}

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
    {
        if (settings->owner)
            IUnknown_Release(settings->owner);
        HeapFree(GetProcessHeap(), 0, settings);
    }
    return refcount;
}

static void STDMETHODCALLTYPE
d3d12_dred_settings_SetAutoBreadcrumbsEnablement(ID3D12DeviceRemovedExtendedDataSettings2 *iface,
                                                 D3D12_DRED_ENABLEMENT enablement)
{
    struct d3d12_dred_settings *settings = impl_from_ID3D12DeviceRemovedExtendedDataSettings2(iface);

    InterlockedExchange(&settings->state->auto_breadcrumbs, enablement);
    d3d12_dred_settings_changed(settings);
}

static void STDMETHODCALLTYPE
d3d12_dred_settings_SetPageFaultEnablement(ID3D12DeviceRemovedExtendedDataSettings2 *iface,
                                           D3D12_DRED_ENABLEMENT enablement)
{
    struct d3d12_dred_settings *settings = impl_from_ID3D12DeviceRemovedExtendedDataSettings2(iface);

    InterlockedExchange(&settings->state->page_fault, enablement);
    d3d12_dred_settings_changed(settings);
}

static void STDMETHODCALLTYPE
d3d12_dred_settings_SetWatsonDumpEnablement(ID3D12DeviceRemovedExtendedDataSettings2 *iface,
                                            D3D12_DRED_ENABLEMENT enablement)
{
    struct d3d12_dred_settings *settings = impl_from_ID3D12DeviceRemovedExtendedDataSettings2(iface);

    InterlockedExchange(&settings->state->watson_dump, enablement);
    d3d12_dred_settings_changed(settings);
}

static void STDMETHODCALLTYPE
d3d12_dred_settings_SetBreadcrumbContextEnablement(ID3D12DeviceRemovedExtendedDataSettings2 *iface,
                                                   D3D12_DRED_ENABLEMENT enablement)
{
    struct d3d12_dred_settings *settings = impl_from_ID3D12DeviceRemovedExtendedDataSettings2(iface);

    InterlockedExchange(&settings->state->breadcrumb_context, enablement);
    d3d12_dred_settings_changed(settings);
}

static void STDMETHODCALLTYPE
d3d12_dred_settings_UseMarkersOnlyAutoBreadcrumbs(ID3D12DeviceRemovedExtendedDataSettings2 *iface, BOOL markers_only)
{
    struct d3d12_dred_settings *settings = impl_from_ID3D12DeviceRemovedExtendedDataSettings2(iface);

    InterlockedExchange(&settings->state->markers_only, markers_only);
    d3d12_dred_settings_changed(settings);
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
d3d12_dred_settings_create(struct d3d12_dred_state *state, IUnknown *owner, REFIID iid, void **out)
{
    struct d3d12_dred_settings *settings;
    HRESULT hr;

    if (!(settings = HeapAlloc(GetProcessHeap(), 0, sizeof(*settings))))
        return E_OUTOFMEMORY;
    settings->ID3D12DeviceRemovedExtendedDataSettings2_iface.lpVtbl = &d3d12_dred_settings_vtbl;
    settings->refcount = 1;
    settings->state = state;
    if ((settings->owner = owner))
        IUnknown_AddRef(owner);
    hr = ID3D12DeviceRemovedExtendedDataSettings2_QueryInterface(
            &settings->ID3D12DeviceRemovedExtendedDataSettings2_iface, iid, out);
    ID3D12DeviceRemovedExtendedDataSettings2_Release(&settings->ID3D12DeviceRemovedExtendedDataSettings2_iface);
    return hr;
}

struct d3d12_sdk_configuration
{
    ID3D12SDKConfiguration1 ID3D12SDKConfiguration1_iface;
    LONG refcount;
};

static struct d3d12_sdk_configuration *impl_from_ID3D12SDKConfiguration1(ID3D12SDKConfiguration1 *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_sdk_configuration, ID3D12SDKConfiguration1_iface);
}

static HRESULT STDMETHODCALLTYPE
d3d12_sdk_configuration_QueryInterface(ID3D12SDKConfiguration1 *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_ID3D12SDKConfiguration1) || IsEqualGUID(iid, &IID_ID3D12SDKConfiguration)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12SDKConfiguration1_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d3d12_sdk_configuration_AddRef(ID3D12SDKConfiguration1 *iface)
{
    return InterlockedIncrement(&impl_from_ID3D12SDKConfiguration1(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE
d3d12_sdk_configuration_Release(ID3D12SDKConfiguration1 *iface)
{
    struct d3d12_sdk_configuration *configuration = impl_from_ID3D12SDKConfiguration1(iface);
    ULONG refcount = InterlockedDecrement(&configuration->refcount);

    if (!refcount)
        HeapFree(GetProcessHeap(), 0, configuration);
    return refcount;
}

static HRESULT STDMETHODCALLTYPE
d3d12_sdk_configuration_SetSDKVersion(ID3D12SDKConfiguration1 *iface, UINT version, const char *path)
{
    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT d3d12_device_factory_create(REFIID iid, void **out);

static BOOL d3d12_sdk_path_has_core(const char *path)
{
    char file[MAX_PATH];
    DWORD length = 0;
    char *slash;

    if (!path)
        return FALSE;
    if (!(path[0] && path[1] == ':') && path[0] != '\\' && path[0] != '/')
    {
        if (!(length = GetModuleFileNameA(NULL, file, ARRAYSIZE(file))) || length >= ARRAYSIZE(file))
            return FALSE;
        if ((slash = strrchr(file, '\\')))
            slash[1] = 0;
        length = strlen(file);
    }
    else
        file[0] = 0;
    if (length + strlen(path) + sizeof("\\D3D12Core.dll") > ARRAYSIZE(file))
        return FALSE;
    strcat(file, path);
    length = strlen(file);
    if (length && file[length - 1] != '\\' && file[length - 1] != '/')
        strcat(file, "\\");
    strcat(file, "D3D12Core.dll");
    return GetFileAttributesA(file) != INVALID_FILE_ATTRIBUTES;
}

static HRESULT STDMETHODCALLTYPE
d3d12_sdk_configuration_CreateDeviceFactory(ID3D12SDKConfiguration1 *iface, UINT version, const char *path,
        REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (version && !d3d12_sdk_path_has_core(path))
        return D3D12_ERROR_INVALID_REDIST;
    return d3d12_device_factory_create(iid, out);
}

static void STDMETHODCALLTYPE
d3d12_sdk_configuration_FreeUnusedSDKs(ID3D12SDKConfiguration1 *iface)
{
}

static const ID3D12SDKConfiguration1Vtbl d3d12_sdk_configuration_vtbl =
{
    d3d12_sdk_configuration_QueryInterface,
    d3d12_sdk_configuration_AddRef,
    d3d12_sdk_configuration_Release,
    d3d12_sdk_configuration_SetSDKVersion,
    d3d12_sdk_configuration_CreateDeviceFactory,
    d3d12_sdk_configuration_FreeUnusedSDKs,
};

static HRESULT
d3d12_sdk_configuration_create(REFIID iid, void **out)
{
    struct d3d12_sdk_configuration *configuration;
    HRESULT hr;

    if (!(configuration = HeapAlloc(GetProcessHeap(), 0, sizeof(*configuration))))
        return E_OUTOFMEMORY;
    configuration->ID3D12SDKConfiguration1_iface.lpVtbl = &d3d12_sdk_configuration_vtbl;
    configuration->refcount = 1;
    hr = ID3D12SDKConfiguration1_QueryInterface(&configuration->ID3D12SDKConfiguration1_iface, iid, out);
    ID3D12SDKConfiguration1_Release(&configuration->ID3D12SDKConfiguration1_iface);
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
        return d3d12_dred_settings_create(&d3d12_dred, NULL, iid, debug);

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

struct d3d12_device_factory
{
    ID3D12DeviceFactory ID3D12DeviceFactory_iface;
    ID3D12DeviceConfiguration ID3D12DeviceConfiguration_iface;
    LONG refcount;
    LONG flags;
    struct d3d12_dred_state dred;
};

static struct d3d12_device_factory *impl_from_ID3D12DeviceFactory(ID3D12DeviceFactory *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_device_factory, ID3D12DeviceFactory_iface);
}

static struct d3d12_device_factory *impl_from_factory_configuration(ID3D12DeviceConfiguration *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_device_factory, ID3D12DeviceConfiguration_iface);
}

static HRESULT STDMETHODCALLTYPE
d3d12_device_factory_QueryInterface(ID3D12DeviceFactory *iface, REFIID iid, void **out)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceFactory(iface);

    if (IsEqualGUID(iid, &IID_ID3D12DeviceFactory) || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID3D12DeviceFactory_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    if (IsEqualGUID(iid, &IID_ID3D12DeviceConfiguration))
    {
        ID3D12DeviceFactory_AddRef(iface);
        *out = &factory->ID3D12DeviceConfiguration_iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
d3d12_device_factory_AddRef(ID3D12DeviceFactory *iface)
{
    return InterlockedIncrement(&impl_from_ID3D12DeviceFactory(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE
d3d12_device_factory_Release(ID3D12DeviceFactory *iface)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceFactory(iface);
    ULONG refcount = InterlockedDecrement(&factory->refcount);

    if (!refcount)
        HeapFree(GetProcessHeap(), 0, factory);
    return refcount;
}

static HRESULT STDMETHODCALLTYPE
d3d12_device_factory_InitializeFromGlobalState(ID3D12DeviceFactory *iface)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceFactory(iface);

    InterlockedExchange(&factory->dred.auto_breadcrumbs, d3d12_dred.auto_breadcrumbs);
    InterlockedExchange(&factory->dred.page_fault, d3d12_dred.page_fault);
    InterlockedExchange(&factory->dred.watson_dump, d3d12_dred.watson_dump);
    InterlockedExchange(&factory->dred.breadcrumb_context, d3d12_dred.breadcrumb_context);
    InterlockedExchange(&factory->dred.markers_only, d3d12_dred.markers_only);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d3d12_device_factory_ApplyToGlobalState(ID3D12DeviceFactory *iface)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceFactory(iface);

    InterlockedExchange(&d3d12_dred.auto_breadcrumbs, factory->dred.auto_breadcrumbs);
    InterlockedExchange(&d3d12_dred.page_fault, factory->dred.page_fault);
    InterlockedExchange(&d3d12_dred.watson_dump, factory->dred.watson_dump);
    InterlockedExchange(&d3d12_dred.breadcrumb_context, factory->dred.breadcrumb_context);
    InterlockedExchange(&d3d12_dred.markers_only, factory->dred.markers_only);
    d3d12_dred_update();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d3d12_device_factory_SetFlags(ID3D12DeviceFactory *iface, D3D12_DEVICE_FACTORY_FLAGS flags)
{
    const UINT all = D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE
            | D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_INCOMPATIBLE_EXISTING_DEVICE
            | D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON;

    if ((flags & ~all) || ((flags & D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_INCOMPATIBLE_EXISTING_DEVICE)
            && !(flags & D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE)))
        return E_INVALIDARG;
    InterlockedExchange(&impl_from_ID3D12DeviceFactory(iface)->flags, flags);
    return S_OK;
}

static D3D12_DEVICE_FACTORY_FLAGS STDMETHODCALLTYPE
d3d12_device_factory_GetFlags(ID3D12DeviceFactory *iface)
{
    return impl_from_ID3D12DeviceFactory(iface)->flags;
}

static HRESULT STDMETHODCALLTYPE
d3d12_device_factory_GetConfigurationInterface(ID3D12DeviceFactory *iface, REFCLSID clsid, REFIID iid, void **out)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceFactory(iface);

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(clsid, &CLSID_D3D12DeviceRemovedExtendedData))
        return d3d12_dred_settings_create(&factory->dred, (IUnknown *)iface, iid, out);
    if (IsEqualGUID(clsid, &CLSID_D3D12Debug))
        return DXGI_ERROR_SDK_COMPONENT_MISSING;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE
d3d12_device_factory_EnableExperimentalFeatures(ID3D12DeviceFactory *iface, UINT count, const IID *iids,
        void *configurations, UINT *sizes)
{
    return D3D12EnableExperimentalFeatures(count, iids, configurations, sizes);
}

static HRESULT STDMETHODCALLTYPE
d3d12_device_factory_CreateDevice(ID3D12DeviceFactory *iface, IUnknown *adapter, D3D_FEATURE_LEVEL feature_level,
        REFIID iid, void **device)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceFactory(iface);
    struct vkd3d_device_configuration_info configuration;

    memset(&configuration, 0, sizeof(configuration));
    configuration.type = VKD3D_STRUCTURE_TYPE_DEVICE_CONFIGURATION_INFO;
    configuration.dred_auto_breadcrumbs = factory->dred.auto_breadcrumbs;
    configuration.dred_page_fault = factory->dred.page_fault;
    configuration.configuration_flags = d3d12_dred_flags(&factory->dred);
    return d3d12_create_configured_device(adapter, feature_level, &configuration, iid, device);
}

static const ID3D12DeviceFactoryVtbl d3d12_device_factory_vtbl =
{
    d3d12_device_factory_QueryInterface,
    d3d12_device_factory_AddRef,
    d3d12_device_factory_Release,
    d3d12_device_factory_InitializeFromGlobalState,
    d3d12_device_factory_ApplyToGlobalState,
    d3d12_device_factory_SetFlags,
    d3d12_device_factory_GetFlags,
    d3d12_device_factory_GetConfigurationInterface,
    d3d12_device_factory_EnableExperimentalFeatures,
    d3d12_device_factory_CreateDevice,
};

static HRESULT STDMETHODCALLTYPE
d3d12_factory_configuration_QueryInterface(ID3D12DeviceConfiguration *iface, REFIID iid, void **out)
{
    return d3d12_device_factory_QueryInterface(&impl_from_factory_configuration(iface)->ID3D12DeviceFactory_iface,
            iid, out);
}

static ULONG STDMETHODCALLTYPE
d3d12_factory_configuration_AddRef(ID3D12DeviceConfiguration *iface)
{
    return d3d12_device_factory_AddRef(&impl_from_factory_configuration(iface)->ID3D12DeviceFactory_iface);
}

static ULONG STDMETHODCALLTYPE
d3d12_factory_configuration_Release(ID3D12DeviceConfiguration *iface)
{
    return d3d12_device_factory_Release(&impl_from_factory_configuration(iface)->ID3D12DeviceFactory_iface);
}

static D3D12_DEVICE_CONFIGURATION_DESC * STDMETHODCALLTYPE
d3d12_factory_configuration_GetDesc(ID3D12DeviceConfiguration *iface, D3D12_DEVICE_CONFIGURATION_DESC *desc)
{
    struct d3d12_device_factory *factory = impl_from_factory_configuration(iface);

    desc->Flags = d3d12_dred_flags(&factory->dred);
    desc->GpuBasedValidationFlags = D3D12_GPU_BASED_VALIDATION_FLAGS_NONE;
    desc->SDKVersion = 612;
    desc->NumEnabledExperimentalFeatures = 0;
    return desc;
}

static HRESULT STDMETHODCALLTYPE
d3d12_factory_configuration_GetEnabledExperimentalFeatures(ID3D12DeviceConfiguration *iface, GUID *guids,
        UINT count)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE
d3d12_factory_configuration_SerializeVersionedRootSignature(ID3D12DeviceConfiguration *iface,
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, ID3DBlob **blob, ID3DBlob **error)
{
    return D3D12SerializeVersionedRootSignature(desc, blob, error);
}

static HRESULT STDMETHODCALLTYPE
d3d12_factory_configuration_CreateVersionedRootSignatureDeserializer(ID3D12DeviceConfiguration *iface,
        const void *data, SIZE_T size, REFIID iid, void **deserializer)
{
    return D3D12CreateVersionedRootSignatureDeserializer(data, size, iid, deserializer);
}

static const ID3D12DeviceConfigurationVtbl d3d12_factory_configuration_vtbl =
{
    d3d12_factory_configuration_QueryInterface,
    d3d12_factory_configuration_AddRef,
    d3d12_factory_configuration_Release,
    d3d12_factory_configuration_GetDesc,
    d3d12_factory_configuration_GetEnabledExperimentalFeatures,
    d3d12_factory_configuration_SerializeVersionedRootSignature,
    d3d12_factory_configuration_CreateVersionedRootSignatureDeserializer,
};

static HRESULT d3d12_device_factory_create(REFIID iid, void **out)
{
    struct d3d12_device_factory *factory;
    HRESULT hr;

    if (!(factory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*factory))))
        return E_OUTOFMEMORY;
    factory->ID3D12DeviceFactory_iface.lpVtbl = &d3d12_device_factory_vtbl;
    factory->ID3D12DeviceConfiguration_iface.lpVtbl = &d3d12_factory_configuration_vtbl;
    factory->refcount = 1;
    hr = ID3D12DeviceFactory_QueryInterface(&factory->ID3D12DeviceFactory_iface, iid, out);
    ID3D12DeviceFactory_Release(&factory->ID3D12DeviceFactory_iface);
    return hr;
}

HRESULT WINAPI
D3D12GetInterface(REFCLSID clsid, REFIID iid, void **debug)
{
    if (IsEqualGUID(clsid, &CLSID_D3D12Debug))
        return D3D12GetDebugInterface(iid, debug);
    if (debug && IsEqualGUID(clsid, &CLSID_D3D12DeviceRemovedExtendedData))
        return d3d12_dred_settings_create(&d3d12_dred, NULL, iid, debug);
    if (debug && IsEqualGUID(clsid, &CLSID_D3D12SDKConfiguration))
        return d3d12_sdk_configuration_create(iid, debug);
    if (debug && IsEqualGUID(clsid, &CLSID_D3D12DeviceFactory))
        return d3d12_device_factory_create(iid, debug);

    return E_NOINTERFACE;
}
