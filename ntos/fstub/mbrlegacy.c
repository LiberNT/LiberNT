/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The partition routines that predate partition styles, and the defaults behind them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fstubp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/* Says a legacy layout the way the style-aware routines want to hear it */
static
NTSTATUS
FstubConvertLayoutToExtended(
    _In_ PDRIVE_LAYOUT_INFORMATION Layout,
    _Outptr_ PDRIVE_LAYOUT_INFORMATION_EX *LayoutEx)
{
    PDRIVE_LAYOUT_INFORMATION_EX Result = NULL;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    *LayoutEx = NULL;

    if (Layout->PartitionCount == 0) return STATUS_INVALID_PARAMETER;

    Status = FstubAdjustPartitionCount(PARTITION_STYLE_MBR,
                                       Layout->PartitionCount,
                                       &Result);
    if (!NT_SUCCESS(Status)) return Status;

    Result->PartitionStyle = PARTITION_STYLE_MBR;
    Result->PartitionCount = Layout->PartitionCount;
    Result->Mbr.Signature = Layout->Signature;

    for (i = 0; i < Layout->PartitionCount; i++)
    {
        PPARTITION_INFORMATION Source = &Layout->PartitionEntry[i];
        PPARTITION_INFORMATION_EX Target = &Result->PartitionEntry[i];

        RtlZeroMemory(Target, sizeof(*Target));

        Target->PartitionStyle = PARTITION_STYLE_MBR;
        Target->StartingOffset = Source->StartingOffset;
        Target->PartitionLength = Source->PartitionLength;
        Target->PartitionNumber = Source->PartitionNumber;
        Target->RewritePartition = Source->RewritePartition;

        Target->Mbr.PartitionType = Source->PartitionType;
        Target->Mbr.BootIndicator = Source->BootIndicator;
        Target->Mbr.RecognizedPartition = Source->RecognizedPartition;
        Target->Mbr.HiddenSectors = Source->HiddenSectors;
    }

    *LayoutEx = Result;
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
FstubFixupEfiPartition(
    _In_ PPARTITION_DESCRIPTOR PartitionDescriptor,
    _In_ ULONGLONG MaxOffset)
{
    ULONG StartingSector;
    ULONGLONG Length;

    if (PartitionDescriptor->PartitionType != PARTITION_GPT) return;

    StartingSector = GET_STARTING_SECTOR(PartitionDescriptor);

    if (MaxOffset <= StartingSector) return;

    Length = MaxOffset - StartingSector;

    SET_PARTITION_LENGTH(PartitionDescriptor,
                         (Length > MAXULONG) ? MAXULONG : (ULONG)Length);
}

VOID
FASTCALL
xHalExamineMBR(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ ULONG MbrTypeIdentifier,
    _Out_ PVOID *MbrBuffer)
{
    PPARTITION_DESCRIPTOR Entries;
    DISK_GEOMETRY_EX Geometry;
    PFSTUB_DISK Disk;
    NTSTATUS Status;
    PVOID Buffer;

    PAGED_CODE();

    *MbrBuffer = NULL;

    if (SectorSize <= FSTUB_BOOT_SIGNATURE_OFFSET) return;

    Status = FstubGetDiskGeometry(DeviceObject, &Geometry);
    if (!NT_SUCCESS(Status)) return;

    Geometry.Geometry.BytesPerSector = SectorSize;

    Status = FstubAllocateDiskInformation(DeviceObject, &Disk, &Geometry);
    if (!NT_SUCCESS(Status)) return;

    Status = FstubReadSector(Disk, 1, 0, Disk->Sector);

    if (NT_SUCCESS(Status) &&
        *(PUSHORT)(Disk->Sector + FSTUB_BOOT_SIGNATURE_OFFSET) ==
            FSTUB_BOOT_SIGNATURE)
    {
        Entries = (PPARTITION_DESCRIPTOR)(Disk->Sector +
                                          FSTUB_PARTITION_TABLE_OFFSET);

        if (Entries[0].PartitionType == MbrTypeIdentifier)
        {
            Buffer = ExAllocatePoolZero(NonPagedPool, SectorSize, TAG_FSTUB);

            if (Buffer != NULL)
            {
                RtlCopyMemory(Buffer, Disk->Sector, SectorSize);
                *MbrBuffer = Buffer;
            }
        }
    }

    FstubFreeDiskInformation(Disk);
}

NTSTATUS
FASTCALL
xHalIoReadPartitionTable(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ BOOLEAN ReturnRecognizedPartitions,
    _Out_ PDRIVE_LAYOUT_INFORMATION *PartitionBuffer)
{
    PDRIVE_LAYOUT_INFORMATION_EX LayoutEx;
    DISK_GEOMETRY_EX Geometry;
    PFSTUB_DISK Disk;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    *PartitionBuffer = NULL;

    if (SectorSize == 0) return STATUS_INVALID_PARAMETER;

    Status = FstubGetDiskGeometry(DeviceObject, &Geometry);
    if (!NT_SUCCESS(Status)) return Status;

    Geometry.Geometry.BytesPerSector = SectorSize;

    Status = FstubAllocateDiskInformation(DeviceObject, &Disk, &Geometry);
    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubReadPartitionTableMBR(Disk,
                                        ReturnRecognizedPartitions,
                                        &LayoutEx);

    if (NT_SUCCESS(Status))
    {
        for (i = 0; i < LayoutEx->PartitionCount; i++)
        {
            PPARTITION_INFORMATION_EX Entry = &LayoutEx->PartitionEntry[i];

            if (Entry->Mbr.PartitionType != PARTITION_GPT) continue;

            Entry->PartitionLength.QuadPart =
                (LONGLONG)((Disk->SectorCount -
                            (ULONGLONG)Entry->StartingOffset.QuadPart /
                                SectorSize) * SectorSize);
        }

        *PartitionBuffer = FstubConvertExtendedToLayout(LayoutEx);

        if (*PartitionBuffer == NULL) Status = STATUS_INSUFFICIENT_RESOURCES;

        ExFreePoolWithTag(LayoutEx, TAG_FSTUB);
    }

    FstubFreeDiskInformation(Disk);

    return Status;
}

NTSTATUS
FASTCALL
xHalIoWritePartitionTable(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ ULONG SectorsPerTrack,
    _In_ ULONG NumberOfHeads,
    _In_ PDRIVE_LAYOUT_INFORMATION PartitionBuffer)
{
    PDRIVE_LAYOUT_INFORMATION_EX LayoutEx;
    DISK_GEOMETRY_EX Geometry;
    PFSTUB_DISK Disk;
    NTSTATUS Status;

    PAGED_CODE();

    if (SectorSize == 0 || PartitionBuffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = FstubGetDiskGeometry(DeviceObject, &Geometry);
    if (!NT_SUCCESS(Status)) return Status;

    Geometry.Geometry.BytesPerSector = SectorSize;

    if (SectorsPerTrack != 0) Geometry.Geometry.SectorsPerTrack = SectorsPerTrack;
    if (NumberOfHeads != 0) Geometry.Geometry.TracksPerCylinder = NumberOfHeads;

    Status = FstubAllocateDiskInformation(DeviceObject, &Disk, &Geometry);
    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubConvertLayoutToExtended(PartitionBuffer, &LayoutEx);

    if (NT_SUCCESS(Status))
    {
        Status = FstubWritePartitionTableMBR(Disk, LayoutEx);
        ExFreePoolWithTag(LayoutEx, TAG_FSTUB);
    }

    FstubFreeDiskInformation(Disk);

    return Status;
}

/* Changes the type byte of one partition of a boot record */
NTSTATUS
FASTCALL
xHalIoSetPartitionInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ ULONG PartitionNumber,
    _In_ ULONG PartitionType)
{
    SET_PARTITION_INFORMATION_EX Information;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(SectorSize);

    RtlZeroMemory(&Information, sizeof(Information));

    Information.PartitionStyle = PARTITION_STYLE_MBR;
    Information.Mbr.PartitionType = (UCHAR)PartitionType;

    return IoSetPartitionInformationEx(DeviceObject,
                                       PartitionNumber,
                                       &Information);
}

VOID
FASTCALL
HalExamineMBR(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ ULONG MBRTypeIdentifier,
    _Out_ PVOID *Buffer)
{
    HALDISPATCH->HalExamineMBR(DeviceObject,
                               SectorSize,
                               MBRTypeIdentifier,
                               Buffer);
}

NTSTATUS
FASTCALL
IoReadPartitionTable(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ BOOLEAN ReturnRecognizedPartitions,
    _Out_ struct _DRIVE_LAYOUT_INFORMATION **PartitionBuffer)
{
    return HALDISPATCH->HalIoReadPartitionTable(DeviceObject,
                                                SectorSize,
                                                ReturnRecognizedPartitions,
                                                PartitionBuffer);
}

NTSTATUS
FASTCALL
IoWritePartitionTable(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ ULONG SectorsPerTrack,
    _In_ ULONG NumberOfHeads,
    _In_ struct _DRIVE_LAYOUT_INFORMATION *PartitionBuffer)
{
    return HALDISPATCH->HalIoWritePartitionTable(DeviceObject,
                                                 SectorSize,
                                                 SectorsPerTrack,
                                                 NumberOfHeads,
                                                 PartitionBuffer);
}

NTSTATUS
FASTCALL
IoSetPartitionInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ ULONG PartitionNumber,
    _In_ ULONG PartitionType)
{
    return HALDISPATCH->HalIoSetPartitionInformation(DeviceObject,
                                                     SectorSize,
                                                     PartitionNumber,
                                                     PartitionType);
}

/* EOF */
