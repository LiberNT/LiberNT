/*
 * PROJECT:     LiberNT NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     $OBJECT_ID attribute and the volume's $ObjId index
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define NTFS_OBJECT_ID_EXTENDED_LENGTH (sizeof(ObjectIdEx) - sizeof(GUID))
#define NTFS_OBJECT_ID_ENTRY_DATA_LENGTH (sizeof(ULONGLONG) + NTFS_OBJECT_ID_EXTENDED_LENGTH)
#define NTFS_OBJECT_ID_ENTRY_LENGTH \
    ALIGN_UP_BY(FIELD_OFFSET(IndexEntry, IndexStream) + sizeof(GUID) + NTFS_OBJECT_ID_ENTRY_DATA_LENGTH, \
                sizeof(ULONGLONG))

static
BOOLEAN
ObjectIdExtendedIsZero(
    _In_ const ObjectIdEx* ObjectId)
{
    const UCHAR* Extended = reinterpret_cast<const UCHAR*>(&ObjectId->BirthVolId);

    for (ULONG Index = 0; Index < NTFS_OBJECT_ID_EXTENDED_LENGTH; Index++)
    {
        if (Extended[Index])
            return FALSE;
    }
    return TRUE;
}

static
ULONG
ObjectIdValueLength(
    _In_ const ObjectIdEx* ObjectId)
{
    return ObjectIdExtendedIsZero(ObjectId) ? sizeof(GUID) : sizeof(ObjectIdEx);
}

static
NTSTATUS
OpenObjectIdIndex(
    _In_ PVolume DiskVolume,
    _Out_ PFileRecord* IndexFile)
{
    WCHAR Path[] = L"\\$Extend\\$ObjId";
    NTSTATUS Status;

    *IndexFile = NULL;
    if (!DiskVolume || !DiskVolume->MFT)
        return STATUS_INVALID_PARAMETER;
    Status = DiskVolume->MFT->GetFileRecordFromQuery(Path, IndexFile);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND ||
        Status == STATUS_NOT_FOUND)
    {
        return STATUS_VOLUME_NOT_UPGRADED;
    }
    return Status;
}

NTSTATUS
NtfsCheckObjectIdSupport(
    _In_ PVolume DiskVolume)
{
    PFileRecord IndexFile;
    NTSTATUS Status;

    Status = OpenObjectIdIndex(DiskVolume, &IndexFile);
    delete IndexFile;
    return Status;
}

NTSTATUS
NtfsUpdateObjectIdIndex(
    _In_ PVolume DiskVolume,
    _In_ const ObjectIdEx* ObjectId,
    _In_ ULONGLONG FileReference,
    _In_ BOOLEAN Remove)
{
    UCHAR EntryData[NTFS_OBJECT_ID_ENTRY_LENGTH];
    PIndexEntry Entry = reinterpret_cast<PIndexEntry>(EntryData);
    PFileRecord IndexFile = NULL;
    IndexSearchKey Key;
    GUID KeyValue;
    NTSTATUS Status;

    Status = OpenObjectIdIndex(DiskVolume, &IndexFile);
    if (!NT_SUCCESS(Status))
        return Status;

    KeyValue = ObjectId->ObjectId;
    Key.CollationRule = ATTRDEF_COLLATION_ULONG_MULTI;
    Key.Name = NULL;
    Key.Value = reinterpret_cast<PUCHAR>(&KeyValue);
    Key.ValueLength = sizeof(KeyValue);

    {
        Directory ObjectIdIndex(DiskVolume);

        if (Remove)
        {
            Status = ObjectIdIndex.RemoveIndexEntry(IndexFile, L"$O", 0, &Key, 0);
            if (Status == STATUS_NOT_FOUND)
                Status = STATUS_SUCCESS;
        }
        else
        {
            RtlZeroMemory(EntryData, sizeof(EntryData));
            Entry->Data.ViewIndex.DataOffset = (USHORT)(FIELD_OFFSET(IndexEntry, IndexStream) + sizeof(GUID));
            Entry->Data.ViewIndex.DataLength = (USHORT)NTFS_OBJECT_ID_ENTRY_DATA_LENGTH;
            Entry->EntryLength = (UINT16)sizeof(EntryData);
            Entry->StreamLength = (UINT16)sizeof(GUID);
            RtlCopyMemory(Entry->IndexStream, &KeyValue, sizeof(GUID));
            RtlCopyMemory(EntryData + Entry->Data.ViewIndex.DataOffset, &FileReference, sizeof(FileReference));
            RtlCopyMemory(EntryData + Entry->Data.ViewIndex.DataOffset + sizeof(FileReference),
                          &ObjectId->BirthVolId,
                          NTFS_OBJECT_ID_EXTENDED_LENGTH);
            Status = ObjectIdIndex.AddIndexEntry(IndexFile, L"$O", 0, &Key, Entry, sizeof(EntryData));
            if (Status == STATUS_OBJECT_NAME_COLLISION)
                Status = STATUS_DUPLICATE_NAME;
        }
    }

    delete IndexFile;
    return Status;
}

NTSTATUS
FileRecord::ReadObjectId(
    _Out_ PObjectIdEx ObjectId)
{
    PAttribute Attribute;
    ULONG Length;

    RtlZeroMemory(ObjectId, sizeof(*ObjectId));
    Attribute = GetAttribute(TypeObjectId, NULL);
    if (!Attribute)
        return STATUS_OBJECTID_NOT_FOUND;
    if (Attribute->IsNonResident)
        return STATUS_FILE_CORRUPT_ERROR;

    Length = Attribute->Resident.DataLength;
    if (Length < sizeof(GUID) || Length > sizeof(*ObjectId) ||
        Attribute->Resident.DataOffset < 0x18 ||
        Attribute->Resident.DataOffset > Attribute->Length ||
        Length > Attribute->Length - Attribute->Resident.DataOffset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    RtlCopyMemory(ObjectId, GetResidentDataPointer(Attribute), Length);
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::MakeObjectIdSpace(
    _In_ ULONG Required)
{
    PAttribute DataAttribute;
    NTSTATUS Status;

    if (Header->ActualSize > Header->AllocatedSize)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Header->AllocatedSize - Header->ActualSize >= Required)
        return STATUS_SUCCESS;

    if (!FindAttributeInRecord(TypeAttributeList, NULL, NULL))
    {
        DataAttribute = (Header->Flags & FR_IS_DIRECTORY) ? NULL : FindAttributeInRecord(TypeData, NULL, NULL);
        if (DataAttribute && !DataAttribute->IsNonResident && DataAttribute->Resident.DataLength)
        {
            Status = PromoteResidentData(DataAttribute, NULL, 0, 0,
                                         DataAttribute->Resident.DataLength,
                                         DataAttribute->Resident.DataLength);
            if (!NT_SUCCESS(Status))
                return Status;
            if (Header->AllocatedSize - Header->ActualSize >= Required)
                return STATUS_SUCCESS;
        }
        Status = CreateInitialAttributeList();
        if (!NT_SUCCESS(Status))
            return Status == STATUS_INVALID_PARAMETER ? STATUS_BUFFER_TOO_SMALL : Status;
        if (Header->AllocatedSize - Header->ActualSize >= Required)
            return STATUS_SUCCESS;
    }

    /* TODO: the object identifier is not placed in an extension record when the base record cannot make room */
    return ReleaseBaseRecordSpace(Required);
}

NTSTATUS
FileRecord::InsertObjectIdAttribute(
    _In_ const ObjectIdEx* ObjectId)
{
    PAttribute Inserted = NULL;
    ULONG Length = ObjectIdValueLength(ObjectId);
    BOOLEAN OwnerReferenced = FALSE;
    NTSTATUS Status;

    Status = MakeObjectIdSpace(ALIGN_UP_BY(0x18 + Length, sizeof(ULONGLONG)));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = InsertResidentAttribute(TypeObjectId, NULL, &Inserted);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ReplaceResidentData(Inserted, reinterpret_cast<const UCHAR*>(ObjectId), Length);
    if (NT_SUCCESS(Status))
        Status = PrepareAutomaticTimestamps(NTFS_BASIC_INFO_CHANGE_TIME, NULL);
    if (NT_SUCCESS(Status) && FindAttributeInRecord(TypeAttributeList, NULL, NULL))
        Status = InsertAttributeListEntry(Inserted, this, 0, FALSE, &OwnerReferenced);
    if (!NT_SUCCESS(Status))
    {
        Inserted = FindAttributeInRecord(TypeObjectId, NULL, NULL);
        if (Inserted && !OwnerReferenced)
            (void)RemoveAttributeRecord(Inserted);
        return Status;
    }
    return DiskVolume->MFT->WriteFileRecordToMFT(this);
}

NTSTATUS
FileRecord::RemoveObjectIdAttribute()
{
    PAttribute Attribute = GetAttribute(TypeObjectId, NULL);
    NTSTATUS Status;

    if (!Attribute)
        return STATUS_OBJECTID_NOT_FOUND;
    if (FindAttributeInRecord(TypeAttributeList, NULL, NULL))
    {
        Status = RemoveListedAttribute(Attribute);
    }
    else
    {
        Status = RemoveAttributeRecord(Attribute);
    }
    if (NT_SUCCESS(Status))
        Status = PrepareAutomaticTimestamps(NTFS_BASIC_INFO_CHANGE_TIME, NULL);
    if (NT_SUCCESS(Status))
        Status = DiskVolume->MFT->WriteFileRecordToMFT(this);
    return Status;
}

NTSTATUS
FileRecord::SetObjectId(
    _In_ const ObjectIdEx* ObjectId)
{
    ULONGLONG FileReference;
    ObjectIdEx Existing;
    NTSTATUS Status;

    if (!ObjectId || !DiskVolume || !Header || Header->BaseFileRecord != 0)
        return STATUS_INVALID_PARAMETER;
    if (DiskVolume->IsReadOnly)
        return STATUS_MEDIA_WRITE_PROTECTED;
    Status = NtfsCheckObjectIdSupport(DiskVolume);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ReadObjectId(&Existing);
    if (NT_SUCCESS(Status))
        return STATUS_OBJECT_NAME_COLLISION;
    if (Status != STATUS_OBJECTID_NOT_FOUND)
        return Status;

    FileReference = ((ULONGLONG)Header->SequenceNumber << 48) | RecordNumber;
    Status = NtfsUpdateObjectIdIndex(DiskVolume, ObjectId, FileReference, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = InsertObjectIdAttribute(ObjectId);
    if (!NT_SUCCESS(Status))
        (void)NtfsUpdateObjectIdIndex(DiskVolume, ObjectId, FileReference, TRUE);
    return Status;
}

NTSTATUS
FileRecord::SetObjectIdExtended(
    _In_reads_bytes_(48) const UCHAR* ExtendedInfo)
{
    ULONGLONG FileReference;
    ObjectIdEx Current;
    ObjectIdEx Updated;
    NTSTATUS Status;

    if (!ExtendedInfo || !DiskVolume || !Header || Header->BaseFileRecord != 0)
        return STATUS_INVALID_PARAMETER;
    if (DiskVolume->IsReadOnly)
        return STATUS_MEDIA_WRITE_PROTECTED;
    Status = NtfsCheckObjectIdSupport(DiskVolume);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ReadObjectId(&Current);
    if (!NT_SUCCESS(Status))
        return Status;
    Updated = Current;
    RtlCopyMemory(&Updated.BirthVolId, ExtendedInfo, NTFS_OBJECT_ID_EXTENDED_LENGTH);

    FileReference = ((ULONGLONG)Header->SequenceNumber << 48) | RecordNumber;
    Status = RemoveObjectIdAttribute();
    if (!NT_SUCCESS(Status))
        return Status;
    Status = InsertObjectIdAttribute(&Updated);
    if (!NT_SUCCESS(Status))
    {
        (void)InsertObjectIdAttribute(&Current);
        return Status;
    }

    Status = NtfsUpdateObjectIdIndex(DiskVolume, &Current, FileReference, TRUE);
    if (NT_SUCCESS(Status))
        Status = NtfsUpdateObjectIdIndex(DiskVolume, &Updated, FileReference, FALSE);
    return Status;
}

NTSTATUS
FileRecord::DeleteObjectId()
{
    ULONGLONG FileReference;
    ObjectIdEx Current;
    NTSTATUS Status;

    if (!DiskVolume || !Header || Header->BaseFileRecord != 0)
        return STATUS_INVALID_PARAMETER;
    if (DiskVolume->IsReadOnly)
        return STATUS_MEDIA_WRITE_PROTECTED;
    Status = NtfsCheckObjectIdSupport(DiskVolume);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ReadObjectId(&Current);
    if (Status == STATUS_OBJECTID_NOT_FOUND)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    FileReference = ((ULONGLONG)Header->SequenceNumber << 48) | RecordNumber;
    Status = RemoveObjectIdAttribute();
    if (NT_SUCCESS(Status))
        Status = NtfsUpdateObjectIdIndex(DiskVolume, &Current, FileReference, TRUE);
    return Status;
}
