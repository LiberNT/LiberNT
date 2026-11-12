/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that registered shell items are named by their LocalizedString value
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "shelltest.h"

static void
TestItem(PCWSTR Clsid)
{
    WCHAR Key[64], Parse[48], Expected[MAX_PATH];
    SHFILEINFOW Info;
    PIDLIST_ABSOLUTE Pidl = NULL;
    HKEY hKey;
    LONG Error;
    DWORD Size = 0;
    HRESULT hr;

    wsprintfW(Key, L"CLSID\\%s", Clsid);
    Error = RegOpenKeyExW(HKEY_CLASSES_ROOT, Key, 0, KEY_READ, &hKey);
    ok(Error == ERROR_SUCCESS, "%ls: RegOpenKeyExW error %ld\n", Clsid, Error);
    if (Error != ERROR_SUCCESS)
        return;
    Expected[0] = UNICODE_NULL;
    Error = RegLoadMUIStringW(hKey, L"LocalizedString", Expected, sizeof(Expected), &Size, 0, NULL);
    RegCloseKey(hKey);
    ok(Error == ERROR_SUCCESS, "%ls: RegLoadMUIStringW error %ld\n", Clsid, Error);
    if (Error != ERROR_SUCCESS)
        return;

    wsprintfW(Parse, L"::%s", Clsid);
    hr = SHParseDisplayName(Parse, NULL, &Pidl, 0, NULL);
    ok(hr == S_OK, "%ls: SHParseDisplayName hr 0x%lx\n", Clsid, hr);
    if (FAILED(hr))
        return;
    Info.szDisplayName[0] = UNICODE_NULL;
    ok(SHGetFileInfoW((PCWSTR)Pidl, 0, &Info, sizeof(Info), SHGFI_PIDL | SHGFI_DISPLAYNAME) != 0,
       "%ls: SHGetFileInfoW failed\n", Clsid);
    ok(!wcscmp(Info.szDisplayName, Expected), "%ls: display name %s, LocalizedString %s\n", Clsid,
       wine_dbgstr_w(Info.szDisplayName), wine_dbgstr_w(Expected));
    CoTaskMemFree(Pidl);
}

START_TEST(RegItemDisplayName)
{
    LANGID UiLanguage = GetUserDefaultUILanguage();

    CoInitialize(NULL);
    if (PRIMARYLANGID(UiLanguage) != LANG_JAPANESE)
        SetThreadUILanguage(MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT));

    TestItem(L"{645FF040-5081-101B-9F08-00AA002F954E}");
    TestItem(L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}");

    SetThreadUILanguage(UiLanguage);
    CoUninitialize();
}
