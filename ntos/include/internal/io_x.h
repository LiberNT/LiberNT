/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     I/O manager inline helpers: file object locks, IRP lists, counters
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

static
__inline
NTSTATUS
IopLockFileObject(
    _In_ PFILE_OBJECT FileObject,
    _In_ KPROCESSOR_MODE WaitMode)
{
    BOOLEAN LockFailed;
    NTSTATUS Status;

    if (InterlockedExchange((PLONG)&FileObject->Busy, TRUE) == FALSE)
    {
        ObReferenceObject(FileObject);
        return STATUS_SUCCESS;
    }

    Status = IopAcquireFileObjectLock(FileObject, WaitMode, BooleanFlagOn(FileObject->Flags, FO_ALERTABLE_IO), &LockFailed);
    ASSERT((Status == STATUS_SUCCESS) == !LockFailed);
    return Status;
}

static
__inline
VOID
IopUnlockFileObject(
    _In_ PFILE_OBJECT FileObject)
{
    NT_VERIFY(InterlockedExchange((PLONG)&FileObject->Busy, FALSE) == TRUE);

    if (FileObject->Waiters != 0)
        KeSetEvent(&FileObject->Lock, IO_NO_INCREMENT, FALSE);

    ObDereferenceObject(FileObject);
}

FORCEINLINE
VOID
IopQueueIrpToThread(
    _In_ PIRP Irp)
{
    PETHREAD Thread = Irp->Tail.Overlay.Thread;
    KIRQL OldIrql;

    /* With special kernel APCs off, IopCompleteRequest cannot run on this thread meanwhile */
    KeEnterGuardedRegionThread(&Thread->Tcb);

    KeAcquireSpinLock(&Thread->IrpListLock, &OldIrql);
    InsertHeadList(&Thread->IrpList, &Irp->ThreadListEntry);
    KeReleaseSpinLock(&Thread->IrpListLock, OldIrql);

    KeLeaveGuardedRegionThread(&Thread->Tcb);
}

FORCEINLINE
VOID
IopUnQueueIrpFromThread(
    _In_ PIRP Irp)
{
    /* The list changes only in its thread's context; the completion APC overlays Tail.Overlay.Thread */
    PETHREAD Thread = PsGetCurrentThread();
    KIRQL OldIrql;

    ASSERT(KeAreAllApcsDisabled());

    if (IsListEmpty(&Irp->ThreadListEntry))
        return;

    KeAcquireSpinLock(&Thread->IrpListLock, &OldIrql);
    RemoveEntryList(&Irp->ThreadListEntry);
    InitializeListHead(&Irp->ThreadListEntry);
    KeReleaseSpinLock(&Thread->IrpListLock, OldIrql);
}

static
__inline
VOID
IopUpdateOperationCount(
    _In_ IOP_TRANSFER_TYPE Type)
{
    PEPROCESS Process;
    PKPRCB Prcb;

    if (!IoCountOperations)
        return;

    Process = PsGetCurrentProcess();
    Prcb = KeGetCurrentPrcb();

    switch (Type)
    {
        case IopReadTransfer:
            InterlockedIncrement((PLONG)&Prcb->IoReadOperationCount);
            InterlockedIncrement((PLONG)&IoReadOperationCount);
            ExInterlockedAddLargeStatistic(&Process->ReadOperationCount, 1);
            break;

        case IopWriteTransfer:
            InterlockedIncrement((PLONG)&Prcb->IoWriteOperationCount);
            InterlockedIncrement((PLONG)&IoWriteOperationCount);
            ExInterlockedAddLargeStatistic(&Process->WriteOperationCount, 1);
            break;

        default:
            InterlockedIncrement((PLONG)&Prcb->IoOtherOperationCount);
            InterlockedIncrement((PLONG)&IoOtherOperationCount);
            ExInterlockedAddLargeStatistic(&Process->OtherOperationCount, 1);
            break;
    }
}

static
__inline
VOID
IopUpdateTransferCount(
    _In_ IOP_TRANSFER_TYPE Type,
    _In_ ULONG TransferCount)
{
    PEPROCESS Process;
    PKPRCB Prcb;

    if (!IoCountOperations)
        return;

    Process = PsGetCurrentProcess();
    Prcb = KeGetCurrentPrcb();

    switch (Type)
    {
        case IopReadTransfer:
            ExInterlockedAddLargeStatistic(&Prcb->IoReadTransferCount, TransferCount);
            ExInterlockedAddLargeStatistic(&IoReadTransferCount, TransferCount);
            ExInterlockedAddLargeStatistic(&Process->ReadTransferCount, TransferCount);
            break;

        case IopWriteTransfer:
            ExInterlockedAddLargeStatistic(&Prcb->IoWriteTransferCount, TransferCount);
            ExInterlockedAddLargeStatistic(&IoWriteTransferCount, TransferCount);
            ExInterlockedAddLargeStatistic(&Process->WriteTransferCount, TransferCount);
            break;

        default:
            ExInterlockedAddLargeStatistic(&Prcb->IoOtherTransferCount, TransferCount);
            ExInterlockedAddLargeStatistic(&IoOtherTransferCount, TransferCount);
            ExInterlockedAddLargeStatistic(&Process->OtherTransferCount, TransferCount);
            break;
    }
}

static
__inline
BOOLEAN
IopValidateOpenPacket(
    _In_opt_ POPEN_PACKET OpenPacket)
{
    return (BOOLEAN)(OpenPacket != NULL &&
                     OpenPacket->Type == IO_TYPE_OPEN_PACKET &&
                     OpenPacket->Size == sizeof(OPEN_PACKET));
}
