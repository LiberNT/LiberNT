/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Bounded NTFS reparse-point reading and name-surrogate parsing
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

typedef struct _NTFS_SYMLINK_REPARSE_HEADER
{
    USHORT SubstituteNameOffset;
    USHORT SubstituteNameLength;
    USHORT PrintNameOffset;
    USHORT PrintNameLength;
    ULONG Flags;
} NTFS_SYMLINK_REPARSE_HEADER;

typedef struct _NTFS_MOUNT_POINT_REPARSE_HEADER
{
    USHORT SubstituteNameOffset;
    USHORT SubstituteNameLength;
    USHORT PrintNameOffset;
    USHORT PrintNameLength;
} NTFS_MOUNT_POINT_REPARSE_HEADER;

static_assert(sizeof(NTFS_SYMLINK_REPARSE_HEADER) == 12,
              "symlink reparse header layout changed");
static_assert(sizeof(NTFS_MOUNT_POINT_REPARSE_HEADER) == 8,
              "mount-point reparse header layout changed");

NTSTATUS
NtfsValidateReparseBuffer(
    _In_reads_bytes_(BufferLength) const UCHAR* Buffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PReparsePointEx CommonHeader)
{
    const ULONG CommonHeaderSize = sizeof(ReparsePointEx);
    const ULONG GuidHeaderSize = sizeof(ThirdPartyReparsePointEx);
    GUID ZeroGuid = {};
    ReparsePointEx Header;
    ULONG ExpectedLength;
    ULONG HeaderSize;

    if (!Buffer || BufferLength < CommonHeaderSize ||
        BufferLength > NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    RtlCopyMemory(&Header, Buffer, sizeof(Header));
    if (Header.ReparseType <= 1 ||
        (Header.ReparseType & ~((UINT32)0xF000FFFF)) != 0)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    /* Third-party tags have bit 31 clear and carry a GUID after the common
     * header. Their ReparseDataLength counts only the opaque data after it.
     */
    HeaderSize = (Header.ReparseType & ((UINT32)0x80000000))
        ? CommonHeaderSize
        : GuidHeaderSize;
    if (BufferLength < HeaderSize ||
        Header.ReparseDataLength >
            NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE - HeaderSize)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    ExpectedLength = HeaderSize + Header.ReparseDataLength;
    if (ExpectedLength != BufferLength)
        return STATUS_IO_REPARSE_DATA_INVALID;
    if (!(Header.ReparseType & ((UINT32)0x80000000)) &&
        RtlCompareMemory(
            Buffer + sizeof(ReparsePointEx),
            &ZeroGuid,
            sizeof(ZeroGuid)) == sizeof(ZeroGuid))
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (CommonHeader)
        *CommonHeader = Header;
    return STATUS_SUCCESS;
}

static BOOLEAN
IsNameRangeValid(_In_ const UCHAR* PathBuffer,
                 _In_ ULONG PathBufferLength,
                 _In_ USHORT NameOffset,
                 _In_ USHORT NameLength)
{
    ULONG CharacterCount;

    if ((NameOffset & (sizeof(WCHAR) - 1)) != 0 ||
        (NameLength & (sizeof(WCHAR) - 1)) != 0 ||
        NameOffset > PathBufferLength ||
        NameLength > PathBufferLength - NameOffset)
    {
        return FALSE;
    }

    CharacterCount = NameLength / sizeof(WCHAR);
    for (ULONG Index = 0; Index < CharacterCount; Index++)
    {
        WCHAR Character;

        RtlCopyMemory(&Character,
                      PathBuffer + NameOffset + Index * sizeof(WCHAR),
                      sizeof(Character));
        if (Character == L'\0')
            return FALSE;
        if (Character >= 0xD800 && Character <= 0xDBFF)
        {
            WCHAR LowSurrogate;

            if (++Index >= CharacterCount)
                return FALSE;
            RtlCopyMemory(&LowSurrogate,
                          PathBuffer + NameOffset +
                              Index * sizeof(WCHAR),
                          sizeof(LowSurrogate));
            if (LowSurrogate < 0xDC00 || LowSurrogate > 0xDFFF)
                return FALSE;
        }
        else if (Character >= 0xDC00 && Character <= 0xDFFF)
        {
            return FALSE;
        }
    }

    return TRUE;
}

BOOLEAN
FileRecord::IsWofBacked()
{
    PAttribute Attribute = GetAttribute(TypeReparsePoint, NULL);
    UINT32 Tag = 0;
    ULONG BytesRemaining = sizeof(Tag);

    return Attribute &&
           NT_SUCCESS(CopyData(Attribute, reinterpret_cast<PUCHAR>(&Tag), &BytesRemaining, 0)) &&
           BytesRemaining == 0 &&
           Tag == NTFS_IO_REPARSE_TAG_WOF;
}

NTSTATUS
FileRecord::ReadReparsePoint(_In_opt_ PUCHAR Buffer,
                             _Inout_ PULONG BufferLength)
{
    PAttribute Attribute;
    ULONGLONG AttributeLength;
    ULONG Capacity;
    ULONG BytesRemaining;
    NTSTATUS Status;

    if (!BufferLength)
        return STATUS_INVALID_PARAMETER;

    Attribute = GetAttribute(TypeReparsePoint, NULL);
    if (!Attribute)
    {
        *BufferLength = 0;
        return STATUS_NOT_A_REPARSE_POINT;
    }

    AttributeLength = GetAttributeDataSize(Attribute);
    if (AttributeLength < sizeof(ReparsePointEx) ||
        AttributeLength > NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        *BufferLength = 0;
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    Capacity = *BufferLength;
    *BufferLength = (ULONG)AttributeLength;
    if (!Buffer || Capacity < AttributeLength)
        return STATUS_BUFFER_TOO_SMALL;

    BytesRemaining = (ULONG)AttributeLength;
    Status = CopyData(Attribute, Buffer, &BytesRemaining, 0);
    if (!NT_SUCCESS(Status))
        return Status;
    if (BytesRemaining != 0)
        return STATUS_END_OF_FILE;

    return NtfsValidateReparseBuffer(
        Buffer,
        (ULONG)AttributeLength,
        NULL);
}

extern "C" NTSTATUS
NtfsParseNameSurrogateReparsePoint(
    _In_ const UCHAR* Buffer,
    _In_ ULONG BufferLength,
    _Out_ PNtfsReparseNameView View)
{
    ReparsePointEx Common;
    const UCHAR* PathBuffer;
    ULONG PathBufferLength;
    USHORT SubstituteNameOffset;
    USHORT SubstituteNameLength;
    USHORT PrintNameOffset;
    USHORT PrintNameLength;
    ULONG Flags;
    NTSTATUS Status;

    if (!View)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(View, sizeof(*View));

    Status = NtfsValidateReparseBuffer(
        Buffer,
        BufferLength,
        &Common);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Common.ReparseType == NTFS_IO_REPARSE_TAG_SYMLINK)
    {
        NTFS_SYMLINK_REPARSE_HEADER Header;

        if (Common.ReparseDataLength < sizeof(Header))
            return STATUS_IO_REPARSE_DATA_INVALID;
        RtlCopyMemory(&Header,
                      Buffer + sizeof(ReparsePointEx),
                      sizeof(Header));
        if (Header.Flags & ~NTFS_SYMLINK_FLAG_RELATIVE)
            return STATUS_IO_REPARSE_DATA_INVALID;

        SubstituteNameOffset = Header.SubstituteNameOffset;
        SubstituteNameLength = Header.SubstituteNameLength;
        PrintNameOffset = Header.PrintNameOffset;
        PrintNameLength = Header.PrintNameLength;
        Flags = Header.Flags;
        PathBuffer = Buffer + sizeof(ReparsePointEx) + sizeof(Header);
        PathBufferLength = Common.ReparseDataLength - sizeof(Header);
    }
    else if (Common.ReparseType == NTFS_IO_REPARSE_TAG_MOUNT_POINT)
    {
        NTFS_MOUNT_POINT_REPARSE_HEADER Header;

        if (Common.ReparseDataLength < sizeof(Header))
            return STATUS_IO_REPARSE_DATA_INVALID;
        RtlCopyMemory(&Header,
                      Buffer + sizeof(ReparsePointEx),
                      sizeof(Header));

        SubstituteNameOffset = Header.SubstituteNameOffset;
        SubstituteNameLength = Header.SubstituteNameLength;
        PrintNameOffset = Header.PrintNameOffset;
        PrintNameLength = Header.PrintNameLength;
        Flags = 0;
        PathBuffer = Buffer + sizeof(ReparsePointEx) + sizeof(Header);
        PathBufferLength = Common.ReparseDataLength - sizeof(Header);
    }
    else
    {
        return STATUS_IO_REPARSE_TAG_NOT_HANDLED;
    }

    if (SubstituteNameLength == 0 ||
        !IsNameRangeValid(PathBuffer,
                          PathBufferLength,
                          SubstituteNameOffset,
                          SubstituteNameLength) ||
        !IsNameRangeValid(PathBuffer,
                          PathBufferLength,
                          PrintNameOffset,
                          PrintNameLength))
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    View->ReparseTag = Common.ReparseType;
    View->Flags = Flags;
    View->SubstituteName = PathBuffer + SubstituteNameOffset;
    View->SubstituteNameLength =
        SubstituteNameLength / sizeof(WCHAR);
    View->PrintName = PathBuffer + PrintNameOffset;
    View->PrintNameLength = PrintNameLength / sizeof(WCHAR);
    return STATUS_SUCCESS;
}

static int
NtfsCompareReparseKeys(
    _In_reads_(3) const ULONG* Left,
    _In_reads_(3) const ULONG* Right)
{
    for (ULONG Index = 0; Index < 3; Index++)
    {
        if (Left[Index] != Right[Index])
            return Left[Index] < Right[Index] ? -1 : 1;
    }
    return 0;
}

static NTSTATUS
NtfsScanReparseNode(
    _In_ PIndexNodeHeader Header,
    _In_ ULONG HeaderBytes,
    _In_reads_(3) const ULONG* Start,
    _In_ BOOLEAN Inclusive,
    _Inout_ ULONG* Best,
    _Inout_ PBOOLEAN Found)
{
    PUCHAR Base = reinterpret_cast<PUCHAR>(Header);
    ULONG Offset;

    if (HeaderBytes < sizeof(*Header) ||
        Header->IndexOffset < sizeof(*Header) ||
        Header->IndexOffset > Header->TotalIndexSize ||
        Header->TotalIndexSize > HeaderBytes)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    for (Offset = Header->IndexOffset; Offset < Header->TotalIndexSize;)
    {
        PIndexEntry Entry = reinterpret_cast<PIndexEntry>(Base + Offset);
        ULONG Remaining = Header->TotalIndexSize - Offset;
        ULONG Key[3];
        int Order;

        if (Remaining < FIELD_OFFSET(IndexEntry, IndexStream) ||
            Entry->EntryLength < FIELD_OFFSET(IndexEntry, IndexStream) ||
            Entry->EntryLength > Remaining)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        if (Entry->Flags & INDEX_ENTRY_END)
            return STATUS_SUCCESS;
        if (Entry->StreamLength != sizeof(Key) ||
            FIELD_OFFSET(IndexEntry, IndexStream) + sizeof(Key) > Entry->EntryLength)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        RtlCopyMemory(Key, Entry->IndexStream, sizeof(Key));
        Order = NtfsCompareReparseKeys(Key, Start);
        if ((Order > 0 || (Inclusive && Order == 0)) &&
            (!*Found || NtfsCompareReparseKeys(Key, Best) < 0))
        {
            RtlCopyMemory(Best, Key, sizeof(Key));
            *Found = TRUE;
        }
        Offset += Entry->EntryLength;
    }
    return STATUS_FILE_CORRUPT_ERROR;
}

NTSTATUS
NtfsNextReparseIndexEntry(
    _In_ PVolume DiskVolume,
    _In_ ULONG StartTag,
    _In_ ULONGLONG StartFileReference,
    _In_ BOOLEAN Inclusive,
    _Out_ PULONG Tag,
    _Out_ PULONGLONG FileReference)
{
    WCHAR Path[] = L"\\$Extend\\$Reparse";
    WCHAR IndexName[] = L"$R";
    PFileRecord ReparseFile = NULL;
    PAttribute RootAttribute;
    PAttribute AllocationAttribute;
    PAttribute BitmapAttribute;
    PIndexRootEx IndexRoot;
    PUCHAR RecordData = NULL;
    ULONG Start[3];
    ULONG Best[3];
    BOOLEAN Found = FALSE;
    ULONG IndexRecordSize;
    NTSTATUS Status;

    if (!DiskVolume || !DiskVolume->MFT || !Tag || !FileReference)
        return STATUS_INVALID_PARAMETER;

    Start[0] = StartTag;
    Start[1] = (ULONG)StartFileReference;
    Start[2] = (ULONG)(StartFileReference >> 32);
    Status = DiskVolume->MFT->GetFileRecordFromQuery(Path, &ReparseFile);
    if (!NT_SUCCESS(Status))
        return Status;

    RootAttribute = ReparseFile->GetAttribute(TypeIndexRoot, IndexName);
    if (!RootAttribute ||
        RootAttribute->IsNonResident ||
        RootAttribute->Resident.DataLength <
            FIELD_OFFSET(IndexRootEx, Header) + sizeof(IndexNodeHeader))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    IndexRoot = reinterpret_cast<PIndexRootEx>(GetResidentDataPointer(RootAttribute));
    IndexRecordSize = IndexRoot->BytesPerIndexRec;
    if (IndexRoot->AttributeType != 0 ||
        IndexRoot->CollationRule != ATTRDEF_COLLATION_ULONG_MULTI)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    Status = NtfsScanReparseNode(&IndexRoot->Header,
                                 RootAttribute->Resident.DataLength -
                                     FIELD_OFFSET(IndexRootEx, Header),
                                 Start,
                                 Inclusive,
                                 Best,
                                 &Found);
    if (!NT_SUCCESS(Status))
        goto Done;

    AllocationAttribute = ReparseFile->GetAttribute(TypeIndexAllocation, IndexName);
    BitmapAttribute = ReparseFile->GetAttribute(TypeBitmap, IndexName);
    if (AllocationAttribute && BitmapAttribute)
    {
        ULONGLONG BitmapLength = GetAttributeDataSize(BitmapAttribute);
        ULONGLONG Records;

        if (IndexRecordSize < sizeof(IndexBuffer) ||
            IndexRecordSize % DiskVolume->BytesPerSector != 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        RecordData = new(PagedPool, TAG_NTFS) UCHAR[IndexRecordSize];
        if (!RecordData)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }

        Records = GetAttributeDataSize(AllocationAttribute) / IndexRecordSize;
        for (ULONGLONG Record = 0; Record < Records && (Record >> 3) < BitmapLength; Record++)
        {
            PIndexBuffer Node;
            UCHAR BitmapValue;
            ULONG Bytes = sizeof(BitmapValue);

            Status = ReparseFile->CopyData(BitmapAttribute, &BitmapValue, &Bytes, Record >> 3);
            if (!NT_SUCCESS(Status) || Bytes != 0)
            {
                if (NT_SUCCESS(Status))
                    Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
            if (!(BitmapValue & (1 << (Record & 7))))
                continue;

            Bytes = IndexRecordSize;
            Status = ReparseFile->CopyData(AllocationAttribute,
                                           RecordData,
                                           &Bytes,
                                           Record * IndexRecordSize);
            if (!NT_SUCCESS(Status) || Bytes != 0)
            {
                if (NT_SUCCESS(Status))
                    Status = STATUS_END_OF_FILE;
                goto Done;
            }

            Node = reinterpret_cast<PIndexBuffer>(RecordData);
            Status = NtfsApplyFixup(&Node->RecordHeader,
                                    IndexRecordSize,
                                    DiskVolume->BytesPerSector);
            if (!NT_SUCCESS(Status) ||
                RtlCompareMemory(Node->RecordHeader.TypeID, "INDX", 4) != 4)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }

            Status = NtfsScanReparseNode(&Node->IndexHeader,
                                         IndexRecordSize -
                                             FIELD_OFFSET(IndexBuffer, IndexHeader),
                                         Start,
                                         Inclusive,
                                         Best,
                                         &Found);
            if (!NT_SUCCESS(Status))
                goto Done;
        }
    }

    if (Found)
    {
        *Tag = Best[0];
        *FileReference = ((ULONGLONG)Best[2] << 32) | Best[1];
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = STATUS_NO_MORE_FILES;
    }

Done:
    delete[] RecordData;
    delete ReparseFile;
    return Status;
}
