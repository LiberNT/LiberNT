/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Large and base mapped control blocks for file system drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define TAG_MCB_PAIRS 'pbcM'
#define TAG_MCB_MUTEX 'mbcM'
#define MCB_INITIAL_PAIRS 8
#define MCB_HOLE ((LONGLONG)-1)

typedef struct _MCB_PAIR
{
    LONGLONG NextVbn;
    LONGLONG Lbn;
} MCB_PAIR, *PMCB_PAIR;

static NPAGED_LOOKASIDE_LIST FsRtlpMcbMutexLookaside;

static
LONGLONG
FsRtlpMcbStart(
    _In_ PMCB_PAIR Pairs,
    _In_ ULONG Index)
{
    return Index ? Pairs[Index - 1].NextVbn : 0;
}

static
LONGLONG
FsRtlpMcbEnd(
    _In_ PBASE_MCB Mcb)
{
    return Mcb->PairCount ? ((PMCB_PAIR)Mcb->Mapping)[Mcb->PairCount - 1].NextVbn : 0;
}

static
ULONG
FsRtlpMcbFind(
    _In_ PBASE_MCB Mcb,
    _In_ LONGLONG Vbn)
{
    PMCB_PAIR Pairs = Mcb->Mapping;
    ULONG Low = 0;
    ULONG High = Mcb->PairCount;
    ULONG Middle;

    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        if (Pairs[Middle].NextVbn > Vbn)
            High = Middle;
        else
            Low = Middle + 1;
    }

    return Low;
}

static
LONGLONG
FsRtlpMcbShiftLbn(
    _In_ LONGLONG Lbn,
    _In_ LONGLONG Offset)
{
    return Lbn == MCB_HOLE ? MCB_HOLE : Lbn + Offset;
}

static
BOOLEAN
FsRtlpMcbReserve(
    _Inout_ PBASE_MCB Mcb,
    _In_ ULONG Extra)
{
    PMCB_PAIR Pairs;
    ULONG Needed;
    ULONG Capacity;

    if (Mcb->PairCount > MAXULONG - Extra)
        goto Failure;

    Needed = Mcb->PairCount + Extra;
    if (Needed <= Mcb->MaximumPairCount)
        return TRUE;

    Capacity = max(Mcb->MaximumPairCount, MCB_INITIAL_PAIRS);
    while (Capacity < Needed)
    {
        if (Capacity > MAXULONG / 2)
            goto Failure;
        Capacity *= 2;
    }

    if (Capacity > MAXULONG / sizeof(MCB_PAIR))
        goto Failure;

    Pairs = ExAllocatePoolWithTag((POOL_TYPE)Mcb->PoolType, Capacity * sizeof(MCB_PAIR), TAG_MCB_PAIRS);
    if (Pairs == NULL)
        goto Failure;

    if (Mcb->PairCount)
        RtlCopyMemory(Pairs, Mcb->Mapping, Mcb->PairCount * sizeof(MCB_PAIR));
    if (Mcb->Mapping)
        ExFreePoolWithTag(Mcb->Mapping, TAG_MCB_PAIRS);

    Mcb->Mapping = Pairs;
    Mcb->MaximumPairCount = Capacity;
    return TRUE;

Failure:
    if (Mcb->Flags & MCB_FLAG_RAISE_ON_ALLOCATION_FAILURE)
        ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
    return FALSE;
}

static
VOID
FsRtlpMcbNormalize(
    _Inout_ PBASE_MCB Mcb)
{
    PMCB_PAIR Pairs = Mcb->Mapping;
    ULONG Read;
    ULONG Write = 0;
    LONGLONG Start = 0;
    LONGLONG PreviousStart = 0;

    for (Read = 0; Read < Mcb->PairCount; Read++)
    {
        MCB_PAIR Current = Pairs[Read];

        if (Current.NextVbn <= Start)
            continue;

        if (Write != 0)
        {
            PMCB_PAIR Previous = &Pairs[Write - 1];

            if ((Previous->Lbn == MCB_HOLE && Current.Lbn == MCB_HOLE) ||
                (Previous->Lbn != MCB_HOLE && Current.Lbn != MCB_HOLE &&
                 Previous->Lbn + (Previous->NextVbn - PreviousStart) == Current.Lbn))
            {
                Previous->NextVbn = Current.NextVbn;
                Start = Current.NextVbn;
                continue;
            }
        }

        PreviousStart = Start;
        Pairs[Write++] = Current;
        Start = Current.NextVbn;
    }

    while (Write != 0 && Pairs[Write - 1].Lbn == MCB_HOLE)
        Write--;

    Mcb->PairCount = Write;
}

static
VOID
FsRtlpMcbReplace(
    _Inout_ PBASE_MCB Mcb,
    _In_ LONGLONG First,
    _In_ LONGLONG Last,
    _In_ LONGLONG Lbn)
{
    PMCB_PAIR Pairs = Mcb->Mapping;
    ULONG Count = Mcb->PairCount;
    LONGLONG End = FsRtlpMcbEnd(Mcb);
    ULONG FirstIndex = FsRtlpMcbFind(Mcb, First);
    ULONG LastIndex = FsRtlpMcbFind(Mcb, Last);
    BOOLEAN Prefix = FirstIndex < Count && FsRtlpMcbStart(Pairs, FirstIndex) < First;
    BOOLEAN Gap = First > End;
    ULONG Tail = FirstIndex + (Prefix ? 1 : 0) + (Gap ? 1 : 0) + 1;
    LONGLONG PrefixLbn = Prefix ? Pairs[FirstIndex].Lbn : MCB_HOLE;
    LONGLONG LastStart = LastIndex < Count ? FsRtlpMcbStart(Pairs, LastIndex) : 0;
    ULONG Write = FirstIndex;

    if (LastIndex < Count)
    {
        RtlMoveMemory(&Pairs[Tail], &Pairs[LastIndex], (Count - LastIndex) * sizeof(MCB_PAIR));
        Pairs[Tail].Lbn = FsRtlpMcbShiftLbn(Pairs[Tail].Lbn, Last - LastStart);
    }

    if (Prefix)
    {
        Pairs[Write].NextVbn = First;
        Pairs[Write].Lbn = PrefixLbn;
        Write++;
    }

    if (Gap)
    {
        Pairs[Write].NextVbn = First;
        Pairs[Write].Lbn = MCB_HOLE;
        Write++;
    }

    Pairs[Write].NextVbn = Last;
    Pairs[Write].Lbn = Lbn;

    Mcb->PairCount = Tail + (LastIndex < Count ? Count - LastIndex : 0);
    FsRtlpMcbNormalize(Mcb);
}

VOID
NTAPI
FsRtlInitializeBaseMcb(
    _Out_ PBASE_MCB Mcb,
    _In_ POOL_TYPE PoolType)
{
    Mcb->MaximumPairCount = 0;
    Mcb->PairCount = 0;
    Mcb->PoolType = (USHORT)PoolType;
    Mcb->Flags = MCB_FLAG_RAISE_ON_ALLOCATION_FAILURE;
    Mcb->Mapping = NULL;
}

VOID
NTAPI
FsRtlUninitializeBaseMcb(
    _In_ PBASE_MCB Mcb)
{
    if (Mcb->Mapping)
        ExFreePoolWithTag(Mcb->Mapping, TAG_MCB_PAIRS);

    Mcb->Mapping = NULL;
    Mcb->MaximumPairCount = 0;
    Mcb->PairCount = 0;
}

VOID
NTAPI
FsRtlResetBaseMcb(
    _Out_ PBASE_MCB Mcb)
{
    Mcb->PairCount = 0;
}

BOOLEAN
NTAPI
FsRtlAddBaseMcbEntry(
    _Inout_ PBASE_MCB Mcb,
    _In_ LONGLONG Vbn,
    _In_ LONGLONG Lbn,
    _In_ LONGLONG SectorCount)
{
    PMCB_PAIR Pairs;
    ULONG Index;
    LONGLONG Last;

    if (SectorCount == 0)
        return TRUE;

    if (Vbn < 0 || Lbn < 0 || SectorCount < 0 ||
        Vbn > MAXLONGLONG - SectorCount || Lbn > MAXLONGLONG - SectorCount)
    {
        return FALSE;
    }

    Last = Vbn + SectorCount;
    Pairs = Mcb->Mapping;
    for (Index = FsRtlpMcbFind(Mcb, Vbn); Index < Mcb->PairCount; Index++)
    {
        LONGLONG Start = FsRtlpMcbStart(Pairs, Index);

        if (Start >= Last)
            break;

        if (Pairs[Index].Lbn != MCB_HOLE && Pairs[Index].Lbn - Start != Lbn - Vbn)
            return FALSE;
    }

    if (!FsRtlpMcbReserve(Mcb, 2))
        return FALSE;

    FsRtlpMcbReplace(Mcb, Vbn, Last, Lbn);
    return TRUE;
}

BOOLEAN
NTAPI
FsRtlRemoveBaseMcbEntry(
    _Inout_ PBASE_MCB Mcb,
    _In_ LONGLONG Vbn,
    _In_ LONGLONG SectorCount)
{
    LONGLONG End = FsRtlpMcbEnd(Mcb);
    LONGLONG Last;

    if (Vbn < 0 || SectorCount <= 0 || Vbn >= End)
        return TRUE;

    Last = SectorCount > End - Vbn ? End : Vbn + SectorCount;

    if (!FsRtlpMcbReserve(Mcb, 2))
        return FALSE;

    FsRtlpMcbReplace(Mcb, Vbn, Last, MCB_HOLE);
    return TRUE;
}

VOID
NTAPI
FsRtlTruncateBaseMcb(
    _Inout_ PBASE_MCB Mcb,
    _In_ LONGLONG Vbn)
{
    PMCB_PAIR Pairs = Mcb->Mapping;
    ULONG Index;

    if (Vbn <= 0)
    {
        Mcb->PairCount = 0;
        return;
    }

    Index = FsRtlpMcbFind(Mcb, Vbn);
    if (Index == Mcb->PairCount)
        return;

    if (FsRtlpMcbStart(Pairs, Index) < Vbn)
    {
        Pairs[Index].NextVbn = Vbn;
        Index++;
    }

    Mcb->PairCount = Index;
    FsRtlpMcbNormalize(Mcb);
}

BOOLEAN
NTAPI
FsRtlSplitBaseMcb(
    _Inout_ PBASE_MCB Mcb,
    _In_ LONGLONG Vbn,
    _In_ LONGLONG Amount)
{
    PMCB_PAIR Pairs;
    LONGLONG End = FsRtlpMcbEnd(Mcb);
    LONGLONG Start;
    ULONG Index;
    ULONG Shift;

    if (Amount == 0 || Vbn >= End)
        return TRUE;

    if (Vbn < 0 || Amount < 0 || End > MAXLONGLONG - Amount)
        return FALSE;

    if (!FsRtlpMcbReserve(Mcb, 2))
        return FALSE;

    Pairs = Mcb->Mapping;
    Index = FsRtlpMcbFind(Mcb, Vbn);
    Start = FsRtlpMcbStart(Pairs, Index);

    if (Start < Vbn)
    {
        RtlMoveMemory(&Pairs[Index + 1], &Pairs[Index], (Mcb->PairCount - Index) * sizeof(MCB_PAIR));
        Pairs[Index].NextVbn = Vbn;
        Pairs[Index + 1].Lbn = FsRtlpMcbShiftLbn(Pairs[Index + 1].Lbn, Vbn - Start);
        Mcb->PairCount++;
        Index++;
    }

    RtlMoveMemory(&Pairs[Index + 1], &Pairs[Index], (Mcb->PairCount - Index) * sizeof(MCB_PAIR));
    Pairs[Index].NextVbn = Vbn;
    Pairs[Index].Lbn = MCB_HOLE;
    Mcb->PairCount++;

    for (Shift = Index; Shift < Mcb->PairCount; Shift++)
        Pairs[Shift].NextVbn += Amount;

    FsRtlpMcbNormalize(Mcb);
    return TRUE;
}

BOOLEAN
NTAPI
FsRtlLookupBaseMcbEntry(
    _In_ PBASE_MCB Mcb,
    _In_ LONGLONG Vbn,
    _Out_opt_ PLONGLONG Lbn,
    _Out_opt_ PLONGLONG SectorCountFromLbn,
    _Out_opt_ PLONGLONG StartingLbn,
    _Out_opt_ PLONGLONG SectorCountFromStartingLbn,
    _Out_opt_ PULONG Index)
{
    PMCB_PAIR Pairs = Mcb->Mapping;
    LONGLONG Start;
    ULONG Run;

    if (Vbn < 0)
        return FALSE;

    Run = FsRtlpMcbFind(Mcb, Vbn);
    if (Run == Mcb->PairCount)
        return FALSE;

    Start = FsRtlpMcbStart(Pairs, Run);

    if (Lbn)
        *Lbn = FsRtlpMcbShiftLbn(Pairs[Run].Lbn, Vbn - Start);
    if (SectorCountFromLbn)
        *SectorCountFromLbn = Pairs[Run].NextVbn - Vbn;
    if (StartingLbn)
        *StartingLbn = Pairs[Run].Lbn;
    if (SectorCountFromStartingLbn)
        *SectorCountFromStartingLbn = Pairs[Run].NextVbn - Start;
    if (Index)
        *Index = Run;

    return TRUE;
}

BOOLEAN
NTAPI
FsRtlLookupLastBaseMcbEntryAndIndex(
    _In_ PBASE_MCB OpaqueMcb,
    _Inout_ PLONGLONG LargeVbn,
    _Inout_ PLONGLONG LargeLbn,
    _Inout_ PULONG Index)
{
    PMCB_PAIR Pairs = OpaqueMcb->Mapping;
    ULONG Last;

    if (OpaqueMcb->PairCount == 0)
        return FALSE;

    Last = OpaqueMcb->PairCount - 1;
    *LargeVbn = Pairs[Last].NextVbn - 1;
    *LargeLbn = FsRtlpMcbShiftLbn(Pairs[Last].Lbn, Pairs[Last].NextVbn - FsRtlpMcbStart(Pairs, Last) - 1);
    *Index = Last;
    return TRUE;
}

BOOLEAN
NTAPI
FsRtlLookupLastBaseMcbEntry(
    _In_ PBASE_MCB Mcb,
    _Out_ PLONGLONG Vbn,
    _Out_ PLONGLONG Lbn)
{
    ULONG Index;

    return FsRtlLookupLastBaseMcbEntryAndIndex(Mcb, Vbn, Lbn, &Index);
}

ULONG
NTAPI
FsRtlNumberOfRunsInBaseMcb(
    _In_ PBASE_MCB Mcb)
{
    return Mcb->PairCount;
}

BOOLEAN
NTAPI
FsRtlGetNextBaseMcbEntry(
    _In_ PBASE_MCB Mcb,
    _In_ ULONG RunIndex,
    _Out_ PLONGLONG Vbn,
    _Out_ PLONGLONG Lbn,
    _Out_ PLONGLONG SectorCount)
{
    PMCB_PAIR Pairs = Mcb->Mapping;
    LONGLONG Start;

    if (RunIndex >= Mcb->PairCount)
        return FALSE;

    Start = FsRtlpMcbStart(Pairs, RunIndex);
    *Vbn = Start;
    *Lbn = Pairs[RunIndex].Lbn;
    *SectorCount = Pairs[RunIndex].NextVbn - Start;
    return TRUE;
}

CODE_SEG("INIT")
VOID
NTAPI
FsRtlInitializeLargeMcbs(VOID)
{
    ExInitializeNPagedLookasideList(&FsRtlpMcbMutexLookaside,
                                    NULL,
                                    NULL,
                                    0,
                                    sizeof(KGUARDED_MUTEX),
                                    TAG_MCB_MUTEX,
                                    0);
}

VOID
NTAPI
FsRtlInitializeLargeMcb(
    _Out_ PLARGE_MCB Mcb,
    _In_ POOL_TYPE PoolType)
{
    FsRtlInitializeBaseMcb(&Mcb->BaseMcb, PoolType);

    Mcb->GuardedMutex = ExAllocateFromNPagedLookasideList(&FsRtlpMcbMutexLookaside);
    if (Mcb->GuardedMutex == NULL)
        ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);

    KeInitializeGuardedMutex(Mcb->GuardedMutex);
}

VOID
NTAPI
FsRtlUninitializeLargeMcb(
    _Inout_ PLARGE_MCB Mcb)
{
    FsRtlUninitializeBaseMcb(&Mcb->BaseMcb);

    if (Mcb->GuardedMutex)
    {
        ExFreeToNPagedLookasideList(&FsRtlpMcbMutexLookaside, Mcb->GuardedMutex);
        Mcb->GuardedMutex = NULL;
    }
}

VOID
NTAPI
FsRtlResetLargeMcb(
    _Inout_ PLARGE_MCB Mcb,
    _In_ BOOLEAN SelfSynchronized)
{
    if (!SelfSynchronized)
        KeAcquireGuardedMutex(Mcb->GuardedMutex);

    FsRtlResetBaseMcb(&Mcb->BaseMcb);

    if (!SelfSynchronized)
        KeReleaseGuardedMutex(Mcb->GuardedMutex);
}

VOID
NTAPI
FsRtlTruncateLargeMcb(
    _Inout_ PLARGE_MCB Mcb,
    _In_ LONGLONG Vbn)
{
    KeAcquireGuardedMutex(Mcb->GuardedMutex);
    FsRtlTruncateBaseMcb(&Mcb->BaseMcb, Vbn);
    KeReleaseGuardedMutex(Mcb->GuardedMutex);
}

BOOLEAN
NTAPI
FsRtlAddLargeMcbEntry(
    _Inout_ PLARGE_MCB Mcb,
    _In_ LONGLONG Vbn,
    _In_ LONGLONG Lbn,
    _In_ LONGLONG SectorCount)
{
    BOOLEAN Result = FALSE;

    KeAcquireGuardedMutex(Mcb->GuardedMutex);
    _SEH2_TRY
    {
        Result = FsRtlAddBaseMcbEntry(&Mcb->BaseMcb, Vbn, Lbn, SectorCount);
    }
    _SEH2_FINALLY
    {
        KeReleaseGuardedMutex(Mcb->GuardedMutex);
    }
    _SEH2_END;

    return Result;
}

VOID
NTAPI
FsRtlRemoveLargeMcbEntry(
    _Inout_ PLARGE_MCB Mcb,
    _In_ LONGLONG Vbn,
    _In_ LONGLONG SectorCount)
{
    KeAcquireGuardedMutex(Mcb->GuardedMutex);
    _SEH2_TRY
    {
        FsRtlRemoveBaseMcbEntry(&Mcb->BaseMcb, Vbn, SectorCount);
    }
    _SEH2_FINALLY
    {
        KeReleaseGuardedMutex(Mcb->GuardedMutex);
    }
    _SEH2_END;
}

BOOLEAN
NTAPI
FsRtlSplitLargeMcb(
    _Inout_ PLARGE_MCB Mcb,
    _In_ LONGLONG Vbn,
    _In_ LONGLONG Amount)
{
    BOOLEAN Result = FALSE;

    KeAcquireGuardedMutex(Mcb->GuardedMutex);
    _SEH2_TRY
    {
        Result = FsRtlSplitBaseMcb(&Mcb->BaseMcb, Vbn, Amount);
    }
    _SEH2_FINALLY
    {
        KeReleaseGuardedMutex(Mcb->GuardedMutex);
    }
    _SEH2_END;

    return Result;
}

BOOLEAN
NTAPI
FsRtlLookupLargeMcbEntry(
    _In_ PLARGE_MCB Mcb,
    _In_ LONGLONG Vbn,
    _Out_opt_ PLONGLONG Lbn,
    _Out_opt_ PLONGLONG SectorCountFromLbn,
    _Out_opt_ PLONGLONG StartingLbn,
    _Out_opt_ PLONGLONG SectorCountFromStartingLbn,
    _Out_opt_ PULONG Index)
{
    BOOLEAN Result;

    KeAcquireGuardedMutex(Mcb->GuardedMutex);
    Result = FsRtlLookupBaseMcbEntry(&Mcb->BaseMcb, Vbn, Lbn, SectorCountFromLbn,
                                     StartingLbn, SectorCountFromStartingLbn, Index);
    KeReleaseGuardedMutex(Mcb->GuardedMutex);

    return Result;
}

BOOLEAN
NTAPI
FsRtlLookupLastLargeMcbEntryAndIndex(
    _In_ PLARGE_MCB OpaqueMcb,
    _Out_ PLONGLONG LargeVbn,
    _Out_ PLONGLONG LargeLbn,
    _Out_ PULONG Index)
{
    BOOLEAN Result;

    KeAcquireGuardedMutex(OpaqueMcb->GuardedMutex);
    Result = FsRtlLookupLastBaseMcbEntryAndIndex(&OpaqueMcb->BaseMcb, LargeVbn, LargeLbn, Index);
    KeReleaseGuardedMutex(OpaqueMcb->GuardedMutex);

    return Result;
}

BOOLEAN
NTAPI
FsRtlLookupLastLargeMcbEntry(
    _In_ PLARGE_MCB Mcb,
    _Out_ PLONGLONG Vbn,
    _Out_ PLONGLONG Lbn)
{
    BOOLEAN Result;

    KeAcquireGuardedMutex(Mcb->GuardedMutex);
    Result = FsRtlLookupLastBaseMcbEntry(&Mcb->BaseMcb, Vbn, Lbn);
    KeReleaseGuardedMutex(Mcb->GuardedMutex);

    return Result;
}

ULONG
NTAPI
FsRtlNumberOfRunsInLargeMcb(
    _In_ PLARGE_MCB Mcb)
{
    ULONG Runs;

    KeAcquireGuardedMutex(Mcb->GuardedMutex);
    Runs = FsRtlNumberOfRunsInBaseMcb(&Mcb->BaseMcb);
    KeReleaseGuardedMutex(Mcb->GuardedMutex);

    return Runs;
}

BOOLEAN
NTAPI
FsRtlGetNextLargeMcbEntry(
    _In_ PLARGE_MCB Mcb,
    _In_ ULONG RunIndex,
    _Out_ PLONGLONG Vbn,
    _Out_ PLONGLONG Lbn,
    _Out_ PLONGLONG SectorCount)
{
    BOOLEAN Result;

    KeAcquireGuardedMutex(Mcb->GuardedMutex);
    Result = FsRtlGetNextBaseMcbEntry(&Mcb->BaseMcb, RunIndex, Vbn, Lbn, SectorCount);
    KeReleaseGuardedMutex(Mcb->GuardedMutex);

    return Result;
}
