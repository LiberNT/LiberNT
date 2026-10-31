/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for IOCTL_DISK_GET_DRIVE_LAYOUT_EX on blank and MBR disks
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <winioctl.h>
#include <strsafe.h>

#define LAYOUT_ENTRIES 128

static BOOL
IsZero(
    _In_reads_bytes_(Size) const BYTE *Buffer,
    _In_ ULONG Size)
{
    ULONG i;

    for (i = 0; i < Size; i++)
    {
        if (Buffer[i])
            return FALSE;
    }
    return TRUE;
}

static BOOL
HasProtectiveEntry(
    _In_reads_bytes_(512) const BYTE *Sector)
{
    ULONG i;

    for (i = 0; i < 4; i++)
    {
        if (Sector[0x1BE + i * 16 + 4] == 0xEE)
            return TRUE;
    }
    return FALSE;
}

static void
TestDisk(
    _In_ ULONG Number,
    _Inout_ PULONG BlankDisks,
    _Inout_ PULONG MbrDisks)
{
    WCHAR Path[MAX_PATH];
    DISK_GEOMETRY_EX Geometry;
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    DWORD Size = FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry[LAYOUT_ENTRIES]);
    DWORD Returned;
    PBYTE Sector;
    HANDLE Disk;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", Number);
    Disk = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Disk == INVALID_HANDLE_VALUE)
        return;

    if (!DeviceIoControl(Disk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &Geometry, sizeof(Geometry), &Returned,
                         NULL) ||
        Geometry.Geometry.MediaType != FixedMedia || Geometry.Geometry.BytesPerSector < 512)
    {
        CloseHandle(Disk);
        return;
    }

    Sector = HeapAlloc(GetProcessHeap(), 0, Geometry.Geometry.BytesPerSector);
    Layout = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (!Sector || !Layout)
    {
        skip("Out of memory\n");
        goto Quit;
    }
    if (!ReadFile(Disk, Sector, Geometry.Geometry.BytesPerSector, &Returned, NULL) ||
        Returned != Geometry.Geometry.BytesPerSector)
    {
        skip("Disk %lu: reading sector 0 failed with %lu\n", Number, GetLastError());
        goto Quit;
    }

    if (!DeviceIoControl(Disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, Layout, Size, &Returned, NULL))
    {
        ok(0, "Disk %lu: IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed with %lu\n", Number, GetLastError());
        goto Quit;
    }

    if (IsZero(Sector, Geometry.Geometry.BytesPerSector))
    {
        (*BlankDisks)++;
        ok(Layout->PartitionStyle == PARTITION_STYLE_MBR, "Disk %lu: blank disk style %lu\n", Number,
           Layout->PartitionStyle);
        ok(Layout->PartitionCount == 0, "Disk %lu: blank disk has %lu entries\n", Number, Layout->PartitionCount);
        ok(Layout->Mbr.Signature == 0, "Disk %lu: blank disk signature 0x%08lx\n", Number, Layout->Mbr.Signature);
        ok(Returned == FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry), "Disk %lu: returned %lu\n", Number,
           Returned);
    }
    else if (Sector[510] == 0x55 && Sector[511] == 0xAA && !HasProtectiveEntry(Sector))
    {
        (*MbrDisks)++;
        ok(Layout->PartitionStyle == PARTITION_STYLE_MBR, "Disk %lu: MBR disk style %lu\n", Number,
           Layout->PartitionStyle);
        ok(Layout->PartitionCount >= 4 && Layout->PartitionCount % 4 == 0, "Disk %lu: MBR disk has %lu entries\n",
           Number, Layout->PartitionCount);
        ok(Layout->Mbr.Signature == *(ULONG UNALIGNED *)&Sector[0x1B8], "Disk %lu: MBR signature 0x%08lx\n", Number,
           Layout->Mbr.Signature);
    }

Quit:
    if (Layout)
        HeapFree(GetProcessHeap(), 0, Layout);
    if (Sector)
        HeapFree(GetProcessHeap(), 0, Sector);
    CloseHandle(Disk);
}

START_TEST(DriveLayout)
{
    ULONG Number, BlankDisks = 0, MbrDisks = 0;

    for (Number = 0; Number < 32; Number++)
        TestDisk(Number, &BlankDisks, &MbrDisks);

    if (!MbrDisks)
        skip("No fixed MBR disk\n");
    if (!BlankDisks)
        skip("No blank fixed disk; attach a zero-filled disk to check its layout\n");
}
