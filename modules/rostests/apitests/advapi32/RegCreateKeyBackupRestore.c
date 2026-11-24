/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which privileges let REG_OPTION_BACKUP_RESTORE create a subkey the parent's DACL refuses
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/cmfuncs.h>
#include <ndk/obfuncs.h>
#include <sddl.h>

#define PARENT L"Software\\LiberNTRegBackupRestore"

static
BOOL
SetPrivilege(
    _In_ PCWSTR Name,
    _In_ BOOL Enable)
{
    TOKEN_PRIVILEGES Privileges = { 1 };
    HANDLE Token;
    BOOL Ok;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Token))
        return FALSE;
    Ok = LookupPrivilegeValueW(NULL, Name, &Privileges.Privileges[0].Luid);
    Privileges.Privileges[0].Attributes = Enable ? SE_PRIVILEGE_ENABLED : 0;
    Ok = Ok && AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(Token);
    return Ok;
}

static
BOOL
SetParentDacl(
    _In_ PCWSTR Sddl)
{
    PSECURITY_DESCRIPTOR Sd;
    HKEY Key;
    LONG Error;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl, SDDL_REVISION_1, &Sd, NULL))
        return FALSE;
    Error = RegOpenKeyExW(HKEY_CURRENT_USER, PARENT, 0, WRITE_DAC, &Key);
    if (Error == ERROR_SUCCESS)
    {
        Error = RegSetKeySecurity(Key, DACL_SECURITY_INFORMATION, Sd);
        RegCloseKey(Key);
    }
    LocalFree(Sd);
    return Error == ERROR_SUCCESS;
}

static
VOID
DeleteLinkKey(
    _In_ PCWSTR Child)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name;
    HANDLE Link;
    HKEY Parent;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, PARENT, 0, KEY_READ, &Parent) != ERROR_SUCCESS)
        return;
    RtlInitUnicodeString(&Name, Child);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_OPENLINK, Parent, NULL);
    if (NT_SUCCESS(NtOpenKey(&Link, DELETE, &Attributes)))
    {
        NtDeleteKey(Link);
        NtClose(Link);
    }
    RegCloseKey(Parent);
}

static
VOID
TestCreate(
    _In_ PCWSTR Child,
    _In_ DWORD Options,
    _In_ LONG ExpectedError,
    _In_ DWORD ExpectedDisposition)
{
    WCHAR Path[MAX_PATH];
    DWORD Disposition = 0;
    HKEY Key;
    LONG Error;

    StringCchPrintfW(Path, ARRAYSIZE(Path), PARENT L"\\%s", Child);
    Error = RegCreateKeyExW(HKEY_CURRENT_USER, Path, 0, NULL, Options, KEY_READ, NULL, &Key, &Disposition);
    ok(Error == ExpectedError, "%ls: RegCreateKeyExW returned %ld, expected %ld\n", Child, Error, ExpectedError);
    if (Error != ERROR_SUCCESS)
        return;
    ok(Disposition == ExpectedDisposition, "%ls: disposition %lu, expected %lu\n", Child, Disposition, ExpectedDisposition);
    RegCloseKey(Key);
}

START_TEST(RegCreateKeyBackupRestore)
{
    HKEY Key;
    LONG Error;

    if (!SetPrivilege(L"SeBackupPrivilege", TRUE) || !SetPrivilege(L"SeRestorePrivilege", TRUE))
    {
        skip("The backup and restore privileges are not held\n");
        return;
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, PARENT);
    RegDeleteKeyW(HKEY_CURRENT_USER, PARENT);
    Error = RegCreateKeyExW(HKEY_CURRENT_USER, PARENT L"\\Existing", 0, NULL, 0, KEY_READ, NULL, &Key, NULL);
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
        return;
    RegCloseKey(Key);

    /* Everyone may only read the parent; its owner keeps READ_CONTROL and WRITE_DAC */
    ok(SetParentDacl(L"D:P(A;;KR;;;WD)"), "The parent's DACL was not set\n");

    TestCreate(L"Plain", 0, ERROR_ACCESS_DENIED, 0);
    TestCreate(L"BackupRestore", REG_OPTION_BACKUP_RESTORE, ERROR_SUCCESS, REG_CREATED_NEW_KEY);
    TestCreate(L"Existing", REG_OPTION_BACKUP_RESTORE, ERROR_SUCCESS, REG_OPENED_EXISTING_KEY);
    TestCreate(L"BackupRestoreLink", REG_OPTION_BACKUP_RESTORE | REG_OPTION_CREATE_LINK, ERROR_SUCCESS, REG_CREATED_NEW_KEY);

    ok(SetPrivilege(L"SeBackupPrivilege", FALSE), "SeBackupPrivilege was not disabled\n");
    TestCreate(L"RestoreOnly", REG_OPTION_BACKUP_RESTORE, ERROR_SUCCESS, REG_CREATED_NEW_KEY);

    ok(SetPrivilege(L"SeBackupPrivilege", TRUE), "SeBackupPrivilege was not enabled\n");
    ok(SetPrivilege(L"SeRestorePrivilege", FALSE), "SeRestorePrivilege was not disabled\n");
    TestCreate(L"BackupOnly", REG_OPTION_BACKUP_RESTORE, ERROR_SUCCESS, REG_CREATED_NEW_KEY);
    TestCreate(L"BackupOnlyLink", REG_OPTION_BACKUP_RESTORE | REG_OPTION_CREATE_LINK, ERROR_SUCCESS, REG_CREATED_NEW_KEY);

    ok(SetPrivilege(L"SeBackupPrivilege", FALSE), "SeBackupPrivilege was not disabled\n");
    TestCreate(L"NoPrivilege", REG_OPTION_BACKUP_RESTORE, ERROR_ACCESS_DENIED, 0);

    SetPrivilege(L"SeRestorePrivilege", TRUE);
    SetParentDacl(L"D:(A;;KA;;;WD)");
    DeleteLinkKey(L"BackupRestoreLink");
    DeleteLinkKey(L"BackupOnlyLink");
    ok_long(RegDeleteTreeW(HKEY_CURRENT_USER, PARENT), ERROR_SUCCESS);
    RegDeleteKeyW(HKEY_CURRENT_USER, PARENT);
    SetPrivilege(L"SeRestorePrivilege", FALSE);
}
