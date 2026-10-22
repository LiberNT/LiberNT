/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AppInstallOptions and AppUpdateOptions of the Store install control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

struct install_options
{
    IAppInstallOptions IAppInstallOptions_iface;
    IAppInstallOptions2 IAppInstallOptions2_iface;
    LONG ref;
    HSTRING catalog_id;
    boolean force_non_removable_storage;
    boolean allow_forced_restart;
    boolean repair;
    IPackageVolume *target_volume;
    boolean launch_after_install;
    boolean pin_to_desktop;
    boolean pin_to_start;
    boolean pin_to_taskbar;
    AppInstallationToastNotificationMode completed_toast_mode;
    AppInstallationToastNotificationMode progress_toast_mode;
    boolean install_for_all_users;
    boolean stage_only;
    HSTRING campaign_id;
    HSTRING extended_campaign_id;
};

struct update_options
{
    IAppUpdateOptions IAppUpdateOptions_iface;
    IAppUpdateOptions2 IAppUpdateOptions2_iface;
    LONG ref;
    HSTRING catalog_id;
    boolean allow_forced_restart;
    boolean automatically_install;
};

static HRESULT replace_hstring(HSTRING *field, HSTRING value)
{
    HSTRING copy;
    HRESULT hr;

    if (FAILED(hr = WindowsDuplicateString(value, &copy)))
        return hr;
    WindowsDeleteString(*field);
    *field = copy;
    return S_OK;
}

static HRESULT read_hstring(HSTRING field, HSTRING *value)
{
    if (!value)
        return E_POINTER;
    return WindowsDuplicateString(field, value);
}

#define DEFINE_BOOLEAN_PROPERTY(prefix, iface_type, impl_from, name, field)                            \
    static HRESULT WINAPI prefix##_get_##name(iface_type *iface, boolean *value)                       \
    {                                                                                                  \
        if (!value)                                                                                    \
            return E_POINTER;                                                                          \
        *value = impl_from(iface)->field;                                                              \
        return S_OK;                                                                                   \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_put_##name(iface_type *iface, boolean value)                        \
    {                                                                                                  \
        impl_from(iface)->field = value;                                                               \
        return S_OK;                                                                                   \
    }

#define DEFINE_STRING_PROPERTY(prefix, iface_type, impl_from, name, field)                             \
    static HRESULT WINAPI prefix##_get_##name(iface_type *iface, HSTRING *value)                       \
    {                                                                                                  \
        return read_hstring(impl_from(iface)->field, value);                                           \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_put_##name(iface_type *iface, HSTRING value)                        \
    {                                                                                                  \
        return replace_hstring(&impl_from(iface)->field, value);                                       \
    }

static inline struct install_options *install_from_IAppInstallOptions(IAppInstallOptions *iface)
{
    return CONTAINING_RECORD(iface, struct install_options, IAppInstallOptions_iface);
}

static inline struct install_options *install_from_IAppInstallOptions2(IAppInstallOptions2 *iface)
{
    return CONTAINING_RECORD(iface, struct install_options, IAppInstallOptions2_iface);
}

static HRESULT WINAPI install_QueryInterface(IAppInstallOptions *iface, REFIID iid, void **out)
{
    struct install_options *impl = install_from_IAppInstallOptions(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IAppInstallOptions))
        *out = &impl->IAppInstallOptions_iface;
    else if (IsEqualGUID(iid, &IID_IAppInstallOptions2))
        *out = &impl->IAppInstallOptions2_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI install_AddRef(IAppInstallOptions *iface)
{
    return InterlockedIncrement(&install_from_IAppInstallOptions(iface)->ref);
}

static ULONG WINAPI install_Release(IAppInstallOptions *iface)
{
    struct install_options *impl = install_from_IAppInstallOptions(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        WindowsDeleteString(impl->catalog_id);
        WindowsDeleteString(impl->campaign_id);
        WindowsDeleteString(impl->extended_campaign_id);
        if (impl->target_volume)
            IPackageVolume_Release(impl->target_volume);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI install_GetIids(IAppInstallOptions *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IAppInstallOptions, count, iids);
}

static HRESULT WINAPI install_GetRuntimeClassName(IAppInstallOptions *iface, HSTRING *class_name)
{
    return class_name_string(RuntimeClass_Windows_ApplicationModel_Store_Preview_InstallControl_AppInstallOptions,
                             class_name);
}

static HRESULT WINAPI install_GetTrustLevel(IAppInstallOptions *iface, TrustLevel *level)
{
    return trust_level(level);
}

DEFINE_STRING_PROPERTY(install, IAppInstallOptions, install_from_IAppInstallOptions, CatalogId, catalog_id)
DEFINE_BOOLEAN_PROPERTY(install, IAppInstallOptions, install_from_IAppInstallOptions, ForceUseOfNonRemovableStorage,
                        force_non_removable_storage)
DEFINE_BOOLEAN_PROPERTY(install, IAppInstallOptions, install_from_IAppInstallOptions, AllowForcedAppRestart,
                        allow_forced_restart)
DEFINE_BOOLEAN_PROPERTY(install, IAppInstallOptions, install_from_IAppInstallOptions, Repair, repair)
DEFINE_BOOLEAN_PROPERTY(install, IAppInstallOptions, install_from_IAppInstallOptions, LaunchAfterInstall,
                        launch_after_install)

static HRESULT WINAPI install_get_TargetVolume(IAppInstallOptions *iface, IPackageVolume **value)
{
    struct install_options *impl = install_from_IAppInstallOptions(iface);

    if (!value)
        return E_POINTER;
    if ((*value = impl->target_volume))
        IPackageVolume_AddRef(*value);
    return S_OK;
}

static HRESULT WINAPI install_put_TargetVolume(IAppInstallOptions *iface, IPackageVolume *value)
{
    struct install_options *impl = install_from_IAppInstallOptions(iface);

    if (value)
        IPackageVolume_AddRef(value);
    if (impl->target_volume)
        IPackageVolume_Release(impl->target_volume);
    impl->target_volume = value;
    return S_OK;
}

static const IAppInstallOptionsVtbl install_vtbl =
{
    install_QueryInterface,
    install_AddRef,
    install_Release,
    install_GetIids,
    install_GetRuntimeClassName,
    install_GetTrustLevel,
    install_get_CatalogId,
    install_put_CatalogId,
    install_get_ForceUseOfNonRemovableStorage,
    install_put_ForceUseOfNonRemovableStorage,
    install_get_AllowForcedAppRestart,
    install_put_AllowForcedAppRestart,
    install_get_Repair,
    install_put_Repair,
    install_get_TargetVolume,
    install_put_TargetVolume,
    install_get_LaunchAfterInstall,
    install_put_LaunchAfterInstall,
};

static HRESULT WINAPI install2_QueryInterface(IAppInstallOptions2 *iface, REFIID iid, void **out)
{
    return install_QueryInterface(&install_from_IAppInstallOptions2(iface)->IAppInstallOptions_iface, iid, out);
}

static ULONG WINAPI install2_AddRef(IAppInstallOptions2 *iface)
{
    return install_AddRef(&install_from_IAppInstallOptions2(iface)->IAppInstallOptions_iface);
}

static ULONG WINAPI install2_Release(IAppInstallOptions2 *iface)
{
    return install_Release(&install_from_IAppInstallOptions2(iface)->IAppInstallOptions_iface);
}

static HRESULT WINAPI install2_GetIids(IAppInstallOptions2 *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IAppInstallOptions2, count, iids);
}

static HRESULT WINAPI install2_GetRuntimeClassName(IAppInstallOptions2 *iface, HSTRING *class_name)
{
    return install_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI install2_GetTrustLevel(IAppInstallOptions2 *iface, TrustLevel *level)
{
    return trust_level(level);
}

DEFINE_BOOLEAN_PROPERTY(install2, IAppInstallOptions2, install_from_IAppInstallOptions2, PinToDesktopAfterInstall,
                        pin_to_desktop)
DEFINE_BOOLEAN_PROPERTY(install2, IAppInstallOptions2, install_from_IAppInstallOptions2, PinToStartAfterInstall,
                        pin_to_start)
DEFINE_BOOLEAN_PROPERTY(install2, IAppInstallOptions2, install_from_IAppInstallOptions2, PinToTaskbarAfterInstall,
                        pin_to_taskbar)
DEFINE_BOOLEAN_PROPERTY(install2, IAppInstallOptions2, install_from_IAppInstallOptions2, InstallForAllUsers,
                        install_for_all_users)
DEFINE_BOOLEAN_PROPERTY(install2, IAppInstallOptions2, install_from_IAppInstallOptions2, StageButDoNotInstall,
                        stage_only)
DEFINE_STRING_PROPERTY(install2, IAppInstallOptions2, install_from_IAppInstallOptions2, CampaignId, campaign_id)
DEFINE_STRING_PROPERTY(install2, IAppInstallOptions2, install_from_IAppInstallOptions2, ExtendedCampaignId,
                       extended_campaign_id)

static HRESULT WINAPI install2_get_CompletedInstallToastNotificationMode(IAppInstallOptions2 *iface,
        AppInstallationToastNotificationMode *value)
{
    if (!value)
        return E_POINTER;
    *value = install_from_IAppInstallOptions2(iface)->completed_toast_mode;
    return S_OK;
}

static HRESULT WINAPI install2_put_CompletedInstallToastNotificationMode(IAppInstallOptions2 *iface,
        AppInstallationToastNotificationMode value)
{
    if ((UINT)value > AppInstallationToastNotificationMode_NoToast)
        return E_INVALIDARG;
    install_from_IAppInstallOptions2(iface)->completed_toast_mode = value;
    return S_OK;
}

static HRESULT WINAPI install2_get_InstallInProgressToastNotificationMode(IAppInstallOptions2 *iface,
        AppInstallationToastNotificationMode *value)
{
    if (!value)
        return E_POINTER;
    *value = install_from_IAppInstallOptions2(iface)->progress_toast_mode;
    return S_OK;
}

static HRESULT WINAPI install2_put_InstallInProgressToastNotificationMode(IAppInstallOptions2 *iface,
        AppInstallationToastNotificationMode value)
{
    if ((UINT)value > AppInstallationToastNotificationMode_NoToast)
        return E_INVALIDARG;
    install_from_IAppInstallOptions2(iface)->progress_toast_mode = value;
    return S_OK;
}

static const IAppInstallOptions2Vtbl install2_vtbl =
{
    install2_QueryInterface,
    install2_AddRef,
    install2_Release,
    install2_GetIids,
    install2_GetRuntimeClassName,
    install2_GetTrustLevel,
    install2_get_PinToDesktopAfterInstall,
    install2_put_PinToDesktopAfterInstall,
    install2_get_PinToStartAfterInstall,
    install2_put_PinToStartAfterInstall,
    install2_get_PinToTaskbarAfterInstall,
    install2_put_PinToTaskbarAfterInstall,
    install2_get_CompletedInstallToastNotificationMode,
    install2_put_CompletedInstallToastNotificationMode,
    install2_get_InstallInProgressToastNotificationMode,
    install2_put_InstallInProgressToastNotificationMode,
    install2_get_InstallForAllUsers,
    install2_put_InstallForAllUsers,
    install2_get_StageButDoNotInstall,
    install2_put_StageButDoNotInstall,
    install2_get_CampaignId,
    install2_put_CampaignId,
    install2_get_ExtendedCampaignId,
    install2_put_ExtendedCampaignId,
};

static inline struct update_options *update_from_IAppUpdateOptions(IAppUpdateOptions *iface)
{
    return CONTAINING_RECORD(iface, struct update_options, IAppUpdateOptions_iface);
}

static inline struct update_options *update_from_IAppUpdateOptions2(IAppUpdateOptions2 *iface)
{
    return CONTAINING_RECORD(iface, struct update_options, IAppUpdateOptions2_iface);
}

static HRESULT WINAPI update_QueryInterface(IAppUpdateOptions *iface, REFIID iid, void **out)
{
    struct update_options *impl = update_from_IAppUpdateOptions(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IAppUpdateOptions))
        *out = &impl->IAppUpdateOptions_iface;
    else if (IsEqualGUID(iid, &IID_IAppUpdateOptions2))
        *out = &impl->IAppUpdateOptions2_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI update_AddRef(IAppUpdateOptions *iface)
{
    return InterlockedIncrement(&update_from_IAppUpdateOptions(iface)->ref);
}

static ULONG WINAPI update_Release(IAppUpdateOptions *iface)
{
    struct update_options *impl = update_from_IAppUpdateOptions(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        WindowsDeleteString(impl->catalog_id);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI update_GetIids(IAppUpdateOptions *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IAppUpdateOptions, count, iids);
}

static HRESULT WINAPI update_GetRuntimeClassName(IAppUpdateOptions *iface, HSTRING *class_name)
{
    return class_name_string(RuntimeClass_Windows_ApplicationModel_Store_Preview_InstallControl_AppUpdateOptions,
                             class_name);
}

static HRESULT WINAPI update_GetTrustLevel(IAppUpdateOptions *iface, TrustLevel *level)
{
    return trust_level(level);
}

DEFINE_STRING_PROPERTY(update, IAppUpdateOptions, update_from_IAppUpdateOptions, CatalogId, catalog_id)
DEFINE_BOOLEAN_PROPERTY(update, IAppUpdateOptions, update_from_IAppUpdateOptions, AllowForcedAppRestart,
                        allow_forced_restart)

static const IAppUpdateOptionsVtbl update_vtbl =
{
    update_QueryInterface,
    update_AddRef,
    update_Release,
    update_GetIids,
    update_GetRuntimeClassName,
    update_GetTrustLevel,
    update_get_CatalogId,
    update_put_CatalogId,
    update_get_AllowForcedAppRestart,
    update_put_AllowForcedAppRestart,
};

static HRESULT WINAPI update2_QueryInterface(IAppUpdateOptions2 *iface, REFIID iid, void **out)
{
    return update_QueryInterface(&update_from_IAppUpdateOptions2(iface)->IAppUpdateOptions_iface, iid, out);
}

static ULONG WINAPI update2_AddRef(IAppUpdateOptions2 *iface)
{
    return update_AddRef(&update_from_IAppUpdateOptions2(iface)->IAppUpdateOptions_iface);
}

static ULONG WINAPI update2_Release(IAppUpdateOptions2 *iface)
{
    return update_Release(&update_from_IAppUpdateOptions2(iface)->IAppUpdateOptions_iface);
}

static HRESULT WINAPI update2_GetIids(IAppUpdateOptions2 *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IAppUpdateOptions2, count, iids);
}

static HRESULT WINAPI update2_GetRuntimeClassName(IAppUpdateOptions2 *iface, HSTRING *class_name)
{
    return update_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI update2_GetTrustLevel(IAppUpdateOptions2 *iface, TrustLevel *level)
{
    return trust_level(level);
}

DEFINE_BOOLEAN_PROPERTY(update2, IAppUpdateOptions2, update_from_IAppUpdateOptions2,
                        AutomaticallyDownloadAndInstallUpdateIfFound, automatically_install)

static const IAppUpdateOptions2Vtbl update2_vtbl =
{
    update2_QueryInterface,
    update2_AddRef,
    update2_Release,
    update2_GetIids,
    update2_GetRuntimeClassName,
    update2_GetTrustLevel,
    update2_get_AutomaticallyDownloadAndInstallUpdateIfFound,
    update2_put_AutomaticallyDownloadAndInstallUpdateIfFound,
};

struct options_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
    BOOL install;
};

static inline struct options_factory *factory_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct options_factory, IActivationFactory_iface);
}

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
    return InterlockedIncrement(&factory_from_IActivationFactory(iface)->ref);
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    return InterlockedDecrement(&factory_from_IActivationFactory(iface)->ref);
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IActivationFactory, count, iids);
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *class_name)
{
    if (factory_from_IActivationFactory(iface)->install)
        return install_GetRuntimeClassName(NULL, class_name);
    return update_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level)
{
    return trust_level(level);
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    if (!instance)
        return E_POINTER;
    *instance = NULL;
    if (factory_from_IActivationFactory(iface)->install)
    {
        struct install_options *impl = calloc(1, sizeof(*impl));

        if (!impl)
            return E_OUTOFMEMORY;
        impl->IAppInstallOptions_iface.lpVtbl = (IAppInstallOptionsVtbl *)&install_vtbl;
        impl->IAppInstallOptions2_iface.lpVtbl = (IAppInstallOptions2Vtbl *)&install2_vtbl;
        impl->ref = 1;
        impl->completed_toast_mode = AppInstallationToastNotificationMode_Default;
        impl->progress_toast_mode = AppInstallationToastNotificationMode_Default;
        *instance = (IInspectable *)&impl->IAppInstallOptions_iface;
    }
    else
    {
        struct update_options *impl = calloc(1, sizeof(*impl));

        if (!impl)
            return E_OUTOFMEMORY;
        impl->IAppUpdateOptions_iface.lpVtbl = (IAppUpdateOptionsVtbl *)&update_vtbl;
        impl->IAppUpdateOptions2_iface.lpVtbl = (IAppUpdateOptions2Vtbl *)&update2_vtbl;
        impl->ref = 1;
        *instance = (IInspectable *)&impl->IAppUpdateOptions_iface;
    }
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

static struct options_factory install_factory = {{(IActivationFactoryVtbl *)&factory_vtbl}, 1, TRUE};
static struct options_factory update_factory = {{(IActivationFactoryVtbl *)&factory_vtbl}, 1, FALSE};

IActivationFactory *app_install_options_factory = &install_factory.IActivationFactory_iface;
IActivationFactory *app_update_options_factory = &update_factory.IActivationFactory_iface;
