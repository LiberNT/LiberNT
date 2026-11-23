/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Synchronize duplicated $FILE_NAME stream sizes
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define NTFS_INDEX_HEADER_LARGE 1

extern const WCHAR NtfsI30Name[] = L"$I30";

static BOOLEAN
FileNameKeysMatch(_In_ PIndexEntry Entry,
                  _In_ PFileNameEx TargetName,
                  _In_ ULONGLONG FileReference)
{
    PFileNameEx IndexedName;
    ULONG NameBytes;

    if ((Entry->Flags & INDEX_ENTRY_END) ||
        Entry->Data.Directory.IndexedFile != FileReference)
    {
        return FALSE;
    }

    IndexedName =
        reinterpret_cast<PFileNameEx>(Entry->IndexStream);
    if (IndexedName->ParentFileReference !=
            TargetName->ParentFileReference ||
        IndexedName->NameLength != TargetName->NameLength ||
        IndexedName->NameType != TargetName->NameType)
    {
        return FALSE;
    }

    NameBytes =
        TargetName->NameLength * sizeof(WCHAR);
    return NameBytes == 0 ||
           RtlCompareMemory(IndexedName->Name,
                            TargetName->Name,
                            NameBytes) == NameBytes;
}

static NTSTATUS
UpdateIndexNodeInformation(
    _Inout_ PIndexNodeHeader Header,
    _In_ ULONG HeaderBytes,
    _In_ PFileNameEx TargetName,
    _In_ ULONGLONG FileReference,
    _In_ UINT32 Fields,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize,
    _In_ ULONG EaInformation,
    _In_ ULONG ReparseTag,
    _In_ ULONG StorageFlags)
{
    PIndexEntry Entry;
    PFileNameEx MatchedName = NULL;
    ULONG_PTR End;
    BOOLEAN FoundEnd = FALSE;

    if (Fields == 0 ||
        (Fields & ~NTFS_FILE_NAME_UPDATE_ALL) != 0 ||
        (StorageFlags &
         ~(FILE_PERM_SPARSE |
           FILE_PERM_COMPRESSED)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Header ||
        HeaderBytes < sizeof(*Header) ||
        Header->IndexOffset < sizeof(*Header) ||
        Header->IndexOffset > Header->TotalIndexSize ||
        Header->TotalIndexSize > Header->AllocatedSize ||
        Header->AllocatedSize > HeaderBytes ||
        (Header->Flags & ~NTFS_INDEX_HEADER_LARGE) != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Entry = reinterpret_cast<PIndexEntry>(
        reinterpret_cast<PUCHAR>(Header) +
            Header->IndexOffset);
    End = reinterpret_cast<ULONG_PTR>(Header) +
          Header->TotalIndexSize;

    while (reinterpret_cast<ULONG_PTR>(Entry) < End)
    {
        if (!NtfsIsDirectoryIndexEntryValid(
                Entry,
                (ULONG)(End -
                    reinterpret_cast<ULONG_PTR>(Entry))))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Entry->Flags & INDEX_ENTRY_END)
        {
            if (reinterpret_cast<ULONG_PTR>(Entry) +
                    Entry->EntryLength != End)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            FoundEnd = TRUE;
            break;
        }

        if (FileNameKeysMatch(Entry,
                              TargetName,
                              FileReference))
        {
            PFileNameEx IndexedName =
                reinterpret_cast<PFileNameEx>(
                    Entry->IndexStream);

            if (MatchedName)
                return STATUS_FILE_CORRUPT_ERROR;
            MatchedName = IndexedName;
        }

        Entry = reinterpret_cast<PIndexEntry>(
            reinterpret_cast<PUCHAR>(Entry) +
                Entry->EntryLength);
    }

    if (!FoundEnd)
        return STATUS_FILE_CORRUPT_ERROR;
    if (!MatchedName)
        return STATUS_NOT_FOUND;

    if (Fields & NTFS_FILE_NAME_UPDATE_SIZES)
    {
        MatchedName->AllocatedSize = AllocatedSize;
        MatchedName->DataSize = DataSize;
    }
    if (Fields & NTFS_FILE_NAME_UPDATE_REPARSE_TAG)
    {
        if (ReparseTag != 0)
        {
            MatchedName->Flags |=
                FILE_PERM_REPARSE_PT;
            MatchedName->Extended.ReparseTag =
                ReparseTag;
        }
        else
        {
            MatchedName->Flags &=
                ~FILE_PERM_REPARSE_PT;
            NtfsSetFileNameEaInformation(MatchedName, EaInformation);
        }
    }
    if ((Fields & NTFS_FILE_NAME_UPDATE_EA_SIZE) &&
        !(MatchedName->Flags & FILE_PERM_REPARSE_PT))
    {
        NtfsSetFileNameEaInformation(MatchedName, EaInformation);
    }
    if (Fields & NTFS_FILE_NAME_UPDATE_ARCHIVE)
        MatchedName->Flags |= FILE_PERM_ARCHIVE;
    if (Fields & NTFS_FILE_NAME_UPDATE_STORAGE_FLAGS)
    {
        MatchedName->Flags =
            (MatchedName->Flags &
             ~(FILE_PERM_SPARSE |
               FILE_PERM_COMPRESSED)) |
            StorageFlags;
    }
    if (Fields & NTFS_FILE_NAME_UPDATE_BASIC)
    {
        MatchedName->CreationTime = TargetName->CreationTime;
        MatchedName->LastAccessTime = TargetName->LastAccessTime;
        MatchedName->LastWriteTime = TargetName->LastWriteTime;
        MatchedName->ChangeTime = TargetName->ChangeTime;
        MatchedName->Flags = TargetName->Flags;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::UpdateParentIndexEntry(
    _In_ PFileNameEx TargetName,
    _In_ ULONGLONG FileReference,
    _In_ UINT32 Fields,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize,
    _In_ ULONG EaInformation,
    _In_ ULONG ReparseTag,
    _In_ ULONG StorageFlags)
{
    PFileRecord Parent = NULL;
    PFileRecord IndexRootOwner;
    PAttribute IndexRootAttribute;
    PIndexRootEx IndexRoot;
    PUCHAR IndexBufferData = NULL;
    PNtfsLookupSlot Pinned = NULL;
    ULONGLONG ParentRecordNumber;
    ULONGLONG AllocationUnit;
    ULONG IndexRecordSize;
    USHORT ParentSequence;
    IndexSearchKey Key;
    UNICODE_STRING Name;
    ULONGLONG ChildVcn;
    ULONGLONG VisitedVcns[64];
    ULONG VisitedCount = 0;
    ULONG EntryOffset;
    BOOLEAN Descend;
    BOOLEAN FoundExact;
    NTSTATUS Status;

    ParentRecordNumber =
        GetFRNFromFileRef(
            TargetName->ParentFileReference);
    ParentSequence = (USHORT)(
        TargetName->ParentFileReference >> 48);
    if (ParentRecordNumber > MAXULONG)
        return STATUS_FILE_CORRUPT_ERROR;

    Status = DiskVolume->MFT->GetFileRecord(
        (ULONG)ParentRecordNumber,
        &Parent);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Parent ||
        !(Parent->Header->Flags & FR_IS_DIRECTORY) ||
        (ParentSequence != 0 &&
         Parent->Header->SequenceNumber != ParentSequence))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    IndexRootAttribute = Parent->GetAttribute(
        TypeIndexRoot,
        const_cast<PWSTR>(NtfsI30Name));
    if (!IndexRootAttribute ||
        IndexRootAttribute->IsNonResident ||
        IndexRootAttribute->Resident.DataOffset < 0x18 ||
        IndexRootAttribute->Resident.DataLength <
            FIELD_OFFSET(IndexRootEx, Header) +
                sizeof(IndexNodeHeader) ||
        IndexRootAttribute->Resident.DataOffset >
            IndexRootAttribute->Length ||
        IndexRootAttribute->Resident.DataLength >
            IndexRootAttribute->Length -
                IndexRootAttribute->Resident.DataOffset)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    IndexRoot = reinterpret_cast<PIndexRootEx>(
        GetResidentDataPointer(IndexRootAttribute));
    IndexRecordSize = IndexRoot->BytesPerIndexRec;
    if (IndexRoot->AttributeType != TypeFileName ||
        IndexRoot->CollationRule !=
            ATTRDEF_COLLATION_FILENAME ||
        IndexRecordSize !=
            BytesPerIndexRecord(DiskVolume) ||
        IndexRecordSize < sizeof(IndexBuffer) ||
        IndexRecordSize %
            DiskVolume->BytesPerSector != 0 ||
        IndexRoot->ClusPerIndexRec !=
            (UCHAR)DiskVolume->ClustersPerIndexRecord)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    Name = NtfsMakeCountedUnicodeString(
        TargetName->Name,
        TargetName->NameLength * sizeof(WCHAR));
    Key.CollationRule = ATTRDEF_COLLATION_FILENAME;
    Key.Name = &Name;
    Key.Value = NULL;
    Key.ValueLength = 0;
    Key.CaseSensitive = Parent->IsCaseSensitiveDirectory();
    Status = NtfsFindIndexInsertionPoint(
        DiskVolume,
        &IndexRoot->Header,
        IndexRootAttribute->Resident.DataLength -
            FIELD_OFFSET(IndexRootEx, Header),
        &Key,
        &EntryOffset,
        &ChildVcn,
        &Descend,
        &FoundExact);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (!FoundExact)
    {
        if (!Descend)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        goto SearchAllocation;
    }

    Status = UpdateIndexNodeInformation(
        &IndexRoot->Header,
        IndexRootAttribute->Resident.DataLength -
            FIELD_OFFSET(IndexRootEx, Header),
        TargetName,
        FileReference,
        Fields,
        AllocatedSize,
        DataSize,
        EaInformation,
        ReparseTag,
        StorageFlags);
    if (NT_SUCCESS(Status))
    {
        IndexRootOwner =
            Parent->GetAttributeOwner(IndexRootAttribute);
        if (!IndexRootOwner)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        Status = DiskVolume->MFT->WriteFileRecordToMFT(
            IndexRootOwner);
        goto Done;
    }
    if (Status == STATUS_NOT_FOUND)
        Status = STATUS_FILE_CORRUPT_ERROR;
    goto Done;

SearchAllocation:
    AllocationUnit =
        IndexRecordSize < BytesPerCluster(DiskVolume)
            ? DiskVolume->BytesPerSector
            : BytesPerCluster(DiskVolume);
    if (AllocationUnit == 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    while (Descend)
    {
        PIndexBuffer NodeBuffer;
        ULONGLONG NodeVcn = ChildVcn;

        if (VisitedCount == RTL_NUMBER_OF(VisitedVcns) ||
            ChildVcn > ~(ULONGLONG)0 / AllocationUnit)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        for (ULONG Index = 0; Index < VisitedCount; Index++)
        {
            if (VisitedVcns[Index] == ChildVcn)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
        }
        VisitedVcns[VisitedCount++] = ChildVcn;

        Status = PinIndexNode(DiskVolume, Parent, NtfsI30Name, IndexRecordSize, AllocationUnit, ChildVcn, &Pinned);
        if (!NT_SUCCESS(Status))
            goto Done;
        NodeBuffer = reinterpret_cast<PIndexBuffer>(Pinned->Buffer);
        Status = NtfsFindIndexInsertionPoint(
            DiskVolume,
            &NodeBuffer->IndexHeader,
            IndexRecordSize - FIELD_OFFSET(IndexBuffer, IndexHeader),
            &Key,
            &EntryOffset,
            &ChildVcn,
            &Descend,
            &FoundExact,
            &Pinned->Table);
        if (!NT_SUCCESS(Status))
            goto Done;
        if (!FoundExact)
        {
            if (Descend)
                continue;
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        /* Only the node holding the name changes: it is edited in a copy of its own. */
        IndexBufferData =
            new(PagedPool, TAG_NTFS) UCHAR[IndexRecordSize];
        if (!IndexBufferData)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }
        RtlCopyMemory(IndexBufferData, Pinned->Buffer, IndexRecordSize);
        DiskVolume->ReleaseLookupSlot(Pinned);
        Pinned = NULL;
        NodeBuffer = reinterpret_cast<PIndexBuffer>(IndexBufferData);

        Status = UpdateIndexNodeInformation(
            &NodeBuffer->IndexHeader,
            IndexRecordSize -
                FIELD_OFFSET(IndexBuffer, IndexHeader),
            TargetName,
            FileReference,
            Fields,
            AllocatedSize,
            DataSize,
            EaInformation,
            ReparseTag,
            StorageFlags);
        if (Status == STATUS_NOT_FOUND)
            Status = STATUS_FILE_CORRUPT_ERROR;
        if (!NT_SUCCESS(Status))
            goto Done;

        Status = WriteIndexNode(DiskVolume, Parent, NtfsI30Name, NodeVcn, AllocationUnit, IndexRecordSize,
                                IndexBufferData);
        goto Done;
    }

    Status = STATUS_FILE_CORRUPT_ERROR;

Done:
    if (Pinned)
        DiskVolume->ReleaseLookupSlot(Pinned);
    delete[] IndexBufferData;
    delete Parent;
    return Status;
}

NTSTATUS
FileRecord::SynchronizeFileNameSizes(
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize)
{
    return SynchronizeFileNameInformation(
        NTFS_FILE_NAME_UPDATE_SIZES,
        AllocatedSize,
        DataSize,
        0,
        0,
        0);
}

NTSTATUS
FileRecord::SynchronizeFileNameEaSize(
    _In_ ULONG EaInformation)
{
    return SynchronizeFileNameInformation(
        NTFS_FILE_NAME_UPDATE_EA_SIZE,
        0,
        0,
        EaInformation,
        0,
        0);
}

NTSTATUS
FileRecord::SynchronizeFileNameInformation(
    _In_ UINT32 Fields,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize,
    _In_ ULONG EaInformation,
    _In_ ULONG ReparseTag,
    _In_ ULONG StorageFlags)
{
    ULONGLONG FileReference;
    ULONG Offset;
    NTSTATUS Status;

    if (Fields == 0 ||
        (Fields & ~NTFS_FILE_NAME_UPDATE_ALL) != 0 ||
        (StorageFlags &
         ~(FILE_PERM_SPARSE |
           FILE_PERM_COMPRESSED)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Data || !Header)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    if (Header->BaseFileRecord != 0)
    {
        /* An extension record holds no names; its base record carries them. */
        ASSERT(BaseRecordOwner);
        return BaseRecordOwner
            ? BaseRecordOwner->SynchronizeFileNameInformation(Fields,
                                                              AllocatedSize,
                                                              DataSize,
                                                              EaInformation,
                                                              ReparseTag,
                                                              StorageFlags)
            : STATUS_SUCCESS;
    }
    if (FileNameUpdatesDeferred)
    {
        if (Fields & NTFS_FILE_NAME_UPDATE_SIZES)
        {
            PendingAllocatedSize = AllocatedSize;
            PendingDataSize = DataSize;
        }
        if (Fields & (NTFS_FILE_NAME_UPDATE_EA_SIZE | NTFS_FILE_NAME_UPDATE_REPARSE_TAG))
            PendingEaInformation = EaInformation;
        if (Fields & NTFS_FILE_NAME_UPDATE_REPARSE_TAG)
            PendingReparseTag = ReparseTag;
        if (Fields & NTFS_FILE_NAME_UPDATE_STORAGE_FLAGS)
            PendingStorageFlags = StorageFlags;
        PendingFileNameFields |= Fields;
        return STATUS_SUCCESS;
    }

    FileReference =
        ((ULONGLONG)Header->SequenceNumber << 48) |
        RecordNumber;

    /* Names in extension records are listed; every name's parent entry carries the copy. */
    if (FindAttributeInRecord(TypeAttributeList, NULL, NULL))
    {
        ULONG ListOffset = 0;

        for (;;)
        {
            PAttribute Attribute = NULL;
            PFileNameEx FileName;

            Status = FindNextAttributeFromList(TypeFileName, &ListOffset, &Attribute);
            if (!NT_SUCCESS(Status))
                return Status;
            if (!Attribute)
                break;
            if (Attribute->IsNonResident ||
                Attribute->Resident.DataOffset < 0x18 ||
                Attribute->Resident.DataOffset > Attribute->Length ||
                Attribute->Resident.DataLength < FIELD_OFFSET(FileNameEx, Name) ||
                Attribute->Resident.DataLength >
                    Attribute->Length - Attribute->Resident.DataOffset)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            FileName = reinterpret_cast<PFileNameEx>(GetResidentDataPointer(Attribute));
            if (FileName->NameLength * sizeof(WCHAR) >
                Attribute->Resident.DataLength - FIELD_OFFSET(FileNameEx, Name))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            if (Fields & NTFS_FILE_NAME_UPDATE_BASIC)
                ApplyBasicInformationToFileName(FileName);
            Status = UpdateParentIndexEntry(FileName,
                                            FileReference,
                                            Fields,
                                            AllocatedSize,
                                            DataSize,
                                            EaInformation,
                                            ReparseTag,
                                            StorageFlags);
            if (!NT_SUCCESS(Status))
                return Status;
        }
        return UpdateFileNameInformation(Fields,
                                         AllocatedSize,
                                         DataSize,
                                         EaInformation,
                                         ReparseTag,
                                         StorageFlags);
    }

    Offset = Header->AttributeOffset;
    while (Offset < Header->ActualSize)
    {
        PAttribute Attribute;
        ULONG Remaining = Header->ActualSize - Offset;

        if (Remaining < sizeof(ULONG))
            return STATUS_FILE_CORRUPT_ERROR;
        Attribute =
            reinterpret_cast<PAttribute>(Data + Offset);
        if (Attribute->AttributeType ==
            TypeAttributeEndMarker)
        {
            break;
        }
        if (Attribute->Length < 0x18 ||
            (Attribute->Length &
             (sizeof(ULONGLONG) - 1)) != 0 ||
            Attribute->Length > Remaining)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Attribute->AttributeType == TypeFileName)
        {
            PFileNameEx FileName;
            ULONG NameBytes;

            if (Attribute->IsNonResident ||
                Attribute->Resident.DataOffset < 0x18 ||
                Attribute->Resident.DataOffset >
                    Attribute->Length ||
                Attribute->Resident.DataLength <
                    FIELD_OFFSET(FileNameEx, Name) ||
                Attribute->Resident.DataLength >
                    Attribute->Length -
                        Attribute->Resident.DataOffset)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            FileName = reinterpret_cast<PFileNameEx>(
                GetResidentDataPointer(Attribute));
            NameBytes =
                FileName->NameLength * sizeof(WCHAR);
            if (NameBytes >
                Attribute->Resident.DataLength -
                    FIELD_OFFSET(FileNameEx, Name))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            if (Fields & NTFS_FILE_NAME_UPDATE_BASIC)
                ApplyBasicInformationToFileName(FileName);
            Status = UpdateParentIndexEntry(
                FileName,
                FileReference,
                Fields,
                AllocatedSize,
                DataSize,
                EaInformation,
                ReparseTag,
                StorageFlags);
            if (!NT_SUCCESS(Status))
                return Status;
        }

        Offset += Attribute->Length;
    }

    return UpdateFileNameInformation(
        Fields,
        AllocatedSize,
        DataSize,
        EaInformation,
        ReparseTag,
        StorageFlags);
}

NTSTATUS
FileRecord::DeferFileNameUpdates(
    _In_ BOOLEAN Defer)
{
    if (!Header || Header->BaseFileRecord != 0 || !DiskVolume)
        return STATUS_INVALID_PARAMETER;
    if (Defer)
    {
        FileNameUpdatesDeferred = TRUE;
        return STATUS_SUCCESS;
    }
    FileNameUpdatesDeferred = FALSE;
    return FlushFileNameUpdates();
}

NTSTATUS
FileRecord::FlushFileNameUpdates()
{
    const UINT32 FlagFields = NTFS_FILE_NAME_UPDATE_ARCHIVE |
                              NTFS_FILE_NAME_UPDATE_REPARSE_TAG |
                              NTFS_FILE_NAME_UPDATE_STORAGE_FLAGS;
    UINT32 Fields = PendingFileNameFields;
    BOOLEAN Deferred = FileNameUpdatesDeferred;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(Fields == 0 || (Header && Header->BaseFileRecord == 0));
    ASSERT((Fields & ~NTFS_FILE_NAME_UPDATE_ALL) == 0);
    if (Fields == 0)
        return STATUS_SUCCESS;
    PendingFileNameFields = 0;
    FileNameUpdatesDeferred = FALSE;
    /* The basic copy rewrites the entry flags, so flag changes follow it. */
    if ((Fields & NTFS_FILE_NAME_UPDATE_BASIC) && (Fields & FlagFields))
    {
        Status = SynchronizeFileNameInformation(NTFS_FILE_NAME_UPDATE_BASIC, 0, 0, 0, 0, 0);
        Fields &= ~NTFS_FILE_NAME_UPDATE_BASIC;
    }
    if (NT_SUCCESS(Status))
    {
        Status = SynchronizeFileNameInformation(Fields,
                                                PendingAllocatedSize,
                                                PendingDataSize,
                                                PendingEaInformation,
                                                PendingReparseTag,
                                                PendingStorageFlags);
    }
    if (NT_SUCCESS(Status))
        Status = DiskVolume->MFT->WriteFileRecordToMFT(this);
    FileNameUpdatesDeferred = Deferred;
    ASSERT(PendingFileNameFields == 0);
    return Status;
}
