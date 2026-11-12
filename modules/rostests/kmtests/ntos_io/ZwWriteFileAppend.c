/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a kernel write through an append-only handle lands at the end of the file on every file system
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
BOOLEAN
OpenVolumeRoot(
    _In_ WCHAR Letter,
    _Out_ PHANDLE Root,
    _Out_writes_(NameCount) PWCHAR FileSystem,
    _In_ ULONG NameCount)
{
    UCHAR Buffer[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + 32 * sizeof(WCHAR)];
    PFILE_FS_ATTRIBUTE_INFORMATION Info = (PFILE_FS_ATTRIBUTE_INFORMATION)Buffer;
    WCHAR Path[] = L"\\??\\C:\\";
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Name;
    NTSTATUS Status;

    Path[4] = Letter;
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(Root,
                        FILE_LIST_DIRECTORY | FILE_ADD_FILE | SYNCHRONIZE,
                        &Attributes,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = ZwQueryVolumeInformationFile(*Root, &IoStatus, Info, sizeof(Buffer), FileFsAttributeInformation);
    if (!NT_SUCCESS(Status) || (Info->FileSystemNameLength / sizeof(WCHAR) >= NameCount))
    {
        ZwClose(*Root);
        return FALSE;
    }

    RtlCopyMemory(FileSystem, Info->FileSystemName, Info->FileSystemNameLength);
    FileSystem[Info->FileSystemNameLength / sizeof(WCHAR)] = UNICODE_NULL;
    return TRUE;
}

static
LONGLONG
QueryPosition(
    _In_ HANDLE File)
{
    FILE_POSITION_INFORMATION Position;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    Position.CurrentByteOffset.QuadPart = -1;
    Status = ZwQueryInformationFile(File, &IoStatus, &Position, sizeof(Position), FilePositionInformation);
    ok_eq_hex(Status, STATUS_SUCCESS);
    return Position.CurrentByteOffset.QuadPart;
}

static
VOID
WriteAt(
    _In_ HANDLE File,
    _In_reads_bytes_(Length) PCSTR Data,
    _In_ ULONG Length,
    _In_ LONGLONG Offset)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER ByteOffset;
    NTSTATUS Status;

    ByteOffset.QuadPart = Offset;
    IoStatus.Status = -1;
    IoStatus.Information = (ULONG_PTR)-1;
    Status = ZwWriteFile(File, NULL, NULL, NULL, &IoStatus, (PVOID)Data, Length, &ByteOffset, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(IoStatus.Status, STATUS_SUCCESS);
    ok_eq_ulongptr(IoStatus.Information, Length);
}

static
VOID
TestVolume(
    _In_ HANDLE Root,
    _In_ PCWSTR FileSystem)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"kmtest-ZwWriteFileAppend.tmp");
    FILE_DISPOSITION_INFORMATION Disposition;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER ByteOffset;
    LONGLONG Position;
    CHAR Data[16];
    NTSTATUS Status;
    HANDLE File;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Root, NULL);
    Status = ZwCreateFile(&File,
                          FILE_WRITE_DATA | SYNCHRONIZE,
                          &Attributes,
                          &IoStatus,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          0,
                          FILE_SUPERSEDE,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                          NULL,
                          0);
    if (skip(NT_SUCCESS(Status), "%ls: cannot create the test file (0x%lx)\n", FileSystem, Status)) return;

    WriteAt(File, "fo", 2, 0);
    ZwClose(File);

    Status = ZwOpenFile(&File,
                        FILE_APPEND_DATA | SYNCHRONIZE,
                        &Attributes,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        WriteAt(File, "ob", 2, 1);
        Position = QueryPosition(File);
        ok(Position == 4, "%ls: the position is %I64d after the first append, expected 4\n", FileSystem, Position);

        WriteAt(File, "ar", 2, 3);
        Position = QueryPosition(File);
        ok(Position == 6, "%ls: the position is %I64d after the second append, expected 6\n", FileSystem, Position);

        ZwClose(File);
    }

    Status = ZwOpenFile(&File,
                        FILE_READ_DATA | DELETE | SYNCHRONIZE,
                        &Attributes,
                        &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    RtlZeroMemory(Data, sizeof(Data));
    ByteOffset.QuadPart = 0;
    Status = ZwReadFile(File, NULL, NULL, NULL, &IoStatus, Data, sizeof(Data) - 1, &ByteOffset, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(IoStatus.Information == 6, "%ls: the file has %Iu bytes, expected 6\n", FileSystem, IoStatus.Information);
    ok(strcmp(Data, "foobar") == 0, "%ls: the file holds '%s', expected 'foobar'\n", FileSystem, Data);

    Disposition.DeleteFile = TRUE;
    Status = ZwSetInformationFile(File, &IoStatus, &Disposition, sizeof(Disposition), FileDispositionInformation);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwClose(File);
}

START_TEST(ZwWriteFileAppend)
{
    WCHAR FileSystem[32];
    ULONG Tested = 0;
    HANDLE Root;
    WCHAR Letter;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        if (!OpenVolumeRoot(Letter, &Root, FileSystem, RTL_NUMBER_OF(FileSystem))) continue;

        trace("%c: is %ls\n", (char)Letter, FileSystem);
        TestVolume(Root, FileSystem);
        ZwClose(Root);
        Tested++;
    }

    ok(Tested != 0, "No volume could be tested\n");
}
