/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     An open for backup intent grants the restore rights to a holder of SeRestorePrivilege
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <sddl.h>
#include <strsafe.h>

static
BOOL
SetRestorePrivilege(
    _In_ BOOL Enable)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE Token;
    BOOL Result;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &Token))
        return FALSE;
    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Attributes = Enable ? SE_PRIVILEGE_ENABLED : 0;
    Result = LookupPrivilegeValueW(NULL, L"SeRestorePrivilege", &Privileges.Privileges[0].Luid) &&
             AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL) &&
             GetLastError() == ERROR_SUCCESS;
    CloseHandle(Token);
    return Result;
}

static
BOOL
SetDacl(
    _In_ PCWSTR Path,
    _In_ PCWSTR Sddl)
{
    PSECURITY_DESCRIPTOR Descriptor;
    BOOL Result;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl, SDDL_REVISION_1, &Descriptor, NULL))
        return FALSE;
    Result = SetFileSecurityW(Path, DACL_SECURITY_INFORMATION, Descriptor);
    LocalFree(Descriptor);
    return Result;
}

static
NTSTATUS
OpenExisting(
    _In_ PCWSTR Path,
    _In_ ACCESS_MASK Access,
    _In_ ULONG Options)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Name;
    HANDLE Handle;
    NTSTATUS Status;

    if (!RtlDosPathNameToNtPathName_U(Path, &Name, NULL, NULL))
        return STATUS_OBJECT_PATH_INVALID;
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Handle, Access | SYNCHRONIZE, &ObjectAttributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, Options | FILE_SYNCHRONOUS_IO_NONALERT);
    RtlFreeUnicodeString(&Name);
    if (NT_SUCCESS(Status))
        NtClose(Handle);
    return Status;
}

START_TEST(NtOpenFileBackupIntent)
{
    WCHAR Directory[MAX_PATH];
    WCHAR Path[MAX_PATH];
    WCHAR Folder[MAX_PATH];
    HANDLE File;

    if (!SetRestorePrivilege(FALSE))
    {
        skip("SeRestorePrivilege is not held\n");
        return;
    }

    GetTempPathW(ARRAYSIZE(Directory), Directory);
    GetTempFileNameW(Directory, L"bsa", 0, Path);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW failed with %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    CloseHandle(File);

    ok(SetDacl(Path, L"D:P(A;;FR;;;WD)"), "Setting a read-only DACL failed with %lu\n", GetLastError());

    StringCchPrintfW(Folder, ARRAYSIZE(Folder), L"%sbsa%lu", Directory, GetCurrentProcessId());
    ok(CreateDirectoryW(Folder, NULL), "CreateDirectoryW failed with %lu\n", GetLastError());
    ok(SetDacl(Folder, L"D:P(A;;FR;;;WD)"), "Setting a read-only directory DACL failed with %lu\n", GetLastError());

    ok_ntstatus(OpenExisting(Path, FILE_WRITE_DATA, 0), STATUS_ACCESS_DENIED);
    ok_ntstatus(OpenExisting(Path, FILE_WRITE_DATA, FILE_OPEN_FOR_BACKUP_INTENT), STATUS_ACCESS_DENIED);

    ok(SetRestorePrivilege(TRUE), "Enabling SeRestorePrivilege failed\n");
    ok_ntstatus(OpenExisting(Path, FILE_WRITE_DATA, 0), STATUS_ACCESS_DENIED);
    ok_ntstatus(OpenExisting(Path, FILE_WRITE_DATA, FILE_OPEN_FOR_BACKUP_INTENT), STATUS_SUCCESS);
    ok_ntstatus(OpenExisting(Path, WRITE_DAC | WRITE_OWNER | DELETE, FILE_OPEN_FOR_BACKUP_INTENT), STATUS_SUCCESS);
    ok_ntstatus(OpenExisting(Path, FILE_READ_DATA | FILE_WRITE_DATA, FILE_OPEN_FOR_BACKUP_INTENT), STATUS_SUCCESS);
    ok_ntstatus(OpenExisting(Folder, WRITE_DAC | WRITE_OWNER, 0), STATUS_ACCESS_DENIED);
    ok_ntstatus(OpenExisting(Folder, WRITE_DAC | WRITE_OWNER, FILE_OPEN_FOR_BACKUP_INTENT), STATUS_SUCCESS);
    ok_ntstatus(OpenExisting(Folder, FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | DELETE, FILE_OPEN_FOR_BACKUP_INTENT), STATUS_SUCCESS);
    SetRestorePrivilege(FALSE);
    ok_ntstatus(OpenExisting(Folder, WRITE_DAC | WRITE_OWNER, FILE_OPEN_FOR_BACKUP_INTENT), STATUS_ACCESS_DENIED);

    ok(SetDacl(Folder, L"D:P(A;;FA;;;WD)"), "Restoring the directory DACL failed with %lu\n", GetLastError());
    ok(RemoveDirectoryW(Folder), "RemoveDirectoryW failed with %lu\n", GetLastError());

    ok(SetDacl(Path, L"D:P(A;;FA;;;WD)"), "Restoring the DACL failed with %lu\n", GetLastError());
    ok(DeleteFileW(Path), "DeleteFileW failed with %lu\n", GetLastError());
}
