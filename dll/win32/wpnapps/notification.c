/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.UI.Notifications.ToastNotification and its event arguments
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

struct toast
{
    IToastNotification IToastNotification_iface;
    IToastNotification2 IToastNotification2_iface;
    IToastNotification3 IToastNotification3_iface;
    IToastNotification4 IToastNotification4_iface;
    IToastNotification6 IToastNotification6_iface;
    LONG ref;
    SRWLOCK lock;
    IXmlDocument *content;
    IReference_DateTime *expiration;
    HSTRING tag;
    HSTRING group;
    HSTRING remote_id;
    boolean suppress_popup;
    boolean expires_on_reboot;
    NotificationMirroring mirroring;
    INotificationData *data;
    ToastNotificationPriority priority;
    struct event_list dismissed;
    struct event_list activated;
    struct event_list failed;
};

struct dismissed_args
{
    IToastDismissedEventArgs IToastDismissedEventArgs_iface;
    LONG ref;
    ToastDismissalReason reason;
};

struct failed_args
{
    IToastFailedEventArgs IToastFailedEventArgs_iface;
    LONG ref;
    HRESULT error;
};

struct activated_args
{
    IToastActivatedEventArgs IToastActivatedEventArgs_iface;
    IToastActivatedEventArgs2 IToastActivatedEventArgs2_iface;
    LONG ref;
    HSTRING arguments;
    IPropertySet *user_input;
};

static const IToastNotificationVtbl toast_vtbl;

static inline struct toast *impl_from_IToastNotification(IToastNotification *iface)
{
    return CONTAINING_RECORD(iface, struct toast, IToastNotification_iface);
}

static HRESULT inspectable_class_name(const WCHAR *name, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    return WindowsCreateString(name, wcslen(name), class_name);
}

static HRESULT inspectable_trust_level(TrustLevel *trust_level)
{
    if (!trust_level)
        return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI dismissed_args_QueryInterface(IToastDismissedEventArgs *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IToastDismissedEventArgs))
    {
        *out = iface;
        IToastDismissedEventArgs_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dismissed_args_AddRef(IToastDismissedEventArgs *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct dismissed_args, IToastDismissedEventArgs_iface)->ref);
}

static ULONG WINAPI dismissed_args_Release(IToastDismissedEventArgs *iface)
{
    struct dismissed_args *impl = CONTAINING_RECORD(iface, struct dismissed_args, IToastDismissedEventArgs_iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
        free(impl);
    return ref;
}

static HRESULT WINAPI dismissed_args_GetIids(IToastDismissedEventArgs *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastDismissedEventArgs, count, iids);
}

static HRESULT WINAPI dismissed_args_GetRuntimeClassName(IToastDismissedEventArgs *iface, HSTRING *class_name)
{
    return inspectable_class_name(RuntimeClass_Windows_UI_Notifications_ToastDismissedEventArgs, class_name);
}

static HRESULT WINAPI dismissed_args_GetTrustLevel(IToastDismissedEventArgs *iface, TrustLevel *trust_level)
{
    return inspectable_trust_level(trust_level);
}

static HRESULT WINAPI dismissed_args_get_Reason(IToastDismissedEventArgs *iface, ToastDismissalReason *value)
{
    if (!value)
        return E_POINTER;
    *value = CONTAINING_RECORD(iface, struct dismissed_args, IToastDismissedEventArgs_iface)->reason;
    return S_OK;
}

static const IToastDismissedEventArgsVtbl dismissed_args_vtbl =
{
    dismissed_args_QueryInterface,
    dismissed_args_AddRef,
    dismissed_args_Release,
    dismissed_args_GetIids,
    dismissed_args_GetRuntimeClassName,
    dismissed_args_GetTrustLevel,
    dismissed_args_get_Reason,
};

static HRESULT WINAPI failed_args_QueryInterface(IToastFailedEventArgs *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IToastFailedEventArgs))
    {
        *out = iface;
        IToastFailedEventArgs_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI failed_args_AddRef(IToastFailedEventArgs *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct failed_args, IToastFailedEventArgs_iface)->ref);
}

static ULONG WINAPI failed_args_Release(IToastFailedEventArgs *iface)
{
    struct failed_args *impl = CONTAINING_RECORD(iface, struct failed_args, IToastFailedEventArgs_iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
        free(impl);
    return ref;
}

static HRESULT WINAPI failed_args_GetIids(IToastFailedEventArgs *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastFailedEventArgs, count, iids);
}

static HRESULT WINAPI failed_args_GetRuntimeClassName(IToastFailedEventArgs *iface, HSTRING *class_name)
{
    return inspectable_class_name(RuntimeClass_Windows_UI_Notifications_ToastFailedEventArgs, class_name);
}

static HRESULT WINAPI failed_args_GetTrustLevel(IToastFailedEventArgs *iface, TrustLevel *trust_level)
{
    return inspectable_trust_level(trust_level);
}

static HRESULT WINAPI failed_args_get_ErrorCode(IToastFailedEventArgs *iface, HRESULT *value)
{
    if (!value)
        return E_POINTER;
    *value = CONTAINING_RECORD(iface, struct failed_args, IToastFailedEventArgs_iface)->error;
    return S_OK;
}

static const IToastFailedEventArgsVtbl failed_args_vtbl =
{
    failed_args_QueryInterface,
    failed_args_AddRef,
    failed_args_Release,
    failed_args_GetIids,
    failed_args_GetRuntimeClassName,
    failed_args_GetTrustLevel,
    failed_args_get_ErrorCode,
};

static HRESULT WINAPI activated_args_QueryInterface(IToastActivatedEventArgs *iface, REFIID iid, void **out)
{
    struct activated_args *impl = CONTAINING_RECORD(iface, struct activated_args, IToastActivatedEventArgs_iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IToastActivatedEventArgs))
        *out = &impl->IToastActivatedEventArgs_iface;
    else if (IsEqualGUID(iid, &IID_IToastActivatedEventArgs2))
        *out = &impl->IToastActivatedEventArgs2_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI activated_args_AddRef(IToastActivatedEventArgs *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct activated_args, IToastActivatedEventArgs_iface)->ref);
}

static ULONG WINAPI activated_args_Release(IToastActivatedEventArgs *iface)
{
    struct activated_args *impl = CONTAINING_RECORD(iface, struct activated_args, IToastActivatedEventArgs_iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        WindowsDeleteString(impl->arguments);
        if (impl->user_input)
            IPropertySet_Release(impl->user_input);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI activated_args_GetIids(IToastActivatedEventArgs *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastActivatedEventArgs, count, iids);
}

static HRESULT WINAPI activated_args_GetRuntimeClassName(IToastActivatedEventArgs *iface, HSTRING *class_name)
{
    return inspectable_class_name(RuntimeClass_Windows_UI_Notifications_ToastActivatedEventArgs, class_name);
}

static HRESULT WINAPI activated_args_GetTrustLevel(IToastActivatedEventArgs *iface, TrustLevel *trust_level)
{
    return inspectable_trust_level(trust_level);
}

static HRESULT WINAPI activated_args_get_Arguments(IToastActivatedEventArgs *iface, HSTRING *value)
{
    struct activated_args *impl = CONTAINING_RECORD(iface, struct activated_args, IToastActivatedEventArgs_iface);

    if (!value)
        return E_POINTER;
    return WindowsDuplicateString(impl->arguments, value);
}

static const IToastActivatedEventArgsVtbl activated_args_vtbl =
{
    activated_args_QueryInterface,
    activated_args_AddRef,
    activated_args_Release,
    activated_args_GetIids,
    activated_args_GetRuntimeClassName,
    activated_args_GetTrustLevel,
    activated_args_get_Arguments,
};

static inline struct activated_args *impl_from_IToastActivatedEventArgs2(IToastActivatedEventArgs2 *iface)
{
    return CONTAINING_RECORD(iface, struct activated_args, IToastActivatedEventArgs2_iface);
}

static HRESULT WINAPI activated_args2_QueryInterface(IToastActivatedEventArgs2 *iface, REFIID iid, void **out)
{
    return activated_args_QueryInterface(&impl_from_IToastActivatedEventArgs2(iface)->IToastActivatedEventArgs_iface,
                                         iid, out);
}

static ULONG WINAPI activated_args2_AddRef(IToastActivatedEventArgs2 *iface)
{
    return activated_args_AddRef(&impl_from_IToastActivatedEventArgs2(iface)->IToastActivatedEventArgs_iface);
}

static ULONG WINAPI activated_args2_Release(IToastActivatedEventArgs2 *iface)
{
    return activated_args_Release(&impl_from_IToastActivatedEventArgs2(iface)->IToastActivatedEventArgs_iface);
}

static HRESULT WINAPI activated_args2_GetIids(IToastActivatedEventArgs2 *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastActivatedEventArgs2, count, iids);
}

static HRESULT WINAPI activated_args2_GetRuntimeClassName(IToastActivatedEventArgs2 *iface, HSTRING *class_name)
{
    return inspectable_class_name(RuntimeClass_Windows_UI_Notifications_ToastActivatedEventArgs, class_name);
}

static HRESULT WINAPI activated_args2_GetTrustLevel(IToastActivatedEventArgs2 *iface, TrustLevel *trust_level)
{
    return inspectable_trust_level(trust_level);
}

static HRESULT WINAPI activated_args2_get_UserInput(IToastActivatedEventArgs2 *iface, IPropertySet **value)
{
    struct activated_args *impl = impl_from_IToastActivatedEventArgs2(iface);

    if (!value)
        return E_POINTER;
    if ((*value = impl->user_input))
        IPropertySet_AddRef(*value);
    return S_OK;
}

static const IToastActivatedEventArgs2Vtbl activated_args2_vtbl =
{
    activated_args2_QueryInterface,
    activated_args2_AddRef,
    activated_args2_Release,
    activated_args2_GetIids,
    activated_args2_GetRuntimeClassName,
    activated_args2_GetTrustLevel,
    activated_args2_get_UserInput,
};

static HRESULT create_user_input(const NOTIFICATION_USER_INPUT_DATA *inputs, ULONG count, IPropertySet **out)
{
    IPropertyValueStatics *statics = NULL;
    IMap_HSTRING_IInspectable *map = NULL;
    IInspectable *instance, *value;
    HSTRING class_name, key, text;
    boolean replaced;
    HRESULT hr;
    ULONG i;

    *out = NULL;
    hr = WindowsCreateString(RuntimeClass_Windows_Foundation_Collections_ValueSet,
                             wcslen(RuntimeClass_Windows_Foundation_Collections_ValueSet), &class_name);
    if (FAILED(hr))
        return hr;
    hr = RoActivateInstance(class_name, &instance);
    WindowsDeleteString(class_name);
    if (FAILED(hr))
        return hr;
    hr = IInspectable_QueryInterface(instance, &IID_IMap_HSTRING_IInspectable, (void **)&map);
    if (SUCCEEDED(hr) && count)
    {
        hr = WindowsCreateString(RuntimeClass_Windows_Foundation_PropertyValue,
                                 wcslen(RuntimeClass_Windows_Foundation_PropertyValue), &class_name);
        if (SUCCEEDED(hr))
        {
            hr = RoGetActivationFactory(class_name, &IID_IPropertyValueStatics, (void **)&statics);
            WindowsDeleteString(class_name);
        }
        for (i = 0; SUCCEEDED(hr) && i < count; i++)
        {
            if (FAILED(hr = WindowsCreateString(inputs[i].Value, wcslen(inputs[i].Value), &text)))
                break;
            hr = IPropertyValueStatics_CreateString(statics, text, &value);
            WindowsDeleteString(text);
            if (FAILED(hr))
                break;
            if (SUCCEEDED(hr = WindowsCreateString(inputs[i].Key, wcslen(inputs[i].Key), &key)))
            {
                hr = IMap_HSTRING_IInspectable_Insert(map, key, value, &replaced);
                WindowsDeleteString(key);
            }
            IInspectable_Release(value);
        }
        if (statics)
            IPropertyValueStatics_Release(statics);
    }
    if (SUCCEEDED(hr))
        hr = IInspectable_QueryInterface(instance, &IID_IPropertySet, (void **)out);
    if (map)
        IMap_HSTRING_IInspectable_Release(map);
    IInspectable_Release(instance);
    return hr;
}

static HRESULT WINAPI toast_QueryInterface(IToastNotification *iface, REFIID iid, void **out)
{
    struct toast *impl = impl_from_IToastNotification(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IToastNotification))
        *out = &impl->IToastNotification_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotification2))
        *out = &impl->IToastNotification2_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotification3))
        *out = &impl->IToastNotification3_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotification4))
        *out = &impl->IToastNotification4_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotification6))
        *out = &impl->IToastNotification6_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI toast_AddRef(IToastNotification *iface)
{
    return InterlockedIncrement(&impl_from_IToastNotification(iface)->ref);
}

static ULONG WINAPI toast_Release(IToastNotification *iface)
{
    struct toast *impl = impl_from_IToastNotification(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        event_list_clear(&impl->dismissed);
        event_list_clear(&impl->activated);
        event_list_clear(&impl->failed);
        IXmlDocument_Release(impl->content);
        if (impl->expiration)
            IReference_DateTime_Release(impl->expiration);
        if (impl->data)
            INotificationData_Release(impl->data);
        WindowsDeleteString(impl->tag);
        WindowsDeleteString(impl->group);
        WindowsDeleteString(impl->remote_id);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI toast_GetIids(IToastNotification *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastNotification, count, iids);
}

static HRESULT WINAPI toast_GetRuntimeClassName(IToastNotification *iface, HSTRING *class_name)
{
    return inspectable_class_name(RuntimeClass_Windows_UI_Notifications_ToastNotification, class_name);
}

static HRESULT WINAPI toast_GetTrustLevel(IToastNotification *iface, TrustLevel *trust_level)
{
    return inspectable_trust_level(trust_level);
}

static HRESULT WINAPI toast_get_Content(IToastNotification *iface, IXmlDocument **value)
{
    struct toast *impl = impl_from_IToastNotification(iface);

    if (!value)
        return E_POINTER;
    *value = impl->content;
    IXmlDocument_AddRef(*value);
    return S_OK;
}

static HRESULT WINAPI toast_put_ExpirationTime(IToastNotification *iface, IReference_DateTime *value)
{
    struct toast *impl = impl_from_IToastNotification(iface);
    IReference_DateTime *previous;

    if (value)
        IReference_DateTime_AddRef(value);
    AcquireSRWLockExclusive(&impl->lock);
    previous = impl->expiration;
    impl->expiration = value;
    ReleaseSRWLockExclusive(&impl->lock);
    if (previous)
        IReference_DateTime_Release(previous);
    return S_OK;
}

static HRESULT WINAPI toast_get_ExpirationTime(IToastNotification *iface, IReference_DateTime **value)
{
    struct toast *impl = impl_from_IToastNotification(iface);

    if (!value)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    if ((*value = impl->expiration))
        IReference_DateTime_AddRef(*value);
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}

static HRESULT WINAPI toast_add_Dismissed(IToastNotification *iface,
        ITypedEventHandler_ToastNotification_ToastDismissedEventArgs *handler, EventRegistrationToken *token)
{
    return event_list_add(&impl_from_IToastNotification(iface)->dismissed, handler,
                          &IID_ITypedEventHandler_ToastNotification_ToastDismissedEventArgs, token);
}

static HRESULT WINAPI toast_remove_Dismissed(IToastNotification *iface, EventRegistrationToken token)
{
    return event_list_remove(&impl_from_IToastNotification(iface)->dismissed, token);
}

static HRESULT WINAPI toast_add_Activated(IToastNotification *iface,
        ITypedEventHandler_ToastNotification_IInspectable *handler, EventRegistrationToken *token)
{
    return event_list_add(&impl_from_IToastNotification(iface)->activated, handler,
                          &IID_ITypedEventHandler_ToastNotification_IInspectable, token);
}

static HRESULT WINAPI toast_remove_Activated(IToastNotification *iface, EventRegistrationToken token)
{
    return event_list_remove(&impl_from_IToastNotification(iface)->activated, token);
}

static HRESULT WINAPI toast_add_Failed(IToastNotification *iface,
        ITypedEventHandler_ToastNotification_ToastFailedEventArgs *handler, EventRegistrationToken *token)
{
    return event_list_add(&impl_from_IToastNotification(iface)->failed, handler,
                          &IID_ITypedEventHandler_ToastNotification_ToastFailedEventArgs, token);
}

static HRESULT WINAPI toast_remove_Failed(IToastNotification *iface, EventRegistrationToken token)
{
    return event_list_remove(&impl_from_IToastNotification(iface)->failed, token);
}

static const IToastNotificationVtbl toast_vtbl =
{
    toast_QueryInterface,
    toast_AddRef,
    toast_Release,
    toast_GetIids,
    toast_GetRuntimeClassName,
    toast_GetTrustLevel,
    toast_get_Content,
    toast_put_ExpirationTime,
    toast_get_ExpirationTime,
    toast_add_Dismissed,
    toast_remove_Dismissed,
    toast_add_Activated,
    toast_remove_Activated,
    toast_add_Failed,
    toast_remove_Failed,
};

#define DEFINE_TOAST_FORWARDERS(prefix, iface_type, member)                                            \
    static inline struct toast *impl_from_##iface_type(iface_type *iface)                              \
    {                                                                                                  \
        return CONTAINING_RECORD(iface, struct toast, member);                                         \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_QueryInterface(iface_type *iface, REFIID iid, void **out)           \
    {                                                                                                  \
        return toast_QueryInterface(&impl_from_##iface_type(iface)->IToastNotification_iface, iid, out);\
    }                                                                                                  \
    static ULONG WINAPI prefix##_AddRef(iface_type *iface)                                             \
    {                                                                                                  \
        return toast_AddRef(&impl_from_##iface_type(iface)->IToastNotification_iface);                 \
    }                                                                                                  \
    static ULONG WINAPI prefix##_Release(iface_type *iface)                                            \
    {                                                                                                  \
        return toast_Release(&impl_from_##iface_type(iface)->IToastNotification_iface);                \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_GetIids(iface_type *iface, ULONG *count, IID **iids)                \
    {                                                                                                  \
        return get_iids(&IID_##iface_type, count, iids);                                               \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_GetRuntimeClassName(iface_type *iface, HSTRING *class_name)         \
    {                                                                                                  \
        return toast_GetRuntimeClassName(NULL, class_name);                                            \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_GetTrustLevel(iface_type *iface, TrustLevel *trust_level)           \
    {                                                                                                  \
        return inspectable_trust_level(trust_level);                                                   \
    }

static HRESULT set_string(struct toast *impl, HSTRING *field, HSTRING value, HRESULT too_long)
{
    HSTRING copy, previous;
    HRESULT hr;

    if (WindowsGetStringLen(value) > TOAST_MAX_TAG_LENGTH)
        return too_long;
    hr = WindowsDuplicateString(value, &copy);
    if (FAILED(hr))
        return hr;
    AcquireSRWLockExclusive(&impl->lock);
    previous = *field;
    *field = copy;
    ReleaseSRWLockExclusive(&impl->lock);
    WindowsDeleteString(previous);
    return S_OK;
}

static HRESULT get_string(struct toast *impl, HSTRING *field, HSTRING *value)
{
    HRESULT hr;

    if (!value)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    hr = WindowsDuplicateString(*field, value);
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

DEFINE_TOAST_FORWARDERS(toast2, IToastNotification2, IToastNotification2_iface)

static HRESULT WINAPI toast2_put_Tag(IToastNotification2 *iface, HSTRING value)
{
    struct toast *impl = impl_from_IToastNotification2(iface);

    return set_string(impl, &impl->tag, value, WPN_E_TAG_SIZE);
}

static HRESULT WINAPI toast2_get_Tag(IToastNotification2 *iface, HSTRING *value)
{
    struct toast *impl = impl_from_IToastNotification2(iface);

    return get_string(impl, &impl->tag, value);
}

static HRESULT WINAPI toast2_put_Group(IToastNotification2 *iface, HSTRING value)
{
    struct toast *impl = impl_from_IToastNotification2(iface);

    return set_string(impl, &impl->group, value, WPN_E_GROUP_SIZE);
}

static HRESULT WINAPI toast2_get_Group(IToastNotification2 *iface, HSTRING *value)
{
    struct toast *impl = impl_from_IToastNotification2(iface);

    return get_string(impl, &impl->group, value);
}

static HRESULT WINAPI toast2_put_SuppressPopup(IToastNotification2 *iface, boolean value)
{
    impl_from_IToastNotification2(iface)->suppress_popup = value;
    return S_OK;
}

static HRESULT WINAPI toast2_get_SuppressPopup(IToastNotification2 *iface, boolean *value)
{
    if (!value)
        return E_POINTER;
    *value = impl_from_IToastNotification2(iface)->suppress_popup;
    return S_OK;
}

static const IToastNotification2Vtbl toast2_vtbl =
{
    toast2_QueryInterface,
    toast2_AddRef,
    toast2_Release,
    toast2_GetIids,
    toast2_GetRuntimeClassName,
    toast2_GetTrustLevel,
    toast2_put_Tag,
    toast2_get_Tag,
    toast2_put_Group,
    toast2_get_Group,
    toast2_put_SuppressPopup,
    toast2_get_SuppressPopup,
};

DEFINE_TOAST_FORWARDERS(toast3, IToastNotification3, IToastNotification3_iface)

static HRESULT WINAPI toast3_get_NotificationMirroring(IToastNotification3 *iface, NotificationMirroring *value)
{
    if (!value)
        return E_POINTER;
    *value = impl_from_IToastNotification3(iface)->mirroring;
    return S_OK;
}

static HRESULT WINAPI toast3_put_NotificationMirroring(IToastNotification3 *iface, NotificationMirroring value)
{
    if (value != NotificationMirroring_Allowed && value != NotificationMirroring_Disabled)
        return E_INVALIDARG;
    impl_from_IToastNotification3(iface)->mirroring = value;
    return S_OK;
}

static HRESULT WINAPI toast3_get_RemoteId(IToastNotification3 *iface, HSTRING *value)
{
    struct toast *impl = impl_from_IToastNotification3(iface);

    return get_string(impl, &impl->remote_id, value);
}

static HRESULT WINAPI toast3_put_RemoteId(IToastNotification3 *iface, HSTRING value)
{
    struct toast *impl = impl_from_IToastNotification3(iface);
    HSTRING copy, previous;
    HRESULT hr;

    hr = WindowsDuplicateString(value, &copy);
    if (FAILED(hr))
        return hr;
    AcquireSRWLockExclusive(&impl->lock);
    previous = impl->remote_id;
    impl->remote_id = copy;
    ReleaseSRWLockExclusive(&impl->lock);
    WindowsDeleteString(previous);
    return S_OK;
}

static const IToastNotification3Vtbl toast3_vtbl =
{
    toast3_QueryInterface,
    toast3_AddRef,
    toast3_Release,
    toast3_GetIids,
    toast3_GetRuntimeClassName,
    toast3_GetTrustLevel,
    toast3_get_NotificationMirroring,
    toast3_put_NotificationMirroring,
    toast3_get_RemoteId,
    toast3_put_RemoteId,
};

DEFINE_TOAST_FORWARDERS(toast4, IToastNotification4, IToastNotification4_iface)

static HRESULT WINAPI toast4_get_Data(IToastNotification4 *iface, INotificationData **value)
{
    struct toast *impl = impl_from_IToastNotification4(iface);

    if (!value)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    if ((*value = impl->data))
        INotificationData_AddRef(*value);
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}

static HRESULT WINAPI toast4_put_Data(IToastNotification4 *iface, INotificationData *value)
{
    struct toast *impl = impl_from_IToastNotification4(iface);
    INotificationData *previous;

    if (value)
        INotificationData_AddRef(value);
    AcquireSRWLockExclusive(&impl->lock);
    previous = impl->data;
    impl->data = value;
    ReleaseSRWLockExclusive(&impl->lock);
    if (previous)
        INotificationData_Release(previous);
    return S_OK;
}

static HRESULT WINAPI toast4_get_Priority(IToastNotification4 *iface, ToastNotificationPriority *value)
{
    if (!value)
        return E_POINTER;
    *value = impl_from_IToastNotification4(iface)->priority;
    return S_OK;
}

static HRESULT WINAPI toast4_put_Priority(IToastNotification4 *iface, ToastNotificationPriority value)
{
    if (value != ToastNotificationPriority_Default && value != ToastNotificationPriority_High)
        return E_INVALIDARG;
    impl_from_IToastNotification4(iface)->priority = value;
    return S_OK;
}

static const IToastNotification4Vtbl toast4_vtbl =
{
    toast4_QueryInterface,
    toast4_AddRef,
    toast4_Release,
    toast4_GetIids,
    toast4_GetRuntimeClassName,
    toast4_GetTrustLevel,
    toast4_get_Data,
    toast4_put_Data,
    toast4_get_Priority,
    toast4_put_Priority,
};

DEFINE_TOAST_FORWARDERS(toast6, IToastNotification6, IToastNotification6_iface)

static HRESULT WINAPI toast6_get_ExpiresOnReboot(IToastNotification6 *iface, boolean *value)
{
    if (!value)
        return E_POINTER;
    *value = impl_from_IToastNotification6(iface)->expires_on_reboot;
    return S_OK;
}

static HRESULT WINAPI toast6_put_ExpiresOnReboot(IToastNotification6 *iface, boolean value)
{
    impl_from_IToastNotification6(iface)->expires_on_reboot = value;
    return S_OK;
}

static const IToastNotification6Vtbl toast6_vtbl =
{
    toast6_QueryInterface,
    toast6_AddRef,
    toast6_Release,
    toast6_GetIids,
    toast6_GetRuntimeClassName,
    toast6_GetTrustLevel,
    toast6_get_ExpiresOnReboot,
    toast6_put_ExpiresOnReboot,
};

HRESULT toast_create(IXmlDocument *content, IToastNotification **out)
{
    struct toast *impl;

    *out = NULL;
    if (!content)
        return E_INVALIDARG;
    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IToastNotification_iface.lpVtbl = (IToastNotificationVtbl *)&toast_vtbl;
    impl->IToastNotification2_iface.lpVtbl = (IToastNotification2Vtbl *)&toast2_vtbl;
    impl->IToastNotification3_iface.lpVtbl = (IToastNotification3Vtbl *)&toast3_vtbl;
    impl->IToastNotification4_iface.lpVtbl = (IToastNotification4Vtbl *)&toast4_vtbl;
    impl->IToastNotification6_iface.lpVtbl = (IToastNotification6Vtbl *)&toast6_vtbl;
    impl->ref = 1;
    InitializeSRWLock(&impl->lock);
    impl->content = content;
    IXmlDocument_AddRef(impl->content);
    impl->mirroring = NotificationMirroring_Allowed;
    impl->priority = ToastNotificationPriority_Default;
    event_list_init(&impl->dismissed);
    event_list_init(&impl->activated);
    event_list_init(&impl->failed);
    *out = &impl->IToastNotification_iface;
    return S_OK;
}

BOOL toast_is_ours(IToastNotification *toast)
{
    return toast && toast->lpVtbl == &toast_vtbl;
}

void toast_get_identity(IToastNotification *toast, WCHAR **tag, WCHAR **group)
{
    struct toast *impl = impl_from_IToastNotification(toast);

    AcquireSRWLockShared(&impl->lock);
    if (tag)
        hstring_to_wide(impl->tag, tag);
    if (group)
        hstring_to_wide(impl->group, group);
    ReleaseSRWLockShared(&impl->lock);
}

BOOL toast_get_suppress_popup(IToastNotification *toast)
{
    return impl_from_IToastNotification(toast)->suppress_popup;
}

BOOL toast_get_expires_on_reboot(IToastNotification *toast)
{
    return impl_from_IToastNotification(toast)->expires_on_reboot;
}

INT64 toast_get_expiration(IToastNotification *toast)
{
    struct toast *impl = impl_from_IToastNotification(toast);
    DateTime expiration = {0};

    AcquireSRWLockShared(&impl->lock);
    if (impl->expiration && FAILED(IReference_DateTime_get_Value(impl->expiration, &expiration)))
        expiration.UniversalTime = 0;
    ReleaseSRWLockShared(&impl->lock);
    return expiration.UniversalTime;
}

HRESULT date_time_reference_create(INT64 value, IReference_DateTime **out)
{
    IPropertyValueStatics *statics;
    IInspectable *boxed;
    HSTRING class_name;
    DateTime date_time;
    HRESULT hr;

    *out = NULL;
    hr = WindowsCreateString(RuntimeClass_Windows_Foundation_PropertyValue,
                             wcslen(RuntimeClass_Windows_Foundation_PropertyValue), &class_name);
    if (FAILED(hr))
        return hr;
    hr = RoGetActivationFactory(class_name, &IID_IPropertyValueStatics, (void **)&statics);
    WindowsDeleteString(class_name);
    if (FAILED(hr))
        return hr;
    date_time.UniversalTime = value;
    hr = IPropertyValueStatics_CreateDateTime(statics, date_time, &boxed);
    IPropertyValueStatics_Release(statics);
    if (FAILED(hr))
        return hr;
    hr = IInspectable_QueryInterface(boxed, &IID_IReference_DateTime, (void **)out);
    IInspectable_Release(boxed);
    return hr;
}

HRESULT toast_restore(IXmlDocument *content, const WCHAR *tag, const WCHAR *group, INT64 expiration,
                      BOOL suppress_popup, INotificationData *data, IToastNotification **out)
{
    struct toast *impl;
    HRESULT hr;

    if (FAILED(hr = toast_create(content, out)))
        return hr;
    impl = impl_from_IToastNotification(*out);
    WindowsCreateString(tag, wcslen(tag), &impl->tag);
    WindowsCreateString(group, wcslen(group), &impl->group);
    impl->suppress_popup = suppress_popup;
    if (expiration)
        date_time_reference_create(expiration, &impl->expiration);
    if ((impl->data = data))
        INotificationData_AddRef(impl->data);
    return S_OK;
}

BOOL toast_is_expired(IToastNotification *toast)
{
    struct toast *impl = impl_from_IToastNotification(toast);
    DateTime expiration;
    FILETIME now;
    BOOL expired = FALSE;

    AcquireSRWLockShared(&impl->lock);
    if (impl->expiration && SUCCEEDED(IReference_DateTime_get_Value(impl->expiration, &expiration)))
    {
        GetSystemTimeAsFileTime(&now);
        expired = expiration.UniversalTime <= (INT64)(((UINT64)now.dwHighDateTime << 32) | now.dwLowDateTime);
    }
    ReleaseSRWLockShared(&impl->lock);
    return expired;
}

void toast_raise_dismissed(IToastNotification *toast, ToastDismissalReason reason)
{
    struct toast *impl = impl_from_IToastNotification(toast);
    struct dismissed_args *args;
    IUnknown **handlers;
    UINT32 count, i;

    if (!(count = event_list_snapshot(&impl->dismissed, &handlers)))
        return;
    if ((args = calloc(1, sizeof(*args))))
    {
        args->IToastDismissedEventArgs_iface.lpVtbl = (IToastDismissedEventArgsVtbl *)&dismissed_args_vtbl;
        args->ref = 1;
        args->reason = reason;
        for (i = 0; i < count; i++)
        {
            ITypedEventHandler_ToastNotification_ToastDismissedEventArgs_Invoke(
                (ITypedEventHandler_ToastNotification_ToastDismissedEventArgs *)handlers[i], toast,
                &args->IToastDismissedEventArgs_iface);
        }
        IToastDismissedEventArgs_Release(&args->IToastDismissedEventArgs_iface);
    }
    event_list_release_snapshot(handlers, count);
}

void toast_raise_activated(IToastNotification *toast, const WCHAR *arguments,
                           const NOTIFICATION_USER_INPUT_DATA *inputs, ULONG input_count)
{
    struct toast *impl = impl_from_IToastNotification(toast);
    struct activated_args *args;
    IUnknown **handlers;
    UINT32 count, i;

    if (!(count = event_list_snapshot(&impl->activated, &handlers)))
        return;
    if ((args = calloc(1, sizeof(*args))))
    {
        args->IToastActivatedEventArgs_iface.lpVtbl = (IToastActivatedEventArgsVtbl *)&activated_args_vtbl;
        args->IToastActivatedEventArgs2_iface.lpVtbl = (IToastActivatedEventArgs2Vtbl *)&activated_args2_vtbl;
        args->ref = 1;
        if (arguments)
            WindowsCreateString(arguments, wcslen(arguments), &args->arguments);
        create_user_input(inputs, input_count, &args->user_input);
        for (i = 0; i < count; i++)
        {
            ITypedEventHandler_ToastNotification_IInspectable_Invoke(
                (ITypedEventHandler_ToastNotification_IInspectable *)handlers[i], toast,
                (IInspectable *)&args->IToastActivatedEventArgs_iface);
        }
        IToastActivatedEventArgs_Release(&args->IToastActivatedEventArgs_iface);
    }
    event_list_release_snapshot(handlers, count);
}

void toast_raise_failed(IToastNotification *toast, HRESULT error)
{
    struct toast *impl = impl_from_IToastNotification(toast);
    struct failed_args *args;
    IUnknown **handlers;
    UINT32 count, i;

    if (!(count = event_list_snapshot(&impl->failed, &handlers)))
        return;
    if ((args = calloc(1, sizeof(*args))))
    {
        args->IToastFailedEventArgs_iface.lpVtbl = (IToastFailedEventArgsVtbl *)&failed_args_vtbl;
        args->ref = 1;
        args->error = error;
        for (i = 0; i < count; i++)
        {
            ITypedEventHandler_ToastNotification_ToastFailedEventArgs_Invoke(
                (ITypedEventHandler_ToastNotification_ToastFailedEventArgs *)handlers[i], toast,
                &args->IToastFailedEventArgs_iface);
        }
        IToastFailedEventArgs_Release(&args->IToastFailedEventArgs_iface);
    }
    event_list_release_snapshot(handlers, count);
}

struct toast_factory
{
    IActivationFactory IActivationFactory_iface;
    IToastNotificationFactory IToastNotificationFactory_iface;
    LONG ref;
};

static inline struct toast_factory *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct toast_factory, IActivationFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct toast_factory *impl = impl_from_IActivationFactory(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IActivationFactory))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotificationFactory))
        *out = &impl->IToastNotificationFactory_iface;
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
    return get_iids(&IID_IToastNotificationFactory, count, iids);
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *class_name)
{
    return toast_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *trust_level)
{
    return inspectable_trust_level(trust_level);
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

static inline struct toast_factory *impl_from_IToastNotificationFactory(IToastNotificationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct toast_factory, IToastNotificationFactory_iface);
}

static HRESULT WINAPI notification_factory_QueryInterface(IToastNotificationFactory *iface, REFIID iid, void **out)
{
    return factory_QueryInterface(&impl_from_IToastNotificationFactory(iface)->IActivationFactory_iface, iid, out);
}

static ULONG WINAPI notification_factory_AddRef(IToastNotificationFactory *iface)
{
    return factory_AddRef(&impl_from_IToastNotificationFactory(iface)->IActivationFactory_iface);
}

static ULONG WINAPI notification_factory_Release(IToastNotificationFactory *iface)
{
    return factory_Release(&impl_from_IToastNotificationFactory(iface)->IActivationFactory_iface);
}

static HRESULT WINAPI notification_factory_GetIids(IToastNotificationFactory *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastNotificationFactory, count, iids);
}

static HRESULT WINAPI notification_factory_GetRuntimeClassName(IToastNotificationFactory *iface, HSTRING *class_name)
{
    return toast_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI notification_factory_GetTrustLevel(IToastNotificationFactory *iface, TrustLevel *trust_level)
{
    return inspectable_trust_level(trust_level);
}

static HRESULT WINAPI notification_factory_CreateToastNotification(IToastNotificationFactory *iface,
        IXmlDocument *content, IToastNotification **value)
{
    if (!value)
        return E_POINTER;
    return toast_create(content, value);
}

static const IToastNotificationFactoryVtbl notification_factory_vtbl =
{
    notification_factory_QueryInterface,
    notification_factory_AddRef,
    notification_factory_Release,
    notification_factory_GetIids,
    notification_factory_GetRuntimeClassName,
    notification_factory_GetTrustLevel,
    notification_factory_CreateToastNotification,
};

static struct toast_factory toast_factory =
{
    {(IActivationFactoryVtbl *)&factory_vtbl},
    {(IToastNotificationFactoryVtbl *)&notification_factory_vtbl},
    1,
};

IActivationFactory *toast_notification_factory = &toast_factory.IActivationFactory_iface;
