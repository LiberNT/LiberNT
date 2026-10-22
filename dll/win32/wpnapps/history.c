/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Toast notification history shared by every process of the user
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

#define HISTORY_DEFAULT_LIFETIME (3LL * 24 * 60 * 60 * 10000000)

struct history_entry
{
    struct list entry;
    WCHAR *app_id;
    WCHAR key[STORE_NAME_LENGTH];
    IToastNotification *toast;
};

static struct list history_entries = LIST_INIT(history_entries);
static SRWLOCK history_lock = SRWLOCK_INIT;

static LSTATUS open_app_key(const WCHAR *app_id, BOOL create, HKEY *key)
{
    return store_open_app(L"History", app_id, create, key);
}

static BOOL entry_matches(HKEY entry, const WCHAR *tag, const WCHAR *group)
{
    WCHAR *value;
    BOOL match = TRUE;

    if (tag)
    {
        value = store_read_string(entry, L"Tag");
        match = value && !wcscmp(value, tag);
        free(value);
    }
    if (match && group)
    {
        value = store_read_string(entry, L"Group");
        match = value && !wcscmp(value, group);
        free(value);
    }
    return match;
}

static BOOL entry_expired(HKEY entry, INT64 now)
{
    INT64 expiration = (INT64)store_read_number(entry, L"Expiration");
    INT64 purge = (INT64)store_read_number(entry, L"Purge");

    return (expiration && expiration <= now) || (purge && purge <= now);
}

static void remove_entries(HKEY app, const WCHAR *tag, const WCHAR *group, BOOL expired_only)
{
    WCHAR (*names)[STORE_NAME_LENGTH];
    INT64 now = store_now();
    UINT32 count, i;
    BOOL remove;
    HKEY entry;

    count = store_list(app, &names);
    for (i = 0; i < count; i++)
    {
        if (RegOpenKeyExW(app, names[i], 0, KEY_READ, &entry))
            continue;
        remove = entry_expired(entry, now) || (!expired_only && entry_matches(entry, tag, group));
        RegCloseKey(entry);
        if (remove)
            RegDeleteTreeW(app, names[i]);
    }
    free(names);
}

static void write_data(HKEY entry, INotificationData *data)
{
    IIterable_IKeyValuePair_HSTRING_HSTRING *iterable;
    IIterator_IKeyValuePair_HSTRING_HSTRING *iterator;
    IKeyValuePair_HSTRING_HSTRING *pair;
    IMap_HSTRING_HSTRING *values;
    boolean has_current = FALSE;
    UINT32 sequence = 0;
    HSTRING key, value;
    HKEY data_key;

    RegDeleteTreeW(entry, L"Data");
    if (RegCreateKeyExW(entry, L"Data", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &data_key, NULL))
        return;
    if (SUCCEEDED(INotificationData_get_Values(data, &values)))
    {
        if (SUCCEEDED(IMap_HSTRING_HSTRING_QueryInterface(values, &IID_IIterable_IKeyValuePair_HSTRING_HSTRING,
                                                          (void **)&iterable)))
        {
            if (SUCCEEDED(IIterable_IKeyValuePair_HSTRING_HSTRING_First(iterable, &iterator)))
            {
                IIterator_IKeyValuePair_HSTRING_HSTRING_get_HasCurrent(iterator, &has_current);
                while (has_current)
                {
                    if (SUCCEEDED(IIterator_IKeyValuePair_HSTRING_HSTRING_get_Current(iterator, &pair)))
                    {
                        key = value = NULL;
                        if (SUCCEEDED(IKeyValuePair_HSTRING_HSTRING_get_Key(pair, &key)) &&
                            SUCCEEDED(IKeyValuePair_HSTRING_HSTRING_get_Value(pair, &value)))
                        {
                            store_write_string(data_key, WindowsGetStringRawBuffer(key, NULL),
                                         WindowsGetStringRawBuffer(value, NULL));
                        }
                        WindowsDeleteString(key);
                        WindowsDeleteString(value);
                        IKeyValuePair_HSTRING_HSTRING_Release(pair);
                    }
                    if (FAILED(IIterator_IKeyValuePair_HSTRING_HSTRING_MoveNext(iterator, &has_current)))
                        break;
                }
                IIterator_IKeyValuePair_HSTRING_HSTRING_Release(iterator);
            }
            IIterable_IKeyValuePair_HSTRING_HSTRING_Release(iterable);
        }
        IMap_HSTRING_HSTRING_Release(values);
    }
    RegCloseKey(data_key);
    INotificationData_get_SequenceNumber(data, &sequence);
    store_write_dword(entry, L"DataSequence", sequence);
    store_write_dword(entry, L"DataRevision", (DWORD)store_read_number(entry, L"DataRevision") + 1);
}

static INotificationData *read_data(HKEY entry)
{
    DWORD name_length, value_size, type, index;
    INotificationData *data = NULL;
    IMap_HSTRING_HSTRING *values;
    WCHAR name[256], *value;
    HSTRING key, text;
    boolean replaced;
    HKEY data_key;

    if (RegOpenKeyExW(entry, L"Data", 0, KEY_READ, &data_key))
        return NULL;
    if (SUCCEEDED(notification_data_create(NULL, (UINT32)store_read_number(entry, L"DataSequence"), &data)) &&
        SUCCEEDED(INotificationData_get_Values(data, &values)))
    {
        for (index = 0;; index++)
        {
            name_length = ARRAYSIZE(name);
            value_size = 0;
            if (RegEnumValueW(data_key, index, name, &name_length, NULL, &type, NULL, &value_size))
                break;
            if (type != REG_SZ || !(value = calloc(value_size / sizeof(WCHAR) + 1, sizeof(WCHAR))))
                continue;
            name_length = ARRAYSIZE(name);
            if (!RegEnumValueW(data_key, index, name, &name_length, NULL, NULL, (BYTE *)value, &value_size) &&
                SUCCEEDED(WindowsCreateString(name, name_length, &key)))
            {
                if (SUCCEEDED(WindowsCreateString(value, wcslen(value), &text)))
                {
                    IMap_HSTRING_HSTRING_Insert(values, key, text, &replaced);
                    WindowsDeleteString(text);
                }
                WindowsDeleteString(key);
            }
            free(value);
        }
        IMap_HSTRING_HSTRING_Release(values);
    }
    RegCloseKey(data_key);
    return data;
}

static BOOL store_exists(const WCHAR *app_id, const WCHAR *name)
{
    HKEY app, entry;
    BOOL exists = FALSE;

    if (open_app_key(app_id, FALSE, &app))
        return FALSE;
    if (!RegOpenKeyExW(app, name, 0, KEY_READ, &entry))
    {
        exists = !entry_expired(entry, store_now());
        RegCloseKey(entry);
    }
    RegCloseKey(app);
    return exists;
}

static BOOL store_delete(const WCHAR *app_id, const WCHAR *name)
{
    HKEY app;
    BOOL deleted;

    if (open_app_key(app_id, FALSE, &app))
        return FALSE;
    deleted = !RegDeleteTreeW(app, name);
    RegCloseKey(app);
    return deleted;
}

static void history_free_entry(struct history_entry *entry)
{
    IToastNotification_Release(entry->toast);
    free(entry->app_id);
    free(entry);
}

static void history_release_removed(struct list *removed)
{
    struct history_entry *entry, *next;

    LIST_FOR_EACH_ENTRY_SAFE(entry, next, removed, struct history_entry, entry)
    {
        list_remove(&entry->entry);
        history_free_entry(entry);
    }
}

static struct history_entry *find_local(IToastNotification *toast)
{
    struct history_entry *entry;

    LIST_FOR_EACH_ENTRY(entry, &history_entries, struct history_entry, entry)
    {
        if (entry->toast == toast)
            return entry;
    }
    return NULL;
}

static IToastNotification *find_local_key(const WCHAR *key)
{
    struct history_entry *entry;
    IToastNotification *toast = NULL;

    AcquireSRWLockShared(&history_lock);
    LIST_FOR_EACH_ENTRY(entry, &history_entries, struct history_entry, entry)
    {
        if (!wcscmp(entry->key, key))
        {
            toast = entry->toast;
            IToastNotification_AddRef(toast);
            break;
        }
    }
    ReleaseSRWLockShared(&history_lock);
    return toast;
}

void history_sync(void)
{
    struct history_entry *entry, *next;
    struct list removed = LIST_INIT(removed);
    WCHAR (*keys)[STORE_NAME_LENGTH] = NULL;
    WCHAR **apps = NULL;
    BOOL *missing = NULL;
    UINT32 count = 0, i;

    AcquireSRWLockShared(&history_lock);
    if ((count = list_count(&history_entries)) &&
        (!(keys = calloc(count, sizeof(*keys))) || !(apps = calloc(count, sizeof(*apps))) ||
         !(missing = calloc(count, sizeof(*missing)))))
        count = 0;
    i = 0;
    LIST_FOR_EACH_ENTRY(entry, &history_entries, struct history_entry, entry)
    {
        if (i >= count)
            break;
        wcscpy(keys[i], entry->key);
        apps[i++] = wcsdup(entry->app_id);
    }
    ReleaseSRWLockShared(&history_lock);
    for (i = 0; i < count; i++)
        missing[i] = apps[i] && !store_exists(apps[i], keys[i]);

    AcquireSRWLockExclusive(&history_lock);
    LIST_FOR_EACH_ENTRY_SAFE(entry, next, &history_entries, struct history_entry, entry)
    {
        for (i = 0; i < count; i++)
        {
            if (missing[i] && !wcscmp(entry->key, keys[i]))
            {
                list_remove(&entry->entry);
                list_add_tail(&removed, &entry->entry);
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&history_lock);
    for (i = 0; i < count; i++)
        free(apps[i]);
    free(apps);
    free(keys);
    free(missing);
    history_release_removed(&removed);
}

HRESULT history_store_post(const WCHAR *app_id, const struct history_record *record, WCHAR *name)
{
    HKEY app, key;
    LSTATUS status;

    if ((status = open_app_key(app_id, TRUE, &app)))
        return HRESULT_FROM_WIN32(status);
    remove_entries(app, record->tag[0] ? record->tag : NULL, record->group, !record->tag[0]);
    store_new_name(name);
    status = RegCreateKeyExW(app, name, 0, NULL, record->expires_on_reboot ? REG_OPTION_VOLATILE : 0, KEY_ALL_ACCESS,
                             NULL, &key, NULL);
    if (!status)
    {
        store_write_string(key, L"Content", record->xml);
        store_write_string(key, L"Tag", record->tag);
        store_write_string(key, L"Group", record->group);
        if (record->expiration)
            store_write_qword(key, L"Expiration", record->expiration);
        store_write_qword(key, L"Purge", (UINT64)(store_now() + HISTORY_DEFAULT_LIFETIME));
        store_write_dword(key, L"SuppressPopup", record->suppress_popup);
        store_write_dword(key, L"Scheduled", record->scheduled);
        if (record->data)
            write_data(key, record->data);
        store_write_dword(key, L"Popup", !record->suppress_popup);
        RegCloseKey(key);
    }
    RegCloseKey(app);
    return HRESULT_FROM_WIN32(status);
}

static DWORD WINAPI listener_thread(void *context)
{
    HANDLE event = context;
    HKEY root = NULL;
    HRESULT init;

    init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    while (SUCCEEDED(store_watch(event, &root)))
    {
        history_dispatch_events();
        history_sync();
        WaitForSingleObject(event, INFINITE);
    }
    if (root)
        RegCloseKey(root);
    CloseHandle(event);
    if (SUCCEEDED(init))
        CoUninitialize();
    return 0;
}

static BOOL CALLBACK listener_init(INIT_ONCE *once, void *parameter, void **context)
{
    HMODULE module;
    HANDLE event, thread;

    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       (const WCHAR *)listener_init, &module);
    if (!(event = CreateEventW(NULL, FALSE, FALSE, NULL)))
        return FALSE;
    if (!(thread = CreateThread(NULL, 0, listener_thread, event, 0, NULL)))
    {
        CloseHandle(event);
        return FALSE;
    }
    CloseHandle(thread);
    return TRUE;
}

HRESULT history_post(const WCHAR *app_id, IToastNotification *toast)
{
    static INIT_ONCE listener_once = INIT_ONCE_STATIC_INIT;
    struct history_entry *entry, *existing;
    struct list removed = LIST_INIT(removed);
    struct history_record record = {0};
    IToastNotification4 *toast4;
    IXmlDocument *content;
    WCHAR *tag = NULL, *group = NULL, *xml = NULL;
    HRESULT hr;

    InitOnceExecuteOnce(&listener_once, listener_init, NULL, NULL);
    if (!(entry = calloc(1, sizeof(*entry))) || !(entry->app_id = wcsdup(app_id)))
    {
        free(entry);
        return E_OUTOFMEMORY;
    }
    toast_get_identity(toast, &tag, &group);
    hr = IToastNotification_get_Content(toast, &content);
    if (SUCCEEDED(hr))
    {
        hr = xml_get_string(content, &xml);
        IXmlDocument_Release(content);
    }
    if (SUCCEEDED(hr) && (!tag || !group))
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        AcquireSRWLockExclusive(&history_lock);
        if ((existing = find_local(toast)))
        {
            list_remove(&existing->entry);
            list_add_tail(&removed, &existing->entry);
        }
        ReleaseSRWLockExclusive(&history_lock);
        if (existing)
            store_delete(existing->app_id, existing->key);

        record.xml = xml;
        record.tag = tag;
        record.group = group;
        record.expiration = toast_get_expiration(toast);
        record.suppress_popup = toast_get_suppress_popup(toast);
        record.expires_on_reboot = toast_get_expires_on_reboot(toast);
        if (SUCCEEDED(IToastNotification_QueryInterface(toast, &IID_IToastNotification4, (void **)&toast4)))
        {
            IToastNotification4_get_Data(toast4, &record.data);
            IToastNotification4_Release(toast4);
        }
        hr = history_store_post(app_id, &record, entry->key);
        if (record.data)
            INotificationData_Release(record.data);
    }
    free(tag);
    free(group);
    free(xml);
    if (FAILED(hr))
    {
        free(entry->app_id);
        free(entry);
        history_release_removed(&removed);
        return hr;
    }

    entry->toast = toast;
    IToastNotification_AddRef(entry->toast);
    AcquireSRWLockExclusive(&history_lock);
    list_add_tail(&history_entries, &entry->entry);
    ReleaseSRWLockExclusive(&history_lock);
    history_release_removed(&removed);
    history_sync();
    return S_OK;
}

BOOL history_remove(IToastNotification *toast)
{
    struct history_entry *entry;
    BOOL removed;

    AcquireSRWLockExclusive(&history_lock);
    if ((entry = find_local(toast)))
        list_remove(&entry->entry);
    ReleaseSRWLockExclusive(&history_lock);
    if (!entry)
        return FALSE;
    removed = store_exists(entry->app_id, entry->key) && store_delete(entry->app_id, entry->key);
    history_free_entry(entry);
    return removed;
}

void history_remove_matching(const WCHAR *app_id, const WCHAR *tag, const WCHAR *group)
{
    HKEY app;

    if (!open_app_key(app_id, FALSE, &app))
    {
        remove_entries(app, tag, group, FALSE);
        RegCloseKey(app);
    }
    history_sync();
}

HRESULT history_update(const WCHAR *app_id, const WCHAR *tag, const WCHAR *group, INotificationData *data,
                       NotificationUpdateResult *result)
{
    WCHAR (*names)[STORE_NAME_LENGTH];
    INT64 now = store_now();
    UINT32 count, i, sequence = 0, stored;
    HKEY app, key;
    BOOL found = FALSE;

    *result = NotificationUpdateResult_NotificationNotFound;
    if (open_app_key(app_id, FALSE, &app))
        return S_OK;
    count = store_list(app, &names);
    for (i = 0; i < count && !found; i++)
    {
        if (RegOpenKeyExW(app, names[i], 0, KEY_ALL_ACCESS, &key))
            continue;
        if (!entry_expired(key, now) && entry_matches(key, tag, group ? group : L""))
        {
            found = TRUE;
            if (!data)
            {
                *result = NotificationUpdateResult_Failed;
            }
            else
            {
                INotificationData_get_SequenceNumber(data, &sequence);
                stored = (UINT32)store_read_number(key, L"DataSequence");
                if (!sequence || !stored || sequence > stored)
                    write_data(key, data);
                *result = NotificationUpdateResult_Succeeded;
            }
        }
        RegCloseKey(key);
    }
    free(names);
    RegCloseKey(app);
    return S_OK;
}

BOOL history_entry_exists(const WCHAR *app_id, const WCHAR *name)
{
    return store_exists(app_id, name);
}

void history_entry_delete(const WCHAR *app_id, const WCHAR *name)
{
    store_delete(app_id, name);
}

INotificationData *history_entry_data(const WCHAR *app_id, const WCHAR *name, DWORD *revision)
{
    INotificationData *data = NULL;
    HKEY app, key;

    *revision = 0;
    if (open_app_key(app_id, FALSE, &app))
        return NULL;
    if (!RegOpenKeyExW(app, name, 0, KEY_READ, &key))
    {
        *revision = (DWORD)store_read_number(key, L"DataRevision");
        data = read_data(key);
        RegCloseKey(key);
    }
    RegCloseKey(app);
    return data;
}

void history_entry_popup_done(const WCHAR *app_id, const WCHAR *name)
{
    HKEY app, key;
    BOOL scheduled = FALSE;

    if (open_app_key(app_id, FALSE, &app))
        return;
    if (!RegOpenKeyExW(app, name, 0, KEY_ALL_ACCESS, &key))
    {
        scheduled = (BOOL)store_read_number(key, L"Scheduled");
        store_write_dword(key, L"Popup", 0);
        RegCloseKey(key);
        if (scheduled)
            RegDeleteTreeW(app, name);
    }
    RegCloseKey(app);
}

void history_scan_popups(void (*callback)(const WCHAR *app_id, const WCHAR *name, void *context), void *context)
{
    WCHAR (*names)[STORE_NAME_LENGTH];
    UINT32 app_count, count, i, j;
    INT64 now = store_now();
    WCHAR **apps;
    HKEY app, key;
    BOOL pending;

    app_count = store_list_apps(L"History", &apps);
    for (i = 0; i < app_count; i++)
    {
        if (open_app_key(apps[i], FALSE, &app))
            continue;
        count = store_list(app, &names);
        for (j = 0; j < count; j++)
        {
            if (RegOpenKeyExW(app, names[j], 0, KEY_ALL_ACCESS, &key))
                continue;
            pending = store_read_number(key, L"Popup") == 1 && !entry_expired(key, now);
            if (pending)
                store_write_dword(key, L"Popup", 2);
            RegCloseKey(key);
            if (pending)
                callback(apps[i], names[j], context);
        }
        free(names);
        RegCloseKey(app);
    }
    store_free_apps(apps, app_count);
}

static BOOL process_alive(DWORD process_id)
{
    HANDLE process;
    BOOL alive;

    if (!process_id || process_id == GetCurrentProcessId())
        return process_id != 0;
    if (!(process = OpenProcess(SYNCHRONIZE, FALSE, process_id)))
        return FALSE;
    alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

BOOL history_owner_alive(const WCHAR *name)
{
    return process_alive(store_name_process(name));
}

void history_post_event(const WCHAR *name, DWORD kind, DWORD value, const WCHAR *arguments,
                        const struct toast_input *inputs, ULONG input_count)
{
    HKEY events, key, inputs_key;
    ULONG i;

    if (!history_owner_alive(name) || store_name_process(name) == GetCurrentProcessId())
        return;
    if (store_open_area(L"Events", TRUE, KEY_ALL_ACCESS, &events))
        return;
    if (!RegCreateKeyExW(events, name, 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &key, NULL))
    {
        if (input_count &&
            !RegCreateKeyExW(key, L"Inputs", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &inputs_key, NULL))
        {
            for (i = 0; i < input_count; i++)
                store_write_string(inputs_key, inputs[i].id ? inputs[i].id : L"", inputs[i].value ? inputs[i].value : L"");
            RegCloseKey(inputs_key);
        }
        if (arguments)
            store_write_string(key, L"Arguments", arguments);
        store_write_dword(key, L"Value", value);
        store_write_dword(key, L"Kind", kind);
        RegCloseKey(key);
    }
    RegCloseKey(events);
}

static ULONG read_inputs(HKEY key, NOTIFICATION_USER_INPUT_DATA **inputs)
{
    DWORD name_length, value_size, type, index, count = 0;
    WCHAR name[256], *value;
    HKEY inputs_key;

    *inputs = NULL;
    if (RegOpenKeyExW(key, L"Inputs", 0, KEY_READ, &inputs_key))
        return 0;
    if (!RegQueryInfoKeyW(inputs_key, NULL, NULL, NULL, NULL, NULL, NULL, &count, NULL, NULL, NULL, NULL) && count &&
        (*inputs = calloc(count, sizeof(**inputs))))
    {
        for (index = 0, count = 0;; index++)
        {
            name_length = ARRAYSIZE(name);
            value_size = 0;
            if (RegEnumValueW(inputs_key, index, name, &name_length, NULL, &type, NULL, &value_size))
                break;
            if (type != REG_SZ || !(value = calloc(value_size / sizeof(WCHAR) + 1, sizeof(WCHAR))))
                continue;
            name_length = ARRAYSIZE(name);
            if (RegEnumValueW(inputs_key, index, name, &name_length, NULL, NULL, (BYTE *)value, &value_size) ||
                !((*inputs)[count].Key = wcsdup(name)))
            {
                free(value);
                continue;
            }
            (*inputs)[count++].Value = value;
        }
    }
    RegCloseKey(inputs_key);
    return count;
}

void history_dispatch_events(void)
{
    WCHAR (*names)[STORE_NAME_LENGTH];
    NOTIFICATION_USER_INPUT_DATA *inputs;
    IToastNotification *toast;
    DWORD kind, value, self = GetCurrentProcessId();
    WCHAR *arguments;
    ULONG input_count, j;
    UINT32 count, i;
    HKEY events, key;

    if (store_open_area(L"Events", FALSE, KEY_ALL_ACCESS, &events))
        return;
    count = store_list(events, &names);
    for (i = 0; i < count; i++)
    {
        if (store_name_process(names[i]) != self || RegOpenKeyExW(events, names[i], 0, KEY_READ, &key))
            continue;
        kind = (DWORD)store_read_number(key, L"Kind");
        value = (DWORD)store_read_number(key, L"Value");
        arguments = store_read_string(key, L"Arguments");
        input_count = read_inputs(key, &inputs);
        RegCloseKey(key);
        if (kind && !RegDeleteTreeW(events, names[i]) && (toast = find_local_key(names[i])))
        {
            if (kind == HISTORY_EVENT_DISMISSED)
                toast_raise_dismissed(toast, (ToastDismissalReason)value);
            else if (kind == HISTORY_EVENT_ACTIVATED)
                toast_raise_activated(toast, arguments ? arguments : L"", inputs, input_count);
            else if (kind == HISTORY_EVENT_FAILED)
                toast_raise_failed(toast, (HRESULT)value);
            IToastNotification_Release(toast);
        }
        for (j = 0; j < input_count; j++)
        {
            free((WCHAR *)inputs[j].Key);
            free((WCHAR *)inputs[j].Value);
        }
        free(inputs);
        free(arguments);
    }
    free(names);
    RegCloseKey(events);
}

void history_collect_events(void)
{
    WCHAR (*names)[STORE_NAME_LENGTH];
    UINT32 count, i;
    HKEY events;

    if (store_open_area(L"Events", FALSE, KEY_ALL_ACCESS, &events))
        return;
    count = store_list(events, &names);
    for (i = 0; i < count; i++)
    {
        if (!history_owner_alive(names[i]))
            RegDeleteTreeW(events, names[i]);
    }
    free(names);
    RegCloseKey(events);
}

static IToastNotification *restore_entry(HKEY key)
{
    IToastNotification *toast = NULL;
    INotificationData *data;
    IXmlDocument *document;
    WCHAR *xml, *tag, *group;

    xml = store_read_string(key, L"Content");
    tag = store_read_string(key, L"Tag");
    group = store_read_string(key, L"Group");
    if (xml && tag && group && SUCCEEDED(xml_load(xml, &document)))
    {
        data = read_data(key);
        toast_restore(document, tag, group, (INT64)store_read_number(key, L"Expiration"),
                      (BOOL)store_read_number(key, L"SuppressPopup"), data, &toast);
        if (data)
            INotificationData_Release(data);
        IXmlDocument_Release(document);
    }
    free(xml);
    free(tag);
    free(group);
    return toast;
}

IToastNotification *history_entry_restore(const WCHAR *app_id, const WCHAR *name)
{
    IToastNotification *toast = NULL;
    HKEY app, key;

    if (open_app_key(app_id, FALSE, &app))
        return NULL;
    if (!RegOpenKeyExW(app, name, 0, KEY_READ, &key))
    {
        toast = restore_entry(key);
        RegCloseKey(key);
    }
    RegCloseKey(app);
    return toast;
}

HRESULT history_view(const WCHAR *app_id, IVectorView_ToastNotification **view)
{
    WCHAR (*names)[STORE_NAME_LENGTH] = NULL;
    IInspectable **items = NULL;
    UINT32 count = 0, found = 0, i;
    HKEY app = NULL, key;
    HRESULT hr;

    if (!open_app_key(app_id, FALSE, &app))
    {
        remove_entries(app, NULL, NULL, TRUE);
        count = store_list(app, &names);
    }
    if (count && !(items = calloc(count, sizeof(*items))))
        count = 0;
    for (i = 0; i < count; i++)
    {
        if (RegOpenKeyExW(app, names[i], 0, KEY_READ, &key))
            continue;
        if (!store_read_number(key, L"Scheduled") && (items[found] = (IInspectable *)restore_entry(key)))
            found++;
        RegCloseKey(key);
    }
    if (app)
        RegCloseKey(app);
    free(names);
    hr = vector_view_create(&IID_IVectorView_ToastNotification, &IID_IIterable_ToastNotification,
                            &IID_IIterator_ToastNotification, items, found, (void **)view);
    for (i = 0; i < found; i++)
        IInspectable_Release(items[i]);
    free(items);
    return hr;
}

struct history
{
    IToastNotificationHistory IToastNotificationHistory_iface;
    IToastNotificationHistory2 IToastNotificationHistory2_iface;
    LONG ref;
};

static inline struct history *impl_from_IToastNotificationHistory(IToastNotificationHistory *iface)
{
    return CONTAINING_RECORD(iface, struct history, IToastNotificationHistory_iface);
}

static HRESULT WINAPI history_QueryInterface(IToastNotificationHistory *iface, REFIID iid, void **out)
{
    struct history *impl = impl_from_IToastNotificationHistory(iface);

    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IToastNotificationHistory))
        *out = &impl->IToastNotificationHistory_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotificationHistory2))
        *out = &impl->IToastNotificationHistory2_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI history_AddRef(IToastNotificationHistory *iface)
{
    return InterlockedIncrement(&impl_from_IToastNotificationHistory(iface)->ref);
}

static ULONG WINAPI history_Release(IToastNotificationHistory *iface)
{
    struct history *impl = impl_from_IToastNotificationHistory(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
        free(impl);
    return ref;
}

static HRESULT WINAPI history_GetIids(IToastNotificationHistory *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastNotificationHistory, count, iids);
}

static HRESULT WINAPI history_GetRuntimeClassName(IToastNotificationHistory *iface, HSTRING *class_name)
{
    if (!class_name)
        return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_UI_Notifications_ToastNotificationHistory,
                               wcslen(RuntimeClass_Windows_UI_Notifications_ToastNotificationHistory), class_name);
}

static HRESULT WINAPI history_GetTrustLevel(IToastNotificationHistory *iface, TrustLevel *trust_level)
{
    if (!trust_level)
        return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT remove_for_app(HSTRING app_id, HSTRING tag, HSTRING group, BOOL any_tag, BOOL any_group)
{
    WCHAR *app = NULL, *tag_text = NULL, *group_text = NULL;
    HRESULT hr;

    if (app_id)
        hr = hstring_to_wide(app_id, &app);
    else
        hr = current_app_id(&app);
    if (FAILED(hr))
        return hr;
    if (!app[0])
    {
        free(app);
        return E_INVALIDARG;
    }
    if (!any_tag && FAILED(hr = hstring_to_wide(tag, &tag_text)))
        goto done;
    if (!any_group && FAILED(hr = hstring_to_wide(group, &group_text)))
        goto done;
    history_remove_matching(app, tag_text, group_text);
    hr = S_OK;

done:
    free(app);
    free(tag_text);
    free(group_text);
    return hr;
}

static HRESULT WINAPI history_RemoveGroup(IToastNotificationHistory *iface, HSTRING group)
{
    return remove_for_app(NULL, NULL, group, TRUE, FALSE);
}

static HRESULT WINAPI history_RemoveGroupWithId(IToastNotificationHistory *iface, HSTRING group, HSTRING app_id)
{
    if (!app_id)
        return E_INVALIDARG;
    return remove_for_app(app_id, NULL, group, TRUE, FALSE);
}

static HRESULT WINAPI history_RemoveGroupedTagWithId(IToastNotificationHistory *iface, HSTRING tag,
                                                     HSTRING group, HSTRING app_id)
{
    if (!app_id)
        return E_INVALIDARG;
    return remove_for_app(app_id, tag, group, FALSE, FALSE);
}

static HRESULT WINAPI history_RemoveGroupedTag(IToastNotificationHistory *iface, HSTRING tag, HSTRING group)
{
    return remove_for_app(NULL, tag, group, FALSE, FALSE);
}

static HRESULT WINAPI history_Remove(IToastNotificationHistory *iface, HSTRING tag)
{
    return remove_for_app(NULL, tag, NULL, FALSE, FALSE);
}

static HRESULT WINAPI history_Clear(IToastNotificationHistory *iface)
{
    return remove_for_app(NULL, NULL, NULL, TRUE, TRUE);
}

static HRESULT WINAPI history_ClearWithId(IToastNotificationHistory *iface, HSTRING app_id)
{
    if (!app_id)
        return E_INVALIDARG;
    return remove_for_app(app_id, NULL, NULL, TRUE, TRUE);
}

static const IToastNotificationHistoryVtbl history_vtbl =
{
    history_QueryInterface,
    history_AddRef,
    history_Release,
    history_GetIids,
    history_GetRuntimeClassName,
    history_GetTrustLevel,
    history_RemoveGroup,
    history_RemoveGroupWithId,
    history_RemoveGroupedTagWithId,
    history_RemoveGroupedTag,
    history_Remove,
    history_Clear,
    history_ClearWithId,
};

static inline struct history *impl_from_IToastNotificationHistory2(IToastNotificationHistory2 *iface)
{
    return CONTAINING_RECORD(iface, struct history, IToastNotificationHistory2_iface);
}

static HRESULT WINAPI history2_QueryInterface(IToastNotificationHistory2 *iface, REFIID iid, void **out)
{
    return history_QueryInterface(&impl_from_IToastNotificationHistory2(iface)->IToastNotificationHistory_iface, iid, out);
}

static ULONG WINAPI history2_AddRef(IToastNotificationHistory2 *iface)
{
    return history_AddRef(&impl_from_IToastNotificationHistory2(iface)->IToastNotificationHistory_iface);
}

static ULONG WINAPI history2_Release(IToastNotificationHistory2 *iface)
{
    return history_Release(&impl_from_IToastNotificationHistory2(iface)->IToastNotificationHistory_iface);
}

static HRESULT WINAPI history2_GetIids(IToastNotificationHistory2 *iface, ULONG *count, IID **iids)
{
    return get_iids(&IID_IToastNotificationHistory2, count, iids);
}

static HRESULT WINAPI history2_GetRuntimeClassName(IToastNotificationHistory2 *iface, HSTRING *class_name)
{
    return history_GetRuntimeClassName(NULL, class_name);
}

static HRESULT WINAPI history2_GetTrustLevel(IToastNotificationHistory2 *iface, TrustLevel *trust_level)
{
    return history_GetTrustLevel(NULL, trust_level);
}

static HRESULT WINAPI history2_GetHistory(IToastNotificationHistory2 *iface, IVectorView_ToastNotification **result)
{
    WCHAR *app;
    HRESULT hr;

    if (!result)
        return E_POINTER;
    *result = NULL;
    if (FAILED(hr = current_app_id(&app)))
        return hr;
    hr = history_view(app, result);
    free(app);
    return hr;
}

static HRESULT WINAPI history2_GetHistoryWithId(IToastNotificationHistory2 *iface, HSTRING app_id,
                                                IVectorView_ToastNotification **result)
{
    WCHAR *app;
    HRESULT hr;

    if (!result)
        return E_POINTER;
    *result = NULL;
    if (!app_id)
        return E_INVALIDARG;
    if (FAILED(hr = hstring_to_wide(app_id, &app)))
        return hr;
    hr = history_view(app, result);
    free(app);
    return hr;
}

static const IToastNotificationHistory2Vtbl history2_vtbl =
{
    history2_QueryInterface,
    history2_AddRef,
    history2_Release,
    history2_GetIids,
    history2_GetRuntimeClassName,
    history2_GetTrustLevel,
    history2_GetHistory,
    history2_GetHistoryWithId,
};

HRESULT history_create(IToastNotificationHistory **out)
{
    struct history *impl;

    if (!(impl = calloc(1, sizeof(*impl))))
        return E_OUTOFMEMORY;
    impl->IToastNotificationHistory_iface.lpVtbl = (IToastNotificationHistoryVtbl *)&history_vtbl;
    impl->IToastNotificationHistory2_iface.lpVtbl = (IToastNotificationHistory2Vtbl *)&history2_vtbl;
    impl->ref = 1;
    *out = &impl->IToastNotificationHistory_iface;
    return S_OK;
}
