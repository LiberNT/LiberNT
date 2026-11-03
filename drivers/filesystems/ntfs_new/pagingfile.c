/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Paging file I/O that takes no file system lock and allocates no memory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

#define NTFS_PAGING_MAX_PAGES       64
#define NTFS_PAGING_FLUSH_BYTES     0x40000000ULL
#define NTFS_PAGING_INITIAL_RUNS    64

typedef struct _NTFS_PAGING_RUN
{
    ULONGLONG StartVcn;
    ULONGLONG NextVcn;
    ULONGLONG Lcn;
} NTFS_PAGING_RUN, *PNTFS_PAGING_RUN;

typedef struct _NTFS_PAGING_MAP
{
    struct _NTFS_PAGING_MAP* Previous;
    ULONG RunCount;
    NTFS_PAGING_RUN Run[ANYSIZE_ARRAY];
} NTFS_PAGING_MAP, *PNTFS_PAGING_MAP;

typedef struct _NTFS_PAGING_FILE
{
    PNTFS_PAGING_MAP Map;
    ULONGLONG ClusterSize;
    PIRP Irp;
    KEVENT Available;
} NTFS_PAGING_FILE, *PNTFS_PAGING_FILE;

typedef struct _NTFS_PAGING_REQUEST
{
    PVolumeContextBlock VolCB;
    PNTFS_PAGING_FILE Paging;
    PNTFS_PAGING_MAP Map;
    PIRP Master;
    ULONGLONG FileOffset;
    ULONG Length;
    BOOLEAN Write;
} NTFS_PAGING_REQUEST, *PNTFS_PAGING_REQUEST;

static
PNTFS_PAGING_RUN
NtfsPagingFindRun(
    _In_ PNTFS_PAGING_MAP Map,
    _In_ ULONGLONG Vcn)
{
    ULONG Low = 0;
    ULONG High = Map->RunCount;

    while (Low < High)
    {
        ULONG Middle = Low + (High - Low) / 2;
        PNTFS_PAGING_RUN Run = &Map->Run[Middle];

        if (Vcn < Run->StartVcn)
            High = Middle;
        else if (Vcn >= Run->NextVcn)
            Low = Middle + 1;
        else
            return Run;
    }

    return NULL;
}

static
BOOLEAN
NtfsPagingTranslate(
    _In_ PNTFS_PAGING_FILE Paging,
    _In_ PNTFS_PAGING_MAP Map,
    _In_ ULONGLONG FileOffset,
    _Out_ PULONGLONG DiskOffset,
    _Out_ PULONGLONG Contiguous)
{
    ULONGLONG Vcn = FileOffset / Paging->ClusterSize;
    ULONGLONG InCluster = FileOffset % Paging->ClusterSize;
    PNTFS_PAGING_RUN Run = NtfsPagingFindRun(Map, Vcn);

    if (Run == NULL)
        return FALSE;

    *DiskOffset = (Run->Lcn + (Vcn - Run->StartVcn)) * Paging->ClusterSize + InCluster;
    *Contiguous = (Run->NextVcn - Vcn) * Paging->ClusterSize - InCluster;
    return TRUE;
}

static
BOOLEAN
NtfsPagingCovers(
    _In_ PNTFS_PAGING_FILE Paging,
    _In_ PNTFS_PAGING_MAP Map,
    _In_ ULONGLONG FileOffset,
    _In_ ULONG Length)
{
    while (Length != 0)
    {
        ULONGLONG DiskOffset;
        ULONGLONG Contiguous;
        ULONG Step;

        if (!NtfsPagingTranslate(Paging, Map, FileOffset, &DiskOffset, &Contiguous))
            return FALSE;

        Step = (ULONG)min(Contiguous, (ULONGLONG)Length);
        FileOffset += Step;
        Length -= Step;
    }

    return TRUE;
}

static
NTSTATUS
NtfsPagingSubmit(
    _In_ PNTFS_PAGING_REQUEST Request,
    _In_ ULONGLONG DiskOffset)
{
    PIRP Irp = Request->Master;
    PIO_STACK_LOCATION Next;

    IoCopyCurrentIrpStackLocationToNext(Irp);
    Next = IoGetNextIrpStackLocation(Irp);
    Next->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    Next->FileObject = NULL;
    if (Request->Write)
    {
        Next->Parameters.Write.Length = Request->Length;
        Next->Parameters.Write.ByteOffset.QuadPart = (LONGLONG)DiskOffset;
    }
    else
    {
        Next->Parameters.Read.Length = Request->Length;
        Next->Parameters.Read.ByteOffset.QuadPart = (LONGLONG)DiskOffset;
    }

    return IoCallDriver(Request->VolCB->StorageDevice, Irp);
}

static
NTSTATUS
NTAPI
NtfsPagingChunkComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NtfsPagingChunk(
    _In_ PNTFS_PAGING_REQUEST Request,
    _Inout_ PMDL Partial,
    _In_ ULONG Done,
    _In_ ULONGLONG DiskOffset,
    _In_ ULONG Chunk)
{
    PIRP Irp = Request->Paging->Irp;
    PMDL Source = Request->Master->MdlAddress;
    PIO_STACK_LOCATION Next;
    KEVENT Finished;

    KeInitializeEvent(&Finished, NotificationEvent, FALSE);
    MmInitializeMdl(Partial, NULL, NTFS_PAGING_MAX_PAGES * PAGE_SIZE);
    IoBuildPartialMdl(Source, Partial, (PUCHAR)MmGetMdlVirtualAddress(Source) + Done, Chunk);

    IoReuseIrp(Irp, STATUS_NOT_SUPPORTED);
    Irp->MdlAddress = Partial;
    Irp->UserBuffer = MmGetMdlVirtualAddress(Partial);
    Irp->Flags = Request->Master->Flags &
                 (IRP_PAGING_IO | IRP_NOCACHE | IRP_SYNCHRONOUS_PAGING_IO | IRP_INPUT_OPERATION);
    Irp->RequestorMode = KernelMode;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();

    Next = IoGetNextIrpStackLocation(Irp);
    Next->MajorFunction = Request->Write ? IRP_MJ_WRITE : IRP_MJ_READ;
    Next->Flags = SL_OVERRIDE_VERIFY_VOLUME;
    if (Request->Write)
    {
        Next->Parameters.Write.Length = Chunk;
        Next->Parameters.Write.ByteOffset.QuadPart = (LONGLONG)DiskOffset;
    }
    else
    {
        Next->Parameters.Read.Length = Chunk;
        Next->Parameters.Read.ByteOffset.QuadPart = (LONGLONG)DiskOffset;
    }
    IoSetCompletionRoutine(Irp, NtfsPagingChunkComplete, &Finished, TRUE, TRUE, TRUE);

    if (IoCallDriver(Request->VolCB->StorageDevice, Irp) == STATUS_PENDING)
        KeWaitForSingleObject(&Finished, Executive, KernelMode, FALSE, NULL);

    MmPrepareMdlForReuse(Partial);
    Irp->MdlAddress = NULL;
    return Irp->IoStatus.Status;
}

static
NTSTATUS
NtfsPagingSplit(
    _In_ PNTFS_PAGING_REQUEST Request)
{
    struct
    {
        MDL Mdl;
        PFN_NUMBER Pages[NTFS_PAGING_MAX_PAGES + 1];
    } Partial;
    PIRP Master = Request->Master;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Done = 0;

    KeWaitForSingleObject(&Request->Paging->Available, Executive, KernelMode, FALSE, NULL);

    while (NT_SUCCESS(Status) && Done < Request->Length)
    {
        ULONGLONG DiskOffset = 0;
        ULONGLONG Contiguous = 0;
        ULONG Chunk;

        if (!NtfsPagingTranslate(Request->Paging, Request->Map, Request->FileOffset + Done, &DiskOffset,
                                 &Contiguous))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }

        Chunk = (ULONG)min(Contiguous, (ULONGLONG)(Request->Length - Done));
        Status = NtfsPagingChunk(Request, &Partial.Mdl, Done, DiskOffset, Chunk);
        if (NT_SUCCESS(Status))
            Done += Chunk;
    }

    KeSetEvent(&Request->Paging->Available, IO_NO_INCREMENT, FALSE);

    Master->IoStatus.Status = Status;
    Master->IoStatus.Information = NT_SUCCESS(Status) ? Request->Length : 0;
    IoCompleteRequest(Master, IO_DISK_INCREMENT);
    return Status;
}

BOOLEAN
NtfsPagingFileIo(
    _In_ PVolumeContextBlock VolCB,
    _In_ PFileContextBlock FileCB,
    _Inout_ PIRP Irp,
    _In_ BOOLEAN Write,
    _Out_ PNTSTATUS Status)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PNTFS_PAGING_FILE Paging = FileCB->PagingFile;
    NTFS_PAGING_REQUEST Request;
    ULONGLONG DiskOffset;
    ULONGLONG Contiguous;
    LONGLONG FileOffset;
    PMDL Mdl = Irp->MdlAddress;

    if (Paging == NULL)
        return FALSE;

    Request.VolCB = VolCB;
    Request.Paging = Paging;
    Request.Map = (PNTFS_PAGING_MAP)ReadPointerAcquire((PVOID const volatile*)&Paging->Map);
    Request.Master = Irp;
    Request.Write = Write;
    Request.Length = Write ? IrpSp->Parameters.Write.Length : IrpSp->Parameters.Read.Length;
    FileOffset = Write ? IrpSp->Parameters.Write.ByteOffset.QuadPart : IrpSp->Parameters.Read.ByteOffset.QuadPart;

    if (Request.Map == NULL || Mdl == NULL || Mdl->Next != NULL || FileOffset < 0 || Request.Length == 0 ||
        Request.Length > NTFS_PAGING_MAX_PAGES * PAGE_SIZE || MmGetMdlByteCount(Mdl) < Request.Length ||
        !VolCB->StorageDevice || !VolCB->BytesPerSector || Request.Length % VolCB->BytesPerSector != 0 ||
        (ULONGLONG)FileOffset % VolCB->BytesPerSector != 0 ||
        ((ULONG_PTR)MmGetMdlVirtualAddress(Mdl) & VolCB->StorageDevice->AlignmentRequirement) != 0)
    {
        return FALSE;
    }

    Request.FileOffset = (ULONGLONG)FileOffset;
    if (!NtfsPagingCovers(Paging, Request.Map, Request.FileOffset, Request.Length) ||
        !NtfsPagingTranslate(Paging, Request.Map, Request.FileOffset, &DiskOffset, &Contiguous))
    {
        return FALSE;
    }

    *Status = (Request.Length <= Contiguous) ? NtfsPagingSubmit(&Request, DiskOffset) : NtfsPagingSplit(&Request);
    return TRUE;
}

static
NTSTATUS
NtfsPagingMapFill(
    _In_ PFileContextBlock FileCB,
    _Inout_ PNTFS_PAGING_MAP Map,
    _In_ ULONG Capacity)
{
    ULONGLONG Vcn = 0;

    Map->RunCount = 0;
    for (;;)
    {
        NtfsRetrievalExtent Extent;
        ULONGLONG Returned;
        ULONG Count = 1;
        NTSTATUS Status;
        PNTFS_PAGING_RUN Run;

        Status = NtfsFileRecordQueryRetrievalPointers(FileCB->FileRec, FileCB->RequestedType,
                                                      FileCB->RequestedStream, Vcn, &Returned, &Extent, &Count);
        if (Status == STATUS_END_OF_FILE)
            return STATUS_SUCCESS;
        if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
            return Status;
        if (Count != 1 || Returned > Vcn || Extent.NextVcn <= Vcn || Extent.Lcn < 0)
            return STATUS_NOT_SUPPORTED;
        if (Map->RunCount == Capacity)
            return STATUS_BUFFER_OVERFLOW;

        Run = &Map->Run[Map->RunCount++];
        Run->StartVcn = Vcn;
        Run->NextVcn = Extent.NextVcn;
        Run->Lcn = (ULONGLONG)Extent.Lcn + (Vcn - Returned);
        Vcn = Extent.NextVcn;
    }
}

static
PNTFS_PAGING_MAP
NtfsPagingMapBuild(
    _In_ PVolumeContextBlock VolCB,
    _In_ PFileContextBlock FileCB)
{
    ULONG Capacity = NTFS_PAGING_INITIAL_RUNS;

    for (;;)
    {
        PNTFS_PAGING_MAP Map;
        NTSTATUS Status;

        Map = ExAllocatePoolZero(NonPagedPoolNx, FIELD_OFFSET(NTFS_PAGING_MAP, Run[Capacity]), TAG_NTFS);
        if (Map == NULL)
            return NULL;

        NtfsAcquireMetadata(VolCB);
        Status = NtfsPagingMapFill(FileCB, Map, Capacity);
        NtfsReleaseMetadata(VolCB);

        if (NT_SUCCESS(Status))
            return Map;

        ExFreePoolWithTag(Map, TAG_NTFS);
        if (Status != STATUS_BUFFER_OVERFLOW)
            return NULL;

        Capacity *= 2;
    }
}

static
BOOLEAN
NtfsPagingMapFlush(
    _In_ PVolumeContextBlock VolCB,
    _In_ PNTFS_PAGING_FILE Paging,
    _In_ PNTFS_PAGING_MAP Map)
{
    ULONG Index;

    for (Index = 0; Index < Map->RunCount; Index++)
    {
        PNTFS_PAGING_RUN Run = &Map->Run[Index];
        ULONGLONG DiskOffset = Run->Lcn * Paging->ClusterSize;
        ULONGLONG Remaining = (Run->NextVcn - Run->StartVcn) * Paging->ClusterSize;

        while (Remaining != 0)
        {
            ULONG Chunk = (ULONG)min(Remaining, NTFS_PAGING_FLUSH_BYTES);

            if (!NT_SUCCESS(NtfsDiskFlushRangeKm(VolCB->StorageDevice, DiskOffset, Chunk)))
                return FALSE;

            DiskOffset += Chunk;
            Remaining -= Chunk;
        }
    }

    return TRUE;
}

static
PNTFS_PAGING_FILE
NtfsPagingFileAttach(
    _In_ PVolumeContextBlock VolCB,
    _Inout_ PFileContextBlock FileCB)
{
    PNTFS_PAGING_FILE Paging = FileCB->PagingFile;

    if (Paging != NULL || !VolCB->StorageDevice || !VolCB->BytesPerSector)
        return Paging;

    Paging = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Paging), TAG_NTFS);
    if (Paging == NULL)
        return NULL;

    Paging->ClusterSize = (ULONGLONG)VolCB->BytesPerSector * NtfsVolumeGetSectorsPerCluster(VolCB->DiskVolume);
    Paging->Irp = IoAllocateIrp(VolCB->StorageDevice->StackSize, FALSE);
    if (Paging->ClusterSize == 0 || Paging->Irp == NULL)
    {
        if (Paging->Irp != NULL)
            IoFreeIrp(Paging->Irp);
        ExFreePoolWithTag(Paging, TAG_NTFS);
        return NULL;
    }

    KeInitializeEvent(&Paging->Available, SynchronizationEvent, TRUE);
    InterlockedExchangePointer((PVOID volatile*)&FileCB->PagingFile, Paging);
    return Paging;
}

VOID
NtfsPagingFileRefresh(
    _In_ PVolumeContextBlock VolCB,
    _Inout_ PFileContextBlock FileCB)
{
    PNTFS_PAGING_FILE Paging;
    PNTFS_PAGING_MAP Map;

    Paging = NtfsPagingFileAttach(VolCB, FileCB);
    if (Paging == NULL)
        return;

    KeEnterCriticalRegion();
    Map = NtfsPagingMapBuild(VolCB, FileCB);
    KeLeaveCriticalRegion();
    if (Map == NULL)
        return;

    if (!NtfsPagingMapFlush(VolCB, Paging, Map))
    {
        ExFreePoolWithTag(Map, TAG_NTFS);
        return;
    }

    Map->Previous = Paging->Map;
    InterlockedExchangePointer((PVOID volatile*)&Paging->Map, Map);
}

VOID
NtfsPagingFileClose(
    _Inout_ PFileContextBlock FileCB)
{
    PNTFS_PAGING_FILE Paging = FileCB->PagingFile;
    PNTFS_PAGING_MAP Map;

    if (FileCB->PagingFileOpen && FileCB->StreamCB)
        FileCB->StreamCB->PagingFile = FALSE;

    if (Paging == NULL)
        return;

    FileCB->PagingFile = NULL;
    Map = Paging->Map;
    while (Map != NULL)
    {
        PNTFS_PAGING_MAP Previous = Map->Previous;

        ExFreePoolWithTag(Map, TAG_NTFS);
        Map = Previous;
    }

    IoFreeIrp(Paging->Irp);
    ExFreePoolWithTag(Paging, TAG_NTFS);
}
