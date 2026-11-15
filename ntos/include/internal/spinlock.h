/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Spin lock and queued spin lock primitives shared with the HAL
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

FORCEINLINE
ULONG_PTR
KxLoadAcquirePointer(
    _In_ PVOID const volatile *Address)
{
    return (ULONG_PTR)ReadPointerAcquire(Address);
}

FORCEINLINE
VOID
KxStoreReleasePointer(
    _Out_ PVOID volatile *Address,
    _In_ PVOID Value)
{
    WritePointerRelease(Address, Value);
}

FORCEINLINE
KSPIN_LOCK
KxSpinLockOwnerValue(VOID)
{
    return (KSPIN_LOCK)KeGetCurrentThread() | 1;
}

_Acquires_nonreentrant_lock_(SpinLock)
FORCEINLINE
VOID
KxAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock)
{
#if DBG
    if (*SpinLock == KxSpinLockOwnerValue())
    {
        KeBugCheckEx(SPIN_LOCK_ALREADY_OWNED, (ULONG_PTR)SpinLock, (ULONG_PTR)_ReturnAddress(),
                     (ULONG_PTR)KeGetCurrentThread(), (ULONG_PTR)KeGetCurrentIrql());
    }
#endif

#ifdef CONFIG_SMP
    while (InterlockedBitTestAndSet((PLONG)SpinLock, 0))
    {
        while (*(volatile KSPIN_LOCK *)SpinLock & 1)
            YieldProcessor();
    }
#endif

    KeMemoryBarrierWithoutFence();

#if DBG
    *SpinLock = KxSpinLockOwnerValue();
#endif
}

_Releases_nonreentrant_lock_(SpinLock)
FORCEINLINE
VOID
KxReleaseSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock)
{
#if DBG
    if (*SpinLock != KxSpinLockOwnerValue())
        KeBugCheckEx(SPIN_LOCK_NOT_OWNED, (ULONG_PTR)SpinLock, 0, 0, 0);
#endif

#if defined(CONFIG_SMP) || DBG
    WriteULongPtrRelease((PULONG_PTR)SpinLock, 0);
#else
    UNREFERENCED_PARAMETER(SpinLock);
#endif

    KeMemoryBarrierWithoutFence();
}

#define KX_LOCK_QUEUE_WAIT  ((ULONG_PTR)LOCK_QUEUE_WAIT)
#define KX_LOCK_QUEUE_OWNER ((ULONG_PTR)LOCK_QUEUE_OWNER)
#define KX_LOCK_QUEUE_FLAGS (KX_LOCK_QUEUE_WAIT | KX_LOCK_QUEUE_OWNER)

/*
 * Queued spinlocks use the KSPIN_LOCK as an MCS queue tail. Each acquisition
 * supplies a distinct KSPIN_LOCK_QUEUE node. The low bits of the node's Lock
 * pointer describe whether that node is waiting or owns the lock; release
 * restores the pointer before the node can be reused.
 */
FORCEINLINE
VOID
KxAcquireQueuedSpinLock(
    _Inout_ PKSPIN_LOCK_QUEUE LockQueue)
{
#if defined(CONFIG_SMP) || DBG
    PKSPIN_LOCK SpinLock;
    PKSPIN_LOCK_QUEUE Predecessor;
    ULONG_PTR QueueState;

    SpinLock = LockQueue->Lock;
    ASSERT(SpinLock != NULL);
    ASSERT(((ULONG_PTR)SpinLock & KX_LOCK_QUEUE_FLAGS) == 0);
    ASSERT(((ULONG_PTR)LockQueue & KX_LOCK_QUEUE_FLAGS) == 0);

    LockQueue->Next = NULL;
    LockQueue->Lock = (PKSPIN_LOCK)((ULONG_PTR)SpinLock | KX_LOCK_QUEUE_WAIT);

    Predecessor = (PKSPIN_LOCK_QUEUE)InterlockedExchangePointer(
        (PVOID volatile *)SpinLock,
        LockQueue);
    if (Predecessor == NULL)
    {
        LockQueue->Lock = (PKSPIN_LOCK)((ULONG_PTR)SpinLock | KX_LOCK_QUEUE_OWNER);
        return;
    }

    ASSERT(Predecessor != LockQueue);
    ASSERT(((ULONG_PTR)Predecessor & KX_LOCK_QUEUE_FLAGS) == 0);
    KxStoreReleasePointer((PVOID volatile *)&Predecessor->Next, LockQueue);

    do
    {
        QueueState = KxLoadAcquirePointer((PVOID const volatile *)&LockQueue->Lock);
        if (!(QueueState & KX_LOCK_QUEUE_WAIT))
            break;
        YieldProcessor();
    } while (TRUE);

    ASSERT(QueueState == ((ULONG_PTR)SpinLock | KX_LOCK_QUEUE_OWNER));
#else
    UNREFERENCED_PARAMETER(LockQueue);
    KeMemoryBarrierWithoutFence();
#endif
}

FORCEINLINE
VOID
KxReleaseQueuedSpinLock(
    _Inout_ PKSPIN_LOCK_QUEUE LockQueue)
{
#if defined(CONFIG_SMP) || DBG
    PKSPIN_LOCK SpinLock;
    PKSPIN_LOCK_QUEUE Successor;

    ASSERT(((ULONG_PTR)LockQueue->Lock & KX_LOCK_QUEUE_FLAGS) == KX_LOCK_QUEUE_OWNER);

    SpinLock = (PKSPIN_LOCK)((ULONG_PTR)LockQueue->Lock & ~KX_LOCK_QUEUE_FLAGS);
    ASSERT(SpinLock != NULL);

    Successor = (PKSPIN_LOCK_QUEUE)KxLoadAcquirePointer(
        (PVOID const volatile *)&LockQueue->Next);
    if (Successor == NULL)
    {
        if (InterlockedCompareExchangePointer((PVOID volatile *)SpinLock,
                                              NULL,
                                              LockQueue) == LockQueue)
        {
            LockQueue->Lock = SpinLock;
            LockQueue->Next = NULL;
            return;
        }

        do
        {
            Successor = (PKSPIN_LOCK_QUEUE)KxLoadAcquirePointer(
                (PVOID const volatile *)&LockQueue->Next);
            if (Successor != NULL)
                break;
            YieldProcessor();
        } while (TRUE);
    }
    ASSERT(Successor != LockQueue);
    ASSERT(KxLoadAcquirePointer((PVOID const volatile *)&Successor->Lock) ==
           ((ULONG_PTR)SpinLock | KX_LOCK_QUEUE_WAIT));
    KxStoreReleasePointer(
        (PVOID volatile *)&Successor->Lock,
        (PVOID)((ULONG_PTR)SpinLock | KX_LOCK_QUEUE_OWNER));

    LockQueue->Next = NULL;
    LockQueue->Lock = SpinLock;
#else
    UNREFERENCED_PARAMETER(LockQueue);
    KeMemoryBarrierWithoutFence();
#endif
}

FORCEINLINE
BOOLEAN
KxTryToAcquireQueuedSpinLock(
    _Inout_ PKSPIN_LOCK_QUEUE LockQueue)
{
#if defined(CONFIG_SMP) || DBG
    PKSPIN_LOCK SpinLock;
    ULONG_PTR QueueState;

    QueueState = (ULONG_PTR)LockQueue->Lock;
    ASSERT(((ULONG_PTR)LockQueue & KX_LOCK_QUEUE_FLAGS) == 0);
    ASSERT(QueueState != 0);
    if ((QueueState == 0) || (QueueState & KX_LOCK_QUEUE_FLAGS))
    {
        ASSERT(FALSE);
        return FALSE;
    }
    SpinLock = (PKSPIN_LOCK)QueueState;

    LockQueue->Next = NULL;
    if (InterlockedCompareExchangePointer((PVOID volatile *)SpinLock,
                                          LockQueue,
                                          NULL) != NULL)
    {
        return FALSE;
    }

    LockQueue->Lock = (PKSPIN_LOCK)((ULONG_PTR)SpinLock | KX_LOCK_QUEUE_OWNER);
    return TRUE;
#else
    UNREFERENCED_PARAMETER(LockQueue);
    KeMemoryBarrierWithoutFence();
    return TRUE;
#endif
}
