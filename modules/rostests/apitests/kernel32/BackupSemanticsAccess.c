/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that FILE_FLAG_BACKUP_SEMANTICS grants restore access for specific rights
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <sddl.h>
static BOOL
EnableRestorePrivilege(BOOL Enable)
{
    TOKEN_PRIVILEGES Privileges = { 1 };
    HANDLE Token;
    BOOL Done;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &Token))
        return FALSE;
    Privileges.Privileges[0].Attributes = Enable ? SE_PRIVILEGE_ENABLED : 0;
    Done = LookupPrivilegeValueW(NULL, L"SeRestorePrivilege", &Privileges.Privileges[0].Luid) &&
           AdjustTokenPrivileges(Token, FALSE, &Privileges, sizeof(Privileges), NULL, NULL) &&
           GetLastError() == ERROR_SUCCESS;
    CloseHandle(Token);
    return Done;
}

static BOOL
SetDacl(PCWSTR Path, PCWSTR Sddl)
{
    PSECURITY_DESCRIPTOR Descriptor;
    BOOL Done;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl, SDDL_REVISION_1, &Descriptor, NULL))
        return FALSE;
    Done = SetFileSecurityW(Path, DACL_SECURITY_INFORMATION, Descriptor);
    LocalFree(Descriptor);
    return Done;
}

static DWORD
OpenError(PCWSTR Path, DWORD Access, DWORD Flags, HANDLE *Handle)
{
    *Handle = CreateFileW(Path, Access, FILE_SHARE_READ, NULL, OPEN_EXISTING, Flags, NULL);
    return *Handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
}

START_TEST(BackupSemanticsAccess)
{
    WCHAR Dir[MAX_PATH], Path[MAX_PATH];
    char Text[16] = { 0 };
    HANDLE File;
    DWORD Error, Count = 0;

    GetTempPathW(ARRAYSIZE(Dir), Dir);
    GetTempFileNameW(Dir, L"bks", 0, Path);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "create failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    WriteFile(File, "old", 3, &Count, NULL);
    CloseHandle(File);
    ok(SetDacl(Path, L"D:P(A;;FR;;;WD)"), "SetFileSecurity failed: %lu\n", GetLastError());

    EnableRestorePrivilege(FALSE);
    Error = OpenError(Path, FILE_WRITE_DATA, 0, &File);
    ok(Error == ERROR_ACCESS_DENIED, "plain write open: %lu, expected ERROR_ACCESS_DENIED\n", Error);
    if (File != INVALID_HANDLE_VALUE) CloseHandle(File);
    Error = OpenError(Path, FILE_WRITE_DATA, FILE_FLAG_BACKUP_SEMANTICS, &File);
    ok(Error == ERROR_ACCESS_DENIED, "backup write open without the privilege: %lu, expected ERROR_ACCESS_DENIED\n", Error);
    if (File != INVALID_HANDLE_VALUE) CloseHandle(File);
    Error = OpenError(Path, FILE_READ_DATA, FILE_FLAG_BACKUP_SEMANTICS, &File);
    ok(Error == ERROR_SUCCESS, "backup read open: %lu, expected success\n", Error);
    if (File != INVALID_HANDLE_VALUE) CloseHandle(File);

    if (!EnableRestorePrivilege(TRUE))
    {
        skip("SeRestorePrivilege is not held (not elevated)\n");
    }
    else
    {
        Error = OpenError(Path, FILE_WRITE_DATA, 0, &File);
        ok(Error == ERROR_ACCESS_DENIED, "plain write open with the privilege: %lu, expected ERROR_ACCESS_DENIED\n", Error);
        if (File != INVALID_HANDLE_VALUE) CloseHandle(File);

        Error = OpenError(Path, FILE_WRITE_DATA, FILE_FLAG_BACKUP_SEMANTICS, &File);
        ok(Error == ERROR_SUCCESS, "backup write open with the privilege: %lu, expected success\n", Error);
        if (File != INVALID_HANDLE_VALUE)
        {
            Count = 0;
            ok(WriteFile(File, "new", 3, &Count, NULL) && Count == 3, "write through the backup handle failed: %lu\n", GetLastError());
            CloseHandle(File);
        }

        Error = OpenError(Path, WRITE_DAC | FILE_WRITE_ATTRIBUTES, FILE_FLAG_BACKUP_SEMANTICS, &File);
        ok(Error == ERROR_SUCCESS, "backup WRITE_DAC open with the privilege: %lu, expected success\n", Error);
        if (File != INVALID_HANDLE_VALUE) CloseHandle(File);
        EnableRestorePrivilege(FALSE);

        File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        Count = 0;
        if (File != INVALID_HANDLE_VALUE)
        {
            ReadFile(File, Text, sizeof(Text) - 1, &Count, NULL);
            CloseHandle(File);
        }
        ok(Count == 3 && !memcmp(Text, "new", 3), "content after the backup write: \"%s\"\n", Text);
    }

    SetDacl(Path, L"D:P(A;;FA;;;WD)");
    ok(DeleteFileW(Path), "delete failed: %lu\n", GetLastError());
}
