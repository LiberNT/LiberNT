/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The GUID partition table, its copy at the end of the disk and the record that protects both
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fstubp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
ULONG
FstubHeaderCRC32(
    _In_ PEFI_PARTITION_HEADER Header)
{
    EFI_PARTITION_HEADER Copy = *Header;
    ULONG Size = Header->HeaderSize;

    if (Size < sizeof(EFI_PARTITION_HEADER) || Size > sizeof(Copy))
    {
        Size = sizeof(EFI_PARTITION_HEADER);
    }

    Copy.HeaderCRC32 = 0;

    return RtlComputeCrc32(0, (const UCHAR *)&Copy, Size);
}

/* How many sectors the entry array of a header takes up */
static
ULONG
FstubEntryArraySectors(
    _In_ ULONG NumberOfEntries,
    _In_ ULONG SizeOfPartitionEntry,
    _In_ ULONG BytesPerSector)
{
    ULONGLONG Bytes = (ULONGLONG)NumberOfEntries * SizeOfPartitionEntry;

    return (ULONG)((Bytes + BytesPerSector - 1) / BytesPerSector);
}

/* Whether a header makes sense on a disk of this size */
static
BOOLEAN
FstubHeaderIsValid(
    _In_ PEFI_PARTITION_HEADER Header,
    _In_ PFSTUB_DISK Disk,
    _In_ ULONGLONG ExpectedLba)
{
    if (Header->Signature != EFI_PARTITION_TABLE_SIGNATURE) return FALSE;
    if (Header->Revision != EFI_PARTITION_TABLE_REVISION) return FALSE;

    if (Header->HeaderSize < sizeof(EFI_PARTITION_HEADER)) return FALSE;
    if (Header->HeaderSize > Disk->BytesPerSector) return FALSE;

    if (Header->MyLBA != ExpectedLba) return FALSE;

    if (Header->SizeOfPartitionEntry < EFI_PARTITION_ENTRY_SIZE) return FALSE;
    if ((Header->SizeOfPartitionEntry % EFI_PARTITION_ENTRY_SIZE) != 0) return FALSE;

    if (Header->NumberOfEntries == 0) return FALSE;

    if (Header->FirstUsableLBA > Header->LastUsableLBA) return FALSE;

    if (Disk->SectorCount != 0 && Header->LastUsableLBA >= Disk->SectorCount)
    {
        return FALSE;
    }

    if (Header->HeaderCRC32 != FstubHeaderCRC32(Header)) return FALSE;

    return TRUE;
}

static
NTSTATUS
FstubReadEntryArray(
    _In_ PFSTUB_DISK Disk,
    _In_ PEFI_PARTITION_HEADER Header,
    _Outptr_ PUCHAR *Array,
    _Out_ PULONG ArraySize)
{
    NTSTATUS Status;
    PUCHAR Buffer;
    ULONG Sectors;
    ULONG Size;

    *Array = NULL;
    *ArraySize = 0;

    Sectors = FstubEntryArraySectors(Header->NumberOfEntries,
                                     Header->SizeOfPartitionEntry,
                                     Disk->BytesPerSector);
    if (Sectors == 0) return STATUS_DISK_CORRUPT_ERROR;

    Size = Sectors * Disk->BytesPerSector;

    Buffer = ExAllocatePoolZero(NonPagedPool, Size, TAG_FSTUB);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = FstubReadSector(Disk,
                             Sectors,
                             Header->PartitionEntryLBA,
                             Buffer);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_FSTUB);
        return Status;
    }

    if (RtlComputeCrc32(0,
                        Buffer,
                        Header->NumberOfEntries *
                            Header->SizeOfPartitionEntry) !=
        Header->PartitionEntryCRC32)
    {
        ExFreePoolWithTag(Buffer, TAG_FSTUB);
        return STATUS_DISK_CORRUPT_ERROR;
    }

    *Array = Buffer;
    *ArraySize = Size;

    return STATUS_SUCCESS;
}

/* Whether an entry describes no partition at all */
static
BOOLEAN
FstubEntryIsUnused(
    _In_ PEFI_PARTITION_ENTRY Entry)
{
    if (IsEqualGUID(&Entry->PartitionType, &PARTITION_ENTRY_UNUSED_GUID))
    {
        return TRUE;
    }

    return Entry->EndingLBA < Entry->StartingLBA;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FstubReadHeaderEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ BOOLEAN ReadBackupTable,
    _Out_ PEFI_PARTITION_HEADER Header)
{
    ULONGLONG Lba;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(Header, sizeof(*Header));

    if (ReadBackupTable)
    {
        if (Disk->SectorCount < 2) return STATUS_DISK_CORRUPT_ERROR;

        Lba = Disk->SectorCount - 1;
    }
    else
    {
        Lba = EFI_PARTITION_HEADER_LBA;
    }

    Status = FstubReadSector(Disk, 1, Lba, Disk->Sector);
    if (!NT_SUCCESS(Status)) return Status;

    RtlCopyMemory(Header, Disk->Sector, sizeof(*Header));

    if (!FstubHeaderIsValid(Header, Disk, Lba))
    {
        RtlZeroMemory(Header, sizeof(*Header));
        return STATUS_DISK_CORRUPT_ERROR;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FstubWriteHeaderEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ ULONG PartitionEntryArraySectors,
    _In_ LPCGUID DiskGuid,
    _In_ ULONG NumberOfEntries,
    _In_ ULONGLONG FirstUsableLBA,
    _In_ ULONGLONG LastUsableLBA,
    _In_ ULONG ArrayCRC32,
    _In_ BOOLEAN WriteBackupTable)
{
    PEFI_PARTITION_HEADER Header;
    ULONGLONG BackupLba;

    PAGED_CODE();

    if (Disk->SectorCount < 2) return STATUS_INVALID_PARAMETER;

    BackupLba = Disk->SectorCount - 1;

    RtlZeroMemory(Disk->Sector, Disk->BytesPerSector);
    Header = (PEFI_PARTITION_HEADER)Disk->Sector;

    Header->Signature = EFI_PARTITION_TABLE_SIGNATURE;
    Header->Revision = EFI_PARTITION_TABLE_REVISION;
    Header->HeaderSize = sizeof(EFI_PARTITION_HEADER);
    Header->DiskGUID = *DiskGuid;
    Header->NumberOfEntries = NumberOfEntries;
    Header->SizeOfPartitionEntry = EFI_PARTITION_ENTRY_SIZE;
    Header->PartitionEntryCRC32 = ArrayCRC32;
    Header->FirstUsableLBA = FirstUsableLBA;
    Header->LastUsableLBA = LastUsableLBA;

    if (WriteBackupTable)
    {
        Header->MyLBA = BackupLba;
        Header->AlternateLBA = EFI_PARTITION_HEADER_LBA;
        Header->PartitionEntryLBA = BackupLba - PartitionEntryArraySectors;
    }
    else
    {
        Header->MyLBA = EFI_PARTITION_HEADER_LBA;
        Header->AlternateLBA = BackupLba;
        Header->PartitionEntryLBA = EFI_PARTITION_HEADER_LBA + 1;
    }

    Header->HeaderCRC32 = FstubHeaderCRC32(Header);

    return FstubWriteSector(Disk, 1, Header->MyLBA, Disk->Sector);
}

/* Says one GUID entry the way a layout entry says it */
VOID
NTAPI
FstubCopyEntryEFI(
    _Out_ PPARTITION_INFORMATION_EX PartitionEntry,
    _In_ PEFI_PARTITION_ENTRY Entry,
    _In_ ULONG BytesPerSector)
{
    RtlZeroMemory(PartitionEntry, sizeof(*PartitionEntry));

    PartitionEntry->PartitionStyle = PARTITION_STYLE_GPT;
    PartitionEntry->StartingOffset.QuadPart =
        (LONGLONG)(Entry->StartingLBA * BytesPerSector);
    PartitionEntry->PartitionLength.QuadPart =
        (LONGLONG)((Entry->EndingLBA - Entry->StartingLBA + 1) * BytesPerSector);
    PartitionEntry->RewritePartition = FALSE;

    PartitionEntry->Gpt.PartitionType = Entry->PartitionType;
    PartitionEntry->Gpt.PartitionId = Entry->UniquePartition;
    PartitionEntry->Gpt.Attributes = Entry->Attributes;

    RtlCopyMemory(PartitionEntry->Gpt.Name,
                  Entry->Name,
                  sizeof(PartitionEntry->Gpt.Name));
}

/* And the other way round */
VOID
NTAPI
FstubWriteEntryEFI(
    _Out_ PEFI_PARTITION_ENTRY Entry,
    _In_ PPARTITION_INFORMATION_EX PartitionEntry,
    _In_ ULONG BytesPerSector)
{
    ULONGLONG StartingLBA;
    ULONGLONG Sectors;

    RtlZeroMemory(Entry, sizeof(*Entry));

    StartingLBA = (ULONGLONG)PartitionEntry->StartingOffset.QuadPart /
                  BytesPerSector;
    Sectors = (ULONGLONG)PartitionEntry->PartitionLength.QuadPart /
              BytesPerSector;

    if (Sectors == 0) return;

    Entry->PartitionType = PartitionEntry->Gpt.PartitionType;
    Entry->UniquePartition = PartitionEntry->Gpt.PartitionId;
    Entry->StartingLBA = StartingLBA;
    Entry->EndingLBA = StartingLBA + Sectors - 1;
    Entry->Attributes = PartitionEntry->Gpt.Attributes;

    RtlCopyMemory(Entry->Name,
                  PartitionEntry->Gpt.Name,
                  sizeof(Entry->Name));
}

NTSTATUS
NTAPI
FstubReadPartitionTableEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ BOOLEAN ReadBackupTable,
    _Outptr_ PDRIVE_LAYOUT_INFORMATION_EX *Layout)
{
    PDRIVE_LAYOUT_INFORMATION_EX Result = NULL;
    PEFI_PARTITION_ENTRY Entry;
    EFI_PARTITION_HEADER Header;
    NTSTATUS Status;
    PUCHAR Array;
    ULONG ArraySize;
    ULONG Count = 0;
    ULONG i;

    PAGED_CODE();

    *Layout = NULL;

    Status = FstubReadHeaderEFI(Disk, ReadBackupTable, &Header);
    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubReadEntryArray(Disk, &Header, &Array, &ArraySize);
    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubAdjustPartitionCount(PARTITION_STYLE_GPT,
                                       Header.NumberOfEntries,
                                       &Result);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Array, TAG_FSTUB);
        return Status;
    }

    for (i = 0; i < Header.NumberOfEntries; i++)
    {
        if ((i + 1) * Header.SizeOfPartitionEntry > ArraySize) break;

        Entry = (PEFI_PARTITION_ENTRY)(Array + i * Header.SizeOfPartitionEntry);

        if (FstubEntryIsUnused(Entry)) continue;

        FstubCopyEntryEFI(&Result->PartitionEntry[Count],
                          Entry,
                          Disk->BytesPerSector);
        Result->PartitionEntry[Count].PartitionNumber = i + 1;
        Count++;
    }

    ExFreePoolWithTag(Array, TAG_FSTUB);

    Result->PartitionStyle = PARTITION_STYLE_GPT;
    Result->PartitionCount = Count;
    Result->Gpt.DiskId = Header.DiskGUID;
    Result->Gpt.StartingUsableOffset.QuadPart =
        (LONGLONG)(Header.FirstUsableLBA * Disk->BytesPerSector);
    Result->Gpt.UsableLength.QuadPart =
        (LONGLONG)((Header.LastUsableLBA - Header.FirstUsableLBA + 1) *
                   Disk->BytesPerSector);
    Result->Gpt.MaxPartitionCount = Header.NumberOfEntries;

    *Layout = Result;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FstubWriteBootSectorEFI(
    _In_ PFSTUB_DISK Disk)
{
    PPARTITION_DESCRIPTOR Entry;
    PUSHORT Signature;
    ULONGLONG Sectors;

    PAGED_CODE();

    RtlZeroMemory(Disk->Sector, Disk->BytesPerSector);

    Entry = (PPARTITION_DESCRIPTOR)(Disk->Sector + FSTUB_PARTITION_TABLE_OFFSET);

    Entry->PartitionType = PARTITION_GPT;
    Entry->StartingTrack = 0;
    Entry->StartingCylinderLsb = 2;
    Entry->StartingCylinderMsb = 0;
    Entry->EndingTrack = 0xFE;
    Entry->EndingCylinderLsb = 0xFF;
    Entry->EndingCylinderMsb = 0xFF;

    Entry->StartingSectorLsb0 = 1;

    Sectors = (Disk->SectorCount > 1) ? Disk->SectorCount - 1 : 0;
    SET_PARTITION_LENGTH(Entry,
                         (Sectors > MAXULONG) ? MAXULONG : (ULONG)Sectors);

    Signature = (PUSHORT)(Disk->Sector + FSTUB_BOOT_SIGNATURE_OFFSET);
    *Signature = FSTUB_BOOT_SIGNATURE;

    return FstubWriteSector(Disk, 1, 0, Disk->Sector);
}

NTSTATUS
NTAPI
FstubWritePartitionTableEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ PDRIVE_LAYOUT_INFORMATION_EX Layout)
{
    PEFI_PARTITION_ENTRY Entries;
    NTSTATUS Status;
    ULONGLONG FirstUsable;
    ULONGLONG LastUsable;
    ULONGLONG BackupArrayLba;
    PUCHAR Array;
    ULONG NumberOfEntries;
    ULONG Sectors;
    ULONG ArrayBytes;
    ULONG ArrayCRC32;
    ULONG i;
    GUID DiskId;

    PAGED_CODE();

    if (Layout->PartitionStyle != PARTITION_STYLE_GPT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Disk->SectorCount < 4) return STATUS_INVALID_PARAMETER;

    NumberOfEntries = Layout->Gpt.MaxPartitionCount;

    if (NumberOfEntries < Layout->PartitionCount)
    {
        NumberOfEntries = Layout->PartitionCount;
    }

    if (NumberOfEntries == 0)
    {
        NumberOfEntries = EFI_PARTITION_ENTRY_ARRAY_MINIMUM /
                          EFI_PARTITION_ENTRY_SIZE;
    }

    Sectors = FstubEntryArraySectors(NumberOfEntries,
                                     EFI_PARTITION_ENTRY_SIZE,
                                     Disk->BytesPerSector);

    /* The array must leave room for both headers and the protective record */
    if ((ULONGLONG)Sectors * 2 + 3 > Disk->SectorCount)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ArrayBytes = Sectors * Disk->BytesPerSector;

    Array = ExAllocatePoolZero(NonPagedPool, ArrayBytes, TAG_FSTUB);
    if (Array == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Entries = (PEFI_PARTITION_ENTRY)Array;

    for (i = 0; i < Layout->PartitionCount; i++)
    {
        /* An entry remembers which slot it came out of, so it goes back there */
        ULONG Slot = (Layout->PartitionEntry[i].PartitionNumber != 0)
                         ? Layout->PartitionEntry[i].PartitionNumber - 1
                         : i;

        if (Slot >= NumberOfEntries) continue;

        FstubWriteEntryEFI(&Entries[Slot],
                           &Layout->PartitionEntry[i],
                           Disk->BytesPerSector);
    }

    ArrayCRC32 = RtlComputeCrc32(0,
                                 Array,
                                 NumberOfEntries * EFI_PARTITION_ENTRY_SIZE);

    DiskId = Layout->Gpt.DiskId;
    BackupArrayLba = Disk->SectorCount - 1 - Sectors;

    FirstUsable = EFI_PARTITION_HEADER_LBA + 1 + Sectors;
    LastUsable = BackupArrayLba - 1;

    if (Layout->Gpt.StartingUsableOffset.QuadPart != 0)
    {
        ULONGLONG Wanted = (ULONGLONG)Layout->Gpt.StartingUsableOffset.QuadPart /
                           Disk->BytesPerSector;

        if (Wanted > FirstUsable) FirstUsable = Wanted;
    }

    if (Layout->Gpt.UsableLength.QuadPart != 0)
    {
        ULONGLONG Wanted = FirstUsable +
                           (ULONGLONG)Layout->Gpt.UsableLength.QuadPart /
                               Disk->BytesPerSector;

        if (Wanted != 0 && Wanted - 1 < LastUsable) LastUsable = Wanted - 1;
    }

    Status = FstubWriteSector(Disk,
                              Sectors,
                              EFI_PARTITION_HEADER_LBA + 1,
                              Array);
    if (NT_SUCCESS(Status))
    {
        Status = FstubWriteSector(Disk, Sectors, BackupArrayLba, Array);
    }

    ExFreePoolWithTag(Array, TAG_FSTUB);

    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubWriteHeaderEFI(Disk,
                                 Sectors,
                                 &DiskId,
                                 NumberOfEntries,
                                 FirstUsable,
                                 LastUsable,
                                 ArrayCRC32,
                                 TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubWriteHeaderEFI(Disk,
                                 Sectors,
                                 &DiskId,
                                 NumberOfEntries,
                                 FirstUsable,
                                 LastUsable,
                                 ArrayCRC32,
                                 FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = FstubReadSector(Disk, 1, 0, Disk->Sector);

    if (NT_SUCCESS(Status) &&
        *(PUSHORT)(Disk->Sector + FSTUB_BOOT_SIGNATURE_OFFSET) ==
            FSTUB_BOOT_SIGNATURE)
    {
        return STATUS_SUCCESS;
    }

    return FstubWriteBootSectorEFI(Disk);
}

NTSTATUS
NTAPI
FstubVerifyPartitionTableEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ BOOLEAN FixErrors)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    EFI_PARTITION_HEADER Primary;
    EFI_PARTITION_HEADER Backup;
    NTSTATUS Status;
    BOOLEAN PrimaryGood;
    BOOLEAN BackupGood;

    PAGED_CODE();

    PrimaryGood = NT_SUCCESS(FstubReadHeaderEFI(Disk, FALSE, &Primary));
    BackupGood = NT_SUCCESS(FstubReadHeaderEFI(Disk, TRUE, &Backup));

    if (PrimaryGood && BackupGood)
    {
        if (Primary.PartitionEntryCRC32 == Backup.PartitionEntryCRC32 &&
            Primary.NumberOfEntries == Backup.NumberOfEntries &&
            IsEqualGUID(&Primary.DiskGUID, &Backup.DiskGUID))
        {
            return STATUS_SUCCESS;
        }

        if (!FixErrors) return STATUS_DISK_CORRUPT_ERROR;

        BackupGood = FALSE;
    }

    if (!PrimaryGood && !BackupGood) return STATUS_DISK_CORRUPT_ERROR;

    if (!FixErrors) return STATUS_DISK_CORRUPT_ERROR;

    Status = FstubReadPartitionTableEFI(Disk, !PrimaryGood, &Layout);
    if (!NT_SUCCESS(Status)) return STATUS_DISK_CORRUPT_ERROR;

    Status = FstubWritePartitionTableEFI(Disk, Layout);

    ExFreePoolWithTag(Layout, TAG_FSTUB);

    return NT_SUCCESS(Status) ? STATUS_SUCCESS : STATUS_DISK_CORRUPT_ERROR;
}

NTSTATUS
NTAPI
FstubCreateDiskEFI(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PCREATE_DISK_GPT DiskInfo)
{
    DRIVE_LAYOUT_INFORMATION_EX Layout;
    PFSTUB_DISK Disk;
    NTSTATUS Status;

    PAGED_CODE();

    Status = FstubAllocateDiskInformation(DeviceObject, &Disk, NULL);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Layout, sizeof(Layout));

    Layout.PartitionStyle = PARTITION_STYLE_GPT;
    Layout.PartitionCount = 0;
    Layout.Gpt.DiskId = DiskInfo->DiskId;
    Layout.Gpt.MaxPartitionCount = DiskInfo->MaxPartitionCount;

    Status = FstubWritePartitionTableEFI(Disk, &Layout);

    if (NT_SUCCESS(Status)) Status = FstubWriteBootSectorEFI(Disk);

    FstubFreeDiskInformation(Disk);

    return Status;
}

NTSTATUS
NTAPI
FstubSetPartitionInformationEFI(
    _In_ PFSTUB_DISK Disk,
    _In_ ULONG PartitionNumber,
    _In_ SET_PARTITION_INFORMATION_GPT *PartitionInfo)
{
    PDRIVE_LAYOUT_INFORMATION_EX Layout;
    PPARTITION_INFORMATION_EX Entry = NULL;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    if (PartitionNumber == 0) return STATUS_INVALID_PARAMETER;

    Status = FstubReadPartitionTableEFI(Disk, FALSE, &Layout);
    if (!NT_SUCCESS(Status)) return Status;

    for (i = 0; i < Layout->PartitionCount; i++)
    {
        if (Layout->PartitionEntry[i].PartitionNumber == PartitionNumber)
        {
            Entry = &Layout->PartitionEntry[i];
            break;
        }
    }

    if (Entry == NULL)
    {
        ExFreePoolWithTag(Layout, TAG_FSTUB);
        return STATUS_INVALID_PARAMETER;
    }

    Entry->Gpt.PartitionType = PartitionInfo->PartitionType;
    Entry->Gpt.PartitionId = PartitionInfo->PartitionId;
    Entry->Gpt.Attributes = PartitionInfo->Attributes;
    RtlCopyMemory(Entry->Gpt.Name,
                  PartitionInfo->Name,
                  sizeof(Entry->Gpt.Name));
    Entry->RewritePartition = TRUE;

    Status = FstubWritePartitionTableEFI(Disk, Layout);

    ExFreePoolWithTag(Layout, TAG_FSTUB);

    return Status;
}

/* EOF */
