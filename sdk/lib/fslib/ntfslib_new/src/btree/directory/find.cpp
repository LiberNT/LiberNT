/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS directory index lookup
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static ULONG
PathElementLength(_In_ PCWSTR FileName)
{
    PCWSTR Separator = NtfsWcsChr(FileName, L'\\');

    if (!Separator)
        Separator = NtfsWcsChr(FileName, L':');

    return Separator ? (ULONG)(Separator - FileName) : NtfsWcsLen(FileName);
}

/* One node of a directory: the name's file, or the child that leads to it. */
static NTSTATUS
FindEntryInNode(
    _In_ PVolume DiskVolume,
    _In_ PUNICODE_STRING FileName,
    _In_ BOOLEAN CaseSensitive,
    _In_ PIndexNodeHeader Header,
    _In_ ULONG HeaderBytes,
    _In_opt_ PNtfsIndexEntryTable Table,
    _Out_ PULONGLONG FileReference,
    _Out_ PULONGLONG ChildVCN,
    _Out_ PBOOLEAN Descend)
{
    IndexSearchKey Key = {ATTRDEF_COLLATION_FILENAME, FileName, NULL, 0, CaseSensitive};
    ULONG Offset;
    BOOLEAN Exact;
    NTSTATUS Status;

    Status = NtfsFindIndexInsertionPoint(DiskVolume, Header, HeaderBytes, &Key, &Offset, ChildVCN, Descend, &Exact, Table);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Exact)
    {
        *FileReference = reinterpret_cast<PIndexEntry>(reinterpret_cast<PUCHAR>(Header) + Offset)->Data.Directory.IndexedFile;
        return STATUS_SUCCESS;
    }
    return *Descend ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

NTSTATUS
Directory::FindNextFile(_In_  PFileRecord File,
                        _In_  PWCHAR FileName,
                        _Out_ PULONGLONG FileReference)
{
    const ULONGLONG MaximumValue = ~(ULONGLONG)0;
    NTSTATUS Status;
    ULONG ElementLength;
    UNICODE_STRING FileNameString;
    PAttribute IndexRootAttribute;
    PIndexRootEx IndexRoot;
    PNtfsLookupSlot Slot = NULL;
    ULONG IndexRecordSize;
    ULONGLONG AllocationUnit;
    ULONGLONG ChildVCN;
    ULONGLONG VisitedVCNs[64];
    ULONG VisitedCount = 0;
    BOOLEAN Descend;

    *FileReference = 0;

    if (!File || !(File->Header->Flags & FR_IS_DIRECTORY))
        return STATUS_NOT_FOUND;
    CaseSensitive = File->IsCaseSensitiveDirectory();

    ElementLength = PathElementLength(FileName);
    if (ElementLength == 0)
        return STATUS_OBJECT_NAME_INVALID;
    if (ElementLength > MAXUSHORT / sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    FileNameString = NtfsMakeCountedUnicodeString(
        FileName,
        (USHORT)(ElementLength * sizeof(WCHAR)));

    IndexRootAttribute = File->GetAttribute(
        TypeIndexRoot,
        const_cast<PWSTR>(NtfsI30Name));
    if (!IndexRootAttribute ||
        IndexRootAttribute->IsNonResident ||
        IndexRootAttribute->Resident.DataLength <
            FIELD_OFFSET(IndexRootEx, Header) + sizeof(IndexNodeHeader))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRoot = (PIndexRootEx)GetResidentDataPointer(IndexRootAttribute);
    IndexRecordSize = BytesPerIndexRecord(DiskVolume);
    if (IndexRecordSize == 0 ||
        IndexRoot->AttributeType != TypeFileName ||
        IndexRoot->CollationRule != ATTRDEF_COLLATION_FILENAME ||
        IndexRoot->BytesPerIndexRec != IndexRecordSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = FindEntryInNode(
        DiskVolume,
        &FileNameString,
        CaseSensitive,
        &IndexRoot->Header,
        IndexRootAttribute->Resident.DataLength -
            FIELD_OFFSET(IndexRootEx, Header),
        NULL,
        FileReference,
        &ChildVCN,
        &Descend);
    if (!NT_SUCCESS(Status) || !Descend)
        return Status;

    AllocationUnit = IndexRecordSize < BytesPerCluster(DiskVolume)
        ? DiskVolume->BytesPerSector
        : BytesPerCluster(DiskVolume);
    if (AllocationUnit == 0)
        return STATUS_FILE_CORRUPT_ERROR;

    while (Descend)
    {
        PIndexBuffer NodeBuffer;

        if (VisitedCount == RTL_NUMBER_OF(VisitedVCNs) ||
            ChildVCN > MaximumValue / AllocationUnit)
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

        Status = PinIndexNode(DiskVolume, File, NtfsI30Name, IndexRecordSize, AllocationUnit, ChildVCN, &Slot);
        if (!NT_SUCCESS(Status))
            goto Done;

        NodeBuffer = (PIndexBuffer)Slot->Buffer;
        Descend = FALSE;
        Status = FindEntryInNode(
            DiskVolume,
            &FileNameString,
            CaseSensitive,
            &NodeBuffer->IndexHeader,
            IndexRecordSize - FIELD_OFFSET(IndexBuffer, IndexHeader),
            &Slot->Table,
            FileReference,
            &ChildVCN,
            &Descend);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

Done:
    if (Slot)
        DiskVolume->ReleaseLookupSlot(Slot);
    return Status;
}
