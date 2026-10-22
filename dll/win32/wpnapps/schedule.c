/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Scheduled toast notifications kept in the per-user notification store
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

#define SCHEDULE_OWNER_GRACE 20000000LL

static LSTATUS open_app_key(const WCHAR *app_id, BOOL create, HKEY *key)
{
    return store_open_app(L"Schedule", app_id, create, key);
}

HRESULT schedule_store_add(const WCHAR *app_id, IScheduledToastNotification *scheduled, WCHAR *name)
{
    WCHAR *tag = NULL, *group = NULL, *xml = NULL, *id = NULL;
    UINT32 maximum_snooze_count;
    INT64 snooze_interval, expiration;
    IXmlDocument *content;
    DateTime delivery;
    BOOL suppress;
    HKEY app, key;
    LSTATUS status;
    HRESULT hr;

    hr = scheduled_toast_get_details(scheduled, &content, &delivery, &tag, &group, &suppress);
    if (FAILED(hr))
        return hr;
    hr = xml_get_string(content, &xml);
    IXmlDocument_Release(content);
    if (SUCCEEDED(hr))
        hr = scheduled_toast_get_extra(scheduled, &id, &snooze_interval, &maximum_snooze_count, &expiration);
    if (SUCCEEDED(hr) && (status = open_app_key(app_id, TRUE, &app)))
        hr = HRESULT_FROM_WIN32(status);
    if (SUCCEEDED(hr))
    {
        store_new_name(name);
        if (!(status = RegCreateKeyExW(app, name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL)))
        {
            store_write_string(key, L"Content", xml);
            store_write_string(key, L"Tag", tag);
            store_write_string(key, L"Group", group);
            store_write_string(key, L"Id", id);
            store_write_qword(key, L"Delivery", (UINT64)delivery.UniversalTime);
            if (snooze_interval)
                store_write_qword(key, L"SnoozeInterval", (UINT64)snooze_interval);
            store_write_dword(key, L"MaximumSnoozeCount", maximum_snooze_count);
            if (expiration)
                store_write_qword(key, L"Expiration", (UINT64)expiration);
            store_write_dword(key, L"SuppressPopup", suppress);
            RegCloseKey(key);
        }
        hr = HRESULT_FROM_WIN32(status);
        RegCloseKey(app);
    }
    free(tag);
    free(group);
    free(xml);
    free(id);
    return hr;
}

BOOL schedule_store_remove(const WCHAR *app_id, const WCHAR *name)
{
    HKEY app;
    BOOL removed;

    if (open_app_key(app_id, FALSE, &app))
        return FALSE;
    removed = !RegDeleteTreeW(app, name);
    RegCloseKey(app);
    return removed;
}

static IScheduledToastNotification *restore_entry(HKEY key, const WCHAR *name)
{
    IScheduledToastNotification *scheduled = NULL;
    WCHAR *xml, *tag, *group, *id;
    IXmlDocument *document;

    xml = store_read_string(key, L"Content");
    tag = store_read_string(key, L"Tag");
    group = store_read_string(key, L"Group");
    id = store_read_string(key, L"Id");
    if (xml && tag && group && id && SUCCEEDED(xml_load(xml, &document)))
    {
        scheduled_toast_restore(document, (INT64)store_read_number(key, L"Delivery"), tag, group, id,
                                (INT64)store_read_number(key, L"SnoozeInterval"),
                                (UINT32)store_read_number(key, L"MaximumSnoozeCount"),
                                (BOOL)store_read_number(key, L"SuppressPopup"),
                                (INT64)store_read_number(key, L"Expiration"), name, &scheduled);
        IXmlDocument_Release(document);
    }
    free(xml);
    free(tag);
    free(group);
    free(id);
    return scheduled;
}

UINT32 schedule_store_list(const WCHAR *app_id, WCHAR (**names)[STORE_NAME_LENGTH])
{
    UINT32 count = 0;
    HKEY app;

    *names = NULL;
    if (open_app_key(app_id, FALSE, &app))
        return 0;
    count = store_list(app, names);
    RegCloseKey(app);
    return count;
}

IScheduledToastNotification *schedule_store_restore(const WCHAR *app_id, const WCHAR *name)
{
    IScheduledToastNotification *scheduled = NULL;
    HKEY app, key;

    if (open_app_key(app_id, FALSE, &app))
        return NULL;
    if (!RegOpenKeyExW(app, name, 0, KEY_READ, &key))
    {
        scheduled = restore_entry(key, name);
        RegCloseKey(key);
    }
    RegCloseKey(app);
    return scheduled;
}

static BOOL claim_entry(HKEY app, const WCHAR *name, struct history_record *record, WCHAR **storage)
{
    HKEY key;

    if (RegOpenKeyExW(app, name, 0, KEY_READ, &key))
        return FALSE;
    storage[0] = store_read_string(key, L"Content");
    storage[1] = store_read_string(key, L"Tag");
    storage[2] = store_read_string(key, L"Group");
    record->expiration = (INT64)store_read_number(key, L"Expiration");
    record->suppress_popup = (BOOL)store_read_number(key, L"SuppressPopup");
    RegCloseKey(key);
    record->xml = storage[0];
    record->tag = storage[1];
    record->group = storage[2];
    record->scheduled = TRUE;
    return storage[0] && storage[1] && storage[2] && !RegDeleteTreeW(app, name);
}

BOOL schedule_store_claim(const WCHAR *app_id, const WCHAR *name, BOOL deliver)
{
    struct history_record record = {0};
    WCHAR *storage[3] = {NULL}, history_name[STORE_NAME_LENGTH];
    BOOL claimed;
    HKEY app;
    UINT32 i;

    if (open_app_key(app_id, FALSE, &app))
        return FALSE;
    claimed = claim_entry(app, name, &record, storage);
    RegCloseKey(app);
    if (claimed && deliver && !record.suppress_popup && (!record.expiration || record.expiration > store_now()))
    {
        history_store_post(app_id, &record, history_name);
        host_ensure();
    }
    for (i = 0; i < ARRAYSIZE(storage); i++)
        free(storage[i]);
    return claimed;
}

void schedule_deliver(const WCHAR *app_id, IScheduledToastNotification *scheduled)
{
    struct history_record record = {0};
    WCHAR *tag = NULL, *group = NULL, *xml = NULL, *id = NULL, name[STORE_NAME_LENGTH];
    UINT32 maximum_snooze_count;
    INT64 snooze_interval;
    IXmlDocument *content;
    DateTime delivery;
    BOOL suppress;

    if (FAILED(scheduled_toast_get_details(scheduled, &content, &delivery, &tag, &group, &suppress)))
        return;
    if (SUCCEEDED(xml_get_string(content, &xml)) &&
        SUCCEEDED(scheduled_toast_get_extra(scheduled, &id, &snooze_interval, &maximum_snooze_count,
                                            &record.expiration)) &&
        !suppress && (!record.expiration || record.expiration > store_now()))
    {
        record.xml = xml;
        record.tag = tag;
        record.group = group;
        record.scheduled = TRUE;
        if (SUCCEEDED(history_store_post(app_id, &record, name)))
            host_ensure();
    }
    IXmlDocument_Release(content);
    free(tag);
    free(group);
    free(xml);
    free(id);
}

void schedule_host_scan(HWND window, UINT_PTR timer)
{
    WCHAR (*names)[STORE_NAME_LENGTH];
    INT64 now = store_now(), next = 0, due;
    UINT32 app_count, count, i, j;
    WCHAR **apps;
    HKEY app, key;
    UINT64 wait;

    KillTimer(window, timer);
    app_count = store_list_apps(L"Schedule", &apps);
    for (i = 0; i < app_count; i++)
    {
        if (open_app_key(apps[i], FALSE, &app))
            continue;
        count = store_list(app, &names);
        RegCloseKey(app);
        for (j = 0; j < count; j++)
        {
            if (open_app_key(apps[i], FALSE, &app))
                break;
            due = 0;
            if (!RegOpenKeyExW(app, names[j], 0, KEY_READ, &key))
            {
                due = (INT64)store_read_number(key, L"Delivery");
                RegCloseKey(key);
            }
            RegCloseKey(app);
            if (!due)
                continue;
            if (history_owner_alive(names[j]) && store_name_process(names[j]) != GetCurrentProcessId())
                due += SCHEDULE_OWNER_GRACE;
            if (due <= now)
                schedule_store_claim(apps[i], names[j], TRUE);
            else if (!next || due < next)
                next = due;
        }
        free(names);
    }
    store_free_apps(apps, app_count);
    if (next)
    {
        wait = (UINT64)(next - now) / 10000 + 1;
        SetTimer(window, timer, (UINT)min(wait, (UINT64)USER_TIMER_MAXIMUM), NULL);
    }
}
