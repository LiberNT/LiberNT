/*
 * PROJECT:     LiberNT Setup Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Partition device elements of the boot configuration data
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include "devutils.h"
#include "bcddev.h"

BOOLEAN
IsBcdPartitionElementPresent(
    _In_reads_bytes_(BCD_DEVICE_ELEMENT_SIZE) const UCHAR *Element)
{
    union
    {
        DRIVE_LAYOUT_INFORMATION_EX Layout;
        UCHAR Buffer[FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + 128 * sizeof(PARTITION_INFORMATION_EX)];
    } Drive;
    SYSTEM_DEVICE_INFORMATION DeviceInformation;
    const PARTITION_INFORMATION_EX* Entry;
    IO_STATUS_BLOCK IoStatusBlock;
    WCHAR Path[MAX_PATH];
    ULONG Type, Style, Signature, i, j;
    ULONGLONG Offset;
    HANDLE Handle;
    NTSTATUS Status;

    RtlCopyMemory(&Type, Element + BCD_DEVICE_TYPE_OFFSET, sizeof(Type));
    if (Type != BCD_DEVICE_PARTITION)
        return TRUE;

    RtlCopyMemory(&Style, Element + BCD_DEVICE_STYLE_OFFSET, sizeof(Style));
    RtlCopyMemory(&Offset, Element + BCD_DEVICE_PARTITION_OFFSET, sizeof(Offset));
    RtlCopyMemory(&Signature, Element + BCD_DEVICE_DISK_OFFSET, sizeof(Signature));

    Status = NtQuerySystemInformation(SystemDeviceInformation,
                                      &DeviceInformation,
                                      sizeof(DeviceInformation),
                                      NULL);
    if (!NT_SUCCESS(Status))
        return TRUE;

    for (i = 0; i < DeviceInformation.NumberOfDisks; i++)
    {
        RtlStringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Device\\Harddisk%lu\\Partition0", i);
        Status = pOpenDevice(Path, &Handle);
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND)
            continue;
        if (!NT_SUCCESS(Status))
            return TRUE;

        Status = NtDeviceIoControlFile(Handle, NULL, NULL, NULL, &IoStatusBlock,
                                       IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
                                       NULL, 0, &Drive, sizeof(Drive));
        NtClose(Handle);
        if (Status == STATUS_NO_MEDIA_IN_DEVICE)
            continue;
        if (!NT_SUCCESS(Status))
            return TRUE;

        ASSERT(Drive.Layout.PartitionCount <= 128);
        if (Style == BCD_PARTITION_STYLE_MBR)
        {
            if (Drive.Layout.PartitionStyle != PARTITION_STYLE_MBR || Drive.Layout.Mbr.Signature != Signature)
                continue;

            for (j = 0; j < Drive.Layout.PartitionCount; j++)
            {
                Entry = &Drive.Layout.PartitionEntry[j];
                if (Entry->PartitionStyle == PARTITION_STYLE_MBR &&
                    Entry->Mbr.PartitionType != PARTITION_ENTRY_UNUSED &&
                    !IsContainerPartition(Entry->Mbr.PartitionType) &&
                    (ULONGLONG)Entry->StartingOffset.QuadPart == Offset)
                {
                    return TRUE;
                }
            }
        }
        else
        {
            if (Drive.Layout.PartitionStyle != PARTITION_STYLE_GPT ||
                !RtlEqualMemory(&Drive.Layout.Gpt.DiskId, Element + BCD_DEVICE_DISK_OFFSET, sizeof(GUID)))
            {
                continue;
            }

            for (j = 0; j < Drive.Layout.PartitionCount; j++)
            {
                Entry = &Drive.Layout.PartitionEntry[j];
                if (Entry->PartitionStyle == PARTITION_STYLE_GPT &&
                    RtlEqualMemory(&Entry->Gpt.PartitionId, Element + BCD_DEVICE_PARTITION_OFFSET, sizeof(GUID)))
                {
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}
