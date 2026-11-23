/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AnalyticsInfo activation and the device family version
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "private.h"
#include <assert.h>
#include <winreg.h>

WINE_DEFAULT_DEBUG_CHANNEL(systemid);

NTSTATUS NTAPI RtlGetVersion(RTL_OSVERSIONINFOW *info);

struct analytics_info
{
    IActivationFactory IActivationFactory_iface;
    IAnalyticsInfoStatics IAnalyticsInfoStatics_iface;
    LONG ref;
};

struct version_info
{
    IAnalyticsVersionInfo IAnalyticsVersionInfo_iface;
    LONG ref;
    UINT64 version;
};

static struct analytics_info *impl_from_factory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct analytics_info, IActivationFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct analytics_info *impl = impl_from_factory(iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IActivationFactory) || IsEqualGUID(iid, &IID_IAgileObject))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_IAnalyticsInfoStatics))
        *out = &impl->IAnalyticsInfoStatics_iface;
    else
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    return InterlockedIncrement(&impl_from_factory(iface)->ref);
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    ULONG ref = InterlockedDecrement(&impl_from_factory(iface)->ref);
    assert(ref != 0);
    return ref;
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = CoTaskMemAlloc(sizeof(**iids));
    if (!*iids) return E_OUTOFMEMORY;
    **iids = IID_IAnalyticsInfoStatics;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name)
{
    const WCHAR *class_name = RuntimeClass_Windows_System_Profile_AnalyticsInfo;
    if (!name) return E_POINTER;
    return WindowsCreateString(class_name, wcslen(class_name), name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level)
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    return E_NOTIMPL;
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release,
    factory_GetIids, factory_GetRuntimeClassName, factory_GetTrustLevel,
    factory_ActivateInstance
};

static struct version_info *impl_from_version(IAnalyticsVersionInfo *iface)
{
    return CONTAINING_RECORD(iface, struct version_info, IAnalyticsVersionInfo_iface);
}

static HRESULT WINAPI version_QueryInterface(IAnalyticsVersionInfo *iface, REFIID iid, void **out)
{
    struct version_info *impl = impl_from_version(iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IAnalyticsVersionInfo))
        *out = &impl->IAnalyticsVersionInfo_iface;
    else
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI version_AddRef(IAnalyticsVersionInfo *iface)
{
    ULONG ref = InterlockedIncrement(&impl_from_version(iface)->ref);
    assert(ref > 1);
    return ref;
}

static ULONG WINAPI version_Release(IAnalyticsVersionInfo *iface)
{
    struct version_info *impl = impl_from_version(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    assert(ref != (ULONG)-1);
    if (!ref) HeapFree(GetProcessHeap(), 0, impl);
    return ref;
}

static HRESULT WINAPI version_GetIids(IAnalyticsVersionInfo *iface, ULONG *count, IID **iids)
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = CoTaskMemAlloc(sizeof(**iids));
    if (!*iids) return E_OUTOFMEMORY;
    **iids = IID_IAnalyticsVersionInfo;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI version_GetRuntimeClassName(IAnalyticsVersionInfo *iface, HSTRING *name)
{
    const WCHAR *class_name = RuntimeClass_Windows_System_Profile_AnalyticsVersionInfo;
    if (!name) return E_POINTER;
    return WindowsCreateString(class_name, wcslen(class_name), name);
}

static HRESULT WINAPI version_GetTrustLevel(IAnalyticsVersionInfo *iface, TrustLevel *level)
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI version_get_DeviceFamily(IAnalyticsVersionInfo *iface, HSTRING *value)
{
    static const WCHAR family[] = L"Windows.Desktop";
    if (!value) return E_POINTER;
    return WindowsCreateString(family, ARRAY_SIZE(family) - 1, value);
}

static HRESULT WINAPI version_get_DeviceFamilyVersion(IAnalyticsVersionInfo *iface, HSTRING *value)
{
    WCHAR buffer[21];
    if (!value) return E_POINTER;
    swprintf(buffer, ARRAY_SIZE(buffer), L"%I64u", impl_from_version(iface)->version);
    return WindowsCreateString(buffer, wcslen(buffer), value);
}

static const IAnalyticsVersionInfoVtbl version_vtbl =
{
    version_QueryInterface, version_AddRef, version_Release,
    version_GetIids, version_GetRuntimeClassName, version_GetTrustLevel,
    version_get_DeviceFamily, version_get_DeviceFamilyVersion
};

DEFINE_IINSPECTABLE_(statics, IAnalyticsInfoStatics, struct analytics_info,
                     impl_from_statics, IAnalyticsInfoStatics_iface,
                     &impl->IActivationFactory_iface)

static UINT64 device_family_version(void)
{
    RTL_OSVERSIONINFOW info = { sizeof(info) };
    DWORD revision = 0, size = sizeof(revision);
    NTSTATUS status = RtlGetVersion(&info);

    assert(status >= 0);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", L"UBR",
                     RRF_RT_REG_DWORD, NULL, &revision, &size) != ERROR_SUCCESS)
        revision = 0;
    return ((UINT64)(info.dwMajorVersion & 0xffff) << 48) | ((UINT64)(info.dwMinorVersion & 0xffff) << 32) |
           ((UINT64)(info.dwBuildNumber & 0xffff) << 16) | (revision & 0xffff);
}

static HRESULT WINAPI statics_get_VersionInfo(IAnalyticsInfoStatics *iface, IAnalyticsVersionInfo **value)
{
    struct version_info *impl;

    if (!value) return E_POINTER;
    *value = NULL;
    impl = HeapAlloc(GetProcessHeap(), 0, sizeof(*impl));
    if (!impl) return E_OUTOFMEMORY;
    impl->IAnalyticsVersionInfo_iface.lpVtbl = &version_vtbl;
    impl->ref = 1;
    impl->version = device_family_version();
    *value = &impl->IAnalyticsVersionInfo_iface;
    return S_OK;
}

static HRESULT WINAPI statics_get_DeviceForm(IAnalyticsInfoStatics *iface, HSTRING *value)
{
    if (!value) return E_POINTER;
    *value = NULL;
    /* TODO: DeviceForm needs the form factor Windows 11 reports */
    FIXME("iface %p, value %p stub!\n", iface, value);
    return E_NOTIMPL;
}

static const IAnalyticsInfoStaticsVtbl statics_vtbl =
{
    statics_QueryInterface, statics_AddRef, statics_Release,
    statics_GetIids, statics_GetRuntimeClassName, statics_GetTrustLevel,
    statics_get_VersionInfo, statics_get_DeviceForm
};

static struct analytics_info analytics = {{&factory_vtbl}, {&statics_vtbl}, 1};
IActivationFactory *analytics_info_factory = &analytics.IActivationFactory_iface;
