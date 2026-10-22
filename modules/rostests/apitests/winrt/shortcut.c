/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Start menu shortcut with an application user model ID for the toast tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <shlobj.h>
#include <propkey.h>

BOOL
CreateAppShortcut(
    _Out_writes_(MAX_PATH) WCHAR *Path,
    _In_ const WCHAR *AppId)
{
    IPropertyStore *Store;
    IShellLinkW *Link;
    IPersistFile *File;
    PROPVARIANT Value;
    WCHAR Exe[MAX_PATH];
    HRESULT hr;

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, Path)))
        return FALSE;
    wcscat(Path, L"\\LiberNT ApiTest Toast.lnk");
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&Link);
    if (FAILED(hr))
        return FALSE;
    GetModuleFileNameW(NULL, Exe, ARRAYSIZE(Exe));
    IShellLinkW_SetPath(Link, Exe);
    hr = IShellLinkW_QueryInterface(Link, &IID_IPropertyStore, (void **)&Store);
    if (SUCCEEDED(hr))
    {
        PropVariantInit(&Value);
        Value.vt = VT_LPWSTR;
        Value.pwszVal = (LPWSTR)AppId;
        hr = IPropertyStore_SetValue(Store, &PKEY_AppUserModel_ID, &Value);
        if (SUCCEEDED(hr))
            hr = IPropertyStore_Commit(Store);
        IPropertyStore_Release(Store);
    }
    if (SUCCEEDED(hr))
        hr = IShellLinkW_QueryInterface(Link, &IID_IPersistFile, (void **)&File);
    if (SUCCEEDED(hr))
    {
        hr = IPersistFile_Save(File, Path, TRUE);
        IPersistFile_Release(File);
    }
    IShellLinkW_Release(Link);
    trace("shortcut %ls hr %#lx\n", Path, hr);
    return SUCCEEDED(hr);
}
