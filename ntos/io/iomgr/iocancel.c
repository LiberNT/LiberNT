/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Taking back a request that is still in flight
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

static KSPIN_LOCK IopCancelSpinLock;

typedef struct _IOP_CANCEL_FILTER
{
    PFILE_OBJECT FileObject;
    PIO_STATUS_BLOCK Match;
    PETHREAD Thread;
    PEPROCESS Process;
    BOOLEAN SynchronousOnly;
    BOOLEAN Retry;
} IOP_CANCEL_FILTER, *PIOP_CANCEL_FILTER;

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
VOID
IoInitCancelHandling(VOID)
{
    KeInitializeSpinLock(&IopCancelSpinLock);
}

static
BOOLEAN
IopIrpMatchesFilter(
    _In_ PIRP Irp,
    _In_ PIOP_CANCEL_FILTER Filter)
{
    if (Filter->FileObject != NULL && Irp->Tail.Overlay.OriginalFileObject != Filter->FileObject) return FALSE;
    if (Filter->Match != NULL && Irp->UserIosb != Filter->Match) return FALSE;
    if (Filter->Thread != NULL && Irp->Tail.Overlay.Thread != Filter->Thread) return FALSE;
    if (Filter->Process != NULL && PsGetThreadProcess(Irp->Tail.Overlay.Thread) != Filter->Process) return FALSE;
    if (Filter->SynchronousOnly && !IsIrpSynchronous(Irp, Irp->Tail.Overlay.OriginalFileObject)) return FALSE;
    return TRUE;
}

/* A cancel routine claimed under the list lock cannot see its request complete; it runs once the lock is dropped */
static
BOOLEAN
IopCancelListIrps(
    _In_ PLIST_ENTRY List,
    _In_ PKSPIN_LOCK ListLock,
    _In_ PIOP_CANCEL_FILTER Filter)
{
    PDRIVER_CANCEL CancelRoutine;
    BOOLEAN Found = FALSE;
    PLIST_ENTRY Entry;
    KIRQL CancelIrql;
    KIRQL OldIrql;
    PIRP Irp = NULL;

    for (;;)
    {
        KeAcquireSpinLock(ListLock, &OldIrql);

        for (Entry = List->Flink; Entry != List; Entry = Entry->Flink)
        {
            Irp = CONTAINING_RECORD(Entry, IRP, ThreadListEntry);

            if (!IopIrpMatchesFilter(Irp, Filter)) continue;

            Found = TRUE;
            if (Filter->Retry ? (Irp->CancelRoutine != NULL) : !Irp->Cancel) break;
        }

        if (Entry == List)
        {
            KeReleaseSpinLock(ListLock, OldIrql);
            return Found;
        }

        IoAcquireCancelSpinLock(&CancelIrql);
        Irp->Cancel = TRUE;
        CancelRoutine = IoSetCancelRoutine(Irp, NULL);
        KeReleaseSpinLockFromDpcLevel(ListLock);

        if (CancelRoutine == NULL || Irp->CurrentLocation > Irp->StackCount + 1)
        {
            IoReleaseCancelSpinLock(OldIrql);
            continue;
        }

        Irp->CancelIrql = OldIrql;
        CancelRoutine(IoGetCurrentIrpStackLocation(Irp)->DeviceObject, Irp);
    }
}

BOOLEAN
IopCancelFileIrps(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PIO_STATUS_BLOCK Match,
    _In_opt_ PETHREAD Thread,
    _In_opt_ PEPROCESS Process)
{
    IOP_CANCEL_FILTER Filter = { NULL, Match, Thread, Process, FALSE, FALSE };

    return IopCancelListIrps(&FileObject->IrpList, &FileObject->IrpListLock, &Filter);
}

static
BOOLEAN
IopCancelThreadIrpsForFile(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PIO_STATUS_BLOCK Match)
{
    PETHREAD Thread = PsGetCurrentThread();
    IOP_CANCEL_FILTER Filter = { FileObject, Match, NULL, NULL, FALSE, FALSE };

    return IopCancelListIrps(&Thread->IrpList, &Thread->IrpListLock, &Filter);
}

static
BOOLEAN
IopThreadHasIrps(
    _In_ PETHREAD Thread)
{
    BOOLEAN HasIrps;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Thread->IrpListLock, &OldIrql);
    HasIrps = !IsListEmpty(&Thread->IrpList);
    KeReleaseSpinLock(&Thread->IrpListLock, OldIrql);

    return HasIrps;
}

/* Another thread's request list is held still by its IrpListLock */
static
BOOLEAN
IopCancelProcessThreadIrps(
    _In_ PEPROCESS Process,
    _In_ PIOP_CANCEL_FILTER Filter)
{
    BOOLEAN Found = FALSE;
    PETHREAD Thread;

    PAGED_CODE();

    for (Thread = PsGetNextProcessThread(Process, NULL);
         Thread != NULL;
         Thread = PsGetNextProcessThread(Process, Thread))
    {
        if (IopCancelListIrps(&Thread->IrpList, &Thread->IrpListLock, Filter))
            Found = TRUE;
    }

    return Found;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
IoAcquireCancelSpinLock(
    _Out_ PKIRQL Irql)
{
    KeAcquireSpinLock(&IopCancelSpinLock, Irql);
}

VOID
NTAPI
IoReleaseCancelSpinLock(
    _In_ KIRQL Irql)
{
    KeReleaseSpinLock(&IopCancelSpinLock, Irql);
}

BOOLEAN
NTAPI
IoCancelIrp(
    _In_ PIRP Irp)
{
    PDRIVER_CANCEL CancelRoutine;
    KIRQL OldIrql;

    IoAcquireCancelSpinLock(&OldIrql);

    Irp->Cancel = TRUE;

    CancelRoutine = IoSetCancelRoutine(Irp, NULL);
    if (CancelRoutine == NULL)
    {
        IoReleaseCancelSpinLock(OldIrql);
        return FALSE;
    }

    if (Irp->CurrentLocation > Irp->StackCount + 1)
    {
        IoReleaseCancelSpinLock(OldIrql);
        return FALSE;
    }

    Irp->CancelIrql = OldIrql;

    CancelRoutine(IoGetCurrentIrpStackLocation(Irp)->DeviceObject, Irp);

    return TRUE;
}

VOID
NTAPI
IoCancelThreadIo(
    _In_ PETHREAD Thread)
{
    IOP_CANCEL_FILTER Filter = { NULL, NULL, NULL, NULL, FALSE, FALSE };
    LARGE_INTEGER Interval;
    PLIST_ENTRY Entry;
    ULONG Attempts = 0;
    KIRQL OldIrql;

    PAGED_CODE();

    IopCancelListIrps(&Thread->IrpList, &Thread->IrpListLock, &Filter);

    Interval.QuadPart = -100 * 1000 * 10;

    while (IopThreadHasIrps(Thread))
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);

        Attempts++;
        if (Attempts < 10) continue;

        KeAcquireSpinLock(&Thread->IrpListLock, &OldIrql);
        for (Entry = Thread->IrpList.Flink; Entry != &Thread->IrpList; Entry = Entry->Flink)
            DPRINT1("I/O: request %p would not cancel on thread exit\n", CONTAINING_RECORD(Entry, IRP, ThreadListEntry));
        KeReleaseSpinLock(&Thread->IrpListLock, OldIrql);

        Filter.Retry = TRUE;
        IopCancelListIrps(&Thread->IrpList, &Thread->IrpListLock, &Filter);

        Attempts = 0;
    }
}

VOID
NTAPI
IoCancelFileOpen(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PFILE_OBJECT FileObject)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    if (FileObject->Flags & FO_HANDLE_CREATED) return;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return;

    Irp->Flags = IRP_SYNCHRONOUS_API | IRP_CLOSE_OPERATION;
    Irp->UserIosb = &IoStatusBlock;
    Irp->UserEvent = &Event;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;
    Irp->RequestorMode = KernelMode;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_CLEANUP;
    StackPtr->FileObject = FileObject;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }

    KeClearEvent(&Event);

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL) return;

    Irp->Flags = IRP_SYNCHRONOUS_API | IRP_CLOSE_OPERATION;
    Irp->UserIosb = &IoStatusBlock;
    Irp->UserEvent = &Event;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Tail.Overlay.OriginalFileObject = FileObject;
    Irp->RequestorMode = KernelMode;

    StackPtr = IoGetNextIrpStackLocation(Irp);
    StackPtr->MajorFunction = IRP_MJ_CLOSE;
    StackPtr->FileObject = FileObject;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }

    FileObject->DeviceObject = NULL;
    FileObject->Vpb = NULL;
    FileObject->Flags |= FO_FILE_OPEN_CANCELLED;
}

NTSTATUS
NTAPI
NtCancelIoFile(
    _In_ HANDLE FileHandle,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    IO_STATUS_BLOCK KernelIosb;
    PFILE_OBJECT FileObject;
    NTSTATUS Status;

    PAGED_CODE();

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
                                       0,
                                       IoFileObjectType,
                                       PreviousMode,
                                       (PVOID *)&FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    IopCancelThreadIrpsForFile(FileObject, NULL);
    IopCancelFileIrps(FileObject, NULL, PsGetCurrentThread(), NULL);

    KernelIosb.Status = STATUS_SUCCESS;
    KernelIosb.Information = 0;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            IopWriteIoStatusBlock(IopIs32BitFileIo(FileObject, PreviousMode) ?
                                      IopGetWow64IoStatusBlock(IoStatusBlock) : IoStatusBlock,
                                  &KernelIosb,
                                  IopIs32BitFileIo(FileObject, PreviousMode));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *IoStatusBlock = KernelIosb;
    }

    ObDereferenceObject(FileObject);

    return Status;
}

NTSTATUS
NTAPI
NtCancelIoFileEx(
    _In_ HANDLE FileHandle,
    _In_opt_ PIO_STATUS_BLOCK IoRequestToCancel,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    IO_STATUS_BLOCK KernelIosb;
    PFILE_OBJECT FileObject;
    IOP_CANCEL_FILTER Filter = { NULL, NULL, NULL, NULL, FALSE, FALSE };
    BOOLEAN Found = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

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
                                       0,
                                       IoFileObjectType,
                                       PreviousMode,
                                       (PVOID *)&FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Filter.FileObject = FileObject;
    Filter.Match = IoRequestToCancel;
    Found = IopCancelProcessThreadIrps(PsGetCurrentProcess(), &Filter);
    if (IopCancelFileIrps(FileObject, IoRequestToCancel, NULL, PsGetCurrentProcess()))
    {
        Found = TRUE;
    }

    if (!Found)
    {
        ObDereferenceObject(FileObject);
        return STATUS_NOT_FOUND;
    }

    KernelIosb.Status = STATUS_SUCCESS;
    KernelIosb.Information = 0;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            IopWriteIoStatusBlock(IopIs32BitFileIo(FileObject, PreviousMode) ?
                                      IopGetWow64IoStatusBlock(IoStatusBlock) : IoStatusBlock,
                                  &KernelIosb,
                                  IopIs32BitFileIo(FileObject, PreviousMode));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *IoStatusBlock = KernelIosb;
    }

    ObDereferenceObject(FileObject);

    return Status;
}

NTSTATUS
NTAPI
NtCancelSynchronousIoFile(
    _In_ HANDLE ThreadHandle,
    _In_opt_ PIO_STATUS_BLOCK IoRequestToCancel,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    IOP_CANCEL_FILTER Filter = { NULL, NULL, NULL, NULL, FALSE, FALSE };
    IO_STATUS_BLOCK KernelIosb;
    BOOLEAN Found = FALSE;
    PETHREAD Thread;
    NTSTATUS Status;

    PAGED_CODE();

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

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_TERMINATE,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID *)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (PsGetThreadProcess(Thread) != PsGetCurrentProcess())
    {
        ObDereferenceObject(Thread);
        return STATUS_ACCESS_DENIED;
    }

    Filter.Match = IoRequestToCancel;
    Filter.SynchronousOnly = TRUE;
    Found = IopCancelListIrps(&Thread->IrpList, &Thread->IrpListLock, &Filter);

    ObDereferenceObject(Thread);

    if (!Found) return STATUS_NOT_FOUND;

    KernelIosb.Status = STATUS_SUCCESS;
    KernelIosb.Information = 0;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *IoStatusBlock = KernelIosb;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *IoStatusBlock = KernelIosb;
    }

    return Status;
}
