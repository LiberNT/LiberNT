/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exported spin lock entry points: raise IRQL, then take the lock
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

#undef KeAcquireSpinLock
#undef KeReleaseSpinLock

FORCEINLINE
PKSPIN_LOCK_QUEUE
KiNumberedLockQueue(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    PKSPIN_LOCK_QUEUE LockQueue;

    ASSERT(LockNumber < LockQueueMaximumLock);

    LockQueue = &KeGetCurrentPrcb()->LockQueue[LockNumber];
    ASSERT(LockQueue->Lock != NULL);
    return LockQueue;
}

FORCEINLINE
KIRQL
KiRaiseAndAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL NewIrql)
{
    KIRQL OldIrql;

    KeRaiseIrql(NewIrql, &OldIrql);
    KxAcquireSpinLock(SpinLock);
    return OldIrql;
}

FORCEINLINE
KIRQL
KiRaiseAndAcquireNumberedLock(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber,
    _In_ KIRQL NewIrql)
{
    KIRQL OldIrql;

    KeRaiseIrql(NewIrql, &OldIrql);
    KxAcquireQueuedSpinLock(KiNumberedLockQueue(LockNumber));
    return OldIrql;
}

FORCEINLINE
VOID
KiRaiseAndAcquireInStackLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Out_ PKLOCK_QUEUE_HANDLE LockHandle,
    _In_ KIRQL NewIrql)
{
    LockHandle->LockQueue.Next = NULL;
    LockHandle->LockQueue.Lock = SpinLock;

    KeRaiseIrql(NewIrql, &LockHandle->OldIrql);
    KxAcquireQueuedSpinLock(&LockHandle->LockQueue);
}

KIRQL
NTAPI
KeAcquireSpinLockRaiseToDpc(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    return KiRaiseAndAcquireSpinLock(SpinLock, DISPATCH_LEVEL);
}

KIRQL
FASTCALL
KeAcquireSpinLockRaiseToSynch(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    return KiRaiseAndAcquireSpinLock(SpinLock, SYNCH_LEVEL);
}

VOID
NTAPI
KeReleaseSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
    KxReleaseSpinLock(SpinLock);
    KeLowerIrql(OldIrql);
}

KIRQL
FASTCALL
KeAcquireQueuedSpinLock(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    return KiRaiseAndAcquireNumberedLock(LockNumber, DISPATCH_LEVEL);
}

KIRQL
FASTCALL
KeAcquireQueuedSpinLockRaiseToSynch(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber)
{
    return KiRaiseAndAcquireNumberedLock(LockNumber, SYNCH_LEVEL);
}

VOID
FASTCALL
KeReleaseQueuedSpinLock(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber,
    _In_ KIRQL OldIrql)
{
    KxReleaseQueuedSpinLock(KiNumberedLockQueue(LockNumber));
    KeLowerIrql(OldIrql);
}

VOID
FASTCALL
KeAcquireInStackQueuedSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Out_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    KiRaiseAndAcquireInStackLock(SpinLock, LockHandle, DISPATCH_LEVEL);
}

VOID
FASTCALL
KeAcquireInStackQueuedSpinLockRaiseToSynch(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Out_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    KiRaiseAndAcquireInStackLock(SpinLock, LockHandle, SYNCH_LEVEL);
}

VOID
FASTCALL
KeReleaseInStackQueuedSpinLock(
    _In_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    KxReleaseQueuedSpinLock(&LockHandle->LockQueue);
    KeLowerIrql(LockHandle->OldIrql);
}

LOGICAL
FASTCALL
KeTryToAcquireQueuedSpinLock(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber,
    _Out_ PKIRQL OldIrql)
{
    KIRQL PreviousIrql;

    KeRaiseIrql(DISPATCH_LEVEL, &PreviousIrql);
    if (!KxTryToAcquireQueuedSpinLock(KiNumberedLockQueue(LockNumber)))
    {
        KeLowerIrql(PreviousIrql);
        return FALSE;
    }

    *OldIrql = PreviousIrql;
    return TRUE;
}

BOOLEAN
FASTCALL
KeTryToAcquireQueuedSpinLockRaiseToSynch(
    _In_ KSPIN_LOCK_QUEUE_NUMBER LockNumber,
    _Out_ PKIRQL OldIrql)
{
    KeRaiseIrql(SYNCH_LEVEL, OldIrql);
    return KxTryToAcquireQueuedSpinLock(KiNumberedLockQueue(LockNumber));
}
