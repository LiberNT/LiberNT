/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new entry point
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfspch.h"

/* GLOBALS *****************************************************************/

/* Every open of a stream shares its SECTION_OBJECT_POINTERS through the SCB. */
const BOOLEAN NtfsCachedReadsEnabled = TRUE;

/* The cache manager retains this table for the lifetime of every cache map. */
static CACHE_MANAGER_CALLBACKS NtfsCacheManagerCallbacks =
{
    NtfsAcqLazyWrite,
    NtfsRelLazyWrite,
    NtfsAcqReadAhead,
    NtfsRelReadAhead
};

VOID
NtfsInitializeStreamCache(_In_ PFileContextBlock FileCB,
                          _In_ PFILE_OBJECT FileObject)
{
    PCC_FILE_SIZES FileSizes = (PCC_FILE_SIZES)&NtfsGetCommonFcbHeader(FileCB)->AllocationSize;

    CcInitializeCacheMap(FileObject,
                         FileSizes,
                         FALSE,
                         &NtfsCacheManagerCallbacks,
                         FileCB);
    CcSetFileSizes(FileObject, FileSizes);
    CcSetReadAheadGranularity(FileObject, 0x40000);
    FileObject->Flags |= FO_CACHE_SUPPORTED;
}

/*
 * NTFS proper refreshes a file's last-access time at most once per hour; a
 * driver that rewrites the record on every read pays an exclusive lock and a
 * metadata update per I/O that Windows does not.
 */
BOOLEAN
NtfsShouldStampLastAccess(_In_ PFileContextBlock FileCB)
{
    NtfsFileBasicInformation Basic;
    LARGE_INTEGER Now;

    if (!NT_SUCCESS(NtfsFileRecordGetBasicInformation(FileCB->FileRec, &Basic)))
        return TRUE;

    KeQuerySystemTime(&Now);
    return Now.QuadPart - (LONGLONG)Basic.LastAccessTime >= 36000000000LL;
}

typedef struct _NTFS_SYNC_READ_CONTEXT
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
} NTFS_SYNC_READ_CONTEXT, *PNTFS_SYNC_READ_CONTEXT;

static
NTSTATUS
NTAPI
NtfsDirectIoCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PNTFS_SYNC_READ_CONTEXT ReadContext = (PNTFS_SYNC_READ_CONTEXT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    ReadContext->IoStatus = Irp->IoStatus;
    KeSetEvent(&ReadContext->Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* A write-through file or a non-cached write marked write-through must reach the media, so the device is told to bypass its cache */
static
BOOLEAN
NtfsIsWriteThrough(_In_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);

    if (BooleanFlagOn(Irp->Flags, IRP_PAGING_IO))
        return FALSE;
    return (IrpSp->FileObject && BooleanFlagOn(IrpSp->FileObject->Flags, FO_WRITE_THROUGH)) ||
           (BooleanFlagOn(Irp->Flags, IRP_NOCACHE) && BooleanFlagOn(IrpSp->Flags, SL_WRITE_THROUGH));
}

NTSTATUS
NtfsSubmitDirectIo(
    _In_ PVolumeContextBlock VolCB,
    _Inout_ PIRP Irp,
    _In_ UCHAR MajorFunction,
    _In_ ULONGLONG DiskOffset,
    _In_ ULONG Length)
{
    NTFS_SYNC_READ_CONTEXT ReadContext;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    KeInitializeEvent(&ReadContext.Event, NotificationEvent, FALSE);
    ReadContext.IoStatus.Status = STATUS_PENDING;
    ReadContext.IoStatus.Information = 0;

    NT_ASSERT(MajorFunction == IRP_MJ_READ || MajorFunction == IRP_MJ_WRITE);
    NT_ASSERT(Irp->MdlAddress && MmGetMdlByteCount(Irp->MdlAddress) >= Length);
    NT_ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    NT_ASSERT(VolCB->StorageDevice != NULL && VolCB->BytesPerSector != 0);
    NT_ASSERT(Length != 0 && Length % VolCB->BytesPerSector == 0);
    NT_ASSERT(DiskOffset % VolCB->BytesPerSector == 0);

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = MajorFunction;
    IoStack->MinorFunction = 0;
    IoStack->Flags = SL_OVERRIDE_VERIFY_VOLUME;
    if (MajorFunction == IRP_MJ_WRITE && NtfsIsWriteThrough(Irp))
        IoStack->Flags |= SL_WRITE_THROUGH;
    IoStack->Control = 0;
    IoStack->FileObject = NULL;
    if (MajorFunction == IRP_MJ_WRITE)
    {
        IoStack->Parameters.Write.Length = Length;
        IoStack->Parameters.Write.Key = 0;
        IoStack->Parameters.Write.ByteOffset.QuadPart = DiskOffset;
    }
    else
    {
        IoStack->Parameters.Read.Length = Length;
        IoStack->Parameters.Read.Key = 0;
        IoStack->Parameters.Read.ByteOffset.QuadPart = DiskOffset;
    }
    IoSetCompletionRoutine(Irp,
                           NtfsDirectIoCompletion,
                           &ReadContext,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(VolCB->StorageDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&ReadContext.Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    NT_ASSERT(ReadContext.IoStatus.Status != STATUS_PENDING);
    Irp->IoStatus = ReadContext.IoStatus;
    return ReadContext.IoStatus.Status;
}

static
NTSTATUS
NtfsSubmitDirectIoRun(_In_ PVolumeContextBlock VolCB,
                      _In_ PIRP MasterIrp,
                      _In_ UCHAR MajorFunction,
                      _In_ ULONG BufferOffset,
                      _In_ PNTFS_DIRECT_IO_RUN Run)
{
    NTFS_SYNC_READ_CONTEXT RunContext;
    PIO_STACK_LOCATION IoStack;
    PUCHAR RunVa;
    PMDL Mdl;
    PIRP Irp;
    NTSTATUS Status;

    ASSERT(MmGetMdlByteCount(MasterIrp->MdlAddress) >= BufferOffset + Run->Length);
    Irp = IoAllocateIrp(VolCB->StorageDevice->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    RunVa = (PUCHAR)MmGetMdlVirtualAddress(MasterIrp->MdlAddress) + BufferOffset;
    Mdl = IoAllocateMdl(RunVa, Run->Length, FALSE, FALSE, NULL);
    if (!Mdl)
    {
        IoFreeIrp(Irp);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    IoBuildPartialMdl(MasterIrp->MdlAddress, Mdl, RunVa, Run->Length);

    Irp->MdlAddress = Mdl;
    Irp->Flags = IRP_NOCACHE | (MasterIrp->Flags & (IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO));
    Irp->RequestorMode = KernelMode;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = MajorFunction;
    IoStack->Flags = SL_OVERRIDE_VERIFY_VOLUME;
    if (MajorFunction == IRP_MJ_WRITE && NtfsIsWriteThrough(MasterIrp))
        IoStack->Flags |= SL_WRITE_THROUGH;
    if (MajorFunction == IRP_MJ_WRITE)
    {
        IoStack->Parameters.Write.Length = Run->Length;
        IoStack->Parameters.Write.ByteOffset.QuadPart = Run->DiskOffset;
    }
    else
    {
        IoStack->Parameters.Read.Length = Run->Length;
        IoStack->Parameters.Read.ByteOffset.QuadPart = Run->DiskOffset;
    }
    KeInitializeEvent(&RunContext.Event, NotificationEvent, FALSE);
    RunContext.IoStatus.Status = STATUS_PENDING;
    RunContext.IoStatus.Information = 0;
    IoSetCompletionRoutine(Irp, NtfsDirectIoCompletion, &RunContext, TRUE, TRUE, TRUE);

    Status = IoCallDriver(VolCB->StorageDevice, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&RunContext.Event, Executive, KernelMode, FALSE, NULL);
    NT_ASSERT(RunContext.IoStatus.Status != STATUS_PENDING);
    Status = RunContext.IoStatus.Status;
    if (NT_SUCCESS(Status) && RunContext.IoStatus.Information != Run->Length)
        Status = STATUS_DATA_ERROR;

    MmPrepareMdlForReuse(Mdl);
    IoFreeMdl(Mdl);
    IoFreeIrp(Irp);
    return Status;
}

/* A transfer that spans several runs goes to the device one run at a time, each through its own IRP */
NTSTATUS
NtfsSubmitDirectIoMap(_In_ PVolumeContextBlock VolCB,
                      _Inout_ PIRP Irp,
                      _In_ UCHAR MajorFunction,
                      _In_ PNTFS_DIRECT_IO_MAP Map)
{
    ULONG BufferOffset = 0;
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    NT_ASSERT(MajorFunction == IRP_MJ_READ || MajorFunction == IRP_MJ_WRITE);
    NT_ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(Map->RunCount != 0 && Map->RunCount <= NTFS_DIRECT_IO_RUNS);

    if (Map->RunCount == 1)
        return NtfsSubmitDirectIo(VolCB, Irp, MajorFunction, Map->Runs[0].DiskOffset, Map->Runs[0].Length);

    for (Index = 0; Index < Map->RunCount && NT_SUCCESS(Status); Index++)
    {
        Status = NtfsSubmitDirectIoRun(VolCB, Irp, MajorFunction, BufferOffset, &Map->Runs[Index]);
        if (NT_SUCCESS(Status))
            BufferOffset += Map->Runs[Index].Length;
    }
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = NT_SUCCESS(Status) ? BufferOffset : 0;
    return Status;
}

NTSTATUS
NtfsLockTransferBuffer(
    _Inout_ PIRP Irp,
    _In_ ULONG Length,
    _In_ LOCK_OPERATION Operation)
{
    PMDL Mdl;
    NTSTATUS Status = STATUS_SUCCESS;

    NT_ASSERT(Operation == IoReadAccess || Operation == IoWriteAccess);
    NT_ASSERT(Length != 0);

    if (Irp->MdlAddress)
        return STATUS_SUCCESS;
    if (!Irp->UserBuffer)
        return STATUS_INVALID_USER_BUFFER;

    Mdl = IoAllocateMdl(Irp->UserBuffer, Length, FALSE, FALSE, Irp);
    if (!Mdl)
        return STATUS_INSUFFICIENT_RESOURCES;
    NT_ASSERT(Irp->MdlAddress == Mdl);

    _SEH2_TRY
    {
        MmProbeAndLockPages(Mdl, Irp->RequestorMode, Operation);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(Mdl);
        Irp->MdlAddress = NULL;
        if (!FsRtlIsNtstatusExpected(Status))
            Status = STATUS_INVALID_USER_BUFFER;
    }
    return Status;
}

/*
 * Work out which volume offset backs this read. The caller holds the volume
 * metadata lock for this, and releases it before the transfer: the file's own
 * resource is held shared for the whole request, so the mapping cannot move
 * under the transfer, and a volume-wide lock must not span a device round trip.
 */
NTSTATUS
NtfsMapDirectIo(
    _In_ PVolumeContextBlock VolCB,
    _In_ PFileContextBlock FileCB,
    _Inout_ PIRP Irp,
    _In_ ULONGLONG FileOffset,
    _In_ ULONG Length,
    _In_ LOCK_OPERATION Operation,
    _In_ BOOLEAN AllowPastValidData,
    _Out_ PNTFS_DIRECT_IO_MAP Map,
    _Out_ PBOOLEAN Handled)
{
    PAttribute Attribute;
    NtfsRetrievalExtent Extent;
    ULONGLONG ReturnedStartingVcn;
    ULONGLONG StartingVcn;
    ULONGLONG ClusterSize;
    ULONGLONG InClusterOffset;
    ULONGLONG Available;
    ULONGLONG PhysicalCluster;
    ULONGLONG DiskOffset;
    ULONGLONG ValidDataLength;
    ULONGLONG DataSize;
    ULONG ExtentCount;
    ULONG Mapped;
    ULONG Piece;
    NTSTATUS Status;

    NT_ASSERT(ExIsResourceAcquiredSharedLite(&VolCB->MetadataResource));
    NT_ASSERT(Operation == IoReadAccess || Operation == IoWriteAccess);
    NT_ASSERT(Length != 0);

    *Handled = FALSE;
    Map->RunCount = 0;
    Map->TransferLength = Length;
    Map->ValidLength = Length;
    Map->DataLength = Length;
    if (!Irp->MdlAddress &&
        !Irp->AssociatedIrp.SystemBuffer)
    {
        Status = NtfsLockTransferBuffer(Irp, Length, Operation);
        if (!NT_SUCCESS(Status))
        {
            *Handled = TRUE;
            return Status;
        }
    }
    if (!VolCB->StorageDevice ||
        !VolCB->BytesPerSector ||
        !Irp->MdlAddress ||
        Irp->MdlAddress->Next ||
        MmGetMdlByteCount(Irp->MdlAddress) < Length ||
        ((ULONG_PTR)MmGetMdlVirtualAddress(Irp->MdlAddress) &
         VolCB->StorageDevice->AlignmentRequirement) != 0 ||
        Length % VolCB->BytesPerSector != 0 ||
        FileOffset % VolCB->BytesPerSector != 0)
    {
        return STATUS_SUCCESS;
    }

    Attribute = NtfsFileRecordGetAttribute(FileCB->FileRec,
                                           FileCB->RequestedType,
                                           FileCB->RequestedStream);
    if (!Attribute ||
        !Attribute->IsNonResident ||
        (Attribute->Flags & (ATTR_COMPRESSION_MASK | ATTR_ENCRYPTED)) ||
        FileOffset > ~(ULONGLONG)0 - Length)
    {
        return STATUS_SUCCESS;
    }
    ValidDataLength = Attribute->NonResident.InitalizedDataSize;
    DataSize = Attribute->NonResident.DataSize;
    /* The valid data length cannot pass the recorded size, so data written past it would later be zeroed over */
    if (FileOffset + Length > ValidDataLength)
    {
        if (!AllowPastValidData ||
            Attribute->Flags != 0 ||
            ValidDataLength > DataSize)
        {
            return STATUS_SUCCESS;
        }
        if (Operation == IoReadAccess)
        {
            if (FileOffset > ValidDataLength || FileOffset + Length > DataSize)
                return STATUS_SUCCESS;
        }
        else
        {
            /* Only the sectors holding valid data come from the device: what follows them reads as zeros, as the library returns it */
            if (FileOffset >= ValidDataLength)
                return STATUS_SUCCESS;
            Map->ValidLength = (ULONG)(ValidDataLength - FileOffset);
            Map->DataLength = (ULONG)min((ULONGLONG)Length, DataSize - FileOffset);
            Map->TransferLength = (Map->ValidLength + VolCB->BytesPerSector - 1) /
                                  VolCB->BytesPerSector * VolCB->BytesPerSector;
            ASSERT(Map->ValidLength < Length);
            ASSERT(Map->ValidLength <= Map->DataLength);
            ASSERT(Map->TransferLength >= Map->ValidLength);
            ASSERT(Map->TransferLength <= Length);
        }
    }

    ClusterSize = (ULONGLONG)VolCB->BytesPerSector *
                  NtfsVolumeGetSectorsPerCluster(VolCB->DiskVolume);
    if (!ClusterSize)
        return STATUS_SUCCESS;

    for (Mapped = 0; Mapped < Map->TransferLength; Mapped += Piece)
    {
        StartingVcn = (FileOffset + Mapped) / ClusterSize;
        InClusterOffset = (FileOffset + Mapped) % ClusterSize;
        ExtentCount = 1;
        Status = NtfsFileRecordQueryRetrievalPointers(FileCB->FileRec,
                                                      FileCB->RequestedType,
                                                      FileCB->RequestedStream,
                                                      StartingVcn,
                                                      &ReturnedStartingVcn,
                                                      &Extent,
                                                      &ExtentCount);
        if ((!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) ||
            ExtentCount != 1 ||
            ReturnedStartingVcn > StartingVcn ||
            Extent.NextVcn <= StartingVcn ||
            Extent.Lcn < 0)
        {
            Map->RunCount = 0;
            return STATUS_SUCCESS;
        }

        Available = Extent.NextVcn - StartingVcn;
        if (Available > ~(ULONGLONG)0 / ClusterSize)
            Available = ~(ULONGLONG)0 / ClusterSize;
        Available *= ClusterSize;
        ASSERT(Available > InClusterOffset);
        Available -= InClusterOffset;
        Piece = (ULONG)min(Available, (ULONGLONG)(Map->TransferLength - Mapped));
        ASSERT(Piece != 0);
        ASSERT(Piece % VolCB->BytesPerSector == 0);

        PhysicalCluster = StartingVcn - ReturnedStartingVcn;
        if ((ULONGLONG)Extent.Lcn > ~(ULONGLONG)0 - PhysicalCluster)
        {
            Map->RunCount = 0;
            return STATUS_SUCCESS;
        }
        PhysicalCluster += (ULONGLONG)Extent.Lcn;
        if (PhysicalCluster > (~(ULONGLONG)0 - InClusterOffset) / ClusterSize)
        {
            Map->RunCount = 0;
            return STATUS_SUCCESS;
        }
        DiskOffset = PhysicalCluster * ClusterSize + InClusterOffset;
        NT_ASSERT(DiskOffset % VolCB->BytesPerSector == 0);

        if (Map->RunCount != 0 &&
            Map->Runs[Map->RunCount - 1].DiskOffset + Map->Runs[Map->RunCount - 1].Length == DiskOffset)
        {
            Map->Runs[Map->RunCount - 1].Length += Piece;
        }
        else if (Map->RunCount < NTFS_DIRECT_IO_RUNS)
        {
            Map->Runs[Map->RunCount].DiskOffset = DiskOffset;
            Map->Runs[Map->RunCount].Length = Piece;
            Map->RunCount++;
        }
        else
        {
            Map->RunCount = 0;
            return STATUS_SUCCESS;
        }
    }

    ASSERT(Map->RunCount != 0);
    ASSERT(Mapped == Map->TransferLength);
    *Handled = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsFlushDirectIoMap(_In_ PVolumeContextBlock VolCB,
                     _In_ PNTFS_DIRECT_IO_MAP Map)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    ASSERT(Map->RunCount != 0 && Map->RunCount <= NTFS_DIRECT_IO_RUNS);
    for (Index = 0; Index < Map->RunCount && NT_SUCCESS(Status); Index++)
        Status = NtfsDiskFlushRangeKm(VolCB->StorageDevice, Map->Runs[Index].DiskOffset, Map->Runs[Index].Length);
    return Status;
}

VOID
NtfsDropDirectIoMap(_In_ PVolumeContextBlock VolCB,
                    _In_ PNTFS_DIRECT_IO_MAP Map)
{
    ULONG Index;

    ASSERT(Map->RunCount != 0 && Map->RunCount <= NTFS_DIRECT_IO_RUNS);
    for (Index = 0; Index < Map->RunCount; Index++)
        NtfsDiskDropCleanRangeKm(VolCB->StorageDevice, Map->Runs[Index].DiskOffset, Map->Runs[Index].Length);
}

#define NTFS_POSTED_READ ((PVOID)(ULONG_PTR)0x4E524450)

static IO_WORKITEM_ROUTINE NtfsPostedReadWorker;

static
VOID
NTAPI
NtfsPostedReadWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PIRP Irp = (PIRP)Context;
    PIO_WORKITEM WorkItem = (PIO_WORKITEM)Irp->Tail.Overlay.DriverContext[1];

    Irp->Tail.Overlay.DriverContext[1] = NULL;
    (VOID)NtfsFsdRead(DeviceObject, Irp);
    IoFreeWorkItem(WorkItem);
}

static
BOOLEAN
NtfsPostAsyncRead(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFileContextBlock FileCB;
    PIO_WORKITEM WorkItem;

    if (VolumeDeviceObject == NtfsDiskFileSystemDeviceObject ||
        Irp->Tail.Overlay.DriverContext[0] == NTFS_POSTED_READ ||
        BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) ||
        IrpSp->MinorFunction != IRP_MN_NORMAL ||
        IrpSp->Parameters.Read.Length == 0 ||
        !IrpSp->FileObject ||
        !(BooleanFlagOn(Irp->Flags, IRP_NOCACHE) ||
          BooleanFlagOn(IrpSp->FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING)) ||
        IoIsOperationSynchronous(Irp))
    {
        return FALSE;
    }

    FileCB = NtfsGetFileContext(IrpSp->FileObject);
    if (!FileCB || FileCB->IsVolumeOpen || !FileCB->FileRec)
        return FALSE;
    if (!NT_SUCCESS(NtfsLockTransferBuffer(Irp, IrpSp->Parameters.Read.Length, IoWriteAccess)))
        return FALSE;

    WorkItem = IoAllocateWorkItem(VolumeDeviceObject);
    if (!WorkItem)
        return FALSE;

    Irp->Tail.Overlay.DriverContext[0] = NTFS_POSTED_READ;
    Irp->Tail.Overlay.DriverContext[1] = WorkItem;
    IoMarkIrpPending(Irp);
    IoQueueWorkItem(WorkItem, NtfsPostedReadWorker, DelayedWorkQueue, Irp);
    return TRUE;
}

/* FUNCTIONS ****************************************************************/
_Function_class_(IRP_MJ_READ)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdRead(_In_ PDEVICE_OBJECT VolumeDeviceObject,
            _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    if (NtfsPostAsyncRead(VolumeDeviceObject, Irp))
        return STATUS_PENDING;
    /* Overview:
     * Handles read requests.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-read
     */
    PIO_STACK_LOCATION IrpSp;
    PFILE_OBJECT FileObject;
    PVolumeContextBlock VolCB;
    PNtfsVolume DiskVolume;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    NTSTATUS TimestampStatus;
    PUCHAR Buffer;
    LARGE_INTEGER ReadOffset;
    ULONG OriginalLength;
    ULONG RequestedLength;
    ULONG BytesRead;
    PFileContextBlock FileCB;
    BOOLEAN ResourceAcquired = FALSE;
    BOOLEAN PagingIo = FALSE;
    BOOLEAN DirectRead;
    NTFS_DIRECT_IO_MAP DirectMap;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    FileObject = IrpSp->FileObject;
    VolCB = VolumeDeviceObject &&
            VolumeDeviceObject->DeviceExtension
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    DiskVolume = VolCB ? VolCB->DiskVolume : NULL;
    Buffer = NULL;
    ReadOffset = IrpSp->Parameters.Read.ByteOffset;
    RequestedLength = IrpSp->Parameters.Read.Length;
    OriginalLength = RequestedLength;
    FileCB = NtfsGetFileContext(FileObject);

    if (FileCB && FileCB->IsVolumeOpen)
        return NtfsForwardVolumeIo(VolCB, FileCB, Irp, FALSE);

    if (FileCB && BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) && NtfsPagingFileIo(VolCB, FileCB, Irp, FALSE, &Status))
        return Status;

    if (!FileCB || !FileCB->FileRec || !DiskVolume)
    {
        DPRINT1("NtfsFsdRead(): invalid file or volume context!\n");
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }
    if ((NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY) &&
        !FileCB->RequestedStream &&
        !(Irp->Flags & IRP_PAGING_IO))
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Complete;
    }
    if (ReadOffset.QuadPart < 0)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }
    if (!BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
        FileCB->StreamCB &&
        !FsRtlCheckLockForReadAccess(&FileCB->StreamCB->FileLock, Irp))
    {
        Status = STATUS_FILE_LOCK_CONFLICT;
        goto Complete;
    }

    /* Paging I/O is serviced while a caller may already hold MainResource,
     * so it synchronizes on PagingIoResource instead. */
    PagingIo = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO);
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(PagingIo ? NtfsGetPagingIoResource(FileCB) : NtfsGetMainResource(FileCB), TRUE);
    ResourceAcquired = TRUE;

    if (IrpSp->MinorFunction == IRP_MN_COMPLETE)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Complete;
    }

    /*
     * A direct or uncached read bypasses the VACB that normally shares the
     * data-section page.  Flush the live section first so that file reads and
     * mapped writes remain coherent.  Paging reads are the section page-in
     * itself and must not recurse here.
     */
    if (RequestedLength &&
        !PagingIo &&
        FileCB->RequestedType == TypeData &&
        FileObject->SectionObjectPointer != NULL &&
        FileObject->SectionObjectPointer->DataSectionObject != NULL &&
        (BooleanFlagOn(Irp->Flags, IRP_NOCACHE) ||
         BooleanFlagOn(FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING)))
    {
        IO_STATUS_BLOCK FlushIoStatus;

        CcFlushCache(FileObject->SectionObjectPointer, &ReadOffset, RequestedLength, &FlushIoStatus);
        if (!NT_SUCCESS(FlushIoStatus.Status))
        {
            Status = FlushIoStatus.Status;
            goto Complete;
        }
    }

    if (RequestedLength &&
        FileCB->RequestedType == TypeData &&
        (PagingIo ||
         BooleanFlagOn(Irp->Flags, IRP_NOCACHE) ||
         BooleanFlagOn(FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING)))
    {
        NtfsAcquireMetadata(VolCB);
        Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            goto Complete;
        }
        Status = NtfsMapDirectIo(VolCB,
                                 FileCB,
                                 Irp,
                                 (ULONGLONG)ReadOffset.QuadPart,
                                 RequestedLength,
                                 IoWriteAccess,
                                 TRUE,
                                 &DirectMap,
                                 &DirectRead);
        NtfsReleaseMetadata(VolCB);
        if (DirectRead)
        {
            PUCHAR ZeroBase = NULL;

            /* The stream resource held shared excludes every path that frees or moves this stream's clusters. */
            ASSERT(ExIsResourceAcquiredSharedLite(PagingIo ? NtfsGetPagingIoResource(FileCB)
                                                           : NtfsGetMainResource(FileCB)));
            if (NT_SUCCESS(Status) && DirectMap.ValidLength < RequestedLength)
            {
                ASSERT(DirectMap.TransferLength <= RequestedLength);
                ASSERT(DirectMap.DataLength <= RequestedLength);
                ZeroBase = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority | MdlMappingNoExecute);
                if (!ZeroBase)
                    Status = STATUS_INSUFFICIENT_RESOURCES;
            }
            if (NT_SUCCESS(Status))
            {
                /* Small file writes may still be in the library's block cache.
                 * Paging and unbuffered reads bypass it, so commit only the
                 * overlapping blocks before sending this IRP to the device. */
                Status = NtfsFlushDirectIoMap(VolCB, &DirectMap);
                if (NT_SUCCESS(Status))
                    Status = NtfsSubmitDirectIoMap(VolCB, Irp, IRP_MJ_READ, &DirectMap);
            }
            if (NT_SUCCESS(Status) && ZeroBase)
            {
                if (Irp->IoStatus.Information != DirectMap.TransferLength)
                {
                    Status = STATUS_DATA_ERROR;
                }
                else
                {
                    RtlZeroMemory(ZeroBase + DirectMap.ValidLength,
                                  max(DirectMap.TransferLength, DirectMap.DataLength) - DirectMap.ValidLength);
                    Irp->IoStatus.Information = DirectMap.DataLength;
                }
            }
            if (NT_SUCCESS(Status))
            {
                if (Irp->IoStatus.Information <= OriginalLength)
                {
                    RequestedLength = OriginalLength -
                                      (ULONG)Irp->IoStatus.Information;
                }
                else
                {
                    Status = STATUS_DATA_ERROR;
                }
            }
            goto ReadDone;
        }
    }

    Buffer = (PUCHAR)GetBuffer(Irp);
    if (RequestedLength != 0 && !Buffer)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        goto Complete;
    }

    /* The first ordinary read establishes the stream's shared cache map. Cc
     * keeps that map in the stream's section object pointers, so a stream the
     * create path left without one is read uncached. */
    if (NtfsCachedReadsEnabled &&
        RequestedLength &&
        !PagingIo &&
        !BooleanFlagOn(Irp->Flags, IRP_NOCACHE) &&
        !BooleanFlagOn(FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING) &&
        FileCB->RequestedType == TypeData &&
        FileObject->SectionObjectPointer != NULL &&
        FileObject->PrivateCacheMap == NULL)
    {
        NTSTATUS CacheStatus = STATUS_SUCCESS;

        _SEH2_TRY
        {
            NtfsInitializeStreamCache(FileCB, FileObject);
        }
        _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                     EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            CacheStatus = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(CacheStatus))
        {
            Status = CacheStatus;
            goto Complete;
        }
    }

    /*
     * Ordinary reads go through the cache manager so repeated and sequential
     * access is served from memory. Paging and no-buffering requests must
     * reach the volume directly.
     */
    if (NtfsCachedReadsEnabled &&
        RequestedLength &&
        !BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
        !BooleanFlagOn(Irp->Flags, IRP_NOCACHE) &&
        !BooleanFlagOn(FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING) &&
        FileObject->PrivateCacheMap != NULL &&
        FileCB->RequestedType == TypeData)
    {
        if (ReadOffset.QuadPart >= NtfsGetCommonFcbHeader(FileCB)->FileSize.QuadPart)
        {
            RequestedLength = 0;
            Status = STATUS_END_OF_FILE;
        }
        else
        {
            if (ReadOffset.QuadPart + RequestedLength > NtfsGetCommonFcbHeader(FileCB)->FileSize.QuadPart)
            {
                RequestedLength =
                    (ULONG)(NtfsGetCommonFcbHeader(FileCB)->FileSize.QuadPart -
                            ReadOffset.QuadPart);
                OriginalLength = RequestedLength;
            }
            _SEH2_TRY
            {
                if (!CcCopyRead(FileObject, &ReadOffset, RequestedLength,
                                TRUE, Buffer, &Irp->IoStatus))
                {
                    Status = STATUS_CANT_WAIT;
                }
                else
                {
                    Status = Irp->IoStatus.Status;
                    if (NT_SUCCESS(Status))
                    {
                        RequestedLength = OriginalLength -
                                          (ULONG)Irp->IoStatus.Information;
                    }
                }
            }
            _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                         EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                Status = _SEH2_GetExceptionCode();
                Irp->IoStatus.Information = 0;
                RequestedLength = 0;
            }
            _SEH2_END;
        }
    }
    else if (RequestedLength)
    {
        /*
         * A paging read targets the section's own pages, which Mm has already
         * locked for this transfer. Filling them by way of the storage stack
         * would probe and lock them a second time, so land the data in pool
         * memory and copy it across.
         */
        PVOID Bounce = PagingIo
            ? ExAllocatePoolUninitialized(NonPagedPoolNx, RequestedLength, TAG_NTFS)
            : NULL;

        // Copy data from $DATA into file buffer.
        NtfsAcquireMetadata(VolCB);
        Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (NT_SUCCESS(Status))
            Status = NtfsFileRecordCopyData(FileCB->FileRec,
                                            FileCB->RequestedType,
                                            FileCB->RequestedStream,
                                            Bounce ? (PUCHAR)Bounce : Buffer,
                                            &RequestedLength,
                                            ReadOffset.QuadPart);
        NtfsReleaseMetadata(VolCB);
        if (Bounce)
        {
            if (NT_SUCCESS(Status) && OriginalLength > RequestedLength)
                RtlCopyMemory(Buffer, Bounce, OriginalLength - RequestedLength);
            ExFreePoolWithTag(Bounce, TAG_NTFS);
        }
    }

    else
    {
        // If we aren't reading anything, don't read anything.
        Status = STATUS_SUCCESS;
    }

ReadDone:
    ExReleaseResourceLite(PagingIo ? NtfsGetPagingIoResource(FileCB)
                                   : NtfsGetMainResource(FileCB));
    KeLeaveCriticalRegion();
    ResourceAcquired = FALSE;

    if (NT_SUCCESS(Status))
    {
        BytesRead = OriginalLength - RequestedLength;
        if (BytesRead != 0 &&
            !PagingIo &&
            FileCB->RequestedType == TypeData &&
            !NtfsVolumeIsReadOnly(DiskVolume) &&
            FileCB->LastAccessStampPending)
        {
            FileCB->LastAccessStampPending = FALSE;
            /*
             * Last-access is metadata, so serialize its MFT update after the
             * shared data read. A timestamp failure must not discard bytes
             * already delivered successfully to the caller.
             *
             * Paging reads are excluded: they are the cache manager faulting
             * pages in on behalf of a request that already stamped the file,
             * and they run on a thread that still holds MainResource shared,
             * which no exclusive acquire here could ever be granted over.
             */
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
            NtfsAcquireMetadata(VolCB);
            TimestampStatus = NtfsRefreshDirectoryRecord(VolCB, FileCB);
            if (NT_SUCCESS(TimestampStatus))
                TimestampStatus = NtfsFileRecordUpdateAutomaticTimestamps(
                    FileCB->FileRec,
                    NTFS_BASIC_INFO_LAST_ACCESS_TIME);
            if (NT_SUCCESS(TimestampStatus) &&
                (NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY))
            {
                InterlockedIncrement(&VolCB->DirGeneration);
            }
            NtfsReleaseMetadata(VolCB);
            ExReleaseResourceLite(NtfsGetMainResource(FileCB));
            KeLeaveCriticalRegion();
            if (!NT_SUCCESS(TimestampStatus))
            {
                DPRINT1(
                    "NtfsFsdRead(): failed to update last-access time: 0x%08lx\n",
                    TimestampStatus);
            }
        }

        /* Cache page-ins and read-ahead use this file object too. Their
         * explicit offsets must not move the application's file pointer. */
        if (!PagingIo && (FileObject->Flags & FO_SYNCHRONOUS_IO))
        {
            FileObject->CurrentByteOffset.QuadPart =
                ReadOffset.QuadPart + BytesRead;
        }

        Irp->IoStatus.Information = BytesRead;
    }

    else
    {
        Irp->IoStatus.Information = 0;
    }

Complete:
    if (ResourceAcquired)
    {
        ExReleaseResourceLite(PagingIo ? NtfsGetPagingIoResource(FileCB)
                                       : NtfsGetMainResource(FileCB));
        KeLeaveCriticalRegion();
    }
    if (!NT_SUCCESS(Status))
        Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}
