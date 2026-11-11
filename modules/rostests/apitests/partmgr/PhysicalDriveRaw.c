/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the file system and the file position of a whole disk opened as PhysicalDriveN
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>
#include <strsafe.h>

#define MAX_DISKS 32

static BOOL
ReadAt(
    _In_ HANDLE Disk,
    _In_ LONGLONG Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PBYTE Buffer)
{
    OVERLAPPED Overlapped;
    DWORD Returned = 0;

    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.Offset = (DWORD)Offset;
    Overlapped.OffsetHigh = (DWORD)(Offset >> 32);
    return ReadFile(Disk, Buffer, Length, &Returned, &Overlapped) && Returned == Length;
}

static void
CheckSeekAndRead(
    _In_ HANDLE Disk,
    _In_ ULONG Number,
    _In_ LONGLONG Offset,
    _In_ ULONG SectorSize,
    _Out_writes_bytes_(SectorSize * 2) PBYTE Buffer)
{
    LARGE_INTEGER Position, NewPosition;
    DWORD Returned = 0;
    BOOL Result;

    Position.QuadPart = Offset;
    NewPosition.QuadPart = -1;
    Result = SetFilePointerEx(Disk, Position, &NewPosition, FILE_BEGIN);
    ok(Result, "Disk %lu: SetFilePointerEx(%I64d) failed with %lu\n", Number, Offset, GetLastError());
    if (!Result)
        return;
    ok(NewPosition.QuadPart == Offset, "Disk %lu: position %I64d, expected %I64d\n", Number, NewPosition.QuadPart, Offset);

    Result = ReadFile(Disk, Buffer, SectorSize, &Returned, NULL);
    ok(Result, "Disk %lu: ReadFile at %I64d failed with %lu\n", Number, Offset, GetLastError());
    if (!Result)
        return;
    ok(Returned == SectorSize, "Disk %lu: read %lu bytes at %I64d\n", Number, Returned, Offset);

    Position.QuadPart = 0;
    Result = SetFilePointerEx(Disk, Position, &NewPosition, FILE_CURRENT);
    ok(Result, "Disk %lu: SetFilePointerEx(FILE_CURRENT) failed with %lu\n", Number, GetLastError());
    if (Result)
    {
        ok(NewPosition.QuadPart == Offset + SectorSize, "Disk %lu: position %I64d after the read, expected %I64d\n",
           Number, NewPosition.QuadPart, Offset + SectorSize);
    }

    Result = ReadAt(Disk, Offset, SectorSize, Buffer + SectorSize);
    ok(Result, "Disk %lu: ReadFile at explicit offset %I64d failed with %lu\n", Number, Offset, GetLastError());
    if (Result)
        ok(!memcmp(Buffer, Buffer + SectorSize, SectorSize), "Disk %lu: the two reads at %I64d differ\n", Number, Offset);
}

static void
CheckDisk(
    _In_ HANDLE Disk,
    _In_ ULONG Number)
{
    union
    {
        FILE_FS_ATTRIBUTE_INFORMATION Information;
        UCHAR Buffer[FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName) + MAX_PATH * sizeof(WCHAR)];
    } Attribute;
    static const WCHAR Raw[] = L"RAW";
    IO_STATUS_BLOCK IoStatus;
    DISK_GEOMETRY_EX Geometry;
    DWORD Returned;
    ULONG SectorSize;
    NTSTATUS Status;
    PBYTE Buffer;

    if (!DeviceIoControl(Disk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &Geometry, sizeof(Geometry), &Returned, NULL) ||
        Geometry.Geometry.BytesPerSector == 0 ||
        Geometry.DiskSize.QuadPart < 2 * (LONGLONG)Geometry.Geometry.BytesPerSector)
    {
        skip("Disk %lu: no media geometry (error %lu)\n", Number, GetLastError());
        return;
    }
    SectorSize = Geometry.Geometry.BytesPerSector;

    RtlZeroMemory(&Attribute, sizeof(Attribute));
    Status = NtQueryVolumeInformationFile(Disk, &IoStatus, &Attribute, sizeof(Attribute), FileFsAttributeInformation);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(Attribute.Information.FileSystemNameLength == sizeof(Raw) - sizeof(WCHAR) &&
           !memcmp(Attribute.Information.FileSystemName, Raw, sizeof(Raw) - sizeof(WCHAR)),
           "Disk %lu: file system %s, expected 'RAW'\n", Number,
           wine_dbgstr_wn(Attribute.Information.FileSystemName,
                          Attribute.Information.FileSystemNameLength / sizeof(WCHAR)));
    }

    Buffer = VirtualAlloc(NULL, SectorSize * 2, MEM_COMMIT, PAGE_READWRITE);
    if (!Buffer)
    {
        skip("Disk %lu: out of memory\n", Number);
        return;
    }

    CheckSeekAndRead(Disk, Number, SectorSize, SectorSize, Buffer);
    CheckSeekAndRead(Disk, Number, Geometry.DiskSize.QuadPart - SectorSize, SectorSize, Buffer);

    VirtualFree(Buffer, 0, MEM_RELEASE);
}

START_TEST(PhysicalDriveRaw)
{
    WCHAR Path[MAX_PATH];
    ULONG Number, Tested = 0, Denied = 0;
    HANDLE Disk;

    for (Number = 0; Number < MAX_DISKS; Number++)
    {
        StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", Number);
        Disk = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (Disk == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_ACCESS_DENIED)
                Denied++;
            continue;
        }

        CheckDisk(Disk, Number);
        CloseHandle(Disk);
        Tested++;
    }

    if (Tested == 0)
        skip("No disk could be opened (%lu denied)\n", Denied);
}
