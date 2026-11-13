/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the type name of a file comes from the FriendlyTypeName of its ProgID
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "shelltest.h"

#define IDS_FILETYPE_SHORT 100
#define IDS_FILETYPE_LONG 101
#define CLASSES_KEY L"Software\\Classes\\"

static void
RegisterType(PCWSTR Extension, PCWSTR ProgId, UINT StringId)
{
    WCHAR Module[MAX_PATH], Value[MAX_PATH + 16], Key[MAX_PATH];
    HKEY hKey;

    GetModuleFileNameW(NULL, Module, _countof(Module));
    wsprintfW(Value, L"@%s,-%u", Module, StringId);

    wsprintfW(Key, CLASSES_KEY L"%s", Extension);
    ok_long(RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL), ERROR_SUCCESS);
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE *)ProgId, (lstrlenW(ProgId) + 1) * sizeof(WCHAR));
    RegCloseKey(hKey);

    wsprintfW(Key, CLASSES_KEY L"%s", ProgId);
    ok_long(RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL), ERROR_SUCCESS);
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE *)L"Fallback", sizeof(L"Fallback"));
    RegSetValueExW(hKey, L"FriendlyTypeName", 0, REG_SZ, (const BYTE *)Value, (lstrlenW(Value) + 1) * sizeof(WCHAR));
    RegCloseKey(hKey);
}

static void
UnregisterType(PCWSTR Extension, PCWSTR ProgId)
{
    WCHAR Key[MAX_PATH];

    wsprintfW(Key, CLASSES_KEY L"%s", Extension);
    RegDeleteKeyW(HKEY_CURRENT_USER, Key);
    wsprintfW(Key, CLASSES_KEY L"%s", ProgId);
    RegDeleteKeyW(HKEY_CURRENT_USER, Key);
}

static void
TestTypeName(PCWSTR Extension, PCWSTR ProgId, UINT StringId)
{
    WCHAR File[MAX_PATH], Expected[MAX_PATH];
    SHFILEINFOW Info;

    LoadStringW(GetModuleHandleW(NULL), StringId, Expected, _countof(Expected));
    RegisterType(Extension, ProgId, StringId);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    wsprintfW(File, L"test%s", Extension);
    ZeroMemory(&Info, sizeof(Info));
    ok(SHGetFileInfoW(File, FILE_ATTRIBUTE_NORMAL, &Info, sizeof(Info), SHGFI_USEFILEATTRIBUTES | SHGFI_TYPENAME) != 0,
       "%ls: SHGetFileInfoW failed\n", Extension);
    ok(!wcscmp(Info.szTypeName, Expected), "%ls: type name %s, expected %s\n", Extension,
       wine_dbgstr_w(Info.szTypeName), wine_dbgstr_w(Expected));

    UnregisterType(Extension, ProgId);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}

START_TEST(FileTypeName)
{
    CoInitialize(NULL);
    TestTypeName(L".lntftnshort", L"LiberNT.FileTypeNameShort", IDS_FILETYPE_SHORT);
    TestTypeName(L".lntftnlong", L"LiberNT.FileTypeNameLong", IDS_FILETYPE_LONG);
    CoUninitialize();
}
