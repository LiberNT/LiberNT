/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that renaming through a handle opened for asynchronous I/O completes before NtSetInformationFile returns
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>

static BOOL CreateEmptyFile(LPCWSTR Path)
{
    HANDLE File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    CloseHandle(File);
    return TRUE;
}

static void TestRenameOnAsyncHandle(LPCWSTR Dir)
{
    WCHAR Source[MAX_PATH], Target[MAX_PATH], Moved[MAX_PATH], Gone[MAX_PATH];
    CHAR MissingA[MAX_PATH], MovedA[MAX_PATH];
    UCHAR Buffer[sizeof(FILE_RENAME_INFORMATION) + MAX_PATH * sizeof(WCHAR)];
    PFILE_RENAME_INFORMATION Rename = (PFILE_RENAME_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING SourceName, TargetName;
    IO_STATUS_BLOCK IoStatus;
    HANDLE File;
    NTSTATUS Status;

    StringCchPrintfW(Source, ARRAYSIZE(Source), L"%s\\async_src.txt", Dir);
    StringCchPrintfW(Target, ARRAYSIZE(Target), L"%s\\async_dst.txt", Dir);
    StringCchPrintfW(Gone, ARRAYSIZE(Gone), L"%s\\async_gone.txt", Dir);
    StringCchPrintfW(Moved, ARRAYSIZE(Moved), L"%s\\async_moved.txt", Dir);
    DeleteFileW(Source);
    DeleteFileW(Target);
    DeleteFileW(Gone);
    DeleteFileW(Moved);

    ok(CreateEmptyFile(Source), "creating %S failed with %lu\n", Source, GetLastError());
    ok(RtlDosPathNameToNtPathName_U(Source, &SourceName, NULL, NULL), "converting %S failed\n", Source);
    ok(RtlDosPathNameToNtPathName_U(Target, &TargetName, NULL, NULL), "converting %S failed\n", Target);

    /* No FILE_SYNCHRONOUS_IO_* option: the file object is opened for asynchronous I/O, as MoveFile opens it */
    InitializeObjectAttributes(&Attributes, &SourceName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&File, DELETE | SYNCHRONIZE, &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(Buffer, sizeof(Buffer));
        Rename->ReplaceIfExists = FALSE;
        Rename->RootDirectory = NULL;
        Rename->FileNameLength = TargetName.Length;
        RtlCopyMemory(Rename->FileName, TargetName.Buffer, TargetName.Length);

        IoStatus.Status = 0x12345678;
        IoStatus.Information = 0x12345678;
        Status = NtSetInformationFile(File, &IoStatus, Rename,
                                      FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + TargetName.Length,
                                      FileRenameInformation);
        ok_hex(Status, STATUS_SUCCESS);
        ok_hex(IoStatus.Status, STATUS_SUCCESS);
        ok(GetFileAttributesW(Target) != INVALID_FILE_ATTRIBUTES, "%S missing after the rename returned\n", Target);
        ok(GetFileAttributesW(Source) == INVALID_FILE_ATTRIBUTES, "%S still present after the rename returned\n", Source);
        NtClose(File);
    }
    RtlFreeUnicodeString(&SourceName);
    RtlFreeUnicodeString(&TargetName);

    ok(MoveFileWithProgressW(Target, Gone, NULL, NULL, 0), "MoveFileWithProgressW(%S) failed with %lu\n", Target, GetLastError());
    ok(GetFileAttributesW(Gone) != INVALID_FILE_ATTRIBUTES, "%S missing after MoveFileWithProgressW\n", Gone);
    WideCharToMultiByte(CP_ACP, 0, Target, -1, MissingA, ARRAYSIZE(MissingA), NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, Moved, -1, MovedA, ARRAYSIZE(MovedA), NULL, NULL);
    SetLastError(0xdeadbeef);
    ok(!MoveFileWithProgressA(MissingA, MovedA, NULL, NULL, 0), "MoveFileWithProgressA of a missing file succeeded\n");
    ok_long(GetLastError(), ERROR_FILE_NOT_FOUND);

    DeleteFileW(Gone);
    DeleteFileW(Moved);
    DeleteFileW(Target);
    DeleteFileW(Source);
}

static BOOL FindFatVolume(LPWSTR Root, DWORD RootCount)
{
    WCHAR Drives[256], FileSystem[MAX_PATH];
    LPWSTR Drive;

    if (!GetLogicalDriveStringsW(ARRAYSIZE(Drives), Drives))
        return FALSE;
    for (Drive = Drives; *Drive; Drive += wcslen(Drive) + 1)
    {
        if (GetDriveTypeW(Drive) != DRIVE_FIXED && GetDriveTypeW(Drive) != DRIVE_REMOVABLE)
            continue;
        if (!GetVolumeInformationW(Drive, NULL, 0, NULL, NULL, NULL, FileSystem, ARRAYSIZE(FileSystem)))
            continue;
        if (!wcsncmp(FileSystem, L"FAT", 3))
        {
            StringCchCopyW(Root, RootCount, Drive);
            return TRUE;
        }
    }
    return FALSE;
}

START_TEST(RenameAsyncHandle)
{
    WCHAR Temp[MAX_PATH], Root[MAX_PATH], Dir[MAX_PATH];

    GetTempPathW(ARRAYSIZE(Temp), Temp);
    Temp[wcslen(Temp) - 1] = UNICODE_NULL;
    TestRenameOnAsyncHandle(Temp);

    if (!FindFatVolume(Root, ARRAYSIZE(Root)))
    {
        skip("No FAT volume to rename on\n");
        return;
    }

    StringCchPrintfW(Dir, ARRAYSIZE(Dir), L"%sRenameAsyncHandle", Root);
    if (!CreateDirectoryW(Dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        skip("Cannot create %S (error %lu)\n", Dir, GetLastError());
        return;
    }
    TestRenameOnAsyncHandle(Dir);
    RemoveDirectoryW(Dir);
}
