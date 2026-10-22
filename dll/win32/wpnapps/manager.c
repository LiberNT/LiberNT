/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.UI.Notifications.ToastNotificationManager statics
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

struct manager
{
    IActivationFactory IActivationFactory_iface;
    IToastNotificationManagerStatics IToastNotificationManagerStatics_iface;
    IToastNotificationManagerStatics2 IToastNotificationManagerStatics2_iface;
    LONG ref;
};

static const struct
{
    const WCHAR *name;
    BOOL image;
    UINT texts;
}
toast_templates[] =
{
    { L"ToastImageAndText01", TRUE, 1 },
    { L"ToastImageAndText02", TRUE, 2 },
    { L"ToastImageAndText03", TRUE, 2 },
    { L"ToastImageAndText04", TRUE, 3 },
    { L"ToastText01", FALSE, 1 },
    { L"ToastText02", FALSE, 2 },
    { L"ToastText03", FALSE, 2 },
    { L"ToastText04", FALSE, 3 },
};

static inline struct manager *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct manager, IActivationFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct manager *impl = impl_from_IActivationFactory(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IActivationFactory))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotificationManagerStatics))
        *out = &impl->IToastNotificationManagerStatics_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotificationManagerStatics2))
        *out = &impl->IToastNotificationManagerStatics2_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    return InterlockedIncrement(&impl_from_IActivationFactory(iface)->ref);
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    return InterlockedDecrement(&impl_from_IActivationFactory(iface)->ref);
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastNotificationManagerStatics, count, iids);
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager,
                               wcslen(RuntimeClass_Windows_UI_Notifications_ToastNotificationManager), class_name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *trust_level)
{
    if (!trust_level)
        return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    if (!instance)
        return E_POINTER;
    *instance = NULL;
    return E_NOTIMPL;
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

static inline struct manager *impl_from_IToastNotificationManagerStatics(IToastNotificationManagerStatics *iface)
{
    return CONTAINING_RECORD(iface, struct manager, IToastNotificationManagerStatics_iface);
}

static HRESULT WINAPI statics_QueryInterface(IToastNotificationManagerStatics *iface, REFIID iid, void **out)
{
    return factory_QueryInterface(&impl_from_IToastNotificationManagerStatics(iface)->IActivationFactory_iface, iid, out);
}

static ULONG WINAPI statics_AddRef(IToastNotificationManagerStatics *iface)
{
    return factory_AddRef(&impl_from_IToastNotificationManagerStatics(iface)->IActivationFactory_iface);
}

static ULONG WINAPI statics_Release(IToastNotificationManagerStatics *iface)
{
    return factory_Release(&impl_from_IToastNotificationManagerStatics(iface)->IActivationFactory_iface);
}

static HRESULT WINAPI statics_GetIids(IToastNotificationManagerStatics *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastNotificationManagerStatics, count, iids);
}

static HRESULT WINAPI statics_GetRuntimeClassName(IToastNotificationManagerStatics *iface, HSTRING *class_name)
{
    return factory_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI statics_GetTrustLevel(IToastNotificationManagerStatics *iface, TrustLevel *trust_level)
{
    return factory_GetTrustLevel(NULL, trust_level);
}

static HRESULT WINAPI statics_CreateToastNotifier(IToastNotificationManagerStatics *iface, IToastNotifier **result)
{
    WCHAR *app_id;
    HRESULT hr;

    if (!result)
        return E_POINTER;
    *result = NULL;
    if (FAILED(hr = current_app_id(&app_id)))
        return hr;
    hr = notifier_create(app_id, result);
    free(app_id);
    return hr;
}

static HRESULT WINAPI statics_CreateToastNotifierWithId(IToastNotificationManagerStatics *iface, HSTRING application_id,
                                                        IToastNotifier **result)
{
    WCHAR *app_id;
    HRESULT hr;

    if (!result)
        return E_POINTER;
    *result = NULL;
    if (!WindowsGetStringLen(application_id))
        return E_INVALIDARG;
    if (FAILED(hr = hstring_to_wide(application_id, &app_id)))
        return hr;
    hr = notifier_create(app_id, result);
    free(app_id);
    return hr;
}

static HRESULT WINAPI statics_GetTemplateContent(IToastNotificationManagerStatics *iface, ToastTemplateType type,
                                                 IXmlDocument **result)
{
    WCHAR xml[512], text[32];
    UINT i;

    if (!result)
        return E_POINTER;
    *result = NULL;
    if ((UINT)type >= ARRAYSIZE(toast_templates))
        return E_INVALIDARG;
    wcscpy(xml, L"<toast><visual><binding template=\"");
    wcscat(xml, toast_templates[type].name);
    wcscat(xml, L"\">");
    if (toast_templates[type].image)
        wcscat(xml, L"<image id=\"1\" src=\"\"/>");
    for (i = 1; i <= toast_templates[type].texts; i++)
    {
        swprintf(text, ARRAYSIZE(text), L"<text id=\"%u\"></text>", i);
        wcscat(xml, text);
    }
    wcscat(xml, L"</binding></visual></toast>");
    return xml_load(xml, result);
}

static const IToastNotificationManagerStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_CreateToastNotifier,
    statics_CreateToastNotifierWithId,
    statics_GetTemplateContent,
};

static inline struct manager *impl_from_IToastNotificationManagerStatics2(IToastNotificationManagerStatics2 *iface)
{
    return CONTAINING_RECORD(iface, struct manager, IToastNotificationManagerStatics2_iface);
}

static HRESULT WINAPI statics2_QueryInterface(IToastNotificationManagerStatics2 *iface, REFIID iid, void **out)
{
    return factory_QueryInterface(&impl_from_IToastNotificationManagerStatics2(iface)->IActivationFactory_iface, iid, out);
}

static ULONG WINAPI statics2_AddRef(IToastNotificationManagerStatics2 *iface)
{
    return factory_AddRef(&impl_from_IToastNotificationManagerStatics2(iface)->IActivationFactory_iface);
}

static ULONG WINAPI statics2_Release(IToastNotificationManagerStatics2 *iface)
{
    return factory_Release(&impl_from_IToastNotificationManagerStatics2(iface)->IActivationFactory_iface);
}

static HRESULT WINAPI statics2_GetIids(IToastNotificationManagerStatics2 *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastNotificationManagerStatics2, count, iids);
}

static HRESULT WINAPI statics2_GetRuntimeClassName(IToastNotificationManagerStatics2 *iface, HSTRING *class_name)
{
    return factory_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI statics2_GetTrustLevel(IToastNotificationManagerStatics2 *iface, TrustLevel *trust_level)
{
    return factory_GetTrustLevel(NULL, trust_level);
}

static HRESULT WINAPI statics2_get_History(IToastNotificationManagerStatics2 *iface, IToastNotificationHistory **value)
{
    if (!value)
        return E_POINTER;
    return history_create(value);
}

static const IToastNotificationManagerStatics2Vtbl statics2_vtbl =
{
    statics2_QueryInterface,
    statics2_AddRef,
    statics2_Release,
    statics2_GetIids,
    statics2_GetRuntimeClassName,
    statics2_GetTrustLevel,
    statics2_get_History,
};

static struct manager manager =
{
    {(IActivationFactoryVtbl *)&factory_vtbl},
    {(IToastNotificationManagerStaticsVtbl *)&statics_vtbl},
    {(IToastNotificationManagerStatics2Vtbl *)&statics2_vtbl},
    1,
};

IActivationFactory *toast_manager_factory = &manager.IActivationFactory_iface;
