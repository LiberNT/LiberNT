/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Finishing a request and telling the requestor about it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

FORCEINLINE
VOID
IopClearStackLocation(
    _Inout_ PIO_STACK_LOCATION StackPtr)
{
    StackPtr->MinorFunction = 0;
    StackPtr->Flags = 0;
    StackPtr->Control &= SL_ERROR_RETURNED;
    StackPtr->Parameters.Others.Argument1 = NULL;
    StackPtr->Parameters.Others.Argument2 = NULL;
    StackPtr->Parameters.Others.Argument3 = NULL;
    StackPtr->FileObject = NULL;
}

static
VOID
IopFreeMdlChain(
    _Inout_ PIRP Irp)
{
    PMDL Mdl;

    while (Irp->MdlAddress != NULL)
    {
        Mdl = Irp->MdlAddress;
        Irp->MdlAddress = Mdl->Next;

        if (Mdl->MdlFlags & MDL_PAGES_LOCKED) MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
    }
}

VOID
IopDeadIrp(VOID)
{
    KeBugCheckEx(NO_MORE_IRP_STACK_LOCATIONS, 0, 0, 0, 0);
}

VOID
IopDisassociateThreadIrp(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();
    PLIST_ENTRY Entry;
    PIRP Irp;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopDeviceTreeLock, &OldIrql);

    while (!IsListEmpty(&Thread->IrpList))
    {
        Entry = RemoveHeadList(&Thread->IrpList);
        Irp = CONTAINING_RECORD(Entry, IRP, ThreadListEntry);

        InitializeListHead(&Irp->ThreadListEntry);
        Irp->Tail.Overlay.Thread = NULL;
    }

    KeReleaseSpinLock(&IopDeviceTreeLock, OldIrql);
}

VOID
NTAPI
IopQueueThreadAgnosticIrp(
    IN PIRP Irp,
    IN PFILE_OBJECT FileObject)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&FileObject->IrpListLock, &OldIrql);
    InsertTailList(&FileObject->IrpList, &Irp->ThreadListEntry);
    KeReleaseSpinLock(&FileObject->IrpListLock, OldIrql);
}

VOID
NTAPI
IopAbandonThreadAgnosticIrp(
    IN PIRP Irp)
{
    PFILE_OBJECT FileObject;
    KIRQL OldIrql;

    FileObject = Irp->Tail.Overlay.OriginalFileObject;
    if (FileObject == NULL) return;

    KeAcquireSpinLock(&FileObject->IrpListLock, &OldIrql);

    if (!IsListEmpty(&Irp->ThreadListEntry))
    {
        RemoveEntryList(&Irp->ThreadListEntry);
        InitializeListHead(&Irp->ThreadListEntry);
    }

    KeReleaseSpinLock(&FileObject->IrpListLock, OldIrql);
}

VOID
NTAPI
IopFreeIrpKernelApc(
    _In_ PKAPC Apc)
{
    PIRP Irp = CONTAINING_RECORD(Apc, IRP, Tail.Apc);

    IopUnQueueIrpFromThread(Irp);
    IoFreeIrp(Irp);
}

VOID
NTAPI
IopAbortIrpKernelApc(
    _In_ PKAPC Apc)
{
    PIRP Irp = CONTAINING_RECORD(Apc, IRP, Tail.Apc);

    IopAbandonThreadAgnosticIrp(Irp);
    IopFreeIrpKernelApc(Apc);
}

VOID
IopReportIncompleteSynchronousIrp(
    _In_ PIRP Irp)
{
    DPRINT1("I/O: a synchronous request was abandoned while in flight (%p)\n",
            Irp);
}

VOID
IopCleanupAfterException(
    _In_ PFILE_OBJECT FileObject,
    _In_ PIRP Irp,
    _In_opt_ PKEVENT Event,
    _In_opt_ PKEVENT LocalEvent)
{
    if (Irp != NULL)
    {
        KeRaiseIrql(APC_LEVEL, &Irp->CancelIrql);
        IopUnQueueIrpFromThread(Irp);
        KeLowerIrql(Irp->CancelIrql);

        IoFreeIrp(Irp);
    }

    if (Event != NULL) ObDereferenceObject(Event);
    if (LocalEvent != NULL) ExFreePool(LocalEvent);

    if (FileObject->Flags & FO_SYNCHRONOUS_IO) IopUnlockFileObject(FileObject);

    ObDereferenceObject(FileObject);
}

/* COMPLETION APC *************************************************************/

VOID
NTAPI
IopCompleteRequest(
    IN PKAPC Apc,
    IN PKNORMAL_ROUTINE *NormalRoutine,
    IN PVOID *NormalContext,
    IN PVOID *SystemArgument1,
    IN PVOID *SystemArgument2)
{
    PFILE_OBJECT FileObject;
    PIRP Irp;
    PMDL Mdl;
    NTSTATUS Status;
    BOOLEAN Is32Bit;
    PVOID Port = NULL;
    PVOID Key = NULL;
    BOOLEAN SignaledEvent = FALSE;

    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);

    Irp = CONTAINING_RECORD(Apc, IRP, Tail.Apc);
    FileObject = (PFILE_OBJECT)*SystemArgument1;

    IopUnQueueIrpFromThread(Irp);

    Is32Bit = IopIs32BitFileIo(FileObject, Irp->RequestorMode);

    if (NT_SUCCESS(Irp->IoStatus.Status) &&
        (Irp->Flags & (IRP_BUFFERED_IO | IRP_INPUT_OPERATION)) ==
            (IRP_BUFFERED_IO | IRP_INPUT_OPERATION))
    {
        _SEH2_TRY
        {
            RtlCopyMemory(Irp->UserBuffer,
                          Irp->AssociatedIrp.SystemBuffer,
                          Irp->IoStatus.Information);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Irp->IoStatus.Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

    if (Irp->Flags & IRP_DEALLOCATE_BUFFER)
    {
        ExFreePool(Irp->AssociatedIrp.SystemBuffer);
        Irp->AssociatedIrp.SystemBuffer = NULL;
        Irp->Flags &= ~IRP_DEALLOCATE_BUFFER;
    }

    while (Irp->MdlAddress != NULL)
    {
        Mdl = Irp->MdlAddress;
        Irp->MdlAddress = Mdl->Next;

        if (Mdl->MdlFlags & MDL_PAGES_LOCKED) MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
    }

    if (Irp->Tail.Overlay.AuxiliaryBuffer != NULL)
    {
        ExFreePool(Irp->Tail.Overlay.AuxiliaryBuffer);
        Irp->Tail.Overlay.AuxiliaryBuffer = NULL;
    }

    Status = Irp->IoStatus.Status;

    if (FileObject != NULL) FileObject->FinalStatus = Status;

    if (Irp->UserIosb != NULL)
    {
        if (Irp->RequestorMode == KernelMode)
        {
            IopWriteIoStatusBlock(Irp->UserIosb, &Irp->IoStatus, FALSE);
        }
        else
        {
            _SEH2_TRY
            {
                IopWriteIoStatusBlock(Irp->UserIosb, &Irp->IoStatus, Is32Bit);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
    }

    if (FileObject != NULL &&
        FileObject->CompletionContext != NULL &&
        Irp->Overlay.AsynchronousParameters.UserApcRoutine == NULL &&
        !(Irp->Flags & IRP_CREATE_OPERATION))
    {
        Port = FileObject->CompletionContext->Port;
        Key = FileObject->CompletionContext->Key;
    }

    if (Irp->UserEvent != NULL)
    {
        KeSetEvent(Irp->UserEvent, 0, FALSE);
        SignaledEvent = TRUE;

        if (FileObject != NULL &&
            !(FileObject->Flags & FO_SYNCHRONOUS_IO))
        {
            ObDereferenceObject(Irp->UserEvent);
        }
    }
    else if (FileObject != NULL && (FileObject->Flags & FO_SYNCHRONOUS_IO))
    {
        KeSetEvent(&FileObject->Event, 0, FALSE);
        SignaledEvent = TRUE;
    }

    if (Port != NULL)
    {
        NTSTATUS PortStatus;

        PortStatus = IoSetIoCompletion(Port,
                                       Key,
                                       Irp->Overlay.AsynchronousParameters.UserApcContext,
                                       Irp->IoStatus.Status,
                                       Irp->IoStatus.Information,
                                       TRUE);
        if (!NT_SUCCESS(PortStatus))
        {
            DPRINT1("I/O: the completion packet was dropped (%lx)\n",
                    PortStatus);
        }
    }

    if (Irp->Overlay.AsynchronousParameters.UserApcRoutine != NULL)
    {
        *NormalRoutine =
            (PKNORMAL_ROUTINE)Irp->Overlay.AsynchronousParameters.UserApcRoutine;
        *NormalContext =
            Irp->Overlay.AsynchronousParameters.UserApcContext;
        *SystemArgument1 = Irp->UserIosb;
        *SystemArgument2 = NULL;
    }
    else
    {
        *SystemArgument1 = NULL;
        *SystemArgument2 = NULL;
    }

    UNREFERENCED_PARAMETER(SignaledEvent);

    if (FileObject != NULL) ObDereferenceObject(FileObject);

    IoFreeIrp(Irp);
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
FASTCALL
IofCompleteRequest(
    _In_ PIRP Irp,
    _In_ CCHAR PriorityBoost)
{
    PIO_COMPLETION_ROUTINE Routine;
    PIO_STACK_LOCATION StackPtr;
    PDEVICE_OBJECT DeviceObject;
    PFILE_OBJECT FileObject;
    PETHREAD Thread;
    PIRP MasterIrp;
    NTSTATUS Status;
    PVOID Context;
    BOOLEAN Invoke;
    UCHAR Control;

    ASSERT(Irp != NULL);
    ASSERT(Irp->Type == IO_TYPE_IRP);
    ASSERT(Irp->IoStatus.Status != STATUS_PENDING);
    ASSERT(Irp->CancelRoutine == NULL);

    if (Irp->CurrentLocation > (CCHAR)(Irp->StackCount + 1))
    {
        KeBugCheckEx(MULTIPLE_IRP_COMPLETE_REQUESTS, (ULONG_PTR)Irp, 0, 0, 0);
    }

    StackPtr = IoGetCurrentIrpStackLocation(Irp);

    while (Irp->CurrentLocation <= Irp->StackCount)
    {
        Irp->PendingReturned = (StackPtr->Control & SL_PENDING_RETURNED) != 0;

        Routine = StackPtr->CompletionRoutine;
        Context = StackPtr->Context;
        Control = StackPtr->Control;

        IopClearStackLocation(StackPtr);

        Irp->CurrentLocation++;
        Irp->Tail.Overlay.CurrentStackLocation++;
        StackPtr++;

        Invoke = FALSE;
        if (Routine != NULL)
        {
            if (NT_SUCCESS(Irp->IoStatus.Status) &&
                (Control & SL_INVOKE_ON_SUCCESS))
            {
                Invoke = TRUE;
            }
            else if (!NT_SUCCESS(Irp->IoStatus.Status) &&
                     (Control & SL_INVOKE_ON_ERROR))
            {
                Invoke = TRUE;
            }
            else if (Irp->Cancel && (Control & SL_INVOKE_ON_CANCEL))
            {
                Invoke = TRUE;
            }
        }

        if (Invoke)
        {
            DeviceObject = (Irp->CurrentLocation > Irp->StackCount)
                               ? NULL
                               : StackPtr->DeviceObject;

            Status = Routine(DeviceObject, Irp, Context);
            if (Status == STATUS_MORE_PROCESSING_REQUIRED) return;

            continue;
        }

        if (Irp->PendingReturned && Irp->CurrentLocation <= Irp->StackCount)
        {
            IoMarkIrpPending(Irp);
        }
    }

    if (Irp->Flags & IRP_ASSOCIATED_IRP)
    {
        MasterIrp = Irp->AssociatedIrp.MasterIrp;

        IopFreeMdlChain(Irp);
        IoFreeIrp(Irp);

        if (InterlockedDecrement(&MasterIrp->AssociatedIrp.IrpCount) == 0)
        {
            IofCompleteRequest(MasterIrp, PriorityBoost);
        }

        return;
    }

    if (Irp->Flags & (IRP_PAGING_IO | IRP_CLOSE_OPERATION))
    {
        if (Irp->Flags & IRP_SYNCHRONOUS_PAGING_IO)
        {
            if (Irp->UserIosb != NULL) *Irp->UserIosb = Irp->IoStatus;
            if (Irp->UserEvent != NULL)
            {
                KeSetEvent(Irp->UserEvent, PriorityBoost, FALSE);
            }

            return;
        }

        /* TODO: no Mm page-write callback; the packet is retired here */
        if (Irp->UserIosb != NULL) *Irp->UserIosb = Irp->IoStatus;
        if (Irp->UserEvent != NULL)
        {
            KeSetEvent(Irp->UserEvent, PriorityBoost, FALSE);
        }

        IopFreeMdlChain(Irp);
        IoFreeIrp(Irp);

        return;
    }

    Thread = Irp->Tail.Overlay.Thread;
    if (Thread == NULL) IopDeadIrp();

    FileObject = Irp->Tail.Overlay.OriginalFileObject;

    if (Irp->Flags & IRP_DEFER_IO_COMPLETION && !Irp->PendingReturned) return;

    KeInitializeApc(&Irp->Tail.Apc,
                    &Thread->Tcb,
                    Irp->ApcEnvironment,
                    IopCompleteRequest,
                    IopAbortIrpKernelApc,
                    NULL,
                    KernelMode,
                    NULL);

    KeInsertQueueApc(&Irp->Tail.Apc,
                     FileObject,
                     NULL,
                     PriorityBoost);
}

#undef IoCompleteRequest
VOID
NTAPI
IoCompleteRequest(
    _In_ PIRP Irp,
    _In_ CCHAR PriorityBoost)
{
    IofCompleteRequest(Irp, PriorityBoost);
}

/* SYNCHRONOUS AND ASYNCHRONOUS HELPERS ***************************************/

NTSTATUS
IopPerformSynchronousRequest(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Deferred,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SynchIo,
    _In_ IOP_TRANSFER_TYPE TransferType)
{
    NTSTATUS Status;

    IopUpdateOperationCount(TransferType);

    if (Deferred) Irp->Flags |= IRP_DEFER_IO_COMPLETION;

    Status = IoCallDriver(DeviceObject, Irp);

    if (!SynchIo) return Status;

    if (Status == STATUS_PENDING)
    {
        Status = KeWaitForSingleObject(&FileObject->Event,
                                       Executive,
                                       PreviousMode,
                                       BooleanFlagOn(FileObject->Flags,
                                                     FO_ALERTABLE_IO),
                                       NULL);
        if (Status == STATUS_ALERTED || Status == STATUS_USER_APC)
        {
            IopAbortInterruptedIrp(&FileObject->Event, Irp);
        }

        Status = FileObject->FinalStatus;
    }

    if (FileObject->Flags & FO_SYNCHRONOUS_IO) IopUnlockFileObject(FileObject);

    return Status;
}

NTSTATUS
IopFinalizeAsynchronousIo(
    _In_ NTSTATUS SynchStatus,
    _In_ PKEVENT Event,
    _In_ PIRP Irp,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PIO_STATUS_BLOCK KernelIosb,
    _Out_ PIO_STATUS_BLOCK UserIosb)
{
    NTSTATUS Status = SynchStatus;

    UNREFERENCED_PARAMETER(Irp);

    if (Status == STATUS_PENDING)
    {
        Status = KeWaitForSingleObject(Event,
                                       Executive,
                                       PreviousMode,
                                       FALSE,
                                       NULL);
        if (NT_SUCCESS(Status))
        {
            _SEH2_TRY
            {
                IopWriteIoStatusBlock(UserIosb, KernelIosb, FALSE);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (NT_SUCCESS(Status)) Status = KernelIosb->Status;
        }
    }

    ExFreePool(Event);

    return Status;
}

VOID
NTAPI
IopAbortInterruptedIrp(
    IN PKEVENT EventObject,
    IN PIRP Irp)
{
    LARGE_INTEGER Timeout;
    BOOLEAN Cancelled;
    KIRQL OldIrql;

    KeRaiseIrql(APC_LEVEL, &OldIrql);

    if (IsListEmpty(&Irp->ThreadListEntry))
    {
        KeLowerIrql(OldIrql);
        return;
    }

    KeLowerIrql(OldIrql);

    Cancelled = IoCancelIrp(Irp);
    if (!Cancelled)
    {
        IopReportIncompleteSynchronousIrp(Irp);
        return;
    }

    Timeout.QuadPart = -100 * 1000 * 10;

    while (KeWaitForSingleObject(EventObject,
                                 Executive,
                                 KernelMode,
                                 FALSE,
                                 &Timeout) == STATUS_TIMEOUT)
    {
        KeRaiseIrql(APC_LEVEL, &OldIrql);
        Cancelled = IsListEmpty(&Irp->ThreadListEntry);
        KeLowerIrql(OldIrql);

        if (Cancelled) return;
    }
}

NTSTATUS
NTAPI
IopCleanupFailedIrp(
    IN PFILE_OBJECT FileObject,
    IN PKEVENT EventObject,
    IN PVOID Buffer OPTIONAL)
{
    PAGED_CODE();

    if (Buffer != NULL) ExFreePool(Buffer);
    if (EventObject != NULL) ObDereferenceObject(EventObject);

    if (FileObject->Flags & FO_SYNCHRONOUS_IO)
    {
        IopUnlockFileObject(FileObject);
    }

    ObDereferenceObject(FileObject);

    return STATUS_INSUFFICIENT_RESOURCES;
}
