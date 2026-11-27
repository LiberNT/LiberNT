/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test replacing a file the caller may not delete, with and without restore semantics
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <aclapi.h>
#include <ndk/iofuncs.h>
#include <ndk/rtlfuncs.h>

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
WriteText(PCWSTR Path, const char *Text)
{
    HANDLE File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD Written = 0;
    BOOL Done;

    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Done = WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL) && Written == strlen(Text);
    CloseHandle(File);
    return Done;
}

static void
ReadText(PCWSTR Path, char *Text, DWORD Size)
{
    HANDLE File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD Read = 0;

    Text[0] = 0;
    if (File == INVALID_HANDLE_VALUE)
        return;
    ReadFile(File, Text, Size - 1, &Read, NULL);
    Text[Read] = 0;
    CloseHandle(File);
}

static DWORD
SetEveryone(PCWSTR Path, ACCESS_MASK Access, DWORD Inheritance)
{
    EXPLICIT_ACCESSW Entry;
    PACL Acl = NULL;
    DWORD Error;

    ZeroMemory(&Entry, sizeof(Entry));
    Entry.grfAccessPermissions = Access;
    Entry.grfAccessMode = SET_ACCESS;
    Entry.grfInheritance = Inheritance;
    Entry.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    Entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    Entry.Trustee.ptstrName = (LPWSTR)L"EVERYONE";
    Error = SetEntriesInAclW(1, &Entry, NULL, &Acl);
    if (Error == ERROR_SUCCESS)
    {
        Error = SetNamedSecurityInfoW((LPWSTR)Path, SE_FILE_OBJECT,
                                      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL,
                                      Acl, NULL);
        LocalFree(Acl);
    }
    return Error;
}

static NTSTATUS
RenameOver(PCWSTR Source, PCWSTR Target, BOOL Backup)
{
    UNICODE_STRING NtTarget;
    IO_STATUS_BLOCK IoStatus;
    PFILE_RENAME_INFORMATION Rename;
    HANDLE File;
    NTSTATUS Status;
    ULONG Length;

    File = CreateFileW(Source, DELETE | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, Backup ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "opening %ls for delete failed with %lu\n", Source, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return STATUS_UNSUCCESSFUL;
    if (!RtlDosPathNameToNtPathName_U(Target, &NtTarget, NULL, NULL))
    {
        CloseHandle(File);
        return STATUS_OBJECT_PATH_INVALID;
    }
    Length = FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + NtTarget.Length;
    Rename = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length);
    if (!Rename)
    {
        RtlFreeUnicodeString(&NtTarget);
        CloseHandle(File);
        return STATUS_NO_MEMORY;
    }
    Rename->ReplaceIfExists = TRUE;
    Rename->FileNameLength = NtTarget.Length;
    CopyMemory(Rename->FileName, NtTarget.Buffer, NtTarget.Length);
    Status = NtSetInformationFile(File, &IoStatus, Rename, Length, FileRenameInformation);
    HeapFree(GetProcessHeap(), 0, Rename);
    RtlFreeUnicodeString(&NtTarget);
    CloseHandle(File);
    return Status;
}

START_TEST(RenameReplaceRestore)
{
    WCHAR Folder[MAX_PATH], Base[MAX_PATH], Source[MAX_PATH], Target[MAX_PATH];
    char Text[32];
    NTSTATUS Status;
    DWORD Error;

    GetTempPathW(ARRAYSIZE(Base), Base);
    StringCchPrintfW(Folder, ARRAYSIZE(Folder), L"%sRenameReplaceRestore%lu", Base, GetCurrentProcessId());
    StringCchPrintfW(Source, ARRAYSIZE(Source), L"%s\\source.txt", Folder);
    StringCchPrintfW(Target, ARRAYSIZE(Target), L"%s\\target.txt", Folder);
    ok(CreateDirectoryW(Folder, NULL), "CreateDirectory failed with %lu\n", GetLastError());

    ok(WriteText(Target, "protected"), "writing the target failed with %lu\n", GetLastError());
    ok(WriteText(Source, "replacement"), "writing the source failed with %lu\n", GetLastError());
    Error = SetEveryone(Folder, FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_TRAVERSE | DELETE | WRITE_DAC,
                        NO_INHERITANCE);
    ok(Error == ERROR_SUCCESS, "folder without delete-child: %lu\n", Error);
    Error = SetEveryone(Target, FILE_GENERIC_READ | WRITE_DAC, NO_INHERITANCE);
    ok(Error == ERROR_SUCCESS, "target without delete: %lu\n", Error);
    Error = SetEveryone(Source, FILE_ALL_ACCESS, NO_INHERITANCE);
    ok(Error == ERROR_SUCCESS, "source with full access: %lu\n", Error);

    Status = RenameOver(Source, Target, FALSE);
    ok(Status == STATUS_ACCESS_DENIED, "replacing a file without delete access returned 0x%lx\n", Status);
    ReadText(Target, Text, sizeof(Text));
    ok(!strcmp(Text, "protected"), "the target holds \"%s\"\n", Text);

    if (!EnableRestorePrivilege(TRUE))
    {
        skip("SeRestorePrivilege is not held\n");
    }
    else
    {
        Status = RenameOver(Source, Target, TRUE);
        ok(Status == STATUS_SUCCESS, "replacing with restore semantics returned 0x%lx\n", Status);
        ReadText(Target, Text, sizeof(Text));
        ok(!strcmp(Text, "replacement"), "the target holds \"%s\"\n", Text);
        ok(GetFileAttributesW(Source) == INVALID_FILE_ATTRIBUTES, "the source is still there\n");
        EnableRestorePrivilege(FALSE);
    }

    SetEveryone(Target, FILE_ALL_ACCESS, NO_INHERITANCE);
    SetEveryone(Folder, FILE_ALL_ACCESS, NO_INHERITANCE);
    DeleteFileW(Source);
    DeleteFileW(Target);
    ok(RemoveDirectoryW(Folder), "RemoveDirectory failed with %lu\n", GetLastError());
}
