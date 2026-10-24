/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux futexes, robust futex lists and the futex2 system calls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define LX_FUTEX_HASH_SIZE 256

typedef struct _LX_FUTEX_KEY
{
    PVOID Object;
    ULONG64 Offset;
} LX_FUTEX_KEY, *PLX_FUTEX_KEY;

typedef struct _LX_FUTEX_BLOCK
{
    KEVENT Event;
    LONG Woken;
    BOOLEAN Acquired;
} LX_FUTEX_BLOCK, *PLX_FUTEX_BLOCK;

typedef struct _LX_FUTEX_WAITER
{
    LIST_ENTRY Link;
    LX_FUTEX_KEY Key;
    PLX_FUTEX_BLOCK Block;
    ULONG Bitset;
    ULONG Index;
    ULONG Tid;
    BOOLEAN Pi;
    BOOLEAN Queued;
} LX_FUTEX_WAITER, *PLX_FUTEX_WAITER;

static LIST_ENTRY LxFutexHash[LX_FUTEX_HASH_SIZE];
static FAST_MUTEX LxFutexLock;

VOID
LxFutexInitialize(VOID)
{
    ULONG Index;

    for (Index = 0; Index < LX_FUTEX_HASH_SIZE; Index++)
        InitializeListHead(&LxFutexHash[Index]);
    ExInitializeFastMutex(&LxFutexLock);
}

static
PLIST_ENTRY
LxFutexBucket(
    _In_ PLX_FUTEX_KEY Key)
{
    ULONG64 Hash = ((ULONG64)(ULONG_PTR)Key->Object >> 4) ^ (Key->Offset >> 2);

    Hash *= 0x9E3779B97F4A7C15ULL;
    return &LxFutexHash[Hash >> 56];
}

static
BOOLEAN
LxFutexKeyEqual(
    _In_ PLX_FUTEX_KEY Left,
    _In_ PLX_FUTEX_KEY Right)
{
    return (Left->Object == Right->Object) && (Left->Offset == Right->Offset);
}

static
LONG
LxFutexReadLocked(
    _In_ ULONG64 Address,
    _Out_ PULONG Value)
{
    LONG Result = 0;

    _SEH2_TRY
    {
        ProbeForRead((PVOID)(ULONG_PTR)Address, sizeof(ULONG), sizeof(ULONG));
        *Value = *(volatile ULONG *)(ULONG_PTR)Address;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = -LX_EFAULT;
    }
    _SEH2_END;
    return Result;
}

static
LONG
LxFutexCmpxchgLocked(
    _In_ ULONG64 Address,
    _In_ ULONG Expected,
    _In_ ULONG New,
    _Out_ PULONG Previous)
{
    LONG Result = 0;

    _SEH2_TRY
    {
        ProbeForRead((PVOID)(ULONG_PTR)Address, sizeof(ULONG), sizeof(ULONG));
        *Previous = (ULONG)InterlockedCompareExchange((volatile LONG *)(ULONG_PTR)Address, (LONG)New, (LONG)Expected);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = -LX_EFAULT;
    }
    _SEH2_END;
    return Result;
}

static
BOOLEAN
LxFutexFault(
    _In_ ULONG64 Address,
    _In_ BOOLEAN Write)
{
    ULONG Value;

    if (!Write)
        return LxCopyFromUser(&Value, Address, sizeof(Value)) == 0;
    return LxMmFaultIn(Address, sizeof(ULONG), TRUE);
}

static
LONG
LxFutexGetKey(
    _In_ ULONG64 Address,
    _In_ ULONG Flags,
    _Out_ PLX_FUTEX_KEY Key)
{
    PLX_THREAD Thread = LxCurrentThread();
    ULONG64 Offset;
    PVOID Object;

    if (Flags & LX_FUTEX2_NUMA)
    {
        ULONG Node;
        LONG64 Result;

        if (Address & 7)
            return -LX_EINVAL;
        Result = LxCopyFromUser(&Node, Address + sizeof(ULONG), sizeof(Node));
        if (Result != 0)
            return (LONG)Result;
        if ((Node != LX_FUTEX_NO_NODE) && (Node != 0))
            return -LX_EINVAL;
        if (Node == LX_FUTEX_NO_NODE)
        {
            Node = 0;
            Result = LxCopyToUser(Address + sizeof(ULONG), &Node, sizeof(Node));
            if (Result != 0)
                return (LONG)Result;
        }
    }
    else if (Address & 3)
    {
        return -LX_EINVAL;
    }

    Key->Object = PsGetCurrentProcess();
    Key->Offset = Address;
    if (!(Flags & LX_FUTEX2_PRIVATE) && (Thread != NULL) &&
        LxMmSharedKey(Thread->Process, Address, &Object, &Offset))
    {
        Key->Object = Object;
        Key->Offset = Offset;
    }
    return 0;
}

static
VOID
LxFutexWakeWaiter(
    _In_ PLX_FUTEX_WAITER Waiter,
    _In_ BOOLEAN Acquired)
{
    RemoveEntryList(&Waiter->Link);
    Waiter->Queued = FALSE;
    if (Waiter->Block->Woken < 0)
    {
        Waiter->Block->Woken = (LONG)Waiter->Index;
        Waiter->Block->Acquired = Acquired;
        KeSetEvent(&Waiter->Block->Event, IO_NO_INCREMENT, FALSE);
    }
}

static
LONG
LxFutexWakeKey(
    _In_ PLX_FUTEX_KEY Key,
    _In_ LONG Count,
    _In_ ULONG Bitset)
{
    PLIST_ENTRY Bucket = LxFutexBucket(Key), Entry, Next;
    LONG Woken = 0;

    for (Entry = Bucket->Flink; (Entry != Bucket) && (Woken < Count); Entry = Next)
    {
        PLX_FUTEX_WAITER Waiter = CONTAINING_RECORD(Entry, LX_FUTEX_WAITER, Link);

        Next = Entry->Flink;
        if (!LxFutexKeyEqual(&Waiter->Key, Key) || !(Waiter->Bitset & Bitset))
            continue;
        if (Waiter->Pi)
            return -LX_EINVAL;
        if (Waiter->Block->Woken >= 0)
        {
            RemoveEntryList(&Waiter->Link);
            Waiter->Queued = FALSE;
            continue;
        }
        LxFutexWakeWaiter(Waiter, FALSE);
        Woken++;
    }
    return Woken;
}

static
PLX_FUTEX_WAITER
LxFutexFirstPiWaiter(
    _In_ PLX_FUTEX_KEY Key,
    _In_opt_ PLX_FUTEX_WAITER After)
{
    PLIST_ENTRY Bucket = LxFutexBucket(Key), Entry;

    for (Entry = (After != NULL) ? After->Link.Flink : Bucket->Flink; Entry != Bucket; Entry = Entry->Flink)
    {
        PLX_FUTEX_WAITER Waiter = CONTAINING_RECORD(Entry, LX_FUTEX_WAITER, Link);

        if (Waiter->Pi && (Waiter->Block->Woken < 0) && LxFutexKeyEqual(&Waiter->Key, Key))
            return Waiter;
    }
    return NULL;
}

static
VOID
LxFutexDequeue(
    _Inout_updates_(Count) PLX_FUTEX_WAITER Waiters,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Waiters[Index].Queued)
        {
            RemoveEntryList(&Waiters[Index].Link);
            Waiters[Index].Queued = FALSE;
        }
    }
}

static
PLARGE_INTEGER
LxFutexRemaining(
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ LONG64 Deadline,
    _Out_ PLARGE_INTEGER Remaining)
{
    LONG64 Now;

    if (Timeout == NULL)
        return NULL;
    Now = (LONG64)KeQueryInterruptTime();
    Remaining->QuadPart = (Now >= Deadline) ? 0 : -(Deadline - Now);
    return Remaining;
}

static
LONG64
LxFutexWaitVector(
    _In_reads_(Count) PLX_FUTEX_WAITV Vector,
    _In_ ULONG Count,
    _In_ ULONG Bitset,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ LONG64 Interrupted)
{
    LONG64 Deadline = (LONG64)KeQueryInterruptTime() - ((Timeout != NULL) ? Timeout->QuadPart : 0);
    PLX_FUTEX_WAITER Waiters;
    PLX_FUTEX_BLOCK Block;
    LARGE_INTEGER Remaining;
    NTSTATUS Status;
    LONG64 Result;
    ULONG Index;
    ULONG Value;

    Block = LxAlloc(sizeof(*Block) + Count * sizeof(*Waiters));
    if (Block == NULL)
        return -LX_ENOMEM;
    Waiters = (PLX_FUTEX_WAITER)(Block + 1);

    for (Index = 0; Index < Count; Index++)
    {
        Result = LxFutexGetKey(Vector[Index].Address, Vector[Index].Flags, &Waiters[Index].Key);
        if (Result != 0)
        {
            LxFree(Block);
            return Result;
        }
        Waiters[Index].Block = Block;
        Waiters[Index].Bitset = Bitset;
        Waiters[Index].Index = Index;
    }

    for (;;)
    {
        KeInitializeEvent(&Block->Event, NotificationEvent, FALSE);
        Block->Woken = -1;
        Block->Acquired = FALSE;

        ExAcquireFastMutex(&LxFutexLock);
        for (Index = 0, Result = 0; Index < Count; Index++)
        {
            Result = LxFutexReadLocked(Vector[Index].Address, &Value);
            if ((Result != 0) || (Value != (ULONG)Vector[Index].Value))
                break;
            InsertTailList(LxFutexBucket(&Waiters[Index].Key), &Waiters[Index].Link);
            Waiters[Index].Queued = TRUE;
        }
        if (Index < Count)
        {
            LxFutexDequeue(Waiters, Count);
            ExReleaseFastMutex(&LxFutexLock);
            if ((Result != 0) && LxFutexFault(Vector[Index].Address, FALSE))
                continue;
            Result = (Result != 0) ? -LX_EFAULT : -LX_EAGAIN;
            break;
        }
        ExReleaseFastMutex(&LxFutexLock);

        Status = LxWaitForEvent(&Block->Event, LxFutexRemaining(Timeout, Deadline, &Remaining));

        ExAcquireFastMutex(&LxFutexLock);
        LxFutexDequeue(Waiters, Count);
        ExReleaseFastMutex(&LxFutexLock);

        if (Block->Woken >= 0)
        {
            Result = Block->Woken;
            break;
        }
        if (Status == STATUS_SUCCESS)
            continue;
        Result = (Status == STATUS_TIMEOUT) ? -LX_ETIMEDOUT : Interrupted;
        break;
    }

    LxFree(Block);
    return Result;
}

static
LONG64
LxFutexWakeAddress(
    _In_ ULONG64 Address,
    _In_ ULONG Flags,
    _In_ LONG Count,
    _In_ ULONG Bitset,
    _In_ BOOLEAN Strict)
{
    LX_FUTEX_KEY Key;
    LONG64 Result;

    if (Bitset == 0)
        return -LX_EINVAL;
    Result = LxFutexGetKey(Address, Flags, &Key);
    if (Result != 0)
        return Result;
    if (Count <= 0)
    {
        if (Strict && (Count == 0))
            return 0;
        Count = 1;
    }

    ExAcquireFastMutex(&LxFutexLock);
    Result = LxFutexWakeKey(&Key, Count, Bitset);
    ExReleaseFastMutex(&LxFutexLock);
    return Result;
}

LONG64
LxFutexWake(
    _In_ ULONG64 Address,
    _In_ LONG Count)
{
    return LxFutexWakeAddress(Address, 0, Count, LX_FUTEX_BITSET_MATCH_ANY, FALSE);
}

static
LONG64
LxFutexRequeue(
    _In_ ULONG64 Address,
    _In_ ULONG64 Address2,
    _In_ ULONG Flags,
    _In_ LONG WakeCount,
    _In_ LONG RequeueCount,
    _In_opt_ PULONG Compare)
{
    LX_FUTEX_KEY Key, Key2;
    PLIST_ENTRY Bucket, Entry, Next, Last;
    LONG64 Result;
    LONG Total;
    ULONG Value;

    if ((WakeCount < 0) || (RequeueCount < 0))
        return -LX_EINVAL;
    Result = LxFutexGetKey(Address, Flags, &Key);
    if (Result == 0)
        Result = LxFutexGetKey(Address2, Flags, &Key2);
    if (Result != 0)
        return Result;

    for (;;)
    {
        ExAcquireFastMutex(&LxFutexLock);
        if (Compare != NULL)
        {
            Result = LxFutexReadLocked(Address, &Value);
            if (Result != 0)
            {
                ExReleaseFastMutex(&LxFutexLock);
                if (LxFutexFault(Address, FALSE))
                    continue;
                return -LX_EFAULT;
            }
            if (Value != *Compare)
            {
                ExReleaseFastMutex(&LxFutexLock);
                return -LX_EAGAIN;
            }
        }
        break;
    }

    Total = 0;
    Bucket = LxFutexBucket(&Key);
    Last = Bucket->Blink;
    for (Entry = Bucket->Flink; Entry != Bucket; Entry = Next)
    {
        PLX_FUTEX_WAITER Waiter = CONTAINING_RECORD(Entry, LX_FUTEX_WAITER, Link);
        BOOLEAN Final = (Entry == Last);

        Next = Entry->Flink;
        if (LxFutexKeyEqual(&Waiter->Key, &Key))
        {
            if (Waiter->Pi)
            {
                Total = -LX_EINVAL;
                break;
            }
            if (Waiter->Block->Woken >= 0)
            {
                RemoveEntryList(&Waiter->Link);
                Waiter->Queued = FALSE;
            }
            else if (Total < WakeCount)
            {
                LxFutexWakeWaiter(Waiter, FALSE);
                Total++;
            }
            else if (Total - WakeCount < RequeueCount)
            {
                RemoveEntryList(&Waiter->Link);
                Waiter->Key = Key2;
                InsertTailList(LxFutexBucket(&Key2), &Waiter->Link);
                Total++;
            }
            else
            {
                break;
            }
        }
        if (Final)
            break;
    }
    ExReleaseFastMutex(&LxFutexLock);
    return Total;
}

static
LONG64
LxFutexWakeOp(
    _In_ ULONG64 Address,
    _In_ ULONG64 Address2,
    _In_ ULONG Flags,
    _In_ LONG WakeCount,
    _In_ LONG WakeCount2,
    _In_ ULONG Encoded)
{
    ULONG Op = (Encoded >> 28) & 0xF;
    ULONG Cmp = (Encoded >> 24) & 0xF;
    LONG OpArg = ((LONG)(Encoded << 8)) >> 20;
    LONG CmpArg = ((LONG)(Encoded << 20)) >> 20;
    LX_FUTEX_KEY Key, Key2;
    ULONG Old = 0, New = 0, Previous = 0;
    BOOLEAN Matched;
    LONG64 Result;

    Result = LxFutexGetKey(Address, Flags, &Key);
    if (Result == 0)
        Result = LxFutexGetKey(Address2, Flags, &Key2);
    if (Result != 0)
        return Result;
    if (Op & LX_FUTEX_OP_OPARG_SHIFT)
        OpArg = (LONG)(1U << (OpArg & 31));
    Op &= 7;
    if (Op > 4)
        return -LX_ENOSYS;
    if (WakeCount <= 0)
        WakeCount = 1;
    if (WakeCount2 <= 0)
        WakeCount2 = 1;

    for (;;)
    {
        ExAcquireFastMutex(&LxFutexLock);
        Result = LxFutexReadLocked(Address2, &Old);
        if (Result == 0)
        {
            switch (Op)
            {
                case 0: New = (ULONG)OpArg; break;
                case 1: New = Old + (ULONG)OpArg; break;
                case 2: New = Old | (ULONG)OpArg; break;
                case 3: New = Old & ~(ULONG)OpArg; break;
                default: New = Old ^ (ULONG)OpArg; break;
            }
            Result = LxFutexCmpxchgLocked(Address2, Old, New, &Previous);
        }
        if (Result != 0)
        {
            ExReleaseFastMutex(&LxFutexLock);
            if (LxFutexFault(Address2, TRUE))
                continue;
            return -LX_EFAULT;
        }
        if (Previous != Old)
        {
            ExReleaseFastMutex(&LxFutexLock);
            continue;
        }
        break;
    }

    switch (Cmp)
    {
        case 0: Matched = ((LONG)Old == CmpArg); break;
        case 1: Matched = ((LONG)Old != CmpArg); break;
        case 2: Matched = ((LONG)Old < CmpArg); break;
        case 3: Matched = ((LONG)Old <= CmpArg); break;
        case 4: Matched = ((LONG)Old > CmpArg); break;
        case 5: Matched = ((LONG)Old >= CmpArg); break;
        default:
            ExReleaseFastMutex(&LxFutexLock);
            return -LX_ENOSYS;
    }

    Result = LxFutexWakeKey(&Key, WakeCount, LX_FUTEX_BITSET_MATCH_ANY);
    if ((Result >= 0) && Matched)
    {
        LONG Woken2 = LxFutexWakeKey(&Key2, WakeCount2, LX_FUTEX_BITSET_MATCH_ANY);

        Result = (Woken2 < 0) ? Woken2 : Result + Woken2;
    }
    ExReleaseFastMutex(&LxFutexLock);
    return Result;
}

static
BOOLEAN
LxFutexOwnerAlive(
    _In_ PLX_PROCESS Viewer,
    _In_ ULONG Tid)
{
    PLX_PROCESS Owner;
    PLX_THREAD Thread = LxThreadLookupNr(Viewer, Tid, &Owner);

    if (Owner != NULL)
        LxProcessDereference(Owner);
    return (Thread != NULL);
}

static
LONG64
LxFutexLockPi(
    _In_ PLX_THREAD Thread,
    _In_ ULONG64 Address,
    _In_ ULONG Flags,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ BOOLEAN Try)
{
    LONG64 Deadline = (LONG64)KeQueryInterruptTime() - ((Timeout != NULL) ? Timeout->QuadPart : 0);
    ULONG Tid = LxTidNr(Thread->Process, Thread);
    PLX_FUTEX_WAITER Waiter;
    PLX_FUTEX_BLOCK Block;
    LARGE_INTEGER Remaining;
    ULONG Value, New, Previous = 0;
    NTSTATUS Status;
    LONG64 Result;

    Block = LxAlloc(sizeof(*Block) + sizeof(*Waiter));
    if (Block == NULL)
        return -LX_ENOMEM;
    Waiter = (PLX_FUTEX_WAITER)(Block + 1);
    Result = LxFutexGetKey(Address, Flags, &Waiter->Key);
    if (Result != 0)
    {
        LxFree(Block);
        return Result;
    }
    Waiter->Block = Block;
    Waiter->Bitset = LX_FUTEX_BITSET_MATCH_ANY;
    Waiter->Tid = Tid;
    Waiter->Pi = TRUE;

    for (;;)
    {
        BOOLEAN Write = FALSE;

        ExAcquireFastMutex(&LxFutexLock);
        Result = LxFutexReadLocked(Address, &Value);
        if ((Result == 0) && ((Value & LX_FUTEX_TID_MASK) == Tid))
        {
            ExReleaseFastMutex(&LxFutexLock);
            Result = -LX_EDEADLK;
            break;
        }
        if ((Result == 0) && ((Value & LX_FUTEX_TID_MASK) == 0))
        {
            New = Tid | (Value & LX_FUTEX_OWNER_DIED) |
                  ((LxFutexFirstPiWaiter(&Waiter->Key, NULL) != NULL) ? LX_FUTEX_WAITERS : 0);
            Write = TRUE;
            Result = LxFutexCmpxchgLocked(Address, Value, New, &Previous);
            if ((Result == 0) && (Previous == Value))
            {
                ExReleaseFastMutex(&LxFutexLock);
                break;
            }
        }
        else if ((Result == 0) && Try)
        {
            ExReleaseFastMutex(&LxFutexLock);
            Result = -LX_EAGAIN;
            break;
        }
        else if ((Result == 0) && !LxFutexOwnerAlive(Thread->Process, Value & LX_FUTEX_TID_MASK))
        {
            ExReleaseFastMutex(&LxFutexLock);
            Result = -LX_ESRCH;
            break;
        }
        else if ((Result == 0) && !(Value & LX_FUTEX_WAITERS))
        {
            Write = TRUE;
            Result = LxFutexCmpxchgLocked(Address, Value, Value | LX_FUTEX_WAITERS, &Previous);
        }
        if ((Result == 0) && Write && (Previous != Value))
        {
            ExReleaseFastMutex(&LxFutexLock);
            continue;
        }
        if (Result != 0)
        {
            ExReleaseFastMutex(&LxFutexLock);
            if (LxFutexFault(Address, Write))
                continue;
            Result = -LX_EFAULT;
            break;
        }

        KeInitializeEvent(&Block->Event, NotificationEvent, FALSE);
        Block->Woken = -1;
        Block->Acquired = FALSE;
        InsertTailList(LxFutexBucket(&Waiter->Key), &Waiter->Link);
        Waiter->Queued = TRUE;
        ExReleaseFastMutex(&LxFutexLock);

        Status = LxWaitForEvent(&Block->Event, LxFutexRemaining(Timeout, Deadline, &Remaining));

        ExAcquireFastMutex(&LxFutexLock);
        LxFutexDequeue(Waiter, 1);
        ExReleaseFastMutex(&LxFutexLock);

        if (Block->Acquired)
        {
            Result = 0;
            break;
        }
        if ((Block->Woken >= 0) || (Status == STATUS_SUCCESS))
            continue;
        Result = (Status == STATUS_TIMEOUT) ? -LX_ETIMEDOUT : -LX_ERESTARTNOINTR;
        break;
    }

    LxFree(Block);
    return Result;
}

static
LONG64
LxFutexUnlockPi(
    _In_ ULONG Tid,
    _In_ ULONG64 Address,
    _In_ ULONG Flags)
{
    PLX_FUTEX_WAITER Next = NULL;
    ULONG Value, New, Previous = 0;
    LX_FUTEX_KEY Key;
    LONG64 Result;

    Result = LxFutexGetKey(Address, Flags, &Key);
    if (Result != 0)
        return Result;

    for (;;)
    {
        BOOLEAN Write = FALSE;

        ExAcquireFastMutex(&LxFutexLock);
        Result = LxFutexReadLocked(Address, &Value);
        if (Result == 0)
        {
            if ((Value & LX_FUTEX_TID_MASK) != Tid)
            {
                ExReleaseFastMutex(&LxFutexLock);
                return -LX_EPERM;
            }
            Next = LxFutexFirstPiWaiter(&Key, NULL);
            New = 0;
            if (Next != NULL)
                New = Next->Tid | ((LxFutexFirstPiWaiter(&Key, Next) != NULL) ? LX_FUTEX_WAITERS : 0);
            Write = TRUE;
            Result = LxFutexCmpxchgLocked(Address, Value, New, &Previous);
        }
        if (Result != 0)
        {
            ExReleaseFastMutex(&LxFutexLock);
            if (LxFutexFault(Address, Write))
                continue;
            return -LX_EFAULT;
        }
        if (Previous != Value)
        {
            ExReleaseFastMutex(&LxFutexLock);
            continue;
        }
        if (Next != NULL)
            LxFutexWakeWaiter(Next, TRUE);
        ExReleaseFastMutex(&LxFutexLock);
        return 0;
    }
}

static
VOID
LxFutexHandleDeath(
    _In_ ULONG64 Address,
    _In_ ULONG Tid,
    _In_ BOOLEAN Pi,
    _In_ BOOLEAN PendingOp)
{
    ULONG Value, Previous = 0;
    LX_FUTEX_KEY Key;
    LONG Result;

    if ((Address & 3) || (LxFutexGetKey(Address, 0, &Key) != 0))
        return;

    for (;;)
    {
        ExAcquireFastMutex(&LxFutexLock);
        Result = LxFutexReadLocked(Address, &Value);
        if (Result == 0)
        {
            if (PendingOp && !Pi && (Value == 0))
            {
                LxFutexWakeKey(&Key, 1, LX_FUTEX_BITSET_MATCH_ANY);
                ExReleaseFastMutex(&LxFutexLock);
                return;
            }
            if ((Value & LX_FUTEX_TID_MASK) != Tid)
            {
                ExReleaseFastMutex(&LxFutexLock);
                return;
            }
            Result = LxFutexCmpxchgLocked(Address, Value, (Value & LX_FUTEX_WAITERS) | LX_FUTEX_OWNER_DIED, &Previous);
        }
        if (Result != 0)
        {
            ExReleaseFastMutex(&LxFutexLock);
            if (LxFutexFault(Address, TRUE))
                continue;
            return;
        }
        if (Previous != Value)
        {
            ExReleaseFastMutex(&LxFutexLock);
            continue;
        }
        if (Value & LX_FUTEX_WAITERS)
        {
            PLX_FUTEX_WAITER Waiter = Pi ? LxFutexFirstPiWaiter(&Key, NULL) : NULL;

            if (Waiter != NULL)
                LxFutexWakeWaiter(Waiter, FALSE);
            else if (!Pi)
                LxFutexWakeKey(&Key, 1, LX_FUTEX_BITSET_MATCH_ANY);
        }
        ExReleaseFastMutex(&LxFutexLock);
        return;
    }
}

VOID
LxFutexExitRobustList(
    _In_ PLX_THREAD Thread)
{
    LX_ROBUST_LIST_HEAD Head;
    ULONG64 HeadAddress = Thread->RobustList;
    ULONG64 Entry, Next, Pending;
    ULONG Limit = LX_ROBUST_LIST_LIMIT;
    BOOLEAN Pi, PendingPi;

    Thread->RobustList = 0;
    if ((HeadAddress == 0) || (LxCopyFromUser(&Head, HeadAddress, sizeof(Head)) != 0))
        return;

    Pending = Head.ListOpPending & ~1ULL;
    PendingPi = (Head.ListOpPending & 1) != 0;
    Entry = Head.List & ~1ULL;
    Pi = (Head.List & 1) != 0;
    while (Entry != HeadAddress)
    {
        LONG64 Fetch = LxCopyFromUser(&Next, Entry, sizeof(Next));

        if (Entry != Pending)
            LxFutexHandleDeath(Entry + (ULONG64)Head.FutexOffset, LxTidNr(Thread->Process, Thread), Pi, FALSE);
        if (Fetch != 0)
            return;
        Entry = Next & ~1ULL;
        Pi = (Next & 1) != 0;
        if (--Limit == 0)
            break;
    }
    if (Pending != 0)
        LxFutexHandleDeath(Pending + (ULONG64)Head.FutexOffset, LxTidNr(Thread->Process, Thread), PendingPi, TRUE);
}

static
LONG64
LxFutexTimeout(
    _In_ ULONG64 Pointer,
    _In_ BOOLEAN Absolute,
    _In_ ULONG Clock,
    _Out_ PLARGE_INTEGER Timeout)
{
    LX_TIMESPEC Timespec, Now;
    LONG64 Nanoseconds;
    LONG64 Result = LxCopyFromUser(&Timespec, Pointer, sizeof(Timespec));

    if (Result != 0)
        return Result;
    if ((Timespec.Seconds < 0) || (Timespec.Nanoseconds < 0) || (Timespec.Nanoseconds >= 1000000000))
        return -LX_EINVAL;
    if (Absolute)
    {
        LxQueryClock(Clock, &Now);
        Timespec.Seconds -= Now.Seconds;
        Timespec.Nanoseconds -= Now.Nanoseconds;
        if (Timespec.Nanoseconds < 0)
        {
            Timespec.Nanoseconds += 1000000000;
            Timespec.Seconds--;
        }
        if (Timespec.Seconds < 0)
        {
            Timeout->QuadPart = 0;
            return 0;
        }
    }
    Nanoseconds = LxTimespecToNs(&Timespec);
    Timeout->QuadPart = -((Nanoseconds / 100) + ((Nanoseconds % 100) != 0));
    return 0;
}

LX_SYSCALL(LxSysFutex)
{
    ULONG64 Address = Call->Args[0];
    ULONG Op = (ULONG)Call->Args[1];
    ULONG Command = Op & ~(LX_FUTEX_PRIVATE | LX_FUTEX_CLOCK_REALTIME);
    ULONG Flags = (Op & LX_FUTEX_PRIVATE) ? LX_FUTEX2_PRIVATE : 0;
    ULONG Value = (ULONG)Call->Args[2];
    ULONG Value3 = (ULONG)Call->Args[5];
    PLARGE_INTEGER TimeoutPointer = NULL;
    LX_FUTEX_WAITV Vector;
    LARGE_INTEGER Timeout;
    LONG64 Result;

    if ((Op & LX_FUTEX_CLOCK_REALTIME) && (Command != LX_FUTEX_WAIT_BITSET) &&
        (Command != LX_FUTEX_WAIT_REQUEUE_PI) && (Command != LX_FUTEX_LOCK_PI2))
    {
        return -LX_ENOSYS;
    }

    if ((Call->Args[3] != 0) &&
        ((Command == LX_FUTEX_WAIT) || (Command == LX_FUTEX_WAIT_BITSET) ||
         (Command == LX_FUTEX_LOCK_PI) || (Command == LX_FUTEX_LOCK_PI2)))
    {
        ULONG Clock = ((Command == LX_FUTEX_LOCK_PI) || (Op & LX_FUTEX_CLOCK_REALTIME)) ?
                      LX_CLOCK_REALTIME : LX_CLOCK_MONOTONIC;

        Result = LxFutexTimeout(Call->Args[3], Command != LX_FUTEX_WAIT, Clock, &Timeout);
        if (Result != 0)
            return Result;
        TimeoutPointer = &Timeout;
    }

    switch (Command)
    {
        case LX_FUTEX_WAIT:
        case LX_FUTEX_WAIT_BITSET:
            if (Command == LX_FUTEX_WAIT)
                Value3 = LX_FUTEX_BITSET_MATCH_ANY;
            if (Value3 == 0)
                return -LX_EINVAL;
            Vector.Value = Value;
            Vector.Address = Address;
            Vector.Flags = Flags;
            Vector.Reserved = 0;
            Result = LxFutexWaitVector(&Vector, 1, Value3, TimeoutPointer,
                                       ((Command == LX_FUTEX_WAIT) && (TimeoutPointer != NULL)) ?
                                       -LX_EINTR : -LX_ERESTARTSYS);
            return (Result > 0) ? 0 : Result;

        case LX_FUTEX_WAKE:
        case LX_FUTEX_WAKE_BITSET:
            if (Command == LX_FUTEX_WAKE)
                Value3 = LX_FUTEX_BITSET_MATCH_ANY;
            return LxFutexWakeAddress(Address, Flags, (LONG)Value, Value3, FALSE);

        case LX_FUTEX_REQUEUE:
            return LxFutexRequeue(Address, Call->Args[4], Flags, (LONG)Value, (LONG)(ULONG)Call->Args[3], NULL);

        case LX_FUTEX_CMP_REQUEUE:
            return LxFutexRequeue(Address, Call->Args[4], Flags, (LONG)Value, (LONG)(ULONG)Call->Args[3], &Value3);

        case LX_FUTEX_WAKE_OP:
            return LxFutexWakeOp(Address, Call->Args[4], Flags, (LONG)Value, (LONG)(ULONG)Call->Args[3], Value3);

        case LX_FUTEX_LOCK_PI:
        case LX_FUTEX_LOCK_PI2:
            return LxFutexLockPi(Call->Thread, Address, Flags, TimeoutPointer, FALSE);

        case LX_FUTEX_TRYLOCK_PI:
            return LxFutexLockPi(Call->Thread, Address, Flags, NULL, TRUE);

        case LX_FUTEX_UNLOCK_PI:
            return LxFutexUnlockPi(LxTidNr(Call->Process, Call->Thread), Address, Flags);

        default:
            return -LX_ENOSYS;
    }
}

static
LONG
LxFutex2Validate(
    _In_ ULONG Flags,
    _In_ ULONG64 Value)
{
    if (Flags & ~LX_FUTEX2_VALID_MASK)
        return -LX_EINVAL;
    if ((Flags & LX_FUTEX2_SIZE_MASK) != LX_FUTEX2_SIZE_U32)
        return -LX_EINVAL;
    if (Value >> 32)
        return -LX_EINVAL;
    return 0;
}

static
LONG64
LxFutex2Timeout(
    _In_ ULONG64 Pointer,
    _In_ LONG Clock,
    _Out_ PLARGE_INTEGER Timeout,
    _Out_ PLARGE_INTEGER *TimeoutPointer)
{
    LONG64 Result;

    *TimeoutPointer = NULL;
    if (Pointer == 0)
        return 0;
    if ((Clock != LX_CLOCK_REALTIME) && (Clock != LX_CLOCK_MONOTONIC))
        return -LX_EINVAL;
    Result = LxFutexTimeout(Pointer, TRUE, (ULONG)Clock, Timeout);
    if (Result == 0)
        *TimeoutPointer = Timeout;
    return Result;
}

static
LONG64
LxFutex2ParseVector(
    _In_ ULONG64 Pointer,
    _Out_writes_(Count) PLX_FUTEX_WAITV Vector,
    _In_ ULONG Count)
{
    LONG64 Result;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        Result = LxCopyFromUser(&Vector[Index], Pointer + Index * sizeof(LX_FUTEX_WAITV), sizeof(LX_FUTEX_WAITV));
        if (Result != 0)
            return Result;
        if (Vector[Index].Reserved != 0)
            return -LX_EINVAL;
        Result = LxFutex2Validate(Vector[Index].Flags, Vector[Index].Value);
        if (Result != 0)
            return Result;
    }
    return 0;
}

LX_SYSCALL(LxSysFutexWaitv)
{
    ULONG Count = (ULONG)Call->Args[1];
    PLARGE_INTEGER TimeoutPointer;
    PLX_FUTEX_WAITV Vector;
    LARGE_INTEGER Timeout;
    LONG64 Result;

    if ((ULONG)Call->Args[2] != 0)
        return -LX_EINVAL;
    if ((Count == 0) || (Count > LX_FUTEX_WAITV_MAX) || (Call->Args[0] == 0))
        return -LX_EINVAL;
    Result = LxFutex2Timeout(Call->Args[3], (LONG)Call->Args[4], &Timeout, &TimeoutPointer);
    if (Result != 0)
        return Result;

    Vector = LxAlloc(Count * sizeof(*Vector));
    if (Vector == NULL)
        return -LX_ENOMEM;
    Result = LxFutex2ParseVector(Call->Args[0], Vector, Count);
    if (Result == 0)
        Result = LxFutexWaitVector(Vector, Count, LX_FUTEX_BITSET_MATCH_ANY, TimeoutPointer, -LX_ERESTARTSYS);
    LxFree(Vector);
    return Result;
}

LX_SYSCALL(LxSysFutexWake)
{
    ULONG Flags = (ULONG)Call->Args[3];
    LONG Result = LxFutex2Validate(Flags, Call->Args[1]);

    if (Result != 0)
        return Result;
    return LxFutexWakeAddress(Call->Args[0], Flags, (LONG)Call->Args[2], (ULONG)Call->Args[1], TRUE);
}

LX_SYSCALL(LxSysFutexWait)
{
    ULONG Flags = (ULONG)Call->Args[3];
    PLARGE_INTEGER TimeoutPointer;
    LX_FUTEX_WAITV Vector;
    LARGE_INTEGER Timeout;
    LONG64 Result;

    Result = LxFutex2Validate(Flags, Call->Args[1]);
    if (Result == 0)
        Result = LxFutex2Validate(Flags, Call->Args[2]);
    if (Result != 0)
        return Result;
    if ((ULONG)Call->Args[2] == 0)
        return -LX_EINVAL;
    Result = LxFutex2Timeout(Call->Args[4], (LONG)Call->Args[5], &Timeout, &TimeoutPointer);
    if (Result != 0)
        return Result;

    Vector.Value = Call->Args[1];
    Vector.Address = Call->Args[0];
    Vector.Flags = Flags;
    Vector.Reserved = 0;
    Result = LxFutexWaitVector(&Vector, 1, (ULONG)Call->Args[2], TimeoutPointer, -LX_ERESTARTSYS);
    return (Result > 0) ? 0 : Result;
}

LX_SYSCALL(LxSysFutexRequeue)
{
    LX_FUTEX_WAITV Vector[2];
    ULONG Compare;
    LONG64 Result;

    if (((ULONG)Call->Args[1] != 0) || (Call->Args[0] == 0))
        return -LX_EINVAL;
    Result = LxFutex2ParseVector(Call->Args[0], Vector, 2);
    if (Result != 0)
        return Result;
    if (Vector[0].Flags != Vector[1].Flags)
        return -LX_EINVAL;
    Compare = (ULONG)Vector[0].Value;
    return LxFutexRequeue(Vector[0].Address, Vector[1].Address, Vector[0].Flags,
                          (LONG)Call->Args[2], (LONG)Call->Args[3], &Compare);
}

LX_SYSCALL(LxSysSetRobustList)
{
    if (Call->Args[1] != sizeof(LX_ROBUST_LIST_HEAD))
        return -LX_EINVAL;
    Call->Thread->RobustList = Call->Args[0];
    return 0;
}

LX_SYSCALL(LxSysGetRobustList)
{
    LONG Pid = (LONG)Call->Args[0];
    ULONG64 Length = sizeof(LX_ROBUST_LIST_HEAD);
    ULONG64 Head;
    LONG64 Result;

    if (Pid == 0)
    {
        Head = Call->Thread->RobustList;
    }
    else
    {
        PLX_PROCESS Owner;
        PLX_THREAD Target = (Pid > 0) ? LxThreadLookupNr(Call->Process, (ULONG)Pid, &Owner) : NULL;

        if (Target == NULL)
            return -LX_ESRCH;
        Head = Target->RobustList;
        if ((Call->Process->Uid[1] != 0) && (Owner->Uid[0] != Call->Process->Uid[0]))
            Result = -LX_EPERM;
        else
            Result = 0;
        LxProcessDereference(Owner);
        if (Result != 0)
            return Result;
    }

    Result = LxCopyToUser(Call->Args[2], &Length, sizeof(Length));
    if (Result == 0)
        Result = LxCopyToUser(Call->Args[1], &Head, sizeof(Head));
    return Result;
}
