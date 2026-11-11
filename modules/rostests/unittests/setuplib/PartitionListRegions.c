/*
 * PROJECT:     LiberNT Setup Library - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that every region of the partition list lies inside its disk
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

extern HANDLE ProcessHeap;

static
ULONGLONG
DiskLength(
    _In_ ULONG DiskNumber)
{
    GET_LENGTH_INFORMATION Length;
    WCHAR Path[MAX_PATH];
    HANDLE Disk;
    DWORD Returned;
    BOOL Success;

    swprintf(Path, ARRAYSIZE(Path), L"\\\\.\\PhysicalDrive%lu", DiskNumber);
    Disk = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (Disk == INVALID_HANDLE_VALUE)
        return 0;
    Success = DeviceIoControl(Disk, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &Length, sizeof(Length), &Returned, NULL);
    CloseHandle(Disk);
    return Success ? (ULONGLONG)Length.Length.QuadPart : 0;
}

static
VOID
CheckRegions(
    _In_ PDISKENTRY Disk,
    _In_ ULONGLONG Sectors,
    _In_ PLIST_ENTRY Head,
    _Inout_ PULONG Regions)
{
    PLIST_ENTRY Entry;
    PPARTENTRY Part;

    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        Part = CONTAINING_RECORD(Entry, PARTENTRY, ListEntry);
        ok(Part->StartSector.QuadPart <= Sectors && Part->SectorCount.QuadPart <= Sectors - Part->StartSector.QuadPart,
           "Disk %lu of %I64u sectors lists a %s region of %I64u sectors at %I64u\n", Disk->DiskNumber, Sectors,
           Part->IsPartitioned ? "partitioned" : "free", Part->SectorCount.QuadPart, Part->StartSector.QuadPart);
        (*Regions)++;
    }
}

START_TEST(PartitionListRegions)
{
    PPARTLIST List;
    PLIST_ENTRY Entry;
    PDISKENTRY Disk;
    ULONGLONG Length;
    ULONG Disks = 0, Regions = 0;

    ProcessHeap = GetProcessHeap();
    List = CreatePartitionList();
    ok(List != NULL, "CreatePartitionList failed\n");
    if (!List)
        return;

    for (Entry = List->DiskListHead.Flink; Entry != &List->DiskListHead; Entry = Entry->Flink)
    {
        Disk = CONTAINING_RECORD(Entry, DISKENTRY, ListEntry);
        Disks++;
        Length = DiskLength(Disk->DiskNumber);
        ok(Length != 0 && Disk->BytesPerSector != 0, "Disk %lu has no length (%lu bytes per sector)\n",
           Disk->DiskNumber, Disk->BytesPerSector);
        if (!Length || !Disk->BytesPerSector)
            continue;
        CheckRegions(Disk, Length / Disk->BytesPerSector, &Disk->PrimaryPartListHead, &Regions);
        CheckRegions(Disk, Length / Disk->BytesPerSector, &Disk->LogicalPartListHead, &Regions);
    }
    ok(Disks != 0, "No disk was listed\n");
    trace("%lu disks, %lu regions\n", Disks, Regions);
    DestroyPartitionList(List);
}
