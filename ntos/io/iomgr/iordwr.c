/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The data-moving system calls on an open file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

typedef struct _IOP_REQUEST
{
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    PKEVENT EventObject;
    PIRP Irp;
    PIO_STACK_LOCATION StackPtr;
    KPROCESSOR_MODE PreviousMode;
    BOOLEAN SynchIo;
    BOOLEAN Locked;
} IOP_REQUEST, *PIOP_REQUEST;

/* TODO: FileIdGlobalTxDirectoryInformation and the FileIdExtd classes are refused until a file system answers them */
static const UCHAR IopQueryDirectoryLength[FileMaximumInformation] =
{
    [FileDirectoryInformation] = sizeof(FILE_DIRECTORY_INFORMATION),
    [FileFullDirectoryInformation] = sizeof(FILE_FULL_DIR_INFORMATION),
    [FileBothDirectoryInformation] = sizeof(FILE_BOTH_DIR_INFORMATION),
    [FileNamesInformation] = sizeof(FILE_NAMES_INFORMATION),
    [FileObjectIdInformation] = sizeof(FILE_OBJECTID_INFORMATION),
    [FileQuotaInformation] = sizeof(FILE_QUOTA_INFORMATION),
    [FileReparsePointInformation] = sizeof(FILE_REPARSE_POINT_INFORMATION),
    [FileIdBothDirectoryInformation] = sizeof(FILE_ID_BOTH_DIR_INFORMATION),
    [FileIdFullDirectoryInformation] = sizeof(FILE_ID_FULL_DIR_INFORMATION)
};

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
IopBeginRequest(
    _Out_ PIOP_REQUEST Request,
    _In_ HANDLE FileHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ HANDLE EventHandle,
    _In_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ UCHAR MajorFunction,
    _In_ BOOLEAN RequireLock)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PFILE_OBJECT FileObject;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(Request, sizeof(*Request));
    Request->PreviousMode = PreviousMode;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteIoStatusBlock(IoStatusBlock);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(FileHandle,
                                       DesiredAccess,
                                       IoFileObjectType,
                                       PreviousMode,
                                       (PVOID *)&FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Request->FileObject = FileObject;

    if (EventHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(EventHandle,
                                           EVENT_MODIFY_STATE,
                                           ExEventObjectType,
                                           PreviousMode,
                                           (PVOID *)&Request->EventObject,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(FileObject);
            return Status;
        }

        KeClearEvent(Request->EventObject);
    }

    Request->SynchIo = (FileObject->Flags & FO_SYNCHRONOUS_IO) != 0;

    if (Request->SynchIo && RequireLock)
    {
        Status = IopLockFileObject(FileObject, PreviousMode);
        if (!NT_SUCCESS(Status))
        {
            if (Request->EventObject != NULL)
            {
                ObDereferenceObject(Request->EventObject);
            }

            ObDereferenceObject(FileObject);
            return Status;
        }

        Request->Locked = TRUE;
    }

    Request->DeviceObject = IoGetRelatedDeviceObject(FileObject);
    if (Request->DeviceObject == NULL)
    {
        if (Request->Locked) IopUnlockFileObject(FileObject);
        if (Request->EventObject != NULL)
        {
            ObDereferenceObject(Request->EventObject);
        }

        ObDereferenceObject(FileObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    Request->Irp = IoAllocateIrp(Request->DeviceObject->StackSize, FALSE);
    if (Request->Irp == NULL)
    {
        if (Request->Locked) IopUnlockFileObject(FileObject);
        if (Request->EventObject != NULL)
        {
            ObDereferenceObject(Request->EventObject);
        }

        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Request->Irp->Tail.Overlay.OriginalFileObject = FileObject;
    Request->Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Request->Irp->RequestorMode = PreviousMode;
    Request->Irp->UserIosb = IoStatusBlock;
    Request->Irp->UserEvent = Request->EventObject;

    Request->StackPtr = IoGetNextIrpStackLocation(Request->Irp);
    Request->StackPtr->MajorFunction = MajorFunction;
    Request->StackPtr->FileObject = FileObject;

    return STATUS_SUCCESS;
}

static
VOID
IopAbortRequest(
    _In_ PIOP_REQUEST Request)
{
    if (Request->Irp != NULL)
    {
        IoFreeIrp(Request->Irp);
        Request->Irp = NULL;
    }

    if (Request->Locked) IopUnlockFileObject(Request->FileObject);
    if (Request->EventObject != NULL)
    {
        ObDereferenceObject(Request->EventObject);
    }

    ObDereferenceObject(Request->FileObject);
}

static
BOOLEAN
IopIsTransferValid(
    _In_ PIOP_REQUEST Request,
    _In_ BOOLEAN Write,
    _In_ PVOID Buffer,
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER Offset)
{
    PDEVICE_OBJECT DeviceObject = Request->DeviceObject;
    ULONG SectorSize = DeviceObject->SectorSize;

    if (Offset->QuadPart < 0 &&
        !(Write && Offset->QuadPart == -1) &&
        !(Request->FileObject->Flags & (FO_NAMED_PIPE | FO_MAILSLOT)))
    {
        return FALSE;
    }

    if (!(Request->FileObject->Flags & FO_NO_INTERMEDIATE_BUFFERING)) return TRUE;

    if ((ULONG_PTR)Buffer & DeviceObject->AlignmentRequirement) return FALSE;

    if (SectorSize == 0) return TRUE;

    if (Length % SectorSize != 0) return FALSE;

    return Offset->QuadPart < 0 || (Offset->QuadPart % SectorSize) == 0;
}

static
NTSTATUS
IopSetAsyncParameters(
    _In_ PIOP_REQUEST Request,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext)
{
    PIRP Irp = Request->Irp;

    if (ApcRoutine != NULL && Request->FileObject->CompletionContext != NULL)
    {
        IopAbortRequest(Request);
        return STATUS_INVALID_PARAMETER;
    }

    Irp->Overlay.AsynchronousParameters.UserApcRoutine = ApcRoutine;
    Irp->Overlay.AsynchronousParameters.UserApcContext = ApcContext;

    return STATUS_SUCCESS;
}

static
NTSTATUS
IopBeginRequestEitherAccess(
    _Out_ PIOP_REQUEST Request,
    _In_ HANDLE FileHandle,
    _In_ ACCESS_MASK FirstAccess,
    _In_ ACCESS_MASK SecondAccess,
    _In_opt_ HANDLE EventHandle,
    _In_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ UCHAR MajorFunction)
{
    NTSTATUS Status;

    Status = IopBeginRequest(Request,
                             FileHandle,
                             FirstAccess,
                             EventHandle,
                             IoStatusBlock,
                             MajorFunction,
                             TRUE);
    if (Status != STATUS_ACCESS_DENIED) return Status;

    return IopBeginRequest(Request,
                           FileHandle,
                           SecondAccess,
                           EventHandle,
                           IoStatusBlock,
                           MajorFunction,
                           TRUE);
}

static
NTSTATUS
IopIssueRequest(
    _In_ PIOP_REQUEST Request,
    _In_ IOP_TRANSFER_TYPE TransferType)
{
    PIO_STATUS_BLOCK CallerIosb = NULL;
    IO_STATUS_BLOCK LocalIosb;
    KEVENT LocalEvent;
    BOOLEAN LocalWait;
    BOOLEAN Deferred;
    NTSTATUS Status;

    KeClearEvent(&Request->FileObject->Event);

    /* A synchronous service returns only once its request is done, even on a file opened for asynchronous I/O */
    LocalWait = !Request->SynchIo && (Request->Irp->Flags & IRP_SYNCHRONOUS_API);
    if (LocalWait)
    {
        ASSERT(Request->Irp->UserEvent == NULL);

        KeInitializeEvent(&LocalEvent, NotificationEvent, FALSE);
        CallerIosb = Request->Irp->UserIosb;
        Request->Irp->UserIosb = &LocalIosb;
        Request->Irp->UserEvent = &LocalEvent;
    }

    IopQueueIrpToThread(Request->Irp);

    IopUpdateOperationCount(TransferType);

    Deferred = BooleanFlagOn(Request->Irp->Flags, IRP_DEFER_IO_COMPLETION);

    Status = IoCallDriver(Request->DeviceObject, Request->Irp);

    if (Deferred && Status != STATUS_PENDING)
    {
        IopCompleteDeferredIrp(Request->Irp, Request->FileObject);
    }

    if (LocalWait)
    {
        /* An error the driver gave straight back left the status block alone and signalled nothing */
        if (Status != STATUS_PENDING && NT_ERROR(Status)) return Status;

        KeWaitForSingleObject(&LocalEvent, Executive, KernelMode, FALSE, NULL);
        if (Status == STATUS_PENDING) Status = LocalIosb.Status;

        if (Request->PreviousMode == KernelMode)
        {
            *CallerIosb = LocalIosb;
            return Status;
        }

        _SEH2_TRY
        {
            IopWriteIoStatusBlock(CallerIosb, &LocalIosb, FALSE);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    if (!Request->SynchIo) return Status;

    if (Status == STATUS_PENDING)
    {
        Status = KeWaitForSingleObject(&Request->FileObject->Event,
                                       Executive,
                                       Request->PreviousMode,
                                       BooleanFlagOn(Request->FileObject->Flags,
                                                     FO_ALERTABLE_IO),
                                       NULL);
        if (Status == STATUS_ALERTED || Status == STATUS_USER_APC)
        {
            IopAbortInterruptedIrp(&Request->FileObject->Event, Request->Irp);
        }

        Status = Request->FileObject->FinalStatus;
    }

    if (Request->Locked) IopUnlockFileObject(Request->FileObject);

    return Status;
}

/*
 * A file its file system keeps in the cache can often be read or written with
 * no packet at all. The file system is asked first; when it does not take the
 * request, or takes it without finishing it, the packet goes down as usual.
 */
static
BOOLEAN
IopTryFastIo(
    _In_ PIOP_REQUEST Request,
    _In_ BOOLEAN Write,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_ ULONG Length,
    _In_ ULONG Key,
    _In_ PVOID Buffer,
    _Out_ PIO_STATUS_BLOCK IoStatus)
{
    PFILE_OBJECT FileObject = Request->FileObject;
    PFAST_IO_DISPATCH FastIo;

    /* TODO: a file opened for asynchronous I/O always gets a packet */
    if (!Request->SynchIo || FileObject->PrivateCacheMap == NULL) return FALSE;

    FastIo = Request->DeviceObject->DriverObject->FastIoDispatch;
    if (FastIo == NULL) return FALSE;

    /* The two oldest members of the table are read without asking its size, which a driver need not have set */
    if (Write)
    {
        if (FastIo->FastIoWrite == NULL) return FALSE;

        if (!FastIo->FastIoWrite(FileObject,
                                 ByteOffset,
                                 Length,
                                 TRUE,
                                 Key,
                                 Buffer,
                                 IoStatus,
                                 Request->DeviceObject))
        {
            return FALSE;
        }

        return (BOOLEAN)(IoStatus->Status == STATUS_SUCCESS);
    }

    if (FastIo->FastIoRead == NULL) return FALSE;

    if (!FastIo->FastIoRead(FileObject,
                            ByteOffset,
                            Length,
                            TRUE,
                            Key,
                            Buffer,
                            IoStatus,
                            Request->DeviceObject))
    {
        return FALSE;
    }

    return (BOOLEAN)(IoStatus->Status == STATUS_SUCCESS ||
                     IoStatus->Status == STATUS_BUFFER_OVERFLOW ||
                     IoStatus->Status == STATUS_END_OF_FILE);
}

/* Ends a request the file system finished without a packet */
static
NTSTATUS
IopFinishFastIo(
    _In_ PIOP_REQUEST Request,
    _In_ PIO_STATUS_BLOCK IoStatus,
    _Out_ PIO_STATUS_BLOCK UserIosb,
    _In_ IOP_TRANSFER_TYPE TransferType)
{
    NTSTATUS Status = IoStatus->Status;

    IopUpdateOperationCount(TransferType);

    _SEH2_TRY
    {
        IopWriteIoStatusBlock(UserIosb,
                              IoStatus,
                              IopIs32BitFileIo(Request->FileObject,
                                               Request->PreviousMode));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (Request->EventObject != NULL)
    {
        KeSetEvent(Request->EventObject, 0, FALSE);
    }

    IopAbortRequest(Request);

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
NtReadFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_opt_ PULONG Key)
{
    LARGE_INTEGER CapturedOffset;
    IO_STATUS_BLOCK FastIoStatus;
    PFILE_OBJECT FileObject;
    IOP_REQUEST Request;
    ULONG CapturedKey = 0;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    /* The buffer is probed before the caller's event is reset: a bad buffer leaves the event as it was */
    if (ExGetPreviousMode() != KernelMode)
    {
        Status = ObReferenceObjectByHandle(FileHandle,
                                           FILE_READ_DATA,
                                           IoFileObjectType,
                                           UserMode,
                                           (PVOID *)&FileObject,
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;

        _SEH2_TRY
        {
            ProbeForWrite(Buffer, Length, sizeof(UCHAR));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        ObDereferenceObject(FileObject);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = IopBeginRequest(&Request,
                             FileHandle,
                             FILE_READ_DATA,
                             Event,
                             IoStatusBlock,
                             IRP_MJ_READ,
                             TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopSetAsyncParameters(&Request, ApcRoutine, ApcContext);
    if (!NT_SUCCESS(Status)) return Status;

    Irp = Request.Irp;

    if (Request.PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (ByteOffset != NULL)
            {
                CapturedOffset = ProbeForReadLargeInteger(ByteOffset);
            }
            else
            {
                CapturedOffset.QuadPart = 0;
            }

            if (Key != NULL) CapturedKey = ProbeForReadUlong(Key);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IopAbortRequest(&Request);
            return Status;
        }
    }
    else
    {
        if (ByteOffset != NULL)
        {
            CapturedOffset = *ByteOffset;
        }
        else
        {
            CapturedOffset.QuadPart = 0;
        }

        if (Key != NULL) CapturedKey = *Key;
    }

    if (ByteOffset == NULL ||
        (CapturedOffset.HighPart == -1 &&
         CapturedOffset.LowPart == FILE_USE_FILE_POINTER_POSITION))
    {
        /* Pipes and mailslots have no position, so they need no offset */
        if (!(Request.FileObject->Flags &
              (FO_SYNCHRONOUS_IO | FO_NAMED_PIPE | FO_MAILSLOT)))
        {
            IopAbortRequest(&Request);
            return STATUS_INVALID_PARAMETER;
        }

        CapturedOffset = Request.FileObject->CurrentByteOffset;
    }

    if (!IopIsTransferValid(&Request, FALSE, Buffer, Length, &CapturedOffset))
    {
        IopAbortRequest(&Request);
        return STATUS_INVALID_PARAMETER;
    }

    if (IopTryFastIo(&Request,
                     FALSE,
                     &CapturedOffset,
                     Length,
                     CapturedKey,
                     Buffer,
                     &FastIoStatus))
    {
        Request.FileObject->CurrentByteOffset.QuadPart =
            CapturedOffset.QuadPart + FastIoStatus.Information;

        return IopFinishFastIo(&Request,
                               &FastIoStatus,
                               IoStatusBlock,
                               IopReadTransfer);
    }

    Irp->Flags = IRP_READ_OPERATION | IRP_DEFER_IO_COMPLETION;
    if (Request.FileObject->Flags & FO_NO_INTERMEDIATE_BUFFERING)
    {
        Irp->Flags |= IRP_NOCACHE;
    }

    Request.StackPtr->Parameters.Read.Length = Length;
    Request.StackPtr->Parameters.Read.Key = CapturedKey;
    Request.StackPtr->Parameters.Read.ByteOffset = CapturedOffset;

    if (Request.DeviceObject->Flags & DO_BUFFERED_IO)
    {
        if (Length != 0)
        {
            Irp->AssociatedIrp.SystemBuffer =
                ExAllocatePoolWithQuotaTag(NonPagedPool |
                                               POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                           Length,
                                           TAG_IOBUF);
            if (Irp->AssociatedIrp.SystemBuffer == NULL)
            {
                IopAbortRequest(&Request);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Irp->Flags |= IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER |
                          IRP_INPUT_OPERATION;
        }

        Irp->UserBuffer = Buffer;
    }
    else if (Request.DeviceObject->Flags & DO_DIRECT_IO)
    {
        if (Length != 0)
        {
            PMDL Mdl;

            Mdl = IoAllocateMdl(Buffer, Length, FALSE, TRUE, Irp);
            if (Mdl == NULL)
            {
                IopAbortRequest(&Request);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            _SEH2_TRY
            {
                MmProbeAndLockPages(Mdl, Request.PreviousMode, IoWriteAccess);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (!NT_SUCCESS(Status))
            {
                IopAbortRequest(&Request);
                return Status;
            }
        }

        Irp->UserBuffer = Buffer;
    }
    else
    {
        Irp->UserBuffer = Buffer;
    }

    return IopIssueRequest(&Request, IopReadTransfer);
}

NTSTATUS
NTAPI
NtWriteFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_opt_ PLARGE_INTEGER ByteOffset,
    _In_opt_ PULONG Key)
{
    LARGE_INTEGER CapturedOffset;
    IO_STATUS_BLOCK FastIoStatus;
    ACCESS_MASK DesiredAccess;
    IOP_REQUEST Request;
    ULONG CapturedKey = 0;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    DesiredAccess = FILE_WRITE_DATA;

    Status = IopBeginRequest(&Request,
                             FileHandle,
                             DesiredAccess,
                             Event,
                             IoStatusBlock,
                             IRP_MJ_WRITE,
                             TRUE);
    if (Status == STATUS_ACCESS_DENIED)
    {
        Status = IopBeginRequest(&Request,
                                 FileHandle,
                                 FILE_APPEND_DATA,
                                 Event,
                                 IoStatusBlock,
                                 IRP_MJ_WRITE,
                                 TRUE);
    }

    if (!NT_SUCCESS(Status)) return Status;

    Status = IopSetAsyncParameters(&Request, ApcRoutine, ApcContext);
    if (!NT_SUCCESS(Status)) return Status;

    Irp = Request.Irp;

    if (Request.PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(Buffer, Length, sizeof(UCHAR));

            if (ByteOffset != NULL)
            {
                CapturedOffset = ProbeForReadLargeInteger(ByteOffset);
            }
            else
            {
                CapturedOffset.QuadPart = 0;
            }

            if (Key != NULL) CapturedKey = ProbeForReadUlong(Key);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IopAbortRequest(&Request);
            return Status;
        }
    }
    else
    {
        if (ByteOffset != NULL)
        {
            CapturedOffset = *ByteOffset;
        }
        else
        {
            CapturedOffset.QuadPart = 0;
        }

        if (Key != NULL) CapturedKey = *Key;
    }

    if (ByteOffset == NULL ||
        (CapturedOffset.HighPart == -1 &&
         CapturedOffset.LowPart == FILE_USE_FILE_POINTER_POSITION))
    {
        /* Pipes and mailslots have no position, so they need no offset */
        if (!(Request.FileObject->Flags &
              (FO_SYNCHRONOUS_IO | FO_NAMED_PIPE | FO_MAILSLOT)))
        {
            IopAbortRequest(&Request);
            return STATUS_INVALID_PARAMETER;
        }

        CapturedOffset = Request.FileObject->CurrentByteOffset;
    }

    if (!IopIsTransferValid(&Request, TRUE, Buffer, Length, &CapturedOffset))
    {
        IopAbortRequest(&Request);
        return STATUS_INVALID_PARAMETER;
    }

    if (IopTryFastIo(&Request,
                     TRUE,
                     &CapturedOffset,
                     Length,
                     CapturedKey,
                     Buffer,
                     &FastIoStatus))
    {
        /* A write to the end of the file has no offset to count from; the file system moved the position itself */
        if (!(CapturedOffset.HighPart == -1 &&
              CapturedOffset.LowPart == FILE_WRITE_TO_END_OF_FILE))
        {
            Request.FileObject->CurrentByteOffset.QuadPart =
                CapturedOffset.QuadPart + FastIoStatus.Information;
        }

        return IopFinishFastIo(&Request,
                               &FastIoStatus,
                               IoStatusBlock,
                               IopWriteTransfer);
    }

    Irp->Flags = IRP_WRITE_OPERATION | IRP_DEFER_IO_COMPLETION;
    if (Request.FileObject->Flags & FO_NO_INTERMEDIATE_BUFFERING)
    {
        Irp->Flags |= IRP_NOCACHE;
    }

    Request.StackPtr->Parameters.Write.Length = Length;
    Request.StackPtr->Parameters.Write.Key = CapturedKey;
    Request.StackPtr->Parameters.Write.ByteOffset = CapturedOffset;

    if (Request.DeviceObject->Flags & DO_BUFFERED_IO)
    {
        if (Length != 0)
        {
            Irp->AssociatedIrp.SystemBuffer =
                ExAllocatePoolWithQuotaTag(NonPagedPool |
                                               POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                           Length,
                                           TAG_IOBUF);
            if (Irp->AssociatedIrp.SystemBuffer == NULL)
            {
                IopAbortRequest(&Request);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Irp->Flags |= IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER;

            _SEH2_TRY
            {
                RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, Buffer, Length);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (!NT_SUCCESS(Status))
            {
                IopAbortRequest(&Request);
                return Status;
            }
        }
    }
    else if (Request.DeviceObject->Flags & DO_DIRECT_IO)
    {
        if (Length != 0)
        {
            PMDL Mdl;

            Mdl = IoAllocateMdl(Buffer, Length, FALSE, TRUE, Irp);
            if (Mdl == NULL)
            {
                IopAbortRequest(&Request);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            _SEH2_TRY
            {
                MmProbeAndLockPages(Mdl, Request.PreviousMode, IoReadAccess);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (!NT_SUCCESS(Status))
            {
                IopAbortRequest(&Request);
                return Status;
            }
        }
    }
    else
    {
        Irp->UserBuffer = Buffer;
    }

    return IopIssueRequest(&Request, IopWriteTransfer);
}

static
NTSTATUS
IopScatterGather(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ FILE_SEGMENT_ELEMENT Segments[],
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_opt_ PULONG Key,
    _In_ BOOLEAN Write)
{
    LARGE_INTEGER CapturedOffset;
    ULONG CapturedKey = 0;
    IOP_REQUEST Request;
    PVOID FirstPage;
    NTSTATUS Status;
    ULONG PageCount;
    PMDL Mdl;
    PIRP Irp;

    PAGED_CODE();

    PageCount = BYTES_TO_PAGES(Length);
    if (PageCount == 0) return STATUS_INVALID_PARAMETER;

    Status = IopBeginRequest(&Request,
                             FileHandle,
                             Write ? FILE_WRITE_DATA : FILE_READ_DATA,
                             Event,
                             IoStatusBlock,
                             Write ? IRP_MJ_WRITE : IRP_MJ_READ,
                             TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopSetAsyncParameters(&Request, ApcRoutine, ApcContext);
    if (!NT_SUCCESS(Status)) return Status;

    Irp = Request.Irp;

    if (Request.PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(Segments,
                         PageCount * sizeof(FILE_SEGMENT_ELEMENT),
                         sizeof(ULONG_PTR));
            FirstPage = Segments[0].Buffer;
            CapturedOffset = ProbeForReadLargeInteger(ByteOffset);
            if (Key != NULL) CapturedKey = ProbeForReadUlong(Key);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IopAbortRequest(&Request);
            return Status;
        }
    }
    else
    {
        FirstPage = Segments[0].Buffer;
        CapturedOffset = *ByteOffset;
        if (Key != NULL) CapturedKey = *Key;
    }

    if ((Request.FileObject->Flags & FO_SYNCHRONOUS_IO) ||
        !(Request.FileObject->Flags & FO_NO_INTERMEDIATE_BUFFERING) ||
        !IopIsTransferValid(&Request, Write, FirstPage, Length, &CapturedOffset))
    {
        IopAbortRequest(&Request);
        return STATUS_INVALID_PARAMETER;
    }

    Mdl = IoAllocateMdl(FirstPage, Length, FALSE, TRUE, Irp);
    if (Mdl == NULL)
    {
        IopAbortRequest(&Request);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        MmProbeAndLockSelectedPages(Mdl,
                                    Segments,
                                    Request.PreviousMode,
                                    Write ? IoReadAccess : IoWriteAccess);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        IopAbortRequest(&Request);
        return Status;
    }

    Irp->Flags = IRP_NOCACHE | IRP_DEFER_IO_COMPLETION |
                 (Write ? IRP_WRITE_OPERATION : IRP_READ_OPERATION);

    Request.StackPtr->Parameters.Read.Length = Length;
    Request.StackPtr->Parameters.Read.Key = CapturedKey;
    Request.StackPtr->Parameters.Read.ByteOffset = CapturedOffset;

    return IopIssueRequest(&Request,
                           Write ? IopWriteTransfer : IopReadTransfer);
}

NTSTATUS
NTAPI
NtReadFileScatter(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE UserApcRoutine,
    _In_opt_ PVOID UserApcContext,
    _Out_ PIO_STATUS_BLOCK UserIoStatusBlock,
    _In_ FILE_SEGMENT_ELEMENT BufferDescription[],
    _In_ ULONG BufferLength,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_opt_ PULONG Key)
{
    return IopScatterGather(FileHandle,
                            Event,
                            UserApcRoutine,
                            UserApcContext,
                            UserIoStatusBlock,
                            BufferDescription,
                            BufferLength,
                            ByteOffset,
                            Key,
                            FALSE);
}

NTSTATUS
NTAPI
NtWriteFileGather(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ FILE_SEGMENT_ELEMENT BufferDescription[],
    _In_ ULONG BufferLength,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_opt_ PULONG Key)
{
    return IopScatterGather(FileHandle,
                            Event,
                            ApcRoutine,
                            ApcContext,
                            IoStatusBlock,
                            BufferDescription,
                            BufferLength,
                            ByteOffset,
                            Key,
                            TRUE);
}

NTSTATUS
NTAPI
NtFlushBuffersFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    IOP_REQUEST Request;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopBeginRequestEitherAccess(&Request,
                                         FileHandle,
                                         FILE_WRITE_DATA,
                                         FILE_APPEND_DATA,
                                         NULL,
                                         IoStatusBlock,
                                         IRP_MJ_FLUSH_BUFFERS);
    if (!NT_SUCCESS(Status)) return Status;

    Request.Irp->Flags = IRP_SYNCHRONOUS_API | IRP_DEFER_IO_COMPLETION;

    return IopIssueRequest(&Request, IopOtherTransfer);
}

NTSTATUS
NTAPI
NtFlushBuffersFileEx(
    _In_ HANDLE FileHandle,
    _In_ ULONG Flags,
    _In_reads_bytes_(ParametersSize) PVOID Parameters,
    _In_ ULONG ParametersSize,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    IOP_REQUEST Request;
    NTSTATUS Status;

    PAGED_CODE();

    /* TODO: the flush flag names are not published; only zero is accepted */
    if (Flags != 0) return STATUS_INVALID_PARAMETER;

    if (ParametersSize != 0 && Parameters == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = IopBeginRequestEitherAccess(&Request,
                                         FileHandle,
                                         FILE_WRITE_DATA,
                                         FILE_APPEND_DATA,
                                         NULL,
                                         IoStatusBlock,
                                         IRP_MJ_FLUSH_BUFFERS);
    if (!NT_SUCCESS(Status)) return Status;

    Request.Irp->Flags = IRP_SYNCHRONOUS_API | IRP_DEFER_IO_COMPLETION;
    Request.StackPtr->Flags = (UCHAR)Flags;

    return IopIssueRequest(&Request, IopOtherTransfer);
}

NTSTATUS
NTAPI
NtLockFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_ PLARGE_INTEGER Length,
    _In_ ULONG Key,
    _In_ BOOLEAN FailImmediately,
    _In_ BOOLEAN ExclusiveLock)
{
    LARGE_INTEGER CapturedOffset;
    LARGE_INTEGER CapturedLength;
    IOP_REQUEST Request;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopBeginRequestEitherAccess(&Request,
                                         FileHandle,
                                         FILE_READ_DATA,
                                         FILE_WRITE_DATA,
                                         Event,
                                         IoStatusBlock,
                                         IRP_MJ_LOCK_CONTROL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopSetAsyncParameters(&Request, ApcRoutine, ApcContext);
    if (!NT_SUCCESS(Status)) return Status;

    if (Request.PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            CapturedOffset = ProbeForReadLargeInteger(ByteOffset);
            CapturedLength = ProbeForReadLargeInteger(Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IopAbortRequest(&Request);
            return Status;
        }
    }
    else
    {
        CapturedOffset = *ByteOffset;
        CapturedLength = *Length;
    }

    Request.Irp->Flags = 0;
    Request.StackPtr->MinorFunction = IRP_MN_LOCK;
    Request.StackPtr->Flags = 0;

    if (FailImmediately) Request.StackPtr->Flags |= SL_FAIL_IMMEDIATELY;
    if (ExclusiveLock) Request.StackPtr->Flags |= SL_EXCLUSIVE_LOCK;

    Request.StackPtr->Parameters.LockControl.Length = &CapturedLength;
    Request.StackPtr->Parameters.LockControl.ByteOffset = CapturedOffset;
    Request.StackPtr->Parameters.LockControl.Key = Key;

    Request.FileObject->LockOperation = TRUE;

    return IopIssueRequest(&Request, IopOtherTransfer);
}

NTSTATUS
NTAPI
NtUnlockFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_ PLARGE_INTEGER Length,
    _In_ ULONG Key)
{
    LARGE_INTEGER CapturedOffset;
    LARGE_INTEGER CapturedLength;
    IOP_REQUEST Request;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopBeginRequestEitherAccess(&Request,
                                         FileHandle,
                                         FILE_READ_DATA,
                                         FILE_WRITE_DATA,
                                         NULL,
                                         IoStatusBlock,
                                         IRP_MJ_LOCK_CONTROL);
    if (!NT_SUCCESS(Status)) return Status;

    if (Request.PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            CapturedOffset = ProbeForReadLargeInteger(ByteOffset);
            CapturedLength = ProbeForReadLargeInteger(Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IopAbortRequest(&Request);
            return Status;
        }
    }
    else
    {
        CapturedOffset = *ByteOffset;
        CapturedLength = *Length;
    }

    Request.Irp->Flags = IRP_SYNCHRONOUS_API;
    Request.StackPtr->MinorFunction = IRP_MN_UNLOCK_SINGLE;
    Request.StackPtr->Parameters.LockControl.Length = &CapturedLength;
    Request.StackPtr->Parameters.LockControl.ByteOffset = CapturedOffset;
    Request.StackPtr->Parameters.LockControl.Key = Key;

    return IopIssueRequest(&Request, IopOtherTransfer);
}

NTSTATUS
IopDeviceFsIoControl(
    _In_ HANDLE DeviceHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE UserApcRoutine,
    _In_opt_ PVOID UserApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _In_ BOOLEAN IsDevIoCtl)
{
    ACCESS_MASK DesiredAccess;
    IOP_REQUEST Request;
    NTSTATUS Status;
    ULONG Method;
    PIRP Irp;

    PAGED_CODE();

    Method = IO_METHOD_FROM_CTL_CODE(IoControlCode);

    DesiredAccess = (ACCESS_MASK)((IoControlCode >> 14) & 0x3);
    if (DesiredAccess == FILE_ANY_ACCESS) DesiredAccess = 0;

    Status = IopBeginRequest(&Request,
                             DeviceHandle,
                             DesiredAccess,
                             Event,
                             IoStatusBlock,
                             IsDevIoCtl ? IRP_MJ_DEVICE_CONTROL
                                        : IRP_MJ_FILE_SYSTEM_CONTROL,
                             TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopSetAsyncParameters(&Request, UserApcRoutine, UserApcContext);
    if (!NT_SUCCESS(Status)) return Status;

    Irp = Request.Irp;
    Irp->Flags = IRP_DEFER_IO_COMPLETION;

    Request.StackPtr->Parameters.DeviceIoControl.IoControlCode = IoControlCode;
    Request.StackPtr->Parameters.DeviceIoControl.InputBufferLength =
        InputBufferLength;
    Request.StackPtr->Parameters.DeviceIoControl.OutputBufferLength =
        OutputBufferLength;

    if (Method == METHOD_BUFFERED)
    {
        ULONG BufferLength = max(InputBufferLength, OutputBufferLength);

        if (BufferLength != 0)
        {
            Irp->AssociatedIrp.SystemBuffer =
                ExAllocatePoolWithQuotaTag(NonPagedPool |
                                               POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                           BufferLength,
                                           TAG_IOBUF);
            if (Irp->AssociatedIrp.SystemBuffer == NULL)
            {
                IopAbortRequest(&Request);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Irp->Flags |= IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER;

            if (InputBuffer != NULL && InputBufferLength != 0)
            {
                _SEH2_TRY
                {
                    if (Request.PreviousMode != KernelMode)
                    {
                        ProbeForRead(InputBuffer,
                                     InputBufferLength,
                                     sizeof(UCHAR));
                    }

                    RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer,
                                  InputBuffer,
                                  InputBufferLength);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;

                if (!NT_SUCCESS(Status))
                {
                    IopAbortRequest(&Request);
                    return Status;
                }
            }

            if (OutputBuffer != NULL && OutputBufferLength != 0)
            {
                if (Request.PreviousMode != KernelMode)
                {
                    _SEH2_TRY
                    {
                        ProbeForWrite(OutputBuffer,
                                      OutputBufferLength,
                                      sizeof(UCHAR));
                    }
                    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                    {
                        Status = _SEH2_GetExceptionCode();
                    }
                    _SEH2_END;

                    if (!NT_SUCCESS(Status))
                    {
                        IopAbortRequest(&Request);
                        return Status;
                    }
                }

                Irp->Flags |= IRP_INPUT_OPERATION;
            }
        }

        Irp->UserBuffer = OutputBuffer;
    }
    else if (Method == METHOD_IN_DIRECT || Method == METHOD_OUT_DIRECT)
    {
        if (InputBuffer != NULL && InputBufferLength != 0)
        {
            Irp->AssociatedIrp.SystemBuffer =
                ExAllocatePoolWithQuotaTag(NonPagedPool |
                                               POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                           InputBufferLength,
                                           TAG_IOBUF);
            if (Irp->AssociatedIrp.SystemBuffer == NULL)
            {
                IopAbortRequest(&Request);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Irp->Flags |= IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER;

            _SEH2_TRY
            {
                if (Request.PreviousMode != KernelMode)
                {
                    ProbeForRead(InputBuffer,
                                 InputBufferLength,
                                 sizeof(UCHAR));
                }

                RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer,
                              InputBuffer,
                              InputBufferLength);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (!NT_SUCCESS(Status))
            {
                IopAbortRequest(&Request);
                return Status;
            }
        }

        if (OutputBuffer != NULL && OutputBufferLength != 0)
        {
            PMDL Mdl;

            Mdl = IoAllocateMdl(OutputBuffer,
                                OutputBufferLength,
                                FALSE,
                                TRUE,
                                Irp);
            if (Mdl == NULL)
            {
                IopAbortRequest(&Request);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            _SEH2_TRY
            {
                MmProbeAndLockPages(Mdl,
                                    Request.PreviousMode,
                                    (Method == METHOD_IN_DIRECT)
                                        ? IoReadAccess
                                        : IoWriteAccess);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (!NT_SUCCESS(Status))
            {
                IopAbortRequest(&Request);
                return Status;
            }
        }
    }
    else
    {
        if (Request.PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                if (InputBuffer != NULL && InputBufferLength != 0)
                {
                    ProbeForRead(InputBuffer,
                                 InputBufferLength,
                                 sizeof(UCHAR));
                }

                if (OutputBuffer != NULL && OutputBufferLength != 0)
                {
                    ProbeForWrite(OutputBuffer,
                                  OutputBufferLength,
                                  sizeof(UCHAR));
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (!NT_SUCCESS(Status))
            {
                IopAbortRequest(&Request);
                return Status;
            }
        }

        Request.StackPtr->Parameters.DeviceIoControl.Type3InputBuffer =
            InputBuffer;
        Irp->UserBuffer = OutputBuffer;
    }

    return IopIssueRequest(&Request, IopOtherTransfer);
}

NTSTATUS
NTAPI
NtDeviceIoControlFile(
    _In_ HANDLE DeviceHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength)
{
    return IopDeviceFsIoControl(DeviceHandle,
                                Event,
                                ApcRoutine,
                                ApcContext,
                                IoStatusBlock,
                                IoControlCode,
                                InputBuffer,
                                InputBufferLength,
                                OutputBuffer,
                                OutputBufferLength,
                                TRUE);
}

NTSTATUS
NTAPI
NtFsControlFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG FsControlCode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength)
{
    return IopDeviceFsIoControl(FileHandle,
                                Event,
                                ApcRoutine,
                                ApcContext,
                                IoStatusBlock,
                                FsControlCode,
                                InputBuffer,
                                InputBufferLength,
                                OutputBuffer,
                                OutputBufferLength,
                                FALSE);
}

static
NTSTATUS
IopNotifyChangeDirectory(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _In_ ULONG CompletionFilter,
    _In_ BOOLEAN WatchTree,
    _In_ ULONG NotifyInformationClass)
{
    IOP_REQUEST Request;
    NTSTATUS Status;
    PIRP Irp;
    PMDL Mdl;

    PAGED_CODE();

    if (ExGetPreviousMode() != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteIoStatusBlock(IoStatusBlock);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    if (CompletionFilter == 0 ||
        (CompletionFilter & ~FILE_NOTIFY_VALID_MASK) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = IopBeginRequest(&Request,
                             FileHandle,
                             FILE_LIST_DIRECTORY,
                             Event,
                             IoStatusBlock,
                             IRP_MJ_DIRECTORY_CONTROL,
                             TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopSetAsyncParameters(&Request, ApcRoutine, ApcContext);
    if (!NT_SUCCESS(Status)) return Status;

    Irp = Request.Irp;
    Irp->Flags = IRP_BUFFERED_IO;

    Request.StackPtr->MinorFunction = IRP_MN_NOTIFY_CHANGE_DIRECTORY;
    Request.StackPtr->Flags = WatchTree ? SL_WATCH_TREE : 0;
    Request.StackPtr->Parameters.NotifyDirectory.CompletionFilter =
        CompletionFilter;
    Request.StackPtr->Parameters.NotifyDirectory.Length = BufferSize;

    if (NotifyInformationClass != 0)
    {
        Request.StackPtr->MinorFunction =
            IRP_MN_NOTIFY_CHANGE_DIRECTORY_EX;
    }

    Irp->UserBuffer = Buffer;

    if (BufferSize != 0)
    {
        Mdl = IoAllocateMdl(Buffer, BufferSize, FALSE, TRUE, Irp);
        if (Mdl == NULL)
        {
            IopAbortRequest(&Request);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        _SEH2_TRY
        {
            MmProbeAndLockPages(Mdl, Request.PreviousMode, IoWriteAccess);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IopAbortRequest(&Request);
            return Status;
        }
    }

    return IopIssueRequest(&Request, IopOtherTransfer);
}

NTSTATUS
NTAPI
NtNotifyChangeDirectoryFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_ PVOID Buffer,
    _In_ ULONG BufferSize,
    _In_ ULONG CompletionFilter,
    _In_ BOOLEAN WatchTree)
{
    return IopNotifyChangeDirectory(FileHandle,
                                    Event,
                                    ApcRoutine,
                                    ApcContext,
                                    IoStatusBlock,
                                    Buffer,
                                    BufferSize,
                                    CompletionFilter,
                                    WatchTree,
                                    0);
}

NTSTATUS
NTAPI
NtNotifyChangeDirectoryFileEx(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _In_ ULONG CompletionFilter,
    _In_ BOOLEAN WatchTree,
    _In_ DIRECTORY_NOTIFY_INFORMATION_CLASS DirectoryNotifyInformationClass)
{
    if (DirectoryNotifyInformationClass != DirectoryNotifyInformation &&
        DirectoryNotifyInformationClass != DirectoryNotifyExtendedInformation)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return IopNotifyChangeDirectory(FileHandle,
                                    Event,
                                    ApcRoutine,
                                    ApcContext,
                                    IoStatusBlock,
                                    Buffer,
                                    BufferSize,
                                    CompletionFilter,
                                    WatchTree,
                                    (ULONG)DirectoryNotifyInformationClass);
}

static
NTSTATUS
IopQueryDirectoryFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ ULONG QueryFlags,
    _In_opt_ PUNICODE_STRING FileName)
{
    UNICODE_STRING CapturedName;
    IOP_REQUEST Request;
    NTSTATUS Status;
    PIRP Irp;
    PMDL Mdl;

    PAGED_CODE();

    if ((ULONG)FileInformationClass >= FileMaximumInformation ||
        IopQueryDirectoryLength[FileInformationClass] == 0)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (Length < IopQueryDirectoryLength[FileInformationClass])
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    RtlInitEmptyUnicodeString(&CapturedName, NULL, 0);

    Status = IopBeginRequest(&Request,
                             FileHandle,
                             FILE_LIST_DIRECTORY,
                             Event,
                             IoStatusBlock,
                             IRP_MJ_DIRECTORY_CONTROL,
                             TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IopSetAsyncParameters(&Request, ApcRoutine, ApcContext);
    if (!NT_SUCCESS(Status)) return Status;

    Irp = Request.Irp;

    if (FileName != NULL)
    {
        if (Request.PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                CapturedName = ProbeForReadUnicodeString(FileName);
                if (CapturedName.Length != 0)
                {
                    ProbeForRead(CapturedName.Buffer,
                                 CapturedName.Length,
                                 sizeof(WCHAR));
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (!NT_SUCCESS(Status))
            {
                IopAbortRequest(&Request);
                return Status;
            }
        }
        else
        {
            CapturedName = *FileName;
        }
    }

    Irp->Flags = IRP_BUFFERED_IO | IRP_DEFER_IO_COMPLETION;

    Request.StackPtr->MinorFunction = IRP_MN_QUERY_DIRECTORY;
    Request.StackPtr->Flags = (UCHAR)QueryFlags;
    Request.StackPtr->Parameters.QueryDirectory.Length = Length;
    Request.StackPtr->Parameters.QueryDirectory.FileInformationClass =
        FileInformationClass;
    Request.StackPtr->Parameters.QueryDirectory.FileName =
        (FileName != NULL) ? &CapturedName : NULL;
    Request.StackPtr->Parameters.QueryDirectory.FileIndex = 0;

    Irp->UserBuffer = FileInformation;

    if (Length != 0)
    {
        Mdl = IoAllocateMdl(FileInformation, Length, FALSE, TRUE, Irp);
        if (Mdl == NULL)
        {
            IopAbortRequest(&Request);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        _SEH2_TRY
        {
            MmProbeAndLockPages(Mdl, Request.PreviousMode, IoWriteAccess);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IopAbortRequest(&Request);
            return Status;
        }
    }

    return IopIssueRequest(&Request, IopOtherTransfer);
}

NTSTATUS
NTAPI
NtQueryDirectoryFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ BOOLEAN ReturnSingleEntry,
    _In_opt_ PUNICODE_STRING FileName,
    _In_ BOOLEAN RestartScan)
{
    ULONG Flags = 0;

    if (ReturnSingleEntry) Flags |= SL_RETURN_SINGLE_ENTRY;
    if (RestartScan) Flags |= SL_RESTART_SCAN;

    return IopQueryDirectoryFile(FileHandle,
                                 Event,
                                 ApcRoutine,
                                 ApcContext,
                                 IoStatusBlock,
                                 FileInformation,
                                 Length,
                                 FileInformationClass,
                                 Flags,
                                 FileName);
}

NTSTATUS
NTAPI
NtQueryDirectoryFileEx(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_writes_bytes_(Length) PVOID FileInformation,
    _In_ ULONG Length,
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ ULONG QueryFlags,
    _In_opt_ PUNICODE_STRING FileName)
{
    if (QueryFlags & ~(SL_RESTART_SCAN | SL_RETURN_SINGLE_ENTRY |
                       SL_INDEX_SPECIFIED))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return IopQueryDirectoryFile(FileHandle,
                                 Event,
                                 ApcRoutine,
                                 ApcContext,
                                 IoStatusBlock,
                                 FileInformation,
                                 Length,
                                 FileInformationClass,
                                 QueryFlags,
                                 FileName);
}

NTSTATUS
NTAPI
NtQueryEaFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_ PVOID Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN ReturnSingleEntry,
    _In_opt_ PVOID EaList,
    _In_ ULONG EaListLength,
    _In_opt_ PULONG EaIndex,
    _In_ BOOLEAN RestartScan)
{
    IOP_REQUEST Request;
    ULONG CapturedIndex = 0;
    NTSTATUS Status;
    PIRP Irp;
    PMDL Mdl;

    PAGED_CODE();

    Status = IopBeginRequest(&Request,
                             FileHandle,
                             FILE_READ_EA,
                             NULL,
                             IoStatusBlock,
                             IRP_MJ_QUERY_EA,
                             TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Irp = Request.Irp;

    if (EaIndex != NULL)
    {
        if (Request.PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                CapturedIndex = ProbeForReadUlong(EaIndex);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (!NT_SUCCESS(Status))
            {
                IopAbortRequest(&Request);
                return Status;
            }
        }
        else
        {
            CapturedIndex = *EaIndex;
        }
    }

    Irp->Flags = IRP_BUFFERED_IO | IRP_SYNCHRONOUS_API;

    Request.StackPtr->Flags = 0;
    if (ReturnSingleEntry) Request.StackPtr->Flags |= SL_RETURN_SINGLE_ENTRY;
    if (RestartScan) Request.StackPtr->Flags |= SL_RESTART_SCAN;
    if (EaIndex != NULL) Request.StackPtr->Flags |= SL_INDEX_SPECIFIED;

    Request.StackPtr->Parameters.QueryEa.Length = Length;
    Request.StackPtr->Parameters.QueryEa.EaList = EaList;
    Request.StackPtr->Parameters.QueryEa.EaListLength = EaListLength;
    Request.StackPtr->Parameters.QueryEa.EaIndex = CapturedIndex;

    Irp->UserBuffer = Buffer;

    if (Length != 0)
    {
        Mdl = IoAllocateMdl(Buffer, Length, FALSE, TRUE, Irp);
        if (Mdl == NULL)
        {
            IopAbortRequest(&Request);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        _SEH2_TRY
        {
            MmProbeAndLockPages(Mdl, Request.PreviousMode, IoWriteAccess);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IopAbortRequest(&Request);
            return Status;
        }
    }

    return IopIssueRequest(&Request, IopOtherTransfer);
}

NTSTATUS
NTAPI
NtSetEaFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ PVOID EaBuffer,
    _In_ ULONG EaBufferSize)
{
    IOP_REQUEST Request;
    NTSTATUS Status;
    PIRP Irp;
    PMDL Mdl;

    PAGED_CODE();

    Status = IopBeginRequest(&Request,
                             FileHandle,
                             FILE_WRITE_EA,
                             NULL,
                             IoStatusBlock,
                             IRP_MJ_SET_EA,
                             TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Irp = Request.Irp;
    Irp->Flags = IRP_BUFFERED_IO | IRP_SYNCHRONOUS_API;

    Request.StackPtr->Parameters.SetEa.Length = EaBufferSize;

    Irp->UserBuffer = EaBuffer;

    if (EaBufferSize != 0)
    {
        Mdl = IoAllocateMdl(EaBuffer, EaBufferSize, FALSE, TRUE, Irp);
        if (Mdl == NULL)
        {
            IopAbortRequest(&Request);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        _SEH2_TRY
        {
            MmProbeAndLockPages(Mdl, Request.PreviousMode, IoReadAccess);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IopAbortRequest(&Request);
            return Status;
        }
    }

    return IopIssueRequest(&Request, IopOtherTransfer);
}

NTSTATUS
NTAPI
NtQueryQuotaInformationFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _Out_ PVOID Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN ReturnSingleEntry,
    _In_opt_ PVOID SidList,
    _In_ ULONG SidListLength,
    _In_opt_ PSID StartSid,
    _In_ BOOLEAN RestartScan)
{
    IOP_REQUEST Request;
    NTSTATUS Status;
    PIRP Irp;
    PMDL Mdl;

    PAGED_CODE();

    Status = IopBeginRequest(&Request,
                             FileHandle,
                             FILE_READ_DATA,
                             NULL,
                             IoStatusBlock,
                             IRP_MJ_QUERY_QUOTA,
                             TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Irp = Request.Irp;
    Irp->Flags = IRP_BUFFERED_IO | IRP_SYNCHRONOUS_API;

    Request.StackPtr->Flags = 0;
    if (ReturnSingleEntry) Request.StackPtr->Flags |= SL_RETURN_SINGLE_ENTRY;
    if (RestartScan) Request.StackPtr->Flags |= SL_RESTART_SCAN;
    if (StartSid != NULL) Request.StackPtr->Flags |= SL_INDEX_SPECIFIED;

    Request.StackPtr->Parameters.QueryQuota.Length = Length;
    Request.StackPtr->Parameters.QueryQuota.SidList = SidList;
    Request.StackPtr->Parameters.QueryQuota.SidListLength = SidListLength;
    Request.StackPtr->Parameters.QueryQuota.StartSid = StartSid;

    Irp->UserBuffer = Buffer;

    if (Length != 0)
    {
        Mdl = IoAllocateMdl(Buffer, Length, FALSE, TRUE, Irp);
        if (Mdl == NULL)
        {
            IopAbortRequest(&Request);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        _SEH2_TRY
        {
            MmProbeAndLockPages(Mdl, Request.PreviousMode, IoWriteAccess);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IopAbortRequest(&Request);
            return Status;
        }
    }

    return IopIssueRequest(&Request, IopOtherTransfer);
}

NTSTATUS
NTAPI
NtSetQuotaInformationFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength)
{
    IOP_REQUEST Request;
    NTSTATUS Status;
    PIRP Irp;
    PMDL Mdl;

    PAGED_CODE();

    Status = IopBeginRequest(&Request,
                             FileHandle,
                             FILE_WRITE_DATA,
                             NULL,
                             IoStatusBlock,
                             IRP_MJ_SET_QUOTA,
                             TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Irp = Request.Irp;
    Irp->Flags = IRP_BUFFERED_IO | IRP_SYNCHRONOUS_API;

    Request.StackPtr->Parameters.SetQuota.Length = BufferLength;

    Irp->UserBuffer = Buffer;

    if (BufferLength != 0)
    {
        Mdl = IoAllocateMdl(Buffer, BufferLength, FALSE, TRUE, Irp);
        if (Mdl == NULL)
        {
            IopAbortRequest(&Request);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        _SEH2_TRY
        {
            MmProbeAndLockPages(Mdl, Request.PreviousMode, IoReadAccess);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            IopAbortRequest(&Request);
            return Status;
        }
    }

    return IopIssueRequest(&Request, IopOtherTransfer);
}

NTSTATUS
NTAPI
NtFlushWriteBuffer(VOID)
{
    PAGED_CODE();

    KeFlushWriteBuffer();

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtFlushProcessWriteBuffers(VOID)
{
    PAGED_CODE();

    /* TODO: no per-process write-buffer flush; the global barrier is used */
    KeFlushWriteBuffer();

    return STATUS_SUCCESS;
}
