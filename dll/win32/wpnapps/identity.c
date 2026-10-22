/*
 * PROJECT:     LiberNT Windows Runtime
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Resolves application user model IDs to display names, icons and activators
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

#define COBJMACROS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <propsys.h>
#include <initguid.h>
#include <propkey.h>
#include <wine/list.h>

#include "identity.h"

#define IDENTITY_CACHE_TIME 30000
#define SHORTCUT_SEARCH_DEPTH 4

struct identity_cache_entry
{
    struct list entry;
    WCHAR *app_id;
    BOOL found;
    DWORD tick;
    struct app_identity identity;
};

static struct list identity_cache = LIST_INIT(identity_cache);
static SRWLOCK identity_lock = SRWLOCK_INIT;

static void read_display_name(HKEY key, struct app_identity *identity)
{
    WCHAR value[512];
    DWORD size = sizeof(value);

    if (RegGetValueW(key, NULL, L"DisplayName", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, value, &size))
        return;
    if (value[0] == L'@')
        SHLoadIndirectString(value, identity->display_name, ARRAYSIZE(identity->display_name), NULL);
    else
        lstrcpynW(identity->display_name, value, ARRAYSIZE(identity->display_name));
}

static BOOL lookup_registry(HKEY root, const WCHAR *app_id, struct app_identity *identity)
{
    WCHAR path[512], value[64];
    DWORD size;
    HKEY key;

    swprintf(path, ARRAYSIZE(path), L"Software\\Classes\\AppUserModelId\\%ls", app_id);
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &key))
        return FALSE;
    read_display_name(key, identity);
    size = sizeof(identity->icon_path);
    if (RegGetValueW(key, NULL, L"IconUri", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, identity->icon_path, &size))
        identity->icon_path[0] = 0;
    size = sizeof(value);
    if (!RegGetValueW(key, NULL, L"CustomActivator", RRF_RT_REG_SZ, NULL, value, &size) &&
        SUCCEEDED(CLSIDFromString(value, &identity->activator)))
        identity->has_activator = TRUE;
    RegCloseKey(key);
    return TRUE;
}

static BOOL check_shortcut(const WCHAR *path, const WCHAR *app_id, struct app_identity *identity)
{
    IPropertyStore *store = NULL;
    IShellLinkW *link = NULL;
    IPersistFile *file = NULL;
    PROPVARIANT value;
    BOOL found = FALSE;
    const WCHAR *name;
    WCHAR *extension;

    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link)))
        return FALSE;
    if (FAILED(IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&file)) ||
        FAILED(IPersistFile_Load(file, path, STGM_READ)) ||
        FAILED(IShellLinkW_QueryInterface(link, &IID_IPropertyStore, (void **)&store)))
        goto done;

    PropVariantInit(&value);
    if (SUCCEEDED(IPropertyStore_GetValue(store, &PKEY_AppUserModel_ID, &value)) &&
        value.vt == VT_LPWSTR && value.pwszVal && !wcsicmp(value.pwszVal, app_id))
        found = TRUE;
    PropVariantClear(&value);
    if (!found)
        goto done;

    name = PathFindFileNameW(path);
    lstrcpynW(identity->display_name, name, ARRAYSIZE(identity->display_name));
    if ((extension = PathFindExtensionW(identity->display_name)) && !wcsicmp(extension, L".lnk"))
        *extension = 0;
    PropVariantInit(&value);
    if (SUCCEEDED(IPropertyStore_GetValue(store, &PKEY_AppUserModel_ToastActivatorCLSID, &value)) &&
        value.vt == VT_CLSID && value.puuid)
    {
        identity->activator = *value.puuid;
        identity->has_activator = TRUE;
    }
    PropVariantClear(&value);
    if (FAILED(IShellLinkW_GetIconLocation(link, identity->icon_path, ARRAYSIZE(identity->icon_path),
                                           &identity->icon_index)) || !identity->icon_path[0])
    {
        identity->icon_index = 0;
        if (FAILED(IShellLinkW_GetPath(link, identity->icon_path, ARRAYSIZE(identity->icon_path), NULL, 0)))
            identity->icon_path[0] = 0;
    }

done:
    if (store)
        IPropertyStore_Release(store);
    if (file)
        IPersistFile_Release(file);
    IShellLinkW_Release(link);
    return found;
}

static BOOL search_shortcuts(const WCHAR *directory, const WCHAR *app_id, struct app_identity *identity, UINT depth)
{
    WIN32_FIND_DATAW data;
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    HANDLE find;
    BOOL found = FALSE;

    if (!PathCombineW(pattern, directory, L"*"))
        return FALSE;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return FALSE;
    do
    {
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L".."))
            continue;
        if (!PathCombineW(path, directory, data.cFileName))
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (depth && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                found = search_shortcuts(path, app_id, identity, depth - 1);
        }
        else if (!wcsicmp(PathFindExtensionW(data.cFileName), L".lnk"))
        {
            found = check_shortcut(path, app_id, identity);
        }
    }
    while (!found && FindNextFileW(find, &data));
    FindClose(find);
    return found;
}

static BOOL lookup_uncached(const WCHAR *app_id, struct app_identity *identity)
{
    static const int folders[] = { CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS };
    WCHAR directory[MAX_PATH];
    BOOL found = FALSE;
    HRESULT init;
    UINT i;

    memset(identity, 0, sizeof(*identity));
    if (lookup_registry(HKEY_CURRENT_USER, app_id, identity) || lookup_registry(HKEY_LOCAL_MACHINE, app_id, identity))
    {
        if (!identity->display_name[0])
            lstrcpynW(identity->display_name, app_id, ARRAYSIZE(identity->display_name));
        return TRUE;
    }
    init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    for (i = 0; i < ARRAYSIZE(folders) && !found; i++)
    {
        if (SUCCEEDED(SHGetFolderPathW(NULL, folders[i], NULL, SHGFP_TYPE_CURRENT, directory)))
            found = search_shortcuts(directory, app_id, identity, SHORTCUT_SEARCH_DEPTH);
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return found;
}

HRESULT current_app_id(WCHAR **app_id)
{
    WCHAR *explicit_id = NULL;
    HRESULT hr;

    *app_id = NULL;
    hr = GetCurrentProcessExplicitAppUserModelID(&explicit_id);
    if (FAILED(hr) || !explicit_id || !explicit_id[0])
    {
        CoTaskMemFree(explicit_id);
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    *app_id = wcsdup(explicit_id);
    CoTaskMemFree(explicit_id);
    return *app_id ? S_OK : E_OUTOFMEMORY;
}

BOOL app_identity_lookup(const WCHAR *app_id, struct app_identity *identity)
{
    struct identity_cache_entry *entry, *found = NULL;
    DWORD now = GetTickCount();
    BOOL result;

    AcquireSRWLockShared(&identity_lock);
    LIST_FOR_EACH_ENTRY(entry, &identity_cache, struct identity_cache_entry, entry)
    {
        if (!wcsicmp(entry->app_id, app_id) && now - entry->tick < IDENTITY_CACHE_TIME)
        {
            found = entry;
            *identity = entry->identity;
            result = entry->found;
            break;
        }
    }
    ReleaseSRWLockShared(&identity_lock);
    if (found)
        return result;

    result = lookup_uncached(app_id, identity);

    AcquireSRWLockExclusive(&identity_lock);
    LIST_FOR_EACH_ENTRY(entry, &identity_cache, struct identity_cache_entry, entry)
    {
        if (!wcsicmp(entry->app_id, app_id))
        {
            found = entry;
            break;
        }
    }
    if (!found && (found = calloc(1, sizeof(*found))))
    {
        if ((found->app_id = wcsdup(app_id)))
            list_add_tail(&identity_cache, &found->entry);
        else
        {
            free(found);
            found = NULL;
        }
    }
    if (found)
    {
        found->found = result;
        found->tick = now;
        found->identity = *identity;
    }
    ReleaseSRWLockExclusive(&identity_lock);
    return result;
}
