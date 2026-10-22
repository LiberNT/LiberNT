/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Per-user notification store shared by every process of the user
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

static const WCHAR store_root[] = L"Software\\LiberNT\\Notifications";
static LONG store_counter;

INT64 store_now(void)
{
    FILETIME now;

    GetSystemTimeAsFileTime(&now);
    return (INT64)(((UINT64)now.dwHighDateTime << 32) | now.dwLowDateTime);
}

void store_new_name(WCHAR *name)
{
    swprintf(name, STORE_NAME_LENGTH, L"%016I64x%08lx%08lx", store_now(), GetCurrentProcessId(),
             (ULONG)InterlockedIncrement(&store_counter));
}

DWORD store_name_process(const WCHAR *name)
{
    WCHAR process[9];

    if (wcslen(name) != STORE_NAME_LENGTH - 1)
        return 0;
    memcpy(process, name + 16, 8 * sizeof(WCHAR));
    process[8] = 0;
    return wcstoul(process, NULL, 16);
}

LSTATUS store_open_area(const WCHAR *area, BOOL create, REGSAM access, HKEY *key)
{
    WCHAR path[128];

    swprintf(path, ARRAYSIZE(path), L"%ls\\%ls", store_root, area);
    if (create)
        return RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, access, NULL, key, NULL);
    return RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, access, key);
}

LSTATUS store_open_root(HKEY *key)
{
    return RegCreateKeyExW(HKEY_CURRENT_USER, store_root, 0, NULL, 0, KEY_NOTIFY | KEY_READ, NULL, key, NULL);
}

LSTATUS store_open_app(const WCHAR *area, const WCHAR *app_id, BOOL create, HKEY *key)
{
    WCHAR *name, *p;
    LSTATUS status;
    HKEY parent;

    *key = NULL;
    if ((status = store_open_area(area, create, KEY_ALL_ACCESS, &parent)))
        return status;
    if (!(name = wcsdup(app_id)))
    {
        RegCloseKey(parent);
        return ERROR_OUTOFMEMORY;
    }
    for (p = name; *p; p++)
    {
        if (*p == L'\\')
            *p = L'/';
    }
    if (create)
        status = RegCreateKeyExW(parent, name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, key, NULL);
    else
        status = RegOpenKeyExW(parent, name, 0, KEY_ALL_ACCESS, key);
    free(name);
    RegCloseKey(parent);
    return status;
}

WCHAR *store_read_string(HKEY key, const WCHAR *name)
{
    DWORD size = 0, type;
    WCHAR *value;

    if (RegQueryValueExW(key, name, NULL, &type, NULL, &size) || type != REG_SZ)
        return wcsdup(L"");
    if (!(value = calloc(size / sizeof(WCHAR) + 1, sizeof(WCHAR))))
        return NULL;
    if (RegQueryValueExW(key, name, NULL, NULL, (BYTE *)value, &size))
        value[0] = 0;
    return value;
}

UINT64 store_read_number(HKEY key, const WCHAR *name)
{
    UINT64 value = 0;
    DWORD size = sizeof(value), type;

    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &size))
        return 0;
    if (type == REG_DWORD)
        return (DWORD)value;
    return type == REG_QWORD ? value : 0;
}

void store_write_string(HKEY key, const WCHAR *name, const WCHAR *value)
{
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (wcslen(value) + 1) * sizeof(WCHAR));
}

void store_write_dword(HKEY key, const WCHAR *name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

void store_write_qword(HKEY key, const WCHAR *name, UINT64 value)
{
    RegSetValueExW(key, name, 0, REG_QWORD, (const BYTE *)&value, sizeof(value));
}

UINT32 store_list(HKEY key, WCHAR (**names)[STORE_NAME_LENGTH])
{
    DWORD count = 0, length, i;
    UINT32 found = 0;

    *names = NULL;
    if (RegQueryInfoKeyW(key, NULL, NULL, NULL, &count, NULL, NULL, NULL, NULL, NULL, NULL, NULL) || !count)
        return 0;
    if (!(*names = calloc(count, sizeof(**names))))
        return 0;
    for (i = 0; i < count; i++)
    {
        length = STORE_NAME_LENGTH;
        if (!RegEnumKeyExW(key, i, (*names)[found], &length, NULL, NULL, NULL, NULL))
            found++;
    }
    return found;
}

UINT32 store_list_apps(const WCHAR *area, WCHAR ***apps)
{
    DWORD count = 0, length, i;
    WCHAR name[256], *p;
    UINT32 found = 0;
    HKEY key;

    *apps = NULL;
    if (store_open_area(area, FALSE, KEY_READ, &key))
        return 0;
    if (!RegQueryInfoKeyW(key, NULL, NULL, NULL, &count, NULL, NULL, NULL, NULL, NULL, NULL, NULL) && count &&
        (*apps = calloc(count, sizeof(**apps))))
    {
        for (i = 0; i < count; i++)
        {
            length = ARRAYSIZE(name);
            if (RegEnumKeyExW(key, i, name, &length, NULL, NULL, NULL, NULL))
                continue;
            for (p = name; *p; p++)
            {
                if (*p == L'/')
                    *p = L'\\';
            }
            if (((*apps)[found] = wcsdup(name)))
                found++;
        }
    }
    RegCloseKey(key);
    return found;
}

void store_free_apps(WCHAR **apps, UINT32 count)
{
    while (count)
        free(apps[--count]);
    free(apps);
}

HRESULT store_watch(HANDLE event, HKEY *root)
{
    LSTATUS status;

    if (!*root && (status = store_open_root(root)))
        return HRESULT_FROM_WIN32(status);
    status = RegNotifyChangeKeyValue(*root, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, event, TRUE);
    return HRESULT_FROM_WIN32(status);
}
