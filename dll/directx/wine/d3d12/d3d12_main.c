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
    application_info.api_version = VKD3D_API_VERSION_1_0;

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

HRESULT WINAPI
D3D12GetDebugInterface(REFIID iid, void **debug)
{
    UNREFERENCED_PARAMETER(iid);
    UNREFERENCED_PARAMETER(debug);

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
    if (debug)
        *debug = NULL;

    if (IsEqualGUID(clsid, &CLSID_D3D12Debug))
        return D3D12GetDebugInterface(iid, debug);

    return E_NOINTERFACE;
}
