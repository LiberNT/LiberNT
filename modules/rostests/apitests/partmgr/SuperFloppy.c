/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for the layout removable media without a partition table reports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>
#include <strsafe.h>

#define LAYOUT_ENTRIES 16

static BOOL
HasPartitionEntry(
    _In_reads_bytes_(512) const BYTE *Sector)
{
    ULONG i;

    if (Sector[0x1FE] != 0x55 || Sector[0x1FF] != 0xAA)
        return FALSE;

    for (i = 0; i < 4; i++)
    {
        const BYTE *Entry = Sector + 0x1BE + i * 16;
        ULONG j;

        for (j = 0; j < 16; j++)
        {
            if (Entry[j] != 0)
                return TRUE;
        }
    }
    return FALSE;
}

static BOOL
IsFileSystemBootSector(
    _In_reads_bytes_(512) const BYTE *Sector)
{
    USHORT BytesPerSector;
    BYTE SectorsPerCluster;

    if (Sector[0] != 0xEB && Sector[0] != 0xE9)
        return FALSE;
    if (memcmp(Sector + 3, "EXFAT   ", 8) == 0)
        return TRUE;

    BytesPerSector = (USHORT)(Sector[11] | (Sector[12] << 8));
    SectorsPerCluster = Sector[13];
    if (BytesPerSector < 512 || BytesPerSector > 4096 ||
        (BytesPerSector & (BytesPerSector - 1)) != 0)
    {
        return FALSE;
    }
    return SectorsPerCluster != 0 && SectorsPerCluster <= 128 &&
           (SectorsPerCluster & (SectorsPerCluster - 1)) == 0;
}

/*
 * Windows 7 x64, a FAT32 volume written over a whole removable USB disk:
 * one MBR partition number 1 at offset 0 for the whole medium, type 0x04,
 * recognized, not bootable, no hidden sectors, and the layout signature is 1.
 * The same image on a fixed disk reports four empty entries instead.
 */
START_TEST(SuperFloppy)
{
    BYTE LayoutBuffer[sizeof(DRIVE_LAYOUT_INFORMATION_EX) +
                      LAYOUT_ENTRIES * sizeof(PARTITION_INFORMATION_EX)];
    PDRIVE_LAYOUT_INFORMATION_EX Layout = (PDRIVE_LAYOUT_INFORMATION_EX)LayoutBuffer;
    BYTE Sector[512];
    WCHAR Path[64];
    DISK_GEOMETRY_EX Geometry;
    ULONG Number, Tested = 0;

    for (Number = 0; Number < 8; Number++)
    {
        PPARTITION_INFORMATION_EX Entry;
        DWORD Returned;
        HANDLE Disk;

        StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", Number);
        Disk = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
        if (Disk == INVALID_HANDLE_VALUE)
            continue;

        memset(&Geometry, 0, sizeof(Geometry));
        if (!DeviceIoControl(Disk, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
                            &Geometry, sizeof(Geometry), &Returned, NULL) ||
            Geometry.Geometry.MediaType != RemovableMedia ||
            Geometry.Geometry.BytesPerSector != sizeof(Sector))
        {
            CloseHandle(Disk);
            continue;
        }

        if (!ReadFile(Disk, Sector, sizeof(Sector), &Returned, NULL) ||
            Returned != sizeof(Sector) ||
            HasPartitionEntry(Sector) ||
            !IsFileSystemBootSector(Sector))
        {
            CloseHandle(Disk);
            continue;
        }

        Tested++;
        memset(LayoutBuffer, 0, sizeof(LayoutBuffer));
        if (!DeviceIoControl(Disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0,
                             LayoutBuffer, sizeof(LayoutBuffer), &Returned, NULL))
        {
            ok(FALSE, "Disk %lu: IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed with %lu\n",
               Number, GetLastError());
            CloseHandle(Disk);
            continue;
        }

        ok(Layout->PartitionStyle == PARTITION_STYLE_MBR,
           "Disk %lu: PartitionStyle = %lu\n", Number, Layout->PartitionStyle);
        ok(Layout->PartitionCount == 1,
           "Disk %lu: PartitionCount = %lu\n", Number, Layout->PartitionCount);
        ok(Layout->Mbr.Signature == 1,
           "Disk %lu: Signature = %08lx\n", Number, Layout->Mbr.Signature);
        if (Layout->PartitionStyle != PARTITION_STYLE_MBR || Layout->PartitionCount < 1)
        {
            CloseHandle(Disk);
            continue;
        }

        Entry = &Layout->PartitionEntry[0];
        ok(Entry->PartitionStyle == PARTITION_STYLE_MBR,
           "Disk %lu: entry style = %u\n", Number, Entry->PartitionStyle);
        ok(Entry->PartitionNumber == 1,
           "Disk %lu: PartitionNumber = %lu\n", Number, Entry->PartitionNumber);
        ok(Entry->StartingOffset.QuadPart == 0,
           "Disk %lu: StartingOffset = %I64u\n", Number, Entry->StartingOffset.QuadPart);
        ok(Entry->PartitionLength.QuadPart == Geometry.DiskSize.QuadPart,
           "Disk %lu: PartitionLength = %I64u, disk size %I64u\n",
           Number, Entry->PartitionLength.QuadPart, Geometry.DiskSize.QuadPart);
        ok(Entry->Mbr.PartitionType == PARTITION_FAT_16,
           "Disk %lu: PartitionType = %02x\n", Number, Entry->Mbr.PartitionType);
        ok(Entry->Mbr.RecognizedPartition == TRUE,
           "Disk %lu: RecognizedPartition = %u\n", Number, Entry->Mbr.RecognizedPartition);
        ok(Entry->Mbr.BootIndicator == FALSE,
           "Disk %lu: BootIndicator = %u\n", Number, Entry->Mbr.BootIndicator);
        ok(Entry->Mbr.HiddenSectors == 0,
           "Disk %lu: HiddenSectors = %lu\n", Number, Entry->Mbr.HiddenSectors);

        CloseHandle(Disk);
    }

    if (Tested == 0)
        skip("No removable disk without a partition table is attached\n");
}
