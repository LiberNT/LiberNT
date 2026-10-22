/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

#define STORE_SERVICE_UNAVAILABLE HRESULT_FROM_WIN32(ERROR_SERVICE_DOES_NOT_EXIST)
#define STORE_AUTODOWNLOAD_OFF 2
#define STORE_AUTODOWNLOAD_ON 4

static const WCHAR store_policy_key[] = L"Software\\Policies\\Microsoft\\WindowsStore";
static const WCHAR store_update_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\WindowsStore\\WindowsUpdate";

struct manager
{
    IAppInstallManager IAppInstallManager_iface;
    IAppInstallManager2 IAppInstallManager2_iface;
    IAppInstallManager3 IAppInstallManager3_iface;
    IAppInstallManager4 IAppInstallManager4_iface;
    IAppInstallManager5 IAppInstallManager5_iface;
    IAppInstallManager6 IAppInstallManager6_iface;
    IAppInstallManager7 IAppInstallManager7_iface;
    LONG ref;
    SRWLOCK lock;
    HSTRING acquisition_identity;
    struct event_list item_completed;
    struct event_list item_status_changed;
};

static DWORD read_dword(HKEY root, const WCHAR *key, const WCHAR *value, DWORD fallback)
{
    DWORD data, size = sizeof(data);

    if (RegGetValueW(root, key, value, RRF_RT_REG_DWORD, NULL, &data, &size))
        return fallback;
    return data;
}

static BOOL store_blocked_by_policy(void)
{
    return read_dword(HKEY_LOCAL_MACHINE, store_policy_key, L"RemoveWindowsStore", 0) == 1 ||
           read_dword(HKEY_CURRENT_USER, store_policy_key, L"RemoveWindowsStore", 0) == 1 ||
           read_dword(HKEY_LOCAL_MACHINE, store_policy_key, L"DisableStoreApps", 0) == 1;
}

static HRESULT failed_item(IAsyncOperation_AppInstallItem **operation)
{
    return async_object_create(&IID_IAsyncOperation_AppInstallItem, STORE_SERVICE_UNAVAILABLE, (void **)operation);
}

static HRESULT failed_items(IAsyncOperation_IVectorView_AppInstallItem **operation)
{
    return async_object_create(&IID_IAsyncOperation_IVectorView_AppInstallItem, STORE_SERVICE_UNAVAILABLE,
                               (void **)operation);
}

static HRESULT failed_boolean(IAsyncOperation_boolean **operation)
{
    return async_boolean_create(STORE_SERVICE_UNAVAILABLE, FALSE, operation);
}

static HRESULT failed_entitlement(IAsyncOperation_GetEntitlementResult **operation)
{
    return async_object_create(&IID_IAsyncOperation_GetEntitlementResult, STORE_SERVICE_UNAVAILABLE,
                               (void **)operation);
}

static inline struct manager *impl_from_IAppInstallManager(IAppInstallManager *iface)
{
    return CONTAINING_RECORD(iface, struct manager, IAppInstallManager_iface);
}

static HRESULT WINAPI manager_QueryInterface(IAppInstallManager *iface, REFIID iid, void **out)
{
    struct manager *impl = impl_from_IAppInstallManager(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IAppInstallManager))
        *out = &impl->IAppInstallManager_iface;
    else if (IsEqualGUID(iid, &IID_IAppInstallManager2))
        *out = &impl->IAppInstallManager2_iface;
    else if (IsEqualGUID(iid, &IID_IAppInstallManager3))
        *out = &impl->IAppInstallManager3_iface;
    else if (IsEqualGUID(iid, &IID_IAppInstallManager4))
        *out = &impl->IAppInstallManager4_iface;
    else if (IsEqualGUID(iid, &IID_IAppInstallManager5))
        *out = &impl->IAppInstallManager5_iface;
    else if (IsEqualGUID(iid, &IID_IAppInstallManager6))
        *out = &impl->IAppInstallManager6_iface;
    else if (IsEqualGUID(iid, &IID_IAppInstallManager7))
        *out = &impl->IAppInstallManager7_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI manager_AddRef(IAppInstallManager *iface)
{
    return InterlockedIncrement(&impl_from_IAppInstallManager(iface)->ref);
}

static ULONG WINAPI manager_Release(IAppInstallManager *iface)
{
    struct manager *impl = impl_from_IAppInstallManager(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        event_list_clear(&impl->item_completed);
        event_list_clear(&impl->item_status_changed);
        WindowsDeleteString(impl->acquisition_identity);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI manager_GetIids(IAppInstallManager *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IAppInstallManager, count, iids);
}

static HRESULT WINAPI manager_GetRuntimeClassName(IAppInstallManager *iface, HSTRING *class_name)
{
    return class_name_string(RuntimeClass_Windows_ApplicationModel_Store_Preview_InstallControl_AppInstallManager,
                             class_name);
}

static HRESULT WINAPI manager_GetTrustLevel(IAppInstallManager *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI manager_get_AppInstallItems(IAppInstallManager *iface, IVectorView_AppInstallItem **value)
{
    return empty_install_items(value);
}

static HRESULT WINAPI manager_Cancel(IAppInstallManager *iface, HSTRING product_id)
{
    return E_BOUNDS;
}

static HRESULT WINAPI manager_Pause(IAppInstallManager *iface, HSTRING product_id)
{
    return E_BOUNDS;
}

static HRESULT WINAPI manager_Restart(IAppInstallManager *iface, HSTRING product_id)
{
    return E_BOUNDS;
}

static HRESULT WINAPI manager_add_ItemCompleted(IAppInstallManager *iface,
        ITypedEventHandler_AppInstallManager_AppInstallManagerItemEventArgs *handler, EventRegistrationToken *token)
{
    return event_list_add(&impl_from_IAppInstallManager(iface)->item_completed, handler,
                          &IID_ITypedEventHandler_AppInstallManager_AppInstallManagerItemEventArgs, token);
}

static HRESULT WINAPI manager_remove_ItemCompleted(IAppInstallManager *iface, EventRegistrationToken token)
{
    return event_list_remove(&impl_from_IAppInstallManager(iface)->item_completed, token);
}

static HRESULT WINAPI manager_add_ItemStatusChanged(IAppInstallManager *iface,
        ITypedEventHandler_AppInstallManager_AppInstallManagerItemEventArgs *handler, EventRegistrationToken *token)
{
    return event_list_add(&impl_from_IAppInstallManager(iface)->item_status_changed, handler,
                          &IID_ITypedEventHandler_AppInstallManager_AppInstallManagerItemEventArgs, token);
}

static HRESULT WINAPI manager_remove_ItemStatusChanged(IAppInstallManager *iface, EventRegistrationToken token)
{
    return event_list_remove(&impl_from_IAppInstallManager(iface)->item_status_changed, token);
}

static HRESULT WINAPI manager_get_AutoUpdateSetting(IAppInstallManager *iface, AutoUpdateSetting *value)
{
    DWORD policy;

    if (!value)
        return E_POINTER;
    policy = read_dword(HKEY_LOCAL_MACHINE, store_policy_key, L"AutoDownload", 0);
    if (policy == STORE_AUTODOWNLOAD_OFF)
        *value = AutoUpdateSetting_DisabledByPolicy;
    else if (policy == STORE_AUTODOWNLOAD_ON)
        *value = AutoUpdateSetting_EnabledByPolicy;
    else if (read_dword(HKEY_LOCAL_MACHINE, store_update_key, L"AutoDownload", STORE_AUTODOWNLOAD_ON) ==
             STORE_AUTODOWNLOAD_OFF)
        *value = AutoUpdateSetting_Disabled;
    else
        *value = AutoUpdateSetting_Enabled;
    return S_OK;
}

static HRESULT WINAPI manager_put_AutoUpdateSetting(IAppInstallManager *iface, AutoUpdateSetting value)
{
    DWORD data;
    LSTATUS status;

    if (value != AutoUpdateSetting_Enabled && value != AutoUpdateSetting_Disabled)
        return E_INVALIDARG;
    data = value == AutoUpdateSetting_Enabled ? STORE_AUTODOWNLOAD_ON : STORE_AUTODOWNLOAD_OFF;
    status = RegSetKeyValueW(HKEY_LOCAL_MACHINE, store_update_key, L"AutoDownload", REG_DWORD, &data, sizeof(data));
    return HRESULT_FROM_WIN32(status);
}

static HRESULT WINAPI manager_get_AcquisitionIdentity(IAppInstallManager *iface, HSTRING *value)
{
    struct manager *impl = impl_from_IAppInstallManager(iface);
    HRESULT hr;

    if (!value)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    hr = WindowsDuplicateString(impl->acquisition_identity, value);
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT WINAPI manager_put_AcquisitionIdentity(IAppInstallManager *iface, HSTRING value)
{
    struct manager *impl = impl_from_IAppInstallManager(iface);
    HSTRING copy, previous;
    HRESULT hr;

    if (FAILED(hr = WindowsDuplicateString(value, &copy)))
        return hr;
    AcquireSRWLockExclusive(&impl->lock);
    previous = impl->acquisition_identity;
    impl->acquisition_identity = copy;
    ReleaseSRWLockExclusive(&impl->lock);
    WindowsDeleteString(previous);
    return S_OK;
}

static HRESULT WINAPI manager_GetIsApplicableAsync(IAppInstallManager *iface, HSTRING product_id, HSTRING sku_id,
                                                   IAsyncOperation_boolean **operation)
{
    return failed_boolean(operation);
}

static HRESULT WINAPI manager_StartAppInstallAsync(IAppInstallManager *iface, HSTRING product_id, HSTRING sku_id,
                                                   boolean repair, boolean force_non_removable_storage,
                                                   IAsyncOperation_AppInstallItem **operation)
{
    return failed_item(operation);
}

static HRESULT WINAPI manager_UpdateAppByPackageFamilyNameAsync(IAppInstallManager *iface, HSTRING family_name,
                                                                IAsyncOperation_AppInstallItem **operation)
{
    return failed_item(operation);
}

static HRESULT WINAPI manager_SearchForUpdatesAsync(IAppInstallManager *iface, HSTRING product_id, HSTRING sku_id,
                                                    IAsyncOperation_AppInstallItem **operation)
{
    return failed_item(operation);
}

static HRESULT WINAPI manager_SearchForAllUpdatesAsync(IAppInstallManager *iface,
                                                       IAsyncOperation_IVectorView_AppInstallItem **operation)
{
    return failed_items(operation);
}

static HRESULT WINAPI manager_IsStoreBlockedByPolicyAsync(IAppInstallManager *iface, HSTRING client_name,
                                                          HSTRING client_publisher,
                                                          IAsyncOperation_boolean **operation)
{
    if (!operation)
        return E_POINTER;
    *operation = NULL;
    if (!WindowsGetStringLen(client_name) || !WindowsGetStringLen(client_publisher))
        return E_INVALIDARG;
    return async_boolean_create(S_OK, store_blocked_by_policy(), operation);
}

static HRESULT WINAPI manager_GetIsAppAllowedToInstallAsync(IAppInstallManager *iface, HSTRING product_id,
                                                            IAsyncOperation_boolean **operation)
{
    return failed_boolean(operation);
}

static const IAppInstallManagerVtbl manager_vtbl =
{
    manager_QueryInterface,
    manager_AddRef,
    manager_Release,
    manager_GetIids,
    manager_GetRuntimeClassName,
    manager_GetTrustLevel,
    manager_get_AppInstallItems,
    manager_Cancel,
    manager_Pause,
    manager_Restart,
    manager_add_ItemCompleted,
    manager_remove_ItemCompleted,
    manager_add_ItemStatusChanged,
    manager_remove_ItemStatusChanged,
    manager_get_AutoUpdateSetting,
    manager_put_AutoUpdateSetting,
    manager_get_AcquisitionIdentity,
    manager_put_AcquisitionIdentity,
    manager_GetIsApplicableAsync,
    manager_StartAppInstallAsync,
    manager_UpdateAppByPackageFamilyNameAsync,
    manager_SearchForUpdatesAsync,
    manager_SearchForAllUpdatesAsync,
    manager_IsStoreBlockedByPolicyAsync,
    manager_GetIsAppAllowedToInstallAsync,
};

#define DEFINE_MANAGER_FORWARDERS(prefix, iface_type, member)                                          \
    static inline struct manager *impl_from_##iface_type(iface_type *iface)                            \
    {                                                                                                  \
        return CONTAINING_RECORD(iface, struct manager, member);                                       \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_QueryInterface(iface_type *iface, REFIID iid, void **out)           \
    {                                                                                                  \
        return manager_QueryInterface(&impl_from_##iface_type(iface)->IAppInstallManager_iface, iid, out); \
    }                                                                                                  \
    static ULONG WINAPI prefix##_AddRef(iface_type *iface)                                             \
    {                                                                                                  \
        return manager_AddRef(&impl_from_##iface_type(iface)->IAppInstallManager_iface);               \
    }                                                                                                  \
    static ULONG WINAPI prefix##_Release(iface_type *iface)                                            \
    {                                                                                                  \
        return manager_Release(&impl_from_##iface_type(iface)->IAppInstallManager_iface);              \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_GetIids(iface_type *iface, ULONG *count, IID **iids)                \
    {                                                                                                  \
        return get_iids(&IID_##iface_type, count, iids);                                               \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_GetRuntimeClassName(iface_type *iface, HSTRING *class_name)         \
    {                                                                                                  \
        return manager_GetRuntimeClassName(NULL, class_name);                                          \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_GetTrustLevel(iface_type *iface, TrustLevel *level)                 \
    {                                                                                                  \
        return trust_level(level);                                                                     \
    }

DEFINE_MANAGER_FORWARDERS(manager2, IAppInstallManager2, IAppInstallManager2_iface)

static HRESULT WINAPI manager2_StartAppInstallWithTelemetryAsync(IAppInstallManager2 *iface, HSTRING product_id,
        HSTRING sku_id, boolean repair, boolean force_non_removable_storage, HSTRING catalog_id, HSTRING bundle_id,
        HSTRING correlation_vector, IAsyncOperation_AppInstallItem **operation)
{
    return failed_item(operation);
}

static HRESULT WINAPI manager2_UpdateAppByPackageFamilyNameWithTelemetryAsync(IAppInstallManager2 *iface,
        HSTRING family_name, HSTRING correlation_vector, IAsyncOperation_AppInstallItem **operation)
{
    return failed_item(operation);
}

static HRESULT WINAPI manager2_SearchForUpdatesWithTelemetryAsync(IAppInstallManager2 *iface, HSTRING product_id,
        HSTRING sku_id, HSTRING catalog_id, HSTRING correlation_vector, IAsyncOperation_AppInstallItem **operation)
{
    return failed_item(operation);
}

static HRESULT WINAPI manager2_SearchForAllUpdatesWithTelemetryAsync(IAppInstallManager2 *iface,
        HSTRING correlation_vector, IAsyncOperation_IVectorView_AppInstallItem **operation)
{
    return failed_items(operation);
}

static HRESULT WINAPI manager2_GetIsAppAllowedToInstallWithTelemetryAsync(IAppInstallManager2 *iface,
        HSTRING product_id, HSTRING sku_id, HSTRING catalog_id, HSTRING correlation_vector,
        IAsyncOperation_boolean **operation)
{
    return failed_boolean(operation);
}

static HRESULT WINAPI manager2_CancelWithTelemetry(IAppInstallManager2 *iface, HSTRING product_id,
                                                   HSTRING correlation_vector)
{
    return E_BOUNDS;
}

static HRESULT WINAPI manager2_PauseWithTelemetry(IAppInstallManager2 *iface, HSTRING product_id,
                                                  HSTRING correlation_vector)
{
    return E_BOUNDS;
}

static HRESULT WINAPI manager2_RestartWithTelemetry(IAppInstallManager2 *iface, HSTRING product_id,
                                                    HSTRING correlation_vector)
{
    return E_BOUNDS;
}

static const IAppInstallManager2Vtbl manager2_vtbl =
{
    manager2_QueryInterface,
    manager2_AddRef,
    manager2_Release,
    manager2_GetIids,
    manager2_GetRuntimeClassName,
    manager2_GetTrustLevel,
    manager2_StartAppInstallWithTelemetryAsync,
    manager2_UpdateAppByPackageFamilyNameWithTelemetryAsync,
    manager2_SearchForUpdatesWithTelemetryAsync,
    manager2_SearchForAllUpdatesWithTelemetryAsync,
    manager2_GetIsAppAllowedToInstallWithTelemetryAsync,
    manager2_CancelWithTelemetry,
    manager2_PauseWithTelemetry,
    manager2_RestartWithTelemetry,
};

DEFINE_MANAGER_FORWARDERS(manager3, IAppInstallManager3, IAppInstallManager3_iface)

static HRESULT WINAPI manager3_StartProductInstallAsync(IAppInstallManager3 *iface, HSTRING product_id,
        HSTRING catalog_id, HSTRING flight_id, HSTRING client_id, boolean repair, boolean force_non_removable_storage,
        HSTRING correlation_vector, IPackageVolume *target_volume, IAsyncOperation_IVectorView_AppInstallItem **operation)
{
    return failed_items(operation);
}

static HRESULT WINAPI manager3_StartProductInstallForUserAsync(IAppInstallManager3 *iface, IUser *user,
        HSTRING product_id, HSTRING catalog_id, HSTRING flight_id, HSTRING client_id, boolean repair,
        boolean force_non_removable_storage, HSTRING correlation_vector, IPackageVolume *target_volume,
        IAsyncOperation_IVectorView_AppInstallItem **operation)
{
    return failed_items(operation);
}

static HRESULT WINAPI manager3_UpdateAppByPackageFamilyNameForUserAsync(IAppInstallManager3 *iface, IUser *user,
        HSTRING family_name, HSTRING correlation_vector, IAsyncOperation_AppInstallItem **operation)
{
    return failed_item(operation);
}

static HRESULT WINAPI manager3_SearchForUpdatesForUserAsync(IAppInstallManager3 *iface, IUser *user,
        HSTRING product_id, HSTRING sku_id, HSTRING catalog_id, HSTRING correlation_vector,
        IAsyncOperation_AppInstallItem **operation)
{
    return failed_item(operation);
}

static HRESULT WINAPI manager3_SearchForAllUpdatesForUserAsync(IAppInstallManager3 *iface, IUser *user,
        HSTRING correlation_vector, IAsyncOperation_IVectorView_AppInstallItem **operation)
{
    return failed_items(operation);
}

static HRESULT WINAPI manager3_GetIsAppAllowedToInstallForUserAsync(IAppInstallManager3 *iface, IUser *user,
        HSTRING product_id, HSTRING sku_id, HSTRING catalog_id, HSTRING correlation_vector,
        IAsyncOperation_boolean **operation)
{
    return failed_boolean(operation);
}

static HRESULT WINAPI manager3_GetIsApplicableForUserAsync(IAppInstallManager3 *iface, IUser *user,
        HSTRING product_id, HSTRING sku_id, IAsyncOperation_boolean **operation)
{
    return failed_boolean(operation);
}

static HRESULT WINAPI manager3_MoveToFrontOfDownloadQueue(IAppInstallManager3 *iface, HSTRING product_id,
                                                          HSTRING correlation_vector)
{
    return E_BOUNDS;
}

static const IAppInstallManager3Vtbl manager3_vtbl =
{
    manager3_QueryInterface,
    manager3_AddRef,
    manager3_Release,
    manager3_GetIids,
    manager3_GetRuntimeClassName,
    manager3_GetTrustLevel,
    manager3_StartProductInstallAsync,
    manager3_StartProductInstallForUserAsync,
    manager3_UpdateAppByPackageFamilyNameForUserAsync,
    manager3_SearchForUpdatesForUserAsync,
    manager3_SearchForAllUpdatesForUserAsync,
    manager3_GetIsAppAllowedToInstallForUserAsync,
    manager3_GetIsApplicableForUserAsync,
    manager3_MoveToFrontOfDownloadQueue,
};

DEFINE_MANAGER_FORWARDERS(manager4, IAppInstallManager4, IAppInstallManager4_iface)

static HRESULT WINAPI manager4_GetFreeUserEntitlementAsync(IAppInstallManager4 *iface, HSTRING store_id,
        HSTRING campaign_id, HSTRING correlation_vector, IAsyncOperation_GetEntitlementResult **operation)
{
    return failed_entitlement(operation);
}

static HRESULT WINAPI manager4_GetFreeUserEntitlementForUserAsync(IAppInstallManager4 *iface, IUser *user,
        HSTRING store_id, HSTRING campaign_id, HSTRING correlation_vector,
        IAsyncOperation_GetEntitlementResult **operation)
{
    return failed_entitlement(operation);
}

static HRESULT WINAPI manager4_GetFreeDeviceEntitlementAsync(IAppInstallManager4 *iface, HSTRING store_id,
        HSTRING campaign_id, HSTRING correlation_vector, IAsyncOperation_GetEntitlementResult **operation)
{
    return failed_entitlement(operation);
}

static const IAppInstallManager4Vtbl manager4_vtbl =
{
    manager4_QueryInterface,
    manager4_AddRef,
    manager4_Release,
    manager4_GetIids,
    manager4_GetRuntimeClassName,
    manager4_GetTrustLevel,
    manager4_GetFreeUserEntitlementAsync,
    manager4_GetFreeUserEntitlementForUserAsync,
    manager4_GetFreeDeviceEntitlementAsync,
};

DEFINE_MANAGER_FORWARDERS(manager5, IAppInstallManager5, IAppInstallManager5_iface)

static HRESULT WINAPI manager5_get_AppInstallItemsWithGroupSupport(IAppInstallManager5 *iface,
                                                                   IVectorView_AppInstallItem **value)
{
    return empty_install_items(value);
}

static const IAppInstallManager5Vtbl manager5_vtbl =
{
    manager5_QueryInterface,
    manager5_AddRef,
    manager5_Release,
    manager5_GetIids,
    manager5_GetRuntimeClassName,
    manager5_GetTrustLevel,
    manager5_get_AppInstallItemsWithGroupSupport,
};

DEFINE_MANAGER_FORWARDERS(manager6, IAppInstallManager6, IAppInstallManager6_iface)

static HRESULT WINAPI manager6_SearchForAllUpdatesWithUpdateOptionsAsync(IAppInstallManager6 *iface,
        HSTRING correlation_vector, HSTRING client_id, IAppUpdateOptions *options,
        IAsyncOperation_IVectorView_AppInstallItem **operation)
{
    return failed_items(operation);
}

static HRESULT WINAPI manager6_SearchForAllUpdatesWithUpdateOptionsForUserAsync(IAppInstallManager6 *iface,
        IUser *user, HSTRING correlation_vector, HSTRING client_id, IAppUpdateOptions *options,
        IAsyncOperation_IVectorView_AppInstallItem **operation)
{
    return failed_items(operation);
}

static HRESULT WINAPI manager6_SearchForUpdatesWithUpdateOptionsAsync(IAppInstallManager6 *iface, HSTRING product_id,
        HSTRING sku_id, HSTRING correlation_vector, HSTRING client_id, IAppUpdateOptions *options,
        IAsyncOperation_AppInstallItem **operation)
{
    return failed_item(operation);
}

static HRESULT WINAPI manager6_SearchForUpdatesWithUpdateOptionsForUserAsync(IAppInstallManager6 *iface, IUser *user,
        HSTRING product_id, HSTRING sku_id, HSTRING correlation_vector, HSTRING client_id, IAppUpdateOptions *options,
        IAsyncOperation_AppInstallItem **operation)
{
    return failed_item(operation);
}

static HRESULT WINAPI manager6_StartProductInstallWithOptionsAsync(IAppInstallManager6 *iface, HSTRING product_id,
        HSTRING flight_id, HSTRING client_id, HSTRING correlation_vector, IAppInstallOptions *options,
        IAsyncOperation_IVectorView_AppInstallItem **operation)
{
    return failed_items(operation);
}

static HRESULT WINAPI manager6_StartProductInstallWithOptionsForUserAsync(IAppInstallManager6 *iface, IUser *user,
        HSTRING product_id, HSTRING flight_id, HSTRING client_id, HSTRING correlation_vector,
        IAppInstallOptions *options, IAsyncOperation_IVectorView_AppInstallItem **operation)
{
    return failed_items(operation);
}

static HRESULT WINAPI manager6_GetIsPackageIdentityAllowedToInstallAsync(IAppInstallManager6 *iface,
        HSTRING correlation_vector, HSTRING package_identity_name, HSTRING publisher_certificate_name,
        IAsyncOperation_boolean **operation)
{
    return failed_boolean(operation);
}

static HRESULT WINAPI manager6_GetIsPackageIdentityAllowedToInstallForUserAsync(IAppInstallManager6 *iface,
        IUser *user, HSTRING correlation_vector, HSTRING package_identity_name, HSTRING publisher_certificate_name,
        IAsyncOperation_boolean **operation)
{
    return failed_boolean(operation);
}

static const IAppInstallManager6Vtbl manager6_vtbl =
{
    manager6_QueryInterface,
    manager6_AddRef,
    manager6_Release,
    manager6_GetIids,
    manager6_GetRuntimeClassName,
    manager6_GetTrustLevel,
    manager6_SearchForAllUpdatesWithUpdateOptionsAsync,
    manager6_SearchForAllUpdatesWithUpdateOptionsForUserAsync,
    manager6_SearchForUpdatesWithUpdateOptionsAsync,
    manager6_SearchForUpdatesWithUpdateOptionsForUserAsync,
    manager6_StartProductInstallWithOptionsAsync,
    manager6_StartProductInstallWithOptionsForUserAsync,
    manager6_GetIsPackageIdentityAllowedToInstallAsync,
    manager6_GetIsPackageIdentityAllowedToInstallForUserAsync,
};

DEFINE_MANAGER_FORWARDERS(manager7, IAppInstallManager7, IAppInstallManager7_iface)

static HRESULT WINAPI manager7_get_CanInstallForAllUsers(IAppInstallManager7 *iface, boolean *value)
{
    if (!value)
        return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static const IAppInstallManager7Vtbl manager7_vtbl =
{
    manager7_QueryInterface,
    manager7_AddRef,
    manager7_Release,
    manager7_GetIids,
    manager7_GetRuntimeClassName,
    manager7_GetTrustLevel,
    manager7_get_CanInstallForAllUsers,
};

struct manager_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IActivationFactory))
    {
        *out = iface;
        IActivationFactory_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct manager_factory, IActivationFactory_iface)->ref);
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    return InterlockedDecrement(&CONTAINING_RECORD(iface, struct manager_factory, IActivationFactory_iface)->ref);
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IActivationFactory, count, iids);
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *class_name)
{
    return manager_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    struct manager *impl;

    if (!instance)
        return E_POINTER;
    *instance = NULL;
    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IAppInstallManager_iface.lpVtbl = (IAppInstallManagerVtbl *)&manager_vtbl;
    impl->IAppInstallManager2_iface.lpVtbl = (IAppInstallManager2Vtbl *)&manager2_vtbl;
    impl->IAppInstallManager3_iface.lpVtbl = (IAppInstallManager3Vtbl *)&manager3_vtbl;
    impl->IAppInstallManager4_iface.lpVtbl = (IAppInstallManager4Vtbl *)&manager4_vtbl;
    impl->IAppInstallManager5_iface.lpVtbl = (IAppInstallManager5Vtbl *)&manager5_vtbl;
    impl->IAppInstallManager6_iface.lpVtbl = (IAppInstallManager6Vtbl *)&manager6_vtbl;
    impl->IAppInstallManager7_iface.lpVtbl = (IAppInstallManager7Vtbl *)&manager7_vtbl;
    impl->ref = 1;
    InitializeSRWLock(&impl->lock);
    event_list_init(&impl->item_completed);
    event_list_init(&impl->item_status_changed);
    *instance = (IInspectable *)&impl->IAppInstallManager_iface;
    return S_OK;
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

static struct manager_factory manager_factory = {{(IActivationFactoryVtbl *)&factory_vtbl}, 1};

IActivationFactory *app_install_manager_factory = &manager_factory.IActivationFactory_iface;
