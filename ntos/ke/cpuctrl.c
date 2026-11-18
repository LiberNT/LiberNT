/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Which processors exist, which are usable, and which one is asking
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <libernt/drivers/acpipagr.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* How many processors the system brought up, and which of them are running. */
ULONG KeProcessorCount;
KAFFINITY_EX KeActiveProcessors = { KAFFINITY_EX_STATIC_GROUPS, KAFFINITY_EX_STATIC_GROUPS };

/* The count a caller from before processor groups is given: the processors of group 0 */
volatile CCHAR KeNumberProcessors;

/* Only ever shrinks, so a set written under an older value stays whole under a newer one */
ULONG KiProcessorSetWords = (KiArchMaximumProcessors() + KI_SET_WORD_BITS - 1) / KI_SET_WORD_BITS;

ULONG KiMaximumGroupSize = MAXIMUM_PROC_PER_GROUP;

/* The ceiling the boot options asked for, and whether they asked at all. The
   difference matters: a system told to use two of eight processors must not be
   mistaken for one that only has two. */
ULONG KeMaximumProcessors = KiArchMaximumProcessors();
ULONG KeNumprocSpecified;
ULONG KeBootprocSpecified;

/* PRIVATE DATA ***************************************************************/

/*
 * Processors the power manager has asked the scheduler to leave alone. A parked
 * processor is still running and still takes interrupts; what it stops getting
 * is new work, so that its package can reach a deeper idle state.
 */
static KAFFINITY KiParkedProcessors[KE_MAXIMUM_GROUPS];

KAFFINITY KiHeteroEfficientSet[KE_MAXIMUM_GROUPS];
KAFFINITY KiHeteroPerformanceSet[KE_MAXIMUM_GROUPS];

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
static KAFFINITY KiAggregatorParkedSet[KE_MAXIMUM_GROUPS];
static KAFFINITY KiHeteroParkedSet[KE_MAXIMUM_GROUPS];
static KAFFINITY KiPowerParkedSet[KE_MAXIMUM_GROUPS];
static KI_PARKING_SAMPLE KiParkingSamples[KE_MAXIMUM_PROCESSORS];

/* PRIVATE FUNCTIONS: NUMBERING ************************************************/

VOID
NTAPI
KiSetProcessorCount(
    _In_ ULONG Count)
{
    ASSERT((Count != 0) && (Count <= KE_MAXIMUM_PROCESSORS));
    ASSERT(Count <= KiProcessorSetWords * KI_SET_WORD_BITS);

    KeProcessorCount = Count;
    KeNumberProcessors = (CCHAR)min(Count, KiMaximumGroupSize);
}

VOID
NTAPI
KiActivateProcessor(
    _In_ PKPRCB Prcb)
{
    ASSERT(Prcb->Number < KeProcessorCount);
    ASSERT(KiProcessorBlock[Prcb->Number] == Prcb);

    KiInterlockedAddProcessorToSet(KeActiveProcessors.Bitmap, Prcb->Number);
}

/* The size has to be a power of two no larger than a group, as Windows documents for its groupsize boot option */
CODE_SEG("INIT")
VOID
NTAPI
KeSetMaximumGroupSize(
    _In_ ULONG GroupSize)
{
    ASSERT(KeProcessorCount == 1);

    if ((GroupSize == 0) || (GroupSize > MAXIMUM_PROC_PER_GROUP) || ((GroupSize & (GroupSize - 1)) != 0))
    {
        DPRINT1("Ignoring a processor group size of %lu\n", GroupSize);
        return;
    }

    /* An architecture that keeps its processors in one word has one group */
    if (KiArchMaximumProcessors() <= MAXIMUM_PROC_PER_GROUP)
        return;

    KiMaximumGroupSize = GroupSize;
    KiSetProcessorCount(KeProcessorCount);
}

CODE_SEG("INIT")
VOID
NTAPI
KiCompleteProcessorNumbering(VOID)
{
    ULONG Words = (KeProcessorCount + KI_SET_WORD_BITS - 1) / KI_SET_WORD_BITS;

    ASSERT((Words != 0) && (Words <= KiProcessorSetWords));

    KeMaximumProcessors = KeProcessorCount;
    KiProcessorSetWords = Words;
}

/* A group is a run of KiMaximumGroupSize processor numbers, which starts inside a word when that size is below a word */
KAFFINITY
NTAPI
KiQueryGroupOfProcessorSet(
    _In_ const volatile KAFFINITY *Set,
    _In_ USHORT Group)
{
    ULONG First = (ULONG)Group * KiMaximumGroupSize;
    ULONG Word = KI_SET_WORD(First);
    ULONG Shift = First % KI_SET_WORD_BITS;
    KAFFINITY Mask;

    ASSERT(KiMaximumGroupSize <= KI_SET_WORD_BITS);
    if (Word >= KiProcessorSetWords)
        return 0;

    Mask = Set[Word] >> Shift;
    if ((Shift + KiMaximumGroupSize > KI_SET_WORD_BITS) && (Word + 1 < KiProcessorSetWords))
        Mask |= Set[Word + 1] << (KI_SET_WORD_BITS - Shift);

    if (KiMaximumGroupSize < KI_SET_WORD_BITS)
        Mask &= AFFINITY_MASK(KiMaximumGroupSize) - 1;

    return Mask;
}

VOID
NTAPI
KiAssignGroupOfProcessorSet(
    _Inout_ KAFFINITY *Set,
    _In_ USHORT Group,
    _In_ KAFFINITY Mask)
{
    ULONG First = (ULONG)Group * KiMaximumGroupSize;
    ULONG Word = KI_SET_WORD(First);
    ULONG Shift = First % KI_SET_WORD_BITS;
    KAFFINITY GroupBits = (KAFFINITY)-1;

    ASSERT(KiMaximumGroupSize <= KI_SET_WORD_BITS);
    if (Word >= KiProcessorSetWords)
        return;

    if (KiMaximumGroupSize < KI_SET_WORD_BITS)
        GroupBits = AFFINITY_MASK(KiMaximumGroupSize) - 1;

    Mask &= GroupBits;
    Set[Word] = (Set[Word] & ~(GroupBits << Shift)) | (Mask << Shift);
    if ((Shift + KiMaximumGroupSize > KI_SET_WORD_BITS) && (Word + 1 < KiProcessorSetWords))
    {
        Set[Word + 1] = (Set[Word + 1] & ~(GroupBits >> (KI_SET_WORD_BITS - Shift))) |
                        (Mask >> (KI_SET_WORD_BITS - Shift));
    }
}

/* Windows documents the legacy number outside group 0 only as being below the processor count of group 0 */
ULONG
NTAPI
KiLegacyProcessorNumber(
    _In_ ULONG Number)
{
    return Number % KiMaximumGroupSize;
}

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
    return KiQueryGroupOfProcessorSet(KeActiveProcessors.Bitmap, 0);
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
 * back can only describe one group. Windows documents that group as group 0,
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
    KAFFINITY Active = KeQueryGroupAffinity(0);

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
    ULONG Maximum = min(KeMaximumProcessors, KE_MAXIMUM_PROCESSORS);
    ULONG First;

    if (GroupNumber == ALL_PROCESSOR_GROUPS)
        return Maximum;

    First = (ULONG)GroupNumber * KiMaximumGroupSize;
    if (First >= Maximum)
        return 0;

    return min(Maximum - First, KiMaximumGroupSize);
}

ULONG
NTAPI
KeQueryMaximumProcessorCount(VOID)
{
    return KeQueryMaximumProcessorCountEx(0);
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
        ProcNumber->Number = (UCHAR)(Prcb->Number % KiMaximumGroupSize);
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
VOID
FASTCALL
KiGetNonParkedProcessorSet(
    _Out_writes_(KE_MAXIMUM_GROUPS) KAFFINITY *NonParkedSet)
{
    KiSubtractProcessorSets(NonParkedSet, KeActiveProcessors.Bitmap, KiParkedProcessors);
}

BOOLEAN
FASTCALL
KiIsProcessorParked(
    _In_ PKPRCB Prcb)
{
    return KiIsProcessorInSet(KiParkedProcessors, Prcb->Number);
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

VOID
FASTCALL
KiHeteroPreferredProcessorSet(
    _In_ PKTHREAD Thread,
    _Inout_ KAFFINITY *ProcessorSet)
{
    KI_PROCESSOR_SET EfficientSet;

    if (!KiAndProcessorSets(EfficientSet, ProcessorSet, KiHeteroEfficientSet) ||
        KiAreProcessorSetsEqual(EfficientSet, ProcessorSet))
    {
        return;
    }

    if (!KiThreadPrefersEfficiency(Thread))
        return;

    KiCopyProcessorSet(ProcessorSet, EfficientSet);
}

BOOLEAN
FASTCALL
KiShouldVacateProcessor(
    _In_ PKTHREAD Thread,
    _In_ PKPRCB Prcb)
{
    KI_PROCESSOR_SET Allowed, EfficientSet, NonParkedSet;
    BOOLEAN AnyAllowed;

    ASSERT(Thread != Prcb->IdleThread);
#ifdef CONFIG_SMP
    ASSERT(Prcb->PrcbLock != 0);
#endif

    KiGetNonParkedProcessorSet(NonParkedSet);
    AnyAllowed = KiAndProcessorSets(Allowed, KiThreadAffinity(Thread), NonParkedSet);
    if (!KiIsProcessorInSet(NonParkedSet, Prcb->Number))
        return AnyAllowed;

    if (!KiAndProcessorSets(EfficientSet, KiHeteroEfficientSet, Allowed) ||
        KiAreProcessorSetsEqual(EfficientSet, Allowed))
    {
        return FALSE;
    }

    if (!KiIsProcessorInSet(EfficientSet, Prcb->Number))
        return KiThreadPrefersEfficiency(Thread);

    KiSubtractProcessorSets(Allowed, Allowed, EfficientSet);
    return KiDoProcessorSetsIntersect(Allowed, KiIdleSummary) && !KiThreadPrefersEfficiency(Thread);
}

VOID
FASTCALL
KiCheckProcessorUnpark(
    _In_ PKTHREAD Thread)
{
    KI_PROCESSOR_SET Affinity, NonParkedSet;

    if (!*(volatile LONG *)&KiParkingContained)
        return;

    KiAndProcessorSets(Affinity, KiThreadAffinity(Thread), KeActiveProcessors.Bitmap);
    KiGetNonParkedProcessorSet(NonParkedSet);
    if (!KiAndProcessorSets(NonParkedSet, NonParkedSet, Affinity) ||
        !KiDoProcessorSetsIntersect(Affinity, KiHeteroParkedSet) ||
        KiDoProcessorSetsIntersect(NonParkedSet, KiIdleSummary) ||
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
    const KAFFINITY *ActiveSet = KeActiveProcessors.Bitmap;
    KI_PROCESSOR_SET ParkedSet, NonParkedSet, InterruptSet;
    KAFFINITY OldParked;
    ULONG Processor, Word;
    BOOLEAN Found;
    PKPRCB Prcb;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(KiParkingLockOwner == KeGetCurrentPrcb());
    ASSERT(KiIsProcessorSubset(KiHeteroParkedSet, KiHeteroPerformanceSet));

    KiOrProcessorSets(ParkedSet, KiAggregatorParkedSet, KiHeteroParkedSet);
    KiOrProcessorSets(ParkedSet, ParkedSet, KiPowerParkedSet);
    KiAndProcessorSets(ParkedSet, ParkedSet, ActiveSet);
    if (KiAreProcessorSetsEqual(ParkedSet, ActiveSet))
        KiAndProcessorSets(ParkedSet, KiAggregatorParkedSet, ActiveSet);
    Found = KiSubtractProcessorSets(NonParkedSet, ActiveSet, ParkedSet);
    ASSERT(Found);

    for (Found = KiFindProcessorInSet(ActiveSet, 0, &Processor);
         Found;
         Found = KiFindProcessorInSet(ActiveSet, Processor + 1, &Processor))
    {
        Prcb = KiProcessorBlock[Processor];
        ASSERT((Prcb != NULL) && (Prcb->Number == Processor));
        Prcb->PowerState.Parked = KiIsProcessorInSet(ParkedSet, Processor) ? 1 : 0;
    }

    for (Word = 0; Word < KiProcessorSetWords; Word++)
    {
        OldParked = (KAFFINITY)(ULONG_PTR)InterlockedExchangePointer((PVOID volatile *)&KiParkedProcessors[Word],
                                                                     (PVOID)(ULONG_PTR)ParkedSet[Word]);
        InterruptSet[Word] = NonParkedSet[Word] & OldParked;
    }
    MmWriteableSharedUserData->UnparkedProcessorCount = (USHORT)KiCountProcessorSet(NonParkedSet);

    KiRemoveProcessorFromSet(InterruptSet, KeGetCurrentPrcb()->Number);
    for (Found = KiFindProcessorInSet(InterruptSet, 0, &Processor);
         Found;
         Found = KiFindProcessorInSet(InterruptSet, Processor + 1, &Processor))
    {
        KiIpiSendProcessor(Processor, IPI_DPC);
    }
}

static
VOID
KiParkingResetSamples(VOID)
{
    ULONG Processor;
    BOOLEAN Found;
    PKPRCB Prcb;

    for (Found = KiFindProcessorInSet(KeActiveProcessors.Bitmap, 0, &Processor);
         Found;
         Found = KiFindProcessorInSet(KeActiveProcessors.Bitmap, Processor + 1, &Processor))
    {
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
    ULONG64 CycleTime, IdleCycleTime, Cycles, IdleCycles;
    ULONG Processor, BusyPercent;
    BOOLEAN Found;
    PKPRCB Prcb;

    *EfficientBusy = 0;
    *PerformanceBusy = 0;
    for (Found = KiFindProcessorInSet(KeActiveProcessors.Bitmap, 0, &Processor);
         Found;
         Found = KiFindProcessorInSet(KeActiveProcessors.Bitmap, Processor + 1, &Processor))
    {
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
        if (KiIsProcessorInSet(KiHeteroEfficientSet, Processor))
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
    KI_PROCESSOR_SET EfficientSet;
    ULONG64 Now;

    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT((Dpc == &KiParkingUnparkDpc) || (Dpc == &KiParkingTimerDpc));
    KiAndProcessorSets(EfficientSet, KiHeteroEfficientSet, KeActiveProcessors.Bitmap);
    EfficientCapacity = KiCountProcessorSet(EfficientSet) * 100;
    ASSERT(EfficientCapacity != 0);

    KiAcquireParkingLock();
    Now = KeQueryInterruptTime();
    ASSERT(((KiParkingContained != 0) == !KiIsProcessorSetEmpty(KiHeteroParkedSet)));
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
                DPRINT1("Unparking %lu performance processors: efficiency processors are %lu%% busy\n",
                        KiCountProcessorSet(KiHeteroParkedSet), EfficientBusy * 100 / EfficientCapacity);
                KiClearProcessorSet(KiHeteroParkedSet);
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
                DPRINT1("Parking %lu performance processors: projected load %lu%% of the efficiency processors\n",
                        KiCountProcessorSet(KiHeteroPerformanceSet),
                        (EfficientBusy + KI_PARKING_PERFORMANCE_SCALE * PerformanceBusy) * 100 / EfficientCapacity);
                KeCancelTimer(&KiParkingTimer);
                KiParkingTimerMode = KiParkingTimerIdle;
                KiParkingQuietIntervals = 0;
                KiAndProcessorSets(KiHeteroParkedSet, KiHeteroPerformanceSet, KeActiveProcessors.Bitmap);
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
    UCHAR Classes[KE_MAXIMUM_PROCESSORS];
    const KAFFINITY *ActiveSet = KeActiveProcessors.Bitmap;
    KI_PROCESSOR_SET EfficientSet, PerformanceSet;
    UCHAR LowestClass = MAXUCHAR;
    UCHAR HighestClass = 0;
    ULONG Processor;
    BOOLEAN Found;
    PKPRCB Prcb;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(!KiIsProcessorSetEmpty(ActiveSet));
    ASSERT(KiIsProcessorSetEmpty(KiHeteroEfficientSet) && KiIsProcessorSetEmpty(KiHeteroPerformanceSet));

    RtlZeroMemory(Classes, sizeof(Classes));
    KiQueryProcessorEfficiencyClasses(Classes);

    for (Found = KiFindProcessorInSet(ActiveSet, 0, &Processor);
         Found;
         Found = KiFindProcessorInSet(ActiveSet, Processor + 1, &Processor))
    {
        Prcb = KiProcessorBlock[Processor];
        ASSERT((Prcb != NULL) && (Prcb->Number == Processor));
        ASSERT(Prcb->PowerState.Parked == 0);
        Prcb->PowerState.ArchitecturalEfficiencyClass = Classes[Processor];
        LowestClass = min(LowestClass, Classes[Processor]);
        HighestClass = max(HighestClass, Classes[Processor]);
    }
    KiGetNonParkedProcessorSet(PerformanceSet);
    MmWriteableSharedUserData->UnparkedProcessorCount = (USHORT)KiCountProcessorSet(PerformanceSet);

    if (LowestClass == HighestClass)
    {
        DPRINT1("All %lu processors have efficiency class %u\n", KiCountProcessorSet(ActiveSet), LowestClass);
        return;
    }

    KiClearProcessorSet(EfficientSet);
    for (Found = KiFindProcessorInSet(ActiveSet, 0, &Processor);
         Found;
         Found = KiFindProcessorInSet(ActiveSet, Processor + 1, &Processor))
    {
        if (Classes[Processor] == LowestClass)
            KiAddProcessorToSet(EfficientSet, Processor);
    }
    Found = KiSubtractProcessorSets(PerformanceSet, ActiveSet, EfficientSet);
    ASSERT(Found && !KiIsProcessorSetEmpty(EfficientSet));

    KeInitializeDpc(&KiParkingTimerDpc, KiParkingDpcRoutine, NULL);
    KeInitializeDpc(&KiParkingUnparkDpc, KiParkingDpcRoutine, NULL);
    KeInitializeTimerEx(&KiParkingTimer, NotificationTimer);

    KiCopyProcessorSet(KiHeteroPerformanceSet, PerformanceSet);
    KeMemoryBarrier();
    KiCopyProcessorSet(KiHeteroEfficientSet, EfficientSet);
    DPRINT1("Processor efficiency classes %u to %u: %lu efficiency processors, %lu performance processors\n",
            LowestClass, HighestClass, KiCountProcessorSet(EfficientSet), KiCountProcessorSet(PerformanceSet));

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
    KI_PROCESSOR_SET Park;
    ULONG Count, Available, Index;
    KIRQL OldIrql;

    if (ParkedProcessors == NULL)
        return STATUS_INVALID_PARAMETER;

    KiClearProcessorSet(Park);
    Available = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    /* One processor always has to be left to run things */
    if (RequestedParkedProcessors >= Available)
        RequestedParkedProcessors = Available - 1;

    Count = 0;
    Index = KeProcessorCount;
    while ((Index > 0) && (Count < RequestedParkedProcessors))
    {
        Index--;
        if (!KiIsProcessorInSet(KeActiveProcessors.Bitmap, Index))
            continue;

        KiAddProcessorToSet(Park, Index);
        Count++;
    }

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    KiAcquireParkingLock();
    KiCopyProcessorSet(KiAggregatorParkedSet, Park);
    KiPublishParkedProcessors();
    KiReleaseParkingLock();
    KeLowerIrql(OldIrql);
    *ParkedProcessors = Count;

    return STATUS_SUCCESS;
}

/* The processors the power manager's core parking policy leaves without work */
VOID
NTAPI
KiSetPowerParkedProcessors(
    _In_reads_(KE_MAXIMUM_GROUPS) const KAFFINITY *ParkedSet)
{
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(ParkedSet != NULL);

    KiAcquireParkingLock();
    KiCopyProcessorSet(KiPowerParkedSet, ParkedSet);
    KiPublishParkedProcessors();
    KiReleaseParkingLock();
}

VOID
__cdecl
KeSaveStateForHibernate(
    _Out_ PKPROCESSOR_STATE State)
{
    RtlCaptureContext(&State->ContextFrame);
    KiSaveProcessorControlState(State);
}

/* EOF */
