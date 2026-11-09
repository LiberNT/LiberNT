/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The master boot record and the chain of logical drives behind it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fstubp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/* How deep a chain of logical drives is followed before it is called a loop */
#define FSTUB_MBR_CHAIN_LIMIT 512

/* Writes a starting sector back into the four bytes the record keeps it in */
#define FSTUB_SET_STARTING_SECTOR(p, s)                 \
    ((p)->StartingSectorLsb0 =  (s) & 0xFF,             \
     (p)->StartingSectorLsb1 = ((s) >> 8 ) & 0xFF,      \
     (p)->StartingSectorMsb0 = ((s) >> 16) & 0xFF,      \
     (p)->StartingSectorMsb1 = ((s) >> 24) & 0xFF)

/* PRIVATE FUNCTIONS **********************************************************/

/* The entry array of a boot record read into the scratch sector */
static
PPARTITION_DESCRIPTOR
FstubMbrEntries(
    _In_ PFSTUB_DISK Disk)
{
    return (PPARTITION_DESCRIPTOR)(Disk->Sector + FSTUB_PARTITION_TABLE_OFFSET);
}

/* Whether what is in the scratch sector ends the way a boot record must */
static
BOOLEAN
FstubMbrSignatureIsValid(
    _In_ PFSTUB_DISK Disk)
{
    PUSHORT Signature;

    if (Disk->BytesPerSector <= FSTUB_BOOT_SIGNATURE_OFFSET) return FALSE;

    Signature = (PUSHORT)(Disk->Sector + FSTUB_BOOT_SIGNATURE_OFFSET);

    return *Signature == FSTUB_BOOT_SIGNATURE;
}

/* The number the disk is known by, as the boot record carries it */
static
ULONG
FstubMbrSignature(
    _In_ PFSTUB_DISK Disk)
{
    return *(PULONG)(Disk->Sector + FSTUB_DISK_SIGNATURE_OFFSET);
}

static
ULONG
FstubMbrCheckSum(
    _In_ PFSTUB_DISK Disk)
{
    PULONG Values = (PULONG)Disk->Sector;
    ULONG Count = Disk->BytesPerSector / sizeof(ULONG);
    ULONG Sum = 0;
    ULONG i;

    for (i = 0; i < Count; i++) Sum += Values[i];

    return ~Sum + 1;
}

/* Whether an entry describes no partition at all */
static
BOOLEAN
FstubMbrEntryIsEmpty(
    _In_ PPARTITION_DESCRIPTOR Entry)
{
    return Entry->PartitionType == PARTITION_ENTRY_UNUSED ||
           GET_PARTITION_LENGTH(Entry) == 0;
}

/* Fills one layout entry from one boot-record entry */
static
VOID
FstubMbrCopyEntry(
    _Out_ PPARTITION_INFORMATION_EX Partition,
    _In_ PPARTITION_DESCRIPTOR Entry,
    _In_ ULONG BytesPerSector,
    _In_ ULONGLONG BaseSector,
    _In_ ULONG PartitionNumber)
{
    RtlZeroMemory(Partition, sizeof(*Partition));

    Partition->PartitionStyle = PARTITION_STYLE_MBR;
    Partition->StartingOffset.QuadPart =
        (LONGLONG)((BaseSector + GET_STARTING_SECTOR(Entry)) * BytesPerSector);
    Partition->PartitionLength.QuadPart =
        (LONGLONG)((ULONGLONG)GET_PARTITION_LENGTH(Entry) * BytesPerSector);
    Partition->PartitionNumber = PartitionNumber;
    Partition->RewritePartition = FALSE;

    Partition->Mbr.PartitionType = Entry->PartitionType;
    Partition->Mbr.BootIndicator = (Entry->ActiveFlag & 0x80) != 0;
    Partition->Mbr.RecognizedPartition =
        IsRecognizedPartition(Entry->PartitionType) ? TRUE : FALSE;
    Partition->Mbr.HiddenSectors = GET_STARTING_SECTOR(Entry);
}

static
VOID
FstubMbrWriteEntry(
    _Out_ PPARTITION_DESCRIPTOR Entry,
    _In_ PPARTITION_INFORMATION_EX Partition,
    _In_ PFSTUB_DISK Disk,
    _In_ ULONGLONG BaseSector)
{
    ULONGLONG StartSector;
    ULONGLONG SectorCount;
    ULONG SectorsPerCylinder;
    ULONG Cylinder;
    ULONG Head;
    ULONG Sector;

    RtlZeroMemory(Entry, sizeof(*Entry));

    StartSector = (ULONGLONG)Partition->StartingOffset.QuadPart /
                  Disk->BytesPerSector;
    SectorCount = (ULONGLONG)Partition->PartitionLength.QuadPart /
                  Disk->BytesPerSector;

    if (SectorCount == 0) return;

    StartSector -= BaseSector;

    Entry->PartitionType = Partition->Mbr.PartitionType;
    Entry->ActiveFlag = Partition->Mbr.BootIndicator ? 0x80 : 0x00;

    FSTUB_SET_STARTING_SECTOR(Entry,
                              (StartSector > MAXULONG) ? MAXULONG
                                                       : (ULONG)StartSector);
    SET_PARTITION_LENGTH(Entry,
                         (SectorCount > MAXULONG) ? MAXULONG
                                                  : (ULONG)SectorCount);

    SectorsPerCylinder = Disk->SectorsPerTrack * Disk->TracksPerCylinder;

    if (SectorsPerCylinder == 0 || Disk->SectorsPerTrack == 0) return;

    Cylinder = (ULONG)((BaseSector + StartSector) / SectorsPerCylinder);
    Head = (ULONG)(((BaseSector + StartSector) % SectorsPerCylinder) /
                   Disk->SectorsPerTrack);
    Sector = (ULONG)((BaseSector + StartSector) % Disk->SectorsPerTrack) + 1;

    if (Cylinder > 1023)
    {
        Entry->StartingTrack = 0xFE;
        Entry->StartingCylinderLsb = 0xFF;
        Entry->StartingCylinderMsb = 0xFF;
    }
    else
    {
        Entry->StartingTrack = (UCHAR)Head;
        Entry->StartingCylinderLsb = (UCHAR)(Cylinder & 0xFF);
        Entry->StartingCylinderMsb =
            (UCHAR)(((Cylinder >> 2) & 0xC0) | (Sector & 0x3F));
    }

    Cylinder = (ULONG)((BaseSector + StartSector + SectorCount - 1) /
                       SectorsPerCylinder);
    Head = (ULONG)(((BaseSector + StartSector + SectorCount - 1) %
                    SectorsPerCylinder) / Disk->SectorsPerTrack);
    Sector = (ULONG)((BaseSector + StartSector + SectorCount - 1) %
                     Disk->SectorsPerTrack) + 1;

    if (Cylinder > 1023)
    {
        Entry->EndingTrack = 0xFE;
        Entry->EndingCylinderLsb = 0xFF;
        Entry->EndingCylinderMsb = 0xFF;
    }
    else
    {
        Entry->EndingTrack = (UCHAR)Head;
        Entry->EndingCylinderLsb = (UCHAR)(Cylinder & 0xFF);
        Entry->EndingCylinderMsb =
            (UCHAR)(((Cylinder >> 2) & 0xC0) | (Sector & 0x3F));
    }
}

/* Puts an empty boot record in the scratch sector, keeping its first part */
static
VOID
FstubMbrPrepareSector(
    _In_ PFSTUB_DISK Disk,
    _In_ BOOLEAN KeepCode)
{
    PUSHORT Signature;

    if (!KeepCode) RtlZeroMemory(Disk->Sector, FSTUB_DISK_SIGNATURE_OFFSET);

    RtlZeroMemory(Disk->Sector + FSTUB_PARTITION_TABLE_OFFSET,
                  FSTUB_MBR_ENTRY_COUNT * sizeof(PARTITION_DESCRIPTOR));

    Signature = (PUSHORT)(Disk->Sector + FSTUB_BOOT_SIGNATURE_OFFSET);
    *Signature = FSTUB_BOOT_SIGNATURE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FstubReadPartitionTableMBR(
    _In_ PFSTUB_DISK Disk,
    _In_ BOOLEAN ReturnRecognizedPartitions,
    _Outptr_ PDRIVE_LAYOUT_INFORMATION_EX *Layout)
{
    PDRIVE_LAYOUT_INFORMATION_EX Result = NULL;
    PPARTITION_DESCRIPTOR Entries;
    PARTITION_DESCRIPTOR Primary[FSTUB_MBR_ENTRY_COUNT];
    ULONGLONG ExtendedStart = 0;
    ULONGLONG ChainSector = 0;
    NTSTATUS Status;
    ULONG PartitionNumber = 0;
    ULONG Count = 0;
    ULONG Signature;
    ULONG CheckSum;
    ULONG Follows = 0;
    ULONG i;

    PAGED_CODE();

    *Layout = NULL;

    Status = FstubReadSector(Disk, 1, 0, Disk->Sector);
    if (!NT_SUCCESS(Status)) return Status;

    if (!FstubMbrSignatureIsValid(Disk)) return STATUS_DISK_CORRUPT_ERROR;

    Signature = FstubMbrSignature(Disk);
    CheckSum = FstubMbrCheckSum(Disk);

    Entries = FstubMbrEntries(Disk);
    RtlCopyMemory(Primary, Entries, sizeof(Primary));

    Status = FstubAdjustPartitionCount(PARTITION_STYLE_MBR,
                                       FSTUB_MBR_ENTRY_COUNT,
                                       &Result);
    if (!NT_SUCCESS(Status)) return Status;

    for (i = 0; i < FSTUB_MBR_ENTRY_COUNT; i++)
    {
        if (FstubMbrEntryIsEmpty(&Primary[i]))
        {
            RtlZeroMemory(&Result->PartitionEntry[Count],
                          sizeof(PARTITION_INFORMATION_EX));
            Result->PartitionEntry[Count].PartitionStyle = PARTITION_STYLE_MBR;
            Count++;
            continue;
        }

        if (IsContainerPartition(Primary[i].PartitionType))
        {
            if (ExtendedStart == 0)
            {
                ExtendedStart = GET_STARTING_SECTOR(&Primary[i]);
                ChainSector = ExtendedStart;
            }

            FstubMbrCopyEntry(&Result->PartitionEntry[Count],
                              &Primary[i],
                              Disk->BytesPerSector,
                              0,
                              0);
            Count++;
            continue;
        }

        if (ReturnRecognizedPartitions &&
            !IsRecognizedPartition(Primary[i].PartitionType))
        {
            RtlZeroMemory(&Result->PartitionEntry[Count],
                          sizeof(PARTITION_INFORMATION_EX));
            Result->PartitionEntry[Count].PartitionStyle = PARTITION_STYLE_MBR;
            Count++;
            continue;
        }

        FstubMbrCopyEntry(&Result->PartitionEntry[Count],
                          &Primary[i],
                          Disk->BytesPerSector,
                          0,
                          ++PartitionNumber);
        Count++;
    }

    while (ChainSector != 0 && Follows < FSTUB_MBR_CHAIN_LIMIT)
    {
        PARTITION_DESCRIPTOR Logical;
        PARTITION_DESCRIPTOR Link;
        ULONGLONG Base = ChainSector;

        Follows++;

        Status = FstubReadSector(Disk, 1, Base, Disk->Sector);
        if (!NT_SUCCESS(Status)) break;

        if (!FstubMbrSignatureIsValid(Disk)) break;

        Entries = FstubMbrEntries(Disk);
        Logical = Entries[0];
        Link = Entries[1];

        ChainSector = 0;

        if (!FstubMbrEntryIsEmpty(&Link) &&
            IsContainerPartition(Link.PartitionType))
        {
            ChainSector = ExtendedStart + GET_STARTING_SECTOR(&Link);

            /* A link that points at or behind its own record is a loop */
            if (ChainSector <= Base) ChainSector = 0;
        }

        if (FstubMbrEntryIsEmpty(&Logical)) continue;

        if (ReturnRecognizedPartitions &&
            !IsRecognizedPartition(Logical.PartitionType))
        {
            continue;
        }

        Status = FstubAdjustPartitionCount(PARTITION_STYLE_MBR,
                                           Count + 1,
                                           &Result);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Result, TAG_FSTUB);
            return Status;
        }

        FstubMbrCopyEntry(&Result->PartitionEntry[Count],
                          &Logical,
                          Disk->BytesPerSector,
                          Base,
                          ++PartitionNumber);
        Count++;
    }

    Result->PartitionStyle = PARTITION_STYLE_MBR;
    Result->PartitionCount = Count;
    Result->Mbr.Signature = Signature;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS1)
    Result->Mbr.CheckSum = CheckSum;
#endif

    *Layout = Result;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FstubWritePartitionTableMBR(
    _In_ PFSTUB_DISK Disk,
    _In_ PDRIVE_LAYOUT_INFORMATION_EX Layout)
{
    PPARTITION_DESCRIPTOR Entries;
    ULONGLONG ExtendedStart = 0;
    ULONGLONG ChainSector;
    ULONGLONG NextSector;
    NTSTATUS Status;
    ULONG Primaries;
    ULONG i;

    PAGED_CODE();

    if (Layout->PartitionStyle != PARTITION_STYLE_MBR)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = FstubReadSector(Disk, 1, 0, Disk->Sector);
    if (!NT_SUCCESS(Status)) return Status;

    FstubMbrPrepareSector(Disk, FstubMbrSignatureIsValid(Disk));

    *(PULONG)(Disk->Sector + FSTUB_DISK_SIGNATURE_OFFSET) =
        Layout->Mbr.Signature;

    Entries = FstubMbrEntries(Disk);
    Primaries = min(Layout->PartitionCount, FSTUB_MBR_ENTRY_COUNT);

    for (i = 0; i < Primaries; i++)
    {
        FstubMbrWriteEntry(&Entries[i],
                           &Layout->PartitionEntry[i],
                           Disk,
                           0);

        if (IsContainerPartition(Entries[i].PartitionType) &&
            ExtendedStart == 0)
        {
            ExtendedStart = GET_STARTING_SECTOR(&Entries[i]);
        }
    }

    Status = FstubWriteSector(Disk, 1, 0, Disk->Sector);
    if (!NT_SUCCESS(Status)) return Status;

    if (Layout->PartitionCount <= FSTUB_MBR_ENTRY_COUNT)
    {
        return STATUS_SUCCESS;
    }

    if (ExtendedStart == 0) return STATUS_INVALID_PARAMETER;

    ChainSector = ExtendedStart;

    for (i = FSTUB_MBR_ENTRY_COUNT; i < Layout->PartitionCount; i++)
    {
        PPARTITION_INFORMATION_EX Partition = &Layout->PartitionEntry[i];

        RtlZeroMemory(Disk->Sector, Disk->BytesPerSector);
        FstubMbrPrepareSector(Disk, FALSE);

        Entries = FstubMbrEntries(Disk);

        FstubMbrWriteEntry(&Entries[0], Partition, Disk, ChainSector);

        if (i + 1 < Layout->PartitionCount)
        {
            PPARTITION_INFORMATION_EX Next = &Layout->PartitionEntry[i + 1];

            NextSector = (ULONGLONG)Next->StartingOffset.QuadPart /
                         Disk->BytesPerSector;

            if (NextSector > ExtendedStart)
            {
                RtlZeroMemory(&Entries[1], sizeof(Entries[1]));
                Entries[1].PartitionType = PARTITION_EXTENDED;
                FSTUB_SET_STARTING_SECTOR(
                    &Entries[1],
                    (ULONG)(NextSector - ExtendedStart - 1));
                SET_PARTITION_LENGTH(
                    &Entries[1],
                    (ULONG)((ULONGLONG)Next->PartitionLength.QuadPart /
                            Disk->BytesPerSector + 1));
            }
        }

        Status = FstubWriteSector(Disk, 1, ChainSector, Disk->Sector);
        if (!NT_SUCCESS(Status)) return Status;

        if (i + 1 >= Layout->PartitionCount) break;

        ChainSector = (ULONGLONG)Layout->PartitionEntry[i + 1].StartingOffset.QuadPart /
                      Disk->BytesPerSector;

        if (ChainSector == 0) return STATUS_INVALID_PARAMETER;

        ChainSector -= 1;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FstubCreateDiskMBR(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PCREATE_DISK_MBR DiskInfo)
{
    PFSTUB_DISK Disk;
    NTSTATUS Status;

    PAGED_CODE();

    Status = FstubAllocateDiskInformation(DeviceObject, &Disk, NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubReadSector(Disk, 1, 0, Disk->Sector);

    FstubMbrPrepareSector(Disk, NT_SUCCESS(Status) &&
                                    FstubMbrSignatureIsValid(Disk));

    *(PULONG)(Disk->Sector + FSTUB_DISK_SIGNATURE_OFFSET) = DiskInfo->Signature;

    Status = FstubWriteSector(Disk, 1, 0, Disk->Sector);

    FstubFreeDiskInformation(Disk);

    return Status;
}

NTSTATUS
NTAPI
FstubCreateDiskRaw(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PFSTUB_DISK Disk;
    NTSTATUS Status;

    PAGED_CODE();

    Status = FstubAllocateDiskInformation(DeviceObject, &Disk, NULL);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(Disk->Sector, Disk->BytesPerSector);

    Status = FstubWriteSector(Disk, 1, 0, Disk->Sector);

    if (NT_SUCCESS(Status) && Disk->SectorCount > EFI_PARTITION_HEADER_LBA)
    {
        /* A GUID header left behind would still be found, so it goes too */
        FstubWriteSector(Disk, 1, EFI_PARTITION_HEADER_LBA, Disk->Sector);
        FstubWriteSector(Disk, 1, Disk->SectorCount - 1, Disk->Sector);
    }

    FstubFreeDiskInformation(Disk);

    return Status;
}

PDRIVE_LAYOUT_INFORMATION
NTAPI
FstubConvertExtendedToLayout(
    _In_ PDRIVE_LAYOUT_INFORMATION_EX LayoutEx)
{
    PDRIVE_LAYOUT_INFORMATION Layout;
    ULONG Size;
    ULONG i;

    PAGED_CODE();

    if (LayoutEx->PartitionStyle != PARTITION_STYLE_MBR) return NULL;

    Size = FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION, PartitionEntry) +
           LayoutEx->PartitionCount * sizeof(PARTITION_INFORMATION);

    Layout = ExAllocatePoolZero(NonPagedPool, Size, TAG_FSTUB);
    if (Layout == NULL) return NULL;

    Layout->PartitionCount = LayoutEx->PartitionCount;
    Layout->Signature = LayoutEx->Mbr.Signature;

    for (i = 0; i < LayoutEx->PartitionCount; i++)
    {
        PPARTITION_INFORMATION_EX Source = &LayoutEx->PartitionEntry[i];
        PPARTITION_INFORMATION Target = &Layout->PartitionEntry[i];

        Target->StartingOffset = Source->StartingOffset;
        Target->PartitionLength = Source->PartitionLength;
        Target->HiddenSectors = Source->Mbr.HiddenSectors;
        Target->PartitionNumber = Source->PartitionNumber;
        Target->PartitionType = Source->Mbr.PartitionType;
        Target->BootIndicator = Source->Mbr.BootIndicator;
        Target->RecognizedPartition = Source->Mbr.RecognizedPartition;
        Target->RewritePartition = Source->RewritePartition;
    }

    return Layout;
}

/* EOF */
