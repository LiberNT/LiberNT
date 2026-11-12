/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading, writing, checking and creating a partition table whatever style it is in
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fstubp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/* How much room a layout holding this many entries takes */
static
ULONG
FstubLayoutSize(
    _In_ ULONG PartitionCount)
{
    return FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) +
           PartitionCount * sizeof(PARTITION_INFORMATION_EX);
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FstubAdjustPartitionCount(
    _In_ ULONG PartitionStyle,
    _In_ ULONG PartitionCount,
    _Inout_ PDRIVE_LAYOUT_INFORMATION_EX *Layout)
{
    PDRIVE_LAYOUT_INFORMATION_EX Result;
    PDRIVE_LAYOUT_INFORMATION_EX Current = *Layout;
    ULONG Wanted;
    ULONG Held;

    PAGED_CODE();

    if (PartitionCount == 0) return STATUS_INVALID_PARAMETER;

    Wanted = FstubLayoutSize(PartitionCount);

    if (Current != NULL && Current->PartitionCount >= PartitionCount)
    {
        return STATUS_SUCCESS;
    }

    Result = ExAllocatePoolZero(NonPagedPool, Wanted, TAG_FSTUB);
    if (Result == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (Current != NULL)
    {
        Held = FstubLayoutSize(Current->PartitionCount);
        RtlCopyMemory(Result, Current, Held);
        ExFreePoolWithTag(Current, TAG_FSTUB);
    }
    else
    {
        Result->PartitionStyle = PartitionStyle;
    }

    Result->PartitionCount = PartitionCount;

    *Layout = Result;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FstubDetectPartitionStyle(
    _In_ PFSTUB_DISK Disk,
    _Out_ PARTITION_STYLE *PartitionStyle)
{
    PPARTITION_DESCRIPTOR Entries;
    EFI_PARTITION_HEADER Header;
    NTSTATUS Status;
    BOOLEAN Protective = FALSE;
    ULONG i;

    PAGED_CODE();

    *PartitionStyle = PARTITION_STYLE_RAW;

    Status = FstubReadSector(Disk, 1, 0, Disk->Sector);
    if (!NT_SUCCESS(Status)) return Status;

    if (Disk->BytesPerSector > FSTUB_BOOT_SIGNATURE_OFFSET &&
        *(PUSHORT)(Disk->Sector + FSTUB_BOOT_SIGNATURE_OFFSET) ==
            FSTUB_BOOT_SIGNATURE)
    {
        Entries = (PPARTITION_DESCRIPTOR)(Disk->Sector +
                                          FSTUB_PARTITION_TABLE_OFFSET);

        for (i = 0; i < FSTUB_MBR_ENTRY_COUNT; i++)
        {
            if (Entries[i].PartitionType == PARTITION_GPT) Protective = TRUE;
        }

        *PartitionStyle = PARTITION_STYLE_MBR;
    }

    if (NT_SUCCESS(FstubReadHeaderEFI(Disk, FALSE, &Header)) ||
        NT_SUCCESS(FstubReadHeaderEFI(Disk, TRUE, &Header)))
    {
        *PartitionStyle = PARTITION_STYLE_GPT;
        return STATUS_SUCCESS;
    }

    if (Protective) *PartitionStyle = PARTITION_STYLE_RAW;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoReadPartitionTableEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ struct _DRIVE_LAYOUT_INFORMATION_EX **DriveLayout)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout = NULL;
    PARTITION_STYLE Style;
    PFSTUB_DISK Disk;
    NTSTATUS Status;

    PAGED_CODE();

    if (DeviceObject == NULL || DriveLayout == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *DriveLayout = NULL;

    Status = FstubAllocateDiskInformation(DeviceObject, &Disk, NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubDetectPartitionStyle(Disk, &Style);

    if (NT_SUCCESS(Status))
    {
        if (Style == PARTITION_STYLE_GPT)
        {
            Status = FstubReadPartitionTableEFI(Disk, FALSE, &Layout);

            if (!NT_SUCCESS(Status))
            {
                Status = FstubReadPartitionTableEFI(Disk, TRUE, &Layout);
            }
        }
        else if (Style == PARTITION_STYLE_MBR)
        {
            Status = FstubReadPartitionTableMBR(Disk, FALSE, &Layout);
        }
        else
        {
            Status = FstubAdjustPartitionCount(PARTITION_STYLE_RAW,
                                               1,
                                               &Layout);
            if (NT_SUCCESS(Status))
            {
                Layout->PartitionStyle = PARTITION_STYLE_RAW;
                Layout->PartitionCount = 0;
            }
        }
    }

    FstubFreeDiskInformation(Disk);

    if (!NT_SUCCESS(Status))
    {
        if (Layout != NULL) ExFreePoolWithTag(Layout, TAG_FSTUB);
        return Status;
    }

    *DriveLayout = Layout;
    return STATUS_SUCCESS;
}

/* Writes a layout back to the disk, in the style the layout itself names */
NTSTATUS
NTAPI
IoWritePartitionTableEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ struct _DRIVE_LAYOUT_INFORMATION_EX *DriveLayout)
{
    PFSTUB_DISK Disk;
    NTSTATUS Status;

    PAGED_CODE();

    if (DeviceObject == NULL || DriveLayout == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = FstubAllocateDiskInformation(DeviceObject, &Disk, NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (DriveLayout->PartitionStyle == PARTITION_STYLE_GPT)
    {
        Status = FstubWritePartitionTableEFI(Disk, DriveLayout);
    }
    else if (DriveLayout->PartitionStyle == PARTITION_STYLE_MBR)
    {
        Status = FstubWritePartitionTableMBR(Disk, DriveLayout);
    }
    else
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    FstubFreeDiskInformation(Disk);

    return Status;
}

NTSTATUS
NTAPI
IoSetPartitionInformationEx(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG PartitionNumber,
    _In_ struct _SET_PARTITION_INFORMATION_EX *PartitionInfo)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PARTITION_STYLE Style;
    PFSTUB_DISK Disk;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    if (DeviceObject == NULL || PartitionInfo == NULL || PartitionNumber == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = FstubAllocateDiskInformation(DeviceObject, &Disk, NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubDetectPartitionStyle(Disk, &Style);

    if (NT_SUCCESS(Status) && Style != (PARTITION_STYLE)PartitionInfo->PartitionStyle)
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    if (NT_SUCCESS(Status) && Style == PARTITION_STYLE_GPT)
    {
        Status = FstubSetPartitionInformationEFI(Disk,
                                                PartitionNumber,
                                                &PartitionInfo->Gpt);
        FstubFreeDiskInformation(Disk);
        return Status;
    }

    if (NT_SUCCESS(Status) && Style != PARTITION_STYLE_MBR)
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    if (!NT_SUCCESS(Status))
    {
        FstubFreeDiskInformation(Disk);
        return Status;
    }

    Status = FstubReadPartitionTableMBR(Disk, FALSE, &Layout);

    if (NT_SUCCESS(Status))
    {
        Status = STATUS_INVALID_PARAMETER;

        for (i = 0; i < Layout->PartitionCount; i++)
        {
            if (Layout->PartitionEntry[i].PartitionNumber != PartitionNumber)
            {
                continue;
            }

            Layout->PartitionEntry[i].Mbr.PartitionType =
                PartitionInfo->Mbr.PartitionType;
            Layout->PartitionEntry[i].RewritePartition = TRUE;
            Status = STATUS_SUCCESS;
            break;
        }

        if (NT_SUCCESS(Status))
        {
            Status = FstubWritePartitionTableMBR(Disk, Layout);
        }

        ExFreePoolWithTag(Layout, TAG_FSTUB);
    }

    FstubFreeDiskInformation(Disk);

    return Status;
}

NTSTATUS
NTAPI
IoVerifyPartitionTable(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN FixErrors)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PARTITION_STYLE Style;
    PFSTUB_DISK Disk;
    NTSTATUS Status;

    PAGED_CODE();

    if (DeviceObject == NULL) return STATUS_INVALID_PARAMETER;

    Status = FstubAllocateDiskInformation(DeviceObject, &Disk, NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubDetectPartitionStyle(Disk, &Style);

    if (NT_SUCCESS(Status))
    {
        if (Style == PARTITION_STYLE_GPT)
        {
            Status = FstubVerifyPartitionTableEFI(Disk, FixErrors);
        }
        else if (Style == PARTITION_STYLE_MBR)
        {
            Status = FstubReadPartitionTableMBR(Disk, FALSE, &Layout);

            if (NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Layout, TAG_FSTUB);
            }
            else
            {
                Status = STATUS_DISK_CORRUPT_ERROR;
            }
        }
        else
        {
            /* A disk with no table has no table to be wrong about */
            Status = STATUS_SUCCESS;
        }
    }

    FstubFreeDiskInformation(Disk);

    return Status;
}

NTSTATUS
NTAPI
IoReadDiskSignature(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG BytesPerSector,
    _Out_ PDISK_SIGNATURE Signature)
{
    DISK_GEOMETRY_EX Geometry;
    EFI_PARTITION_HEADER Header;
    PARTITION_STYLE Style;
    PFSTUB_DISK Disk;
    NTSTATUS Status;
    PULONG Values;
    ULONG Count;
    ULONG Sum = 0;
    ULONG i;

    PAGED_CODE();

    if (DeviceObject == NULL || Signature == NULL || BytesPerSector == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Signature, sizeof(*Signature));

    Status = FstubGetDiskGeometry(DeviceObject, &Geometry);
    if (!NT_SUCCESS(Status)) return Status;

    /* The caller's sector size wins, because it is reading the same disk */
    Geometry.Geometry.BytesPerSector = BytesPerSector;

    Status = FstubAllocateDiskInformation(DeviceObject, &Disk, &Geometry);
    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubDetectPartitionStyle(Disk, &Style);

    if (NT_SUCCESS(Status) && Style == PARTITION_STYLE_GPT)
    {
        Status = FstubReadHeaderEFI(Disk, FALSE, &Header);

        if (!NT_SUCCESS(Status))
        {
            Status = FstubReadHeaderEFI(Disk, TRUE, &Header);
        }

        if (NT_SUCCESS(Status))
        {
            Signature->PartitionStyle = PARTITION_STYLE_GPT;
            Signature->Gpt.DiskId = Header.DiskGUID;
        }
        else
        {
            Status = STATUS_DISK_CORRUPT_ERROR;
        }

        FstubFreeDiskInformation(Disk);
        return Status;
    }

    if (NT_SUCCESS(Status) && Style != PARTITION_STYLE_MBR)
    {
        Status = STATUS_DISK_CORRUPT_ERROR;
    }

    if (NT_SUCCESS(Status))
    {
        Status = FstubReadSector(Disk, 1, 0, Disk->Sector);
    }

    if (NT_SUCCESS(Status))
    {
        Values = (PULONG)Disk->Sector;
        Count = Disk->BytesPerSector / sizeof(ULONG);

        for (i = 0; i < Count; i++) Sum += Values[i];

        Signature->PartitionStyle = PARTITION_STYLE_MBR;
        Signature->Mbr.Signature =
            *(PULONG)(Disk->Sector + FSTUB_DISK_SIGNATURE_OFFSET);
        Signature->Mbr.CheckSum = ~Sum + 1;
    }

    FstubFreeDiskInformation(Disk);

    return Status;
}

NTSTATUS
NTAPI
IoCreateDisk(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ struct _CREATE_DISK *Disk)
{
    PAGED_CODE();

    if (DeviceObject == NULL) return STATUS_INVALID_PARAMETER;

    if (Disk == NULL) return FstubCreateDiskRaw(DeviceObject);

    if (Disk->PartitionStyle == PARTITION_STYLE_MBR)
    {
        return FstubCreateDiskMBR(DeviceObject, &Disk->Mbr);
    }

    if (Disk->PartitionStyle == PARTITION_STYLE_GPT)
    {
        return FstubCreateDiskEFI(DeviceObject, &Disk->Gpt);
    }

    if (Disk->PartitionStyle == PARTITION_STYLE_RAW)
    {
        return FstubCreateDiskRaw(DeviceObject);
    }

    return STATUS_INVALID_PARAMETER;
}

/* EOF */
