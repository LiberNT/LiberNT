/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     dxgkrnl vsync interrupt power-saving policy tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "vsync_power_core.h"

static ULONG
PulseUntilWork(
    _Inout_ PDXGK_VSYNC_POWER_CORE Core,
    _In_ BOOLEAN PowerSaveAware,
    _In_ ULONG Limit)
{
    ULONG Pulses;

    for (Pulses = 1; Pulses <= Limit; Pulses++)
    {
        if (DxgkVsyncPowerCorePulse(Core, PowerSaveAware, TRUE))
            return Pulses;
    }
    return 0;
}

START_TEST(DxgkVsyncPower)
{
    DXGK_VSYNC_POWER_CORE Core;
    ULONGLONG Due, Now;
    ULONG Pulses;
    BOOLEAN Result;

    RtlZeroMemory(&Core, sizeof(Core));
    Pulses = PulseUntilWork(&Core, FALSE, 4 * DXGK_VSYNC_POWER_IDLE_PULSES);
    ok_eq_ulong(Pulses, 0UL);
    ok_eq_long(Core.WorkQueued, 0L);

    RtlZeroMemory(&Core, sizeof(Core));
    Pulses = PulseUntilWork(&Core, TRUE, 4 * DXGK_VSYNC_POWER_IDLE_PULSES);
    ok_eq_ulong(Pulses, (ULONG)DXGK_VSYNC_POWER_IDLE_PULSES);
    ok_eq_long(Core.WorkQueued, 1L);
    Result = DxgkVsyncPowerCorePulse(&Core, TRUE, TRUE);
    ok_eq_bool(Result, FALSE);
    Result = DxgkVsyncPowerCoreMayDisable(&Core);
    ok_eq_bool(Result, TRUE);
    DxgkVsyncPowerCoreSetSaved(&Core, TRUE);
    DxgkVsyncPowerCoreWorkDone(&Core);
    ok_eq_long(Core.Saved, 1L);
    ok_eq_long(Core.WorkQueued, 0L);
    Result = DxgkVsyncPowerCorePulse(&Core, TRUE, TRUE);
    ok_eq_bool(Result, FALSE);
    Result = DxgkVsyncPowerCoreDemand(&Core);
    ok_eq_bool(Result, TRUE);
    DxgkVsyncPowerCoreSetSaved(&Core, FALSE);
    ok_eq_long(Core.Saved, 0L);
    ok_eq_long(Core.IdlePulses, 0L);
    Result = DxgkVsyncPowerCoreDemand(&Core);
    ok_eq_bool(Result, FALSE);

    RtlZeroMemory(&Core, sizeof(Core));
    Pulses = PulseUntilWork(&Core, TRUE, DXGK_VSYNC_POWER_IDLE_PULSES - 1);
    ok_eq_ulong(Pulses, 0UL);
    Result = DxgkVsyncPowerCorePulse(&Core, TRUE, FALSE);
    ok_eq_bool(Result, FALSE);
    ok_eq_long(Core.IdlePulses, 0L);
    Pulses = PulseUntilWork(&Core, TRUE, DXGK_VSYNC_POWER_IDLE_PULSES - 1);
    ok_eq_ulong(Pulses, 0UL);
    Result = DxgkVsyncPowerCoreDemand(&Core);
    ok_eq_bool(Result, FALSE);
    ok_eq_long(Core.IdlePulses, 0L);

    RtlZeroMemory(&Core, sizeof(Core));
    Pulses = PulseUntilWork(&Core, TRUE, DXGK_VSYNC_POWER_IDLE_PULSES);
    ok_eq_ulong(Pulses, (ULONG)DXGK_VSYNC_POWER_IDLE_PULSES);
    Result = DxgkVsyncPowerCoreDemand(&Core);
    ok_eq_bool(Result, TRUE);
    Result = DxgkVsyncPowerCoreMayDisable(&Core);
    ok_eq_bool(Result, FALSE);
    DxgkVsyncPowerCoreWorkDone(&Core);
    ok_eq_long(Core.Saved, 0L);

    RtlZeroMemory(&Core, sizeof(Core));
    Pulses = PulseUntilWork(&Core, TRUE, DXGK_VSYNC_POWER_IDLE_PULSES);
    ok_eq_ulong(Pulses, (ULONG)DXGK_VSYNC_POWER_IDLE_PULSES);
    Result = DxgkVsyncPowerCorePulse(&Core, TRUE, FALSE);
    ok_eq_bool(Result, FALSE);
    Result = DxgkVsyncPowerCoreMayDisable(&Core);
    ok_eq_bool(Result, FALSE);
    DxgkVsyncPowerCoreWorkDone(&Core);

    Due = DxgkVsyncPowerCoreResumeDue(1000ULL, 1600ULL, 160000ULL);
    ok_eq_ulonglong(Due, 600ULL);
    Due = DxgkVsyncPowerCoreResumeDue(1600ULL, 1600ULL, 160000ULL);
    ok_eq_ulonglong(Due, 160000ULL);
    Due = DxgkVsyncPowerCoreResumeDue(1600ULL + 3 * 160000ULL + 5, 1600ULL, 160000ULL);
    ok_eq_ulonglong(Due, 160000ULL - 5);
    for (Now = 1600ULL; Now < 1600ULL + 5 * 160000ULL; Now += 37813ULL)
    {
        Due = DxgkVsyncPowerCoreResumeDue(Now, 1600ULL, 160000ULL);
        ok(Due != 0 && Due <= 160000ULL && (Now + Due - 1600ULL) % 160000ULL == 0,
           "resume at %I64u is due in %I64u, off the phase\n", Now, Due);
    }
}
