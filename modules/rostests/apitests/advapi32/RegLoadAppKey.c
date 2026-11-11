/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that an application hive is shared, usable and let go once its keys are closed
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/setypes.h>

static
BOOL
SaveTestHive(
    _In_ PCWSTR Path)
{
    BOOLEAN WasEnabled, Ignored;
    HKEY Source;
    LONG Error;

    Error = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LiberNTAppKeySource", 0, NULL, 0,
                            KEY_ALL_ACCESS, NULL, &Source, NULL);
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW returned %ld\n", Error);
    if (Error != ERROR_SUCCESS) return FALSE;

    if (!NT_SUCCESS(RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &WasEnabled)))
    {
        skip("Cannot enable SeBackupPrivilege\n");
        RegCloseKey(Source);
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\LiberNTAppKeySource");
        return FALSE;
    }

    Error = RegSaveKeyW(Source, Path, NULL);
    ok(Error == ERROR_SUCCESS, "RegSaveKeyW returned %ld\n", Error);

    RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, WasEnabled, FALSE, &Ignored);
    RegCloseKey(Source);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\LiberNTAppKeySource");
    return (Error == ERROR_SUCCESS);
}

START_TEST(RegLoadAppKey)
{
    WCHAR Directory[MAX_PATH];
    WCHAR Path[MAX_PATH];
    WCHAR Log[MAX_PATH];
    HKEY First = NULL;
    HKEY Second = NULL;
    DWORD Value = 42;
    DWORD Data = 0;
    DWORD Size;
    BOOL Deleted = FALSE;
    LONG Error;
    ULONG Attempt;

    GetTempPathW(ARRAYSIZE(Directory), Directory);
    StringCchCatW(Directory, ARRAYSIZE(Directory), L"LiberNTAppKey");
    CreateDirectoryW(Directory, NULL);
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\app.hiv", Directory);
    DeleteFileW(Path);

    if (!SaveTestHive(Path))
    {
        RemoveDirectoryW(Directory);
        return;
    }

    Error = RegLoadAppKeyW(Path, &First, KEY_ALL_ACCESS, 0, 0);
    ok(Error == ERROR_SUCCESS, "RegLoadAppKeyW returned %ld\n", Error);
    ok(First != NULL, "RegLoadAppKeyW gave no key\n");

    Error = RegLoadAppKeyW(Path, &Second, KEY_READ, 0, 0);
    ok(Error == ERROR_SUCCESS, "second RegLoadAppKeyW returned %ld\n", Error);
    ok(Second != NULL, "second RegLoadAppKeyW gave no key\n");

    if (First != NULL)
    {
        Error = RegSetValueExW(First, L"Value", 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
        ok(Error == ERROR_SUCCESS, "RegSetValueExW returned %ld\n", Error);
    }

    if (Second != NULL)
    {
        Size = sizeof(Data);
        Error = RegQueryValueExW(Second, L"Value", NULL, NULL, (BYTE *)&Data, &Size);
        ok(Error == ERROR_SUCCESS, "RegQueryValueExW returned %ld\n", Error);
        ok(Data == Value, "Data = %lu\n", Data);
        RegCloseKey(Second);
    }

    if (First != NULL) RegCloseKey(First);

    for (Attempt = 0; Attempt < 10; Attempt++)
    {
        Deleted = DeleteFileW(Path);
        if (Deleted) break;
        Sleep(200);
    }
    ok(Deleted, "the hive file is still in use, error %lu\n", GetLastError());

    StringCchPrintfW(Log, ARRAYSIZE(Log), L"%s.LOG1", Path);
    DeleteFileW(Log);
    StringCchPrintfW(Log, ARRAYSIZE(Log), L"%s.LOG2", Path);
    DeleteFileW(Log);
    ok(RemoveDirectoryW(Directory), "RemoveDirectoryW failed with %lu\n", GetLastError());
}
