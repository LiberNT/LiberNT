/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Core parking decisions of the processor power manager
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define POP_PPM_PERCENT             100

typedef struct _POP_PPM_PARKING_INPUT
{
    ULONG Count;
    ULONG Unparked;
    ULONG Load;
    ULONG Concurrent;
    ULONG OverUtilized;
    ULONG MinCores;
    ULONG MaxCores;
    ULONG Headroom;
} POP_PPM_PARKING_INPUT, *PPOP_PPM_PARKING_INPUT;

FORCEINLINE
ULONG
PopPpmParkingBound(
    _In_ ULONG Count,
    _In_ ULONG Percent)
{
    return (ULONG)(((ULONGLONG)Count * min(Percent, (ULONG)POP_PPM_PERCENT) + POP_PPM_PERCENT - 1) / POP_PPM_PERCENT);
}

/* FIXME: the processors a class keeps unparked follow the documented settings, not Windows 11's own computation */
FORCEINLINE
ULONG
PopPpmParkingTarget(
    _In_ const POP_PPM_PARKING_INPUT *Input)
{
    ULONG Minimum = PopPpmParkingBound(Input->Count, Input->MinCores);
    ULONG Maximum = max(PopPpmParkingBound(Input->Count, Input->MaxCores), Minimum);
    ULONG Usable = POP_PPM_PERCENT - min(Input->Headroom, (ULONG)POP_PPM_PERCENT - 1);
    ULONG Needed = (Input->Load + Usable - 1) / Usable;

    Needed = max(Needed, Input->Concurrent);
    if (Input->OverUtilized != 0)
        Needed = max(Needed, Input->Unparked + 1);

    return min(max(Needed, Minimum), Maximum);
}

FORCEINLINE
ULONG
PopPpmParkingStep(
    _In_ ULONG Unparked,
    _In_ ULONG Target,
    _In_ ULONG Minimum,
    _In_ ULONG Maximum,
    _In_ ULONG Policy)
{
    switch (Policy)
    {
        case CORE_PARKING_POLICY_CHANGE_SINGLE:
        case CORE_PARKING_POLICY_CHANGE_MULTISTEP:
            /* FIXME: the multistep policy moves one processor at a time like the single one */
            return (Target > Unparked) ? Unparked + 1 : Unparked - 1;

        case CORE_PARKING_POLICY_CHANGE_ROCKET:
            return (Target > Unparked) ? Maximum : Minimum;

        default:
            return Target;
    }
}
