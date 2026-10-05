/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Validated NTFS security descriptor lookup and reading
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE 20
#define NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE 20
#define NTFS_SDS_DUPLICATE_OFFSET 0x40000
#define NTFS_SECURITY_DESCRIPTOR_REVISION 1
#define NTFS_SECURITY_DESCRIPTOR_SELF_RELATIVE 0x8000
#define NTFS_SECURITY_DESCRIPTOR_DACL_PRESENT 0x0004
#define NTFS_SECURITY_DESCRIPTOR_SACL_PRESENT 0x0010
#define NTFS_ACL_REVISION 2
#define NTFS_ACL_REVISION_DS 4
#define NTFS_MAX_SID_SUB_AUTHORITIES 15
#define NTFS_INDEX_HEADER_LARGE 1
#define NTFS_SDS_BLOCK_SIZE 0x80000
#define NTFS_SDS_ALIGNMENT 16
#define NTFS_SDH_PADDING 0x00490049
#define NTFS_FIRST_SECURITY_ID 0x100

typedef struct _NTFS_SECURITY_LOCATION
{
    ULONG Hash;
    ULONG SecurityId;
    ULONGLONG Offset;
    ULONG Length;
} NTFS_SECURITY_LOCATION, *PNTFS_SECURITY_LOCATION;

static const WCHAR NtfsSiiName[] = L"$SII";
static const WCHAR NtfsSdsName[] = L"$SDS";
static const WCHAR NtfsSdhName[] = L"$SDH";

static USHORT
ReadUnalignedU16(_In_ const UCHAR* Data)
{
    USHORT Value;

    RtlCopyMemory(&Value, Data, sizeof(Value));
    return Value;
}

static ULONG
ReadUnalignedU32(_In_ const UCHAR* Data)
{
    ULONG Value;

    RtlCopyMemory(&Value, Data, sizeof(Value));
    return Value;
}

static ULONGLONG
ReadUnalignedU64(_In_ const UCHAR* Data)
{
    ULONGLONG Value;

    RtlCopyMemory(&Value, Data, sizeof(Value));
    return Value;
}

static void
ReadSecurityLocation(_In_ const UCHAR* Data,
                     _Out_ PNTFS_SECURITY_LOCATION Location)
{
    Location->Hash = ReadUnalignedU32(Data);
    Location->SecurityId =
        ReadUnalignedU32(Data + sizeof(ULONG));
    Location->Offset =
        ReadUnalignedU64(Data + 2 * sizeof(ULONG));
    Location->Length =
        ReadUnalignedU32(Data + 2 * sizeof(ULONG) +
                         sizeof(ULONGLONG));
}

static BOOLEAN
ValidateSid(_In_ const UCHAR* Descriptor,
            _In_ ULONG DescriptorLength,
            _In_ ULONG Offset)
{
    ULONG SidLength;
    UCHAR SubAuthorityCount;

    if (Offset == 0)
        return TRUE;
    if ((Offset & (sizeof(ULONG) - 1)) != 0 ||
        Offset < NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE ||
        Offset > DescriptorLength ||
        DescriptorLength - Offset < 8)
    {
        return FALSE;
    }

    if (Descriptor[Offset] != NTFS_SECURITY_DESCRIPTOR_REVISION)
        return FALSE;
    SubAuthorityCount = Descriptor[Offset + 1];
    if (SubAuthorityCount > NTFS_MAX_SID_SUB_AUTHORITIES)
        return FALSE;

    SidLength = 8 + SubAuthorityCount * sizeof(ULONG);
    return SidLength <= DescriptorLength - Offset;
}

static BOOLEAN
ValidateAcl(_In_ const UCHAR* Descriptor,
            _In_ ULONG DescriptorLength,
            _In_ ULONG Offset)
{
    const UCHAR* Acl;
    ULONG AceOffset;
    ULONG AclSize;
    ULONG AceCount;

    if (Offset == 0)
        return TRUE;
    if ((Offset & (sizeof(ULONG) - 1)) != 0 ||
        Offset < NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE ||
        Offset > DescriptorLength ||
        DescriptorLength - Offset < 8)
    {
        return FALSE;
    }

    Acl = Descriptor + Offset;
    if (Acl[0] != NTFS_ACL_REVISION &&
        Acl[0] != NTFS_ACL_REVISION_DS)
    {
        return FALSE;
    }

    AclSize = ReadUnalignedU16(Acl + 2);
    AceCount = ReadUnalignedU16(Acl + 4);
    if (AclSize < 8 || AclSize > DescriptorLength - Offset)
        return FALSE;

    AceOffset = 8;
    for (ULONG Index = 0; Index < AceCount; Index++)
    {
        ULONG AceSize;

        if (AceOffset > AclSize ||
            AclSize - AceOffset < 4)
        {
            return FALSE;
        }

        AceSize = ReadUnalignedU16(Acl + AceOffset + 2);
        if (AceSize < 4 ||
            (AceSize & (sizeof(ULONG) - 1)) != 0 ||
            AceSize > AclSize - AceOffset)
        {
            return FALSE;
        }
        AceOffset += AceSize;
    }

    return AceOffset <= AclSize;
}

static BOOLEAN
ValidateSecurityDescriptor(_In_ const UCHAR* Descriptor,
                           _In_ ULONG DescriptorLength)
{
    ULONG OwnerOffset;
    ULONG GroupOffset;
    ULONG SaclOffset;
    ULONG DaclOffset;
    USHORT Control;

    if (!Descriptor ||
        DescriptorLength < NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE ||
        (DescriptorLength & (sizeof(ULONG) - 1)) != 0 ||
        Descriptor[0] != NTFS_SECURITY_DESCRIPTOR_REVISION ||
        Descriptor[1] != 0)
    {
        return FALSE;
    }

    Control = ReadUnalignedU16(Descriptor + 2);
    if (!(Control & NTFS_SECURITY_DESCRIPTOR_SELF_RELATIVE))
        return FALSE;

    OwnerOffset = ReadUnalignedU32(Descriptor + 4);
    GroupOffset = ReadUnalignedU32(Descriptor + 8);
    SaclOffset = ReadUnalignedU32(Descriptor + 12);
    DaclOffset = ReadUnalignedU32(Descriptor + 16);

    if (!ValidateSid(Descriptor, DescriptorLength, OwnerOffset) ||
        !ValidateSid(Descriptor, DescriptorLength, GroupOffset) ||
        (!(Control & NTFS_SECURITY_DESCRIPTOR_SACL_PRESENT) &&
         SaclOffset != 0) ||
        (!(Control & NTFS_SECURITY_DESCRIPTOR_DACL_PRESENT) &&
         DaclOffset != 0) ||
        !ValidateAcl(Descriptor, DescriptorLength, SaclOffset) ||
        !ValidateAcl(Descriptor, DescriptorLength, DaclOffset))
    {
        return FALSE;
    }

    return TRUE;
}

NTSTATUS
FileRecord::ApplySecurityId(
    _In_ ULONG SecurityId)
{
    PAttribute SecurityAttribute;
    PAttribute StandardAttribute;
    PStandardInformationEx Standard;
    PUCHAR RecordBackup;
    BOOLEAN WriteAttempted = FALSE;
    NTSTATUS Status;

    if (SecurityId == 0 || !Header || !Data || !DiskVolume)
        return STATUS_INVALID_PARAMETER;
    if (FindAttributeInRecord(TypeAttributeList, NULL, NULL))
        return ApplyListedSecurityId(SecurityId);

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD) UCHAR[RecordBufferSize];
    if (!RecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RecordBackup, Data, RecordBufferSize);

    SecurityAttribute = FindAttributeInRecord(
        TypeSecurityDescriptor,
        NULL,
        NULL);
    if (SecurityAttribute)
    {
        Status = RemoveAttributeRecord(SecurityAttribute);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        goto Restore;
    if (StandardAttribute->Resident.DataLength <
        sizeof(StandardInformationEx))
    {
        Status = ResizeResidentData(
            StandardAttribute,
            sizeof(StandardInformationEx));
        if (!NT_SUCCESS(Status))
            goto Restore;
        Status = GetStandardInformationForUpdate(
            &StandardAttribute,
            &Standard);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }
    Standard->SecurityId = SecurityId;

    Status = PrepareAutomaticTimestamps(
        NTFS_BASIC_INFO_CHANGE_TIME,
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;
    WriteAttempted = TRUE;
    Status = DiskVolume->MFT->WriteFileRecordToMFT(this);

Restore:
    if (!NT_SUCCESS(Status))
    {
        RtlCopyMemory(Data, RecordBackup, RecordBufferSize);
        Header = reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
        if (WriteAttempted)
        {
            NTSTATUS RestoreStatus =
                DiskVolume->MFT->WriteFileRecordToMFT(this);
            if (!NT_SUCCESS(RestoreStatus))
                Status = RestoreStatus;
        }
    }
    delete[] RecordBackup;
    return Status;
}

NTSTATUS
FileRecord::ApplyListedSecurityId(
    _In_ ULONG SecurityId)
{
    const ULONG MinimumEntryLength = 0x1a;
    PAttribute ListAttribute;
    PAttribute SecurityAttribute;
    PAttribute StandardAttribute;
    PStandardInformationEx Standard;
    PFileRecord Owner = NULL;
    PUCHAR BaseBackup = NULL;
    PUCHAR OwnerBackup = NULL;
    PUCHAR OldList = NULL;
    PUCHAR NewList = NULL;
    LARGE_INTEGER ListOffset;
    ULONG OldListLength = 0;
    ULONG NewListLength = 0;
    ULONG WrittenLength;
    ULONG Offset;
    BOOLEAN Found = FALSE;
    BOOLEAN ListWriteAttempted = FALSE;
    BOOLEAN BaseWriteAttempted = FALSE;
    NTSTATUS Status;

    if (SecurityId == 0 || !Header || !Data || !DiskVolume ||
        Header->BaseFileRecord != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = LoadAttributeList();
    if (!NT_SUCCESS(Status))
        return Status;
    if (!AttributeListData || AttributeListLength == 0)
        return STATUS_FILE_CORRUPT_ERROR;

    SecurityAttribute = GetAttribute(TypeSecurityDescriptor, NULL);
    if (SecurityAttribute)
    {
        Owner = GetAttributeOwner(SecurityAttribute);
        if (!Owner || SecurityAttribute->NameLength != 0)
            return STATUS_FILE_CORRUPT_ERROR;
        if (SecurityAttribute->IsNonResident)
            return STATUS_BUFFER_TOO_SMALL;
    }

    OldListLength = AttributeListLength;
    OldList = new(PagedPool, TAG_NTFS) UCHAR[OldListLength];
    NewList = new(PagedPool, TAG_NTFS) UCHAR[OldListLength];
    BaseBackup = new(PagedPool, TAG_FILE_RECORD) UCHAR[RecordBufferSize];
    if (Owner && Owner != this)
        OwnerBackup = new(PagedPool, TAG_FILE_RECORD) UCHAR[Owner->RecordBufferSize];
    if (!OldList || !NewList || !BaseBackup ||
        (Owner && Owner != this && !OwnerBackup))
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(OldList, AttributeListData, OldListLength);

    for (Offset = 0; Offset < OldListLength;)
    {
        PAttributeListEx Entry =
            reinterpret_cast<PAttributeListEx>(OldList + Offset);

        if (OldListLength - Offset < MinimumEntryLength ||
            Entry->RecordLength < MinimumEntryLength ||
            Entry->RecordLength > OldListLength - Offset)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        if (SecurityAttribute && !Found &&
            Entry->Type == TypeSecurityDescriptor &&
            Entry->NameLength == 0 &&
            Entry->FirstVCN == 0 &&
            GetFRNFromFileRef(Entry->BaseFileRef) ==
                Owner->Header->MFTRecordNumber &&
            Entry->AttributeId == SecurityAttribute->AttributeID)
        {
            Found = TRUE;
        }
        else
        {
            RtlCopyMemory(NewList + NewListLength, Entry, Entry->RecordLength);
            NewListLength += Entry->RecordLength;
        }
        Offset += Entry->RecordLength;
    }
    if (SecurityAttribute && !Found)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    RtlCopyMemory(BaseBackup, Data, RecordBufferSize);
    if (OwnerBackup)
        RtlCopyMemory(OwnerBackup, Owner->Data, Owner->RecordBufferSize);

    ListAttribute = FindAttributeInRecord(TypeAttributeList, NULL, NULL);
    if (!ListAttribute)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Restore;
    }
    if (SecurityAttribute && ListAttribute->IsNonResident)
    {
        if (ListAttribute->NonResident.DataSize != OldListLength)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Restore;
        }
        ListWriteAttempted = TRUE;
        WrittenLength = NewListLength;
        ListOffset.QuadPart = 0;
        Status = WriteFileData(TypeAttributeList,
                               NULL,
                               NewList,
                               &WrittenLength,
                               &ListOffset);
        if (NT_SUCCESS(Status) && WrittenLength != NewListLength)
            Status = STATUS_END_OF_FILE;
        if (!NT_SUCCESS(Status))
            goto Restore;
        RtlCopyMemory(Data, BaseBackup, RecordBufferSize);
        Header = reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
        SecurityAttribute = GetAttribute(TypeSecurityDescriptor, NULL);
        if (!SecurityAttribute || GetAttributeOwner(SecurityAttribute) != Owner)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Restore;
        }
    }

    if (SecurityAttribute)
    {
        Status = Owner->RemoveAttributeRecord(SecurityAttribute);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }

    Status = GetStandardInformationForUpdate(&StandardAttribute, &Standard);
    if (!NT_SUCCESS(Status))
        goto Restore;
    if (GetAttributeOwner(StandardAttribute) != this)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Restore;
    }
    if (StandardAttribute->Resident.DataLength < sizeof(StandardInformationEx))
    {
        Status = ResizeResidentData(StandardAttribute, sizeof(StandardInformationEx));
        if (!NT_SUCCESS(Status))
            goto Restore;
        Status = GetStandardInformationForUpdate(&StandardAttribute, &Standard);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }
    Standard->SecurityId = SecurityId;

    if (SecurityAttribute)
    {
        ListAttribute = FindAttributeInRecord(TypeAttributeList, NULL, NULL);
        if (!ListAttribute)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Restore;
        }
        if (!ListAttribute->IsNonResident)
        {
            Status = ReplaceResidentData(ListAttribute, NewList, NewListLength);
            if (!NT_SUCCESS(Status))
                goto Restore;
        }
        else
        {
            ListAttribute->NonResident.DataSize = NewListLength;
            ListAttribute->NonResident.InitalizedDataSize = NewListLength;
        }
    }

    Status = PrepareAutomaticTimestamps(NTFS_BASIC_INFO_CHANGE_TIME, NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;
    BaseWriteAttempted = TRUE;
    Status = DiskVolume->MFT->WriteFileRecordToMFT(this);
    if (!NT_SUCCESS(Status))
        goto Restore;

    delete[] AttributeListData;
    AttributeListData = NULL;
    AttributeListLength = 0;

    if (Owner && Owner != this)
    {
        PAttribute First = reinterpret_cast<PAttribute>(
            Owner->Data + Owner->Header->AttributeOffset);
        NTSTATUS CleanupStatus;

        if (First->AttributeType == TypeAttributeEndMarker)
            CleanupStatus = DiskVolume->MFT->DeallocateExtensionFileRecord(Owner);
        else
            CleanupStatus = DiskVolume->MFT->WriteFileRecordToMFT(Owner);
        if (!NT_SUCCESS(CleanupStatus))
        {
            DPRINT1("Extension record %lu was not updated after its "
                    "security descriptor moved to $Secure: 0x%lx.\n",
                    Owner->Header->MFTRecordNumber,
                    CleanupStatus);
        }
        ClearExtentCacheExcept(NULL);
    }
    goto Done;

Restore:
    RtlCopyMemory(Data, BaseBackup, RecordBufferSize);
    Header = reinterpret_cast<PFileRecordHeader>(Data);
    ClearDataRunCache();
    if (OwnerBackup)
    {
        RtlCopyMemory(Owner->Data, OwnerBackup, Owner->RecordBufferSize);
        Owner->Header = reinterpret_cast<PFileRecordHeader>(Owner->Data);
        Owner->ClearDataRunCache();
    }
    if (ListWriteAttempted)
    {
        NTSTATUS RestoreStatus;

        WrittenLength = OldListLength;
        ListOffset.QuadPart = 0;
        RestoreStatus = WriteFileData(TypeAttributeList,
                                      NULL,
                                      OldList,
                                      &WrittenLength,
                                      &ListOffset);
        if (!NT_SUCCESS(RestoreStatus))
            Status = RestoreStatus;
        RtlCopyMemory(Data, BaseBackup, RecordBufferSize);
        Header = reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
    }
    if (BaseWriteAttempted || ListWriteAttempted)
    {
        NTSTATUS RestoreStatus = DiskVolume->MFT->WriteFileRecordToMFT(this);

        if (!NT_SUCCESS(RestoreStatus))
            Status = RestoreStatus;
    }
    delete[] AttributeListData;
    AttributeListData = NULL;
    AttributeListLength = 0;

Done:
    delete[] OwnerBackup;
    delete[] BaseBackup;
    delete[] NewList;
    delete[] OldList;
    return Status;
}

NTSTATUS
FileRecord::SetSecurityDescriptor(
    _In_reads_bytes_(BufferLength) const UCHAR* Buffer,
    _In_ ULONG BufferLength)
{
    PAttribute SecurityAttribute;
    PAttribute StandardAttribute;
    PStandardInformationEx Standard;
    PUCHAR RecordBackup = NULL;
    ULONG SecurityId;
    BOOLEAN Committed = FALSE;
    BOOLEAN WriteAttempted = FALSE;
    NTSTATUS Status;

    if (!Buffer || BufferLength == 0)
        return STATUS_INVALID_PARAMETER;
    if (!DiskVolume)
        return STATUS_INVALID_DEVICE_STATE;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if (!ValidateSecurityDescriptor(Buffer, BufferLength))
        return STATUS_INVALID_PARAMETER;

    Status = DiskVolume->AssignSecurityId(Buffer, BufferLength, &SecurityId);
    if (NT_SUCCESS(Status))
    {
        Status = ApplySecurityId(SecurityId);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            return Status;
    }
    else if (Status != STATUS_NOT_IMPLEMENTED)
    {
        return Status;
    }
    if (FindAttributeInRecord(TypeAttributeList, NULL, NULL))
        return ReplaceSecurityDescriptorData(Buffer, BufferLength);

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD) UCHAR[RecordBufferSize];
    if (!RecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RecordBackup, Data, RecordBufferSize);

    SecurityAttribute = FindAttributeInRecord(
        TypeSecurityDescriptor,
        NULL,
        NULL);
    if (SecurityAttribute &&
        SecurityAttribute->IsNonResident)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Restore;
    }
    if (!SecurityAttribute)
    {
        Status = InsertResidentAttribute(
            TypeSecurityDescriptor,
            NULL,
            &SecurityAttribute);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }
    Status = ReplaceResidentData(SecurityAttribute,
                                 Buffer,
                                 BufferLength);
    if (!NT_SUCCESS(Status))
        goto Restore;

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        goto Restore;
    /* V1 $STANDARD_INFORMATION predates SecurityId; only the NTFS 3.x
     * form carries the field to clear.
     */
    if (StandardAttribute->Resident.DataLength >=
        FIELD_OFFSET(StandardInformationEx, SecurityId) +
            sizeof(UINT32))
    {
        Standard->SecurityId = 0;
    }

    Status = PrepareAutomaticTimestamps(
        NTFS_BASIC_INFO_CHANGE_TIME,
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;
    WriteAttempted = TRUE;
    Status = DiskVolume->MFT->WriteFileRecordToMFT(this);
    if (!NT_SUCCESS(Status))
        goto Restore;
    Committed = TRUE;

Restore:
    if (!Committed)
    {
        RtlCopyMemory(Data, RecordBackup, RecordBufferSize);
        Header = reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
        if (WriteAttempted)
        {
            NTSTATUS RestoreStatus =
                DiskVolume->MFT->WriteFileRecordToMFT(this);
            if (!NT_SUCCESS(RestoreStatus))
                Status = RestoreStatus;
        }
    }
    delete[] RecordBackup;
    if (!WriteAttempted && Status == STATUS_BUFFER_TOO_SMALL)
        return ReplaceSecurityDescriptorData(Buffer, BufferLength);
    return Status;
}

static ULONG
SecurityDescriptorHash(_In_ const UCHAR* Descriptor,
                       _In_ ULONG DescriptorLength)
{
    ULONG Hash = 0;

    for (ULONG Offset = 0;
         Offset < DescriptorLength;
         Offset += sizeof(ULONG))
    {
        Hash = ReadUnalignedU32(Descriptor + Offset) +
               ((Hash << 3) | (Hash >> 29));
    }
    return Hash;
}

static NTSTATUS
ReadExactAttribute(_In_ PFileRecord File,
                   _In_ PAttribute Attribute,
                   _Out_ PUCHAR Buffer,
                   _In_ ULONG Length,
                   _In_ ULONGLONG Offset)
{
    ULONG Remaining = Length;
    NTSTATUS Status;

    Status = File->CopyData(Attribute,
                            Buffer,
                            &Remaining,
                            Offset);
    if (!NT_SUCCESS(Status))
        return Status;
    return Remaining == 0 ? STATUS_SUCCESS : STATUS_END_OF_FILE;
}

static NTSTATUS
FindSecurityIdInNode(
    _In_ ULONG SecurityId,
    _In_ PIndexNodeHeader Header,
    _In_ ULONG HeaderBytes,
    _Out_ PNTFS_SECURITY_LOCATION Location,
    _Out_ PULONGLONG ChildVCN,
    _Out_ PBOOLEAN Descend)
{
    PIndexEntry Entry;
    ULONG_PTR End;
    BOOLEAN FoundEnd = FALSE;

    RtlZeroMemory(Location, sizeof(*Location));
    *ChildVCN = 0;
    *Descend = FALSE;

    if (HeaderBytes < sizeof(*Header) ||
        Header->IndexOffset < sizeof(*Header) ||
        Header->IndexOffset > Header->TotalIndexSize ||
        Header->TotalIndexSize > HeaderBytes ||
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
        ULONG EffectiveLength;
        ULONG Remaining;
        ULONG Key;

        Remaining = (ULONG)(End -
                    reinterpret_cast<ULONG_PTR>(Entry));
        if (Remaining < FIELD_OFFSET(IndexEntry, IndexStream) ||
            Entry->EntryLength <
                FIELD_OFFSET(IndexEntry, IndexStream) ||
            (Entry->EntryLength & (sizeof(ULONGLONG) - 1)) != 0 ||
            Entry->EntryLength > Remaining ||
            (Entry->Flags & ~(INDEX_ENTRY_NODE |
                              INDEX_ENTRY_END)) != 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        EffectiveLength = Entry->EntryLength;
        if (Entry->Flags & INDEX_ENTRY_NODE)
        {
            if (EffectiveLength <
                FIELD_OFFSET(IndexEntry, IndexStream) +
                    sizeof(ULONGLONG))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            EffectiveLength -= sizeof(ULONGLONG);
        }

        if (Entry->Flags & INDEX_ENTRY_END)
        {
            if (Entry->StreamLength != 0 ||
                Entry->Data.ViewIndex.DataLength != 0)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            FoundEnd = TRUE;
            if (Entry->Flags & INDEX_ENTRY_NODE)
            {
                *ChildVCN = ReadUnalignedU64(
                    reinterpret_cast<PUCHAR>(Entry) +
                    Entry->EntryLength - sizeof(ULONGLONG));
                *Descend = TRUE;
            }
            break;
        }

        if (Entry->StreamLength != sizeof(ULONG) ||
            Entry->Data.ViewIndex.Reserved != 0 ||
            Entry->Data.ViewIndex.DataLength !=
                NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE ||
            Entry->Data.ViewIndex.DataOffset <
                FIELD_OFFSET(IndexEntry, IndexStream) +
                    sizeof(ULONG) ||
            Entry->Data.ViewIndex.DataOffset >
                EffectiveLength ||
            Entry->Data.ViewIndex.DataLength >
                EffectiveLength -
                    Entry->Data.ViewIndex.DataOffset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Key = ReadUnalignedU32(Entry->IndexStream);
        ReadSecurityLocation(
            reinterpret_cast<PUCHAR>(Entry) +
                Entry->Data.ViewIndex.DataOffset,
            Location);
        if (Location->SecurityId != Key ||
            Location->Length <
                NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE +
                NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE ||
            (Location->Offset &
                (sizeof(ULONGLONG) * 2 - 1)) != 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (SecurityId == Key)
            return STATUS_SUCCESS;

        if (SecurityId < Key)
        {
            if (Entry->Flags & INDEX_ENTRY_NODE)
            {
                *ChildVCN = ReadUnalignedU64(
                    reinterpret_cast<PUCHAR>(Entry) +
                    Entry->EntryLength - sizeof(ULONGLONG));
                *Descend = TRUE;
                return STATUS_SUCCESS;
            }
            return STATUS_NOT_FOUND;
        }

        Entry = reinterpret_cast<PIndexEntry>(
            reinterpret_cast<PUCHAR>(Entry) +
            Entry->EntryLength);
    }

    if (!FoundEnd)
        return STATUS_FILE_CORRUPT_ERROR;
    return *Descend ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

static NTSTATUS
FindSecurityDescriptorLocation(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord SecureFile,
    _In_ ULONG SecurityId,
    _Out_ PNTFS_SECURITY_LOCATION Location)
{
    const ULONGLONG MaximumValue = ~(ULONGLONG)0;
    PAttribute IndexRootAttribute;
    PAttribute IndexAllocationAttribute;
    PAttribute BitmapAttribute;
    PIndexRootEx IndexRoot;
    PUCHAR IndexBufferData = NULL;
    ULONGLONG BitmapLength;
    ULONGLONG ChildVCN;
    ULONGLONG VisitedVCNs[64];
    ULONG VisitedCount = 0;
    ULONG IndexRecordSize;
    BOOLEAN Descend;
    NTSTATUS Status;

    IndexRootAttribute = SecureFile->GetAttribute(
        TypeIndexRoot,
        const_cast<PWSTR>(NtfsSiiName));
    if (!IndexRootAttribute ||
        IndexRootAttribute->IsNonResident ||
        IndexRootAttribute->Resident.DataLength <
            FIELD_OFFSET(IndexRootEx, Header) +
                sizeof(IndexNodeHeader))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRoot = reinterpret_cast<PIndexRootEx>(
        GetResidentDataPointer(IndexRootAttribute));
    IndexRecordSize = IndexRoot->BytesPerIndexRec;
    if (IndexRoot->AttributeType != 0 ||
        IndexRoot->CollationRule != ATTRDEF_COLLATION_ULONG ||
        IndexRecordSize != BytesPerIndexRecord(DiskVolume) ||
        IndexRecordSize < sizeof(IndexBuffer) ||
        IndexRecordSize % DiskVolume->BytesPerSector != 0 ||
        IndexRoot->ClusPerIndexRec !=
            (UCHAR)DiskVolume->ClustersPerIndexRecord)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = FindSecurityIdInNode(
        SecurityId,
        &IndexRoot->Header,
        IndexRootAttribute->Resident.DataLength -
            FIELD_OFFSET(IndexRootEx, Header),
        Location,
        &ChildVCN,
        &Descend);
    if (!NT_SUCCESS(Status) || !Descend)
        return Status;

    IndexAllocationAttribute = SecureFile->GetAttribute(
        TypeIndexAllocation,
        const_cast<PWSTR>(NtfsSiiName));
    BitmapAttribute = SecureFile->GetAttribute(
        TypeBitmap,
        const_cast<PWSTR>(NtfsSiiName));
    if (!IndexAllocationAttribute ||
        !IndexAllocationAttribute->IsNonResident ||
        !BitmapAttribute)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    BitmapLength = GetAttributeDataSize(BitmapAttribute);
    if (BitmapLength == 0)
        return STATUS_FILE_CORRUPT_ERROR;

    IndexBufferData =
        new(PagedPool, TAG_NTFS) UCHAR[IndexRecordSize];
    if (!IndexBufferData)
        return STATUS_INSUFFICIENT_RESOURCES;

    while (Descend)
    {
        PIndexBuffer NodeBuffer;
        ULONGLONG AllocationUnit;
        ULONGLONG AllocationOffset;
        ULONGLONG IndexRecordNumber;
        ULONGLONG BitmapByte;
        ULONG BytesRemaining;
        UCHAR BitmapMask;
        UCHAR BitmapValue;

        if (VisitedCount == RTL_NUMBER_OF(VisitedVCNs))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        for (ULONG Index = 0; Index < VisitedCount; Index++)
        {
            if (VisitedVCNs[Index] == ChildVCN)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
        }
        VisitedVCNs[VisitedCount++] = ChildVCN;

        AllocationUnit =
            IndexRecordSize < BytesPerCluster(DiskVolume)
                ? DiskVolume->BytesPerSector
                : BytesPerCluster(DiskVolume);
        if (AllocationUnit == 0 ||
            ChildVCN > MaximumValue / AllocationUnit)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        AllocationOffset = ChildVCN * AllocationUnit;
        if (AllocationOffset % IndexRecordSize != 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        IndexRecordNumber = AllocationOffset / IndexRecordSize;
        if ((IndexRecordNumber >> 3) >= BitmapLength)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        BitmapByte = IndexRecordNumber >> 3;
        BitmapMask =
            (UCHAR)(1 << (IndexRecordNumber & 7));
        BytesRemaining = sizeof(BitmapValue);
        Status = SecureFile->CopyData(
            BitmapAttribute,
            &BitmapValue,
            &BytesRemaining,
            BitmapByte);
        if (!NT_SUCCESS(Status) ||
            BytesRemaining != 0 ||
            !(BitmapValue & BitmapMask))
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        BytesRemaining = IndexRecordSize;
        Status = SecureFile->CopyData(
            IndexAllocationAttribute,
            IndexBufferData,
            &BytesRemaining,
            AllocationOffset);
        if (!NT_SUCCESS(Status) || BytesRemaining != 0)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_END_OF_FILE;
            goto Done;
        }

        NodeBuffer =
            reinterpret_cast<PIndexBuffer>(IndexBufferData);
        Status = NtfsApplyFixup(
            &NodeBuffer->RecordHeader,
            IndexRecordSize,
            DiskVolume->BytesPerSector);
        if (!NT_SUCCESS(Status) ||
            RtlCompareMemory(NodeBuffer->RecordHeader.TypeID,
                             "INDX",
                             4) != 4 ||
            NodeBuffer->VCN != ChildVCN)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        Descend = FALSE;
        Status = FindSecurityIdInNode(
            SecurityId,
            &NodeBuffer->IndexHeader,
            IndexRecordSize -
                FIELD_OFFSET(IndexBuffer, IndexHeader),
            Location,
            &ChildVCN,
            &Descend);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

Done:
    delete[] IndexBufferData;
    return Status;
}

static NTSTATUS
ValidateSdsHeaderAt(
    _In_ PFileRecord SecureFile,
    _In_ PAttribute SdsAttribute,
    _In_ PNTFS_SECURITY_LOCATION Location,
    _In_ ULONGLONG PhysicalOffset)
{
    UCHAR Header[NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE];
    NTFS_SECURITY_LOCATION Stored;
    ULONGLONG SdsSize = GetAttributeDataSize(SdsAttribute);
    NTSTATUS Status;

    if (PhysicalOffset > SdsSize ||
        Location->Length > SdsSize - PhysicalOffset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = ReadExactAttribute(
        SecureFile,
        SdsAttribute,
        Header,
        sizeof(Header),
        PhysicalOffset);
    if (!NT_SUCCESS(Status))
        return Status;

    ReadSecurityLocation(Header, &Stored);
    if (Stored.Hash != Location->Hash ||
        Stored.SecurityId != Location->SecurityId ||
        Stored.Offset != Location->Offset ||
        Stored.Length != Location->Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
ReadAndValidateSdsDescriptor(
    _In_ PFileRecord SecureFile,
    _In_ PAttribute SdsAttribute,
    _In_ PNTFS_SECURITY_LOCATION Location,
    _In_ ULONGLONG PhysicalOffset,
    _Out_ PUCHAR Buffer)
{
    ULONG DescriptorLength =
        Location->Length -
        NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE;
    NTSTATUS Status;

    Status = ValidateSdsHeaderAt(
        SecureFile,
        SdsAttribute,
        Location,
        PhysicalOffset);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ReadExactAttribute(
        SecureFile,
        SdsAttribute,
        Buffer,
        DescriptorLength,
        PhysicalOffset +
            NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!ValidateSecurityDescriptor(Buffer,
                                    DescriptorLength) ||
        SecurityDescriptorHash(Buffer,
                               DescriptorLength) !=
            Location->Hash)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
ReadDirectSecurityDescriptor(
    _In_ PFileRecord File,
    _In_ PAttribute Attribute,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength)
{
    ULONGLONG DataSize;
    ULONG Capacity;
    ULONG Required;
    NTSTATUS Status;

    DataSize = GetAttributeDataSize(Attribute);
    if (DataSize < NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE)
        return STATUS_FILE_CORRUPT_ERROR;
    if (DataSize > MAXULONG)
        return STATUS_FILE_TOO_LARGE;

    Capacity = *BufferLength;
    Required = (ULONG)DataSize;
    *BufferLength = Required;
    if (!Buffer || Capacity < Required)
        return STATUS_BUFFER_TOO_SMALL;

    Status = ReadExactAttribute(
        File,
        Attribute,
        Buffer,
        Required,
        0);
    if (!NT_SUCCESS(Status))
        return Status;
    return ValidateSecurityDescriptor(Buffer, Required)
        ? STATUS_SUCCESS
        : STATUS_FILE_CORRUPT_ERROR;
}

NTSTATUS
Volume::ReadSecurityDescriptorById(
    _In_ ULONG SecurityId,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength)
{
    PFileRecord SecureFile = NULL;
    PAttribute SdsAttribute;
    NTFS_SECURITY_LOCATION Location;
    ULONGLONG DuplicateOffset;
    ULONG Capacity;
    ULONG Required;
    NTSTATUS FirstStatus;
    NTSTATUS Status;

    if (!BufferLength || SecurityId == 0)
        return STATUS_INVALID_PARAMETER;

    Status = MFT->GetFileRecord(_Secure, &SecureFile);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FindSecurityDescriptorLocation(
        this,
        SecureFile,
        SecurityId,
        &Location);
    if (!NT_SUCCESS(Status))
        goto Done;

    SdsAttribute = SecureFile->GetAttribute(
        TypeData,
        const_cast<PWSTR>(NtfsSdsName));
    if (!SdsAttribute ||
        Location.Length <
            NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE +
            NTFS_SECURITY_DESCRIPTOR_MINIMUM_SIZE)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    Required =
        Location.Length -
        NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE;
    Capacity = *BufferLength;
    *BufferLength = Required;

    FirstStatus = ValidateSdsHeaderAt(
        SecureFile,
        SdsAttribute,
        &Location,
        Location.Offset);
    if (!NT_SUCCESS(FirstStatus))
    {
        if (Location.Offset >
            ~(ULONGLONG)0 - NTFS_SDS_DUPLICATE_OFFSET)
        {
            Status = FirstStatus;
            goto Done;
        }
        DuplicateOffset =
            Location.Offset + NTFS_SDS_DUPLICATE_OFFSET;
        Status = ValidateSdsHeaderAt(
            SecureFile,
            SdsAttribute,
            &Location,
            DuplicateOffset);
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    else
    {
        DuplicateOffset = Location.Offset;
    }

    if (!Buffer || Capacity < Required)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Done;
    }

    Status = ReadAndValidateSdsDescriptor(
        SecureFile,
        SdsAttribute,
        &Location,
        DuplicateOffset,
        Buffer);
    if (!NT_SUCCESS(Status) &&
        DuplicateOffset == Location.Offset &&
        Location.Offset <=
            ~(ULONGLONG)0 - NTFS_SDS_DUPLICATE_OFFSET)
    {
        Status = ReadAndValidateSdsDescriptor(
            SecureFile,
            SdsAttribute,
            &Location,
            Location.Offset + NTFS_SDS_DUPLICATE_OFFSET,
            Buffer);
    }

Done:
    delete SecureFile;
    return Status;
}

typedef struct _NTFS_SECURE_INDEX
{
    PFileRecord SecureFile;
    PAttribute Allocation;
    PAttribute Bitmap;
    ULONG RecordSize;
    ULONGLONG AllocationUnit;
} NTFS_SECURE_INDEX, *PNTFS_SECURE_INDEX;

static NTSTATUS
LoadSecureIndex(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord SecureFile,
    _In_ PCWSTR Name,
    _In_ ULONG CollationRule,
    _Out_ PIndexNodeHeader* RootHeader,
    _Out_ PULONG RootHeaderBytes,
    _Out_ PNTFS_SECURE_INDEX Index)
{
    PAttribute RootAttribute;
    PIndexRootEx Root;

    RootAttribute = SecureFile->GetAttribute(
        TypeIndexRoot,
        const_cast<PWSTR>(Name));
    if (!RootAttribute)
        return STATUS_NOT_IMPLEMENTED;
    if (RootAttribute->IsNonResident ||
        RootAttribute->Resident.DataLength <
            FIELD_OFFSET(IndexRootEx, Header) +
                sizeof(IndexNodeHeader))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Root = reinterpret_cast<PIndexRootEx>(
        GetResidentDataPointer(RootAttribute));
    Index->SecureFile = SecureFile;
    Index->RecordSize = Root->BytesPerIndexRec;
    if (Root->AttributeType != 0 ||
        Root->CollationRule != CollationRule ||
        Index->RecordSize != BytesPerIndexRecord(DiskVolume) ||
        Index->RecordSize < sizeof(IndexBuffer) ||
        Index->RecordSize % DiskVolume->BytesPerSector != 0 ||
        Root->ClusPerIndexRec !=
            (UCHAR)DiskVolume->ClustersPerIndexRecord)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Index->AllocationUnit =
        Index->RecordSize < BytesPerCluster(DiskVolume)
            ? DiskVolume->BytesPerSector
            : BytesPerCluster(DiskVolume);
    Index->Allocation = SecureFile->GetAttribute(
        TypeIndexAllocation,
        const_cast<PWSTR>(Name));
    Index->Bitmap = SecureFile->GetAttribute(
        TypeBitmap,
        const_cast<PWSTR>(Name));
    *RootHeader = &Root->Header;
    *RootHeaderBytes =
        RootAttribute->Resident.DataLength -
        FIELD_OFFSET(IndexRootEx, Header);
    return STATUS_SUCCESS;
}

static NTSTATUS
ReadSecureIndexNode(
    _In_ PVolume DiskVolume,
    _In_ PNTFS_SECURE_INDEX Index,
    _In_ ULONGLONG Vcn,
    _Out_writes_bytes_(Index->RecordSize) PUCHAR Buffer)
{
    PIndexBuffer NodeBuffer;
    ULONGLONG AllocationOffset;
    ULONGLONG RecordNumber;
    ULONG BytesRemaining;
    UCHAR BitmapValue;
    NTSTATUS Status;

    if (!Index->Allocation ||
        !Index->Allocation->IsNonResident ||
        !Index->Bitmap ||
        Vcn > ~(ULONGLONG)0 / Index->AllocationUnit)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    AllocationOffset = Vcn * Index->AllocationUnit;
    if (AllocationOffset % Index->RecordSize != 0)
        return STATUS_FILE_CORRUPT_ERROR;
    RecordNumber = AllocationOffset / Index->RecordSize;
    if ((RecordNumber >> 3) >=
        GetAttributeDataSize(Index->Bitmap))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    BytesRemaining = sizeof(BitmapValue);
    Status = Index->SecureFile->CopyData(
        Index->Bitmap,
        &BitmapValue,
        &BytesRemaining,
        RecordNumber >> 3);
    if (!NT_SUCCESS(Status))
        return Status;
    if (BytesRemaining != 0 ||
        !(BitmapValue & (1u << (RecordNumber & 7))))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    BytesRemaining = Index->RecordSize;
    Status = Index->SecureFile->CopyData(
        Index->Allocation,
        Buffer,
        &BytesRemaining,
        AllocationOffset);
    if (!NT_SUCCESS(Status))
        return Status;
    if (BytesRemaining != 0)
        return STATUS_END_OF_FILE;

    NodeBuffer = reinterpret_cast<PIndexBuffer>(Buffer);
    Status = NtfsApplyFixup(
        &NodeBuffer->RecordHeader,
        Index->RecordSize,
        DiskVolume->BytesPerSector);
    if (!NT_SUCCESS(Status) ||
        RtlCompareMemory(NodeBuffer->RecordHeader.TypeID,
                         "INDX",
                         4) != 4 ||
        NodeBuffer->VCN != Vcn)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    return STATUS_SUCCESS;
}

static BOOLEAN
IsSecureIndexEntryValid(
    _In_ PIndexEntry Entry,
    _In_ ULONG Remaining,
    _In_ ULONG KeyLength,
    _Out_ PULONG EffectiveLength)
{
    ULONG Length;

    if (Remaining < FIELD_OFFSET(IndexEntry, IndexStream) ||
        Entry->EntryLength < FIELD_OFFSET(IndexEntry, IndexStream) ||
        (Entry->EntryLength & (sizeof(ULONGLONG) - 1)) != 0 ||
        Entry->EntryLength > Remaining ||
        (Entry->Flags & ~(INDEX_ENTRY_NODE | INDEX_ENTRY_END)) != 0)
    {
        return FALSE;
    }

    Length = Entry->EntryLength;
    if (Entry->Flags & INDEX_ENTRY_NODE)
    {
        if (Length < FIELD_OFFSET(IndexEntry, IndexStream) +
                         sizeof(ULONGLONG))
        {
            return FALSE;
        }
        Length -= sizeof(ULONGLONG);
    }
    *EffectiveLength = Length;

    if (Entry->Flags & INDEX_ENTRY_END)
    {
        return Entry->StreamLength == 0 &&
               Entry->Data.ViewIndex.DataLength == 0;
    }
    return Entry->StreamLength == KeyLength &&
           Entry->Data.ViewIndex.DataLength ==
               NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE &&
           Entry->Data.ViewIndex.DataOffset >=
               FIELD_OFFSET(IndexEntry, IndexStream) + KeyLength &&
           Entry->Data.ViewIndex.DataOffset <= Length &&
           Entry->Data.ViewIndex.DataLength <=
               Length - Entry->Data.ViewIndex.DataOffset;
}

static NTSTATUS
FindSecurityByHashInNode(
    _In_ PVolume DiskVolume,
    _In_ PNTFS_SECURE_INDEX Index,
    _In_ PAttribute SdsAttribute,
    _In_ PIndexNodeHeader Header,
    _In_ ULONG HeaderBytes,
    _In_ ULONG Hash,
    _In_reads_bytes_(DescriptorLength) const UCHAR* Descriptor,
    _In_ ULONG DescriptorLength,
    _Inout_updates_bytes_(DescriptorLength) PUCHAR Scratch,
    _In_ ULONG Depth,
    _Out_ PULONG SecurityId)
{
    PIndexEntry Entry;
    PUCHAR NodeData = NULL;
    ULONG_PTR End;
    NTSTATUS Status = STATUS_SUCCESS;

    *SecurityId = 0;
    if (Depth > 32 ||
        HeaderBytes < sizeof(*Header) ||
        Header->IndexOffset < sizeof(*Header) ||
        Header->IndexOffset > Header->TotalIndexSize ||
        Header->TotalIndexSize > HeaderBytes)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Entry = reinterpret_cast<PIndexEntry>(
        reinterpret_cast<PUCHAR>(Header) + Header->IndexOffset);
    End = reinterpret_cast<ULONG_PTR>(Header) + Header->TotalIndexSize;

    while (reinterpret_cast<ULONG_PTR>(Entry) < End)
    {
        NTFS_SECURITY_LOCATION Location;
        ULONG EffectiveLength;
        ULONG EntryHash = 0;

        if (!IsSecureIndexEntryValid(
                Entry,
                (ULONG)(End - reinterpret_cast<ULONG_PTR>(Entry)),
                2 * sizeof(ULONG),
                &EffectiveLength))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        if (!(Entry->Flags & INDEX_ENTRY_END))
            EntryHash = ReadUnalignedU32(Entry->IndexStream);

        if ((Entry->Flags & INDEX_ENTRY_NODE) &&
            ((Entry->Flags & INDEX_ENTRY_END) || EntryHash >= Hash))
        {
            PIndexBuffer Node;

            if (!NodeData)
            {
                NodeData = new(PagedPool, TAG_NTFS) UCHAR[Index->RecordSize];
                if (!NodeData)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto Done;
                }
            }
            Status = ReadSecureIndexNode(
                DiskVolume,
                Index,
                ReadUnalignedU64(reinterpret_cast<PUCHAR>(Entry) +
                                 Entry->EntryLength - sizeof(ULONGLONG)),
                NodeData);
            if (!NT_SUCCESS(Status))
                goto Done;
            Node = reinterpret_cast<PIndexBuffer>(NodeData);
            Status = FindSecurityByHashInNode(
                DiskVolume,
                Index,
                SdsAttribute,
                &Node->IndexHeader,
                Index->RecordSize - FIELD_OFFSET(IndexBuffer, IndexHeader),
                Hash,
                Descriptor,
                DescriptorLength,
                Scratch,
                Depth + 1,
                SecurityId);
            if (!NT_SUCCESS(Status) || *SecurityId != 0)
                goto Done;
        }

        if ((Entry->Flags & INDEX_ENTRY_END) || EntryHash > Hash)
            break;

        ReadSecurityLocation(
            reinterpret_cast<PUCHAR>(Entry) +
                Entry->Data.ViewIndex.DataOffset,
            &Location);
        if (EntryHash == Hash &&
            Location.Hash == Hash &&
            Location.Length ==
                NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE + DescriptorLength)
        {
            NTSTATUS ReadStatus;

            ReadStatus = ReadAndValidateSdsDescriptor(
                Index->SecureFile,
                SdsAttribute,
                &Location,
                Location.Offset,
                Scratch);
            if (!NT_SUCCESS(ReadStatus) &&
                Location.Offset <=
                    ~(ULONGLONG)0 - NTFS_SDS_DUPLICATE_OFFSET)
            {
                ReadStatus = ReadAndValidateSdsDescriptor(
                    Index->SecureFile,
                    SdsAttribute,
                    &Location,
                    Location.Offset + NTFS_SDS_DUPLICATE_OFFSET,
                    Scratch);
            }
            if (NT_SUCCESS(ReadStatus) &&
                RtlCompareMemory(Scratch,
                                 Descriptor,
                                 DescriptorLength) == DescriptorLength)
            {
                *SecurityId = Location.SecurityId;
                goto Done;
            }
        }

        Entry = reinterpret_cast<PIndexEntry>(
            reinterpret_cast<PUCHAR>(Entry) + Entry->EntryLength);
    }

Done:
    delete[] NodeData;
    return Status;
}

static NTSTATUS
FindLastSecurityLocation(
    _In_ PVolume DiskVolume,
    _In_ PNTFS_SECURE_INDEX Index,
    _In_ PIndexNodeHeader RootHeader,
    _In_ ULONG RootHeaderBytes,
    _Out_ PNTFS_SECURITY_LOCATION Last,
    _Out_ PBOOLEAN Found)
{
    PIndexNodeHeader Header = RootHeader;
    ULONG HeaderBytes = RootHeaderBytes;
    PUCHAR NodeData = NULL;
    ULONG Depth;
    NTSTATUS Status = STATUS_SUCCESS;

    RtlZeroMemory(Last, sizeof(*Last));
    *Found = FALSE;

    for (Depth = 0; ; Depth++)
    {
        PIndexEntry Entry;
        ULONG_PTR End;
        BOOLEAN Descend = FALSE;
        ULONGLONG ChildVcn = 0;

        if (Depth > 32 ||
            HeaderBytes < sizeof(*Header) ||
            Header->IndexOffset < sizeof(*Header) ||
            Header->IndexOffset > Header->TotalIndexSize ||
            Header->TotalIndexSize > HeaderBytes)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }

        Entry = reinterpret_cast<PIndexEntry>(
            reinterpret_cast<PUCHAR>(Header) + Header->IndexOffset);
        End = reinterpret_cast<ULONG_PTR>(Header) + Header->TotalIndexSize;
        while (reinterpret_cast<ULONG_PTR>(Entry) < End)
        {
            ULONG EffectiveLength;

            if (!IsSecureIndexEntryValid(
                    Entry,
                    (ULONG)(End - reinterpret_cast<ULONG_PTR>(Entry)),
                    sizeof(ULONG),
                    &EffectiveLength))
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
            if (Entry->Flags & INDEX_ENTRY_END)
            {
                if (Entry->Flags & INDEX_ENTRY_NODE)
                {
                    ChildVcn = ReadUnalignedU64(
                        reinterpret_cast<PUCHAR>(Entry) +
                        Entry->EntryLength - sizeof(ULONGLONG));
                    Descend = TRUE;
                }
                break;
            }
            ReadSecurityLocation(
                reinterpret_cast<PUCHAR>(Entry) +
                    Entry->Data.ViewIndex.DataOffset,
                Last);
            *Found = TRUE;
            Entry = reinterpret_cast<PIndexEntry>(
                reinterpret_cast<PUCHAR>(Entry) + Entry->EntryLength);
        }
        if (!Descend)
            break;

        if (!NodeData)
        {
            NodeData = new(PagedPool, TAG_NTFS) UCHAR[Index->RecordSize];
            if (!NodeData)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
        }
        Status = ReadSecureIndexNode(DiskVolume, Index, ChildVcn, NodeData);
        if (!NT_SUCCESS(Status))
            break;
        Header = &reinterpret_cast<PIndexBuffer>(NodeData)->IndexHeader;
        HeaderBytes = Index->RecordSize - FIELD_OFFSET(IndexBuffer, IndexHeader);
    }

Done:
    delete[] NodeData;
    return Status;
}

static NTSTATUS
WriteSdsRange(
    _In_ PFileRecord SecureFile,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) const UCHAR* Buffer,
    _In_ ULONG Length)
{
    static const UCHAR Zeroes[4096] = {};
    PAttribute SdsAttribute;
    LARGE_INTEGER WriteOffset;
    ULONGLONG Size;
    ULONG WriteLength;
    NTSTATUS Status;

    SdsAttribute = SecureFile->GetAttribute(
        TypeData,
        const_cast<PWSTR>(NtfsSdsName));
    if (!SdsAttribute)
        return STATUS_FILE_CORRUPT_ERROR;
    Size = GetAttributeDataSize(SdsAttribute);

    while (Size < Offset)
    {
        WriteLength = (ULONG)min(Offset - Size, (ULONGLONG)sizeof(Zeroes));
        WriteOffset.QuadPart = (LONGLONG)Size;
        Status = SecureFile->WriteFileData(
            TypeData,
            const_cast<PWSTR>(NtfsSdsName),
            const_cast<PUCHAR>(Zeroes),
            &WriteLength,
            &WriteOffset);
        if (!NT_SUCCESS(Status))
            return Status;
        Size += WriteLength;
    }

    WriteLength = Length;
    WriteOffset.QuadPart = (LONGLONG)Offset;
    Status = SecureFile->WriteFileData(
        TypeData,
        const_cast<PWSTR>(NtfsSdsName),
        const_cast<PUCHAR>(Buffer),
        &WriteLength,
        &WriteOffset);
    if (NT_SUCCESS(Status) && WriteLength != Length)
        Status = STATUS_END_OF_FILE;
    return Status;
}

static void
WriteSecurityLocation(
    _Out_writes_bytes_(NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE) PUCHAR Data,
    _In_ PNTFS_SECURITY_LOCATION Location)
{
    RtlCopyMemory(Data, &Location->Hash, sizeof(ULONG));
    RtlCopyMemory(Data + sizeof(ULONG), &Location->SecurityId, sizeof(ULONG));
    RtlCopyMemory(Data + 2 * sizeof(ULONG), &Location->Offset, sizeof(ULONGLONG));
    RtlCopyMemory(Data + 2 * sizeof(ULONG) + sizeof(ULONGLONG),
                  &Location->Length,
                  sizeof(ULONG));
}

NTSTATUS
Volume::AssignSecurityId(
    _In_reads_bytes_(DescriptorLength) const UCHAR* Descriptor,
    _In_ ULONG DescriptorLength,
    _Out_ PULONG SecurityId)
{
    const ULONG SiiEntryLength = 40;
    const ULONG SdhEntryLength = 48;
    const ULONG SdhPadding = NTFS_SDH_PADDING;
    NTFS_SECURE_INDEX SdhIndex;
    NTFS_SECURE_INDEX SiiIndex;
    NTFS_SECURITY_LOCATION Last;
    NTFS_SECURITY_LOCATION Location;
    PIndexNodeHeader SdhRoot;
    PIndexNodeHeader SiiRoot;
    PFileRecord SecureFile = NULL;
    PAttribute SdsAttribute;
    PIndexEntry SiiEntry = NULL;
    PIndexEntry SdhEntry = NULL;
    PUCHAR Scratch = NULL;
    PUCHAR Record = NULL;
    IndexSearchKey Key;
    UCHAR KeyValue[2 * sizeof(ULONG)];
    ULONGLONG SdsSize;
    ULONGLONG NextOffset;
    ULONG SdhRootBytes;
    ULONG SiiRootBytes;
    ULONG RecordLength;
    ULONG Hash;
    BOOLEAN HaveLast;
    NTSTATUS Status;

    if (!Descriptor || !SecurityId)
        return STATUS_INVALID_PARAMETER;
    *SecurityId = 0;
    if (IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if (DescriptorLength >
            NTFS_SDS_DUPLICATE_OFFSET - NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE ||
        !ValidateSecurityDescriptor(Descriptor, DescriptorLength))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Hash = SecurityDescriptorHash(Descriptor, DescriptorLength);
    RecordLength = NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE + DescriptorLength;

    Status = MFT->GetFileRecord(_Secure, &SecureFile);
    if (!NT_SUCCESS(Status))
        return Status;

    SdsAttribute = SecureFile->GetAttribute(
        TypeData,
        const_cast<PWSTR>(NtfsSdsName));
    if (!SdsAttribute)
    {
        Status = STATUS_NOT_IMPLEMENTED;
        goto Done;
    }
    Status = LoadSecureIndex(this, SecureFile, NtfsSdhName,
                             ATTRDEF_COLLATION_SEC_HASH,
                             &SdhRoot, &SdhRootBytes, &SdhIndex);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = LoadSecureIndex(this, SecureFile, NtfsSiiName,
                             ATTRDEF_COLLATION_ULONG,
                             &SiiRoot, &SiiRootBytes, &SiiIndex);
    if (!NT_SUCCESS(Status))
        goto Done;

    Scratch = new(PagedPool, TAG_NTFS) UCHAR[DescriptorLength];
    if (!Scratch)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    Status = FindSecurityByHashInNode(this, &SdhIndex, SdsAttribute,
                                      SdhRoot, SdhRootBytes, Hash,
                                      Descriptor, DescriptorLength,
                                      Scratch, 0, SecurityId);
    if (!NT_SUCCESS(Status) || *SecurityId != 0)
        goto Done;

    Status = FindLastSecurityLocation(this, &SiiIndex, SiiRoot,
                                      SiiRootBytes, &Last, &HaveLast);
    if (!NT_SUCCESS(Status))
        goto Done;

    RtlZeroMemory(&Location, sizeof(Location));
    Location.Hash = Hash;
    Location.SecurityId = NTFS_FIRST_SECURITY_ID;
    Location.Length = RecordLength;
    NextOffset = 0;
    if (HaveLast)
    {
        if (Last.SecurityId == MAXULONG ||
            Last.Offset > ~(ULONGLONG)0 - Last.Length - NTFS_SDS_ALIGNMENT)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        if (Last.SecurityId >= NTFS_FIRST_SECURITY_ID)
            Location.SecurityId = Last.SecurityId + 1;
        NextOffset = ALIGN_UP_BY(Last.Offset + Last.Length, NTFS_SDS_ALIGNMENT);
    }

    SdsSize = GetAttributeDataSize(SdsAttribute);
    if (SdsSize != 0)
    {
        ULONGLONG BlockBase = ((SdsSize - 1) / NTFS_SDS_BLOCK_SIZE) * NTFS_SDS_BLOCK_SIZE;
        ULONGLONG SizeOffset;

        if (SdsSize > BlockBase + NTFS_SDS_DUPLICATE_OFFSET)
            SizeOffset = SdsSize - NTFS_SDS_DUPLICATE_OFFSET;
        else
            SizeOffset = SdsSize;
        SizeOffset = ALIGN_UP_BY(SizeOffset, NTFS_SDS_ALIGNMENT);
        if (SizeOffset > NextOffset)
            NextOffset = SizeOffset;
    }
    if (NextOffset % NTFS_SDS_BLOCK_SIZE + RecordLength > NTFS_SDS_DUPLICATE_OFFSET)
        NextOffset = ALIGN_UP_BY(NextOffset + 1, NTFS_SDS_BLOCK_SIZE);
    Location.Offset = NextOffset;

    Record = new(PagedPool, TAG_NTFS) UCHAR[RecordLength];
    SiiEntry = reinterpret_cast<PIndexEntry>(new(PagedPool, TAG_NTFS) UCHAR[SiiEntryLength]);
    SdhEntry = reinterpret_cast<PIndexEntry>(new(PagedPool, TAG_NTFS) UCHAR[SdhEntryLength]);
    if (!Record || !SiiEntry || !SdhEntry)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    WriteSecurityLocation(Record, &Location);
    RtlCopyMemory(Record + NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE,
                  Descriptor,
                  DescriptorLength);

    Status = WriteSdsRange(SecureFile, Location.Offset, Record, RecordLength);
    if (NT_SUCCESS(Status))
    {
        Status = WriteSdsRange(SecureFile,
                               Location.Offset + NTFS_SDS_DUPLICATE_OFFSET,
                               Record,
                               RecordLength);
    }
    if (!NT_SUCCESS(Status))
        goto Done;

    RtlZeroMemory(SiiEntry, SiiEntryLength);
    SiiEntry->Data.ViewIndex.DataOffset =
        (USHORT)(FIELD_OFFSET(IndexEntry, IndexStream) + sizeof(ULONG));
    SiiEntry->Data.ViewIndex.DataLength = NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE;
    SiiEntry->EntryLength = (UINT16)SiiEntryLength;
    SiiEntry->StreamLength = sizeof(ULONG);
    RtlCopyMemory(SiiEntry->IndexStream, &Location.SecurityId, sizeof(ULONG));
    WriteSecurityLocation(reinterpret_cast<PUCHAR>(SiiEntry) +
                              SiiEntry->Data.ViewIndex.DataOffset,
                          &Location);

    RtlZeroMemory(SdhEntry, SdhEntryLength);
    SdhEntry->Data.ViewIndex.DataOffset =
        (USHORT)(FIELD_OFFSET(IndexEntry, IndexStream) + 2 * sizeof(ULONG));
    SdhEntry->Data.ViewIndex.DataLength = NTFS_SECURITY_DESCRIPTOR_HEADER_SIZE;
    SdhEntry->EntryLength = (UINT16)SdhEntryLength;
    SdhEntry->StreamLength = 2 * sizeof(ULONG);
    RtlCopyMemory(SdhEntry->IndexStream, &Location.Hash, sizeof(ULONG));
    RtlCopyMemory(SdhEntry->IndexStream + sizeof(ULONG),
                  &Location.SecurityId,
                  sizeof(ULONG));
    WriteSecurityLocation(reinterpret_cast<PUCHAR>(SdhEntry) +
                              SdhEntry->Data.ViewIndex.DataOffset,
                          &Location);
    RtlCopyMemory(reinterpret_cast<PUCHAR>(SdhEntry) + SdhEntryLength - sizeof(ULONG),
                  &SdhPadding,
                  sizeof(ULONG));

    {
        Directory SecureIndex(this);

        RtlCopyMemory(KeyValue, &Location.SecurityId, sizeof(ULONG));
        Key.CollationRule = ATTRDEF_COLLATION_ULONG;
        Key.Name = NULL;
        Key.Value = KeyValue;
        Key.ValueLength = sizeof(ULONG);
        Status = SecureIndex.AddIndexEntry(SecureFile, NtfsSiiName, 0,
                                           &Key, SiiEntry, SiiEntryLength);
        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(KeyValue, &Location.Hash, sizeof(ULONG));
            RtlCopyMemory(KeyValue + sizeof(ULONG),
                          &Location.SecurityId,
                          sizeof(ULONG));
            Key.CollationRule = ATTRDEF_COLLATION_SEC_HASH;
            Key.ValueLength = 2 * sizeof(ULONG);
            Status = SecureIndex.AddIndexEntry(SecureFile, NtfsSdhName, 0,
                                               &Key, SdhEntry, SdhEntryLength);
        }
    }
    if (NT_SUCCESS(Status))
        *SecurityId = Location.SecurityId;

Done:
    delete[] reinterpret_cast<PUCHAR>(SdhEntry);
    delete[] reinterpret_cast<PUCHAR>(SiiEntry);
    delete[] Record;
    delete[] Scratch;
    delete SecureFile;
    return Status;
}

NTSTATUS
FileRecord::ReadSecurityDescriptor(
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength)
{
    PAttribute SecurityAttribute;
    PAttribute StandardAttribute;
    PUCHAR StandardData;
    ULONG SecurityId;

    if (!BufferLength)
        return STATUS_INVALID_PARAMETER;

    SecurityAttribute = GetAttribute(
        TypeSecurityDescriptor,
        NULL);
    if (SecurityAttribute)
    {
        return ReadDirectSecurityDescriptor(
            this,
            SecurityAttribute,
            Buffer,
            BufferLength);
    }

    StandardAttribute = GetAttribute(
        TypeStandardInformation,
        NULL);
    if (!StandardAttribute ||
        StandardAttribute->IsNonResident ||
        StandardAttribute->Resident.DataOffset < 0x18 ||
        StandardAttribute->Resident.DataOffset >
            StandardAttribute->Length ||
        StandardAttribute->Resident.DataLength >
            StandardAttribute->Length -
                StandardAttribute->Resident.DataOffset ||
        StandardAttribute->Resident.DataLength <
            FIELD_OFFSET(StandardInformationEx, OwnerId))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (StandardAttribute->Resident.DataLength <
        FIELD_OFFSET(StandardInformationEx, SecurityId) +
            sizeof(ULONG))
    {
        return STATUS_NOT_FOUND;
    }

    StandardData = reinterpret_cast<PUCHAR>(
        GetResidentDataPointer(StandardAttribute));
    SecurityId = ReadUnalignedU32(
        StandardData +
        FIELD_OFFSET(StandardInformationEx, SecurityId));
    if (SecurityId == 0)
        return STATUS_NOT_FOUND;

    return DiskVolume->ReadSecurityDescriptorById(
        SecurityId,
        Buffer,
        BufferLength);
}
