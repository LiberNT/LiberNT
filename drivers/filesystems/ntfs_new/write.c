/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new write APIs
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfspch.h"

/* GLOBALS *****************************************************************/

#define NTFS_CACHED_GROWTH_LIMIT     (64 * 1024 * 1024)

typedef struct _NTFS_CACHED_GROWTH
{
    LONGLONG FileSize;
    LONGLONG AllocationSize;
    ULONGLONG ResidentSize;
    BOOLEAN ResidentSizeSet;
} NTFS_CACHED_GROWTH, *PNTFS_CACHED_GROWTH;

/* FUNCTIONS ****************************************************************/

/* A non-cached write that extends the stream, or a paging write past a size still pending on disk, records its size first so the transfer can run outside the volume lock */
static
NTSTATUS
NtfsGrowForDirectWrite(_In_ PVolumeContextBlock VolCB,
                       _In_ PFileContextBlock FileCB,
                       _In_ BOOLEAN PagingIo,
                       _In_ ULONGLONG FileOffset,
                       _In_ ULONGLONG EndOffset,
                       _Out_ PULONGLONG OldDataSize,
                       _Out_ PULONGLONG OldAllocation,
                       _Out_ PBOOLEAN Grown)
{
    PAttribute Attribute;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&VolCB->MetadataResource));
    ASSERT(PagingIo || ExIsResourceAcquiredExclusiveLite(NtfsGetMainResource(FileCB)));
    ASSERT(FileCB->StreamCB != NULL);
    ASSERT(ExIsResourceAcquiredSharedLite(&FileCB->StreamCB->PagingIoResource));
    ASSERT(FileOffset < EndOffset);

    *OldDataSize = 0;
    *OldAllocation = 0;
    *Grown = FALSE;
    if (FileCB->StreamCB->DirectExtending || FileCB->StreamCB->Deleted)
        return STATUS_SUCCESS;
    if (PagingIo && EndOffset > (ULONGLONG)NtfsGetCommonFcbHeader(FileCB)->FileSize.QuadPart)
        return STATUS_SUCCESS;
    Attribute = NtfsFileRecordGetAttribute(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream);
    if (!Attribute ||
        !Attribute->IsNonResident ||
        Attribute->Flags != 0 ||
        EndOffset <= Attribute->NonResident.DataSize ||
        (FileOffset > Attribute->NonResident.InitalizedDataSize &&
         (!PagingIo || EndOffset > Attribute->NonResident.AllocatedSize)))
    {
        return STATUS_SUCCESS;
    }

    *OldDataSize = Attribute->NonResident.DataSize;
    *OldAllocation = Attribute->NonResident.AllocatedSize;
    Status = NtfsFileRecordSetWriteSize(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream, EndOffset, *OldAllocation);
    if (!NT_SUCCESS(Status))
        return Status;
    InterlockedIncrement(&VolCB->DirGeneration);
    *Grown = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
NtfsUndoDirectGrowthLocked(_In_ PVolumeContextBlock VolCB,
                           _In_ PFileContextBlock FileCB,
                           _In_ ULONGLONG OldDataSize,
                           _In_ ULONGLONG OldAllocation)
{
    ASSERT(ExIsResourceAcquiredExclusiveLite(&VolCB->MetadataResource));
    ASSERT(ExIsResourceAcquiredExclusiveLite(NtfsGetMainResource(FileCB)));

    if (FileCB->StreamCB->Deleted || !NT_SUCCESS(NtfsRefreshDirectoryRecord(VolCB, FileCB)))
        return;
    if (NT_SUCCESS(NtfsFileRecordSetWriteSize(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream, OldDataSize, OldAllocation)))
        InterlockedIncrement(&VolCB->DirGeneration);
}

static
NTSTATUS
NtfsEndDirectExtendLocked(_In_ PVolumeContextBlock VolCB,
                          _In_ PFileContextBlock FileCB,
                          _In_ PStreamContextBlock StreamCB,
                          _In_ ULONGLONG EndOffset,
                          _In_ NTSTATUS WriteStatus)
{
    PAttribute Attribute;
    ULONGLONG ValidEnd;
    NTSTATUS Status = WriteStatus;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&VolCB->MetadataResource));
    ASSERT(StreamCB->DirectExtending);
    ASSERT(ExIsResourceAcquiredSharedLite(&StreamCB->PagingIoResource));
    if (NT_SUCCESS(Status) && EndOffset && !StreamCB->Deleted)
    {
        ULONGLONG Offset;
        ULONG Size;
        Attribute = NtfsFileRecordGetAttribute(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream);
        if (Attribute && NT_SUCCESS(NtfsGetAttributeRecordRange(FileCB->FileRec, Attribute, &Offset, &Size)))
            NtfsReadAheadMetadata(VolCB, Offset, Size);
        Status = StreamCB->Deleted ? STATUS_SUCCESS : NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (NT_SUCCESS(Status) && !StreamCB->Deleted)
        {
            Attribute = NtfsFileRecordGetAttribute(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream);
            if (!Attribute || !Attribute->IsNonResident)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
            }
            else
            {
                ValidEnd = min(EndOffset, Attribute->NonResident.DataSize);
                if (ValidEnd > Attribute->NonResident.InitalizedDataSize)
                {
                    Status = NtfsFileRecordSetFileValidDataLength(FileCB->FileRec, FileCB->RequestedType,
                                                                  FileCB->RequestedStream, ValidEnd);
                }
            }
        }
    }
    StreamCB->DirectExtending = FALSE;
    KeSetEvent(&StreamCB->DirectExtendDone, IO_NO_INCREMENT, FALSE);
    return Status;
}

static
NTSTATUS
NtfsWritePagingRuns(_In_ PVolumeContextBlock VolCB, _In_ PFileContextBlock FileCB, _In_ ULONGLONG Offset,
                    _In_reads_bytes_(Length) PUCHAR Buffer, _In_ ULONG Length, _Out_ PBOOLEAN Handled)
{
    PStreamContextBlock Stream = FileCB->StreamCB;
    PAttribute Attribute;
    struct DataRun *Runs;
    ULONGLONG Valid;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&VolCB->MetadataResource));
    ASSERT(ExIsResourceAcquiredSharedLite(&Stream->PagingIoResource));
    ASSERT(!Stream->DirectExtending);
    ASSERT(!Stream->Deleted);

    *Handled = FALSE;
    Attribute = NtfsFileRecordGetAttribute(FileCB->FileRec, TypeData, FileCB->RequestedStream);
    if (!Attribute || !Attribute->IsNonResident || Attribute->Flags || Offset > Attribute->NonResident.DataSize ||
        Length > Attribute->NonResident.DataSize - Offset)
        return STATUS_SUCCESS;
    Valid = Attribute->NonResident.InitalizedDataSize;

    if (FileCB->RequestedStream || NtfsFileRecordGetAttribute(FileCB->FileRec, TypeReparsePoint, NULL))
        return STATUS_SUCCESS;

    Runs = NtfsFileRecordSnapshotRuns(FileCB->FileRec, Attribute);
    if (!Runs)
        return STATUS_SUCCESS;
    *Handled = TRUE;
    Stream->DirectExtending = TRUE;
    KeClearEvent(&Stream->DirectExtendDone);
    NtfsReleaseMetadata(VolCB);
    ASSERT(!ExIsResourceAcquiredSharedLite(&VolCB->MetadataResource));
    Status = NtfsWriteAndFreeRunSnapshot(VolCB->DiskVolume, Runs, min(Valid, Offset), Offset, Buffer, Length, NULL);
    NtfsAcquireMetadata(VolCB);
    return NtfsEndDirectExtendLocked(VolCB, FileCB, Stream, Offset + Length > Valid ? Offset + Length : 0, Status);
}

static
BOOLEAN
NtfsGrowForCachedWrite(_In_ PVolumeContextBlock VolCB,
                       _In_ PFileContextBlock FileCB,
                       _In_ PFILE_OBJECT FileObj,
                       _In_ LONGLONG EndOffset,
                       _Out_ PNTFS_CACHED_GROWTH Growth)
{
    PFSRTL_ADVANCED_FCB_HEADER Header = NtfsGetCommonFcbHeader(FileCB);
    LONGLONG Allocation = Header->AllocationSize.QuadPart;
    PAttribute DataAttribute;
    LONGLONG Target;
    NTSTATUS Status;
    BOOLEAN SizePersisted = FALSE;

    RtlZeroMemory(Growth, sizeof(*Growth));
    Growth->FileSize = Header->FileSize.QuadPart;
    Growth->AllocationSize = Allocation;

    if (EndOffset > Allocation)
    {
        Target = Allocation + ((Allocation < NTFS_CACHED_GROWTH_LIMIT) ? Allocation : NTFS_CACHED_GROWTH_LIMIT);
        if (Target < EndOffset)
            Target = EndOffset;

        NtfsAcquireMetadata(VolCB);
        Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            return FALSE;
        }
        DataAttribute = NtfsFileRecordGetAttribute(FileCB->FileRec, FileCB->RequestedType,
                                                   FileCB->RequestedStream);
        if (EndOffset <= PAGE_SIZE &&
            (Allocation == 0 || (DataAttribute && !DataAttribute->IsNonResident)))
        {
            if (DataAttribute && !DataAttribute->IsNonResident)
                Growth->ResidentSize = DataAttribute->Resident.DataLength;
            Status = NtfsFileRecordSetFileDataSize(FileCB->FileRec, FileCB->RequestedType,
                                                   FileCB->RequestedStream, EndOffset);
            SizePersisted = NT_SUCCESS(Status);
            Growth->ResidentSizeSet = SizePersisted;
        }
        else
        {
            Status = NtfsFileRecordSetFileAllocationSize(FileCB->FileRec, FileCB->RequestedType,
                                                         FileCB->RequestedStream, Target);
        }
        if (NT_SUCCESS(Status))
        {
            DataAttribute = NtfsFileRecordGetAttribute(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream);
            if (DataAttribute)
            {
                Allocation = DataAttribute->IsNonResident
                    ? (LONGLONG)NtfsAttributeGetPhysicalAllocationSize(DataAttribute)
                    : DataAttribute->Resident.DataLength;
            }
            if (!SizePersisted && Allocation > EndOffset)
                InterlockedExchange(&FileCB->StreamCB->TrimOnClose, TRUE);
            InterlockedIncrement(&VolCB->DirGeneration);
        }
        NtfsReleaseMetadata(VolCB);
        if (Allocation < EndOffset)
            return FALSE;
        if (SizePersisted)
            FileCB->WriteTimesStamped = TRUE;
    }

    ExAcquireResourceExclusiveLite(NtfsGetPagingIoResource(FileCB), TRUE);
    Header->AllocationSize.QuadPart = Allocation;
    if (Header->FileSize.QuadPart < EndOffset)
        Header->FileSize.QuadPart = EndOffset;
    FileCB->StreamCB->SizePending = !SizePersisted;
    ExReleaseResourceLite(NtfsGetPagingIoResource(FileCB));

    if (FileObj->PrivateCacheMap != NULL)
        CcSetFileSizes(FileObj, (PCC_FILE_SIZES)&Header->AllocationSize);
    FileObj->Flags |= FO_FILE_SIZE_CHANGED;
    FileCB->NotifyFilter |= FILE_NOTIFY_CHANGE_SIZE;
    return TRUE;
}

static
VOID
NtfsUndoCachedGrowth(_In_ PVolumeContextBlock VolCB,
                     _In_ PFileContextBlock FileCB,
                     _In_ PFILE_OBJECT FileObj,
                     _In_ PNTFS_CACHED_GROWTH Growth)
{
    PFSRTL_ADVANCED_FCB_HEADER Header = NtfsGetCommonFcbHeader(FileCB);
    LONGLONG Allocation = Header->AllocationSize.QuadPart;
    NTSTATUS Status;

    if (Growth->ResidentSizeSet)
    {
        NtfsAcquireMetadata(VolCB);
        Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (NT_SUCCESS(Status))
        {
            Status = NtfsFileRecordSetFileDataSize(FileCB->FileRec, FileCB->RequestedType,
                                                   FileCB->RequestedStream, Growth->ResidentSize);
            InterlockedIncrement(&VolCB->DirGeneration);
        }
        NtfsReleaseMetadata(VolCB);
        if (NT_SUCCESS(Status))
            Allocation = Growth->AllocationSize;
    }

    ExAcquireResourceExclusiveLite(NtfsGetPagingIoResource(FileCB), TRUE);
    Header->FileSize.QuadPart = Growth->FileSize;
    Header->AllocationSize.QuadPart = Allocation;
    ExReleaseResourceLite(NtfsGetPagingIoResource(FileCB));

    if (FileObj->PrivateCacheMap != NULL)
        CcSetFileSizes(FileObj, (PCC_FILE_SIZES)&Header->AllocationSize);
}

NTSTATUS
NtfsPersistPendingSize(_In_ PVolumeContextBlock VolCB,
                       _In_ PFileContextBlock FileCB, _In_ BOOLEAN LastHandle)
{
    PFSRTL_ADVANCED_FCB_HEADER Header;
    PAttribute DataAttribute;
    NTSTATUS Status = STATUS_SUCCESS;
    LONGLONG Allocation = -1;
    LONGLONG FileSize;

    BOOLEAN Trim;

    if (!FileCB->StreamCB || !FileCB->FileRec ||
        (!FileCB->StreamCB->SizePending && !(LastHandle && ReadAcquire(&FileCB->StreamCB->TrimOnClose))))
        return STATUS_SUCCESS;

    Header = NtfsGetCommonFcbHeader(FileCB);
    FileSize = Header->FileSize.QuadPart;

    ExAcquireResourceExclusiveLite(NtfsGetPagingIoResource(FileCB), TRUE);
    NtfsAcquireMetadata(VolCB);
    Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
    if (!NT_SUCCESS(Status))
    {
        NtfsReleaseMetadata(VolCB);
        ExReleaseResourceLite(NtfsGetPagingIoResource(FileCB));
        return Status;
    }
    Trim = LastHandle && !ReadAcquire(&FileCB->StreamCB->UncleanCount) && ReadAcquire(&FileCB->StreamCB->TrimOnClose);
    DataAttribute = NtfsFileRecordGetAttribute(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream);
    if (DataAttribute && DataAttribute->IsNonResident)
    {
        if (DataAttribute->NonResident.DataSize != (ULONGLONG)FileSize)
        {
            Status = NtfsFileRecordSetWriteSize(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream,
                                                (ULONGLONG)FileSize, Trim ? 0 : DataAttribute->NonResident.AllocatedSize);
        }
        else if (Trim && !DataAttribute->Flags && DataAttribute->NonResident.AllocatedSize > (ULONGLONG)FileSize &&
                 DataAttribute->NonResident.AllocatedSize - FileSize >=
                     (ULONGLONG)VolCB->BytesPerSector * NtfsVolumeGetSectorsPerCluster(VolCB->DiskVolume))
        {
            Status = NtfsFileRecordSetFileAllocationSize(FileCB->FileRec, FileCB->RequestedType,
                                                         FileCB->RequestedStream, (ULONGLONG)FileSize);
        }
        DataAttribute = NtfsFileRecordGetAttribute(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream);
        if (NT_SUCCESS(Status) && DataAttribute)
            Allocation = DataAttribute->IsNonResident ? (LONGLONG)NtfsAttributeGetPhysicalAllocationSize(DataAttribute)
                                                      : DataAttribute->Resident.DataLength;
        if (NT_SUCCESS(Status))
            InterlockedIncrement(&VolCB->DirGeneration);
    }
    else if (FileCB->StreamCB->SizePending)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
    }
    if (NT_SUCCESS(Status) && Trim)
        InterlockedExchange(&FileCB->StreamCB->TrimOnClose, FALSE);
    NtfsReleaseMetadata(VolCB);

    if (NT_SUCCESS(Status))
    {
        if (Allocation >= FileSize)
            Header->AllocationSize.QuadPart = Allocation;
        FileCB->StreamCB->SizePending = FALSE;
    }
    ExReleaseResourceLite(NtfsGetPagingIoResource(FileCB));
    return Status;
}

NTSTATUS
NtfsCommitFileRange(_In_ PVolumeContextBlock VolCB,
                    _In_ PFileContextBlock FileCB,
                    _In_ ULONGLONG FileOffset,
                    _In_ ULONGLONG Length)
{
    NtfsRetrievalExtent Extent;
    ULONGLONG ReturnedStartingVcn;
    ULONGLONG ClusterSize;
    ULONGLONG Vcn;
    ULONGLONG EndVcn;
    ULONGLONG RunEndVcn;
    ULONGLONG DiskOffset;
    ULONGLONG RunLength;
    ULONG ExtentCount;
    ULONG Chunk;
    NTSTATUS Status;

    ClusterSize = (ULONGLONG)VolCB->BytesPerSector *
                  NtfsVolumeGetSectorsPerCluster(VolCB->DiskVolume);
    if (!VolCB->StorageDevice || !ClusterSize || !FileCB->FileRec ||
        !Length || FileOffset > ~(ULONGLONG)0 - Length)
    {
        return STATUS_SUCCESS;
    }

    Vcn = FileOffset / ClusterSize;
    EndVcn = (FileOffset + Length - 1) / ClusterSize + 1;
    while (Vcn < EndVcn)
    {
        ExtentCount = 1;
        NtfsAcquireMetadata(VolCB);
        Status = NtfsFileRecordQueryRetrievalPointers(FileCB->FileRec,
                                                      FileCB->RequestedType,
                                                      FileCB->RequestedStream,
                                                      Vcn,
                                                      &ReturnedStartingVcn,
                                                      &Extent,
                                                      &ExtentCount);
        NtfsReleaseMetadata(VolCB);
        if ((!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) ||
            ExtentCount != 1 ||
            ReturnedStartingVcn > Vcn ||
            Extent.NextVcn <= Vcn)
        {
            return STATUS_SUCCESS;
        }

        RunEndVcn = min(Extent.NextVcn, EndVcn);
        if (Extent.Lcn >= 0)
        {
            DiskOffset = ((ULONGLONG)Extent.Lcn + Vcn - ReturnedStartingVcn) * ClusterSize;
            RunLength = (RunEndVcn - Vcn) * ClusterSize;
            while (RunLength != 0)
            {
                Chunk = (ULONG)min(RunLength, 0x40000000ULL);
                Status = NtfsDiskFlushRangeKm(VolCB->StorageDevice, DiskOffset, Chunk);
                if (!NT_SUCCESS(Status))
                    return Status;
                DiskOffset += Chunk;
                RunLength -= Chunk;
            }
        }
        Vcn = RunEndVcn;
    }

    return STATUS_SUCCESS;
}

static
BOOLEAN
NtfsCachedWrite(_In_ PVolumeContextBlock VolCB,
                _In_ PFileContextBlock FileCB,
                _In_ PFILE_OBJECT FileObj,
                _In_ PIRP Irp,
                _In_ PVOID Buffer,
                _In_ ULONG Length,
                _In_ PLARGE_INTEGER ByteOffset,
                _Out_ PNTSTATUS Status)
{
    PFSRTL_ADVANCED_FCB_HEADER Header = NtfsGetCommonFcbHeader(FileCB);
    BOOLEAN WriteToEof = (BOOLEAN)(ByteOffset->HighPart == -1 && ByteOffset->LowPart == FILE_WRITE_TO_END_OF_FILE);
    LARGE_INTEGER Offset = *ByteOffset;
    LONGLONG EndOffset;
    NTFS_CACHED_GROWTH Growth;
    BOOLEAN Handled = FALSE;
    BOOLEAN Extending;
    NTSTATUS TimestampStatus;

    if ((!WriteToEof && ByteOffset->QuadPart < 0) || !CcCanIWrite(FileObj, Length, TRUE, FALSE))
        return FALSE;

    KeEnterCriticalRegion();
    if (WriteToEof)
    {
        ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
        Offset.QuadPart = Header->FileSize.QuadPart;
        ASSERT(Offset.QuadPart >= 0);
    }
    else
    {
        ExAcquireResourceSharedLite(NtfsGetMainResource(FileCB), TRUE);
    }
    EndOffset = Offset.QuadPart + Length;
    Extending = EndOffset > Header->ValidDataLength.QuadPart;
    if (Extending)
    {
        if (!WriteToEof)
        {
            ExReleaseResourceLite(NtfsGetMainResource(FileCB));
            ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
        }
        ASSERT(ExIsResourceAcquiredExclusiveLite(NtfsGetMainResource(FileCB)));
        if (!FileCB->StreamCB ||
            FileCB->StreamCB->Deleted ||
            !NtfsGrowForCachedWrite(VolCB, FileCB, FileObj, EndOffset, &Growth))
        {
            ExReleaseResourceLite(NtfsGetMainResource(FileCB));
            KeLeaveCriticalRegion();
            return FALSE;
        }
    }

    if (EndOffset <= Header->ValidDataLength.QuadPart || Extending)
    {
        Handled = TRUE;
        _SEH2_TRY
        {
            if (FileObj->PrivateCacheMap == NULL)
                NtfsInitializeStreamCache(FileCB, FileObj);
            *Status = CcCopyWrite(FileObj, &Offset, Length, TRUE, Buffer) ? STATUS_SUCCESS
                                                                           : STATUS_CANT_WAIT;
        }
        _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                     EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            *Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(*Status) && Extending)
            NtfsUndoCachedGrowth(VolCB, FileCB, FileObj, &Growth);
        if (NT_SUCCESS(*Status))
        {
            if (EndOffset > Header->ValidDataLength.QuadPart)
                Header->ValidDataLength.QuadPart = EndOffset;
            FileObj->Flags |= FO_FILE_MODIFIED;
            FileCB->NotifyFilter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
            if (FileObj->Flags & FO_SYNCHRONOUS_IO)
                FileObj->CurrentByteOffset.QuadPart = Offset.QuadPart + Length;
            Irp->IoStatus.Information = Length;
        }
    }

    ExReleaseResourceLite(NtfsGetMainResource(FileCB));

    if (Handled && NT_SUCCESS(*Status) && !FileCB->WriteTimesStamped)
    {
        FileCB->WriteTimesStamped = TRUE;
        ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
        NtfsAcquireMetadata(VolCB);
        TimestampStatus = NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (NT_SUCCESS(TimestampStatus))
            TimestampStatus = NtfsFileRecordUpdateAutomaticTimestamps(
                FileCB->FileRec,
                NTFS_BASIC_INFO_LAST_WRITE_TIME | NTFS_BASIC_INFO_CHANGE_TIME);
        if (NT_SUCCESS(TimestampStatus))
            InterlockedIncrement(&VolCB->DirGeneration);
        NtfsReleaseMetadata(VolCB);
        ExReleaseResourceLite(NtfsGetMainResource(FileCB));
    }

    KeLeaveCriticalRegion();
    return Handled;
}

static
NTSTATUS
NtfsFlushWriteThroughRecord(_In_ PVolumeContextBlock VolCB, _In_ PFileContextBlock FileCB, _In_ BOOLEAN VolumeMetadata)
{
    ULONGLONG Generation, Offset = 0;
    ULONG Size = 0;
    ULONGLONG ClusterBytes = (ULONGLONG)VolCB->BytesPerSector * NtfsVolumeGetSectorsPerCluster(VolCB->DiskVolume);
    NTSTATUS Status;
    BOOLEAN WholeVolume;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&VolCB->MetadataResource));
    ASSERT(ExIsResourceAcquiredExclusiveLite(NtfsGetPagingIoResource(FileCB)));
    do
    {
        Generation = VolCB->MetadataMoveGeneration;
        WholeVolume = VolumeMetadata;
        if (!WholeVolume)
        {
            Status = NtfsFileRecordGetDiskRange(FileCB->FileRec, &Offset, &Size);
            if (!NT_SUCCESS(Status) && Status != STATUS_NOT_IMPLEMENTED)
                return Status;
            ASSERT(!NT_SUCCESS(Status) || Size != 0);
            WholeVolume = Status == STATUS_NOT_IMPLEMENTED ||
                          NtfsFileRecordGetNumber(FileCB->FileRec) < max((ULONGLONG)4, ClusterBytes / Size);
        }
        NtfsReleaseMetadata(VolCB);
        ASSERT(!ExIsResourceAcquiredSharedLite(&VolCB->MetadataResource));
        Status = WholeVolume ? NtfsDiskFlushVolumeKm(VolCB->StorageDevice)
                             : NtfsDiskFlushRangeKm(VolCB->StorageDevice, Offset, Size);
        if (NT_SUCCESS(Status))
            Status = NtfsDiskFlushMediaKm(VolCB->StorageDevice);
        NtfsAcquireMetadata(VolCB);
    } while (NT_SUCCESS(Status) && Generation != VolCB->MetadataMoveGeneration);
    return Status;
}

static
BOOLEAN
NtfsWriteUnalignedThroughLocked(_In_ PVolumeContextBlock VolCB, _In_ PFileContextBlock FileCB,
                                _In_ ULONGLONG Offset, _In_ ULONG Length, _In_reads_bytes_(Length) PUCHAR Buffer,
                                _Out_ PNTSTATUS Status)
{
    PStreamContextBlock Stream = FileCB->StreamCB;
    PAttribute Attribute;
    struct DataRun *Runs;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&VolCB->MetadataResource));
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Stream->PagingIoResource));
    *Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
    if (Stream->Deleted)
        *Status = STATUS_FILE_DELETED;
    if (!NT_SUCCESS(*Status))
        return TRUE;

    Attribute = NtfsFileRecordGetAttribute(FileCB->FileRec, TypeData, FileCB->RequestedStream);
    if (!Attribute || !Attribute->IsNonResident || Attribute->Flags ||
        Offset > Attribute->NonResident.InitalizedDataSize || Length > Attribute->NonResident.InitalizedDataSize - Offset ||
        NtfsFileRecordGetAttribute(FileCB->FileRec, TypeReparsePoint, NULL))
    {
        return FALSE;
    }

    Runs = NtfsFileRecordSnapshotRuns(FileCB->FileRec, Attribute);
    if (!Runs)
    {
        *Status = STATUS_FILE_CORRUPT_ERROR;
        return TRUE;
    }

    ASSERT(!Stream->DirectExtending);
    NtfsReleaseMetadata(VolCB);
    ASSERT(!ExIsResourceAcquiredSharedLite(&VolCB->MetadataResource));
    *Status = NtfsWriteAndFreeRunSnapshot(VolCB->DiskVolume, Runs, Offset, Offset, Buffer, Length, NtfsWriteDiskRangeKm);
    NtfsAcquireMetadata(VolCB);
    if (NT_SUCCESS(*Status))
    {
        *Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (NT_SUCCESS(*Status))
            *Status = NtfsFileRecordUpdateAutomaticTimestamps(FileCB->FileRec, NTFS_BASIC_INFO_LAST_WRITE_TIME | NTFS_BASIC_INFO_CHANGE_TIME);
        if (NT_SUCCESS(*Status) && !FileCB->RequestedStream)
            InterlockedIncrement(&VolCB->DirGeneration);
    }
    if (NT_SUCCESS(*Status))
        *Status = NtfsFlushWriteThroughRecord(VolCB, FileCB, FALSE);
    return TRUE;
}

static
BOOLEAN
NtfsWriteUnalignedThrough(_In_ PVolumeContextBlock VolCB, _In_ PFileContextBlock FileCB,
                          _In_ ULONGLONG Offset, _In_ ULONG Length, _In_reads_bytes_(Length) PUCHAR Buffer,
                          _Out_ PNTSTATUS Status)
{
    PStreamContextBlock Stream = FileCB->StreamCB;
    BOOLEAN Handled;

    ASSERT(ExIsResourceAcquiredExclusiveLite(NtfsGetMainResource(FileCB)));
    ASSERT(Length && VolCB->BytesPerSector);
    ASSERT(Offset % VolCB->BytesPerSector || Length % VolCB->BytesPerSector);
    if (!Stream || FileCB->RequestedType != TypeData ||
        Offset > (ULONGLONG)NtfsGetCommonFcbHeader(FileCB)->ValidDataLength.QuadPart ||
        Length > (ULONGLONG)NtfsGetCommonFcbHeader(FileCB)->ValidDataLength.QuadPart - Offset)
        return FALSE;

    ExAcquireResourceExclusiveLite(&Stream->PagingIoResource, TRUE);
    NtfsAcquireMetadata(VolCB);
    Handled = NtfsWriteUnalignedThroughLocked(VolCB, FileCB, Offset, Length, Buffer, Status);
    NtfsReleaseMetadata(VolCB);
    ExReleaseResourceLite(&Stream->PagingIoResource);
    return Handled;
}

static
BOOLEAN
NtfsTryDirectWrite(_In_ PVolumeContextBlock VolCB,
                   _In_ PFileContextBlock FileCB,
                   _Inout_ PIRP Irp,
                   _In_ LONGLONG FileOffset,
                   _In_ ULONG Length,
                   _Out_ PNTSTATUS Status)
{
    PAttribute Attribute;
    NtfsRetrievalExtent Extent;
    ULONGLONG ReturnedStartingVcn;
    ULONGLONG StartingVcn;
    ULONGLONG ClusterSize;
    ULONGLONG Available;
    ULONGLONG DiskOffset;
    PVOID Buffer;
    ULONG ExtentCount;
    NTSTATUS QueryStatus;
    NTSTATUS TimestampStatus;

    ASSERT(!BooleanFlagOn(Irp->Flags, IRP_PAGING_IO));
    ASSERT(ExIsResourceAcquiredExclusiveLite(NtfsGetMainResource(FileCB)));

    *Status = STATUS_SUCCESS;
    if (Length == 0 ||
        FileOffset < 0 ||
        (ULONGLONG)FileOffset > ~(ULONGLONG)0 - Length ||
        FileOffset + Length > NtfsGetCommonFcbHeader(FileCB)->ValidDataLength.QuadPart ||
        FileCB->RequestedType != TypeData ||
        !FileCB->FileRec ||
        !VolCB->StorageDevice ||
        !VolCB->BytesPerSector)
    {
        return FALSE;
    }
    if (Irp->MdlAddress)
    {
        if (Irp->MdlAddress->Next ||
            MmGetMdlByteCount(Irp->MdlAddress) < Length)
        {
            return FALSE;
        }
        Buffer = MmGetMdlVirtualAddress(Irp->MdlAddress);
    }
    else
    {
        if (Irp->AssociatedIrp.SystemBuffer || !Irp->UserBuffer)
            return FALSE;
        Buffer = Irp->UserBuffer;
    }
    if (((ULONG_PTR)Buffer & VolCB->StorageDevice->AlignmentRequirement) != 0 ||
        Length % VolCB->BytesPerSector != 0 ||
        (ULONGLONG)FileOffset % VolCB->BytesPerSector != 0)
    {
        return FALSE;
    }

    ClusterSize = (ULONGLONG)VolCB->BytesPerSector *
                  NtfsVolumeGetSectorsPerCluster(VolCB->DiskVolume);
    if (!ClusterSize)
        return FALSE;

    NtfsAcquireMetadata(VolCB);
    if (!NT_SUCCESS(NtfsRefreshDirectoryRecord(VolCB, FileCB)))
        goto Fallback;

    Attribute = NtfsFileRecordGetAttribute(FileCB->FileRec,
                                           TypeData,
                                           FileCB->RequestedStream);
    if (!Attribute ||
        !Attribute->IsNonResident ||
        (Attribute->Flags & (ATTR_COMPRESSION_MASK | ATTR_ENCRYPTED | ATTR_SPARSE)) ||
        (ULONGLONG)FileOffset + Length > Attribute->NonResident.InitalizedDataSize ||
        NtfsFileRecordGetAttribute(FileCB->FileRec, TypeReparsePoint, NULL))
    {
        goto Fallback;
    }

    StartingVcn = (ULONGLONG)FileOffset / ClusterSize;
    ExtentCount = 1;
    QueryStatus = NtfsFileRecordQueryRetrievalPointers(FileCB->FileRec,
                                                       TypeData,
                                                       FileCB->RequestedStream,
                                                       StartingVcn,
                                                       &ReturnedStartingVcn,
                                                       &Extent,
                                                       &ExtentCount);
    if ((!NT_SUCCESS(QueryStatus) && QueryStatus != STATUS_BUFFER_OVERFLOW) ||
        ExtentCount != 1 ||
        ReturnedStartingVcn > StartingVcn ||
        Extent.NextVcn <= StartingVcn ||
        Extent.Lcn < 0 ||
        Extent.NextVcn - StartingVcn > ~(ULONGLONG)0 / ClusterSize)
    {
        goto Fallback;
    }

    Available = (Extent.NextVcn - StartingVcn) * ClusterSize;
    if (Available < (ULONGLONG)FileOffset % ClusterSize ||
        Available - (ULONGLONG)FileOffset % ClusterSize < Length)
    {
        goto Fallback;
    }

    if ((ULONGLONG)Extent.Lcn > ~(ULONGLONG)0 - (StartingVcn - ReturnedStartingVcn))
        goto Fallback;
    DiskOffset = (ULONGLONG)Extent.Lcn + (StartingVcn - ReturnedStartingVcn);
    if (DiskOffset > (~(ULONGLONG)0 - ClusterSize) / ClusterSize)
        goto Fallback;
    DiskOffset = DiskOffset * ClusterSize + (ULONGLONG)FileOffset % ClusterSize;

    if (!NT_SUCCESS(NtfsDiskBeginDirectWriteKm(VolCB->StorageDevice, DiskOffset, Length)))
        goto Fallback;
    NtfsReleaseMetadata(VolCB);

    if (!Irp->MdlAddress &&
        !NT_SUCCESS(NtfsLockTransferBuffer(Irp, Length, IoReadAccess)))
    {
        return FALSE;
    }

    *Status = NtfsSubmitDirectIo(VolCB, Irp, IRP_MJ_WRITE, DiskOffset, Length);

    NtfsAcquireMetadata(VolCB);
    NtfsDiskEndDirectWriteKm(VolCB->StorageDevice, DiskOffset, Length);
    if (NT_SUCCESS(*Status))
    {
        TimestampStatus = NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (NT_SUCCESS(TimestampStatus))
        {
            TimestampStatus = NtfsFileRecordUpdateAutomaticTimestamps(
                FileCB->FileRec,
                NTFS_BASIC_INFO_LAST_WRITE_TIME | NTFS_BASIC_INFO_CHANGE_TIME);
        }
        if (NT_SUCCESS(TimestampStatus) &&
            (!FileCB->RequestedStream ||
             (NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY)))
        {
            InterlockedIncrement(&VolCB->DirGeneration);
        }
        *Status = TimestampStatus;
    }
    NtfsReleaseMetadata(VolCB);
    return TRUE;

Fallback:
    NtfsReleaseMetadata(VolCB);
    return FALSE;
}

typedef struct _NTFS_RESIDENT_WRITE
{
    PVolumeContextBlock Volume;
    PFileContextBlock File;
    ULONGLONG Offset;
    PUCHAR Buffer;
    ULONG Length;
    BOOLEAN WriteThrough;
} NTFS_RESIDENT_WRITE;

static NTSTATUS
NtfsTransferResidentWrite(_In_ void *Context, _In_ struct DataRun *Runs,
                          _In_opt_ PUCHAR Prefix, _In_ ULONG PrefixLength)
{
    NTFS_RESIDENT_WRITE *Write = Context;
    PVolumeContextBlock Volume = Write->Volume;
    NTSTATUS Status;

    ASSERT(ExIsResourceAcquiredExclusiveLite(NtfsGetMainResource(Write->File)));
    ASSERT(ExIsResourceAcquiredExclusiveLite(NtfsGetPagingIoResource(Write->File)));
    ASSERT(ExIsResourceAcquiredExclusiveLite(&Volume->MetadataResource));
    NtfsReleaseMetadata(Volume);
    ASSERT(!ExIsResourceAcquiredSharedLite(&Volume->MetadataResource));
    Status = NtfsWriteReservedRuns(Volume->DiskVolume, Runs, Prefix, PrefixLength, Write->Offset,
                                   Write->Buffer, Write->Length, NtfsWriteDiskRangeKm);
    NtfsAcquireMetadata(Volume);
    if (NT_SUCCESS(Status) && Write->WriteThrough)
        Status = NtfsFlushWriteThroughRecord(Volume, Write->File, TRUE);
    if (NT_SUCCESS(Status))
        Status = Write->File->StreamCB->Deleted ? STATUS_FILE_DELETED : NtfsRefreshDirectoryRecord(Volume, Write->File);
    return Status;
}

_Function_class_(IRP_MJ_WRITE)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdWrite(_In_ PDEVICE_OBJECT VolumeDeviceObject,
             _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Handles write requests.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-write
     */
    NTSTATUS Status;
    PIO_STACK_LOCATION IrpSp;
    PUCHAR Buffer;
    LARGE_INTEGER ByteOffset;
    ULONG Length;
    PFileContextBlock FileCB;
    PFILE_OBJECT FileObj;
    PVolumeContextBlock VolCB;
    PNtfsVolume DiskVolume;
    PNtfsFileRecord FileRec;
    AttributeType RequestedType;
    PWSTR RequestedStream;
    BOOLEAN ResourceAcquired = FALSE;
    BOOLEAN PagingIo = FALSE;
    PVOID BounceBuffer = NULL;
    BOOLEAN PagingRuns = FALSE;
    BOOLEAN ThroughRuns = FALSE;
    BOOLEAN PromotionLocked = FALSE, Promoted = FALSE;
    NTSTATUS PromotionFlushStatus = STATUS_SUCCESS;
    BOOLEAN WriteThrough;
    PEX_RUNDOWN_REF WriteRef = NULL;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    FileObj = IrpSp->FileObject;
    if (!FileObj ||
        !VolumeDeviceObject->DeviceExtension ||
        !FileObj->FsContext)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }

    FileCB = NtfsGetFileContext(FileObj);
    VolCB = (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    if (FileCB->IsVolumeOpen)
        return NtfsForwardVolumeIo(VolCB, FileCB, Irp, TRUE);

    if (BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) && NtfsPagingFileIo(VolCB, FileCB, Irp, TRUE, &Status))
        return Status;

    WriteRef = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) ? &VolCB->PagingWrites : &VolCB->FileWrites;
    if (!ExAcquireRundownProtection(WriteRef))
    {
        WriteRef = NULL;
        Status = STATUS_VOLUME_DISMOUNTED;
        goto Complete;
    }

    FileRec = FileCB->FileRec;
    if (!FileRec)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }
    if ((NtfsFileRecordGetHeader(FileRec)->Flags & FR_IS_DIRECTORY) &&
        !FileCB->RequestedStream &&
        !(Irp->Flags & IRP_PAGING_IO))
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Complete;
    }

    DiskVolume = VolCB->DiskVolume;
    if (!DiskVolume)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Complete;
    }

    if (NtfsVolumeIsReadOnly(DiskVolume))
    {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
        goto Complete;
    }

    if (!BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
        !(FileCB->DesiredAccess &
          (FILE_WRITE_DATA | FILE_APPEND_DATA)))
    {
        Status = STATUS_ACCESS_DENIED;
        goto Complete;
    }

    Buffer = (PUCHAR)GetBuffer(Irp);
    Length = IrpSp->Parameters.Write.Length;
    if (Length != 0 && !Buffer)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        goto Complete;
    }

    ByteOffset = IrpSp->Parameters.Write.ByteOffset;
    RequestedType = FileCB->RequestedType;
    RequestedStream = FileCB->RequestedStream;

    /* Only a handle granted append access *without* plain write access is
     * append-only; FILE_GENERIC_WRITE includes FILE_APPEND_DATA, so testing
     * that bit alone would redirect every ordinary write to end of file. */
    if (!BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
        (FileCB->DesiredAccess & FILE_APPEND_DATA) &&
        !(FileCB->DesiredAccess & FILE_WRITE_DATA))
    {
        ByteOffset.HighPart = -1;
        ByteOffset.LowPart = FILE_WRITE_TO_END_OF_FILE;
    }
    if (!BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
        FileCB->StreamCB &&
        !FsRtlCheckLockForWriteAccess(&FileCB->StreamCB->FileLock, Irp))
    {
        Status = STATUS_FILE_LOCK_CONFLICT;
        goto Complete;
    }

    WriteThrough = !BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
                   (BooleanFlagOn(FileObj->Flags, FO_WRITE_THROUGH) || BooleanFlagOn(IrpSp->Flags, SL_WRITE_THROUGH));
    if (!BooleanFlagOn(Irp->Flags, IRP_PAGING_IO | IRP_NOCACHE) &&
        !WriteThrough && !BooleanFlagOn(FileObj->Flags, FO_NO_INTERMEDIATE_BUFFERING) &&
        Length != 0 &&
        RequestedType == TypeData &&
        FileObj->SectionObjectPointer != NULL &&
        NtfsCachedWrite(VolCB, FileCB, FileObj, Irp, Buffer, Length, &ByteOffset, &Status))
    {
        goto Complete;
    }

    if (!BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) && Length && !Irp->AssociatedIrp.SystemBuffer)
    {
        if (!Irp->MdlAddress && Irp->RequestorMode == UserMode)
        {
            Status = NtfsLockTransferBuffer(Irp, Length, IoReadAccess);
            if (!NT_SUCCESS(Status))
                goto Complete;
        }
        if (Irp->MdlAddress)
        {
            Buffer = GetBuffer(Irp);
            if (!Buffer || Irp->MdlAddress->Next || MmGetMdlByteCount(Irp->MdlAddress) < Length)
            {
                Status = STATUS_INVALID_USER_BUFFER;
                goto Complete;
            }
        }
    }

    /* Paging I/O is serviced while a caller may already hold MainResource,
     * so it synchronizes on PagingIoResource instead. */
    PagingIo = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO);
    KeEnterCriticalRegion();
    if (PagingIo)
        ExAcquireResourceSharedLite(NtfsGetPagingIoResource(FileCB), TRUE);
    else
        ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
    ResourceAcquired = TRUE;

    ASSERT(!PagingIo || ByteOffset.QuadPart >= 0);
    if (!PagingIo && ByteOffset.HighPart == -1 && ByteOffset.LowPart == FILE_WRITE_TO_END_OF_FILE)
        ByteOffset.QuadPart = NtfsGetCommonFcbHeader(FileCB)->FileSize.QuadPart;

    if (PagingIo)
    {
        LONGLONG FileSize = NtfsGetCommonFcbHeader(FileCB)->FileSize.QuadPart;

        if (FileCB->StreamCB && FileCB->StreamCB->Deleted)
        {
            Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = Length;
            goto Complete;
        }
        if (ByteOffset.QuadPart >= FileSize)
            Length = 0;
        else if (ByteOffset.QuadPart + Length > FileSize)
            Length = (ULONG)(FileSize - ByteOffset.QuadPart);
    }
    else if (Length != 0 &&
             FileObj->SectionObjectPointer != NULL &&
             (FileObj->SectionObjectPointer->SharedCacheMap != NULL ||
              FileObj->SectionObjectPointer->DataSectionObject != NULL))
    {
        IO_STATUS_BLOCK FlushStatus;

        CcFlushCache(FileObj->SectionObjectPointer, NULL, 0, &FlushStatus);
        if (!NT_SUCCESS(FlushStatus.Status))
        {
            Status = FlushStatus.Status;
            goto Complete;
        }
    }

    if (WriteThrough && Length && ByteOffset.QuadPart >= 0 && VolCB->BytesPerSector &&
        ((ULONGLONG)ByteOffset.QuadPart % VolCB->BytesPerSector || Length % VolCB->BytesPerSector) &&
        NtfsClustersCoverCacheBlocksKm((ULONGLONG)VolCB->BytesPerSector * NtfsVolumeGetSectorsPerCluster(VolCB->DiskVolume)))
    {
        ThroughRuns = NtfsWriteUnalignedThrough(VolCB, FileCB, ByteOffset.QuadPart, Length, Buffer, &Status);
        if (ThroughRuns)
        {
            if (!NT_SUCCESS(Status))
                NtfsPurgeStreamCache(FileCB, FileObj, &ByteOffset, Length);
            goto WriteDone;
        }
    }
    if (!PagingIo &&
        NtfsTryDirectWrite(VolCB, FileCB, Irp, ByteOffset.QuadPart, Length, &Status))
    {
        goto WriteDone;
    }

    /*
     * An aligned transfer inside the stream's initialised data changes nothing
     * but the clusters it covers, so it can go to the device on its own once
     * the mapping is known. Doing that outside the volume metadata lock is
     * what keeps the rest of the volume usable while a slow device works: the
     * lock otherwise spans the whole round trip and every other caller waits.
     */
    if (Length != 0 &&
        RequestedType == TypeData &&
        (PagingIo ||
         BooleanFlagOn(Irp->Flags, IRP_NOCACHE) ||
         BooleanFlagOn(FileObj->Flags, FO_NO_INTERMEDIATE_BUFFERING)))
    {
        NTFS_DIRECT_IO_MAP DirectMap;
        ULONGLONG OldDataSize = 0, OldAllocation = 0;
        BOOLEAN DirectWrite = FALSE;
        BOOLEAN Extending = FALSE;
        BOOLEAN Grown = FALSE;
        BOOLEAN PagingShared = FALSE;
        PStreamContextBlock ExtendStream = FileCB->StreamCB;

        if (!PagingIo && ExtendStream != NULL)
        {
            ExAcquireResourceSharedLite(&ExtendStream->PagingIoResource, TRUE);
            PagingShared = TRUE;
        }
        NtfsAcquireMetadata(VolCB);
        Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (NT_SUCCESS(Status) && ExtendStream != NULL)
        {
            Status = NtfsGrowForDirectWrite(VolCB,
                                            FileCB,
                                            PagingIo,
                                            (ULONGLONG)ByteOffset.QuadPart,
                                            (ULONGLONG)ByteOffset.QuadPart + Length,
                                            &OldDataSize,
                                            &OldAllocation,
                                            &Grown);
        }
        if (NT_SUCCESS(Status))
        {
            Status = NtfsMapDirectIo(VolCB,
                                     FileCB,
                                     Irp,
                                     (ULONGLONG)ByteOffset.QuadPart,
                                     Length,
                                     IoReadAccess,
                                     ExtendStream != NULL && !ExtendStream->DirectExtending,
                                     &DirectMap,
                                     &DirectWrite);
        }
        if (NT_SUCCESS(Status) && DirectWrite && ExtendStream != NULL)
        {
            PAttribute Attribute = NtfsFileRecordGetAttribute(FileCB->FileRec,
                                                              FileCB->RequestedType,
                                                              FileCB->RequestedStream);

            ASSERT(Attribute != NULL);
            ASSERT(Attribute->IsNonResident);
            Extending = (ULONGLONG)ByteOffset.QuadPart + Length > Attribute->NonResident.InitalizedDataSize;
            if (Extending)
            {
                ASSERT(!ExtendStream->DirectExtending);
                ASSERT((ULONGLONG)ByteOffset.QuadPart <= Attribute->NonResident.InitalizedDataSize);
                ASSERT((ULONGLONG)ByteOffset.QuadPart + Length <= Attribute->NonResident.DataSize);
                ExtendStream->DirectExtending = TRUE;
                KeClearEvent(&ExtendStream->DirectExtendDone);
            }
        }
        if (Grown && !PagingIo && (!NT_SUCCESS(Status) || !DirectWrite))
        {
            NtfsUndoDirectGrowthLocked(VolCB, FileCB, OldDataSize, OldAllocation);
            Grown = FALSE;
        }
        NtfsReleaseMetadata(VolCB);
        if (PagingShared && !Extending)
        {
            ExReleaseResourceLite(&ExtendStream->PagingIoResource);
            PagingShared = FALSE;
        }

        if (DirectWrite)
        {
            if (NT_SUCCESS(Status))
            {
                /* A small write to these blocks may still be held in the
                 * library's block cache, where it would later land on top of
                 * this one. Commit the overlapping blocks first. */
                Status = NtfsFlushDirectIoMap(VolCB, &DirectMap);
                if (NT_SUCCESS(Status))
                    Status = NtfsSubmitDirectIoMap(VolCB, Irp, IRP_MJ_WRITE, &DirectMap);
                NtfsDropDirectIoMap(VolCB, &DirectMap);
            }
            if (Extending)
            {
                NtfsAcquireMetadata(VolCB);
                Status = NtfsEndDirectExtendLocked(VolCB, FileCB, ExtendStream, (ULONGLONG)ByteOffset.QuadPart + Length, Status);
                if (Grown && !PagingIo && !NT_SUCCESS(Status))
                    NtfsUndoDirectGrowthLocked(VolCB, FileCB, OldDataSize, OldAllocation);
                NtfsReleaseMetadata(VolCB);
            }
            if (PagingShared)
                ExReleaseResourceLite(&ExtendStream->PagingIoResource);
            if (!PagingIo && NT_SUCCESS(Status))
                goto WriteDone;
            if (NT_SUCCESS(Status))
            {
                FileObj->Flags |= FO_FILE_MODIFIED;
                if (!PagingIo && BooleanFlagOn(FileObj->Flags, FO_SYNCHRONOUS_IO))
                    FileObj->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + Length;
            }
            else
            {
                Irp->IoStatus.Information = 0;
            }
            goto Complete;
        }
    }

    /*
     * A paging write hands us the section's own pages. Passing them straight
     * through means the storage stack probes and locks pages that Mm has
     * already locked for this transfer, so copy through pool memory instead.
     */
    if (PagingIo && Length != 0)
    {
        BounceBuffer = ExAllocatePoolUninitialized(NonPagedPoolNx, Length, TAG_NTFS);
        if (BounceBuffer)
        {
            RtlCopyMemory(BounceBuffer, Buffer, Length);
            Buffer = BounceBuffer;
        }
    }

    if (!PagingIo && Length && RequestedType == TypeData && !RequestedStream && FileCB->StreamCB &&
        ByteOffset.QuadPart >= 0 && (ULONGLONG)ByteOffset.QuadPart + Length > PAGE_SIZE &&
        NtfsClustersCoverCacheBlocksKm((ULONGLONG)VolCB->BytesPerSector * NtfsVolumeGetSectorsPerCluster(DiskVolume)))
    {
        ExAcquireResourceExclusiveLite(NtfsGetPagingIoResource(FileCB), TRUE);
        PromotionLocked = TRUE;
    }
    ASSERT(!ExIsResourceAcquiredExclusiveLite(&VolCB->MetadataResource));
    NtfsAcquireMetadata(VolCB);
    /* The library zeroes the stream from its valid data length up to the write, over a direct extension still in flight */
    while (FileCB->StreamCB && FileCB->StreamCB->DirectExtending)
    {
        NtfsReleaseMetadata(VolCB);
        KeWaitForSingleObject(&FileCB->StreamCB->DirectExtendDone, Executive, KernelMode, FALSE, NULL);
        NtfsAcquireMetadata(VolCB);
    }
    if (PagingIo && FileCB->StreamCB && FileCB->StreamCB->Deleted)
    {
        NtfsReleaseMetadata(VolCB);
        if (BounceBuffer)
            ExFreePoolWithTag(BounceBuffer, TAG_NTFS);
        Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = Length;
        goto Complete;
    }

    Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
    if (NT_SUCCESS(Status) && PagingIo && Length && RequestedType == TypeData && FileCB->StreamCB)
        Status = NtfsWritePagingRuns(VolCB, FileCB, ByteOffset.QuadPart, Buffer, Length, &PagingRuns);
    if (NT_SUCCESS(Status) && PromotionLocked)
    {
        NTFS_RESIDENT_WRITE Write = {VolCB, FileCB, ByteOffset.QuadPart, Buffer, Length, WriteThrough};
        ASSERT(!FileCB->StreamCB->DirectExtending);
        Status = FileCB->StreamCB->Deleted ? STATUS_FILE_DELETED :
                 NtfsFileRecordWriteResident(FileRec, Buffer, Length, Write.Offset, NtfsTransferResidentWrite, &Write, &Promoted);
    }
    if (NT_SUCCESS(Status) && !PagingRuns && !Promoted)
        Status = NtfsFileRecordWriteFileData(FileRec,
                                             RequestedType,
                                             RequestedStream,
                                             Buffer,
                                             &Length,
                                             &ByteOffset);

    /* The library updates duplicated $FILE_NAME information through a
     * separate parent record. Invalidate parsed directory snapshots
     * before releasing MetadataResource or a later open can reuse the
     * pre-write sizes from CachedLookupParent. */
    if (NT_SUCCESS(Status) && !PagingRuns && Length != 0 &&
        ((RequestedType == TypeData && !RequestedStream) ||
         (NtfsFileRecordGetHeader(FileRec)->Flags & FR_IS_DIRECTORY)))
        InterlockedIncrement(&VolCB->DirGeneration);

    if (BounceBuffer)
    {
        ExFreePoolWithTag(BounceBuffer, TAG_NTFS);
        BounceBuffer = NULL;
    }
    if (Promoted && NT_SUCCESS(Status) && WriteThrough)
        PromotionFlushStatus = NtfsFlushWriteThroughRecord(VolCB, FileCB, TRUE);
    NtfsReleaseMetadata(VolCB);
    if (PromotionLocked)
        ExReleaseResourceLite(NtfsGetPagingIoResource(FileCB));

WriteDone:
    if (NT_SUCCESS(Status))
    {
        if (!PagingIo && !ThroughRuns)
            NtfsRefreshFileSizes(VolCB, FileCB, FileObj);
        if (!PagingIo && Length != 0 && RequestedType == TypeData)
            NtfsPurgeStreamCache(FileCB, FileObj, &ByteOffset, Length);
        FileObj->Flags |=
            FO_FILE_MODIFIED |
            FO_FILE_SIZE_CHANGED;
        if (!PagingIo)
            FileCB->NotifyFilter |= FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;

        /* FIXME: other uncached writes smaller than a block stay in the library block cache until a flush or its lazy writer */
        if (WriteThrough && !ThroughRuns && Length != 0)
        {
            Status = Promoted ? PromotionFlushStatus :
                     NtfsCommitFileRange(VolCB, FileCB, (ULONGLONG)ByteOffset.QuadPart, Length);
        }
        if (NT_SUCCESS(Status))
        {
            if (!PagingIo && (FileObj->Flags & FO_SYNCHRONOUS_IO))
                FileObj->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + Length;
            Irp->IoStatus.Information = Length;
        }
    }
    else
    {
        Irp->IoStatus.Information = 0;
    }

    ExReleaseResourceLite(PagingIo ? NtfsGetPagingIoResource(FileCB)
                                   : NtfsGetMainResource(FileCB));
    KeLeaveCriticalRegion();
    ResourceAcquired = FALSE;

Complete:
    if (ResourceAcquired)
    {
        ExReleaseResourceLite(PagingIo ? NtfsGetPagingIoResource(FileCB)
                                       : NtfsGetMainResource(FileCB));
        KeLeaveCriticalRegion();
    }

    if (!NT_SUCCESS(Status))
        Irp->IoStatus.Information = 0;
    if (WriteRef)
        ExReleaseRundownProtection(WriteRef);
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}
