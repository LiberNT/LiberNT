/*

static
__inline
NTSTATUS
IopLockFileObject(
    _In_ PFILE_OBJECT FileObject,
    _In_ KPROCESSOR_MODE WaitMode)
{
    BOOLEAN LockFailed;

    if (InterlockedExchange((PLONG)&FileObject->Busy, TRUE) == FALSE)
    {
        ObReferenceObject(FileObject);
        return STATUS_SUCCESS;
    }

}

static
__inline
VOID
{
    NT_VERIFY(InterlockedExchange((PLONG)&FileObject->Busy, FALSE) == TRUE);
        KeSetEvent(&FileObject->Lock, IO_NO_INCREMENT, FALSE);
    ObDereferenceObject(FileObject);
}

FORCEINLINE
VOID
{
    PETHREAD Thread = Irp->Tail.Overlay.Thread;
    KIRQL OldIrql;

    KeEnterGuardedRegionThread(&Thread->Tcb);

    KeAcquireSpinLock(&Thread->IrpListLock, &OldIrql);
    InsertHeadList(&Thread->IrpList, &Irp->ThreadListEntry);
    KeReleaseSpinLock(&Thread->IrpListLock, OldIrql);

    KeLeaveGuardedRegionThread(&Thread->Tcb);
}

FORCEINLINE
VOID
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
{

    {
    }
}

static
__inline
VOID
{

    {
    }
}

static
__inline
BOOLEAN
{
}
