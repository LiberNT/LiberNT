/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS volume formatter: system files and metadata
 */

#include "formatint.h"

#define VOLUME_INFORMATION_SIZE 12
#define NTFS_MAJOR_VERSION 3
#define NTFS_MINOR_VERSION 1

/* On-disk sizes; see the note in record.cpp about structure padding. */
#define INDEX_ENTRY_HEADER_SIZE 0x10

/* NTFSRecordHeader + VCN, i.e. what precedes an INDX node header. */
#define INDEX_BUFFER_HEADER_SIZE 0x18

/* $MFT records 0-23 are the metadata region; see mkntfs for the same rule. */
#define NTFS_LAST_SEQUENCED_RECORD 23

#define ROOT_REFERENCE NTFS_ROOT_FILE_REFERENCE
#define EXTEND_REFERENCE NTFS_MK_FILE_REFERENCE(_Extend, _Extend)

#define SYSTEM_FILE_PERMISSIONS (FILE_PERM_HIDDEN | FILE_PERM_SYSTEM)
#define SYSTEM_FILE_NAME_FLAGS  (FN_HIDDEN | FN_SYSTEM)

typedef struct FormatExtent
{
    ULONGLONG Lcn;
    ULONGLONG Count;
} FormatExtent;

static USHORT
FormatSequenceFor(_In_ ULONG RecordNumber)
{
    if (RecordNumber == 0 || RecordNumber > NTFS_LAST_SEQUENCED_RECORD)
        return 1;

    return (USHORT)RecordNumber;
}

static ULONG
FormatSecurityIdFor(_In_ ULONG RecordNumber)
{
    if (RecordNumber == _Volume ||
        RecordNumber == _Secure ||
        RecordNumber >= _Extend)
    {
        return NTFS_FORMAT_SECURITY_ID_WRITE;
    }

    return NTFS_FORMAT_SECURITY_ID_READ;
}

typedef struct FormatSecurityEntry
{
    ULONG Hash;
    ULONG SecurityId;
    ULONGLONG Offset;
    ULONG Length;
    UCHAR Descriptor[128];
} FormatSecurityEntry;

static ULONGLONG
FormatBuildSecurityEntries(_Out_ FormatSecurityEntry* Entries)
{
    ULONGLONG Offset = 0;
    ULONGLONG End = 0;
    ULONG Index;

    for (Index = 0; Index < NTFS_FORMAT_SECURITY_ENTRIES; Index++)
    {
        ULONG DescriptorLength;

        DescriptorLength = FormatBuildSystemSecurityDescriptor(
            Entries[Index].Descriptor,
            sizeof(Entries[Index].Descriptor),
            Index != 0);

        Entries[Index].SecurityId = NTFS_FORMAT_SECURITY_ID_READ + Index;
        Entries[Index].Hash = FormatSecurityDescriptorHash(
            Entries[Index].Descriptor,
            DescriptorLength);
        Entries[Index].Offset = Offset;
        Entries[Index].Length = NTFS_FORMAT_SDS_HEADER_SIZE + DescriptorLength;

        End = Offset + Entries[Index].Length;
        Offset = ALIGN_UP_BY(End, 16);
    }

    return End;
}

ULONGLONG
FormatSecureStreamSize(void)
{
    FormatSecurityEntry Entries[NTFS_FORMAT_SECURITY_ENTRIES];

    return NTFS_FORMAT_SDS_MIRROR_OFFSET + FormatBuildSecurityEntries(Entries);
}

static void
FormatWriteSdsHeader(_Out_ PUCHAR Buffer,
                     _In_ const FormatSecurityEntry* Entry)
{
    RtlCopyMemory(Buffer, &Entry->Hash, sizeof(ULONG));
    RtlCopyMemory(Buffer + 4, &Entry->SecurityId, sizeof(ULONG));
    RtlCopyMemory(Buffer + 8, &Entry->Offset, sizeof(ULONGLONG));
    RtlCopyMemory(Buffer + 16, &Entry->Length, sizeof(ULONG));
}

/* Writes the record currently in Ctx->RecordBuffer to its slot in $MFT, and
 * additionally to $MFTMirr for the four records the mirror covers. */
static NTSTATUS
FormatFlushRecord(_In_ PFormatContext Ctx,
                  _In_ ULONG RecordNumber)
{
    ULONGLONG Offset;
    NTSTATUS Status;

    Status = FormatEndRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Offset = Ctx->MftLcn * Ctx->ClusterSize +
             (ULONGLONG)RecordNumber * Ctx->MftRecordSize;

    Status = FormatWriteAt(Ctx, Offset, Ctx->MftRecordSize, Ctx->RecordBuffer);
    if (!NT_SUCCESS(Status))
        return Status;

    if (RecordNumber < 4)
    {
        Offset = Ctx->MftMirrLcn * Ctx->ClusterSize +
                 (ULONGLONG)RecordNumber * Ctx->MftRecordSize;

        Status = FormatWriteAt(Ctx,
                               Offset,
                               Ctx->MftRecordSize,
                               Ctx->RecordBuffer);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

/*
 * Starts a system file record: $STANDARD_INFORMATION followed by the
 * $FILE_NAME that links it into the root directory.
 */
static NTSTATUS
FormatBeginSystemFile(_In_ PFormatContext Ctx,
                      _In_ ULONG RecordNumber,
                      _In_ PCWSTR Name,
                      _In_ USHORT RecordFlags,
                      _In_ ULONG NameFlags,
                      _In_ ULONGLONG AllocatedSize,
                      _In_ ULONGLONG DataSize)
{
    StandardInformationEx Information;

    FormatBeginRecord(Ctx,
                      RecordNumber,
                      FormatSequenceFor(RecordNumber),
                      RecordFlags);

    FormatFillStandardInformation(Ctx,
                                  &Information,
                                  SYSTEM_FILE_PERMISSIONS |
                                      (NameFlags & FN_INDEX_VIEW));
    Information.SecurityId = FormatSecurityIdFor(RecordNumber);
    if (!FormatAddResident(Ctx,
                           TypeStandardInformation,
                           NULL,
                           &Information,
                           sizeof(Information),
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return FormatAddFileName(Ctx,
                             ROOT_REFERENCE,
                             Name,
                             SYSTEM_FILE_NAME_FLAGS | NameFlags,
                             NAME_TYPE_WIN32_AND_DOS,
                             AllocatedSize,
                             DataSize);
}

/* A plain metadata file whose single unnamed $DATA covers one extent. */
static NTSTATUS
FormatWriteSimpleDataFile(_In_ PFormatContext Ctx,
                          _In_ ULONG RecordNumber,
                          _In_ PCWSTR Name,
                          _In_ ULONGLONG Lcn,
                          _In_ ULONGLONG ClusterCount,
                          _In_ ULONGLONG DataSize)
{
    NTSTATUS Status;

    Status = FormatBeginSystemFile(Ctx,
                                   RecordNumber,
                                   Name,
                                   FR_IN_USE,
                                   0,
                                   ClusterCount * Ctx->ClusterSize,
                                   DataSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!FormatAddNonResident(Ctx,
                              TypeData,
                              NULL,
                              Lcn,
                              ClusterCount,
                              DataSize,
                              DataSize,
                              0,
                              FALSE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return FormatFlushRecord(Ctx, RecordNumber);
}

static NTSTATUS
FormatWriteMftRecord(_In_ PFormatContext Ctx)
{
    NTSTATUS Status;

    Status = FormatBeginSystemFile(Ctx,
                                   _MFT,
                                   L"$MFT",
                                   FR_IN_USE,
                                   0,
                                   Ctx->MftClusters * Ctx->ClusterSize,
                                   Ctx->MftDataSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!FormatAddNonResident(Ctx,
                              TypeData,
                              NULL,
                              Ctx->MftLcn,
                              Ctx->MftClusters,
                              Ctx->MftDataSize,
                              Ctx->MftDataSize,
                              0,
                              FALSE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Which MFT records are in use. */
    if (!FormatAddNonResident(Ctx,
                              TypeBitmap,
                              NULL,
                              Ctx->MftBitmapLcn,
                              Ctx->MftBitmapClusters,
                              Ctx->MftBitmapDataSize,
                              Ctx->MftBitmapDataSize,
                              0,
                              FALSE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return FormatFlushRecord(Ctx, _MFT);
}

static NTSTATUS
FormatWriteVolumeRecord(_In_ PFormatContext Ctx)
{
    UCHAR Information[VOLUME_INFORMATION_SIZE];
    PVolumeInformationEx VolumeInformation = (PVolumeInformationEx)Information;
    ULONG LabelLength = 0;
    NTSTATUS Status;

    Status = FormatBeginSystemFile(Ctx, _Volume, L"$Volume", FR_IN_USE, 0, 0, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Ctx->Params->VolumeLabel)
    {
        while (Ctx->Params->VolumeLabel[LabelLength] != L'\0' &&
               LabelLength < NTFS_FORMAT_MAX_LABEL_CHARS)
        {
            LabelLength++;
        }
    }

    if (LabelLength != 0)
    {
        if (!FormatAddResident(Ctx,
                               TypeVolumeName,
                               NULL,
                               Ctx->Params->VolumeLabel,
                               LabelLength * sizeof(WCHAR),
                               0))
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    RtlZeroMemory(Information, sizeof(Information));
    VolumeInformation->MajorVersion = NTFS_MAJOR_VERSION;
    VolumeInformation->MinorVersion = NTFS_MINOR_VERSION;
    VolumeInformation->Flags = 0;

    if (!FormatAddResident(Ctx,
                           TypeVolumeInformation,
                           NULL,
                           Information,
                           VOLUME_INFORMATION_SIZE,
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!FormatAddResident(Ctx, TypeData, NULL, NULL, 0, 0))
        return STATUS_INSUFFICIENT_RESOURCES;

    return FormatFlushRecord(Ctx, _Volume);
}

/*
 * The system files live in the root directory, so the root index has to list
 * them. Order matters: NTFS index entries are sorted by the collation rule,
 * which for $I30 is the upcased file name.
 */
typedef struct FormatRootEntry
{
    ULONG RecordNumber;
    PCWSTR Name;
    ULONG NameFlags;
} FormatRootEntry;

static const FormatRootEntry FormatRootEntries[] =
{
    { _AttrDef,  L"$AttrDef",  0 },
    { _BadClus,  L"$BadClus",  0 },
    { _Bitmap,   L"$Bitmap",   0 },
    { _Boot,     L"$Boot",     0 },
    { _Extend,   L"$Extend",   FN_DIRECTORY },
    { _LogFile,  L"$LogFile",  0 },
    { _MFT,      L"$MFT",      0 },
    { _MFTMirr,  L"$MFTMirr",  0 },
    { _Secure,   L"$Secure",   FN_INDEX_VIEW },
    { _UpCase,   L"$UpCase",   0 },
    { _Volume,   L"$Volume",   0 },
    { _Root,     L".",         FN_DIRECTORY },
};

/*
 * Index entries cache the file's sizes alongside its name, so report the same
 * values the file record carries.
 */
static void
FormatSystemFileSizes(_In_ PFormatContext Ctx,
                      _In_ ULONG RecordNumber,
                      _Out_ PULONGLONG AllocatedSize,
                      _Out_ PULONGLONG DataSize)
{
    switch (RecordNumber)
    {
    case _MFT:
        *AllocatedSize = Ctx->MftClusters * Ctx->ClusterSize;
        *DataSize = Ctx->MftDataSize;
        return;
    case _MFTMirr:
        *AllocatedSize = Ctx->MftMirrClusters * Ctx->ClusterSize;
        *DataSize = 4ULL * Ctx->MftRecordSize;
        return;
    case _LogFile:
        *AllocatedSize = Ctx->LogFileClusters * Ctx->ClusterSize;
        *DataSize = Ctx->LogFileSize;
        return;
    case _AttrDef:
        *AllocatedSize = Ctx->AttrDefClusters * Ctx->ClusterSize;
        *DataSize = NTFS_ATTRDEF_SIZE;
        return;
    case _Bitmap:
        *AllocatedSize = Ctx->BitmapClusters * Ctx->ClusterSize;
        *DataSize = Ctx->BitmapDataSize;
        return;
    case _Boot:
        *AllocatedSize = Ctx->BootClusters * Ctx->ClusterSize;
        *DataSize = NTFS_BOOT_AREA_SIZE;
        return;
    case _UpCase:
        *AllocatedSize = Ctx->UpCaseClusters * Ctx->ClusterSize;
        *DataSize = NTFS_UPCASE_SIZE;
        return;
    default:
        /* $Volume, $BadClus, $Secure and $Extend have no unnamed data. */
        *AllocatedSize = 0;
        *DataSize = 0;
        return;
    }
}

/*
 * Writes the single index block backing the root directory. Entries are
 * emitted in the order of the table above, which is already collation order
 * for these names.
 */
static NTSTATUS
FormatWriteRootIndexBlock(_In_ PFormatContext Ctx)
{
    PUCHAR Block = Ctx->TransferBuffer;
    ULONG BlockSize = Ctx->IndexRecordSize;
    ULONG AllocatedBytes = (ULONG)(Ctx->RootIndexClusters * Ctx->ClusterSize);
    PIndexBuffer Index = (PIndexBuffer)Block;
    USHORT UsaCount = (USHORT)(BlockSize / Ctx->BytesPerSector + 1);
    ULONG EntriesOffset;
    ULONG Offset;
    ULONG Number;
    PIndexEntry End;

    if (AllocatedBytes > Ctx->TransferSize)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Block, AllocatedBytes);

    Index->RecordHeader.TypeID[0] = 'I';
    Index->RecordHeader.TypeID[1] = 'N';
    Index->RecordHeader.TypeID[2] = 'D';
    Index->RecordHeader.TypeID[3] = 'X';
    Index->RecordHeader.UpdateSequenceOffset = 0x28;
    Index->RecordHeader.SizeOfUpdateSequence = UsaCount;
    Index->RecordHeader.LogFileSequenceNumber = 0;
    Index->VCN = 0;

    /* Entries start after the update sequence array, 8 byte aligned. */
    EntriesOffset = ALIGN_UP_BY(0x28 + UsaCount * sizeof(USHORT), 8);

    /* Offsets in the node header are relative to the node header itself. */
    Index->IndexHeader.IndexOffset = EntriesOffset - INDEX_BUFFER_HEADER_SIZE;
    Index->IndexHeader.AllocatedSize = BlockSize - INDEX_BUFFER_HEADER_SIZE;
    Index->IndexHeader.Flags = 0;

    Offset = EntriesOffset;
    for (Number = 0; Number < RTL_NUMBER_OF(FormatRootEntries); Number++)
    {
        const FormatRootEntry* Entry = &FormatRootEntries[Number];
        ULONGLONG Reference;
        ULONGLONG AllocatedSize, DataSize;
        ULONG EntryLength;

        Reference = NTFS_MK_FILE_REFERENCE(Entry->RecordNumber,
                                           FormatSequenceFor(Entry->RecordNumber));

        FormatSystemFileSizes(Ctx,
                              Entry->RecordNumber,
                              &AllocatedSize,
                              &DataSize);

        EntryLength = FormatBuildFileNameIndexEntry(Ctx,
                                                    Block + Offset,
                                                    Reference,
                                                    ROOT_REFERENCE,
                                                    Entry->Name,
                                                    SYSTEM_FILE_NAME_FLAGS |
                                                        Entry->NameFlags,
                                                    NAME_TYPE_WIN32_AND_DOS,
                                                    AllocatedSize,
                                                    DataSize);
        if (EntryLength == 0)
            return STATUS_INVALID_PARAMETER;

        Offset += EntryLength;

        /* Keep room for the terminating end entry. */
        if (Offset + INDEX_ENTRY_HEADER_SIZE > BlockSize)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    End = (PIndexEntry)(Block + Offset);
    End->EntryLength = INDEX_ENTRY_HEADER_SIZE;
    End->StreamLength = 0;
    End->Flags = INDEX_ENTRY_END;
    Offset += INDEX_ENTRY_HEADER_SIZE;

    Index->IndexHeader.TotalIndexSize = Offset - INDEX_BUFFER_HEADER_SIZE;

    FormatCommitFixupBuffer(Ctx, Block, BlockSize);

    return FormatWriteCluster(Ctx, Ctx->RootIndexLcn, AllocatedBytes, Block);
}

static NTSTATUS
FormatWriteRootRecord(_In_ PFormatContext Ctx)
{
    UCHAR SecurityDescriptor[192];
    StandardInformationEx Information;
    ULONG SecurityLength;
    UCHAR IndexBitmap[8];
    NTSTATUS Status;

    FormatBeginRecord(Ctx,
                      _Root,
                      FormatSequenceFor(_Root),
                      FR_IN_USE | FR_IS_DIRECTORY);

    FormatFillStandardInformation(Ctx, &Information, SYSTEM_FILE_PERMISSIONS);
    if (!FormatAddResident(Ctx,
                           TypeStandardInformation,
                           NULL,
                           &Information,
                           sizeof(Information),
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* The root directory is its own parent. */
    Status = FormatAddFileName(Ctx,
                               ROOT_REFERENCE,
                               L".",
                               SYSTEM_FILE_NAME_FLAGS | FN_DIRECTORY,
                               NAME_TYPE_WIN32_AND_DOS,
                               0,
                               0);
    if (!NT_SUCCESS(Status))
        return Status;

    SecurityLength = FormatBuildDefaultSecurityDescriptor(SecurityDescriptor,
                                                          sizeof(SecurityDescriptor));
    if (SecurityLength != 0)
    {
        if (!FormatAddResident(Ctx,
                               TypeSecurityDescriptor,
                               NULL,
                               SecurityDescriptor,
                               SecurityLength,
                               0))
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    /*
     * The system-file entries do not fit in the record, so the root uses an
     * $INDEX_ROOT node plus an $INDEX_ALLOCATION block and its bitmap.
     */
    Status = FormatAddIndexRootNode(Ctx,
                                    TypeFileName,
                                    ATTRDEF_COLLATION_FILENAME,
                                    L"$I30");
    if (!NT_SUCCESS(Status))
        return Status;

    if (!FormatAddNonResident(Ctx,
                              TypeIndexAllocation,
                              L"$I30",
                              Ctx->RootIndexLcn,
                              Ctx->RootIndexClusters,
                              Ctx->RootIndexClusters * Ctx->ClusterSize,
                              Ctx->RootIndexClusters * Ctx->ClusterSize,
                              0,
                              FALSE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* One bit per index block; the single block we wrote is in use. */
    RtlZeroMemory(IndexBitmap, sizeof(IndexBitmap));
    IndexBitmap[0] = 0x01;

    if (!FormatAddResident(Ctx,
                           TypeBitmap,
                           L"$I30",
                           IndexBitmap,
                           sizeof(IndexBitmap),
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = FormatFlushRecord(Ctx, _Root);
    if (!NT_SUCCESS(Status))
        return Status;

    return FormatWriteRootIndexBlock(Ctx);
}

static NTSTATUS
FormatWriteBadClusRecord(_In_ PFormatContext Ctx)
{
    NTSTATUS Status;

    Status = FormatBeginSystemFile(Ctx, _BadClus, L"$BadClus", FR_IN_USE, 0, 0, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    /* The unnamed stream is empty; the bad cluster list lives in $Bad. */
    if (!FormatAddResident(Ctx, TypeData, NULL, NULL, 0, 0))
        return STATUS_INSUFFICIENT_RESOURCES;

    /*
     * $Bad is a sparse stream spanning the whole volume: every cluster it
     * reports as a hole is a good cluster. A fresh volume has no bad ones.
     */
    if (!FormatAddNonResident(Ctx,
                              TypeData,
                              L"$Bad",
                              0,
                              Ctx->TotalClusters,
                              Ctx->TotalClusters * Ctx->ClusterSize,
                              0,
                              0,
                              TRUE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return FormatFlushRecord(Ctx, _BadClus);
}

static void
FormatWriteViewEntryHeader(_Out_ PUCHAR Entry,
                           _In_ USHORT DataOffset,
                           _In_ USHORT DataLength,
                           _In_ USHORT EntryLength,
                           _In_ USHORT KeyLength)
{
    RtlCopyMemory(Entry, &DataOffset, sizeof(USHORT));
    RtlCopyMemory(Entry + 2, &DataLength, sizeof(USHORT));
    RtlCopyMemory(Entry + 8, &EntryLength, sizeof(USHORT));
    RtlCopyMemory(Entry + 10, &KeyLength, sizeof(USHORT));
}

static NTSTATUS
FormatWriteSecureRecord(_In_ PFormatContext Ctx)
{
    const USHORT SiiEntryLength = 40;
    const USHORT SdhEntryLength = 48;
    const ULONG SdhPadding = 0x00490049;
    FormatSecurityEntry Entries[NTFS_FORMAT_SECURITY_ENTRIES];
    UCHAR Sii[NTFS_FORMAT_SECURITY_ENTRIES * 40];
    UCHAR Sdh[NTFS_FORMAT_SECURITY_ENTRIES * 48];
    ULONG Order[NTFS_FORMAT_SECURITY_ENTRIES];
    ULONG Index;
    NTSTATUS Status;

    FormatBuildSecurityEntries(Entries);

    for (Index = 0; Index < NTFS_FORMAT_SECURITY_ENTRIES; Index++)
    {
        ULONG Position = Index;

        while (Position != 0 &&
               (Entries[Order[Position - 1]].Hash > Entries[Index].Hash ||
                (Entries[Order[Position - 1]].Hash == Entries[Index].Hash &&
                 Entries[Order[Position - 1]].SecurityId > Entries[Index].SecurityId)))
        {
            Order[Position] = Order[Position - 1];
            Position--;
        }
        Order[Position] = Index;
    }

    RtlZeroMemory(Sii, sizeof(Sii));
    RtlZeroMemory(Sdh, sizeof(Sdh));
    for (Index = 0; Index < NTFS_FORMAT_SECURITY_ENTRIES; Index++)
    {
        PUCHAR SiiEntry = Sii + Index * SiiEntryLength;
        PUCHAR SdhEntry = Sdh + Index * SdhEntryLength;
        const FormatSecurityEntry* SdhSource = &Entries[Order[Index]];

        FormatWriteViewEntryHeader(SiiEntry,
                                   INDEX_ENTRY_HEADER_SIZE + sizeof(ULONG),
                                   NTFS_FORMAT_SDS_HEADER_SIZE,
                                   SiiEntryLength,
                                   sizeof(ULONG));
        RtlCopyMemory(SiiEntry + INDEX_ENTRY_HEADER_SIZE,
                      &Entries[Index].SecurityId,
                      sizeof(ULONG));
        FormatWriteSdsHeader(SiiEntry + INDEX_ENTRY_HEADER_SIZE + sizeof(ULONG),
                             &Entries[Index]);

        FormatWriteViewEntryHeader(SdhEntry,
                                   INDEX_ENTRY_HEADER_SIZE + 2 * sizeof(ULONG),
                                   NTFS_FORMAT_SDS_HEADER_SIZE,
                                   SdhEntryLength,
                                   2 * sizeof(ULONG));
        RtlCopyMemory(SdhEntry + INDEX_ENTRY_HEADER_SIZE,
                      &SdhSource->Hash,
                      sizeof(ULONG));
        RtlCopyMemory(SdhEntry + INDEX_ENTRY_HEADER_SIZE + sizeof(ULONG),
                      &SdhSource->SecurityId,
                      sizeof(ULONG));
        FormatWriteSdsHeader(SdhEntry + INDEX_ENTRY_HEADER_SIZE + 2 * sizeof(ULONG),
                             SdhSource);
        RtlCopyMemory(SdhEntry + SdhEntryLength - sizeof(ULONG),
                      &SdhPadding,
                      sizeof(ULONG));
    }

    Status = FormatBeginSystemFile(Ctx,
                                   _Secure,
                                   L"$Secure",
                                   FR_IN_USE | FR_SPECIAL_INDEX,
                                   FN_INDEX_VIEW,
                                   0,
                                   0);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!FormatAddNonResident(Ctx,
                              TypeData,
                              L"$SDS",
                              Ctx->SecureSdsLcn,
                              Ctx->SecureSdsClusters,
                              Ctx->SecureSdsDataSize,
                              Ctx->SecureSdsDataSize,
                              0,
                              FALSE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = FormatAddIndexRootEntries(Ctx,
                                       L"$SDH",
                                       0,
                                       ATTRDEF_COLLATION_SEC_HASH,
                                       Sdh,
                                       sizeof(Sdh));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatAddIndexRootEntries(Ctx,
                                       L"$SII",
                                       0,
                                       ATTRDEF_COLLATION_ULONG,
                                       Sii,
                                       sizeof(Sii));
    if (!NT_SUCCESS(Status))
        return Status;

    return FormatFlushRecord(Ctx, _Secure);
}

static NTSTATUS
FormatWriteSecureStream(_In_ PFormatContext Ctx)
{
    FormatSecurityEntry Entries[NTFS_FORMAT_SECURITY_ENTRIES];
    ULONG Length = (ULONG)(Ctx->SecureSdsClusters * Ctx->ClusterSize);
    PUCHAR Stream;
    ULONG Index;
    NTSTATUS Status;

    FormatBuildSecurityEntries(Entries);

    Stream = (PUCHAR)Ctx->Params->Allocate(Ctx->Params->IoContext, Length);
    if (!Stream)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Stream, Length);
    for (Index = 0; Index < NTFS_FORMAT_SECURITY_ENTRIES; Index++)
    {
        ULONGLONG Offset = Entries[Index].Offset;

        FormatWriteSdsHeader(Stream + Offset, &Entries[Index]);
        RtlCopyMemory(Stream + Offset + NTFS_FORMAT_SDS_HEADER_SIZE,
                      Entries[Index].Descriptor,
                      Entries[Index].Length - NTFS_FORMAT_SDS_HEADER_SIZE);
        RtlCopyMemory(Stream + NTFS_FORMAT_SDS_MIRROR_OFFSET + Offset,
                      Stream + Offset,
                      Entries[Index].Length);
    }

    Status = FormatWriteCluster(Ctx, Ctx->SecureSdsLcn, Length, Stream);
    Ctx->Params->Free(Ctx->Params->IoContext, Stream);

    return Status;
}

typedef struct FormatExtendEntry
{
    ULONG RecordNumber;
    PCWSTR Name;
} FormatExtendEntry;

static const FormatExtendEntry FormatExtendEntries[] =
{
    { NTFS_FORMAT_OBJID_RECORD,   L"$ObjId" },
    { NTFS_FORMAT_QUOTA_RECORD,   L"$Quota" },
    { NTFS_FORMAT_REPARSE_RECORD, L"$Reparse" },
};

static NTSTATUS
FormatWriteExtendRecord(_In_ PFormatContext Ctx)
{
    StandardInformationEx Information;
    UCHAR Entries[NTFS_FORMAT_INDEX_ROOT_ENTRIES_MAX];
    ULONG EntriesLength = 0;
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < RTL_NUMBER_OF(FormatExtendEntries); Index++)
    {
        ULONG RecordNumber = FormatExtendEntries[Index].RecordNumber;
        ULONG EntryLength;

        EntryLength = FormatBuildFileNameIndexEntry(
            Ctx,
            Entries + EntriesLength,
            NTFS_MK_FILE_REFERENCE(RecordNumber, FormatSequenceFor(RecordNumber)),
            EXTEND_REFERENCE,
            FormatExtendEntries[Index].Name,
            SYSTEM_FILE_NAME_FLAGS | FN_INDEX_VIEW,
            NAME_TYPE_POSIX,
            0,
            0);
        if (EntryLength == 0)
            return STATUS_INVALID_PARAMETER;

        EntriesLength += EntryLength;
    }

    FormatBeginRecord(Ctx,
                      _Extend,
                      FormatSequenceFor(_Extend),
                      FR_IN_USE | FR_IS_DIRECTORY);

    FormatFillStandardInformation(Ctx, &Information, SYSTEM_FILE_PERMISSIONS);
    Information.SecurityId = FormatSecurityIdFor(_Extend);
    if (!FormatAddResident(Ctx,
                           TypeStandardInformation,
                           NULL,
                           &Information,
                           sizeof(Information),
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = FormatAddFileName(Ctx,
                               ROOT_REFERENCE,
                               L"$Extend",
                               SYSTEM_FILE_NAME_FLAGS | FN_DIRECTORY,
                               NAME_TYPE_WIN32_AND_DOS,
                               0,
                               0);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatAddIndexRootEntries(Ctx,
                                       L"$I30",
                                       TypeFileName,
                                       ATTRDEF_COLLATION_FILENAME,
                                       Entries,
                                       EntriesLength);
    if (!NT_SUCCESS(Status))
        return Status;

    return FormatFlushRecord(Ctx, _Extend);
}

static NTSTATUS
FormatBeginExtendFile(_In_ PFormatContext Ctx,
                      _In_ ULONG RecordNumber,
                      _In_ PCWSTR Name)
{
    StandardInformationEx Information;

    FormatBeginRecord(Ctx,
                      RecordNumber,
                      FormatSequenceFor(RecordNumber),
                      FR_IN_USE | FR_IS_EXTENSION | FR_SPECIAL_INDEX);

    FormatFillStandardInformation(Ctx,
                                  &Information,
                                  SYSTEM_FILE_PERMISSIONS | FN_INDEX_VIEW);
    Information.SecurityId = FormatSecurityIdFor(RecordNumber);
    if (!FormatAddResident(Ctx,
                           TypeStandardInformation,
                           NULL,
                           &Information,
                           sizeof(Information),
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return FormatAddFileName(Ctx,
                             EXTEND_REFERENCE,
                             Name,
                             SYSTEM_FILE_NAME_FLAGS | FN_INDEX_VIEW,
                             NAME_TYPE_POSIX,
                             0,
                             0);
}

static ULONG
FormatBuildQuotaEntry(_In_ PFormatContext Ctx,
                      _Out_ PUCHAR Entry,
                      _In_ ULONG OwnerId,
                      _In_opt_ const UCHAR* Sid,
                      _In_ ULONG SidLength)
{
    const ULONG QuotaVersion = 2;
    const ULONG QuotaFlags = 1;
    const ULONGLONG NoLimit = ~0ULL;
    USHORT DataLength = (USHORT)(48 + SidLength);
    USHORT EntryLength = (USHORT)ALIGN_UP_BY(INDEX_ENTRY_HEADER_SIZE + sizeof(ULONG) + DataLength, 8);
    PUCHAR Data = Entry + INDEX_ENTRY_HEADER_SIZE + sizeof(ULONG);

    RtlZeroMemory(Entry, EntryLength);
    FormatWriteViewEntryHeader(Entry,
                               INDEX_ENTRY_HEADER_SIZE + sizeof(ULONG),
                               DataLength,
                               EntryLength,
                               sizeof(ULONG));
    RtlCopyMemory(Entry + INDEX_ENTRY_HEADER_SIZE, &OwnerId, sizeof(ULONG));
    RtlCopyMemory(Data, &QuotaVersion, sizeof(ULONG));
    RtlCopyMemory(Data + 4, &QuotaFlags, sizeof(ULONG));
    RtlCopyMemory(Data + 16, &Ctx->CurrentTime, sizeof(ULONGLONG));
    RtlCopyMemory(Data + 24, &NoLimit, sizeof(ULONGLONG));
    RtlCopyMemory(Data + 32, &NoLimit, sizeof(ULONGLONG));
    if (SidLength != 0)
        RtlCopyMemory(Data + 48, Sid, SidLength);

    return EntryLength;
}

static NTSTATUS
FormatWriteQuotaRecord(_In_ PFormatContext Ctx)
{
    static const UCHAR AdministratorsSid[] =
        { 1, 2, 0, 0, 0, 0, 0, 5, 32, 0, 0, 0, 0x20, 2, 0, 0 };
    const ULONG AdminOwnerId = NTFS_FORMAT_QUOTA_ADMIN_OWNER_ID;
    const ULONG DefaultsOwnerId = 1;
    UCHAR Owners[INDEX_ENTRY_HEADER_SIZE + sizeof(AdministratorsSid) + sizeof(ULONG) + 4];
    UCHAR Quotas[2 * (INDEX_ENTRY_HEADER_SIZE + sizeof(ULONG) + 48 + sizeof(AdministratorsSid))];
    ULONG QuotasLength;
    NTSTATUS Status;

    RtlZeroMemory(Owners, sizeof(Owners));
    FormatWriteViewEntryHeader(Owners,
                               INDEX_ENTRY_HEADER_SIZE + sizeof(AdministratorsSid),
                               sizeof(ULONG),
                               sizeof(Owners),
                               sizeof(AdministratorsSid));
    RtlCopyMemory(Owners + INDEX_ENTRY_HEADER_SIZE,
                  AdministratorsSid,
                  sizeof(AdministratorsSid));
    RtlCopyMemory(Owners + INDEX_ENTRY_HEADER_SIZE + sizeof(AdministratorsSid),
                  &AdminOwnerId,
                  sizeof(ULONG));

    QuotasLength = FormatBuildQuotaEntry(Ctx, Quotas, DefaultsOwnerId, NULL, 0);
    QuotasLength += FormatBuildQuotaEntry(Ctx,
                                          Quotas + QuotasLength,
                                          AdminOwnerId,
                                          AdministratorsSid,
                                          sizeof(AdministratorsSid));

    Status = FormatBeginExtendFile(Ctx, NTFS_FORMAT_QUOTA_RECORD, L"$Quota");
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatAddIndexRootEntries(Ctx,
                                       L"$O",
                                       0,
                                       ATTRDEF_COLLATION_SID,
                                       Owners,
                                       sizeof(Owners));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatAddIndexRootEntries(Ctx,
                                       L"$Q",
                                       0,
                                       ATTRDEF_COLLATION_ULONG,
                                       Quotas,
                                       QuotasLength);
    if (!NT_SUCCESS(Status))
        return Status;

    return FormatFlushRecord(Ctx, NTFS_FORMAT_QUOTA_RECORD);
}

static NTSTATUS
FormatWriteEmptyViewFile(_In_ PFormatContext Ctx,
                         _In_ ULONG RecordNumber,
                         _In_ PCWSTR Name,
                         _In_ PCWSTR IndexName)
{
    NTSTATUS Status;

    Status = FormatBeginExtendFile(Ctx, RecordNumber, Name);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatAddIndexRootEntries(Ctx,
                                       IndexName,
                                       0,
                                       ATTRDEF_COLLATION_ULONG_MULTI,
                                       NULL,
                                       0);
    if (!NT_SUCCESS(Status))
        return Status;

    return FormatFlushRecord(Ctx, RecordNumber);
}

/*
 * Records past $Extend up to NTFS_LAST_RESERVED_FILE_RECORD are reserved by
 * NTFS. They are marked in use in the $MFT bitmap, so they must be readable
 * records; like the reserved records Windows creates they carry only
 * $STANDARD_INFORMATION and an empty $DATA and are linked into no directory.
 *
 * Reserving the whole range matters: readers hide records at or below
 * NTFS_LAST_RESERVED_FILE_RECORD from directory enumeration, so a user file
 * allocated there would be invisible.
 *
 * Everything above that gets a valid but free FILE header, which keeps the
 * MFT uniformly parseable.
 */
static NTSTATUS
FormatWriteRemainingRecords(_In_ PFormatContext Ctx)
{
    ULONG RecordNumber;
    NTSTATUS Status;

    for (RecordNumber = _Extend + 1;
         RecordNumber < Ctx->MftRecordCount;
         RecordNumber++)
    {
        BOOLEAN Reserved = (RecordNumber < NTFS_FORMAT_RESERVED_RECORDS);

        FormatBeginRecord(Ctx,
                          RecordNumber,
                          FormatSequenceFor(RecordNumber),
                          Reserved ? FR_IN_USE : 0);

        if (Reserved)
        {
            StandardInformationEx Information;

            ((PFileRecordHeader)Ctx->RecordBuffer)->HardLinkCount = 0;
            FormatFillStandardInformation(Ctx,
                                          &Information,
                                          SYSTEM_FILE_PERMISSIONS);
            Information.SecurityId = FormatSecurityIdFor(RecordNumber);
            if (!FormatAddResident(Ctx,
                                   TypeStandardInformation,
                                   NULL,
                                   &Information,
                                   sizeof(Information),
                                   0))
            {
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            if (!FormatAddResident(Ctx, TypeData, NULL, NULL, 0, 0))
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        Status = FormatFlushRecord(Ctx, RecordNumber);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
FormatWriteMftBitmap(_In_ PFormatContext Ctx)
{
    ULONG Length = (ULONG)(Ctx->MftBitmapClusters * Ctx->ClusterSize);
    ULONG Bit;

    if (Length > Ctx->TransferSize)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Ctx->TransferBuffer, Length);

    for (Bit = 0; Bit < NTFS_FORMAT_RESERVED_RECORDS; Bit++)
        Ctx->TransferBuffer[Bit / 8] |= (UCHAR)(1u << (Bit % 8));

    for (Bit = 0; Bit < RTL_NUMBER_OF(FormatExtendEntries); Bit++)
    {
        ULONG RecordNumber = FormatExtendEntries[Bit].RecordNumber;

        Ctx->TransferBuffer[RecordNumber / 8] |= (UCHAR)(1u << (RecordNumber % 8));
    }

    return FormatWriteCluster(Ctx, Ctx->MftBitmapLcn, Length, Ctx->TransferBuffer);
}

/*
 * Writes the volume bitmap in chunks so that a large volume never needs the
 * whole bitmap resident at once.
 */
static NTSTATUS
FormatWriteVolumeBitmap(_In_ PFormatContext Ctx)
{
    FormatExtent Extents[10];
    ULONG ExtentCount = 0;
    ULONGLONG TotalBytes = Ctx->BitmapClusters * Ctx->ClusterSize;
    ULONGLONG Written = 0;
    NTSTATUS Status;

    Extents[ExtentCount].Lcn = Ctx->BootLcn;
    Extents[ExtentCount++].Count = Ctx->BootClusters;
    Extents[ExtentCount].Lcn = Ctx->MftLcn;
    Extents[ExtentCount++].Count = Ctx->MftClusters;
    Extents[ExtentCount].Lcn = Ctx->MftBitmapLcn;
    Extents[ExtentCount++].Count = Ctx->MftBitmapClusters;
    Extents[ExtentCount].Lcn = Ctx->LogFileLcn;
    Extents[ExtentCount++].Count = Ctx->LogFileClusters;
    Extents[ExtentCount].Lcn = Ctx->BitmapLcn;
    Extents[ExtentCount++].Count = Ctx->BitmapClusters;
    Extents[ExtentCount].Lcn = Ctx->UpCaseLcn;
    Extents[ExtentCount++].Count = Ctx->UpCaseClusters;
    Extents[ExtentCount].Lcn = Ctx->AttrDefLcn;
    Extents[ExtentCount++].Count = Ctx->AttrDefClusters;
    Extents[ExtentCount].Lcn = Ctx->RootIndexLcn;
    Extents[ExtentCount++].Count = Ctx->RootIndexClusters;
    Extents[ExtentCount].Lcn = Ctx->MftMirrLcn;
    Extents[ExtentCount++].Count = Ctx->MftMirrClusters;
    Extents[ExtentCount].Lcn = Ctx->SecureSdsLcn;
    Extents[ExtentCount++].Count = Ctx->SecureSdsClusters;

    while (Written < TotalBytes)
    {
        ULONG Chunk = (TotalBytes - Written) > Ctx->TransferSize
                          ? Ctx->TransferSize
                          : (ULONG)(TotalBytes - Written);
        ULONGLONG FirstBit = Written * 8;
        ULONGLONG LastBit = FirstBit + (ULONGLONG)Chunk * 8;
        ULONG Index;

        RtlZeroMemory(Ctx->TransferBuffer, Chunk);

        for (Index = 0; Index < ExtentCount; Index++)
        {
            ULONGLONG Start = Extents[Index].Lcn;
            ULONGLONG End = Start + Extents[Index].Count;
            ULONGLONG Bit;

            if (End <= FirstBit || Start >= LastBit)
                continue;

            if (Start < FirstBit)
                Start = FirstBit;
            if (End > LastBit)
                End = LastBit;

            for (Bit = Start; Bit < End; Bit++)
            {
                ULONGLONG Local = Bit - FirstBit;
                Ctx->TransferBuffer[Local / 8] |= (UCHAR)(1u << (Local % 8));
            }
        }

        /* Clusters that do not exist must never look available. */
        if (LastBit > Ctx->TotalClusters)
        {
            ULONGLONG Bit = Ctx->TotalClusters > FirstBit ? Ctx->TotalClusters
                                                          : FirstBit;

            for (; Bit < LastBit; Bit++)
            {
                ULONGLONG Local = Bit - FirstBit;
                Ctx->TransferBuffer[Local / 8] |= (UCHAR)(1u << (Local % 8));
            }
        }

        Status = FormatWriteAt(Ctx,
                               Ctx->BitmapLcn * Ctx->ClusterSize + Written,
                               Chunk,
                               Ctx->TransferBuffer);
        if (!NT_SUCCESS(Status))
            return Status;

        Written += Chunk;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
FormatWriteUpCase(_In_ PFormatContext Ctx)
{
    PWCHAR Table;
    ULONGLONG Offset = Ctx->UpCaseLcn * Ctx->ClusterSize;
    ULONGLONG Written = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    Table = (PWCHAR)Ctx->Params->Allocate(Ctx->Params->IoContext,
                                          NTFS_UPCASE_SIZE);
    if (!Table)
        return STATUS_INSUFFICIENT_RESOURCES;

    FormatBuildUpCaseTable(Table);

    while (Written < NTFS_UPCASE_SIZE)
    {
        ULONG Chunk = (NTFS_UPCASE_SIZE - Written) > Ctx->TransferSize
                          ? Ctx->TransferSize
                          : (ULONG)(NTFS_UPCASE_SIZE - Written);

        Status = FormatWriteAt(Ctx,
                               Offset + Written,
                               Chunk,
                               (PUCHAR)Table + Written);
        if (!NT_SUCCESS(Status))
            break;

        Written += Chunk;
    }

    Ctx->Params->Free(Ctx->Params->IoContext, Table);

    if (!NT_SUCCESS(Status))
        return Status;

    /* Pad the rest of the final cluster. */
    if (Ctx->UpCaseClusters * Ctx->ClusterSize > NTFS_UPCASE_SIZE)
    {
        ULONG Tail = (ULONG)(Ctx->UpCaseClusters * Ctx->ClusterSize -
                             NTFS_UPCASE_SIZE);

        RtlZeroMemory(Ctx->TransferBuffer, Tail);
        Status = FormatWriteAt(Ctx,
                               Offset + NTFS_UPCASE_SIZE,
                               Tail,
                               Ctx->TransferBuffer);
    }

    return Status;
}

static NTSTATUS
FormatWriteAttrDef(_In_ PFormatContext Ctx)
{
    ULONG Length = (ULONG)(Ctx->AttrDefClusters * Ctx->ClusterSize);

    if (Length > Ctx->TransferSize)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Ctx->TransferBuffer, Length);
    FormatBuildAttrDefTable((PAttrDefEntry)Ctx->TransferBuffer);

    return FormatWriteCluster(Ctx, Ctx->AttrDefLcn, Length, Ctx->TransferBuffer);
}

NTSTATUS
FormatWriteMetadata(_In_ PFormatContext Ctx)
{
    NTSTATUS Status;

    /*
     * An all-0xFF $LogFile is the canonical empty journal; ntfslib's LFS
     * recognises it and skips restart-page recovery entirely.
     */
    Status = FormatFillClusters(Ctx, Ctx->LogFileLcn, Ctx->LogFileClusters, 0xFF);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteUpCase(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteAttrDef(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSecureStream(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteVolumeBitmap(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteMftBitmap(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteMftRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _MFTMirr,
                                       L"$MFTMirr",
                                       Ctx->MftMirrLcn,
                                       Ctx->MftMirrClusters,
                                       4ULL * Ctx->MftRecordSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _LogFile,
                                       L"$LogFile",
                                       Ctx->LogFileLcn,
                                       Ctx->LogFileClusters,
                                       Ctx->LogFileSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteVolumeRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _AttrDef,
                                       L"$AttrDef",
                                       Ctx->AttrDefLcn,
                                       Ctx->AttrDefClusters,
                                       NTFS_ATTRDEF_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteRootRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _Bitmap,
                                       L"$Bitmap",
                                       Ctx->BitmapLcn,
                                       Ctx->BitmapClusters,
                                       Ctx->BitmapDataSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _Boot,
                                       L"$Boot",
                                       Ctx->BootLcn,
                                       Ctx->BootClusters,
                                       NTFS_BOOT_AREA_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteBadClusRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSecureRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _UpCase,
                                       L"$UpCase",
                                       Ctx->UpCaseLcn,
                                       Ctx->UpCaseClusters,
                                       NTFS_UPCASE_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteExtendRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteRemainingRecords(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteQuotaRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteEmptyViewFile(Ctx, NTFS_FORMAT_OBJID_RECORD, L"$ObjId", L"$O");
    if (!NT_SUCCESS(Status))
        return Status;

    return FormatWriteEmptyViewFile(Ctx, NTFS_FORMAT_REPARSE_RECORD, L"$Reparse", L"$R");
}
