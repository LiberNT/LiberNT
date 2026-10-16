/*
 * PROJECT:     LiberNT NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Cluster relocation behind FSCTL_MOVE_FILE
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define NTFS_MOVE_COPY_BYTES 0x10000

static NTSTATUS
AppendMoveRun(_Inout_ PDataRun* Head,
              _Inout_ PDataRun* Tail,
              _In_ BOOLEAN Sparse,
              _In_ ULONGLONG LCN,
              _In_ ULONGLONG Length)
{
    PDataRun Run;

    if (Length == 0)
        return STATUS_SUCCESS;

    if (*Tail &&
        (*Tail)->IsSparse == Sparse &&
        (Sparse || (*Tail)->LCN + (*Tail)->Length == LCN))
    {
        (*Tail)->Length += Length;
        return STATUS_SUCCESS;
    }

    Run = new(PagedPool, TAG_DATA_RUN) DataRun();
    if (!Run)
        return STATUS_INSUFFICIENT_RESOURCES;
    Run->NextRun = NULL;
    Run->LCN = Sparse ? 0 : LCN;
    Run->Length = Length;
    Run->IsSparse = Sparse;

    if (*Tail)
        (*Tail)->NextRun = Run;
    else
        *Head = Run;
    *Tail = Run;
    return STATUS_SUCCESS;
}

static NTSTATUS
BuildMovedRuns(_In_ PDataRun Existing,
               _In_ ULONGLONG RangeStart,
               _In_ ULONGLONG RangeEnd,
               _In_ ULONGLONG TargetLcn,
               _Out_ PDataRun* Moved,
               _Out_ PDataRun* Released)
{
    PDataRun MovedHead = NULL;
    PDataRun MovedTail = NULL;
    PDataRun ReleasedHead = NULL;
    PDataRun ReleasedTail = NULL;
    ULONGLONG Vcn = 0;
    ULONGLONG Placed = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    *Moved = NULL;
    *Released = NULL;

    for (PDataRun Run = Existing; Run; Run = Run->NextRun)
    {
        ULONGLONG RunEnd = Vcn + Run->Length;
        ULONGLONG First = Vcn > RangeStart ? Vcn : RangeStart;
        ULONGLONG Last = RunEnd < RangeEnd ? RunEnd : RangeEnd;

        if (Run->IsSparse || First >= Last)
        {
            Status = AppendMoveRun(&MovedHead, &MovedTail, Run->IsSparse,
                                   Run->LCN, Run->Length);
            if (!NT_SUCCESS(Status))
                goto Done;
            Vcn = RunEnd;
            continue;
        }

        Status = AppendMoveRun(&MovedHead, &MovedTail, FALSE,
                               Run->LCN, First - Vcn);
        if (NT_SUCCESS(Status))
        {
            Status = AppendMoveRun(&MovedHead, &MovedTail, FALSE,
                                   TargetLcn + Placed, Last - First);
        }
        if (NT_SUCCESS(Status))
        {
            Status = AppendMoveRun(&ReleasedHead, &ReleasedTail, FALSE,
                                   Run->LCN + (First - Vcn), Last - First);
        }
        if (NT_SUCCESS(Status))
        {
            Status = AppendMoveRun(&MovedHead, &MovedTail, FALSE,
                                   Run->LCN + (Last - Vcn), RunEnd - Last);
        }
        if (!NT_SUCCESS(Status))
            goto Done;
        Placed += Last - First;
        Vcn = RunEnd;
    }

    *Moved = MovedHead;
    *Released = ReleasedHead;
    MovedHead = NULL;
    ReleasedHead = NULL;

Done:
    FreeDataRun(MovedHead);
    FreeDataRun(ReleasedHead);
    return Status;
}

NTSTATUS
FileRecord::MoveClusters(_In_ AttributeType AttrType,
                         _In_opt_ PWSTR StreamName,
                         _In_ ULONGLONG StartingVcn,
                         _In_ ULONGLONG StartingLcn,
                         _In_ ULONG ClusterCount)
{
    PAttribute TargetAttribute;
    PFileRecord AttributeOwner;
    PDataRun ExistingRuns;
    PDataRun MovedRuns = NULL;
    PDataRun ReleasedRuns = NULL;
    PNonResidentMappingUpdate MappingUpdate = NULL;
    DataRun Target = {};
    PUCHAR Buffer = NULL;
    PUCHAR RecordBackup = NULL;
    PUCHAR OwnerBackup = NULL;
    ULONGLONG ClusterSize = BytesPerCluster(DiskVolume);
    ULONGLONG AllocatedClusters;
    ULONGLONG RangeStart;
    ULONGLONG RangeEnd;
    ULONGLONG Vcn = 0;
    ULONGLONG Moved = 0;
    ULONGLONG Written = 0;
    ULONGLONG Limit;
    ULONGLONG Unit = 1;
    BOOLEAN Claimed = FALSE;
    BOOLEAN Committed = FALSE;
    BOOLEAN Retried = FALSE;
    BOOLEAN ListCreated;
    NTSTATUS Status;

    if (!DiskVolume || !Header || !Data || ClusterSize == 0)
        return STATUS_INVALID_PARAMETER;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if (StartingLcn >= DiskVolume->ClustersInVolume)
        return STATUS_INVALID_PARAMETER;
    if (ClusterCount == 0)
        return STATUS_SUCCESS;

    if (Header->BaseFileRecord == 0)
    {
        switch (Header->MFTRecordNumber)
        {
            case _MFTMirr:
            case _LogFile:
            case _Bitmap:
            case _Boot:
            case _BadClus:
                return STATUS_INVALID_PARAMETER;

            case _MFT:
            {
                PMasterFileTable Mft = DiskVolume->MFT;
                ULONGLONG FixedClusters;

                if (!Mft || !Mft->MFTFile)
                    return STATUS_INVALID_DEVICE_STATE;
                FixedClusters = ((ULONGLONG)(NTFS_LAST_RESERVED_FILE_RECORD + 1) *
                                 Mft->FileRecordSize + ClusterSize - 1) / ClusterSize;
                if (AttrType == TypeData && StartingVcn < FixedClusters)
                    return STATUS_INVALID_PARAMETER;
                if (this != Mft->MFTFile)
                {
                    Status = Mft->MFTFile->MoveClusters(AttrType,
                                                        StreamName,
                                                        StartingVcn,
                                                        StartingLcn,
                                                        ClusterCount);
                    if (NT_SUCCESS(Status))
                        (void)RefreshFrom(*Mft->MFTFile);
                    return Status;
                }
                break;
            }

            default:
                break;
        }
    }

    TargetAttribute = GetAttribute(AttrType, StreamName);
    if (!TargetAttribute || !TargetAttribute->IsNonResident)
        return STATUS_SUCCESS;
    AttributeOwner = GetAttributeOwner(TargetAttribute);
    if (!AttributeOwner ||
        TargetAttribute->NonResident.FirstVCN != 0 ||
        TargetAttribute->NonResident.AllocatedSize % ClusterSize != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    if (TargetAttribute->Flags & (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
        Unit = 16;

    AllocatedClusters = TargetAttribute->NonResident.AllocatedSize / ClusterSize;
    if (StartingVcn >= AllocatedClusters)
        return STATUS_SUCCESS;
    RangeStart = StartingVcn - StartingVcn % Unit;
    RangeEnd = StartingVcn + ClusterCount;
    if (RangeEnd % Unit != 0)
        RangeEnd += Unit - RangeEnd % Unit;
    if (RangeEnd > AllocatedClusters)
        RangeEnd = AllocatedClusters;

    ExistingRuns = GetCachedDataRuns(TargetAttribute);
    if (!ExistingRuns)
        return STATUS_FILE_CORRUPT_ERROR;
    for (PDataRun Run = ExistingRuns; Run; Run = Run->NextRun)
    {
        ULONGLONG RunEnd;

        if (Run->Length == 0 ||
            Vcn > ~(ULONGLONG)0 - Run->Length ||
            (!Run->IsSparse &&
             (Run->LCN >= DiskVolume->ClustersInVolume ||
              Run->Length > DiskVolume->ClustersInVolume - Run->LCN)))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        RunEnd = Vcn + Run->Length;
        if (!Run->IsSparse && RunEnd > RangeStart && Vcn < RangeEnd)
            Moved += (RunEnd < RangeEnd ? RunEnd : RangeEnd) -
                     (Vcn > RangeStart ? Vcn : RangeStart);
        Vcn = RunEnd;
    }
    if (Vcn != AllocatedClusters)
        return STATUS_FILE_CORRUPT_ERROR;
    if (Moved == 0)
        return STATUS_SUCCESS;

    Limit = DiskVolume->AllocationEnd();
    if (Moved > Limit || StartingLcn > Limit - Moved)
        return STATUS_ALREADY_COMMITTED;

    Status = BuildMovedRuns(ExistingRuns,
                            RangeStart,
                            RangeEnd,
                            StartingLcn,
                            &MovedRuns,
                            &ReleasedRuns);
    if (!NT_SUCCESS(Status))
        goto Done;

    Target.NextRun = NULL;
    Target.LCN = StartingLcn;
    Target.Length = Moved;
    Target.IsSparse = FALSE;
    Status = DiskVolume->ClaimClusters(&Target);
    if (Status == STATUS_DISK_FULL)
        Status = STATUS_ALREADY_COMMITTED;
    if (!NT_SUCCESS(Status))
        goto Done;
    Claimed = TRUE;

    Buffer = new(PagedPool, TAG_NTFS) UCHAR[NTFS_MOVE_COPY_BYTES];
    RecordBackup = new(PagedPool, TAG_FILE_RECORD) UCHAR[RecordBufferSize];
    if (!Buffer || !RecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    for (PDataRun Run = ReleasedRuns; Run; Run = Run->NextRun)
    {
        ULONGLONG Bytes = Run->Length * ClusterSize;
        ULONGLONG Offset = 0;

        while (Offset < Bytes)
        {
            ULONG Chunk = (ULONG)(Bytes - Offset < NTFS_MOVE_COPY_BYTES
                ? Bytes - Offset
                : NTFS_MOVE_COPY_BYTES);

            Status = DiskVolume->ReadVolume(GetOffset(Run->LCN) + Offset,
                                            Chunk,
                                            Buffer);
            if (NT_SUCCESS(Status))
            {
                Status = DiskVolume->WriteVolume(GetOffset(StartingLcn) + Written,
                                                 Chunk,
                                                 Buffer);
            }
            if (!NT_SUCCESS(Status))
                goto Done;
            Offset += Chunk;
            Written += Chunk;
        }
    }

    RtlCopyMemory(RecordBackup, Data, RecordBufferSize);
    if (AttributeOwner != this)
    {
        OwnerBackup = new(PagedPool, TAG_FILE_RECORD)
            UCHAR[AttributeOwner->RecordBufferSize];
        if (!OwnerBackup)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }
        RtlCopyMemory(OwnerBackup,
                      AttributeOwner->Data,
                      AttributeOwner->RecordBufferSize);
    }

Replace:
    Status = AttributeOwner->ReplaceNonResidentMappingPairs(
        &TargetAttribute,
        MovedRuns,
        TargetAttribute->NonResident.AllocatedSize,
        TargetAttribute->NonResident.DataSize,
        TargetAttribute->NonResident.InitalizedDataSize,
        &MappingUpdate,
        NULL);
    if (Status == STATUS_BUFFER_TOO_SMALL && !Retried)
    {
        Retried = TRUE;
        AbortNonResidentMappingUpdate(&MappingUpdate);
        if (OwnerBackup)
        {
            RtlCopyMemory(AttributeOwner->Data,
                          OwnerBackup,
                          AttributeOwner->RecordBufferSize);
            AttributeOwner->Header =
                reinterpret_cast<PFileRecordHeader>(AttributeOwner->Data);
            AttributeOwner->ClearDataRunCache();
        }
        RtlCopyMemory(Data, RecordBackup, RecordBufferSize);
        Header = reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();

        TargetAttribute = GetAttribute(AttrType, StreamName);
        AttributeOwner = TargetAttribute
            ? GetAttributeOwner(TargetAttribute)
            : NULL;
        if (!TargetAttribute || !AttributeOwner)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        Status = EnsureAttributeListForMappingGrowth(AttrType,
                                                     StreamName,
                                                     &TargetAttribute,
                                                     &AttributeOwner,
                                                     &ListCreated);
        if (!NT_SUCCESS(Status))
            goto Done;
        if (!ListCreated)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            goto Done;
        }
        RtlCopyMemory(RecordBackup, Data, RecordBufferSize);
        delete[] OwnerBackup;
        OwnerBackup = NULL;
        if (AttributeOwner != this)
        {
            OwnerBackup = new(PagedPool, TAG_FILE_RECORD)
                UCHAR[AttributeOwner->RecordBufferSize];
            if (!OwnerBackup)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Done;
            }
            RtlCopyMemory(OwnerBackup,
                          AttributeOwner->Data,
                          AttributeOwner->RecordBufferSize);
        }
        goto Replace;
    }
    if (NT_SUCCESS(Status) && MappingUpdate)
    {
        Status = CommitNonResidentMappingUpdate(&MappingUpdate);
    }
    else if (NT_SUCCESS(Status))
    {
        if (AttributeOwner != this)
            Status = DiskVolume->MFT->WriteFileRecordToMFT(AttributeOwner);
        if (NT_SUCCESS(Status))
            Status = DiskVolume->MFT->WriteFileRecordToMFT(this);
    }
    if (!NT_SUCCESS(Status))
    {
        AbortNonResidentMappingUpdate(&MappingUpdate);
        if (OwnerBackup)
        {
            RtlCopyMemory(AttributeOwner->Data,
                          OwnerBackup,
                          AttributeOwner->RecordBufferSize);
            AttributeOwner->Header =
                reinterpret_cast<PFileRecordHeader>(AttributeOwner->Data);
            AttributeOwner->ClearDataRunCache();
        }
        RtlCopyMemory(Data, RecordBackup, RecordBufferSize);
        Header = reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
        goto Done;
    }

    ClearDataRunCache();
    AttributeOwner->ClearDataRunCache();
    if (DiskVolume->MFT && this == DiskVolume->MFT->MFTFile)
        DiskVolume->MFT->MFTDataAttr = GetAttribute(TypeData, NULL);
    Committed = TRUE;
    Status = DiskVolume->ReleaseClusters(ReleasedRuns);

Done:
    if (Claimed && !Committed)
        (void)DiskVolume->ReleaseClusters(&Target);
    FreeDataRun(MovedRuns);
    FreeDataRun(ReleasedRuns);
    delete[] OwnerBackup;
    delete[] RecordBackup;
    delete[] Buffer;
    return Status;
}
