/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows.UI.Notifications.ToastNotifier, notification settings and scheduling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

static const WCHAR notification_settings_key[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings\\";
static const WCHAR push_notifications_key[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\PushNotifications";
static const WCHAR push_notifications_policy_key[] =
    L"Software\\Policies\\Microsoft\\Windows\\CurrentVersion\\PushNotifications";

struct notifier
{
    IToastNotifier IToastNotifier_iface;
    IToastNotifier2 IToastNotifier2_iface;
    IToastNotifier3 IToastNotifier3_iface;
    LONG ref;
    WCHAR *app_id;
    struct event_list scheduled_showing;
};

struct schedule_entry
{
    struct list entry;
    WCHAR *app_id;
    WCHAR key[STORE_NAME_LENGTH];
    IScheduledToastNotification *scheduled;
    struct notifier *notifier;
    PTP_TIMER timer;
};

static struct list schedule_entries = LIST_INIT(schedule_entries);
static SRWLOCK schedule_lock = SRWLOCK_INIT;

struct deferral
{
    IDeferral IDeferral_iface;
    IClosable IClosable_iface;
    LONG ref;
    HANDLE semaphore;
};

struct showing_args
{
    IScheduledToastNotificationShowingEventArgs IScheduledToastNotificationShowingEventArgs_iface;
    LONG ref;
    LONG pending;
    boolean cancel;
    HANDLE semaphore;
    IScheduledToastNotification *scheduled;
};

static void deferral_signal(struct deferral *impl)
{
    HANDLE semaphore = InterlockedExchangePointer(&impl->semaphore, NULL);

    if (semaphore)
    {
        ReleaseSemaphore(semaphore, 1, NULL);
        CloseHandle(semaphore);
    }
}

static DWORD read_policy_dword(HKEY root, const WCHAR *key, const WCHAR *value, DWORD fallback)
{
    DWORD data, size = sizeof(data);

    if (RegGetValueW(root, key, value, RRF_RT_REG_DWORD, NULL, &data, &size))
        return fallback;
    return data;
}

static WCHAR *settings_key_for(const WCHAR *app_id)
{
    size_t length = wcslen(notification_settings_key) + wcslen(app_id) + 1;
    WCHAR *key = malloc(length * sizeof(WCHAR));

    if (key)
    {
        wcscpy(key, notification_settings_key);
        wcscat(key, app_id);
    }
    return key;
}

void notification_register_app(const WCHAR *app_id)
{
    WCHAR *key = settings_key_for(app_id);
    HKEY handle;

    if (!key)
        return;
    if (!RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &handle, NULL))
        RegCloseKey(handle);
    free(key);
}

HRESULT notification_get_setting(const WCHAR *app_id, NotificationSetting *setting)
{
    WCHAR *key = settings_key_for(app_id);
    HKEY handle;
    LSTATUS status;

    if (!key)
        return E_OUTOFMEMORY;
    status = RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &handle);
    if (status)
    {
        free(key);
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    RegCloseKey(handle);

    if (read_policy_dword(HKEY_LOCAL_MACHINE, push_notifications_policy_key, L"NoToastApplicationNotification", 0) ||
        read_policy_dword(HKEY_CURRENT_USER, push_notifications_policy_key, L"NoToastApplicationNotification", 0))
        *setting = NotificationSetting_DisabledByGroupPolicy;
    else if (!read_policy_dword(HKEY_CURRENT_USER, push_notifications_key, L"ToastEnabled", 1))
        *setting = NotificationSetting_DisabledForUser;
    else if (!read_policy_dword(HKEY_CURRENT_USER, key, L"Enabled", 1))
        *setting = NotificationSetting_DisabledForApplication;
    else
        *setting = NotificationSetting_Enabled;
    free(key);
    return S_OK;
}

HRESULT notifier_show(const WCHAR *app_id, IToastNotification *toast)
{
    NotificationSetting setting;
    HRESULT hr;

    notification_register_app(app_id);
    if (SUCCEEDED(notification_get_setting(app_id, &setting)) && setting != NotificationSetting_Enabled)
        return S_OK;
    if (toast_is_expired(toast))
        return S_OK;
    hr = history_post(app_id, toast);
    if (FAILED(hr))
        return hr;
    if (!toast_get_suppress_popup(toast))
        host_ensure();
    return S_OK;
}

static inline struct deferral *impl_from_IDeferral(IDeferral *iface)
{
    return CONTAINING_RECORD(iface, struct deferral, IDeferral_iface);
}

static HRESULT WINAPI deferral_QueryInterface(IDeferral *iface, REFIID iid, void **out)
{
    struct deferral *impl = impl_from_IDeferral(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IDeferral))
        *out = &impl->IDeferral_iface;
    else if (IsEqualGUID(iid, &IID_IClosable))
        *out = &impl->IClosable_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI deferral_AddRef(IDeferral *iface)
{
    return InterlockedIncrement(&impl_from_IDeferral(iface)->ref);
}

static ULONG WINAPI deferral_Release(IDeferral *iface)
{
    struct deferral *impl = impl_from_IDeferral(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        deferral_signal(impl);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI deferral_GetIids(IDeferral *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IDeferral, count, iids);
}

static HRESULT WINAPI deferral_GetRuntimeClassName(IDeferral *iface, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_Foundation_Deferral,
                               wcslen(RuntimeClass_Windows_Foundation_Deferral), class_name);
}

static HRESULT WINAPI deferral_GetTrustLevel(IDeferral *iface, TrustLevel *trust_level)
{
    if (!trust_level)
        return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI deferral_Complete(IDeferral *iface)
{
    deferral_signal(impl_from_IDeferral(iface));
    return S_OK;
}

static const IDeferralVtbl deferral_vtbl =
{
    deferral_QueryInterface,
    deferral_AddRef,
    deferral_Release,
    deferral_GetIids,
    deferral_GetRuntimeClassName,
    deferral_GetTrustLevel,
    deferral_Complete,
};

static inline struct deferral *impl_from_IClosable(IClosable *iface)
{
    return CONTAINING_RECORD(iface, struct deferral, IClosable_iface);
}

static HRESULT WINAPI deferral_closable_QueryInterface(IClosable *iface, REFIID iid, void **out)
{
    return deferral_QueryInterface(&impl_from_IClosable(iface)->IDeferral_iface, iid, out);
}

static ULONG WINAPI deferral_closable_AddRef(IClosable *iface)
{
    return deferral_AddRef(&impl_from_IClosable(iface)->IDeferral_iface);
}

static ULONG WINAPI deferral_closable_Release(IClosable *iface)
{
    return deferral_Release(&impl_from_IClosable(iface)->IDeferral_iface);
}

static HRESULT WINAPI deferral_closable_GetIids(IClosable *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IClosable, count, iids);
}

static HRESULT WINAPI deferral_closable_GetRuntimeClassName(IClosable *iface, HSTRING *class_name)
{
    return deferral_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI deferral_closable_GetTrustLevel(IClosable *iface, TrustLevel *trust_level)
{
    return deferral_GetTrustLevel(NULL, trust_level);
}

static HRESULT WINAPI deferral_closable_Close(IClosable *iface)
{
    return deferral_Complete(&impl_from_IClosable(iface)->IDeferral_iface);
}

static const IClosableVtbl deferral_closable_vtbl =
{
    deferral_closable_QueryInterface,
    deferral_closable_AddRef,
    deferral_closable_Release,
    deferral_closable_GetIids,
    deferral_closable_GetRuntimeClassName,
    deferral_closable_GetTrustLevel,
    deferral_closable_Close,
};

static inline struct showing_args *impl_from_IScheduledToastNotificationShowingEventArgs(
        IScheduledToastNotificationShowingEventArgs *iface)
{
    return CONTAINING_RECORD(iface, struct showing_args, IScheduledToastNotificationShowingEventArgs_iface);
}

static HRESULT WINAPI showing_args_QueryInterface(IScheduledToastNotificationShowingEventArgs *iface,
                                                  REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IScheduledToastNotificationShowingEventArgs))
    {
        *out = iface;
        IScheduledToastNotificationShowingEventArgs_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI showing_args_AddRef(IScheduledToastNotificationShowingEventArgs *iface)
{
    return InterlockedIncrement(&impl_from_IScheduledToastNotificationShowingEventArgs(iface)->ref);
}

static ULONG WINAPI showing_args_Release(IScheduledToastNotificationShowingEventArgs *iface)
{
    struct showing_args *impl = impl_from_IScheduledToastNotificationShowingEventArgs(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        IScheduledToastNotification_Release(impl->scheduled);
        CloseHandle(impl->semaphore);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI showing_args_GetIids(IScheduledToastNotificationShowingEventArgs *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IScheduledToastNotificationShowingEventArgs, count, iids);
}

static HRESULT WINAPI showing_args_GetRuntimeClassName(IScheduledToastNotificationShowingEventArgs *iface,
                                                       HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_UI_Notifications_ScheduledToastNotificationShowingEventArgs,
                               wcslen(RuntimeClass_Windows_UI_Notifications_ScheduledToastNotificationShowingEventArgs),
                               class_name);
}

static HRESULT WINAPI showing_args_GetTrustLevel(IScheduledToastNotificationShowingEventArgs *iface,
                                                 TrustLevel *trust_level)
{
    if (!trust_level)
        return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI showing_args_get_Cancel(IScheduledToastNotificationShowingEventArgs *iface, boolean *value)
{
    if (!value)
        return E_POINTER;
    *value = impl_from_IScheduledToastNotificationShowingEventArgs(iface)->cancel;
    return S_OK;
}

static HRESULT WINAPI showing_args_put_Cancel(IScheduledToastNotificationShowingEventArgs *iface, boolean value)
{
    impl_from_IScheduledToastNotificationShowingEventArgs(iface)->cancel = value;
    return S_OK;
}

static HRESULT WINAPI showing_args_get_ScheduledToastNotification(IScheduledToastNotificationShowingEventArgs *iface,
                                                                  IScheduledToastNotification **value)
{
    struct showing_args *impl = impl_from_IScheduledToastNotificationShowingEventArgs(iface);

    if (!value)
        return E_POINTER;
    *value = impl->scheduled;
    IScheduledToastNotification_AddRef(*value);
    return S_OK;
}

static HRESULT WINAPI showing_args_GetDeferral(IScheduledToastNotificationShowingEventArgs *iface, IDeferral **result)
{
    struct showing_args *impl = impl_from_IScheduledToastNotificationShowingEventArgs(iface);
    struct deferral *deferral;

    if (!result)
        return E_POINTER;
    *result = NULL;
    if (!(deferral = calloc(1, sizeof(*deferral))))
        return E_OUTOFMEMORY;
    deferral->IDeferral_iface.lpVtbl = (IDeferralVtbl *)&deferral_vtbl;
    deferral->IClosable_iface.lpVtbl = (IClosableVtbl *)&deferral_closable_vtbl;
    deferral->ref = 1;
    if (!DuplicateHandle(GetCurrentProcess(), impl->semaphore, GetCurrentProcess(), &deferral->semaphore,
                         0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        free(deferral);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    InterlockedIncrement(&impl->pending);
    *result = &deferral->IDeferral_iface;
    return S_OK;
}

static const IScheduledToastNotificationShowingEventArgsVtbl showing_args_vtbl =
{
    showing_args_QueryInterface,
    showing_args_AddRef,
    showing_args_Release,
    showing_args_GetIids,
    showing_args_GetRuntimeClassName,
    showing_args_GetTrustLevel,
    showing_args_get_Cancel,
    showing_args_put_Cancel,
    showing_args_get_ScheduledToastNotification,
    showing_args_GetDeferral,
};

static BOOL raise_scheduled_showing(struct notifier *notifier, IScheduledToastNotification *scheduled)
{
    struct showing_args *args;
    IUnknown **handlers;
    UINT32 count, i;
    BOOL cancel;

    if (!(count = event_list_snapshot(&notifier->scheduled_showing, &handlers)))
        return FALSE;
    if (!(args = calloc(1, sizeof(*args))))
    {
        event_list_release_snapshot(handlers, count);
        return FALSE;
    }
    args->IScheduledToastNotificationShowingEventArgs_iface.lpVtbl =
        (IScheduledToastNotificationShowingEventArgsVtbl *)&showing_args_vtbl;
    args->ref = 1;
    if (!(args->semaphore = CreateSemaphoreW(NULL, 0, MAXLONG, NULL)))
    {
        free(args);
        event_list_release_snapshot(handlers, count);
        return FALSE;
    }
    args->scheduled = scheduled;
    IScheduledToastNotification_AddRef(args->scheduled);
    for (i = 0; i < count; i++)
    {
        ITypedEventHandler_ToastNotifier_ScheduledToastNotificationShowingEventArgs_Invoke(
            (ITypedEventHandler_ToastNotifier_ScheduledToastNotificationShowingEventArgs *)handlers[i],
            &notifier->IToastNotifier_iface, &args->IScheduledToastNotificationShowingEventArgs_iface);
    }
    event_list_release_snapshot(handlers, count);
    for (i = InterlockedCompareExchange(&args->pending, 0, 0); i; i--)
        WaitForSingleObject(args->semaphore, INFINITE);
    cancel = args->cancel;
    IScheduledToastNotificationShowingEventArgs_Release(&args->IScheduledToastNotificationShowingEventArgs_iface);
    return cancel;
}

static void schedule_free_entry(struct schedule_entry *entry)
{
    IScheduledToastNotification_Release(entry->scheduled);
    IToastNotifier_Release(&entry->notifier->IToastNotifier_iface);
    free(entry->app_id);
    free(entry);
}

static void CALLBACK schedule_timer_callback(PTP_CALLBACK_INSTANCE instance, void *context, PTP_TIMER timer)
{
    struct schedule_entry *entry = context, *current;
    BOOL found = FALSE;

    AcquireSRWLockExclusive(&schedule_lock);
    LIST_FOR_EACH_ENTRY(current, &schedule_entries, struct schedule_entry, entry)
    {
        if (current == entry)
        {
            list_remove(&entry->entry);
            found = TRUE;
            break;
        }
    }
    ReleaseSRWLockExclusive(&schedule_lock);
    if (!found)
        return;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (schedule_store_claim(entry->app_id, entry->key, FALSE) &&
        !raise_scheduled_showing(entry->notifier, entry->scheduled))
        schedule_deliver(entry->app_id, entry->scheduled);
    CoUninitialize();
    CloseThreadpoolTimer(timer);
    entry->timer = NULL;
    schedule_free_entry(entry);
}

static inline struct notifier *impl_from_IToastNotifier(IToastNotifier *iface)
{
    return CONTAINING_RECORD(iface, struct notifier, IToastNotifier_iface);
}

static HRESULT WINAPI notifier_QueryInterface(IToastNotifier *iface, REFIID iid, void **out)
{
    struct notifier *impl = impl_from_IToastNotifier(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IToastNotifier))
        *out = &impl->IToastNotifier_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotifier2))
        *out = &impl->IToastNotifier2_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotifier3))
        *out = &impl->IToastNotifier3_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI notifier_AddRef(IToastNotifier *iface)
{
    return InterlockedIncrement(&impl_from_IToastNotifier(iface)->ref);
}

static ULONG WINAPI notifier_Release(IToastNotifier *iface)
{
    struct notifier *impl = impl_from_IToastNotifier(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        event_list_clear(&impl->scheduled_showing);
        free(impl->app_id);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI notifier_GetIids(IToastNotifier *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastNotifier, count, iids);
}

static HRESULT WINAPI notifier_GetRuntimeClassName(IToastNotifier *iface, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_UI_Notifications_ToastNotifier,
                               wcslen(RuntimeClass_Windows_UI_Notifications_ToastNotifier), class_name);
}

static HRESULT WINAPI notifier_GetTrustLevel(IToastNotifier *iface, TrustLevel *trust_level)
{
    if (!trust_level)
        return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI notifier_Show(IToastNotifier *iface, IToastNotification *notification)
{
    struct notifier *impl = impl_from_IToastNotifier(iface);

    if (!toast_is_ours(notification))
        return E_INVALIDARG;
    return notifier_show(impl->app_id, notification);
}

static HRESULT WINAPI notifier_Hide(IToastNotifier *iface, IToastNotification *notification)
{
    if (!toast_is_ours(notification))
        return E_INVALIDARG;
    if (history_remove(notification))
        toast_raise_dismissed(notification, ToastDismissalReason_ApplicationHidden);
    return S_OK;
}

static HRESULT WINAPI notifier_get_Setting(IToastNotifier *iface, NotificationSetting *value)
{
    if (!value)
        return E_POINTER;
    return notification_get_setting(impl_from_IToastNotifier(iface)->app_id, value);
}

static HRESULT WINAPI notifier_AddToSchedule(IToastNotifier *iface, IScheduledToastNotification *scheduled)
{
    struct notifier *impl = impl_from_IToastNotifier(iface);
    struct schedule_entry *entry;
    IXmlDocument *content;
    DateTime delivery;
    WCHAR *tag, *group;
    BOOL suppress;
    ULARGE_INTEGER due;
    FILETIME due_time;
    HRESULT hr;

    if (!scheduled)
        return E_INVALIDARG;
    hr = scheduled_toast_get_details(scheduled, &content, &delivery, &tag, &group, &suppress);
    if (FAILED(hr))
        return E_INVALIDARG;
    IXmlDocument_Release(content);
    free(tag);
    free(group);

    if (!(entry = calloc(1, sizeof(*entry))) || !(entry->app_id = wcsdup(impl->app_id)))
    {
        free(entry);
        return E_OUTOFMEMORY;
    }
    entry->scheduled = scheduled;
    IScheduledToastNotification_AddRef(entry->scheduled);
    IToastNotifier_AddRef(iface);
    entry->notifier = impl;
    if (!(entry->timer = CreateThreadpoolTimer(schedule_timer_callback, entry, NULL)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        schedule_free_entry(entry);
        return hr;
    }
    if (FAILED(hr = schedule_store_add(impl->app_id, scheduled, entry->key)))
    {
        CloseThreadpoolTimer(entry->timer);
        schedule_free_entry(entry);
        return hr;
    }
    scheduled_toast_set_store_key(scheduled, entry->key);
    notification_register_app(impl->app_id);
    AcquireSRWLockExclusive(&schedule_lock);
    list_add_tail(&schedule_entries, &entry->entry);
    ReleaseSRWLockExclusive(&schedule_lock);
    due.QuadPart = (ULONGLONG)delivery.UniversalTime;
    due_time.dwLowDateTime = due.LowPart;
    due_time.dwHighDateTime = due.HighPart;
    SetThreadpoolTimer(entry->timer, &due_time, 0, 0);
    host_ensure();
    return S_OK;
}

static HRESULT WINAPI notifier_RemoveFromSchedule(IToastNotifier *iface, IScheduledToastNotification *scheduled)
{
    struct notifier *impl = impl_from_IToastNotifier(iface);
    struct schedule_entry *entry, *found = NULL;
    WCHAR key[STORE_NAME_LENGTH];
    BOOL removed;

    if (!scheduled)
        return E_INVALIDARG;
    if (!scheduled_toast_get_store_key(scheduled, key))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    AcquireSRWLockExclusive(&schedule_lock);
    LIST_FOR_EACH_ENTRY(entry, &schedule_entries, struct schedule_entry, entry)
    {
        if (!wcscmp(entry->key, key))
        {
            found = entry;
            list_remove(&entry->entry);
            break;
        }
    }
    ReleaseSRWLockExclusive(&schedule_lock);
    removed = schedule_store_remove(impl->app_id, key);
    if (found)
    {
        SetThreadpoolTimer(found->timer, NULL, 0, 0);
        WaitForThreadpoolTimerCallbacks(found->timer, TRUE);
        CloseThreadpoolTimer(found->timer);
        schedule_free_entry(found);
    }
    return removed ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

static HRESULT WINAPI notifier_GetScheduledToastNotifications(IToastNotifier *iface,
        IVectorView_ScheduledToastNotification **result)
{
    struct notifier *impl = impl_from_IToastNotifier(iface);
    WCHAR (*names)[STORE_NAME_LENGTH];
    struct schedule_entry *entry;
    IInspectable **items = NULL;
    UINT32 count, found = 0, i;
    HRESULT hr;

    if (!result)
        return E_POINTER;
    *result = NULL;
    count = schedule_store_list(impl->app_id, &names);
    if (count && !(items = calloc(count, sizeof(*items))))
    {
        free(names);
        return E_OUTOFMEMORY;
    }
    for (i = 0; i < count; i++)
    {
        items[found] = NULL;
        AcquireSRWLockShared(&schedule_lock);
        LIST_FOR_EACH_ENTRY(entry, &schedule_entries, struct schedule_entry, entry)
        {
            if (!wcscmp(entry->key, names[i]))
            {
                items[found] = (IInspectable *)entry->scheduled;
                IInspectable_AddRef(items[found]);
                break;
            }
        }
        ReleaseSRWLockShared(&schedule_lock);
        if (!items[found])
            items[found] = (IInspectable *)schedule_store_restore(impl->app_id, names[i]);
        if (items[found])
            found++;
    }
    hr = vector_view_create(&IID_IVectorView_ScheduledToastNotification, &IID_IIterable_ScheduledToastNotification,
                            &IID_IIterator_ScheduledToastNotification, items, found, (void **)result);
    for (i = 0; i < found; i++)
        IInspectable_Release(items[i]);
    free(items);
    free(names);
    return hr;
}

static const IToastNotifierVtbl notifier_vtbl =
{
    notifier_QueryInterface,
    notifier_AddRef,
    notifier_Release,
    notifier_GetIids,
    notifier_GetRuntimeClassName,
    notifier_GetTrustLevel,
    notifier_Show,
    notifier_Hide,
    notifier_get_Setting,
    notifier_AddToSchedule,
    notifier_RemoveFromSchedule,
    notifier_GetScheduledToastNotifications,
};

static inline struct notifier *impl_from_IToastNotifier2(IToastNotifier2 *iface)
{
    return CONTAINING_RECORD(iface, struct notifier, IToastNotifier2_iface);
}

static HRESULT WINAPI notifier2_QueryInterface(IToastNotifier2 *iface, REFIID iid, void **out)
{
    return notifier_QueryInterface(&impl_from_IToastNotifier2(iface)->IToastNotifier_iface, iid, out);
}

static ULONG WINAPI notifier2_AddRef(IToastNotifier2 *iface)
{
    return notifier_AddRef(&impl_from_IToastNotifier2(iface)->IToastNotifier_iface);
}

static ULONG WINAPI notifier2_Release(IToastNotifier2 *iface)
{
    return notifier_Release(&impl_from_IToastNotifier2(iface)->IToastNotifier_iface);
}

static HRESULT WINAPI notifier2_GetIids(IToastNotifier2 *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastNotifier2, count, iids);
}

static HRESULT WINAPI notifier2_GetRuntimeClassName(IToastNotifier2 *iface, HSTRING *class_name)
{
    return notifier_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI notifier2_GetTrustLevel(IToastNotifier2 *iface, TrustLevel *trust_level)
{
    return notifier_GetTrustLevel(NULL, trust_level);
}

static HRESULT update_toast(struct notifier *impl, INotificationData *data, HSTRING tag, HSTRING group,
                            NotificationUpdateResult *result)
{
    WCHAR *tag_text, *group_text = NULL;
    HRESULT hr;

    if (!result)
        return E_POINTER;
    if (!tag)
        return E_INVALIDARG;
    if (FAILED(hr = hstring_to_wide(tag, &tag_text)))
        return hr;
    if (FAILED(hr = hstring_to_wide(group, &group_text)))
    {
        free(tag_text);
        return hr;
    }
    hr = history_update(impl->app_id, tag_text, group_text, data, result);
    free(tag_text);
    free(group_text);
    return hr;
}

static HRESULT WINAPI notifier2_UpdateWithTagAndGroup(IToastNotifier2 *iface, INotificationData *data, HSTRING tag,
                                                      HSTRING group, NotificationUpdateResult *result)
{
    return update_toast(impl_from_IToastNotifier2(iface), data, tag, group, result);
}

static HRESULT WINAPI notifier2_UpdateWithTag(IToastNotifier2 *iface, INotificationData *data, HSTRING tag,
                                              NotificationUpdateResult *result)
{
    return update_toast(impl_from_IToastNotifier2(iface), data, tag, NULL, result);
}

static const IToastNotifier2Vtbl notifier2_vtbl =
{
    notifier2_QueryInterface,
    notifier2_AddRef,
    notifier2_Release,
    notifier2_GetIids,
    notifier2_GetRuntimeClassName,
    notifier2_GetTrustLevel,
    notifier2_UpdateWithTagAndGroup,
    notifier2_UpdateWithTag,
};

static inline struct notifier *impl_from_IToastNotifier3(IToastNotifier3 *iface)
{
    return CONTAINING_RECORD(iface, struct notifier, IToastNotifier3_iface);
}

static HRESULT WINAPI notifier3_QueryInterface(IToastNotifier3 *iface, REFIID iid, void **out)
{
    return notifier_QueryInterface(&impl_from_IToastNotifier3(iface)->IToastNotifier_iface, iid, out);
}

static ULONG WINAPI notifier3_AddRef(IToastNotifier3 *iface)
{
    return notifier_AddRef(&impl_from_IToastNotifier3(iface)->IToastNotifier_iface);
}

static ULONG WINAPI notifier3_Release(IToastNotifier3 *iface)
{
    return notifier_Release(&impl_from_IToastNotifier3(iface)->IToastNotifier_iface);
}

static HRESULT WINAPI notifier3_GetIids(IToastNotifier3 *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastNotifier3, count, iids);
}

static HRESULT WINAPI notifier3_GetRuntimeClassName(IToastNotifier3 *iface, HSTRING *class_name)
{
    return notifier_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI notifier3_GetTrustLevel(IToastNotifier3 *iface, TrustLevel *trust_level)
{
    return notifier_GetTrustLevel(NULL, trust_level);
}

static HRESULT WINAPI notifier3_add_ScheduledToastNotificationShowing(IToastNotifier3 *iface,
        ITypedEventHandler_ToastNotifier_ScheduledToastNotificationShowingEventArgs *handler,
        EventRegistrationToken *token)
{
    return event_list_add(&impl_from_IToastNotifier3(iface)->scheduled_showing, handler,
                          &IID_ITypedEventHandler_ToastNotifier_ScheduledToastNotificationShowingEventArgs, token);
}

static HRESULT WINAPI notifier3_remove_ScheduledToastNotificationShowing(IToastNotifier3 *iface,
                                                                         EventRegistrationToken token)
{
    return event_list_remove(&impl_from_IToastNotifier3(iface)->scheduled_showing, token);
}

static const IToastNotifier3Vtbl notifier3_vtbl =
{
    notifier3_QueryInterface,
    notifier3_AddRef,
    notifier3_Release,
    notifier3_GetIids,
    notifier3_GetRuntimeClassName,
    notifier3_GetTrustLevel,
    notifier3_add_ScheduledToastNotificationShowing,
    notifier3_remove_ScheduledToastNotificationShowing,
};

HRESULT notifier_create(const WCHAR *app_id, IToastNotifier **out)
{
    struct notifier *impl;

    *out = NULL;
    if (!(impl = calloc(1, sizeof(*impl))) || !(impl->app_id = wcsdup(app_id)))
    {
        free(impl);
        return E_OUTOFMEMORY;
    }
    impl->IToastNotifier_iface.lpVtbl = (IToastNotifierVtbl *)&notifier_vtbl;
    impl->IToastNotifier2_iface.lpVtbl = (IToastNotifier2Vtbl *)&notifier2_vtbl;
    impl->IToastNotifier3_iface.lpVtbl = (IToastNotifier3Vtbl *)&notifier3_vtbl;
    impl->ref = 1;
    event_list_init(&impl->scheduled_showing);
    *out = &impl->IToastNotifier_iface;
    return S_OK;
}
