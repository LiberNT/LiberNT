/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Work a processor owes itself before it returns to a thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#include "keprivate.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* Whether a DPC marked threaded is actually run in a thread. Turning this off
   makes every DPC an ordinary one, which is what a debugging run wants. */
ULONG KeThreadDpcEnable = TRUE;

/* How deep a processor's queue may get before the dispatch interrupt is asked
   for even though the DPC that was queued did not ask for it itself. */
ULONG KiMaximumDpcQueueDepth = 4;

/* The rate, in DPCs per clock tick, below which a medium-importance DPC is
   worth an immediate interrupt and above which it can wait for the next one. */
ULONG KiMinimumDpcRate = 3;

/* Queue depth at which the processor's threshold is tightened. */
ULONG KiAdjustDpcThreshold = 20;

/* The rate the adjustment aims for. */
ULONG KiIdealDpcRate = 20;

/* How long a single DPC may run before the watchdog treats it as stuck. */
ULONG KiDPCTimeout = 110;

/* How long an interrupt service routine may run, in microseconds. */
ULONG KiTimeLimitIsrMicroseconds;

/* Serialises the one-at-a-time broadcast in KeGenericCallDpc. */
FAST_MUTEX KiGenericCallDpcMutex;

/* PRIVATE DATA ***************************************************************/

/* The thread that runs threaded DPCs for each processor. Windows 11 has no
   KPRCB member for it on this layout, so it is kept beside the kernel. */
static PKTHREAD KiDpcThread[MAXIMUM_PROCESSORS];

/* PRIVATE FUNCTIONS **********************************************************/

FORCEINLINE
PKSPIN_LOCK
KiDpcQueueLock(
    _In_ PKDPC_DATA DpcData)
{
    return (PKSPIN_LOCK)&DpcData->DpcLock;
}

/*
 * Which queue a DPC belongs in. A threaded DPC only goes to the threaded queue
 * where both the system and that processor allow it; otherwise it is run as an
 * ordinary DPC, which is slower for the system but never wrong for the caller.
 */
FORCEINLINE
PKDPC_DATA
KiSelectDpcQueue(
    _In_ PKPRCB Prcb,
    _In_ PKDPC Dpc)
{
    if ((Dpc->Type == ThreadedDpcObject) &&
        KeThreadDpcEnable &&
        Prcb->ThreadDpcEnable)
    {
        return &Prcb->DpcData[DPC_THREADED];
    }

    return &Prcb->DpcData[DPC_NORMAL];
}

/*
 * Whether queueing this DPC should cost an interrupt right away. A high
 * importance DPC always does; a medium one does while the processor is not
 * already busy with DPCs; a low one only once the queue has grown past the
 * depth the processor is willing to carry.
 */
static
BOOLEAN
KiDpcNeedsInterrupt(
    _In_ PKPRCB Prcb,
    _In_ PKDPC_DATA DpcData,
    _In_ PKDPC Dpc)
{
    if (Prcb->DpcRoutineActive)
        return FALSE;

    switch (Dpc->Importance)
    {
        case HighImportance:
            return TRUE;

        case MediumHighImportance:
            return TRUE;

        case MediumImportance:
            if (Prcb->DpcRequestRate < Prcb->MinimumDpcRate)
                return TRUE;
            break;

        default:
            break;
    }

    return DpcData->DpcQueueDepth >= Prcb->MaximumDpcQueueDepth;
}

/*
 * Takes one DPC off a queue. Answers NULL when the queue turned out to be
 * empty, which happens whenever the DPC was removed between the depth check
 * and the lock.
 */
static
PKDPC
KiPopDpc(
    _Inout_ PKDPC_DATA DpcData,
    _Out_ PKDEFERRED_ROUTINE *Routine,
    _Out_ PVOID *Context,
    _Out_ PVOID *SystemArgument1,
    _Out_ PVOID *SystemArgument2)
{
    PSINGLE_LIST_ENTRY Entry;
    PKDPC Dpc;
    KIRQL OldIrql;

    /* The queue is also filled from an interrupt service routine of this
     * processor, so nothing of that level may interrupt the queue lock */
    OldIrql = KfRaiseIrql(HIGH_LEVEL);
    KeAcquireSpinLockAtDpcLevel(KiDpcQueueLock(DpcData));

    Entry = DpcData->DpcList.ListHead.Next;
    if (Entry == NULL)
    {
        KeReleaseSpinLockFromDpcLevel(KiDpcQueueLock(DpcData));
        KeLowerIrql(OldIrql);
        return NULL;
    }

    DpcData->DpcList.ListHead.Next = Entry->Next;
    if (DpcData->DpcList.ListHead.Next == NULL)
        DpcData->DpcList.LastEntry = &DpcData->DpcList.ListHead;

    Dpc = CONTAINING_RECORD(Entry, KDPC, DpcListEntry);

    *Routine = Dpc->DeferredRoutine;
    *Context = Dpc->DeferredContext;
    *SystemArgument1 = Dpc->SystemArgument1;
    *SystemArgument2 = Dpc->SystemArgument2;

    DpcData->ActiveDpc = Dpc;
    DpcData->DpcQueueDepth--;
    DpcData->DpcCount++;

    /*
     * Clearing DpcData is what lets the DPC be queued again. It is published
     * with release ordering so that a processor which sees the DPC as free
     * also sees it unlinked.
     */
    WritePointerRelease(&Dpc->DpcData, NULL);

    KeReleaseSpinLockFromDpcLevel(KiDpcQueueLock(DpcData));
    KeLowerIrql(OldIrql);
    return Dpc;
}

/*
 * The thread that runs one processor's threaded DPCs. It sits at the top of
 * the dynamic range so that a threaded DPC still preempts ordinary work, but
 * below real time so a real-time thread is never delayed by one.
 */
static
VOID
NTAPI
KiDpcThreadRoutine(
    _In_ PVOID Context)
{
    PKPRCB Prcb = (PKPRCB)Context;
    PKDPC_DATA DpcData = &Prcb->DpcData[DPC_THREADED];
    PKDEFERRED_ROUTINE Routine;
    PVOID DeferredContext, SystemArgument1, SystemArgument2;
    PKDPC Dpc;
    KIRQL OldIrql;

    for (;;)
    {
        KeWaitForSingleObject(&Prcb->DpcEvent,
                              WrDispatchInt,
                              KernelMode,
                              FALSE,
                              NULL);

        Prcb->DpcThreadActive = TRUE;

        for (;;)
        {
            OldIrql = KeRaiseIrqlToDpcLevel();
            Dpc = KiPopDpc(DpcData,
                           &Routine,
                           &DeferredContext,
                           &SystemArgument1,
                           &SystemArgument2);
            KeLowerIrql(OldIrql);

            if (Dpc == NULL)
                break;

            /* A threaded DPC runs where it may block, unlike an ordinary one */
            Routine(Dpc, DeferredContext, SystemArgument1, SystemArgument2);
        }

        Prcb->DpcThreadActive = FALSE;
        Prcb->DpcThreadRequested = FALSE;
    }
}

/*
 * Signals that this generic call has finished on one processor. Nothing more
 * than a count: the caller watches it fall to zero.
 */
static
VOID
NTAPI
KiFlushDpcBarrierRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeSignalCallDpcDone(SystemArgument1);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The shared part of describing a DPC. Importance starts at medium and the
 * processor is left unchosen, which means "whichever one queues it".
 */
VOID
NTAPI
KiInitializeDpc(
    _Out_ PKDPC Dpc,
    _In_ PKDEFERRED_ROUTINE DeferredRoutine,
    _In_opt_ PVOID DeferredContext,
    _In_ UCHAR Type)
{
    Dpc->Type = Type;
    Dpc->Importance = MediumImportance;
    Dpc->Number = 0;
    Dpc->DeferredRoutine = DeferredRoutine;
    Dpc->DeferredContext = DeferredContext;
    Dpc->DpcData = NULL;
    Dpc->ProcessorHistory = 0;
    Dpc->SystemArgument1 = NULL;
    Dpc->SystemArgument2 = NULL;
    Dpc->DpcListEntry.Next = NULL;
}

VOID
NTAPI
KeInitializeDpc(
    _Out_ PKDPC Dpc,
    _In_ PKDEFERRED_ROUTINE DeferredRoutine,
    _In_opt_ PVOID DeferredContext)
{
    KiInitializeDpc(Dpc, DeferredRoutine, DeferredContext, DpcObject);
}

/*
 * A DPC that may run in a thread instead of at DISPATCH_LEVEL. The routine is
 * then allowed to block, and in exchange it has no claim on being run before
 * the processor goes back to ordinary threads.
 */
VOID
NTAPI
KeInitializeThreadedDpc(
    _Out_ PKDPC Dpc,
    _In_ PKDEFERRED_ROUTINE DeferredRoutine,
    _In_opt_ PVOID DeferredContext)
{
    KiInitializeDpc(Dpc, DeferredRoutine, DeferredContext, ThreadedDpcObject);
}

/*
 * Says how urgent a DPC is. This changes where it is placed in the queue and
 * whether queueing it is worth an interrupt, never whether it runs.
 */
VOID
NTAPI
KeSetImportanceDpc(
    _Inout_ PKDPC Dpc,
    _In_ KDPC_IMPORTANCE Importance)
{
    ASSERT_DPC(Dpc);
    ASSERT((Importance == LowImportance) ||
           (Importance == MediumImportance) ||
           (Importance == MediumHighImportance) ||
           (Importance == HighImportance));

    Dpc->Importance = (UCHAR)Importance;
}

/*
 * Pins a DPC to one processor. The number is stored above the processor range
 * so that a DPC which was never pinned is told apart from one pinned to
 * processor zero.
 */
VOID
NTAPI
KeSetTargetProcessorDpc(
    _Inout_ PKDPC Dpc,
    _In_ CCHAR Number)
{
    ASSERT_DPC(Dpc);
    ASSERT(Number >= 0);
    ASSERT(Number < MAXIMUM_PROCESSORS);

    Dpc->Number = (USHORT)(MAXIMUM_PROCESSORS + Number);
}

/*
 * Queues a DPC. Answers FALSE when it was already queued, in which case the
 * arguments it was queued with are the ones that stand: a DPC carries one set
 * of arguments, not a queue of them.
 *
 * Callable from an interrupt service routine, so IRQL is raised out of the way
 * of anything that could preempt the queue manipulation.
 */
BOOLEAN
NTAPI
KeInsertQueueDpc(
    _Inout_ PKDPC Dpc,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PKPRCB Prcb, CurrentPrcb;
    PKDPC_DATA DpcData;
    ULONG Processor;
    KIRQL OldIrql;
    BOOLEAN Inserted = FALSE;
    BOOLEAN NeedInterrupt = FALSE;
    BOOLEAN NeedThread = FALSE;

    ASSERT_DPC(Dpc);

    OldIrql = KfRaiseIrql(HIGH_LEVEL);
    CurrentPrcb = KeGetCurrentPrcb();

    if (Dpc->Number >= MAXIMUM_PROCESSORS)
    {
        Processor = (ULONG)(Dpc->Number - MAXIMUM_PROCESSORS);
        Prcb = KiProcessorBlock[Processor];
    }
    else
    {
        Processor = CurrentPrcb->Number;
        Prcb = CurrentPrcb;
    }

    DpcData = KiSelectDpcQueue(Prcb, Dpc);

    KeAcquireSpinLockAtDpcLevel(KiDpcQueueLock(DpcData));

    if (Dpc->DpcData == NULL)
    {
        Dpc->SystemArgument1 = SystemArgument1;
        Dpc->SystemArgument2 = SystemArgument2;
        Dpc->ProcessorHistory |= AFFINITY_MASK(Processor);
        Dpc->DpcData = DpcData;
        DpcData->DpcQueueDepth++;

        /*
         * A high importance DPC is put at the front: everything already queued
         * was content to wait, and this one was not.
         */
        Dpc->DpcListEntry.Next = NULL;
        if (Dpc->Importance == HighImportance)
        {
            Dpc->DpcListEntry.Next = DpcData->DpcList.ListHead.Next;
            DpcData->DpcList.ListHead.Next = &Dpc->DpcListEntry;
            if (Dpc->DpcListEntry.Next == NULL)
                DpcData->DpcList.LastEntry = &Dpc->DpcListEntry;
        }
        else
        {
            DpcData->DpcList.LastEntry->Next = &Dpc->DpcListEntry;
            DpcData->DpcList.LastEntry = &Dpc->DpcListEntry;
        }

        if (DpcData == &Prcb->DpcData[DPC_THREADED])
        {
            if (!Prcb->DpcThreadActive && !Prcb->DpcThreadRequested)
            {
                Prcb->DpcThreadRequested = TRUE;
                NeedThread = TRUE;
            }
        }
        else if (!Prcb->DpcInterruptRequested &&
                 KiDpcNeedsInterrupt(Prcb, DpcData, Dpc))
        {
            Prcb->DpcInterruptRequested = TRUE;
            NeedInterrupt = TRUE;
        }

        Inserted = TRUE;
    }

    KeReleaseSpinLockFromDpcLevel(KiDpcQueueLock(DpcData));

    if (NeedThread)
    {
        /* The event is signalled below DISPATCH_LEVEL by the thread path */
        Prcb->DpcSetEventRequest = TRUE;
        NeedInterrupt = TRUE;
    }

    if (NeedInterrupt)
    {
        if (Prcb == CurrentPrcb)
        {
            HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
        }
        else
        {
            if (SmpDbgEnabled)
                SmpDbgRemoteDpc(Processor, CurrentPrcb->Number);
            KiIpiSend(AFFINITY_MASK(Processor), IPI_DPC);
        }
    }

    KeLowerIrql(OldIrql);
    return Inserted;
}

/*
 * Takes a queued DPC back. Answers FALSE when it was not queued, which also
 * covers the case where it is running right now.
 */
BOOLEAN
NTAPI
KeRemoveQueueDpc(
    _Inout_ PKDPC Dpc)
{
    return KeRemoveQueueDpcEx(Dpc, FALSE);
}

/*
 * As above, and with WaitIfActive the caller also waits out a DPC that is
 * already running, so that on return the routine is known not to be executing
 * anywhere. That wait cannot be made at DISPATCH_LEVEL or above, because the
 * DPC it waits for may be this very processor's.
 */
BOOLEAN
NTAPI
KeRemoveQueueDpcEx(
    _Inout_ PKDPC Dpc,
    _In_ BOOLEAN WaitIfActive)
{
    PKDPC_DATA DpcData;
    PSINGLE_LIST_ENTRY Entry, Previous;
    KIRQL OldIrql;
    BOOLEAN Enqueued = FALSE;

    ASSERT_DPC(Dpc);

    OldIrql = KfRaiseIrql(HIGH_LEVEL);

    DpcData = Dpc->DpcData;
    if (DpcData != NULL)
    {
        KeAcquireSpinLockAtDpcLevel(KiDpcQueueLock(DpcData));

        if (Dpc->DpcData == DpcData)
        {
            Previous = &DpcData->DpcList.ListHead;
            Entry = Previous->Next;
            while (Entry != NULL)
            {
                if (Entry == &Dpc->DpcListEntry)
                {
                    Previous->Next = Entry->Next;
                    if (Entry->Next == NULL)
                        DpcData->DpcList.LastEntry = Previous;

                    DpcData->DpcQueueDepth--;
                    Dpc->DpcData = NULL;
                    Dpc->DpcListEntry.Next = NULL;
                    Enqueued = TRUE;
                    break;
                }

                Previous = Entry;
                Entry = Entry->Next;
            }
        }

        KeReleaseSpinLockFromDpcLevel(KiDpcQueueLock(DpcData));
    }

    KeLowerIrql(OldIrql);

    if (WaitIfActive && !Enqueued)
    {
        /*
         * It was not queued; it may still be running. Draining every
         * processor's queue is what makes the answer good.
         */
        ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);
        KeFlushQueuedDpcs();
    }

    return Enqueued;
}

/*
 * Waits until every DPC queued before this call has run, on every processor.
 * A DPC queued now on each processor can only run after the ones already
 * there, so waiting for ours is waiting for theirs.
 */
VOID
NTAPI
KeFlushQueuedDpcs(VOID)
{
    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    KeGenericCallDpc(KiFlushDpcBarrierRoutine, NULL);
}

/*
 * Says whether the calling code is inside a DPC routine on this processor.
 * A driver uses it to tell a deferred path from a direct call.
 */
BOOLEAN
NTAPI
KeIsExecutingDpc(VOID)
{
    return KeGetCurrentPrcb()->DpcRoutineActive;
}

/*
 * Says whether this processor has something better to do than carry on with
 * the current routine: a DPC waiting, or a thread of higher priority ready.
 */
LOGICAL
NTAPI
KeShouldYieldProcessor(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD Thread;

    if (Prcb->DpcData[DPC_NORMAL].DpcQueueDepth != 0)
        return TRUE;

    if (Prcb->NextThread != NULL)
        return TRUE;

    if (Prcb->QuantumEnd)
        return TRUE;

    Thread = Prcb->CurrentThread;
    if (Thread == NULL)
        return FALSE;

    return (Prcb->ReadySummary >> Thread->Priority) != 0;
}

/*
 * Reports what the DPC watchdog has seen on this processor. The counts are in
 * the processor's own units, and are meaningful only to the caller that set
 * the limits.
 */
NTSTATUS
NTAPI
KeQueryDpcWatchdogInformation(
    _Out_ PKDPC_WATCHDOG_INFORMATION WatchdogInformation)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    WatchdogInformation->DpcTimeLimit = Prcb->DpcTimeLimit;
    WatchdogInformation->DpcTimeCount = Prcb->DpcTimeCount;
    WatchdogInformation->DpcWatchdogLimit = Prcb->DpcWatchdogPeriod;
    WatchdogInformation->DpcWatchdogCount = Prcb->DpcWatchdogCount;
    WatchdogInformation->Reserved = 0;

    return STATUS_SUCCESS;
}

/*
 * Runs the DPCs this processor has queued, in order, until none are left.
 *
 * Entered at DISPATCH_LEVEL on the processor's DPC stack. A DPC routine is
 * expected to come back at the IRQL it was given; one that does not has
 * corrupted the processor's state, and saying so here is worth more than
 * discovering it later somewhere unrelated.
 */
VOID
FASTCALL
KiRetireDpcList(
    _Inout_ PKPRCB Prcb)
{
    PKDPC_DATA DpcData = &Prcb->DpcData[DPC_NORMAL];
    PKDEFERRED_ROUTINE Routine;
    PVOID Context, SystemArgument1, SystemArgument2;
    PKDPC Dpc;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    Prcb->DpcRoutineActive = TRUE;
    Prcb->DpcWatchdogCount = 0;

    /*
     * The caller arrives from a trap gate, which left interrupts off, and
     * switches the stack under us. A deferred routine runs with interrupts
     * enabled, so that one taking its time still lets the clock in; the
     * caller gets them back off for the stack it has to put back.
     */
    _enable();

    for (;;)
    {
        if (DpcData->DpcQueueDepth == 0)
            break;

        Dpc = KiPopDpc(DpcData,
                       &Routine,
                       &Context,
                       &SystemArgument1,
                       &SystemArgument2);
        if (Dpc == NULL)
            break;

        /*
         * The watchdog measures one routine at a time as well as the pass as
         * a whole, so the per-routine count starts again here while the
         * pass count carries on.
         */
        Prcb->DpcTimeCount = 0;

        Routine(Dpc, Context, SystemArgument1, SystemArgument2);

        if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        {
            KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                         (ULONG_PTR)Routine,
                         KeGetCurrentIrql(),
                         DISPATCH_LEVEL,
                         (ULONG_PTR)Dpc);
        }
    }

    _disable();

    DpcData->ActiveDpc = NULL;
    Prcb->DpcRoutineActive = FALSE;
    Prcb->DpcInterruptRequested = FALSE;
    Prcb->DpcTimeCount = 0;
    Prcb->DpcWatchdogCount = 0;

    /* The rate is what the next queue decides its urgency against */
    Prcb->DpcRequestRate = ((DpcData->DpcCount - Prcb->DpcLastCount) +
                            Prcb->DpcRequestRate) / 2;
    Prcb->DpcLastCount = DpcData->DpcCount;

    /*
     * Waking the threaded DPC thread needs the dispatcher, which cannot be
     * entered from inside the DPC pass. It is done on the way out instead.
     */
    if (Prcb->DpcSetEventRequest)
    {
        Prcb->DpcSetEventRequest = FALSE;
        KeSetEvent(&Prcb->DpcEvent, 0, FALSE);
    }
}

/*
 * Runs a routine once on every active processor and waits for all of them.
 *
 * Only one of these may be in flight at a time: each processor's broadcast DPC
 * is a single object in its own PRCB, and a second caller would overwrite the
 * arguments of the first.
 */
VOID
NTAPI
KeGenericCallDpc(
    _In_ PKDEFERRED_ROUTINE Routine,
    _In_opt_ PVOID Context)
{
    DEFERRED_REVERSE_BARRIER ReverseBarrier;
    KAFFINITY Targets;
    volatile LONG Count;
    ULONG Processor, Total;
    PKPRCB Prcb, CurrentPrcb;
    PKDPC Dpc;
    KIRQL OldIrql;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    ExAcquireFastMutex(&KiGenericCallDpcMutex);

    OldIrql = KeRaiseIrqlToDpcLevel();
    CurrentPrcb = KeGetCurrentPrcb();
    Targets = KeActiveProcessors;
    KeLowerIrql(OldIrql);

    Total = 0;
    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        if (Targets & AFFINITY_MASK(Processor))
            Total++;
    }

    Count = (LONG)Total;
    ReverseBarrier.Barrier = Total;
    ReverseBarrier.TotalProcessors = Total;

    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        if ((Targets & AFFINITY_MASK(Processor)) == 0)
            continue;

        if (Processor == CurrentPrcb->Number)
            continue;

        Prcb = KiProcessorBlock[Processor];
        Dpc = &Prcb->CallDpc;

        KeInitializeDpc(Dpc, Routine, Context);
        KeSetTargetProcessorDpc(Dpc, (CCHAR)Processor);
        KeSetImportanceDpc(Dpc, HighImportance);
        KeInsertQueueDpc(Dpc, (PVOID)&Count, &ReverseBarrier);
    }

    /* This processor runs its share itself rather than interrupting itself */
    if (Targets & AFFINITY_MASK(CurrentPrcb->Number))
    {
        Dpc = &CurrentPrcb->CallDpc;
        KeInitializeDpc(Dpc, Routine, Context);
        KeSetTargetProcessorDpc(Dpc, (CCHAR)CurrentPrcb->Number);

        OldIrql = KeRaiseIrqlToDpcLevel();
        Routine(Dpc, Context, (PVOID)&Count, &ReverseBarrier);
        KeLowerIrql(OldIrql);
    }

    /* Spun below DISPATCH_LEVEL so the other processors can make progress */
    while (Count != 0)
        YieldProcessor();

    ExReleaseFastMutex(&KiGenericCallDpcMutex);
}

/*
 * What a generic-call routine calls last. Nothing but a count: the caller is
 * watching it fall to zero.
 */
VOID
NTAPI
KeSignalCallDpcDone(
    _In_ PVOID SystemArgument1)
{
    InterlockedDecrement((PLONG)SystemArgument1);
}

/*
 * Holds every processor in a generic call until all of them have arrived, then
 * lets them all go. Answers TRUE on exactly one of them, which is how a
 * broadcast names the processor that does the single-threaded part of the work.
 */
BOOLEAN
NTAPI
KeSignalCallDpcSynchronize(
    _In_ PVOID SystemArgument2)
{
    PDEFERRED_REVERSE_BARRIER Barrier =
        (PDEFERRED_REVERSE_BARRIER)SystemArgument2;
    ULONG Total = Barrier->TotalProcessors;

    if (Total <= 1)
        return TRUE;

    if (InterlockedDecrement((PLONG)&Barrier->Barrier) == 0)
    {
        /* Last one in releases the rest and takes the single-processor work */
        InterlockedExchange((PLONG)&Barrier->Barrier, (LONG)Total);
        return TRUE;
    }

    while ((ULONG)Barrier->Barrier != Total)
        YieldProcessor();

    return FALSE;
}

/*
 * Creates the per-processor threads that run threaded DPCs. Until this runs,
 * a threaded DPC is queued as an ordinary one, so failing here costs latency
 * rather than correctness.
 */
VOID
NTAPI
KiStartDpcThreads(VOID)
{
    PKPRCB Prcb;
    HANDLE ThreadHandle;
    PETHREAD Thread;
    NTSTATUS Status;
    ULONG Processor;
    KPRIORITY Priority = HIGH_PRIORITY;

    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        if ((KeActiveProcessors & AFFINITY_MASK(Processor)) == 0)
            continue;

        Prcb = KiProcessorBlock[Processor];
        if (Prcb == NULL)
            continue;

        if (KiDpcThread[Processor] != NULL)
            continue;

        Status = PsCreateSystemThread(&ThreadHandle,
                                      THREAD_ALL_ACCESS,
                                      NULL,
                                      NULL,
                                      NULL,
                                      KiDpcThreadRoutine,
                                      Prcb);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("No threaded DPC thread for processor %lu: %lx\n",
                    Processor, Status);
            continue;
        }

        Status = ObReferenceObjectByHandle(ThreadHandle,
                                           THREAD_ALL_ACCESS,
                                           PsThreadType,
                                           KernelMode,
                                           (PVOID *)&Thread,
                                           NULL);
        ObCloseHandle(ThreadHandle, KernelMode);

        if (!NT_SUCCESS(Status))
            continue;

        KeSetPriorityThread(&Thread->Tcb, Priority);
        KeSetAffinityThread(&Thread->Tcb, AFFINITY_MASK(Processor));

        KiDpcThread[Processor] = &Thread->Tcb;
        Prcb->ThreadDpcEnable = TRUE;
    }
}

/* EOF */
