/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test FILE_RENAME_POSIX_SEMANTICS onto a target that has open handles
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/iofuncs.h>

#define RENAME_REPLACE_IF_EXISTS 0x00000001
#define RENAME_POSIX_SEMANTICS 0x00000002

static WCHAR TestDirectory[MAX_PATH];

static
VOID
MakePath(
    _Out_writes_(MAX_PATH) PWSTR Path,
    _In_ PCWSTR Name)
{
    StringCchPrintfW(Path, MAX_PATH, L"%s\\%s", TestDirectory, Name);
}

static
BOOL
WriteTestFile(
    _In_ PCWSTR Path,
    _In_ PCSTR Data)
{
    HANDLE File;
    DWORD Written = 0;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, Data, (DWORD)strlen(Data), &Written, NULL) && Written == strlen(Data);
    ok(Result, "WriteFile(%S) failed with %lu\n", Path, GetLastError());
    CloseHandle(File);
    return Result;
}

static
BOOL
ReadHandle(
    _In_ HANDLE File,
    _In_ PCSTR Expected)
{
    CHAR Data[64];
    DWORD Read = 0;

    memset(Data, 0, sizeof(Data));
    SetFilePointer(File, 0, NULL, FILE_BEGIN);
    return ReadFile(File, Data, sizeof(Data) - 1, &Read, NULL) && Read == strlen(Expected) && !strcmp(Data, Expected);
}

static
BOOL
ReadPath(
    _In_ PCWSTR Path,
    _In_ PCSTR Expected)
{
    HANDLE File;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = ReadHandle(File, Expected);
    CloseHandle(File);
    return Result;
}

static
HANDLE
OpenShared(
    _In_ PCWSTR Path,
    _In_ DWORD Access)
{
    HANDLE File;

    File = CreateFileW(Path, Access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile(%S) failed with %lu\n", Path, GetLastError());
    return File;
}

static
NTSTATUS
RenameEx(
    _In_ PCWSTR Source,
    _In_ PCWSTR NewName,
    _In_ ULONG Flags,
    _In_ FILE_INFORMATION_CLASS Class)
{
    UCHAR Buffer[sizeof(FILE_RENAME_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
    PFILE_RENAME_INFORMATION Info = (PFILE_RENAME_INFORMATION)Buffer;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG Length = (ULONG)(wcslen(NewName) * sizeof(WCHAR));
    HANDLE File;
    NTSTATUS Status;

    File = OpenShared(Source, DELETE | SYNCHRONIZE);
    if (File == INVALID_HANDLE_VALUE)
        return STATUS_UNSUCCESSFUL;
    memset(Buffer, 0, sizeof(Buffer));
    if (Class == FileRenameInformationEx)
        *(PULONG)Info = Flags;
    else
        Info->ReplaceIfExists = !!(Flags & RENAME_REPLACE_IF_EXISTS);
    Info->FileNameLength = Length;
    memcpy(Info->FileName, NewName, Length);
    Status = NtSetInformationFile(File, &IoStatusBlock, Info, FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + Length,
                                  Class);
    CloseHandle(File);
    return Status;
}

static
VOID
TestReplaceOpenTarget(VOID)
{
    WCHAR Source[MAX_PATH];
    WCHAR Target[MAX_PATH];
    DWORD Written;
    HANDLE Open;
    NTSTATUS Status;

    MakePath(Source, L"source.bin");
    MakePath(Target, L"target.bin");
    if (!WriteTestFile(Source, "new") || !WriteTestFile(Target, "old"))
        return;
    Open = OpenShared(Target, GENERIC_READ | GENERIC_WRITE);
    if (Open == INVALID_HANDLE_VALUE)
        return;

    Status = RenameEx(Source, L"target.bin", RENAME_REPLACE_IF_EXISTS, FileRenameInformationEx);
    ok(Status == STATUS_ACCESS_DENIED, "Replacing an open target without POSIX semantics returned 0x%lx\n", Status);
    Status = RenameEx(Source, L"target.bin", RENAME_REPLACE_IF_EXISTS, FileRenameInformation);
    ok(Status == STATUS_ACCESS_DENIED, "FileRenameInformation replacing an open target returned 0x%lx\n", Status);
    Status = RenameEx(Source, L"target.bin", RENAME_POSIX_SEMANTICS, FileRenameInformationEx);
    ok(Status == STATUS_OBJECT_NAME_COLLISION, "POSIX semantics without replace returned 0x%lx\n", Status);

    Status = RenameEx(Source, L"target.bin", RENAME_REPLACE_IF_EXISTS | RENAME_POSIX_SEMANTICS, FileRenameInformationEx);
    ok(Status == STATUS_SUCCESS, "Replacing an open target with POSIX semantics returned 0x%lx\n", Status);
    ok(ReadPath(Target, "new"), "The target name does not open the renamed file\n");
    ok(GetFileAttributesW(Source) == INVALID_FILE_ATTRIBUTES, "The source name is still there\n");
    ok(ReadHandle(Open, "old"), "The handle of the replaced file no longer reads its data\n");
    SetFilePointer(Open, 0, NULL, FILE_END);
    ok(WriteFile(Open, "er", 2, &Written, NULL) && Written == 2, "Writing through the replaced handle failed with %lu\n",
       GetLastError());
    ok(ReadHandle(Open, "older"), "The replaced file does not read back its write\n");
    ok(ReadPath(Target, "new"), "A write through the replaced handle reached the renamed file\n");
    CloseHandle(Open);
    ok(ReadPath(Target, "new"), "Closing the replaced handle changed the renamed file\n");

    ok(WriteTestFile(Source, "again"), "Recreating the source failed\n");
    Status = RenameEx(Source, L"target.bin", RENAME_REPLACE_IF_EXISTS | RENAME_POSIX_SEMANTICS, FileRenameInformationEx);
    ok(Status == STATUS_SUCCESS, "Replacing a closed target with POSIX semantics returned 0x%lx\n", Status);
    ok(ReadPath(Target, "again"), "The second replacement does not read back\n");
    DeleteFileW(Target);
    DeleteFileW(Source);
}

static
VOID
TestReplaceLinkedTarget(VOID)
{
    WCHAR Source[MAX_PATH];
    WCHAR Target[MAX_PATH];
    WCHAR Link[MAX_PATH];
    HANDLE Open;
    NTSTATUS Status;

    MakePath(Source, L"source2.bin");
    MakePath(Target, L"target2.bin");
    MakePath(Link, L"link2.bin");
    if (!WriteTestFile(Source, "new") || !WriteTestFile(Target, "linked"))
        return;
    ok(CreateHardLinkW(Link, Target, NULL), "CreateHardLink failed with %lu\n", GetLastError());
    Open = OpenShared(Target, GENERIC_READ);
    if (Open == INVALID_HANDLE_VALUE)
        return;

    Status = RenameEx(Source, L"target2.bin", RENAME_REPLACE_IF_EXISTS | RENAME_POSIX_SEMANTICS, FileRenameInformationEx);
    ok(Status == STATUS_SUCCESS, "Replacing an open linked target with POSIX semantics returned 0x%lx\n", Status);
    ok(ReadPath(Target, "new"), "The target name does not open the renamed file\n");
    ok(ReadPath(Link, "linked"), "The other link of the replaced file lost its data\n");
    ok(ReadHandle(Open, "linked"), "The handle of the replaced file no longer reads its data\n");
    CloseHandle(Open);
    ok(ReadPath(Link, "linked"), "Closing the replaced handle removed the other link\n");
    DeleteFileW(Link);
    DeleteFileW(Target);
    DeleteFileW(Source);
}

static
VOID
TestRefusedTargets(VOID)
{
    WCHAR Source[MAX_PATH];
    WCHAR Target[MAX_PATH];
    WCHAR Directory[MAX_PATH];
    HANDLE Open;
    NTSTATUS Status;

    MakePath(Source, L"source3.bin");
    MakePath(Target, L"readonly.bin");
    MakePath(Directory, L"dirtarget");
    if (!WriteTestFile(Source, "new") || !WriteTestFile(Target, "ro"))
        return;
    ok(CreateDirectoryW(Directory, NULL), "CreateDirectory failed with %lu\n", GetLastError());

    ok(SetFileAttributesW(Target, FILE_ATTRIBUTE_READONLY), "SetFileAttributes failed with %lu\n", GetLastError());
    Open = OpenShared(Target, GENERIC_READ);
    Status = RenameEx(Source, L"readonly.bin", RENAME_REPLACE_IF_EXISTS | RENAME_POSIX_SEMANTICS, FileRenameInformationEx);
    ok(Status == STATUS_ACCESS_DENIED, "Replacing an open read-only target returned 0x%lx\n", Status);
    if (Open != INVALID_HANDLE_VALUE)
        CloseHandle(Open);
    ok(ReadPath(Target, "ro"), "The refused replacement changed the read-only target\n");
    SetFileAttributesW(Target, FILE_ATTRIBUTE_NORMAL);

    Open = OpenShared(Directory, FILE_READ_ATTRIBUTES);
    Status = RenameEx(Source, L"dirtarget", RENAME_REPLACE_IF_EXISTS | RENAME_POSIX_SEMANTICS, FileRenameInformationEx);
    ok(Status == STATUS_ACCESS_DENIED, "Replacing an open directory returned 0x%lx\n", Status);
    if (Open != INVALID_HANDLE_VALUE)
        CloseHandle(Open);

    RemoveDirectoryW(Directory);
    DeleteFileW(Target);
    DeleteFileW(Source);
}

START_TEST(PosixRename)
{
    WCHAR TempPath[MAX_PATH];
    DWORD Length;

    Length = GetTempPathW(RTL_NUMBER_OF(TempPath), TempPath);
    ok(Length != 0 && Length < RTL_NUMBER_OF(TempPath), "GetTempPath failed with %lu\n", GetLastError());
    StringCchPrintfW(TestDirectory, RTL_NUMBER_OF(TestDirectory), L"%sposixren%lx", TempPath, GetCurrentProcessId());
    ok(CreateDirectoryW(TestDirectory, NULL), "CreateDirectory(%S) failed with %lu\n", TestDirectory, GetLastError());

    TestReplaceOpenTarget();
    TestReplaceLinkedTarget();
    TestRefusedTargets();

    ok(RemoveDirectoryW(TestDirectory), "RemoveDirectory(%S) failed with %lu\n", TestDirectory, GetLastError());
}
