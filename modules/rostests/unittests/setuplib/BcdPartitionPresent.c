/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which BCD partition devices name a partition that still exists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winioctl.h>
#include <ntddstor.h>
#include <strsafe.h>
#include <bcddev.h>

#define ELEMENT_SIZE     0x58
#define TYPE_OFFSET      0x10
#define SIZE_OFFSET      0x18
#define PARTITION_OFFSET 0x20
#define STYLE_OFFSET     0x34
#define DISK_OFFSET      0x38
#define DEVICE_PARTITION 6
#define BODY_SIZE        0x48
#define STYLE_MBR        1
#define MAX_DISKS        16

typedef union _DRIVE_LAYOUT
{
    DRIVE_LAYOUT_INFORMATION_EX Layout;
    UCHAR Buffer[FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + 128 * sizeof(PARTITION_INFORMATION_EX)];
} DRIVE_LAYOUT;

static
VOID
MakeElement(
    _In_ const DRIVE_LAYOUT_INFORMATION_EX* Layout,
    _In_ const PARTITION_INFORMATION_EX* Partition,
    _Out_writes_bytes_(ELEMENT_SIZE) PUCHAR Element)
{
    ULONG Type = DEVICE_PARTITION, Size = BODY_SIZE, Style = STYLE_MBR;

    ZeroMemory(Element, ELEMENT_SIZE);
    CopyMemory(Element + TYPE_OFFSET, &Type, sizeof(Type));
    CopyMemory(Element + SIZE_OFFSET, &Size, sizeof(Size));
    if (Layout->PartitionStyle == PARTITION_STYLE_GPT)
    {
        CopyMemory(Element + PARTITION_OFFSET, &Partition->Gpt.PartitionId, sizeof(GUID));
        CopyMemory(Element + DISK_OFFSET, &Layout->Gpt.DiskId, sizeof(GUID));
    }
    else
    {
        CopyMemory(Element + PARTITION_OFFSET, &Partition->StartingOffset, sizeof(ULONGLONG));
        CopyMemory(Element + STYLE_OFFSET, &Style, sizeof(Style));
        CopyMemory(Element + DISK_OFFSET, &Layout->Mbr.Signature, sizeof(ULONG));
    }
}

static
VOID
TestPartition(
    _In_ ULONG Disk,
    _In_ const DRIVE_LAYOUT_INFORMATION_EX* Layout,
    _In_ const PARTITION_INFORMATION_EX* Partition)
{
    UCHAR Element[ELEMENT_SIZE], Changed[ELEMENT_SIZE];
    PCSTR Style = (Layout->PartitionStyle == PARTITION_STYLE_GPT) ? "GPT" : "MBR";
    ULONG Type = 5;

    MakeElement(Layout, Partition, Element);
    trace("%s disk %lu partition %lu\n", Style, Disk, Partition->PartitionNumber);
    ok(IsBcdPartitionElementPresent(Element), "%s disk %lu partition %lu is not found\n",
       Style, Disk, Partition->PartitionNumber);

    CopyMemory(Changed, Element, sizeof(Changed));
    Changed[PARTITION_OFFSET + 1] ^= 0x5A;
    ok(!IsBcdPartitionElementPresent(Changed), "%s disk %lu partition %lu: a partition that does not exist is found\n",
       Style, Disk, Partition->PartitionNumber);

    CopyMemory(Changed, Element, sizeof(Changed));
    Changed[DISK_OFFSET + 1] ^= 0x5A;
    ok(!IsBcdPartitionElementPresent(Changed), "%s disk %lu partition %lu: a disk that does not exist is found\n",
       Style, Disk, Partition->PartitionNumber);

    CopyMemory(Changed, Element, sizeof(Changed));
    CopyMemory(Changed + TYPE_OFFSET, &Type, sizeof(Type));
    Changed[PARTITION_OFFSET + 1] ^= 0x5A;
    ok(IsBcdPartitionElementPresent(Changed), "%s disk %lu partition %lu: a device that is not a partition is taken as missing\n",
       Style, Disk, Partition->PartitionNumber);
}

START_TEST(BcdPartitionPresent)
{
    const PARTITION_INFORMATION_EX* Partition;
    DRIVE_LAYOUT Drive;
    WCHAR Path[MAX_PATH];
    HANDLE Handle;
    DWORD Bytes;
    ULONG Disk, i, Gpt = 0, Mbr = 0;
    BOOL Ok;

    for (Disk = 0; Disk < MAX_DISKS; Disk++)
    {
        StringCchPrintfW(Path, MAX_PATH, L"\\\\.\\PhysicalDrive%lu", Disk);
        Handle = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (Handle == INVALID_HANDLE_VALUE)
            continue;
        Ok = DeviceIoControl(Handle, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, &Drive, sizeof(Drive), &Bytes, NULL);
        CloseHandle(Handle);
        if (!Ok || Drive.Layout.PartitionStyle == PARTITION_STYLE_RAW)
            continue;

        for (i = 0; i < Drive.Layout.PartitionCount; i++)
        {
            Partition = &Drive.Layout.PartitionEntry[i];
            if (Partition->PartitionStyle == PARTITION_STYLE_MBR &&
                (Partition->Mbr.PartitionType == PARTITION_ENTRY_UNUSED ||
                 IsContainerPartition(Partition->Mbr.PartitionType)))
            {
                continue;
            }

            TestPartition(Disk, &Drive.Layout, Partition);
            if (Drive.Layout.PartitionStyle == PARTITION_STYLE_GPT)
                Gpt++;
            else
                Mbr++;
        }
    }

    trace("%lu GPT and %lu MBR partitions checked\n", Gpt, Mbr);
    if (Gpt + Mbr == 0)
        skip("No disk can be read as Setup reads it (%lu)\n", GetLastError());
}
