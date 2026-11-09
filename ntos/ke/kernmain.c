/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the kernel owns before any of it runs, and how it is started
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "internal/napi.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* What the boot loader handed the kernel. It stays valid only until the
   initialisation that reads it is over. */
PLOADER_PARAMETER_BLOCK KeLoaderBlock;

/* What the processors in this system can do. Filled in by the architecture
   layer as each one starts, and read by everything that has an alternative. */
ULONG64 KeFeatureBits;

/* What the processors are, as the version reporting interfaces describe them. */
USHORT KeProcessorArchitecture;
USHORT KeProcessorLevel;
USHORT KeProcessorRevision;

/* The processor blocks, indexed by processor number. An entry is written once,
   by the processor it belongs to, before that processor is announced. */
PKPRCB KiProcessorBlock[MAXIMUM_PROCESSORS];

/* The system's memory nodes. A system that reports no topology still has one,
   so that everything which places work by node has somewhere to place it. */
KNODE KiNode0;
PKNODE KeNodeBlock[KI_MAX_NUMA_NODES] = { &KiNode0 };
UCHAR KeNumberNodes = 1;
UCHAR KeProcessNodeSeed;

/* The first thread and process, built into the image because there is nothing
   to allocate them from until they are running. */
ETHREAD KiInitialThread;
EPROCESS KiInitialProcess;

/* The dispatcher database lock, for the targets whose queued lock array is
   indexed rather than held per processor. */
KSPIN_LOCK KiDispatcherLock;

/*
 * The timer table lock. One word per group of buckets, so that two processors
 * arming timers whose due times fall in different parts of the table do not
 * contend for the same cache line. KiTimerTableLock is the first of them and
 * keeps the name the rest of the kernel knows.
 */
KSPIN_LOCK KiTimerTableLock;
static KSPIN_LOCK KiTimerTableLocks[LOCK_QUEUE_TIMER_TABLE_LOCKS - 1];

/* Serialises the reverse-stall barrier the broadcast paths use. */
KSPIN_LOCK KiReverseStallIpiLock;

/* How many system services the kernel's own table holds. */
ULONG KiServiceLimit = NUMBER_OF_SYSCALLS;

/*
 * Locks that belong to other components but have to exist before those
 * components initialise, because the paths that take them run first.
 */
KSPIN_LOCK MmPfnLock;
KSPIN_LOCK MmSystemSpaceLock;
KSPIN_LOCK MmNonPagedPoolLock;
KSPIN_LOCK NonPagedPoolLock;
KSPIN_LOCK CcBcbSpinLock;
KSPIN_LOCK CcMasterSpinLock;
KSPIN_LOCK CcVacbSpinLock;
KSPIN_LOCK CcWorkQueueSpinLock;
KSPIN_LOCK IopCancelSpinLock;
KSPIN_LOCK IopCompletionLock;
KSPIN_LOCK IopDatabaseLock;
extern KSPIN_LOCK IopVpbSpinLock;
KSPIN_LOCK NtfsStructLock;
KSPIN_LOCK AfdWorkQueueSpinLock;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Runs a callout on a stack of its own. The thread's own stack pointer is
 * moved to the new stack for the duration and put back afterwards, so the
 * callout sees a stack as deep as it asked for without every thread in the
 * system having to carry one.
 */
static
NTSTATUS
KiCalloutOnNewStack(
    _In_ PEXPAND_STACK_CALLOUT Callout,
    _In_opt_ PVOID Parameter,
    _In_ SIZE_T Size)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PVOID NewStack, OldStack;
    PVOID OldInitialStack, OldStackBase;
    ULONG_PTR OldStackLimit;

    UNREFERENCED_PARAMETER(Size);

    NewStack = MmCreateKernelStack(TRUE, (UCHAR)Thread->IdealProcessor);
    if (NewStack == NULL)
        return STATUS_NO_MEMORY;

    OldInitialStack = Thread->InitialStack;
    OldStackBase = Thread->StackBase;
    OldStackLimit = Thread->StackLimit;

    OldStack = KeSwitchKernelStack(NewStack,
                                   (PVOID)((ULONG_PTR)NewStack -
                                           KERNEL_LARGE_STACK_SIZE));

    Callout(Parameter);

    KeSwitchKernelStack(OldStack,
                        (PVOID)((ULONG_PTR)OldInitialStack -
                                KERNEL_STACK_SIZE));

    Thread->InitialStack = OldInitialStack;
    Thread->StackBase = OldStackBase;
    Thread->StackLimit = OldStackLimit;

    MmDeleteKernelStack(NewStack, TRUE);
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Works out a multiplier and a shift that stand in for a division by Divisor,
 * so that the clock path can turn a time into a tick index without dividing.
 *
 * The answer satisfies
 *     x / Divisor == UnsignedMultiplyHigh(x, Reciprocal) >> Shift
 * for the magnitudes the clock produces. It is found by long division of one
 * by the divisor: the leading zeroes of the quotient are counted into the
 * shift instead of being stored, so all sixty-four bits kept are significant.
 */
CODE_SEG("INIT")
LARGE_INTEGER
NTAPI
KiComputeReciprocal(
    _In_ LONG Divisor,
    _Out_ PUCHAR Shift)
{
    LARGE_INTEGER Reciprocal;
    ULONG64 Fraction = 0;
    ULONG64 Remainder = 1;
    ULONG64 Value;
    ULONG BitCount = 0;
    UCHAR ShiftCount = 0;

    ASSERT(Divisor > 0);
    Value = (ULONG64)(ULONG)Divisor;

    while (BitCount < 64)
    {
        Remainder <<= 1;

        if (Remainder >= Value)
        {
            Remainder -= Value;
            Fraction = (Fraction << 1) | 1;
            BitCount++;
        }
        else if (Fraction != 0)
        {
            Fraction <<= 1;
            BitCount++;
        }
        else
        {
            /* The quotient has not started yet; this is a shift, not a bit */
            ShiftCount++;
        }

        if (Remainder == 0)
        {
            /* The division came out exactly: the rest of the word is zeroes */
            Fraction <<= (64 - BitCount);
            break;
        }
    }

    Reciprocal.QuadPart = (LONGLONG)Fraction;
    *Shift = ShiftCount;

    return Reciprocal;
}

/*
 * Prepares one processor's own locks and queues. Each queued lock the system
 * has is represented on every processor by a node of its own, which is what
 * lets a waiter spin on a word nobody else is writing, and the processor's
 * scheduling queues start out empty.
 */
CODE_SEG("INIT")
VOID
NTAPI
KiInitSpinLocks(
    _Inout_ PKPRCB Prcb,
    _In_ CCHAR Number)
{
    ULONG Index;

    for (Index = 0; Index < LockQueueMaximumLock; Index++)
    {
        Prcb->LockQueue[Index].Next = NULL;
        Prcb->LockQueue[Index].Lock = NULL;
    }

    /*
     * Processor zero owns the lock words themselves; every other processor's
     * nodes point at the same words. A node whose lock word is this
     * processor's own would be a lock nobody else could ever contend for.
     */
    if (Number == 0)
    {
        KeInitializeSpinLock(&KiDispatcherLock);
        KeInitializeSpinLock(&KiTimerTableLock);
        for (Index = 0; Index < RTL_NUMBER_OF(KiTimerTableLocks); Index++)
            KeInitializeSpinLock(&KiTimerTableLocks[Index]);
        KeInitializeSpinLock(&KiReverseStallIpiLock);
        KeInitializeSpinLock(&MmPfnLock);
        KeInitializeSpinLock(&MmSystemSpaceLock);
        KeInitializeSpinLock(&MmNonPagedPoolLock);
        KeInitializeSpinLock(&NonPagedPoolLock);
        KeInitializeSpinLock(&CcBcbSpinLock);
        KeInitializeSpinLock(&CcMasterSpinLock);
        KeInitializeSpinLock(&CcVacbSpinLock);
        KeInitializeSpinLock(&CcWorkQueueSpinLock);
        KeInitializeSpinLock(&IopCancelSpinLock);
        KeInitializeSpinLock(&IopCompletionLock);
        KeInitializeSpinLock(&IopDatabaseLock);
        KeInitializeSpinLock(&NtfsStructLock);
        KeInitializeSpinLock(&AfdWorkQueueSpinLock);
    }

    Prcb->LockQueue[LockQueueDispatcherLock].Lock = &KiDispatcherLock;
    Prcb->LockQueue[LockQueuePfnLock].Lock = &MmPfnLock;
    Prcb->LockQueue[LockQueueSystemSpaceLock].Lock = &MmSystemSpaceLock;
    Prcb->LockQueue[LockQueueNonPagedPoolLock].Lock = &NonPagedPoolLock;
    Prcb->LockQueue[LockQueueMmNonPagedPoolLock].Lock = &MmNonPagedPoolLock;
    Prcb->LockQueue[LockQueueBcbLock].Lock = &CcBcbSpinLock;
    Prcb->LockQueue[LockQueueMasterLock].Lock = &CcMasterSpinLock;
    Prcb->LockQueue[LockQueueVacbLock].Lock = &CcVacbSpinLock;
    Prcb->LockQueue[LockQueueWorkQueueLock].Lock = &CcWorkQueueSpinLock;
    Prcb->LockQueue[LockQueueIoCancelLock].Lock = &IopCancelSpinLock;
    Prcb->LockQueue[LockQueueIoCompletionLock].Lock = &IopCompletionLock;
    Prcb->LockQueue[LockQueueIoDatabaseLock].Lock = &IopDatabaseLock;
    Prcb->LockQueue[LockQueueIoVpbLock].Lock = &IopVpbSpinLock;
    Prcb->LockQueue[LockQueueNtfsStructLock].Lock = &NtfsStructLock;
    Prcb->LockQueue[LockQueueAfdWorkQueueLock].Lock = &AfdWorkQueueSpinLock;
    Prcb->LockQueue[LockQueueExpansionLock].Lock = &KiReverseStallIpiLock;

    /*
     * The timer table is covered by a group of locks rather than one, so that
     * two processors touching different parts of the table do not contend.
     * Which lock covers which buckets is decided by the table index, in
     * KiAcquireTimerLock.
     */
    Prcb->LockQueue[LockQueueTimerTableLock].Lock = &KiTimerTableLock;
    for (Index = 1; Index < LOCK_QUEUE_TIMER_TABLE_LOCKS; Index++)
    {
        Prcb->LockQueue[LockQueueTimerTableLock + Index].Lock =
            &KiTimerTableLocks[Index - 1];
    }

    /*
     * The queues this processor schedules from: one list per priority, the
     * summary that says which of them hold anything, and the list a thread
     * waits on when it is this processor's turn to run it.
     */
    InitializeListHead(&Prcb->WaitListHead);
    Prcb->DeferredReadyListHead.Next = NULL;
    Prcb->ReadySummary = 0;
    Prcb->QueueIndex = 1;

    for (Index = 0; Index < RTL_NUMBER_OF(Prcb->DispatcherReadyListHead); Index++)
    {
        InitializeListHead(&Prcb->DispatcherReadyListHead[Index]);
    }

    /* The deferred procedure call queues this processor will own */
    for (Index = 0; Index < 2; Index++)
    {
        Prcb->DpcData[Index].DpcList.ListHead.Next = NULL;
        Prcb->DpcData[Index].DpcList.LastEntry =
            &Prcb->DpcData[Index].DpcList.ListHead;
        Prcb->DpcData[Index].DpcLock = 0;
        Prcb->DpcData[Index].DpcQueueDepth = 0;
        Prcb->DpcData[Index].DpcCount = 0;
        Prcb->DpcData[Index].ActiveDpc = NULL;
    }

    Prcb->MaximumDpcQueueDepth = (LONG)KiMaximumDpcQueueDepth;
    Prcb->MinimumDpcRate = KiMinimumDpcRate;
    Prcb->AdjustDpcThreshold = KiAdjustDpcThreshold;

    /*
     * The watchdog limits, in clock ticks. One routine is given the timeout
     * and the whole pass four times as long: a pass made of many short
     * routines is not stuck, but one that never ends is.
     */
    Prcb->DpcTimeLimit = KiDPCTimeout;
    Prcb->DpcWatchdogPeriod = (LONG)(KiDPCTimeout * 4);
    Prcb->DpcTimeCount = 0;
    Prcb->DpcWatchdogCount = 0;
    Prcb->DpcRequestRate = 0;
    Prcb->DpcLastCount = 0;
    Prcb->DpcInterruptRequested = FALSE;
    Prcb->DpcThreadRequested = FALSE;
    Prcb->DpcRoutineActive = FALSE;
    Prcb->DpcThreadActive = FALSE;
    Prcb->ThreadDpcEnable = FALSE;
    Prcb->DpcSetEventRequest = 0;

    KeInitializeEvent(&Prcb->DpcEvent, SynchronizationEvent, FALSE);
}

/*
 * Prepares the data the dispatcher owns in common. Every list here is one that
 * something inserts into before the component that owns it has initialised,
 * which is why they belong to the kernel's own startup and not to theirs.
 *
 * Idempotent on purpose: an architecture whose startup has already done part
 * of this calls it anyway rather than keeping two lists of what is left.
 */
CODE_SEG("INIT")
VOID
NTAPI
KiInitSystem(VOID)
{
    ULONG Index;

    InitializeListHead(&KeBugcheckCallbackListHead);
    InitializeListHead(&KeBugcheckReasonCallbackListHead);
    KeInitializeSpinLock(&BugCheckCallbackLock);

    InitializeListHead(&KiProfileListHead);
    InitializeListHead(&KiProfileSourceListHead);
    KeInitializeSpinLock(&KiProfileLock);

    InitializeListHead(&KiProcessListHead);
    InitializeListHead(&KiProcessInSwapListHead);
    InitializeListHead(&KiProcessOutSwapListHead);
    InitializeListHead(&KiStackInSwapListHead);
    KeInitializeEvent(&KiSwapEvent, SynchronizationEvent, FALSE);

    ExInitializeFastMutex(&KiGenericCallDpcMutex);

    for (Index = 0; Index < TIMER_TABLE_SIZE; Index++)
    {
        InitializeListHead(&KiTimerTableListHead[Index].Entry);
        KiTimerTableListHead[Index].Time.HighPart = 0xFFFFFFFF;
        KiTimerTableListHead[Index].Time.LowPart = 0;
    }

    KeInitializeDpc(&KiTimerExpireDpc, KiTimerExpiration, NULL);
    KeSetTargetProcessorDpc(&KiTimerExpireDpc, 0);

    KeServiceDescriptorTable[0].Base = MainSSDT;
    KeServiceDescriptorTable[0].Count = NULL;
    KeServiceDescriptorTable[0].Limit = KiServiceLimit;
    KeServiceDescriptorTable[0].Number = MainSSPT;
    KeServiceDescriptorTableShadow[0] = KeServiceDescriptorTable[0];
}

/*
 * The kernel's share of the second phase of startup, reached once there is a
 * working executive to build on. What is left by then is the work that needs
 * threads, which is why it could not be done earlier.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
KeInitSystem(VOID)
{
    /* Threaded deferred calls need a thread per processor to run on */
    KiStartDpcThreads();

    return TRUE;
}

/*
 * Runs a callout with at least Size bytes of stack available to it. Where the
 * thread's own stack already has that much left, the callout is simply called:
 * moving to another stack would cost more than it saved.
 */
NTSTATUS
NTAPI
KeExpandKernelStackAndCalloutEx(
    _In_ PEXPAND_STACK_CALLOUT Callout,
    _In_opt_ PVOID Parameter,
    _In_ SIZE_T Size,
    _In_ BOOLEAN Wait,
    _In_opt_ PVOID Context)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR Available;

    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(Context);

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    if ((Size == 0) || (Size > KERNEL_LARGE_STACK_SIZE))
        return STATUS_INVALID_PARAMETER;

    /*
     * How much of this thread's stack is still below it. The current frame
     * address stands for the stack pointer closely enough: what matters is
     * whether the callout's own needs fit under it.
     */
    Available = (ULONG_PTR)&Available - Thread->StackLimit;
    if (Available >= Size)
    {
        Callout(Parameter);
        return STATUS_SUCCESS;
    }

    return KiCalloutOnNewStack(Callout, Parameter, Size);
}

NTSTATUS
NTAPI
KeExpandKernelStackAndCallout(
    _In_ PEXPAND_STACK_CALLOUT Callout,
    _In_opt_ PVOID Parameter,
    _In_ SIZE_T Size)
{
    return KeExpandKernelStackAndCalloutEx(Callout,
                                           Parameter,
                                           Size,
                                           TRUE,
                                           NULL);
}

/* EOF */
