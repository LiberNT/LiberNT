/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The 32-bit run map a file system kept before the large one
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
PLARGE_MCB
FsRtlpLargeMcbOf(
    _In_ PMCB Mcb)
{
    return &Mcb->DummyFieldThatSizesThisStructureCorrectly;
}

static
ULONG
FsRtlpNarrow(
    _In_ LONGLONG Value)
{
    return (Value < 0) ? MAXULONG : (ULONG)Value;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
FsRtlInitializeMcb(
    _Out_ PMCB Mcb,
    _In_ POOL_TYPE PoolType)
{
    FsRtlInitializeLargeMcb(FsRtlpLargeMcbOf(Mcb), PoolType);
}

VOID
NTAPI
FsRtlUninitializeMcb(
    _Inout_ PMCB Mcb)
{
    FsRtlUninitializeLargeMcb(FsRtlpLargeMcbOf(Mcb));
}

VOID
NTAPI
FsRtlTruncateMcb(
    _Inout_ PMCB Mcb,
    _In_ VBN Vbn)
{
    FsRtlTruncateLargeMcb(FsRtlpLargeMcbOf(Mcb), (LONGLONG)Vbn);
}

BOOLEAN
NTAPI
FsRtlAddMcbEntry(
    _Inout_ PMCB Mcb,
    _In_ VBN Vbn,
    _In_ LBN Lbn,
    _In_ ULONG SectorCount)
{
    return FsRtlAddLargeMcbEntry(FsRtlpLargeMcbOf(Mcb),
                                 (LONGLONG)Vbn,
                                 (LONGLONG)Lbn,
                                 (LONGLONG)SectorCount);
}

VOID
NTAPI
FsRtlRemoveMcbEntry(
    _Inout_ PMCB Mcb,
    _In_ VBN Vbn,
    _In_ ULONG SectorCount)
{
    FsRtlRemoveLargeMcbEntry(FsRtlpLargeMcbOf(Mcb),
                             (LONGLONG)Vbn,
                             (LONGLONG)SectorCount);
}

BOOLEAN
NTAPI
FsRtlLookupMcbEntry(
    _In_ PMCB Mcb,
    _In_ VBN Vbn,
    _Out_ PLBN Lbn,
    _Out_opt_ PULONG SectorCount,
    _Out_ PULONG Index)
{
    LONGLONG LargeLbn;
    LONGLONG LargeCount;
    ULONG RunIndex;

    if (!FsRtlLookupLargeMcbEntry(FsRtlpLargeMcbOf(Mcb),
                                  (LONGLONG)Vbn,
                                  &LargeLbn,
                                  &LargeCount,
                                  NULL,
                                  NULL,
                                  &RunIndex))
    {
        return FALSE;
    }

    *Lbn = FsRtlpNarrow(LargeLbn);
    if (SectorCount != NULL) *SectorCount = FsRtlpNarrow(LargeCount);
    *Index = RunIndex;

    return TRUE;
}

BOOLEAN
NTAPI
FsRtlLookupLastMcbEntry(
    _In_ PMCB Mcb,
    _Out_ PVBN Vbn,
    _Out_ PLBN Lbn)
{
    LONGLONG LargeVbn;
    LONGLONG LargeLbn;

    if (!FsRtlLookupLastLargeMcbEntry(FsRtlpLargeMcbOf(Mcb),
                                      &LargeVbn,
                                      &LargeLbn))
    {
        return FALSE;
    }

    *Vbn = FsRtlpNarrow(LargeVbn);
    *Lbn = FsRtlpNarrow(LargeLbn);

    return TRUE;
}

ULONG
NTAPI
FsRtlNumberOfRunsInMcb(
    _In_ PMCB Mcb)
{
    return FsRtlNumberOfRunsInLargeMcb(FsRtlpLargeMcbOf(Mcb));
}

BOOLEAN
NTAPI
FsRtlGetNextMcbEntry(
    _In_ PMCB Mcb,
    _In_ ULONG RunIndex,
    _Out_ PVBN Vbn,
    _Out_ PLBN Lbn,
    _Out_ PULONG SectorCount)
{
    LONGLONG LargeVbn;
    LONGLONG LargeLbn;
    LONGLONG LargeCount;

    if (!FsRtlGetNextLargeMcbEntry(FsRtlpLargeMcbOf(Mcb),
                                   RunIndex,
                                   &LargeVbn,
                                   &LargeLbn,
                                   &LargeCount))
    {
        return FALSE;
    }

    *Vbn = FsRtlpNarrow(LargeVbn);
    *Lbn = FsRtlpNarrow(LargeLbn);
    *SectorCount = FsRtlpNarrow(LargeCount);

    return TRUE;
}

/* EOF */
