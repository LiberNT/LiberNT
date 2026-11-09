/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The push lock and the queue of threads waiting on one
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ULONG ExPushLockSpinCount = 0;

/* PRIVATE DATA ***************************************************************/

#define EXP_PUSH_LOCK_PTR_MASK (~EX_PUSH_LOCK_PTR_BITS)

/*
 * A waiter that sleeps on an event instead of a gate, so a caller that gave a
 * timeout can stop waiting. Only the untimed paths can use the gate.
 */
#define EXP_PUSH_LOCK_FLAGS_EVENT 4

/* PRIVATE FUNCTIONS **********************************************************/

static
PEX_PUSH_LOCK_WAIT_BLOCK
ExpPushLockChain(
    _In_ EX_PUSH_LOCK Value)
{
    return (PEX_PUSH_LOCK_WAIT_BLOCK)(Value.Value & EXP_PUSH_LOCK_PTR_MASK);
}

static
VOID
ExpPrepareWaitBlock(
    _Out_ PEX_PUSH_LOCK_WAIT_BLOCK WaitBlock,
    _In_ LONG Flags)
{
    WaitBlock->Flags = Flags | EX_PUSH_LOCK_FLAGS_WAIT;
    WaitBlock->Next = NULL;
    WaitBlock->Last = NULL;
    WaitBlock->Previous = NULL;
    WaitBlock->ShareCount = 0;

    if (Flags & EXP_PUSH_LOCK_FLAGS_EVENT)
    {
        KeInitializeEvent(&WaitBlock->WakeEvent, SynchronizationEvent, FALSE);
    }
    else
    {
        KeInitializeGate(&WaitBlock->WakeGate);
    }
}

/*
 * Both sides clear the wait bit and exactly one of them finds it set. Whoever
 * finds it set arrived first: a waiter that arrives first must sleep, a waker
 * that arrives first need not signal anything.
 */
static
NTSTATUS
ExpSleepOnWaitBlock(
    _Inout_ PEX_PUSH_LOCK_WAIT_BLOCK WaitBlock,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    if (!InterlockedBitTestAndReset(&WaitBlock->Flags, EX_PUSH_LOCK_FLAGS_WAIT_V))
    {
        return STATUS_SUCCESS;
    }

    if (WaitBlock->Flags & EXP_PUSH_LOCK_FLAGS_EVENT)
    {
        return KeWaitForSingleObject(&WaitBlock->WakeEvent,
                                     WrPushLock,
                                     KernelMode,
                                     FALSE,
                                     Timeout);
    }

    KeWaitForGate(&WaitBlock->WakeGate, WrPushLock, KernelMode);
    return STATUS_SUCCESS;
}

static
VOID
ExpWakeWaitBlock(
    _Inout_ PEX_PUSH_LOCK_WAIT_BLOCK WaitBlock)
{
    if (InterlockedBitTestAndReset(&WaitBlock->Flags, EX_PUSH_LOCK_FLAGS_WAIT_V))
    {
        return;
    }

    if (WaitBlock->Flags & EXP_PUSH_LOCK_FLAGS_EVENT)
    {
        KeSetEventBoostPriority(&WaitBlock->WakeEvent, NULL);
    }
    else
    {
        KeSignalGateBoostPriority(&WaitBlock->WakeGate);
    }
}

/*
 * Walks the chain from its head to its tail, filling in the back pointers and
 * caching the tail on the head, so a later walk stops at the first block that
 * already knows where the tail is.
 */
static
PEX_PUSH_LOCK_WAIT_BLOCK
ExpFindWaitBlockChainTail(
    _In_ PEX_PUSH_LOCK_WAIT_BLOCK Head)
{
    PEX_PUSH_LOCK_WAIT_BLOCK Block = Head;
    PEX_PUSH_LOCK_WAIT_BLOCK Tail;

    while (Block->Last == NULL)
    {
        Block->Next->Previous = Block;
        Block = Block->Next;
    }

    Tail = Block->Last;
    Head->Last = Tail;
    return Tail;
}

/*
 * Hands the lock to the threads at the tail of the chain: one exclusive waiter,
 * or every shared waiter up to the first exclusive one. The chain keeps the
 * waiters that are left; the lock bit is not handed over, so a woken thread
 * contends for the lock again.
 */
static
VOID
ExpReleaseWaitChain(
    _Inout_ PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK OldValue, NewValue;
    PEX_PUSH_LOCK_WAIT_BLOCK Head, Tail, Wake, Previous;

    for (;;)
    {
        OldValue.Value = (ULONG_PTR)ReadPointerNoFence(&PushLock->Ptr);

        ASSERT(OldValue.Waking);

        if (!OldValue.Waiting)
        {
            NewValue.Value = OldValue.Value & ~EX_PUSH_LOCK_WAKING;
            if (ExpChangePushlock(PushLock, NewValue.Ptr, OldValue.Ptr) ==
                OldValue.Ptr)
            {
                return;
            }

            continue;
        }

        /* An owner took the lock again; it wakes the chain when it releases. */
        if (OldValue.Locked)
        {
            NewValue.Value = OldValue.Value & ~EX_PUSH_LOCK_WAKING;
            if (ExpChangePushlock(PushLock, NewValue.Ptr, OldValue.Ptr) ==
                OldValue.Ptr)
            {
                return;
            }

            continue;
        }

        Head = ExpPushLockChain(OldValue);
        Tail = ExpFindWaitBlockChainTail(Head);

        Wake = Tail;
        if (!(Tail->Flags & EX_PUSH_LOCK_FLAGS_EXCLUSIVE))
        {
            while ((Wake->Previous != NULL) &&
                   !(Wake->Previous->Flags & EX_PUSH_LOCK_FLAGS_EXCLUSIVE))
            {
                Wake = Wake->Previous;
            }
        }

        Previous = Wake->Previous;
        if (Previous == NULL)
        {
            NewValue.Value = 0;
        }
        else
        {
            Previous->Next = NULL;
            Previous->Last = Previous;
            NewValue.Value = (ULONG_PTR)Head | EX_PUSH_LOCK_WAITING |
                             EX_PUSH_LOCK_LOCK;
            if (Head == Previous)
            {
                Head->Last = Previous;
            }
        }

        if (ExpChangePushlock(PushLock, NewValue.Ptr, OldValue.Ptr) !=
            OldValue.Ptr)
        {
            if (Previous != NULL)
            {
                Previous->Next = Wake;
                Previous->Last = NULL;
            }

            continue;
        }

        while (Wake != NULL)
        {
            PEX_PUSH_LOCK_WAIT_BLOCK Next = Wake->Next;

            ExpWakeWaitBlock(Wake);
            Wake = Next;
        }

        return;
    }
}

/*
 * Puts the caller's block at the head of the chain. The block that lands at the
 * tail remembers how many shared owners the lock had when the chain started, so
 * the count survives while the chain occupies those bits.
 */
static
BOOLEAN
ExpLinkWaitBlock(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _Inout_ PEX_PUSH_LOCK_WAIT_BLOCK WaitBlock,
    _In_ EX_PUSH_LOCK OldValue,
    _In_ BOOLEAN KeepLocked)
{
    EX_PUSH_LOCK NewValue;

    if (OldValue.Waiting)
    {
        WaitBlock->Last = NULL;
        WaitBlock->Next = ExpPushLockChain(OldValue);
        NewValue.Value = (ULONG_PTR)WaitBlock | EX_PUSH_LOCK_WAITING |
                         (OldValue.Value & (EX_PUSH_LOCK_LOCK |
                                            EX_PUSH_LOCK_WAKING |
                                            EX_PUSH_LOCK_MULTIPLE_SHARED));
    }
    else
    {
        WaitBlock->Last = WaitBlock;
        WaitBlock->Next = NULL;
        WaitBlock->ShareCount = (LONG)OldValue.Shared;
        NewValue.Value = (ULONG_PTR)WaitBlock | EX_PUSH_LOCK_WAITING;
        if (KeepLocked)
        {
            NewValue.Value |= OldValue.Value & EX_PUSH_LOCK_LOCK;
        }

        if (WaitBlock->ShareCount > 1)
        {
            NewValue.Value |= EX_PUSH_LOCK_MULTIPLE_SHARED;
        }
    }

    return ExpChangePushlock(PushLock, NewValue.Ptr, OldValue.Ptr) ==
           OldValue.Ptr;
}

static
VOID
ExpSpinOnPushLock(VOID)
{
    ULONG Spin = ExPushLockSpinCount;

    while (Spin-- != 0)
    {
        YieldProcessor();
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

#undef ExInitializePushLock
VOID
NTAPI
ExInitializePushLock(
    _Out_ PULONG_PTR PushLock)
{
    *PushLock = 0;
}

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializePushLocks(VOID)
{
    ExPushLockSpinCount = (KeNumberProcessors > 1) ? 1024 : 0;
}

VOID
FASTCALL
ExfAcquirePushLockExclusive(
    _Inout_ PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK_WAIT_BLOCK WaitBlock;
    EX_PUSH_LOCK OldValue, NewValue;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    for (;;)
    {
        OldValue.Value = (ULONG_PTR)ReadPointerNoFence(&PushLock->Ptr);

        if (!OldValue.Locked)
        {
            NewValue.Value = OldValue.Value | EX_PUSH_LOCK_LOCK;
            if (ExpChangePushlock(PushLock, NewValue.Ptr, OldValue.Ptr) ==
                OldValue.Ptr)
            {
                return;
            }

            continue;
        }

        ExpSpinOnPushLock();

        ExpPrepareWaitBlock(&WaitBlock, EX_PUSH_LOCK_FLAGS_EXCLUSIVE);
        if (!ExpLinkWaitBlock(PushLock, &WaitBlock, OldValue, TRUE))
        {
            continue;
        }

        ExpSleepOnWaitBlock(&WaitBlock, NULL);
    }
}

VOID
FASTCALL
ExfAcquirePushLockShared(
    _Inout_ PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK_WAIT_BLOCK WaitBlock;
    EX_PUSH_LOCK OldValue, NewValue;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    for (;;)
    {
        OldValue.Value = (ULONG_PTR)ReadPointerNoFence(&PushLock->Ptr);

        if (!OldValue.Waiting)
        {
            if (!OldValue.Locked)
            {
                NewValue.Value = EX_PUSH_LOCK_LOCK | EX_PUSH_LOCK_SHARE_INC;
            }
            else if (OldValue.Shared != 0)
            {
                NewValue.Value = OldValue.Value + EX_PUSH_LOCK_SHARE_INC;
                if (OldValue.Shared > 1)
                {
                    NewValue.Value |= EX_PUSH_LOCK_MULTIPLE_SHARED;
                }
            }
            else
            {
                NewValue.Value = 0;
            }

            if (NewValue.Value != 0)
            {
                if (ExpChangePushlock(PushLock, NewValue.Ptr, OldValue.Ptr) ==
                    OldValue.Ptr)
                {
                    return;
                }

                continue;
            }
        }

        ExpSpinOnPushLock();

        ExpPrepareWaitBlock(&WaitBlock, 0);
        if (!ExpLinkWaitBlock(PushLock, &WaitBlock, OldValue, TRUE))
        {
            continue;
        }

        ExpSleepOnWaitBlock(&WaitBlock, NULL);
    }
}

BOOLEAN
FASTCALL
ExfTryAcquirePushLockShared(
    _Inout_ PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK OldValue, NewValue;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    for (;;)
    {
        OldValue.Value = (ULONG_PTR)ReadPointerNoFence(&PushLock->Ptr);

        if (OldValue.Waiting)
        {
            return FALSE;
        }

        if (!OldValue.Locked)
        {
            NewValue.Value = EX_PUSH_LOCK_LOCK | EX_PUSH_LOCK_SHARE_INC;
        }
        else if (OldValue.Shared != 0)
        {
            NewValue.Value = OldValue.Value + EX_PUSH_LOCK_SHARE_INC;
            if (OldValue.Shared > 1)
            {
                NewValue.Value |= EX_PUSH_LOCK_MULTIPLE_SHARED;
            }
        }
        else
        {
            return FALSE;
        }

        if (ExpChangePushlock(PushLock, NewValue.Ptr, OldValue.Ptr) ==
            OldValue.Ptr)
        {
            return TRUE;
        }
    }
}

VOID
FASTCALL
ExfTryToWakePushLock(
    _Inout_ PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK OldValue, NewValue;

    for (;;)
    {
        OldValue.Value = (ULONG_PTR)ReadPointerNoFence(&PushLock->Ptr);

        if (OldValue.Locked || OldValue.Waking || !OldValue.Waiting)
        {
            return;
        }

        NewValue.Value = OldValue.Value | EX_PUSH_LOCK_WAKING;
        if (ExpChangePushlock(PushLock, NewValue.Ptr, OldValue.Ptr) ==
            OldValue.Ptr)
        {
            ExpReleaseWaitChain(PushLock);
            return;
        }
    }
}

VOID
FASTCALL
ExfReleasePushLockExclusive(
    _Inout_ PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK OldValue;

    OldValue.Value = InterlockedExchangeAddSizeT((PSIZE_T)PushLock,
                                                 -(SSIZE_T)EX_PUSH_LOCK_LOCK);

    ASSERT(OldValue.Locked);

    if (OldValue.Waiting && !OldValue.Waking)
    {
        ExfTryToWakePushLock(PushLock);
    }
}

VOID
FASTCALL
ExfReleasePushLockShared(
    _Inout_ PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK OldValue, NewValue;
    PEX_PUSH_LOCK_WAIT_BLOCK Head, Tail;

    for (;;)
    {
        OldValue.Value = (ULONG_PTR)ReadPointerNoFence(&PushLock->Ptr);

        ASSERT(OldValue.Locked);

        if (!OldValue.Waiting)
        {
            ASSERT(OldValue.Shared != 0);

            if (OldValue.Shared > 1)
            {
                NewValue.Value = OldValue.Value - EX_PUSH_LOCK_SHARE_INC;
                if (OldValue.Shared == 2)
                {
                    NewValue.Value &= ~EX_PUSH_LOCK_MULTIPLE_SHARED;
                }
            }
            else
            {
                NewValue.Value = 0;
            }

            if (ExpChangePushlock(PushLock, NewValue.Ptr, OldValue.Ptr) ==
                OldValue.Ptr)
            {
                return;
            }

            continue;
        }

        /*
         * The chain holds the share count, because the waiters occupy the bits
         * that would otherwise carry it.
         */
        Head = ExpPushLockChain(OldValue);
        Tail = ExpFindWaitBlockChainTail(Head);

        ASSERT(Tail->ShareCount > 0);

        if (InterlockedDecrement(&Tail->ShareCount) > 0)
        {
            return;
        }

        if (!OldValue.Waking)
        {
            ExfTryToWakePushLock(PushLock);
        }

        return;
    }
}

VOID
FASTCALL
ExfReleasePushLock(
    _Inout_ PEX_PUSH_LOCK PushLock)
{
    EX_PUSH_LOCK OldValue;

    OldValue.Value = (ULONG_PTR)ReadPointerNoFence(&PushLock->Ptr);

    ASSERT(OldValue.Locked);

    if (!OldValue.Waiting && (OldValue.Shared != 0))
    {
        ExfReleasePushLockShared(PushLock);
        return;
    }

    ExfReleasePushLockExclusive(PushLock);
}

VOID
FASTCALL
ExAcquirePushLockExclusiveEx(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    ExAcquirePushLockExclusive(PushLock);
}

VOID
FASTCALL
ExAcquirePushLockSharedEx(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    ExAcquirePushLockShared(PushLock);
}

BOOLEAN
FASTCALL
ExTryAcquirePushLockExclusiveEx(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    return ExTryToAcquirePushLockExclusive(PushLock);
}

BOOLEAN
FASTCALL
ExTryAcquirePushLockSharedEx(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    return ExfTryAcquirePushLockShared(PushLock);
}

VOID
FASTCALL
ExReleasePushLockExclusiveEx(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    ExfReleasePushLockExclusive(PushLock);
}

VOID
FASTCALL
ExReleasePushLockSharedEx(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    ExfReleasePushLockShared(PushLock);
}

VOID
FASTCALL
ExReleasePushLockEx(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    ExfReleasePushLock(PushLock);
}

/*
 * Queues a block on the lock without asking for the lock. The caller owns the
 * lock exclusively and keeps owning it; the block is woken by
 * ExfUnblockPushLock, not by a release.
 */
VOID
FASTCALL
ExBlockPushLock(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _Inout_ PVOID WaitBlock)
{
    PEX_PUSH_LOCK_WAIT_BLOCK Block = WaitBlock;
    EX_PUSH_LOCK OldValue;

    ExpPrepareWaitBlock(Block, 0);

    for (;;)
    {
        OldValue.Value = (ULONG_PTR)ReadPointerNoFence(&PushLock->Ptr);

        Block->Last = NULL;
        Block->Next = OldValue.Waiting ? ExpPushLockChain(OldValue) : NULL;
        if (Block->Next == NULL)
        {
            Block->Last = Block;
            Block->ShareCount = (LONG)OldValue.Shared;
        }

        if (ExpLinkWaitBlock(PushLock, Block, OldValue, TRUE))
        {
            return;
        }
    }
}

VOID
FASTCALL
ExWaitForUnblockPushLock(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _Inout_ PVOID WaitBlock)
{
    UNREFERENCED_PARAMETER(PushLock);

    ExpSleepOnWaitBlock((PEX_PUSH_LOCK_WAIT_BLOCK)WaitBlock, NULL);
}

NTSTATUS
FASTCALL
ExTimedWaitForUnblockPushLock(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _Inout_ PVOID WaitBlock,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PEX_PUSH_LOCK_WAIT_BLOCK Block = WaitBlock;

    UNREFERENCED_PARAMETER(PushLock);

    if (Timeout == NULL)
    {
        ExpSleepOnWaitBlock(Block, NULL);
        return STATUS_SUCCESS;
    }

    /* FIXME: a block queued as a gate waiter cannot be given a deadline; the
       wait runs to completion instead of timing out. */
    if (!(Block->Flags & EXP_PUSH_LOCK_FLAGS_EVENT))
    {
        ExpSleepOnWaitBlock(Block, NULL);
        return STATUS_SUCCESS;
    }

    return ExpSleepOnWaitBlock(Block, Timeout);
}

VOID
FASTCALL
ExfUnblockPushLock(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _Inout_ PVOID CurrentWaitBlock)
{
    PEX_PUSH_LOCK_WAIT_BLOCK Block = CurrentWaitBlock;
    EX_PUSH_LOCK OldValue, NewValue;
    PEX_PUSH_LOCK_WAIT_BLOCK Head;

    for (;;)
    {
        OldValue.Value = (ULONG_PTR)ReadPointerNoFence(&PushLock->Ptr);

        if (!OldValue.Waiting)
        {
            break;
        }

        Head = ExpPushLockChain(OldValue);
        if (Head != Block)
        {
            ExpFindWaitBlockChainTail(Head);
            break;
        }

        if (Block->Next == NULL)
        {
            NewValue.Value = OldValue.Value &
                             (EX_PUSH_LOCK_LOCK | EX_PUSH_LOCK_WAKING);
            if (Block->ShareCount > 0)
            {
                NewValue.Value |= (ULONG_PTR)Block->ShareCount *
                                  EX_PUSH_LOCK_SHARE_INC;
                if (Block->ShareCount > 1)
                {
                    NewValue.Value |= EX_PUSH_LOCK_MULTIPLE_SHARED;
                }
            }
        }
        else
        {
            NewValue.Value = (ULONG_PTR)Block->Next | EX_PUSH_LOCK_WAITING |
                             (OldValue.Value & (EX_PUSH_LOCK_LOCK |
                                                EX_PUSH_LOCK_WAKING |
                                                EX_PUSH_LOCK_MULTIPLE_SHARED));
        }

        if (ExpChangePushlock(PushLock, NewValue.Ptr, OldValue.Ptr) ==
            OldValue.Ptr)
        {
            break;
        }
    }

    ExpWakeWaitBlock(Block);
}

/*
 * Lets go of the lock and waits until the watched bytes stop matching what the
 * caller last saw, then takes the lock again. The caller holds the lock
 * exclusively on the way in and on the way out.
 */
NTSTATUS
FASTCALL
ExBlockOnAddressPushLock(
    _Inout_ PEX_PUSH_LOCK PushLock,
    _In_reads_bytes_(AddressSize) volatile VOID *Address,
    _In_reads_bytes_(AddressSize) PVOID CompareAddress,
    _In_ SIZE_T AddressSize,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    EX_PUSH_LOCK_WAIT_BLOCK WaitBlock;
    EX_PUSH_LOCK OldValue;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    if (RtlCompareMemory((PVOID)Address, CompareAddress, AddressSize) ==
        AddressSize)
    {
        ExpPrepareWaitBlock(&WaitBlock, EXP_PUSH_LOCK_FLAGS_EVENT);

        for (;;)
        {
            OldValue.Value = (ULONG_PTR)ReadPointerNoFence(&PushLock->Ptr);
            if (ExpLinkWaitBlock(PushLock, &WaitBlock, OldValue, FALSE))
            {
                break;
            }
        }

        if (OldValue.Waiting && !OldValue.Waking)
        {
            ExfTryToWakePushLock(PushLock);
        }

        Status = ExpSleepOnWaitBlock(&WaitBlock, Timeout);

        ExAcquirePushLockExclusive(PushLock);
        return Status;
    }

    return STATUS_SUCCESS;
}

/* EOF */
