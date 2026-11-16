/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Experimental earliest eligible virtual deadline first scheduling of the dynamic priorities
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "keprivate.h"
#define NDEBUG
#include <debug.h>

#define KI_EEVDF_TOP_PRIORITY (LOW_REALTIME_PRIORITY - 1)
#define KI_EEVDF_MINIMUM_SLICE_100NS 30000
#define KI_EEVDF_WAIT_BOUND (1ULL << 40)
#define KI_EEVDF_KEY_BOUND (1LL << 50)
#define KI_EEVDF_VERIFY_LIMIT 32

/* VIRTUAL TIME ARITHMETIC ****************************************************/

static
BOOLEAN
KiEevdfIsFairPriority(
    _In_ LONG Priority)
{
    return (Priority >= 1) && (Priority <= KI_EEVDF_TOP_PRIORITY);
}

/* A priority step doubles the weight, so a cycle costs 2^(15 - priority) units of virtual time */
static
ULONG
KiEevdfShift(
    _In_ UCHAR Priority)
{
    ASSERT(KiEevdfIsFairPriority(Priority));
    return (ULONG)(KI_EEVDF_TOP_PRIORITY - Priority);
}

static
LONG64
KiEevdfWeight(
    _In_ UCHAR Priority)
{
    ASSERT(KiEevdfIsFairPriority(Priority));
    return 1LL << (Priority - 1);
}

static
LONG64
KiEevdfWeightedKey(
    _In_ PKI_EEVDF_QUEUE Queue,
    _In_ PKI_EEVDF_ENTITY Entity)
{
    LONG64 Key = (LONG64)(Entity->Vruntime - Queue->BaseVruntime);

    ASSERT((Key > -KI_EEVDF_KEY_BOUND) && (Key < KI_EEVDF_KEY_BOUND));
    return Key * KiEevdfWeight(Entity->Priority);
}

/* The virtual time of the listed entities and of up to three unlisted ones; the base moves to it so the keys stay small */
static
ULONG64
KiEevdfSettle(
    _Inout_ PKI_EEVDF_QUEUE Queue,
    _In_opt_ PKI_EEVDF_ENTITY First,
    _In_opt_ PKI_EEVDF_ENTITY Second,
    _In_opt_ PKI_EEVDF_ENTITY Third,
    _Out_opt_ PULONG64 TotalLoad)
{
    LONG64 Sum = Queue->WeightedKeySum;
    LONG64 Load = (LONG64)Queue->Load;
    LONG64 Offset;

    if (Second == First)
        Second = NULL;
    if ((Third == First) || (Third == Second))
        Third = NULL;

    if ((First != NULL) && !First->Listed)
    {
        Sum += KiEevdfWeightedKey(Queue, First);
        Load += KiEevdfWeight(First->Priority);
    }
    if ((Second != NULL) && !Second->Listed)
    {
        Sum += KiEevdfWeightedKey(Queue, Second);
        Load += KiEevdfWeight(Second->Priority);
    }
    if ((Third != NULL) && !Third->Listed)
    {
        Sum += KiEevdfWeightedKey(Queue, Third);
        Load += KiEevdfWeight(Third->Priority);
    }

    if (TotalLoad != NULL)
        *TotalLoad = (ULONG64)Load;

    if (Load == 0)
        return Queue->BaseVruntime;

    Offset = Sum / Load;
    if ((Sum % Load) < 0)
        Offset--;

    Queue->BaseVruntime += (ULONG64)Offset;
    Queue->WeightedKeySum -= Offset * (LONG64)Queue->Load;
    return Queue->BaseVruntime;
}

static
BOOLEAN
KiEevdfIsEligible(
    _In_ PKI_EEVDF_ENTITY Entity,
    _In_ ULONG64 VirtualTime)
{
    return (LONG64)(Entity->Vruntime - VirtualTime) <= 0;
}

static
VOID
KiEevdfInsert(
    _Inout_ PKI_EEVDF_QUEUE Queue,
    _Inout_ PKI_EEVDF_ENTITY Entity)
{
    PLIST_ENTRY Link;
    PKI_EEVDF_ENTITY Other;

    ASSERT(!Entity->Listed);

    for (Link = Queue->DeadlineList.Blink; Link != &Queue->DeadlineList; Link = Link->Blink)
    {
        Other = CONTAINING_RECORD(Link, KI_EEVDF_ENTITY, DeadlineLink);
        if ((LONG64)(Other->Deadline - Entity->Deadline) <= 0)
            break;
    }

    /* TODO: the deadline order is a list walked on insertion; an augmented tree would make it logarithmic */
    InsertHeadList(Link, &Entity->DeadlineLink);
    Entity->Listed = TRUE;

    Queue->WeightedKeySum += KiEevdfWeightedKey(Queue, Entity);
    Queue->Load += (ULONG64)KiEevdfWeight(Entity->Priority);
    Queue->Count++;
}

static
VOID
KiEevdfRemove(
    _Inout_ PKI_EEVDF_QUEUE Queue,
    _Inout_ PKI_EEVDF_ENTITY Entity)
{
    ASSERT(Entity->Listed);
    ASSERT(Queue->Count != 0);
    ASSERT(Queue->Load >= (ULONG64)KiEevdfWeight(Entity->Priority));

    RemoveEntryList(&Entity->DeadlineLink);
    Entity->Listed = FALSE;

    Queue->WeightedKeySum -= KiEevdfWeightedKey(Queue, Entity);
    Queue->Load -= (ULONG64)KiEevdfWeight(Entity->Priority);
    Queue->Count--;

    ASSERT((Queue->Count != 0) || ((Queue->Load == 0) && (Queue->WeightedKeySum == 0)));
}

static
VOID
KiEevdfAdvance(
    _Inout_ PKI_EEVDF_ENTITY Entity,
    _In_ ULONG64 Cycles,
    _In_ ULONG64 Slice)
{
    ULONG Shift = KiEevdfShift(Entity->Priority);

    ASSERT(!Entity->Listed);

    Entity->Vruntime += Cycles << Shift;
    if ((LONG64)(Entity->Vruntime - Entity->Deadline) >= 0)
    {
        Entity->Deadline = Entity->Vruntime + (Slice << Shift);
        Entity->SliceEnded = TRUE;
    }
}

/* What it is owed is scaled so that joining does not dilute it; what it owes is paid back by the time away, at the rate waiting here would have paid it */
static
VOID
KiEevdfPlace(
    _Inout_ PKI_EEVDF_QUEUE Queue,
    _In_opt_ PKI_EEVDF_ENTITY Running,
    _Inout_ PKI_EEVDF_ENTITY Entity,
    _In_ UCHAR Priority,
    _In_ ULONG64 Slice,
    _In_ ULONG64 Away)
{
    ULONG64 VirtualTime, Load;
    LONG64 Lag = Entity->Lag;
    LONG64 Weight = KiEevdfWeight(Priority);
    LONG64 Repaid;
    ULONG Shift = KiEevdfShift(Priority);

    ASSERT(!Entity->Listed);
    ASSERT(Running != Entity);

    VirtualTime = KiEevdfSettle(Queue, Running, NULL, NULL, &Load);
    if (Load == 0)
    {
        Lag = 0;
    }
    else
    {
        if (Lag < 0)
        {
            Repaid = (LONG64)((min(Away, KI_EEVDF_WAIT_BOUND) * (ULONG64)Weight) / (Load + (ULONG64)Weight));
            Lag = (Repaid >= -Lag) ? 0 : (Lag + Repaid);
        }

        Lag = (Lag * ((LONG64)Load + Weight)) / (LONG64)Load;
    }

    Entity->Priority = Priority;
    Entity->Vruntime = VirtualTime - (ULONG64)(Lag * (1LL << Shift));
    Entity->Deadline = Entity->Vruntime + (Slice << Shift);
    Entity->Lag = 0;
    Entity->SliceEnded = FALSE;
}

static
VOID
KiEevdfTakeLag(
    _Inout_ PKI_EEVDF_QUEUE Queue,
    _In_opt_ PKI_EEVDF_ENTITY Running,
    _Inout_ PKI_EEVDF_ENTITY Entity,
    _In_ ULONG64 Limit)
{
    ULONG64 VirtualTime;
    LONG64 Lag;

    ASSERT(!Entity->Listed);

    VirtualTime = KiEevdfSettle(Queue, Running, Entity, NULL, NULL);
    Lag = (LONG64)(VirtualTime - Entity->Vruntime) / (1LL << KiEevdfShift(Entity->Priority));

    if (Lag > (LONG64)Limit)
        Lag = (LONG64)Limit;
    else if (Lag < -(LONG64)Limit)
        Lag = -(LONG64)Limit;

    Entity->Lag = Lag;
}

/* A new weight keeps the cycles owed and the cycles left of the request, so both virtual distances scale by the old weight over the new */
static
VOID
KiEevdfReweight(
    _Inout_ PKI_EEVDF_QUEUE Queue,
    _In_opt_ PKI_EEVDF_ENTITY Running,
    _Inout_ PKI_EEVDF_ENTITY Entity,
    _In_ UCHAR Priority)
{
    BOOLEAN Listed = Entity->Listed;
    ULONG64 VirtualTime;
    LONG64 Lag, Request, Scale;

    if (Entity->Priority == Priority)
        return;

    if (Listed)
        KiEevdfRemove(Queue, Entity);

    VirtualTime = KiEevdfSettle(Queue, Running, Entity, NULL, NULL);
    Lag = (LONG64)(VirtualTime - Entity->Vruntime);
    Request = (LONG64)(Entity->Deadline - Entity->Vruntime);

    if (Priority > Entity->Priority)
    {
        Scale = 1LL << (Priority - Entity->Priority);
        Lag /= Scale;
        Request /= Scale;
    }
    else
    {
        Scale = 1LL << (Entity->Priority - Priority);
        Lag *= Scale;
        Request *= Scale;
    }

    Entity->Priority = Priority;
    Entity->Vruntime = VirtualTime - (ULONG64)Lag;
    Entity->Deadline = Entity->Vruntime + (ULONG64)Request;

    if (Listed)
        KiEevdfInsert(Queue, Entity);
}

static
PKI_EEVDF_ENTITY
KiEevdfPick(
    _Inout_ PKI_EEVDF_QUEUE Queue,
    _In_opt_ PKI_EEVDF_ENTITY Running,
    _In_opt_ PKI_EEVDF_ENTITY Incumbent)
{
    PKI_EEVDF_ENTITY Entity, Best = NULL;
    PLIST_ENTRY Link;
    ULONG64 VirtualTime;

    ASSERT((Incumbent == NULL) || !Incumbent->Listed);

    VirtualTime = KiEevdfSettle(Queue, Running, Incumbent, NULL, NULL);

    for (Link = Queue->DeadlineList.Flink; Link != &Queue->DeadlineList; Link = Link->Flink)
    {
        Entity = CONTAINING_RECORD(Link, KI_EEVDF_ENTITY, DeadlineLink);
        if (KiEevdfIsEligible(Entity, VirtualTime))
        {
            Best = Entity;
            break;
        }
    }

    if (Incumbent != NULL)
    {
        if ((Best == NULL) ||
            (KiEevdfIsEligible(Incumbent, VirtualTime) &&
             ((LONG64)(Incumbent->Deadline - Best->Deadline) <= 0)))
        {
            return Incumbent;
        }

        return Best;
    }

    /* The running entity is averaged in but is not a candidate, so every listed one can be ahead of the virtual time */
    if ((Best == NULL) && !IsListEmpty(&Queue->DeadlineList))
        Best = CONTAINING_RECORD(Queue->DeadlineList.Flink, KI_EEVDF_ENTITY, DeadlineLink);

    return Best;
}

static
BOOLEAN
KiEevdfBeats(
    _Inout_ PKI_EEVDF_QUEUE Queue,
    _In_opt_ PKI_EEVDF_ENTITY Running,
    _In_ PKI_EEVDF_ENTITY Candidate,
    _In_ PKI_EEVDF_ENTITY Incumbent)
{
    PKI_EEVDF_ENTITY Entity;
    PLIST_ENTRY Link;
    ULONG64 VirtualTime;

    ASSERT(!Candidate->Listed);
    ASSERT(!Incumbent->Listed);
    ASSERT(Candidate != Incumbent);

    VirtualTime = KiEevdfSettle(Queue, Running, Incumbent, Candidate, NULL);

    if (!KiEevdfIsEligible(Candidate, VirtualTime))
        return FALSE;

    if (KiEevdfIsEligible(Incumbent, VirtualTime) &&
        ((LONG64)(Incumbent->Deadline - Candidate->Deadline) <= 0))
    {
        return FALSE;
    }

    for (Link = Queue->DeadlineList.Flink; Link != &Queue->DeadlineList; Link = Link->Flink)
    {
        Entity = CONTAINING_RECORD(Link, KI_EEVDF_ENTITY, DeadlineLink);
        if ((LONG64)(Entity->Deadline - Candidate->Deadline) > 0)
            break;
        if (KiEevdfIsEligible(Entity, VirtualTime))
            return FALSE;
    }

    return TRUE;
}

static
VOID
KiEevdfForfeit(
    _Inout_ PKI_EEVDF_ENTITY Entity,
    _In_ ULONG64 Slice)
{
    ASSERT(!Entity->Listed);
    Entity->Deadline += Slice << KiEevdfShift(Entity->Priority);
}

static
VOID
KiEevdfAssertQueue(
    _In_ PKI_EEVDF_QUEUE Queue)
{
#if DBG
    PKI_EEVDF_ENTITY Entity, Previous = NULL;
    PLIST_ENTRY Link;
    LONG64 Sum = 0, Load = 0;
    ULONG Count = 0;

    if (Queue->Count > KI_EEVDF_VERIFY_LIMIT)
        return;

    for (Link = Queue->DeadlineList.Flink; Link != &Queue->DeadlineList; Link = Link->Flink)
    {
        Entity = CONTAINING_RECORD(Link, KI_EEVDF_ENTITY, DeadlineLink);
        ASSERT(Entity->Listed);
        ASSERT(Link->Blink->Flink == Link);
        ASSERT((Previous == NULL) || ((LONG64)(Previous->Deadline - Entity->Deadline) <= 0));
        Sum += KiEevdfWeightedKey(Queue, Entity);
        Load += KiEevdfWeight(Entity->Priority);
        Count++;
        Previous = Entity;
    }

    ASSERT(Count == Queue->Count);
    ASSERT((ULONG64)Load == Queue->Load);
    ASSERT(Sum == Queue->WeightedKeySum);
#else
    UNREFERENCED_PARAMETER(Queue);
#endif
}

/* GLOBALS ********************************************************************/

BOOLEAN KiEevdfEnabled;

static KI_EEVDF_QUEUE KiEevdfQueue[MAXIMUM_PROCESSORS];

extern ULONG KiCyclesPerClockQuantum;

/* PRIVATE FUNCTIONS **********************************************************/

FORCEINLINE
PKI_EEVDF_QUEUE
KiEevdfQueueOf(
    _In_ PKPRCB Prcb)
{
    ASSERT(Prcb->Number < RTL_NUMBER_OF(KiEevdfQueue));
    return &KiEevdfQueue[Prcb->Number];
}

FORCEINLINE
ULONG
KiEevdfHomeOf(
    _In_ PKPRCB Prcb)
{
    return (ULONG)Prcb->Number + 1;
}

/* Only a thread object the process manager created has an entity behind it; an idle thread does not */
FORCEINLINE
PKI_EEVDF_ENTITY
KiEevdfEntityOf(
    _In_ PKTHREAD Thread)
{
    ASSERT(KiProcessorBlock[Thread->NextProcessor]->IdleThread != Thread);
    return (PKI_EEVDF_ENTITY)(CONTAINING_RECORD(Thread, ETHREAD, Tcb) + 1);
}

FORCEINLINE
PKTHREAD
KiEevdfThreadOf(
    _In_ PKI_EEVDF_ENTITY Entity)
{
    return &(((PETHREAD)Entity) - 1)->Tcb;
}

/* A turn is only ended on the clock interrupt, and a request shorter than what is served does not bound the lag */
static
ULONG64
KiEevdfSlice(VOID)
{
    ULONG64 Slice;

    if (KeMaximumIncrement == 0)
        return 1;

    Slice = ((ULONG64)KiCyclesPerClockQuantum * CLOCK_QUANTUM_DECREMENT *
             max(KeTimeIncrement, KI_EEVDF_MINIMUM_SLICE_100NS)) / KeMaximumIncrement;
    return max(Slice, 1ULL);
}

static
ULONG64
KiEevdfLagLimit(
    _In_ ULONG64 Slice)
{
    return 2 * Slice;
}

static
ULONG64
KiEevdfReadCycleTime(
    _In_ PKTHREAD Thread)
{
    ULONG64 First, Second;

    Second = Thread->CycleTime;
    if (sizeof(PVOID) >= sizeof(ULONG64))
        return Second;

    /* A counter wider than a register is charged in two stores by the processor running the thread */
    do
    {
        First = Second;
        Second = Thread->CycleTime;
    } while (First != Second);

    return Second;
}

static
PKI_EEVDF_ENTITY
KiEevdfRunningEntity(
    _In_ PKPRCB Prcb)
{
    PKTHREAD Thread = Prcb->CurrentThread;
    PKI_EEVDF_ENTITY Entity;

    if ((Thread == NULL) || (Thread == Prcb->IdleThread))
        return NULL;

    Entity = KiEevdfEntityOf(Thread);
    if ((Entity->Home != KiEevdfHomeOf(Prcb)) || Entity->Listed)
        return NULL;

    return Entity;
}

static
VOID
KiEevdfSync(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _Inout_ PKI_EEVDF_ENTITY Entity,
    _In_ BOOLEAN Executing)
{
    ULONG64 CycleTime, Cycles, Slice;
    KIRQL OldIrql;

    ASSERT(Entity->Home == KiEevdfHomeOf(Prcb));
    ASSERT(!Entity->Listed);

    if (Executing)
    {
        ASSERT(Prcb == KeGetCurrentPrcb());

        /* The clock interrupt charges the same counters, so it is kept out while the cycles in flight are taken */
        KeRaiseIrql(HIGH_LEVEL, &OldIrql);
        KiChargeThreadCycleTime(Prcb, Thread);
        KeLowerIrql(OldIrql);
    }

    Slice = KiEevdfSlice();
    CycleTime = KiEevdfReadCycleTime(Thread);
    Cycles = CycleTime - Entity->ChargedCycleTime;
    if ((LONG64)Cycles > 0)
    {
        Entity->ChargedCycleTime = CycleTime;

        /* A thread is charged at least once a tick, so more than this is a counter that ran while the processor did not */
        KiEevdfAdvance(Entity, min(Cycles, 4 * KiEevdfLagLimit(Slice)), Slice);
    }

    if (KiEevdfIsFairPriority(Thread->Priority))
        KiEevdfReweight(KiEevdfQueueOf(Prcb), KiEevdfRunningEntity(Prcb), Entity, (UCHAR)Thread->Priority);
}

static
VOID
KiEevdfPlaceThread(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _Inout_ PKI_EEVDF_ENTITY Entity)
{
    ULONG64 Now;

    ASSERT(Entity->Home == 0);
    ASSERT(!Entity->Listed);
    ASSERT(KiEevdfIsFairPriority(Thread->Priority));

    Now = KiReadCycleCounter();
    KiEevdfPlace(KiEevdfQueueOf(Prcb),
                 KiEevdfRunningEntity(Prcb),
                 Entity,
                 (UCHAR)Thread->Priority,
                 KiEevdfSlice(),
                 (Now > Entity->LeftAt) ? (Now - Entity->LeftAt) : 0);
    Entity->ChargedCycleTime = KiEevdfReadCycleTime(Thread);

    /* The clock interrupt reads the entity without the lock once it sees the home */
    KeMemoryBarrier();
    Entity->Home = KiEevdfHomeOf(Prcb);
}

static
VOID
KiEevdfLeave(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _Inout_ PKI_EEVDF_ENTITY Entity,
    _In_ BOOLEAN Executing)
{
    ASSERT(Entity->Home == KiEevdfHomeOf(Prcb));
    ASSERT(!Entity->Listed);

    KiEevdfSync(Prcb, Thread, Entity, Executing);
    KiEevdfTakeLag(KiEevdfQueueOf(Prcb),
                   KiEevdfRunningEntity(Prcb),
                   Entity,
                   KiEevdfLagLimit(KiEevdfSlice()));
    Entity->LeftAt = KiReadCycleCounter();
    Entity->Home = 0;
}

static
PKI_EEVDF_ENTITY
KiEevdfHolder(
    _Inout_ PKPRCB Prcb,
    _In_opt_ PKTHREAD Thread)
{
    PKI_EEVDF_ENTITY Entity;

    if ((Thread == NULL) || (Thread == Prcb->IdleThread) || !KiEevdfIsFairPriority(Thread->Priority))
        return NULL;

    Entity = KiEevdfEntityOf(Thread);
    if ((Entity->Home != KiEevdfHomeOf(Prcb)) || Entity->Listed)
        return NULL;

    KiEevdfSync(Prcb,
                Thread,
                Entity,
                (BOOLEAN)((Thread == Prcb->CurrentThread) && (Prcb == KeGetCurrentPrcb())));
    return Entity;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
KeEnableEevdfScheduler(VOID)
{
    ULONG Index;

    ASSERT(!KiEevdfEnabled);
    ASSERT(KeGetCurrentPrcb()->ReadySummary == 0);

    for (Index = 0; Index < RTL_NUMBER_OF(KiEevdfQueue); Index++)
    {
        InitializeListHead(&KiEevdfQueue[Index].DeadlineList);
        KiEevdfQueue[Index].WeightedKeySum = 0;
        KiEevdfQueue[Index].Load = 0;
        KiEevdfQueue[Index].BaseVruntime = 0;
        KiEevdfQueue[Index].Count = 0;
    }

    KiEevdfEnabled = TRUE;
    DPRINT1("Experimental EEVDF scheduling of priorities 1 to %d is on\n", KI_EEVDF_TOP_PRIORITY);
}

VOID
FASTCALL
KiEevdfEnqueue(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _In_ BOOLEAN Executing)
{
    PKI_EEVDF_QUEUE Queue = KiEevdfQueueOf(Prcb);
    PKI_EEVDF_ENTITY Entity = KiEevdfEntityOf(Thread);

    ASSERT(Prcb->PrcbLock != 0);
    ASSERT(Thread->State == Ready);
    ASSERT(!Entity->Listed);
    ASSERT((Entity->Home == 0) || (Entity->Home == KiEevdfHomeOf(Prcb)));

    if (!KiEevdfIsFairPriority(Thread->Priority))
    {
        if (Entity->Home == KiEevdfHomeOf(Prcb))
            KiEevdfLeave(Prcb, Thread, Entity, Executing);
        return;
    }

    if (Entity->Home == KiEevdfHomeOf(Prcb))
        KiEevdfSync(Prcb, Thread, Entity, Executing);
    else
        KiEevdfPlaceThread(Prcb, Thread, Entity);

    ASSERT(Entity->Priority == Thread->Priority);
    KiEevdfInsert(Queue, Entity);
    KiEevdfAssertQueue(Queue);
}

VOID
FASTCALL
KiEevdfUnlist(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread)
{
    PKI_EEVDF_QUEUE Queue = KiEevdfQueueOf(Prcb);
    PKI_EEVDF_ENTITY Entity = KiEevdfEntityOf(Thread);

    ASSERT(Prcb->PrcbLock != 0);

    if (!Entity->Listed)
        return;

    ASSERT(Entity->Home == KiEevdfHomeOf(Prcb));
    KiEevdfRemove(Queue, Entity);
    KiEevdfAssertQueue(Queue);
}

PKTHREAD
FASTCALL
KiEevdfSelectReadyThread(
    _In_ KPRIORITY Priority,
    _Inout_ PKPRCB Prcb,
    _In_opt_ PKTHREAD Incumbent)
{
    PKI_EEVDF_QUEUE Queue = KiEevdfQueueOf(Prcb);
    PKI_EEVDF_ENTITY Holder, Entity;
    PKTHREAD Thread;
    KPRIORITY Floor = max(Priority, LOW_REALTIME_PRIORITY);
    ULONG Above, Highest;

    ASSERT(Prcb->PrcbLock != 0);

    if (Priority > HIGH_PRIORITY)
        return NULL;

    Holder = KiEevdfHolder(Prcb, Incumbent);

    for (;;)
    {
        Above = Prcb->ReadySummary >> Floor;
        if (Above != 0)
        {
            BitScanReverse(&Highest, Above);
            Highest += Floor;
            ASSERT(!IsListEmpty(&Prcb->DispatcherReadyListHead[Highest]));
            Thread = CONTAINING_RECORD(Prcb->DispatcherReadyListHead[Highest].Flink, KTHREAD, WaitListEntry);
        }
        else if (Priority >= LOW_REALTIME_PRIORITY)
        {
            return NULL;
        }
        else
        {
            Entity = KiEevdfPick(Queue, KiEevdfRunningEntity(Prcb), Holder);
            if (Entity != NULL)
            {
                if (Entity == Holder)
                    return NULL;

                Thread = KiEevdfThreadOf(Entity);
            }
            else if ((Priority == 0) && (Prcb->ReadySummary & PRIORITY_MASK(0)))
            {
                Thread = CONTAINING_RECORD(Prcb->DispatcherReadyListHead[0].Flink, KTHREAD, WaitListEntry);
            }
            else
            {
                return NULL;
            }
        }

        ASSERT(Thread != Prcb->IdleThread);
        ASSERT(Thread->State == Ready && !Thread->ProcessReadyQueue);
        ASSERT(Thread->NextProcessor == Prcb->Number);

        KiReadyQueueRemove(Prcb, Thread, Thread->Priority);

        if (KiThreadAffinityMask(Thread) & AFFINITY_MASK(Prcb->Number))
            return Thread;

        KiInsertDeferredReadyList(Thread);
    }
}

VOID
FASTCALL
KiEevdfDepart(
    _Inout_ PKTHREAD Thread,
    _In_ ULONG TargetProcessor)
{
    PKI_EEVDF_ENTITY Entity;
    PKPRCB Prcb;
    ULONG Home;

    if (KiProcessorBlock[Thread->NextProcessor]->IdleThread == Thread)
        return;

    Entity = KiEevdfEntityOf(Thread);
    Home = Entity->Home;
    if ((Home == 0) || (Home == TargetProcessor + 1))
        return;

    ASSERT(Home <= (ULONG)KeNumberProcessors);
    Prcb = KiProcessorBlock[Home - 1];

    KiAcquirePrcbLock(Prcb);
    if (Entity->Home == Home)
    {
        ASSERT(!Entity->Listed);
        KiEevdfLeave(Prcb, Thread, Entity, FALSE);
    }
    KiReleasePrcbLock(Prcb);
}

VOID
FASTCALL
KiEevdfArrive(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread)
{
    PKI_EEVDF_ENTITY Entity;

    ASSERT(Prcb->PrcbLock != 0);

    if (Thread == Prcb->IdleThread)
        return;

    Entity = KiEevdfEntityOf(Thread);
    ASSERT(!Entity->Listed);
    ASSERT((Entity->Home == 0) || (Entity->Home == KiEevdfHomeOf(Prcb)));

    if (Entity->Home == KiEevdfHomeOf(Prcb))
    {
        if (KiEevdfIsFairPriority(Thread->Priority))
            KiEevdfSync(Prcb, Thread, Entity, FALSE);
        else
            KiEevdfLeave(Prcb, Thread, Entity, FALSE);
        return;
    }

    if (KiEevdfIsFairPriority(Thread->Priority))
        KiEevdfPlaceThread(Prcb, Thread, Entity);
}

BOOLEAN
FASTCALL
KiEevdfPreempts(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _Inout_ PKTHREAD Incumbent)
{
    PKI_EEVDF_ENTITY Candidate, Holder;

    ASSERT(Prcb->PrcbLock != 0);
    ASSERT(Thread != Incumbent);

    Holder = KiEevdfIsFairPriority(Thread->Priority) ? KiEevdfHolder(Prcb, Incumbent) : NULL;
    if (Holder == NULL)
        return (BOOLEAN)(Thread->Priority > Incumbent->Priority);

    Candidate = KiEevdfEntityOf(Thread);
    ASSERT(Candidate->Home == KiEevdfHomeOf(Prcb));
    ASSERT(!Candidate->Listed);

    return KiEevdfBeats(KiEevdfQueueOf(Prcb), KiEevdfRunningEntity(Prcb), Candidate, Holder);
}

/* A waker can have put the thread back on this processor before it finished giving it up; then it never left the timeline */
VOID
FASTCALL
KiEevdfBlock(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread)
{
    PKI_EEVDF_ENTITY Entity;

    ASSERT(Prcb->PrcbLock != 0);
    ASSERT(Prcb == KeGetCurrentPrcb());

    if (Thread == Prcb->IdleThread)
        return;

    Entity = KiEevdfEntityOf(Thread);
    if ((Entity->Home != KiEevdfHomeOf(Prcb)) || Entity->Listed || (Prcb->NextThread == Thread))
        return;

    KiEevdfLeave(Prcb, Thread, Entity, TRUE);
}

VOID
FASTCALL
KiEevdfMigrate(
    _Inout_ PKPRCB Source,
    _Inout_ PKPRCB Target,
    _Inout_ PKTHREAD Thread)
{
    PKI_EEVDF_ENTITY Entity = KiEevdfEntityOf(Thread);

    ASSERT(Source->PrcbLock != 0);
    ASSERT(Target->PrcbLock != 0);
    ASSERT(!Entity->Listed);

    if (Entity->Home == KiEevdfHomeOf(Source))
        KiEevdfLeave(Source, Thread, Entity, FALSE);

    KiEevdfArrive(Target, Thread);
}

VOID
FASTCALL
KiEevdfPriorityChanged(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread)
{
    PKI_EEVDF_ENTITY Entity;
    BOOLEAN Executing;

    ASSERT(Prcb->PrcbLock != 0);

    if (Thread == Prcb->IdleThread)
        return;

    Entity = KiEevdfEntityOf(Thread);
    if (Entity->Listed)
        return;

    Executing = (BOOLEAN)((Thread == Prcb->CurrentThread) && (Prcb == KeGetCurrentPrcb()));

    if (Entity->Home == KiEevdfHomeOf(Prcb))
    {
        if (KiEevdfIsFairPriority(Thread->Priority))
            KiEevdfSync(Prcb, Thread, Entity, Executing);
        else
            KiEevdfLeave(Prcb, Thread, Entity, Executing);
        return;
    }

    if ((Entity->Home == 0) && KiEevdfIsFairPriority(Thread->Priority))
        KiEevdfPlaceThread(Prcb, Thread, Entity);
}

VOID
FASTCALL
KiEevdfYield(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread)
{
    PKI_EEVDF_ENTITY Entity;

    ASSERT(Prcb->PrcbLock != 0);
    ASSERT(Prcb == KeGetCurrentPrcb());
    ASSERT(Thread == Prcb->CurrentThread);

    Entity = KiEevdfHolder(Prcb, Thread);
    if (Entity != NULL)
        KiEevdfForfeit(Entity, KiEevdfSlice());
}

/* Runs on the clock interrupt without the lock, so it only reads the entity: a wrong answer costs one needless or one late dispatch interrupt */
BOOLEAN
FASTCALL
KiEevdfClockTick(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _Out_ PBOOLEAN SliceEnded)
{
    PKI_EEVDF_ENTITY Entity;
    ULONG64 Vruntime;
    UCHAR Priority;

    *SliceEnded = FALSE;

    if (Thread == Prcb->IdleThread)
        return FALSE;

    Entity = KiEevdfEntityOf(Thread);
    if (Entity->Home != KiEevdfHomeOf(Prcb))
        return FALSE;

    Priority = Entity->Priority;
    if (!KiEevdfIsFairPriority(Priority))
        return FALSE;

    KiChargeThreadCycleTime(Prcb, Thread);

    Vruntime = Entity->Vruntime + ((Thread->CycleTime - Entity->ChargedCycleTime) << KiEevdfShift(Priority));
    *SliceEnded = (BOOLEAN)((LONG64)(Vruntime - Entity->Deadline) >= 0);
    return TRUE;
}

BOOLEAN
FASTCALL
KiEevdfTurnOver(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread,
    _Out_ PBOOLEAN SliceEnded)
{
    PKI_EEVDF_ENTITY Entity;

    ASSERT(Prcb == KeGetCurrentPrcb());
    ASSERT(Thread == Prcb->CurrentThread);

    *SliceEnded = FALSE;

    KiAcquirePrcbLock(Prcb);

    Entity = KiEevdfHolder(Prcb, Thread);
    if (Entity == NULL)
    {
        KiReleasePrcbLock(Prcb);
        return FALSE;
    }

    *SliceEnded = Entity->SliceEnded;
    Entity->SliceEnded = FALSE;
    KiEevdfSettle(KiEevdfQueueOf(Prcb), Entity, NULL, NULL, NULL);

    KiReleasePrcbLock(Prcb);
    return TRUE;
}

/* EOF */
