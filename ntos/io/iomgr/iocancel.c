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

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
VOID
IoInitCancelHandling(VOID)
{
    KeInitializeSpinLock(&IopCancelSpinLock);
}

static
BOOLEAN
IopCancelIrpsOnList(
    _In_ PLIST_ENTRY ListHead,
    _In_ PKSPIN_LOCK ListLock,
    _In_opt_ PIO_STATUS_BLOCK Match,
    _Out_opt_ PBOOLEAN Found)
{
    PLIST_ENTRY Entry;
    BOOLEAN Any = FALSE;
    KIRQL OldIrql;
    PIRP Irp;

    KeAcquireSpinLock(ListLock, &OldIrql);

    Entry = ListHead->Flink;
    while (Entry != ListHead)
    {
        Irp = CONTAINING_RECORD(Entry, IRP, ThreadListEntry);
        Entry = Entry->Flink;

        if (Match != NULL && Irp->UserIosb != Match) continue;

        Any = TRUE;
        IoCancelIrp(Irp);

        if (Match != NULL) break;
    }

    KeReleaseSpinLock(ListLock, OldIrql);

    if (Found != NULL) *Found = Any;

    return Any;
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

    IopCancelIrpsOnList(&PsGetCurrentThread()->IrpList,
                        &FileObject->IrpListLock,
                        NULL,
                        NULL);

    IopCancelIrpsOnList(&FileObject->IrpList,
                        &FileObject->IrpListLock,
                        NULL,
                        NULL);

    KernelIosb.Status = STATUS_SUCCESS;
    KernelIosb.Information = 0;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            IopWriteIoStatusBlock(IoStatusBlock,
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

    IopCancelIrpsOnList(&FileObject->IrpList,
                        &FileObject->IrpListLock,
                        IoRequestToCancel,
                        &Found);

    if (!Found && IoRequestToCancel != NULL)
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
            IopWriteIoStatusBlock(IoStatusBlock,
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

    KeRaiseIrql(APC_LEVEL, &OldIrql);

    Entry = Thread->IrpList.Flink;
    while (Entry != &Thread->IrpList)
    {
        Irp = CONTAINING_RECORD(Entry, IRP, ThreadListEntry);
        Entry = Entry->Flink;

        if (!(Irp->Flags & IRP_SYNCHRONOUS_API)) continue;
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
