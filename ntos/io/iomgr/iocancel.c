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

typedef struct _IOP_CANCEL_SWEEP
{
    PFILE_OBJECT FileObject;
    PIO_STATUS_BLOCK Match;
    KEVENT Done;
    LONG Pending;
    LONG Found;
} IOP_CANCEL_SWEEP, *PIOP_CANCEL_SWEEP;

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
VOID
IoInitCancelHandling(VOID)
{
    KeInitializeSpinLock(&IopCancelSpinLock);
}

/* A cancel routine claimed under the file's list lock cannot see its request complete; it runs once the lock is dropped */
BOOLEAN
IopCancelFileIrps(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PIO_STATUS_BLOCK Match,
    _In_opt_ PETHREAD Thread,
    _In_opt_ PEPROCESS Process)
{
    PDRIVER_CANCEL CancelRoutine;
    BOOLEAN Found = FALSE;
    PLIST_ENTRY Entry;
    KIRQL CancelIrql;
    KIRQL OldIrql;
    PIRP Irp = NULL;

    for (;;)
    {
        KeAcquireSpinLock(&FileObject->IrpListLock, &OldIrql);

        for (Entry = FileObject->IrpList.Flink;
             Entry != &FileObject->IrpList;
             Entry = Entry->Flink)
        {
            Irp = CONTAINING_RECORD(Entry, IRP, ThreadListEntry);

            if (Match != NULL && Irp->UserIosb != Match) continue;
            if (Thread != NULL && Irp->Tail.Overlay.Thread != Thread) continue;
            if (Process != NULL &&
                PsGetThreadProcess(Irp->Tail.Overlay.Thread) != Process)
            {
                continue;
            }

            Found = TRUE;
            if (!Irp->Cancel) break;
        }

        if (Entry == &FileObject->IrpList)
        {
            KeReleaseSpinLock(&FileObject->IrpListLock, OldIrql);
            return Found;
        }

        IoAcquireCancelSpinLock(&CancelIrql);
        Irp->Cancel = TRUE;
        CancelRoutine = IoSetCancelRoutine(Irp, NULL);
        KeReleaseSpinLockFromDpcLevel(&FileObject->IrpListLock);

        if (CancelRoutine == NULL || Irp->CurrentLocation > Irp->StackCount + 1)
        {
            IoReleaseCancelSpinLock(OldIrql);
            continue;
        }

        Irp->CancelIrql = OldIrql;
        CancelRoutine(IoGetCurrentIrpStackLocation(Irp)->DeviceObject, Irp);
    }
}

/*
 * The list belongs to the calling thread and changes only in its context, so
 * holding off its completion APCs keeps every entry in place, and the cancel
 * routines run at APC_LEVEL, below which a file system may take its locks.
 */
static
BOOLEAN
IopCancelThreadIrpsForFile(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PIO_STATUS_BLOCK Match)
{
    PETHREAD Thread = PsGetCurrentThread();
    BOOLEAN Found = FALSE;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    KeRaiseIrql(APC_LEVEL, &OldIrql);

    Entry = Thread->IrpList.Flink;
    while (Entry != &Thread->IrpList)
    {
        Irp = CONTAINING_RECORD(Entry, IRP, ThreadListEntry);
        Entry = Entry->Flink;

        ASSERT(Irp->Tail.Overlay.Thread == Thread);
        if (Irp->Tail.Overlay.OriginalFileObject != FileObject) continue;
        if (Match != NULL && Irp->UserIosb != Match) continue;

        Found = TRUE;
        IoCancelIrp(Irp);
    }

    KeLowerIrql(OldIrql);

    return Found;
}

static
VOID
IopEndCancelSweep(
    _In_ PKAPC Apc)
{
    PIOP_CANCEL_SWEEP Sweep = Apc->SystemArgument1;

    ExFreePoolWithTag(Apc, TAG_IO);
    if (InterlockedDecrement(&Sweep->Pending) == 0)
        KeSetEvent(&Sweep->Done, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
IopCancelSweepApc(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    PIOP_CANCEL_SWEEP Sweep = *SystemArgument1;

    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (IopCancelThreadIrpsForFile(Sweep->FileObject, Sweep->Match))
        InterlockedExchange(&Sweep->Found, TRUE);
    Apc->SystemArgument1 = Sweep;
    IopEndCancelSweep(Apc);
}

static
VOID
NTAPI
IopCancelSweepRundown(
    _In_ PKAPC Apc)
{
    IopEndCancelSweep(Apc);
}

/*
 * Another thread's request list changes only in that thread's context, so each
 * thread of the process that has requests walks its own list in a special
 * kernel APC and the caller waits for all of them to have looked.
 */
static
BOOLEAN
IopCancelOtherThreadsIrps(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PIO_STATUS_BLOCK Match)
{
    PETHREAD Current = PsGetCurrentThread();
    PEPROCESS Process = PsGetCurrentProcess();
    IOP_CANCEL_SWEEP Sweep;
    PETHREAD Thread;
    PKAPC Apc;

    PAGED_CODE();

    Sweep.FileObject = FileObject;
    Sweep.Match = Match;
    Sweep.Pending = 1;
    Sweep.Found = FALSE;
    KeInitializeEvent(&Sweep.Done, NotificationEvent, FALSE);

    for (Thread = PsGetNextProcessThread(Process, NULL);
         Thread != NULL;
         Thread = PsGetNextProcessThread(Process, Thread))
    {
        if (Thread == Current || IsListEmpty(&Thread->IrpList)) continue;

        Apc = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Apc), TAG_IO);
        if (Apc == NULL) continue;

        KeInitializeApc(Apc,
                        &Thread->Tcb,
                        OriginalApcEnvironment,
                        IopCancelSweepApc,
                        IopCancelSweepRundown,
                        NULL,
                        KernelMode,
                        NULL);

        InterlockedIncrement(&Sweep.Pending);
        if (!KeInsertQueueApc(Apc, &Sweep, NULL, IO_NO_INCREMENT))
        {
            InterlockedDecrement(&Sweep.Pending);
            ExFreePoolWithTag(Apc, TAG_IO);
        }
    }

    if (InterlockedDecrement(&Sweep.Pending) != 0)
        KeWaitForSingleObject(&Sweep.Done, Executive, KernelMode, FALSE, NULL);

    return (BOOLEAN)Sweep.Found;
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
    LARGE_INTEGER Interval;
    PLIST_ENTRY Entry;
    ULONG Attempts = 0;
    KIRQL OldIrql;
    PIRP Irp;

    PAGED_CODE();

    KeRaiseIrql(APC_LEVEL, &OldIrql);

    Entry = Thread->IrpList.Flink;
    while (Entry != &Thread->IrpList)
    {
        Irp = CONTAINING_RECORD(Entry, IRP, ThreadListEntry);
        Entry = Entry->Flink;

        IoCancelIrp(Irp);
    }

    KeLowerIrql(OldIrql);

    Interval.QuadPart = -100 * 1000 * 10;

    while (!IsListEmpty(&Thread->IrpList))
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);

        Attempts++;
        if (Attempts < 10) continue;

        KeRaiseIrql(APC_LEVEL, &OldIrql);

        Entry = Thread->IrpList.Flink;
        while (Entry != &Thread->IrpList)
        {
            Irp = CONTAINING_RECORD(Entry, IRP, ThreadListEntry);
            Entry = Entry->Flink;

            DPRINT1("I/O: request %p would not cancel on thread exit\n", Irp);
            IoCancelIrp(Irp);
        }

        KeLowerIrql(OldIrql);

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

    Found = IopCancelThreadIrpsForFile(FileObject, IoRequestToCancel);
    if (IopCancelOtherThreadsIrps(FileObject, IoRequestToCancel))
    {
        Found = TRUE;
    }
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
    IO_STATUS_BLOCK KernelIosb;
    BOOLEAN Found = FALSE;
    PLIST_ENTRY Entry;
    PETHREAD Thread;
    NTSTATUS Status;
    KIRQL OldIrql;
    PIRP Irp;

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

    /* FIXME: another thread's list is walked without anything that stops that thread from changing it */
    KeRaiseIrql(APC_LEVEL, &OldIrql);

    Entry = Thread->IrpList.Flink;
    while (Entry != &Thread->IrpList)
    {
        Irp = CONTAINING_RECORD(Entry, IRP, ThreadListEntry);
        Entry = Entry->Flink;

        if (!IsIrpSynchronous(Irp, Irp->Tail.Overlay.OriginalFileObject)) continue;
        if (IoRequestToCancel != NULL && Irp->UserIosb != IoRequestToCancel)
        {
            continue;
        }

        Found = TRUE;
        IoCancelIrp(Irp);

        if (IoRequestToCancel != NULL) break;
    }

    KeLowerIrql(OldIrql);

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
