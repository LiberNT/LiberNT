/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Which processors exist, which are usable, and which one is asking
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/drivers/acpipagr.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* How many processors the system brought up, and which of them are running. */
volatile CCHAR KeNumberProcessors;
KAFFINITY KeActiveProcessors;

/* The ceiling the boot options asked for, and whether they asked at all. The
   difference matters: a system told to use two of eight processors must not be
   mistaken for one that only has two. */
ULONG KeMaximumProcessors = MAXIMUM_PROCESSORS;
ULONG KeNumprocSpecified;
ULONG KeBootprocSpecified;

/* PRIVATE DATA ***************************************************************/

/*
 * Processors the power manager has asked the scheduler to leave alone. A parked
 * processor is still running and still takes interrupts; what it stops getting
 * is new work, so that its package can reach a deeper idle state.
 */
static KAFFINITY KiParkedProcessors;

KAFFINITY KiHeteroEfficientSet;
KAFFINITY KiHeteroPerformanceSet;

#define KI_PARKING_PERIOD_MS 50
#define KI_PARKING_CONTAIN_INTERVALS 6
#define KI_PARKING_PARK_PERCENT 50
#define KI_PARKING_UNPARK_PERCENT 75
#define KI_PARKING_PERFORMANCE_SCALE 2
#define KI_PARKING_HINT_HOLDOFF_MS 200

C_ASSERT(KI_PARKING_PARK_PERCENT < KI_PARKING_UNPARK_PERCENT);

typedef enum _KI_PARKING_TIMER_MODE
{
    KiParkingTimerIdle,
    KiParkingTimerSampling,
    KiParkingTimerChecking
} KI_PARKING_TIMER_MODE;

typedef struct _KI_PARKING_SAMPLE
{
    ULONG64 CycleTime;
    ULONG64 IdleCycleTime;
} KI_PARKING_SAMPLE, *PKI_PARKING_SAMPLE;

static KSPIN_LOCK KiParkingLock;
static PKPRCB KiParkingLockOwner;
static KTIMER KiParkingTimer;
static KDPC KiParkingTimerDpc;
static KDPC KiParkingUnparkDpc;
static volatile LONG KiParkingUnparkCheckPending;
static volatile LONG KiParkingContained;
static volatile LONG64 KiParkingHintHoldoff;
static KI_PARKING_TIMER_MODE KiParkingTimerMode;
static ULONG64 KiParkingSampleTime;
static ULONG KiParkingQuietIntervals;
static KAFFINITY KiAggregatorParkedSet;
static KAFFINITY KiHeteroParkedSet;
static KI_PARKING_SAMPLE KiParkingSamples[MAXIMUM_PROCESSORS];

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * The set of processors the system is using. It is not the set that exists:
 * a processor that failed to start, or that the boot options excluded, is
 * never in it.
 */
KAFFINITY
NTAPI
KeQueryActiveProcessors(VOID)
{
    return KeActiveProcessors;
}

/*
 * How many processors are running in one group, or in the whole system when
 * asked for every group at once.
 *
 * The groups are the topology the processor-group code keeps; asking it rather
 * than counting KeActiveProcessors is what makes the answer right on a machine
 * with more than one group, where that mask describes only one of them.
 */
ULONG
NTAPI
KeQueryActiveProcessorCountEx(
    _In_ USHORT GroupNumber)
{
    USHORT Groups = KeQueryActiveGroupCount();
    ULONG Count = 0;
    USHORT Group;

    if (GroupNumber != ALL_PROCESSOR_GROUPS)
    {
        if (GroupNumber >= Groups)
            return 0;

        return RtlNumberOfSetBitsUlongPtr(KeQueryGroupAffinity(GroupNumber));
    }

    for (Group = 0; Group < Groups; Group++)
        Count += RtlNumberOfSetBitsUlongPtr(KeQueryGroupAffinity(Group));

    return Count;
}

/*
 * How many processors are running, and which ones.
 *
 * This is the form from before processor groups existed, so the mask it hands
 * back can only describe one group. It describes the group the caller is in,
 * which on a machine with one group is every processor there is.
 *
 * sdk/include/xdk/kefuncs.h declares it only under SINGLE_GROUP_LEGACY_API,
 * which the kernel does not define, so the published prototype is restated
 * here for the definition to be checked against.
 */
ULONG
NTAPI
KeQueryActiveProcessorCount(
    _Out_opt_ PKAFFINITY ActiveProcessors);

ULONG
NTAPI
KeQueryActiveProcessorCount(
    _Out_opt_ PKAFFINITY ActiveProcessors)
{
    PROCESSOR_NUMBER Processor;
    KAFFINITY Active;

    KeGetCurrentProcessorNumberEx(&Processor);

    /* A processor whose group the topology does not know answers for group 0 */
    if (Processor.Group >= KeQueryActiveGroupCount())
        Processor.Group = 0;

    Active = KeQueryGroupAffinity(Processor.Group);

    if (ActiveProcessors != NULL)
        *ActiveProcessors = Active;

    return RtlNumberOfSetBitsUlongPtr(Active);
}

/* PUBLIC FUNCTIONS: MEMORY NODES **********************************************/

/*
 * The highest numbered memory node on the machine. A machine the firmware
 * described no topology for has one node, numbered zero, so the answer is
 * zero rather than an error: every caller is counting from it.
 */
USHORT
NTAPI
KeQueryHighestNodeNumber(VOID)
{
    UCHAR Nodes = KeNumberNodes;

    if (Nodes <= 1)
        return 0;

    return (USHORT)(Nodes - 1);
}

/*
 * The memory node the calling processor belongs to, which is the node whose
 * memory it reaches fastest.
 *
 * The answer is found by asking which node claims this processor rather than
 * by reading a second record of the same thing: the node to processor mapping
 * has one owner, and a copy of it here could disagree with it.
 *
 * It is a snapshot below DISPATCH_LEVEL, where the caller can be moved to
 * another processor the instant it is read.
 */
USHORT
NTAPI
KeGetCurrentNodeNumber(VOID)
{
    GROUP_AFFINITY Affinity;
    PROCESSOR_NUMBER Processor;
    USHORT Highest, Node;

    KeGetCurrentProcessorNumberEx(&Processor);

    if (Processor.Group >= KeQueryActiveGroupCount())
        Processor.Group = 0;

    Highest = KeQueryHighestNodeNumber();

    for (Node = 0; Node <= Highest; Node++)
    {
        RtlZeroMemory(&Affinity, sizeof(Affinity));
        KeQueryNodeActiveAffinity(Node, &Affinity, NULL);

        if ((Affinity.Group == Processor.Group) &&
            (Affinity.Mask & AFFINITY_MASK(Processor.Number)))
        {
            return Node;
        }
    }

    /* Claimed by nothing: a machine with one node, where that node is zero */
    return 0;
}

/*
 * How many processors could ever be running. A driver sizes a per-processor
 * array against this and not against the current count, because a processor
 * may still be brought up.
 */
ULONG
NTAPI
KeQueryMaximumProcessorCountEx(
    _In_ USHORT GroupNumber)
{
    if ((GroupNumber != ALL_PROCESSOR_GROUPS) && (GroupNumber != 0))
        return 0;

    return min(KeMaximumProcessors, MAXIMUM_PROCESSORS);
}

ULONG
NTAPI
KeQueryMaximumProcessorCount(VOID)
{
    return KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
}

/*
 * Which processor the caller is on. The answer is only meaningful while the
 * caller cannot be moved, which below DISPATCH_LEVEL it can be.
 */
ULONG
NTAPI
KeGetCurrentProcessorNumberEx(
    _Out_opt_ PPROCESSOR_NUMBER ProcNumber)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    if (ProcNumber != NULL)
    {
        ProcNumber->Group = KiPrcbGroup(Prcb);
        ProcNumber->Number = (UCHAR)Prcb->Number;
        ProcNumber->Reserved = 0;
    }

    return Prcb->Number;
}

/*
 * The same, asked from user mode. The number is stale as soon as it is read,
 * which is why the interface exists at all: a caller that needs a number it
 * can rely on has to pin itself to a processor first.
 */
NTSTATUS
NTAPI
NtGetCurrentProcessorNumberEx(
    _Out_opt_ PPROCESSOR_NUMBER ProcNumber)
{
    PROCESSOR_NUMBER Local;

    KeGetCurrentProcessorNumberEx(&Local);

    if (ExGetPreviousMode() != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(ProcNumber, sizeof(*ProcNumber), sizeof(USHORT));
            *ProcNumber = Local;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else if (ProcNumber != NULL)
    {
        *ProcNumber = Local;
    }

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS: PARKING ***************************************************/

/*
 * The processors the scheduler should still place work on. Where every
 * processor a thread may use is parked the set is empty, and the caller falls
 * back to using them anyway: parking must never stop a thread running.
 */
KAFFINITY
FASTCALL
KiGetNonParkedProcessorSet(VOID)
{
    return KeActiveProcessors & ~KiParkedProcessors;
}

BOOLEAN
FASTCALL
KiIsProcessorParked(
    _In_ PKPRCB Prcb)
{
    return (KiParkedProcessors & AFFINITY_MASK(Prcb->Number)) != 0;
}

static
ULONG
KiCountProcessorSet(
    _In_ KAFFINITY ProcessorSet)
{
    ULONG Count = 0;

    while (ProcessorSet)
    {
        ProcessorSet &= ProcessorSet - 1;
        Count++;
    }
    return Count;
}

static
VOID
KiAcquireParkingLock(VOID)
{
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    KeAcquireSpinLockAtDpcLevel(&KiParkingLock);
    ASSERT(KiParkingLockOwner == NULL);
    KiParkingLockOwner = KeGetCurrentPrcb();
}

static
VOID
KiReleaseParkingLock(VOID)
{
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(KiParkingLockOwner == KeGetCurrentPrcb());
    KiParkingLockOwner = NULL;
    KeReleaseSpinLockFromDpcLevel(&KiParkingLock);
}

BOOLEAN
FASTCALL
KiThreadPrefersEfficiency(
    _In_ PKTHREAD Thread)
{
    PETHREAD EThread = CONTAINING_RECORD(Thread, ETHREAD, Tcb);
    PPO_PROCESS_ENERGY_CONTEXT EnergyContext;

    if ((Thread->BasePriority >= LOW_REALTIME_PRIORITY) || (Thread->Priority > Thread->BasePriority))
        return FALSE;

    if (*(volatile ULONG *)&EThread->PowerThrottlingControlMask & THREAD_POWER_THROTTLING_EXECUTION_SPEED)
        return (*(volatile ULONG *)&EThread->PowerThrottlingStateMask & THREAD_POWER_THROTTLING_EXECUTION_SPEED) != 0;

    /* FIXME: the precedence of a thread's power throttling state over its process's is not documented */
    if (Thread->Process != NULL)
    {
        EnergyContext = ReadPointerAcquire((volatile PVOID *)&CONTAINING_RECORD(Thread->Process, EPROCESS, Pcb)->EnergyContext);
        if ((EnergyContext != NULL) &&
            (ReadAcquire(&EnergyContext->PowerThrottlingControlMask) & PROCESS_POWER_THROTTLING_EXECUTION_SPEED))
        {
            return (ReadAcquire(&EnergyContext->PowerThrottlingStateMask) & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0;
        }
    }

    /* TODO: assign the Low and Utility QoS levels from window visibility, background services and thread priority */
    return FALSE;
}

KAFFINITY
FASTCALL
KiHeteroPreferredProcessorSet(
    _In_ PKTHREAD Thread,
    _In_ KAFFINITY ProcessorSet)
{
    KAFFINITY EfficientSet = *(volatile KAFFINITY *)&KiHeteroEfficientSet;

    if (!(ProcessorSet & EfficientSet) || !(ProcessorSet & ~EfficientSet))
        return ProcessorSet;

    if (!KiThreadPrefersEfficiency(Thread))
        return ProcessorSet;

    ASSERT((ProcessorSet & EfficientSet) != 0);
    return ProcessorSet & EfficientSet;
}

BOOLEAN
FASTCALL
KiShouldVacateProcessor(
    _In_ PKTHREAD Thread,
    _In_ PKPRCB Prcb)
{
    KAFFINITY Allowed, EfficientSet, IdleSet, NonParkedSet;

    ASSERT(Thread != Prcb->IdleThread);
    ASSERT(Prcb->SetMember == AFFINITY_MASK(Prcb->Number));
#ifdef CONFIG_SMP
    ASSERT(Prcb->PrcbLock != 0);
#endif

    NonParkedSet = KiGetNonParkedProcessorSet();
    Allowed = KiThreadAffinityMask(Thread) & NonParkedSet;
    if (!(NonParkedSet & Prcb->SetMember))
        return Allowed != 0;

    EfficientSet = *(volatile KAFFINITY *)&KiHeteroEfficientSet & Allowed;
    if ((EfficientSet == 0) || (EfficientSet == Allowed))
        return FALSE;

    if (!(EfficientSet & Prcb->SetMember))
        return KiThreadPrefersEfficiency(Thread);

    IdleSet = *(volatile KAFFINITY *)&KiIdleSummary;
    return (Allowed & ~EfficientSet & IdleSet) && !KiThreadPrefersEfficiency(Thread);
}

VOID
FASTCALL
KiCheckProcessorUnpark(
    _In_ PKTHREAD Thread)
{
    KAFFINITY Affinity, NonParkedSet;

    if (!*(volatile LONG *)&KiParkingContained)
        return;

    Affinity = KiThreadAffinityMask(Thread) & KeActiveProcessors;
    NonParkedSet = KiGetNonParkedProcessorSet();
    if (!(Affinity & NonParkedSet) ||
        !(Affinity & *(volatile KAFFINITY *)&KiHeteroParkedSet) ||
        (Affinity & NonParkedSet & *(volatile KAFFINITY *)&KiIdleSummary) ||
        ((LONG64)KeQueryInterruptTime() < InterlockedCompareExchange64(&KiParkingHintHoldoff, 0, 0)) ||
        KiThreadPrefersEfficiency(Thread))
    {
        return;
    }

    if (InterlockedCompareExchange(&KiParkingUnparkCheckPending, 1, 0) == 0)
        KeInsertQueueDpc(&KiParkingUnparkDpc, NULL, NULL);
}

static
VOID
KiPublishParkedProcessors(VOID)
{
    KAFFINITY ActiveSet = KeActiveProcessors;
    KAFFINITY ParkedSet, NonParkedSet, OldNonParkedSet, InterruptSet, Remaining;
    ULONG Processor;
    PKPRCB Prcb;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(KiParkingLockOwner == KeGetCurrentPrcb());
    ASSERT((KiHeteroParkedSet & ~KiHeteroPerformanceSet) == 0);

    ParkedSet = (KiAggregatorParkedSet | KiHeteroParkedSet) & ActiveSet;
    if (ParkedSet == ActiveSet)
        ParkedSet = KiAggregatorParkedSet & ActiveSet;
    NonParkedSet = ActiveSet & ~ParkedSet;
    ASSERT(NonParkedSet != 0);

    Remaining = ActiveSet;
    while (BitScanForwardAffinity(&Processor, Remaining))
    {
        Remaining &= ~AFFINITY_MASK(Processor);
        Prcb = KiProcessorBlock[Processor];
        ASSERT((Prcb != NULL) && (Prcb->Number == Processor));
        Prcb->PowerState.Parked = (ParkedSet & AFFINITY_MASK(Processor)) ? 1 : 0;
    }

    OldNonParkedSet = ActiveSet & ~(KAFFINITY)(ULONG_PTR)InterlockedExchangePointer((PVOID volatile *)&KiParkedProcessors,
                                                                                    (PVOID)(ULONG_PTR)ParkedSet);
    MmWriteableSharedUserData->UnparkedProcessorCount = (USHORT)KiCountProcessorSet(NonParkedSet);
    ASSERT(KiGetNonParkedProcessorSet() == NonParkedSet);

    InterruptSet = NonParkedSet & ~OldNonParkedSet & ~KeGetCurrentPrcb()->SetMember;
    if (InterruptSet != 0)
        KiIpiSend(InterruptSet, IPI_DPC);
}

static
VOID
KiParkingResetSamples(VOID)
{
    KAFFINITY Remaining = KeActiveProcessors;
    ULONG Processor;
    PKPRCB Prcb;

    while (BitScanForwardAffinity(&Processor, Remaining))
    {
        Remaining &= ~AFFINITY_MASK(Processor);
        Prcb = KiProcessorBlock[Processor];
        ASSERT((Prcb != NULL) && (Prcb->IdleThread != NULL));
        KiParkingSamples[Processor].CycleTime = *(volatile ULONG64 *)&Prcb->CycleTime;
        KiParkingSamples[Processor].IdleCycleTime = *(volatile ULONG64 *)&Prcb->IdleThread->CycleTime;
    }
}

static
VOID
KiParkingSampleLoad(
    _Out_ PULONG EfficientBusy,
    _Out_ PULONG PerformanceBusy)
{
    KAFFINITY Remaining = KeActiveProcessors;
    ULONG64 CycleTime, IdleCycleTime, Cycles, IdleCycles;
    ULONG Processor, BusyPercent;
    PKPRCB Prcb;

    *EfficientBusy = 0;
    *PerformanceBusy = 0;
    while (BitScanForwardAffinity(&Processor, Remaining))
    {
        Remaining &= ~AFFINITY_MASK(Processor);
        Prcb = KiProcessorBlock[Processor];
        ASSERT((Prcb != NULL) && (Prcb->IdleThread != NULL));
        CycleTime = *(volatile ULONG64 *)&Prcb->CycleTime;
        IdleCycleTime = *(volatile ULONG64 *)&Prcb->IdleThread->CycleTime;
        Cycles = CycleTime - KiParkingSamples[Processor].CycleTime;
        IdleCycles = IdleCycleTime - KiParkingSamples[Processor].IdleCycleTime;
        KiParkingSamples[Processor].CycleTime = CycleTime;
        KiParkingSamples[Processor].IdleCycleTime = IdleCycleTime;
        BusyPercent = ((Cycles != 0) && (IdleCycles < Cycles)) ? (ULONG)(((Cycles - IdleCycles) * 100) / Cycles) : 0;
        ASSERT(BusyPercent <= 100);
        if (KiHeteroEfficientSet & AFFINITY_MASK(Processor))
            *EfficientBusy += BusyPercent;
        else
            *PerformanceBusy += BusyPercent;
    }
}

static
VOID
KiArmParkingTimer(
    _In_ KI_PARKING_TIMER_MODE Mode,
    _In_ ULONG64 Now)
{
    LARGE_INTEGER DueTime;
    BOOLEAN Inserted;

    ASSERT(KiParkingLockOwner == KeGetCurrentPrcb());
    ASSERT(KiParkingTimerMode == KiParkingTimerIdle);
    ASSERT((Mode == KiParkingTimerSampling) || (Mode == KiParkingTimerChecking));

    KiParkingResetSamples();
    KiParkingSampleTime = Now;
    KiParkingQuietIntervals = 0;
    KiParkingTimerMode = Mode;
    DueTime.QuadPart = -(LONGLONG)KI_PARKING_PERIOD_MS * 10000;
    Inserted = KeSetTimerEx(&KiParkingTimer,
                            DueTime,
                            (Mode == KiParkingTimerSampling) ? KI_PARKING_PERIOD_MS : 0,
                            &KiParkingTimerDpc);
    ASSERT(!Inserted);
    UNREFERENCED_PARAMETER(Inserted);
}

static
VOID
NTAPI
KiParkingDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    ULONG EfficientBusy, PerformanceBusy, EfficientCapacity;
    ULONG64 Now;

    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT((Dpc == &KiParkingUnparkDpc) || (Dpc == &KiParkingTimerDpc));
    ASSERT(KiHeteroEfficientSet != 0);

    EfficientCapacity = KiCountProcessorSet(KiHeteroEfficientSet & KeActiveProcessors) * 100;
    ASSERT(EfficientCapacity != 0);

    KiAcquireParkingLock();
    Now = KeQueryInterruptTime();
    ASSERT(((KiParkingContained != 0) == (KiHeteroParkedSet != 0)));
    ASSERT((KiParkingTimerMode != KiParkingTimerSampling) || !KiParkingContained);
    ASSERT((KiParkingTimerMode != KiParkingTimerChecking) || KiParkingContained);

    if (Dpc == &KiParkingUnparkDpc)
    {
        ASSERT(KiParkingUnparkCheckPending != 0);
        if (KiParkingContained && (KiParkingTimerMode == KiParkingTimerIdle))
            KiArmParkingTimer(KiParkingTimerChecking, Now);
        else
            InterlockedExchange(&KiParkingUnparkCheckPending, 0);
    }
    else if ((KiParkingTimerMode != KiParkingTimerIdle) &&
             (Now - KiParkingSampleTime >= (ULONG64)KI_PARKING_PERIOD_MS * 10000 / 2))
    {
        KiParkingSampleLoad(&EfficientBusy, &PerformanceBusy);
        KiParkingSampleTime = Now;
        if (KiParkingTimerMode == KiParkingTimerChecking)
        {
            KiParkingTimerMode = KiParkingTimerIdle;
            if (EfficientBusy * 100 >= EfficientCapacity * KI_PARKING_UNPARK_PERCENT)
            {
                DPRINT1("Unparking performance processors 0x%Ix: efficiency processors are %lu%% busy\n",
                        KiHeteroParkedSet, EfficientBusy * 100 / EfficientCapacity);
                KiHeteroParkedSet = 0;
                InterlockedExchange(&KiParkingContained, 0);
                KiPublishParkedProcessors();
                KiArmParkingTimer(KiParkingTimerSampling, Now);
            }
            else
            {
                InterlockedExchange64(&KiParkingHintHoldoff,
                                      (LONG64)(Now + (ULONG64)KI_PARKING_HINT_HOLDOFF_MS * 10000));
            }
            InterlockedExchange(&KiParkingUnparkCheckPending, 0);
        }
        else
        {
            if ((EfficientBusy + KI_PARKING_PERFORMANCE_SCALE * PerformanceBusy) * 100 <=
                EfficientCapacity * KI_PARKING_PARK_PERCENT)
            {
                KiParkingQuietIntervals++;
            }
            else
            {
                KiParkingQuietIntervals = 0;
            }

            if (KiParkingQuietIntervals >= KI_PARKING_CONTAIN_INTERVALS)
            {
                DPRINT1("Parking performance processors 0x%Ix: projected load %lu%% of the efficiency processors\n",
                        KiHeteroPerformanceSet,
                        (EfficientBusy + KI_PARKING_PERFORMANCE_SCALE * PerformanceBusy) * 100 / EfficientCapacity);
                KeCancelTimer(&KiParkingTimer);
                KiParkingTimerMode = KiParkingTimerIdle;
                KiParkingQuietIntervals = 0;
                KiHeteroParkedSet = KiHeteroPerformanceSet & KeActiveProcessors;
                InterlockedExchange(&KiParkingContained, 1);
                KiPublishParkedProcessors();
            }
        }
    }
    KiReleaseParkingLock();
}

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeProcessorEfficiency(VOID)
{
    UCHAR Classes[MAXIMUM_PROCESSORS];
    KAFFINITY ActiveSet = KeActiveProcessors;
    KAFFINITY EfficientSet = 0;
    KAFFINITY Remaining;
    UCHAR LowestClass = MAXUCHAR;
    UCHAR HighestClass = 0;
    ULONG Processor;
    PKPRCB Prcb;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(ActiveSet != 0);
    ASSERT((KiHeteroEfficientSet == 0) && (KiHeteroPerformanceSet == 0));

    RtlZeroMemory(Classes, sizeof(Classes));
    KiQueryProcessorEfficiencyClasses(Classes);

    Remaining = ActiveSet;
    while (BitScanForwardAffinity(&Processor, Remaining))
    {
        Remaining &= ~AFFINITY_MASK(Processor);
        Prcb = KiProcessorBlock[Processor];
        ASSERT((Prcb != NULL) && (Prcb->Number == Processor));
        ASSERT(Prcb->PowerState.Parked == 0);
        Prcb->PowerState.ArchitecturalEfficiencyClass = Classes[Processor];
        LowestClass = min(LowestClass, Classes[Processor]);
        HighestClass = max(HighestClass, Classes[Processor]);
    }
    MmWriteableSharedUserData->UnparkedProcessorCount = (USHORT)KiCountProcessorSet(ActiveSet & ~KiParkedProcessors);

    if (LowestClass == HighestClass)
    {
        DPRINT1("All %lu processors have efficiency class %u\n", KiCountProcessorSet(ActiveSet), LowestClass);
        return;
    }

    Remaining = ActiveSet;
    while (BitScanForwardAffinity(&Processor, Remaining))
    {
        Remaining &= ~AFFINITY_MASK(Processor);
        if (Classes[Processor] == LowestClass)
            EfficientSet |= AFFINITY_MASK(Processor);
    }
    ASSERT((EfficientSet != 0) && ((ActiveSet & ~EfficientSet) != 0));

    KeInitializeDpc(&KiParkingTimerDpc, KiParkingDpcRoutine, NULL);
    KeInitializeDpc(&KiParkingUnparkDpc, KiParkingDpcRoutine, NULL);
    KeInitializeTimerEx(&KiParkingTimer, NotificationTimer);

    KiHeteroPerformanceSet = ActiveSet & ~EfficientSet;
    KeMemoryBarrier();
    KiHeteroEfficientSet = EfficientSet;
    DPRINT1("Processor efficiency classes %u to %u: efficiency processors 0x%Ix, performance processors 0x%Ix\n",
            LowestClass, HighestClass, EfficientSet, KiHeteroPerformanceSet);

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KiAcquireParkingLock();
    KiArmParkingTimer(KiParkingTimerSampling, KeQueryInterruptTime());
    KiReleaseParkingLock();
    KeLowerIrql(OldIrql);
}

/*
 * Asks for a number of processors to be left idle. The ones chosen are the
 * highest numbered, because processor zero takes the clock and the boot work
 * and parking it would save nothing.
 *
 * Answers how many were actually parked, which is less than asked for where
 * that would have left nothing running.
 */
NTSTATUS
NTAPI
PoSetProcessorAggregatorParking(
    _In_ ULONG RequestedParkedProcessors,
    _Out_ PULONG ParkedProcessors)
{
    KAFFINITY Active, Park = 0;
    ULONG Count, Available, Index;
    KIRQL OldIrql;

    if (ParkedProcessors == NULL)
        return STATUS_INVALID_PARAMETER;

    Active = KeActiveProcessors;
    Available = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    /* One processor always has to be left to run things */
    if (RequestedParkedProcessors >= Available)
        RequestedParkedProcessors = Available - 1;

    Count = 0;
    Index = MAXIMUM_PROCESSORS;
    while ((Index > 0) && (Count < RequestedParkedProcessors))
    {
        Index--;
        if ((Active & AFFINITY_MASK(Index)) == 0)
            continue;

        Park |= AFFINITY_MASK(Index);
        Count++;
    }

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KiAcquireParkingLock();
    KiAggregatorParkedSet = Park;
    KiPublishParkedProcessors();
    KiReleaseParkingLock();
    KeLowerIrql(OldIrql);
    *ParkedProcessors = Count;

    return STATUS_SUCCESS;
}

/* EOF */
