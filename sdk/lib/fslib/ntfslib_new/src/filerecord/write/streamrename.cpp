/*
 * PROJECT:     LiberNT NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Rename of a file's $DATA stream
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static
ULONG
DataAttributeHeaderLength(
    _In_ PAttribute Attribute)
{
    if (!Attribute->IsNonResident)
        return 0x18;
    return (Attribute->Flags & (ATTR_COMPRESSION_MASK | ATTR_SPARSE)) ? 0x48 : 0x40;
}

static
NTSTATUS
BuildRenamedAttribute(
    _In_ PAttribute Source,
    _In_reads_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength,
    _Outptr_result_bytebuffer_(*ImageLength) PUCHAR* Image,
    _Out_ PULONG ImageLength)
{
    PAttribute Renamed;
    PUCHAR Buffer;
    ULONG HeaderLength = DataAttributeHeaderLength(Source);
    ULONG BodyOffset;
    ULONG BodyLength;
    ULONG NewBodyOffset;
    ULONG Length;

    *Image = NULL;
    *ImageLength = 0;
    BodyOffset = Source->IsNonResident ? Source->NonResident.DataRunsOffset : Source->Resident.DataOffset;
    if (BodyOffset < HeaderLength || BodyOffset > Source->Length ||
        (!Source->IsNonResident && Source->Resident.DataLength > Source->Length - BodyOffset))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    BodyLength = Source->IsNonResident ? Source->Length - BodyOffset : Source->Resident.DataLength;

    NewBodyOffset = ALIGN_UP_BY(HeaderLength + NameLength * sizeof(WCHAR), sizeof(ULONGLONG));
    Length = ALIGN_UP_BY(NewBodyOffset + BodyLength, sizeof(ULONGLONG));
    if (NewBodyOffset > MAXUSHORT)
        return STATUS_NAME_TOO_LONG;

    Buffer = new(PagedPool, TAG_NTFS) UCHAR[Length];
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Buffer, Length);
    RtlCopyMemory(Buffer, Source, HeaderLength);
    if (NameLength)
        RtlCopyMemory(Buffer + HeaderLength, Name, NameLength * sizeof(WCHAR));
    RtlCopyMemory(Buffer + NewBodyOffset, reinterpret_cast<PUCHAR>(Source) + BodyOffset, BodyLength);

    Renamed = reinterpret_cast<PAttribute>(Buffer);
    Renamed->Length = Length;
    Renamed->NameLength = (UCHAR)NameLength;
    Renamed->NameOffset = (USHORT)HeaderLength;
    if (Renamed->IsNonResident)
        Renamed->NonResident.DataRunsOffset = (USHORT)NewBodyOffset;
    else
        Renamed->Resident.DataOffset = (USHORT)NewBodyOffset;

    *Image = Buffer;
    *ImageLength = Length;
    return STATUS_SUCCESS;
}

static
void
GetDataAttributeSizes(
    _In_opt_ PAttribute Attribute,
    _Out_ PULONGLONG AllocatedSize,
    _Out_ PULONGLONG DataSize)
{
    *AllocatedSize = 0;
    *DataSize = 0;
    if (!Attribute)
        return;
    if (!Attribute->IsNonResident)
    {
        *DataSize = Attribute->Resident.DataLength;
        return;
    }
    *AllocatedSize = (Attribute->Flags & (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
        ? Attribute->NonResident.CompressedDataSize
        : Attribute->NonResident.AllocatedSize;
    *DataSize = Attribute->NonResident.DataSize;
}

NTSTATUS
FileRecord::RenameDataStream(
    _In_opt_ PWSTR OldName,
    _In_opt_ PWSTR NewName,
    _In_ BOOLEAN ReplaceIfExists)
{
    PAttribute Source;
    PAttribute Target;
    PAttribute Inserted;
    PAttribute DefaultStream;
    PDataRun TargetRuns = NULL;
    PUCHAR RecordBackup = NULL;
    PUCHAR Image = NULL;
    ULONG ImageLength = 0;
    ULONG OldNameLength = OldName ? NtfsWcsLen(OldName) : 0;
    ULONG NewNameLength = NewName ? NtfsWcsLen(NewName) : 0;
    ULONGLONG AllocatedSize;
    ULONGLONG DataSize;
    USHORT AttributeId;
    LONG Comparison;
    BOOLEAN Committed = FALSE;
    NTSTATUS Status;

    if (!DiskVolume || !Header || !Data || Header->BaseFileRecord != 0 || NewNameLength > 0xff)
        return STATUS_INVALID_PARAMETER;
    if (DiskVolume->IsReadOnly)
        return STATUS_MEDIA_WRITE_PROTECTED;
    if (!NewNameLength && (Header->Flags & FR_IS_DIRECTORY))
        return STATUS_INVALID_PARAMETER;

    if (OldNameLength == 0 && NewNameLength == 0)
        return STATUS_SUCCESS;
    if (OldNameLength && NewNameLength)
    {
        UNICODE_STRING Left;
        UNICODE_STRING Right;

        Left.Buffer = OldName;
        Left.Length = Left.MaximumLength = (USHORT)(OldNameLength * sizeof(WCHAR));
        Right.Buffer = NewName;
        Right.Length = Right.MaximumLength = (USHORT)(NewNameLength * sizeof(WCHAR));
        Status = DiskVolume->CompareFileNames(&Left, &Right, &Comparison);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Comparison == 0)
            return STATUS_SUCCESS;
    }

    /* TODO: a stream of a record with an attribute list is not renamed */
    if (FindAttributeInRecord(TypeAttributeList, NULL, NULL))
        return STATUS_NOT_IMPLEMENTED;

    Source = FindAttributeInRecord(TypeData, OldNameLength ? OldName : NULL, NULL);
    if (!Source)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    Target = FindAttributeInRecord(TypeData, NewNameLength ? NewName : NULL, NULL);
    if (Target)
    {
        if (!ReplaceIfExists)
            return STATUS_OBJECT_NAME_COLLISION;
        if (GetAttributeDataSize(Target) != 0)
            return STATUS_INVALID_PARAMETER;
        if (Target->IsNonResident)
        {
            if (Target->NonResident.FirstVCN != 0)
                return STATUS_FILE_CORRUPT_ERROR;
            if (Target->NonResident.AllocatedSize != 0)
            {
                PDataRun* Link;

                TargetRuns = FindNonResidentData(Target);
                if (!TargetRuns)
                    return STATUS_FILE_CORRUPT_ERROR;
                Link = &TargetRuns;
                while (*Link)
                {
                    PDataRun Run = *Link;

                    if (Run->IsSparse)
                    {
                        *Link = Run->NextRun;
                        delete Run;
                    }
                    else
                    {
                        Link = &Run->NextRun;
                    }
                }
            }
        }
    }

    RecordBackup = new(PagedPool, TAG_FILE_RECORD) UCHAR[RecordBufferSize];
    if (!RecordBackup)
    {
        FreeDataRun(TargetRuns);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(RecordBackup, Data, RecordBufferSize);

    Status = Target ? RemoveAttributeRecord(Target) : STATUS_SUCCESS;
    if (NT_SUCCESS(Status))
    {
        Source = FindAttributeInRecord(TypeData, OldNameLength ? OldName : NULL, NULL);
        Status = Source ? BuildRenamedAttribute(Source, NewName, NewNameLength, &Image, &ImageLength)
                        : STATUS_FILE_CORRUPT_ERROR;
    }
    if (NT_SUCCESS(Status))
    {
        /* TODO: a record without room for the longer stream name gets no attribute list here */
        if (Header->ActualSize > Header->AllocatedSize ||
            ImageLength + (OldNameLength ? 0 : 0x18) > Header->AllocatedSize - Header->ActualSize + Source->Length)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    if (NT_SUCCESS(Status))
        Status = RemoveAttributeRecord(Source);
    if (NT_SUCCESS(Status))
        Status = InsertResidentAttribute(TypeData, NewNameLength ? NewName : NULL, &Inserted);
    if (NT_SUCCESS(Status))
    {
        AttributeId = Inserted->AttributeID;
        Status = ResizeAttributeRecord(Inserted, ImageLength);
        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(Inserted, Image, ImageLength);
            Inserted->AttributeID = AttributeId;
        }
    }
    if (NT_SUCCESS(Status) && !OldNameLength)
        Status = InsertResidentAttribute(TypeData, NULL, &DefaultStream);
    if (NT_SUCCESS(Status) && (!OldNameLength || !NewNameLength))
    {
        GetDataAttributeSizes(FindAttributeInRecord(TypeData, NULL, NULL), &AllocatedSize, &DataSize);
        Status = SynchronizeFileNameSizes(AllocatedSize, DataSize);
    }
    if (NT_SUCCESS(Status))
        Status = PrepareAutomaticTimestamps(NTFS_BASIC_INFO_CHANGE_TIME, NULL);
    if (NT_SUCCESS(Status))
    {
        Status = DiskVolume->MFT->WriteFileRecordToMFT(this);
        Committed = NT_SUCCESS(Status);
    }

    if (!Committed)
    {
        RtlCopyMemory(Data, RecordBackup, RecordBufferSize);
        Header = reinterpret_cast<PFileRecordHeader>(Data);
    }
    ClearDataRunCache();
    InvalidateWofCompression();
    if (Committed && TargetRuns)
    {
        NTSTATUS ReleaseStatus = DiskVolume->ReleaseClusters(TargetRuns);

        if (!NT_SUCCESS(ReleaseStatus))
            DPRINT1("Replaced stream's clusters were not all released: 0x%lx.\n", ReleaseStatus);
    }
    FreeDataRun(TargetRuns);
    delete[] Image;
    delete[] RecordBackup;
    return Status;
}
