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
        ProcNumber->Group = Prcb->Group;
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
    ULONG Number;

    Number = KeGetCurrentProcessorNumberEx(&Local);

    if (ProcNumber == NULL)
        return (NTSTATUS)Number;

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
    else
    {
        *ProcNumber = Local;
    }

    return (NTSTATUS)Number;
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

    KiParkedProcessors = Park;
    *ParkedProcessors = Count;

    return STATUS_SUCCESS;
}

/* EOF */
