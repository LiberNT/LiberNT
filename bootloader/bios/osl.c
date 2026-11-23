/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PC/AT BIOS hand-over to the operating system and hibernation resume memory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../winload/winload.h"
#include <bios.h>

static
ULONG64
BiosHiberOverlap(
    _In_reads_(RangeCount) PLNT_HIBER_RANGE Ranges,
    _In_ ULONG64 RangeCount,
    _In_ ULONG64 BasePage,
    _In_ ULONG64 PageCount,
    _In_ BOOLEAN Claim)
{
    ULONG64 End = BasePage + PageCount;
    ULONG64 Low = 0, High = RangeCount, Middle;
    ULONG64 Start, Stop, Total = 0;

    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        if (Ranges[Middle].BasePage + Ranges[Middle].PageCount <= BasePage)
            Low = Middle + 1;
        else
            High = Middle;
    }

    for (; Low < RangeCount && Ranges[Low].BasePage < End; Low++)
    {
        Start = max(Ranges[Low].BasePage, BasePage);
        Stop = min(Ranges[Low].BasePage + Ranges[Low].PageCount, End);
        if (Start >= Stop)
            continue;

        if (Claim)
        {
            if (!FwAllocatePagesAt((PVOID)(ULONG_PTR)(Start << PAGE_SHIFT), (SIZE_T)((Stop - Start) << PAGE_SHIFT)))
                continue;
            OslHiberMarkOwned(Start, Stop - Start);
        }

        Total += Stop - Start;
    }

    return Total;
}

BOOLEAN
FwHiberCheckMemoryMap(
    _In_ PLNT_HIBER_HEADER Header,
    _In_reads_(RangeCount) PLNT_HIBER_RANGE Ranges,
    _In_ ULONG64 RangeCount,
    _Out_ PULONG64 HighestPage)
{
    PFN_NUMBER BasePage, PageCount;
    ULONG64 Covered = 0;
    ULONG Index;

    *HighestPage = 0;

    if (Header->RuntimeRangeCount != 0)
        return FALSE;

    for (Index = 0; BiosGetUsableRun(Index, &BasePage, &PageCount); Index++)
    {
        Covered += BiosHiberOverlap(Ranges, RangeCount, BasePage, PageCount, FALSE);
        *HighestPage = max(*HighestPage, (ULONG64)BasePage + PageCount);
    }

    return Covered == Header->TargetPages;
}

VOID
FwHiberClaimFreeMemory(
    _In_reads_(RangeCount) PLNT_HIBER_RANGE Ranges,
    _In_ ULONG64 RangeCount)
{
    PFN_NUMBER Position = 0, BasePage, PageCount;
    ULONG Cursor = 0;

    while (BiosGetFreeRun(&Cursor, &Position, &BasePage, &PageCount))
        BiosHiberOverlap(Ranges, RangeCount, BasePage, PageCount, TRUE);
}

VOID
FwHiberClaimPool(
    _In_ ULONG64 PageCount)
{
    PFN_NUMBER Position = 0, BasePage, Count;
    ULONG Cursor = 0;

    while (PageCount != 0 && BiosGetFreeRun(&Cursor, &Position, &BasePage, &Count))
    {
        if (BasePage == 0)
        {
            BasePage++;
            Count--;
        }

        Count = (PFN_NUMBER)min((ULONG64)Count, PageCount);
        if (Count == 0 ||
            !FwAllocatePagesAt((PVOID)(ULONG_PTR)((ULONG64)BasePage << PAGE_SHIFT), (SIZE_T)Count << PAGE_SHIFT))
        {
            continue;
        }

        OslHiberAddPoolRun(BasePage, Count);
        PageCount -= Count;
    }
}

BOOLEAN
FwHiberExitBootServices(
    _In_ PLNT_HIBER_HEADER Header)
{
    UNREFERENCED_PARAMETER(Header);
    FwExitBootServices();
    return TRUE;
}

VOID
FwExitBootServices(VOID)
{
    _disable();
}

VOID
FwSetVirtualAddressMap(VOID)
{
}

BOOLEAN
FwPrepareRuntimeServices(
    _Inout_ struct _EFI_FIRMWARE_INFORMATION* Information,
    _Inout_ struct _VIRTUAL_EFI_RUNTIME_SERVICES* Services)
{
    UNREFERENCED_PARAMETER(Information);
    UNREFERENCED_PARAMETER(Services);
    return FALSE;
}
