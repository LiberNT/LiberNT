/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Locks that spin, and the ones that stop spinning and wait
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * Some targets reach the plain spin lock entry points through the fastcall
 * ones. Both forms are provided here, so the aliases have to be out of the
 * way before either is defined.
 */
#undef KeAcquireSpinLockAtDpcLevel
#undef KeReleaseSpinLockFromDpcLevel

#undef KeInitializeGuardedMutex
#undef KeAcquireGuardedMutex
#undef KeReleaseGuardedMutex
#undef KeAcquireGuardedMutexUnsafe
#undef KeReleaseGuardedMutexUnsafe
#undef KeTryToAcquireGuardedMutex

/* PUBLIC FUNCTIONS: SPIN LOCKS ***********************************************/

/*
 * A spin lock holds no state beyond "taken" or "free", so starting one is
 * writing a zero. The out-of-line form exists because the export table has
 * always had it; kernel code uses the inline one.
 */
VOID
NTAPI
_KeInitializeSpinLock(
    _Out_ PKSPIN_LOCK SpinLock)
{
    *SpinLock = 0;
}

/*
 * Takes a spin lock at an IRQL that already excludes the thread dispatcher.
 * Nothing is raised here: a caller below DISPATCH_LEVEL that used this would
 * be preempted holding the lock, and every other processor would spin on it
 * until this one was scheduled again.
 */
VOID
NTAPI
KeAcquireSpinLockAtDpcLevel(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KxAcquireSpinLock(SpinLock);
}

VOID
NTAPI
KeReleaseSpinLockFromDpcLevel(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KxReleaseSpinLock(SpinLock);
}

VOID
FASTCALL
KefAcquireSpinLockAtDpcLevel(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KxAcquireSpinLock(SpinLock);
}

VOID
FASTCALL
KefReleaseSpinLockFromDpcLevel(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KxReleaseSpinLock(SpinLock);
}

/*
 * The raw acquire and release the architecture layer and the HAL reach for
 * when they already know the IRQL is right.
 */
VOID
FASTCALL
KiAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    KxAcquireSpinLock(SpinLock);
}

VOID
FASTCALL
KiReleaseSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    KxReleaseSpinLock(SpinLock);
}

/*
 * Takes a spin lock from code that may or may not already be at
 * DISPATCH_LEVEL, which is what a routine shared between a DPC and a thread
 * needs. Answers the IRQL to give back.
 */
KIRQL
FASTCALL
KeAcquireSpinLockForDpc(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    KIRQL OldIrql = KeGetCurrentIrql();

    if (OldIrql < DISPATCH_LEVEL)
        OldIrql = KfRaiseIrql(DISPATCH_LEVEL);

    KxAcquireSpinLock(SpinLock);
    return OldIrql;
}

VOID
FASTCALL
KeReleaseSpinLockForDpc(
    _Inout_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
    KxReleaseSpinLock(SpinLock);

    if (OldIrql < DISPATCH_LEVEL)
        KeLowerIrql(OldIrql);
}

/*
 * Says whether the lock is free. The answer is out of date the moment it is
 * given, so it is only good for a diagnostic or for deciding whether a
 * contended path is worth taking.
 */
BOOLEAN
FASTCALL
KeTestSpinLock(
    _In_ PKSPIN_LOCK SpinLock)
{
    return !(*(volatile KSPIN_LOCK *)SpinLock & 1);
}

/*
 * Takes the lock if it is free and answers whether it did. A caller that gets
 * FALSE has not got the lock and must not release it.
 */
BOOLEAN
FASTCALL
KeTryToAcquireSpinLockAtDpcLevel(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

#ifdef CONFIG_SMP
    if (InterlockedBitTestAndSet((PLONG)SpinLock, 0))
        return FALSE;
#endif

    KeMemoryBarrierWithoutFence();

#if DBG
    *SpinLock = (KSPIN_LOCK)KeGetCurrentThread() | 1;
#endif

    return TRUE;
}

/* PUBLIC FUNCTIONS: QUEUED SPIN LOCKS ****************************************/

/*
 * A queued spin lock makes each waiter spin on a word of its own, so a lock
 * held across a long operation does not cost every waiting processor the cache
 * line the lock itself lives in. The node the waiter spins on is the caller's
 * to supply, which is why there is no plain acquire for one.
 */
VOID
FASTCALL
KeAcquireQueuedSpinLockAtDpcLevel(
    _Inout_ PKSPIN_LOCK_QUEUE LockQueue)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KxAcquireQueuedSpinLock(LockQueue);
}

VOID
FASTCALL
KeReleaseQueuedSpinLockFromDpcLevel(
    _Inout_ PKSPIN_LOCK_QUEUE LockQueue)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KxReleaseQueuedSpinLock(LockQueue);
}

/*
 * The in-stack form: the queue node lives in the caller's frame, so any spin
 * lock can be used as a queued one without finding a node for it first.
 */
VOID
FASTCALL
KeAcquireInStackQueuedSpinLockAtDpcLevel(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Out_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    LockHandle->LockQueue.Next = NULL;
    LockHandle->LockQueue.Lock = SpinLock;
    LockHandle->OldIrql = KeGetCurrentIrql();

    KxAcquireQueuedSpinLock(&LockHandle->LockQueue);
}

VOID
FASTCALL
KeReleaseInStackQueuedSpinLockFromDpcLevel(
    _Inout_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    KxReleaseQueuedSpinLock(&LockHandle->LockQueue);
}

/*
 * As above, for code that may be below DISPATCH_LEVEL. The handle carries the
 * IRQL to give back, so the release does not need to be told.
 */
VOID
FASTCALL
KeAcquireInStackQueuedSpinLockForDpc(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Out_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    KIRQL OldIrql = KeGetCurrentIrql();

    if (OldIrql < DISPATCH_LEVEL)
        OldIrql = KfRaiseIrql(DISPATCH_LEVEL);

    LockHandle->LockQueue.Next = NULL;
    LockHandle->LockQueue.Lock = SpinLock;
    LockHandle->OldIrql = OldIrql;

    KxAcquireQueuedSpinLock(&LockHandle->LockQueue);
}

VOID
FASTCALL
KeReleaseInStackQueuedSpinLockForDpc(
    _Inout_ PKLOCK_QUEUE_HANDLE LockHandle)
{
    KIRQL OldIrql = LockHandle->OldIrql;

    KxReleaseQueuedSpinLock(&LockHandle->LockQueue);

    if (OldIrql < DISPATCH_LEVEL)
        KeLowerIrql(OldIrql);
}

/* PUBLIC FUNCTIONS: INTERRUPT SPIN LOCKS *************************************/

/*
 * Takes the lock an interrupt object's service routine runs under, at the IRQL
 * that routine runs at. That is how a driver reaches state its own interrupt
 * handler also touches without using KeSynchronizeExecution for it.
 */
KIRQL
NTAPI
KeAcquireInterruptSpinLock(
    _Inout_ PKINTERRUPT Interrupt)
{
    KIRQL OldIrql;

    OldIrql = KfRaiseIrql(Interrupt->SynchronizeIrql);
    KxAcquireSpinLock(Interrupt->ActualLock);

    return OldIrql;
}

VOID
NTAPI
KeReleaseInterruptSpinLock(
    _Inout_ PKINTERRUPT Interrupt,
    _In_ KIRQL OldIrql)
{
    KxReleaseSpinLock(Interrupt->ActualLock);
    KeLowerIrql(OldIrql);
}

/* PUBLIC FUNCTIONS: GUARDED MUTEXES ******************************************/

/*
 * The contended acquisition. The count word carries three things at once: the
 * lock bit, how many threads are waiting, and whether one of them has already
 * been woken and has yet to claim the lock.
 *
 * A release hands the lock on by setting the lock bit, marking one waiter
 * woken and taking that waiter's count off, then opening the gate. The woken
 * thread therefore arrives here owing nothing but the woken mark, which it
 * clears before competing for the lock as if it had just arrived.
 */
VOID
FASTCALL
KiAcquireGuardedMutex(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    LONG OldValue, NewValue;
    BOOLEAN Registered = FALSE;

    GuardedMutex->Contention++;

    for (;;)
    {
        OldValue = GuardedMutex->Count;

        if (OldValue & GM_LOCK_BIT)
        {
            /* Free: take it, and stop being counted as a waiter */
            NewValue = OldValue - GM_LOCK_BIT;
            if (Registered)
                NewValue -= GM_LOCK_WAITER_INC;

            if (InterlockedCompareExchange(&GuardedMutex->Count,
                                           NewValue,
                                           OldValue) == OldValue)
            {
                return;
            }

            continue;
        }

        if (!Registered)
        {
            NewValue = OldValue + GM_LOCK_WAITER_INC;
            if (InterlockedCompareExchange(&GuardedMutex->Count,
                                           NewValue,
                                           OldValue) != OldValue)
            {
                continue;
            }

            Registered = TRUE;
        }

        KeWaitForGate(&GuardedMutex->Gate, WrGuardedMutex, KernelMode);

        /*
         * The release already took our waiter count off; all that is left is
         * the woken mark, which has to go before anyone can be woken again.
         */
        InterlockedExchangeAdd(&GuardedMutex->Count, -GM_LOCK_WAITER_WOKEN);
        Registered = FALSE;
    }
}

VOID
FASTCALL
KeInitializeGuardedMutex(
    _Out_ PKGUARDED_MUTEX GuardedMutex)
{
    _KeInitializeGuardedMutex(GuardedMutex);
}

VOID
FASTCALL
KeAcquireGuardedMutex(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    _KeAcquireGuardedMutex(GuardedMutex);
}

VOID
FASTCALL
KeReleaseGuardedMutex(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    _KeReleaseGuardedMutex(GuardedMutex);
}

/*
 * The unsafe forms leave the APC exclusion to the caller, which has already
 * taken it out for a longer stretch than the lock itself.
 */
VOID
FASTCALL
KeAcquireGuardedMutexUnsafe(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    _KeAcquireGuardedMutexUnsafe(GuardedMutex);
}

VOID
FASTCALL
KeReleaseGuardedMutexUnsafe(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    _KeReleaseGuardedMutexUnsafe(GuardedMutex);
}

BOOLEAN
FASTCALL
KeTryToAcquireGuardedMutex(
    _Inout_ PKGUARDED_MUTEX GuardedMutex)
{
    return _KeTryToAcquireGuardedMutex(GuardedMutex);
}

/* PUBLIC FUNCTIONS: FAST MUTEXES *********************************************/

/*
 * The contended acquisition of a fast mutex. Unlike a guarded mutex this waits
 * on an ordinary event, which is what makes a fast mutex usable from code that
 * is not prepared to have special APCs kept out.
 */
VOID
FASTCALL
KiAcquireFastMutex(
    _Inout_ PFAST_MUTEX FastMutex)
{
    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    FastMutex->Contention++;

    KeWaitForSingleObject(&FastMutex->Event,
                          WrMutex,
                          KernelMode,
                          FALSE,
                          NULL);
}

/* EOF */
