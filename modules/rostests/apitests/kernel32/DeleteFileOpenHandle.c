/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test deleting a file that another handle still has open
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>

static
BOOL
WriteNewFile(
    _In_ PCWSTR Path,
    _In_ PCSTR Text)
{
    HANDLE File;
    DWORD Written;
    BOOL Success;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Success = WriteFile(File, Text, lstrlenA(Text), &Written, NULL);
    CloseHandle(File);
    return Success;
}

static
VOID
TestDeleteFileWithOpenHandle(
    _In_ PCWSTR Path,
    _In_ BOOL Posix)
{
    CHAR Buffer[16];
    DWORD Read;
    HANDLE Holder;
    HANDLE Created;

    ok(WriteNewFile(Path, "first"), "WriteNewFile failed with %lu\n", GetLastError());
    Holder = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(Holder != INVALID_HANDLE_VALUE, "Open failed with %lu\n", GetLastError());
    if (Holder == INVALID_HANDLE_VALUE)
        return;

    ok(DeleteFileW(Path), "DeleteFileW failed with %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ok(GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES, "The deleted name still has attributes\n");
    ok(GetLastError() == (Posix ? ERROR_FILE_NOT_FOUND : ERROR_ACCESS_DENIED),
       "GetFileAttributesW error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Created = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Posix)
    {
        ok(Created != INVALID_HANDLE_VALUE, "CREATE_NEW after delete failed with %lu\n", GetLastError());
    }
    else
    {
        ok(Created == INVALID_HANDLE_VALUE, "CREATE_NEW on a delete-pending name succeeded\n");
        ok(GetLastError() == ERROR_ACCESS_DENIED, "CREATE_NEW error %lu\n", GetLastError());
    }
    if (Created != INVALID_HANDLE_VALUE)
        CloseHandle(Created);

    RtlZeroMemory(Buffer, sizeof(Buffer));
    ok(ReadFile(Holder, Buffer, sizeof(Buffer), &Read, NULL), "ReadFile failed with %lu\n", GetLastError());
    ok(Read == 5 && !memcmp(Buffer, "first", 5), "The open handle read %lu bytes '%.*s'\n", Read, (int)Read, Buffer);
    CloseHandle(Holder);

    if (Posix)
    {
        ok(GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES, "The new file is missing\n");
        ok(DeleteFileW(Path), "DeleteFileW of the new file failed with %lu\n", GetLastError());
    }
    SetLastError(0xdeadbeef);
    ok(GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND,
       "The file outlived its last handle (error %lu)\n", GetLastError());
}

static
VOID
TestClassicDispositionWithOpenHandle(
    _In_ PCWSTR Path)
{
    FILE_DISPOSITION_INFORMATION Disposition = { TRUE };
    FILE_BASIC_INFORMATION Basic;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Name;
    NTSTATUS Status;
    HANDLE Holder;
    HANDLE Deleter;
    HANDLE Created;

    ok(WriteNewFile(Path, "second"), "WriteNewFile failed with %lu\n", GetLastError());
    Holder = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(Holder != INVALID_HANDLE_VALUE, "Open failed with %lu\n", GetLastError());
    Deleter = CreateFileW(Path, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(Deleter != INVALID_HANDLE_VALUE, "Open for delete failed with %lu\n", GetLastError());
    if (Holder == INVALID_HANDLE_VALUE || Deleter == INVALID_HANDLE_VALUE)
        goto Cleanup;

    Status = NtSetInformationFile(Deleter, &IoStatus, &Disposition, sizeof(Disposition), FileDispositionInformation);
    ok(Status == STATUS_SUCCESS, "FileDispositionInformation returned 0x%lx\n", Status);
    CloseHandle(Deleter);
    Deleter = INVALID_HANDLE_VALUE;

    ok(RtlDosPathNameToNtPathName_U(Path, &Name, NULL, NULL), "RtlDosPathNameToNtPathName_U failed\n");
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtQueryAttributesFile(&Attributes, &Basic);
    ok(Status == STATUS_DELETE_PENDING, "NtQueryAttributesFile returned 0x%lx\n", Status);
    Status = NtCreateFile(&Created, GENERIC_WRITE | SYNCHRONIZE, &Attributes, &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL,
                          0, FILE_CREATE, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    ok(Status == STATUS_DELETE_PENDING, "FILE_CREATE on a delete-pending name returned 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
        NtClose(Created);
    Status = NtCreateFile(&Created, GENERIC_READ | SYNCHRONIZE, &Attributes, &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    ok(Status == STATUS_DELETE_PENDING, "FILE_OPEN of a delete-pending name returned 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
        NtClose(Created);
    RtlFreeUnicodeString(&Name);

Cleanup:
    if (Deleter != INVALID_HANDLE_VALUE)
        CloseHandle(Deleter);
    if (Holder != INVALID_HANDLE_VALUE)
        CloseHandle(Holder);
    SetLastError(0xdeadbeef);
    ok(GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND,
       "The file outlived its last handle (error %lu)\n", GetLastError());
}

START_TEST(DeleteFileOpenHandle)
{
    WCHAR Drives[256];
    WCHAR FileSystem[MAX_PATH];
    WCHAR Path[MAX_PATH];
    PWSTR Root;
    ULONG Tested = 0;

    ok(GetLogicalDriveStringsW(RTL_NUMBER_OF(Drives), Drives) != 0, "GetLogicalDriveStringsW failed with %lu\n",
       GetLastError());
    for (Root = Drives; *Root; Root += wcslen(Root) + 1)
    {
        BOOL Ntfs;

        if (GetDriveTypeW(Root) != DRIVE_FIXED ||
            !GetVolumeInformationW(Root, NULL, 0, NULL, NULL, NULL, FileSystem, RTL_NUMBER_OF(FileSystem)))
        {
            continue;
        }
        Ntfs = !_wcsicmp(FileSystem, L"NTFS");
        if (!Ntfs && _wcsicmp(FileSystem, L"FAT32") && _wcsicmp(FileSystem, L"FAT"))
            continue;

        trace("Testing %ls (%ls)\n", Root, FileSystem);
        StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%lsdelete-open-handle.tmp", Root);
        DeleteFileW(Path);
        TestDeleteFileWithOpenHandle(Path, Ntfs);
        TestClassicDispositionWithOpenHandle(Path);
        Tested++;
    }
    ok(Tested != 0, "No fixed NTFS or FAT volume was tested\n");
}
