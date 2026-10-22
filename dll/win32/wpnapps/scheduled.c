/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.UI.Notifications.ScheduledToastNotification
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

#define TICKS_PER_SECOND 10000000LL

struct scheduled
{
    IScheduledToastNotification IScheduledToastNotification_iface;
    IScheduledToastNotification2 IScheduledToastNotification2_iface;
    IScheduledToastNotification3 IScheduledToastNotification3_iface;
    IScheduledToastNotification4 IScheduledToastNotification4_iface;
    LONG ref;
    SRWLOCK lock;
    IXmlDocument *content;
    DateTime delivery_time;
    IReference_TimeSpan *snooze_interval;
    UINT32 maximum_snooze_count;
    HSTRING id;
    HSTRING tag;
    HSTRING group;
    HSTRING remote_id;
    boolean suppress_popup;
    NotificationMirroring mirroring;
    IReference_DateTime *expiration;
    WCHAR store_key[STORE_NAME_LENGTH];
};

static const IScheduledToastNotificationVtbl scheduled_vtbl;

static inline struct scheduled *impl_from_IScheduledToastNotification(IScheduledToastNotification *iface)
{
    return CONTAINING_RECORD(iface, struct scheduled, IScheduledToastNotification_iface);
}

static HRESULT replace_string(struct scheduled *impl, HSTRING *field, HSTRING value, UINT32 limit, HRESULT too_long)
{
    HSTRING copy, previous;
    HRESULT hr;

    if (limit && WindowsGetStringLen(value) > limit)
        return too_long;
    if (FAILED(hr = WindowsDuplicateString(value, &copy)))
        return hr;
    AcquireSRWLockExclusive(&impl->lock);
    previous = *field;
    *field = copy;
    ReleaseSRWLockExclusive(&impl->lock);
    WindowsDeleteString(previous);
    return S_OK;
}

static HRESULT read_string(struct scheduled *impl, HSTRING *field, HSTRING *value)
{
    HRESULT hr;

    if (!value)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    hr = WindowsDuplicateString(*field, value);
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT WINAPI scheduled_QueryInterface(IScheduledToastNotification *iface, REFIID iid, void **out)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IScheduledToastNotification))
        *out = &impl->IScheduledToastNotification_iface;
    else if (IsEqualGUID(iid, &IID_IScheduledToastNotification2))
        *out = &impl->IScheduledToastNotification2_iface;
    else if (IsEqualGUID(iid, &IID_IScheduledToastNotification3))
        *out = &impl->IScheduledToastNotification3_iface;
    else if (IsEqualGUID(iid, &IID_IScheduledToastNotification4))
        *out = &impl->IScheduledToastNotification4_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI scheduled_AddRef(IScheduledToastNotification *iface)
{
    return InterlockedIncrement(&impl_from_IScheduledToastNotification(iface)->ref);
}

static ULONG WINAPI scheduled_Release(IScheduledToastNotification *iface)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        IXmlDocument_Release(impl->content);
        if (impl->snooze_interval)
            IReference_TimeSpan_Release(impl->snooze_interval);
        if (impl->expiration)
            IReference_DateTime_Release(impl->expiration);
        WindowsDeleteString(impl->id);
        WindowsDeleteString(impl->tag);
        WindowsDeleteString(impl->group);
        WindowsDeleteString(impl->remote_id);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI scheduled_GetIids(IScheduledToastNotification *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IScheduledToastNotification, count, iids);
}

static HRESULT WINAPI scheduled_GetRuntimeClassName(IScheduledToastNotification *iface, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_UI_Notifications_ScheduledToastNotification,
                               wcslen(RuntimeClass_Windows_UI_Notifications_ScheduledToastNotification), class_name);
}

static HRESULT WINAPI scheduled_GetTrustLevel(IScheduledToastNotification *iface, TrustLevel *trust_level)
{
    if (!trust_level)
        return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI scheduled_get_Content(IScheduledToastNotification *iface, IXmlDocument **value)
{
    if (!value)
        return E_POINTER;
    *value = impl_from_IScheduledToastNotification(iface)->content;
    IXmlDocument_AddRef(*value);
    return S_OK;
}

static HRESULT WINAPI scheduled_get_DeliveryTime(IScheduledToastNotification *iface, DateTime *value)
{
    if (!value)
        return E_POINTER;
    *value = impl_from_IScheduledToastNotification(iface)->delivery_time;
    return S_OK;
}

static HRESULT WINAPI scheduled_get_SnoozeInterval(IScheduledToastNotification *iface, IReference_TimeSpan **value)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification(iface);

    if (!value)
        return E_POINTER;
    if ((*value = impl->snooze_interval))
        IReference_TimeSpan_AddRef(*value);
    return S_OK;
}

static HRESULT WINAPI scheduled_get_MaximumSnoozeCount(IScheduledToastNotification *iface, UINT32 *value)
{
    if (!value)
        return E_POINTER;
    *value = impl_from_IScheduledToastNotification(iface)->maximum_snooze_count;
    return S_OK;
}

static HRESULT WINAPI scheduled_put_Id(IScheduledToastNotification *iface, HSTRING value)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification(iface);

    return replace_string(impl, &impl->id, value, 0, S_OK);
}

static HRESULT WINAPI scheduled_get_Id(IScheduledToastNotification *iface, HSTRING *value)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification(iface);

    return read_string(impl, &impl->id, value);
}

static const IScheduledToastNotificationVtbl scheduled_vtbl =
{
    scheduled_QueryInterface,
    scheduled_AddRef,
    scheduled_Release,
    scheduled_GetIids,
    scheduled_GetRuntimeClassName,
    scheduled_GetTrustLevel,
    scheduled_get_Content,
    scheduled_get_DeliveryTime,
    scheduled_get_SnoozeInterval,
    scheduled_get_MaximumSnoozeCount,
    scheduled_put_Id,
    scheduled_get_Id,
};

#define DEFINE_SCHEDULED_FORWARDERS(prefix, iface_type, member)                                        \
    static inline struct scheduled *impl_from_##iface_type(iface_type *iface)                          \
    {                                                                                                  \
        return CONTAINING_RECORD(iface, struct scheduled, member);                                     \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_QueryInterface(iface_type *iface, REFIID iid, void **out)           \
    {                                                                                                  \
        return scheduled_QueryInterface(&impl_from_##iface_type(iface)->IScheduledToastNotification_iface, iid, out); \
    }                                                                                                  \
    static ULONG WINAPI prefix##_AddRef(iface_type *iface)                                             \
    {                                                                                                  \
        return scheduled_AddRef(&impl_from_##iface_type(iface)->IScheduledToastNotification_iface);    \
    }                                                                                                  \
    static ULONG WINAPI prefix##_Release(iface_type *iface)                                            \
    {                                                                                                  \
        return scheduled_Release(&impl_from_##iface_type(iface)->IScheduledToastNotification_iface);   \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_GetIids(iface_type *iface, ULONG *count, IID **iids)                \
    {                                                                                                  \
        return get_iids(&IID_##iface_type, count, iids);                                               \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_GetRuntimeClassName(iface_type *iface, HSTRING *class_name)         \
    {                                                                                                  \
        return scheduled_GetRuntimeClassName(NULL, class_name);                                        \
    }                                                                                                  \
    static HRESULT WINAPI prefix##_GetTrustLevel(iface_type *iface, TrustLevel *trust_level)           \
    {                                                                                                  \
        return scheduled_GetTrustLevel(NULL, trust_level);                                             \
    }

DEFINE_SCHEDULED_FORWARDERS(scheduled2, IScheduledToastNotification2, IScheduledToastNotification2_iface)

static HRESULT WINAPI scheduled2_put_Tag(IScheduledToastNotification2 *iface, HSTRING value)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification2(iface);

    return replace_string(impl, &impl->tag, value, TOAST_MAX_TAG_LENGTH, WPN_E_TAG_SIZE);
}

static HRESULT WINAPI scheduled2_get_Tag(IScheduledToastNotification2 *iface, HSTRING *value)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification2(iface);

    return read_string(impl, &impl->tag, value);
}

static HRESULT WINAPI scheduled2_put_Group(IScheduledToastNotification2 *iface, HSTRING value)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification2(iface);

    return replace_string(impl, &impl->group, value, TOAST_MAX_TAG_LENGTH, WPN_E_GROUP_SIZE);
}

static HRESULT WINAPI scheduled2_get_Group(IScheduledToastNotification2 *iface, HSTRING *value)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification2(iface);

    return read_string(impl, &impl->group, value);
}

static HRESULT WINAPI scheduled2_put_SuppressPopup(IScheduledToastNotification2 *iface, boolean value)
{
    impl_from_IScheduledToastNotification2(iface)->suppress_popup = value;
    return S_OK;
}

static HRESULT WINAPI scheduled2_get_SuppressPopup(IScheduledToastNotification2 *iface, boolean *value)
{
    if (!value)
        return E_POINTER;
    *value = impl_from_IScheduledToastNotification2(iface)->suppress_popup;
    return S_OK;
}

static const IScheduledToastNotification2Vtbl scheduled2_vtbl =
{
    scheduled2_QueryInterface,
    scheduled2_AddRef,
    scheduled2_Release,
    scheduled2_GetIids,
    scheduled2_GetRuntimeClassName,
    scheduled2_GetTrustLevel,
    scheduled2_put_Tag,
    scheduled2_get_Tag,
    scheduled2_put_Group,
    scheduled2_get_Group,
    scheduled2_put_SuppressPopup,
    scheduled2_get_SuppressPopup,
};

DEFINE_SCHEDULED_FORWARDERS(scheduled3, IScheduledToastNotification3, IScheduledToastNotification3_iface)

static HRESULT WINAPI scheduled3_get_NotificationMirroring(IScheduledToastNotification3 *iface,
                                                           NotificationMirroring *value)
{
    if (!value)
        return E_POINTER;
    *value = impl_from_IScheduledToastNotification3(iface)->mirroring;
    return S_OK;
}

static HRESULT WINAPI scheduled3_put_NotificationMirroring(IScheduledToastNotification3 *iface,
                                                           NotificationMirroring value)
{
    if (value != NotificationMirroring_Allowed && value != NotificationMirroring_Disabled)
        return E_INVALIDARG;
    impl_from_IScheduledToastNotification3(iface)->mirroring = value;
    return S_OK;
}

static HRESULT WINAPI scheduled3_get_RemoteId(IScheduledToastNotification3 *iface, HSTRING *value)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification3(iface);

    return read_string(impl, &impl->remote_id, value);
}

static HRESULT WINAPI scheduled3_put_RemoteId(IScheduledToastNotification3 *iface, HSTRING value)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification3(iface);

    return replace_string(impl, &impl->remote_id, value, 0, S_OK);
}

static const IScheduledToastNotification3Vtbl scheduled3_vtbl =
{
    scheduled3_QueryInterface,
    scheduled3_AddRef,
    scheduled3_Release,
    scheduled3_GetIids,
    scheduled3_GetRuntimeClassName,
    scheduled3_GetTrustLevel,
    scheduled3_get_NotificationMirroring,
    scheduled3_put_NotificationMirroring,
    scheduled3_get_RemoteId,
    scheduled3_put_RemoteId,
};

DEFINE_SCHEDULED_FORWARDERS(scheduled4, IScheduledToastNotification4, IScheduledToastNotification4_iface)

static HRESULT WINAPI scheduled4_get_ExpirationTime(IScheduledToastNotification4 *iface, IReference_DateTime **value)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification4(iface);

    if (!value)
        return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    if ((*value = impl->expiration))
        IReference_DateTime_AddRef(*value);
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}

static HRESULT WINAPI scheduled4_put_ExpirationTime(IScheduledToastNotification4 *iface, IReference_DateTime *value)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification4(iface);
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

static const IScheduledToastNotification4Vtbl scheduled4_vtbl =
{
    scheduled4_QueryInterface,
    scheduled4_AddRef,
    scheduled4_Release,
    scheduled4_GetIids,
    scheduled4_GetRuntimeClassName,
    scheduled4_GetTrustLevel,
    scheduled4_get_ExpirationTime,
    scheduled4_put_ExpirationTime,
};

static HRESULT scheduled_create(IXmlDocument *content, DateTime delivery_time, IReference_TimeSpan *snooze_interval,
                                UINT32 maximum_snooze_count, IScheduledToastNotification **out)
{
    struct scheduled *impl;

    *out = NULL;
    if (!content)
        return E_INVALIDARG;
    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IScheduledToastNotification_iface.lpVtbl = (IScheduledToastNotificationVtbl *)&scheduled_vtbl;
    impl->IScheduledToastNotification2_iface.lpVtbl = (IScheduledToastNotification2Vtbl *)&scheduled2_vtbl;
    impl->IScheduledToastNotification3_iface.lpVtbl = (IScheduledToastNotification3Vtbl *)&scheduled3_vtbl;
    impl->IScheduledToastNotification4_iface.lpVtbl = (IScheduledToastNotification4Vtbl *)&scheduled4_vtbl;
    impl->ref = 1;
    InitializeSRWLock(&impl->lock);
    impl->content = content;
    IXmlDocument_AddRef(impl->content);
    impl->delivery_time = delivery_time;
    if ((impl->snooze_interval = snooze_interval))
        IReference_TimeSpan_AddRef(snooze_interval);
    impl->maximum_snooze_count = maximum_snooze_count;
    impl->mirroring = NotificationMirroring_Allowed;
    *out = &impl->IScheduledToastNotification_iface;
    return S_OK;
}

HRESULT scheduled_toast_get_details(IScheduledToastNotification *scheduled, IXmlDocument **content,
                                    DateTime *delivery_time, WCHAR **tag, WCHAR **group, BOOL *suppress_popup)
{
    struct scheduled *impl;

    if (!scheduled || scheduled->lpVtbl != &scheduled_vtbl)
        return E_INVALIDARG;
    impl = impl_from_IScheduledToastNotification(scheduled);
    AcquireSRWLockShared(&impl->lock);
    *content = impl->content;
    IXmlDocument_AddRef(*content);
    *delivery_time = impl->delivery_time;
    hstring_to_wide(impl->tag, tag);
    hstring_to_wide(impl->group, group);
    *suppress_popup = impl->suppress_popup;
    ReleaseSRWLockShared(&impl->lock);
    if (!*tag || !*group)
    {
        free(*tag);
        free(*group);
        IXmlDocument_Release(*content);
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

void scheduled_toast_set_store_key(IScheduledToastNotification *scheduled, const WCHAR *key)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification(scheduled);

    AcquireSRWLockExclusive(&impl->lock);
    wcscpy(impl->store_key, key);
    ReleaseSRWLockExclusive(&impl->lock);
}

BOOL scheduled_toast_get_store_key(IScheduledToastNotification *scheduled, WCHAR *key)
{
    struct scheduled *impl;

    if (!scheduled || scheduled->lpVtbl != &scheduled_vtbl)
        return FALSE;
    impl = impl_from_IScheduledToastNotification(scheduled);
    AcquireSRWLockShared(&impl->lock);
    wcscpy(key, impl->store_key);
    ReleaseSRWLockShared(&impl->lock);
    return key[0] != 0;
}

HRESULT scheduled_toast_get_extra(IScheduledToastNotification *scheduled, WCHAR **id, INT64 *snooze_interval,
                                  UINT32 *maximum_snooze_count, INT64 *expiration)
{
    struct scheduled *impl = impl_from_IScheduledToastNotification(scheduled);
    TimeSpan interval = {0};
    DateTime date = {0};

    AcquireSRWLockShared(&impl->lock);
    hstring_to_wide(impl->id, id);
    if (impl->snooze_interval && FAILED(IReference_TimeSpan_get_Value(impl->snooze_interval, &interval)))
        interval.Duration = 0;
    if (impl->expiration && FAILED(IReference_DateTime_get_Value(impl->expiration, &date)))
        date.UniversalTime = 0;
    *maximum_snooze_count = impl->maximum_snooze_count;
    ReleaseSRWLockShared(&impl->lock);
    *snooze_interval = interval.Duration;
    *expiration = date.UniversalTime;
    return *id ? S_OK : E_OUTOFMEMORY;
}

static HRESULT time_span_reference_create(INT64 duration, IReference_TimeSpan **out)
{
    IPropertyValueStatics *statics;
    IInspectable *boxed;
    HSTRING class_name;
    TimeSpan span;
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
    span.Duration = duration;
    hr = IPropertyValueStatics_CreateTimeSpan(statics, span, &boxed);
    IPropertyValueStatics_Release(statics);
    if (FAILED(hr))
        return hr;
    hr = IInspectable_QueryInterface(boxed, &IID_IReference_TimeSpan, (void **)out);
    IInspectable_Release(boxed);
    return hr;
}

HRESULT scheduled_toast_restore(IXmlDocument *content, INT64 delivery_time, const WCHAR *tag, const WCHAR *group,
                                const WCHAR *id, INT64 snooze_interval, UINT32 maximum_snooze_count,
                                BOOL suppress_popup, INT64 expiration, const WCHAR *key,
                                IScheduledToastNotification **out)
{
    IReference_TimeSpan *interval = NULL;
    struct scheduled *impl;
    DateTime delivery;
    HRESULT hr;

    if (snooze_interval)
        time_span_reference_create(snooze_interval, &interval);
    delivery.UniversalTime = delivery_time;
    hr = scheduled_create(content, delivery, interval, maximum_snooze_count, out);
    if (interval)
        IReference_TimeSpan_Release(interval);
    if (FAILED(hr))
        return hr;
    impl = impl_from_IScheduledToastNotification(*out);
    WindowsCreateString(tag, wcslen(tag), &impl->tag);
    WindowsCreateString(group, wcslen(group), &impl->group);
    WindowsCreateString(id, wcslen(id), &impl->id);
    impl->suppress_popup = suppress_popup;
    if (expiration)
        date_time_reference_create(expiration, &impl->expiration);
    wcscpy(impl->store_key, key);
    return S_OK;
}

struct scheduled_factory
{
    IActivationFactory IActivationFactory_iface;
    IScheduledToastNotificationFactory IScheduledToastNotificationFactory_iface;
    LONG ref;
};

static inline struct scheduled_factory *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct scheduled_factory, IActivationFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct scheduled_factory *impl = impl_from_IActivationFactory(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IActivationFactory))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_IScheduledToastNotificationFactory))
        *out = &impl->IScheduledToastNotificationFactory_iface;
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
    return get_iids(&IID_IScheduledToastNotificationFactory, count, iids);
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *class_name)
{
    return scheduled_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *trust_level)
{
    return scheduled_GetTrustLevel(NULL, trust_level);
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

static inline struct scheduled_factory *impl_from_IScheduledToastNotificationFactory(
        IScheduledToastNotificationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct scheduled_factory, IScheduledToastNotificationFactory_iface);
}

static HRESULT WINAPI scheduled_factory_QueryInterface(IScheduledToastNotificationFactory *iface, REFIID iid, void **out)
{
    return factory_QueryInterface(&impl_from_IScheduledToastNotificationFactory(iface)->IActivationFactory_iface, iid, out);
}

static ULONG WINAPI scheduled_factory_AddRef(IScheduledToastNotificationFactory *iface)
{
    return factory_AddRef(&impl_from_IScheduledToastNotificationFactory(iface)->IActivationFactory_iface);
}

static ULONG WINAPI scheduled_factory_Release(IScheduledToastNotificationFactory *iface)
{
    return factory_Release(&impl_from_IScheduledToastNotificationFactory(iface)->IActivationFactory_iface);
}

static HRESULT WINAPI scheduled_factory_GetIids(IScheduledToastNotificationFactory *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IScheduledToastNotificationFactory, count, iids);
}

static HRESULT WINAPI scheduled_factory_GetRuntimeClassName(IScheduledToastNotificationFactory *iface,
                                                            HSTRING *class_name)
{
    return scheduled_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI scheduled_factory_GetTrustLevel(IScheduledToastNotificationFactory *iface,
                                                      TrustLevel *trust_level)
{
    return scheduled_GetTrustLevel(NULL, trust_level);
}

static HRESULT WINAPI scheduled_factory_CreateScheduledToastNotification(IScheduledToastNotificationFactory *iface,
        IXmlDocument *content, DateTime delivery_time, IScheduledToastNotification **value)
{
    if (!value)
        return E_POINTER;
    return scheduled_create(content, delivery_time, NULL, 0, value);
}

static HRESULT WINAPI scheduled_factory_CreateScheduledToastNotificationRecurring(
        IScheduledToastNotificationFactory *iface, IXmlDocument *content, DateTime delivery_time,
        TimeSpan snooze_interval, UINT32 maximum_snooze_count, IScheduledToastNotification **value)
{
    IPropertyValueStatics *statics;
    IReference_TimeSpan *interval;
    IInspectable *boxed;
    HSTRING class_name;
    HRESULT hr;

    if (!value)
        return E_POINTER;
    *value = NULL;
    if (snooze_interval.Duration < 60 * TICKS_PER_SECOND || snooze_interval.Duration > 3600 * TICKS_PER_SECOND ||
        maximum_snooze_count < 1 || maximum_snooze_count > 5)
        return E_INVALIDARG;
    hr = WindowsCreateString(RuntimeClass_Windows_Foundation_PropertyValue,
                             wcslen(RuntimeClass_Windows_Foundation_PropertyValue), &class_name);
    if (FAILED(hr))
        return hr;
    hr = RoGetActivationFactory(class_name, &IID_IPropertyValueStatics, (void **)&statics);
    WindowsDeleteString(class_name);
    if (FAILED(hr))
        return hr;
    hr = IPropertyValueStatics_CreateTimeSpan(statics, snooze_interval, &boxed);
    IPropertyValueStatics_Release(statics);
    if (FAILED(hr))
        return hr;
    hr = IInspectable_QueryInterface(boxed, &IID_IReference_TimeSpan, (void **)&interval);
    IInspectable_Release(boxed);
    if (FAILED(hr))
        return hr;
    hr = scheduled_create(content, delivery_time, interval, maximum_snooze_count, value);
    IReference_TimeSpan_Release(interval);
    return hr;
}

static const IScheduledToastNotificationFactoryVtbl scheduled_factory_vtbl =
{
    scheduled_factory_QueryInterface,
    scheduled_factory_AddRef,
    scheduled_factory_Release,
    scheduled_factory_GetIids,
    scheduled_factory_GetRuntimeClassName,
    scheduled_factory_GetTrustLevel,
    scheduled_factory_CreateScheduledToastNotification,
    scheduled_factory_CreateScheduledToastNotificationRecurring,
};

static struct scheduled_factory scheduled_factory =
{
    {(IActivationFactoryVtbl *)&factory_vtbl},
    {(IScheduledToastNotificationFactoryVtbl *)&scheduled_factory_vtbl},
    1,
};

IActivationFactory *scheduled_toast_factory = &scheduled_factory.IActivationFactory_iface;
