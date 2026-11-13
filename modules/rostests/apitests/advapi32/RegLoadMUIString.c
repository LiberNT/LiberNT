/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the parameter checks of RegLoadMUIStringW
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define TEST_KEY L"Software\\LiberNT-apitest-RegLoadMUIString"
#define MUI_VALUE L"@%SystemRoot%\\system32\\shell32.dll,-8964"

static
VOID
TestLoad(
    _In_ HKEY Key,
    _In_opt_ PCWSTR Value,
    _In_ BOOL UseBuffer,
    _In_ DWORD Size,
    _In_ BOOL UseCount,
    _In_ DWORD Flags,
    _In_opt_ PCWSTR BaseDir,
    _In_ LONG Expected,
    _In_ int Line)
{
    WCHAR Buffer[MAX_PATH];
    DWORD Count = 0xdeadbeef;
    LONG Error;

    memset(Buffer, 0xff, sizeof(Buffer));
    Error = RegLoadMUIStringW(Key, Value, UseBuffer ? Buffer : NULL, Size, UseCount ? &Count : NULL, Flags, BaseDir);
    ok(Error == Expected, "line %d: error %ld, expected %ld\n", Line, Error, Expected);
    if (Expected == ERROR_SUCCESS && Error == ERROR_SUCCESS)
    {
        ok(Buffer[0] != 0xffff && Buffer[0] != UNICODE_NULL, "line %d: buffer not written\n", Line);
        if (UseCount)
            ok(Count == (wcslen(Buffer) + 1) * sizeof(WCHAR), "line %d: count %lu for %Iu characters\n", Line, Count, wcslen(Buffer));
    }
    else if (Error == ERROR_INVALID_PARAMETER)
    {
        ok(Buffer[0] == 0xffff, "line %d: buffer written: 0x%04x\n", Line, Buffer[0]);
        if (UseCount)
            ok(Count == 0xdeadbeef, "line %d: count %lu\n", Line, Count);
    }
}

START_TEST(RegLoadMUIString)
{
    WCHAR SystemDir[MAX_PATH];
    HKEY Key;
    LONG Error;

    GetSystemDirectoryW(SystemDir, ARRAYSIZE(SystemDir));
    Error = RegCreateKeyExW(HKEY_CURRENT_USER, TEST_KEY, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Key, NULL);
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW error %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;
    RegSetValueExW(Key, L"Mui", 0, REG_EXPAND_SZ, (const BYTE *)MUI_VALUE, sizeof(MUI_VALUE));
    RegSetValueExW(Key, L"Plain", 0, REG_SZ, (const BYTE *)L"Plain", sizeof(L"Plain"));

    TestLoad(Key, L"Mui", TRUE, MAX_PATH * sizeof(WCHAR), TRUE, 0, NULL, ERROR_SUCCESS, __LINE__);
    TestLoad(Key, L"Mui", TRUE, MAX_PATH * sizeof(WCHAR), FALSE, 0, NULL, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(Key, L"Mui", TRUE, MAX_PATH * sizeof(WCHAR), FALSE, 0, SystemDir, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(Key, L"Mui", TRUE, MAX_PATH * sizeof(WCHAR), FALSE, REG_MUI_STRING_TRUNCATE, NULL, ERROR_SUCCESS, __LINE__);
    TestLoad(Key, L"Mui", TRUE, MAX_PATH * sizeof(WCHAR), TRUE, REG_MUI_STRING_TRUNCATE, NULL, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(Key, L"Mui", TRUE, MAX_PATH * sizeof(WCHAR) - 1, TRUE, 0, NULL, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(Key, L"Mui", FALSE, 0, TRUE, 0, NULL, ERROR_MORE_DATA, __LINE__);
    TestLoad(Key, L"Mui", FALSE, 0, FALSE, 0, NULL, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(Key, L"Missing", TRUE, MAX_PATH * sizeof(WCHAR), TRUE, 0, NULL, ERROR_FILE_NOT_FOUND, __LINE__);
    TestLoad(Key, L"Missing", TRUE, MAX_PATH * sizeof(WCHAR), FALSE, 0, NULL, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(Key, L"Plain", TRUE, MAX_PATH * sizeof(WCHAR), TRUE, 0, NULL, ERROR_INVALID_DATA, __LINE__);
    TestLoad(Key, L"Plain", TRUE, MAX_PATH * sizeof(WCHAR), FALSE, 0, NULL, ERROR_INVALID_PARAMETER, __LINE__);
    TestLoad(NULL, L"Mui", TRUE, MAX_PATH * sizeof(WCHAR), TRUE, 0, NULL, ERROR_INVALID_PARAMETER, __LINE__);

    RegCloseKey(Key);
    RegDeleteKeyW(HKEY_CURRENT_USER, TEST_KEY);
}
