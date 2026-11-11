/*
 * PROJECT:     LiberNT Apple PWM Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Apple silicon PWM controller output, ported from Asahi Linux pwm-apple.c (GPL-2.0 OR MIT)
 * COPYRIGHT:   Copyright The Asahi Linux Contributors
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include "fpwm.h"

#define FPWM_CONTROL                0x00
#define FPWM_OFF_CYCLES             0x18
#define FPWM_ON_CYCLES              0x1C
#define FPWM_CONTROL_ENABLE         0x00000001
#define FPWM_CONTROL_UPDATE         0x00000020
#define FPWM_CONTROL_OUTPUT_ENABLE  0x00004000

static
ULONG64
FpwmDivideRoundUp(
    _In_ ULONG64 Value,
    _In_ ULONG64 Divisor)
{
    ASSERT(Divisor);
    return (Value + Divisor - 1) / Divisor;
}

VOID
FpwmReadOutput(
    _In_ PUCHAR Base,
    _In_ ULONG64 ClockHz,
    _Out_ PFPWM_OUTPUT Output)
{
    ASSERT(ClockHz && ClockHz <= FPWM_NS_PER_SECOND);
    Output->Control = READ_REGISTER_ULONG((volatile ULONG *)(Base + FPWM_CONTROL));
    Output->OnCycles = READ_REGISTER_ULONG((volatile ULONG *)(Base + FPWM_ON_CYCLES));
    Output->OffCycles = READ_REGISTER_ULONG((volatile ULONG *)(Base + FPWM_OFF_CYCLES));
    Output->Enabled = (Output->Control & FPWM_CONTROL_ENABLE) && (Output->Control & FPWM_CONTROL_OUTPUT_ENABLE);
    Output->DutyNs = FpwmDivideRoundUp((ULONG64)Output->OnCycles * FPWM_NS_PER_SECOND, ClockHz);
    Output->PeriodNs = FpwmDivideRoundUp(((ULONG64)Output->OffCycles + Output->OnCycles) * FPWM_NS_PER_SECOND,
                                         ClockHz);
}

VOID
FpwmApply(
    _In_ PUCHAR Base,
    _In_ ULONG64 ClockHz,
    _In_ BOOLEAN Enable,
    _In_ ULONG64 PeriodNs,
    _In_ ULONG64 DutyNs)
{
    ULONG64 OnCycles, TotalCycles, OffCycles;

    ASSERT(ClockHz && ClockHz <= FPWM_NS_PER_SECOND);
    ASSERT(PeriodNs <= MAXULONG && DutyNs <= PeriodNs);
    if (!Enable)
    {
        WRITE_REGISTER_ULONG((volatile ULONG *)(Base + FPWM_CONTROL), 0);
        return;
    }
    OnCycles = ClockHz * DutyNs / FPWM_NS_PER_SECOND;
    TotalCycles = ClockHz * PeriodNs / FPWM_NS_PER_SECOND;
    ASSERT(OnCycles <= TotalCycles);
    OffCycles = TotalCycles - OnCycles;
    if (OnCycles > FPWM_MAX_CYCLES)
        OnCycles = FPWM_MAX_CYCLES;
    if (OffCycles > FPWM_MAX_CYCLES)
        OffCycles = FPWM_MAX_CYCLES;
    WRITE_REGISTER_ULONG((volatile ULONG *)(Base + FPWM_ON_CYCLES), (ULONG)OnCycles);
    WRITE_REGISTER_ULONG((volatile ULONG *)(Base + FPWM_OFF_CYCLES), (ULONG)OffCycles);
    WRITE_REGISTER_ULONG((volatile ULONG *)(Base + FPWM_CONTROL),
                         FPWM_CONTROL_ENABLE | FPWM_CONTROL_OUTPUT_ENABLE | FPWM_CONTROL_UPDATE);
}
