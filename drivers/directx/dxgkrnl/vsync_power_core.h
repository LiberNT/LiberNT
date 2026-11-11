/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Turns the vsync interrupt off while nothing waits for a vertical blank
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _DXGK_VSYNC_POWER_CORE_H_
#define _DXGK_VSYNC_POWER_CORE_H_

#include <ntddk.h>

#define DXGK_VSYNC_POWER_IDLE_PULSES 8

typedef struct _DXGK_VSYNC_POWER_CORE
{
    volatile LONG Demand;
    volatile LONG IdlePulses;
    volatile LONG WorkQueued;
    volatile LONG Saved;
    volatile LONG WorkDemand;
} DXGK_VSYNC_POWER_CORE, *PDXGK_VSYNC_POWER_CORE;

FORCEINLINE
BOOLEAN
DxgkVsyncPowerCorePulse(
    _Inout_ PDXGK_VSYNC_POWER_CORE Core,
    _In_ BOOLEAN PowerSaveAware,
    _In_ BOOLEAN Idle)
{
    if (!PowerSaveAware || !Idle)
    {
        InterlockedExchange(&Core->IdlePulses, 0);
        return FALSE;
    }
    if (InterlockedIncrement(&Core->IdlePulses) < DXGK_VSYNC_POWER_IDLE_PULSES)
        return FALSE;
    if (InterlockedCompareExchange(&Core->Saved, 0, 0) != 0 ||
        InterlockedCompareExchange(&Core->WorkQueued, 1, 0) != 0)
    {
        return FALSE;
    }
    InterlockedExchange(&Core->WorkDemand, InterlockedCompareExchange(&Core->Demand, 0, 0));
    return TRUE;
}

FORCEINLINE
BOOLEAN
DxgkVsyncPowerCoreDemand(
    _Inout_ PDXGK_VSYNC_POWER_CORE Core)
{
    InterlockedIncrement(&Core->Demand);
    InterlockedExchange(&Core->IdlePulses, 0);
    return InterlockedCompareExchange(&Core->WorkQueued, 0, 0) != 0 ||
           InterlockedCompareExchange(&Core->Saved, 0, 0) != 0;
}

FORCEINLINE
BOOLEAN
DxgkVsyncPowerCoreMayDisable(
    _In_ PDXGK_VSYNC_POWER_CORE Core)
{
    ASSERT(InterlockedCompareExchange((volatile LONG *)&Core->WorkQueued, 0, 0) != 0);
    return InterlockedCompareExchange((volatile LONG *)&Core->Saved, 0, 0) == 0 &&
           InterlockedCompareExchange((volatile LONG *)&Core->Demand, 0, 0) ==
               InterlockedCompareExchange((volatile LONG *)&Core->WorkDemand, 0, 0) &&
           InterlockedCompareExchange((volatile LONG *)&Core->IdlePulses, 0, 0) >= DXGK_VSYNC_POWER_IDLE_PULSES;
}

FORCEINLINE
VOID
DxgkVsyncPowerCoreSetSaved(
    _Inout_ PDXGK_VSYNC_POWER_CORE Core,
    _In_ BOOLEAN Saved)
{
    InterlockedExchange(&Core->Saved, Saved ? 1 : 0);
    if (!Saved)
        InterlockedExchange(&Core->IdlePulses, 0);
}

FORCEINLINE
VOID
DxgkVsyncPowerCoreWorkDone(
    _Inout_ PDXGK_VSYNC_POWER_CORE Core)
{
    LONG Previous = InterlockedExchange(&Core->WorkQueued, 0);

    ASSERT(Previous != 0);
    UNREFERENCED_PARAMETER(Previous);
}

FORCEINLINE
ULONGLONG
DxgkVsyncPowerCoreResumeDue(
    _In_ ULONGLONG Now,
    _In_ ULONGLONG Origin,
    _In_ ULONGLONG Period)
{
    ASSERT(Period != 0);
    if (Now < Origin)
        return Origin - Now;
    return Period - ((Now - Origin) % Period);
}

#endif /* _DXGK_VSYNC_POWER_CORE_H_ */
