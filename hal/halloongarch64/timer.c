/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Constant-timer counter and NT increment policy
 *
 * RDTIME.D reads the architecture's constant-frequency counter [V1-2.2.10.4];
 * its rate is CPUCFG4.CC_FREQ scaled by CPUCFG5.CC_MUL/CC_DIV [V1-4.2.6.5],
 * which platform.c turns into HalpLa64TimerFrequency. The counter is not
 * writable and is consistent on every core, so it needs no per-CPU offset and
 * no calibration against another source.
 */

#include <ntifs.h>
#include "halp.h"

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(
    _Out_opt_ PLARGE_INTEGER PerformanceFrequency)
{
    LARGE_INTEGER Counter;

    if ((HalpLa64InitializationPhase == 0) || (HalpLa64TimerFrequency == 0))
    {
        Counter.QuadPart = 0;
        if (PerformanceFrequency != NULL)
            PerformanceFrequency->QuadPart = 0;
        return Counter;
    }

    Counter.QuadPart = (LONGLONG)HalpLa64ReadTime();
    if (PerformanceFrequency != NULL)
        PerformanceFrequency->QuadPart = (LONGLONG)HalpLa64TimerFrequency;
    return Counter;
}

/* The clock handler credits the period that just elapsed and programs the next
 * one from the value stored here, so a change takes effect on the tick after
 * the call and never truncates the deadline that is already running. */
ULONG
NTAPI
HalSetTimeIncrement(
    _In_ ULONG Increment)
{
    if (Increment < HALP_LA64_MINIMUM_INCREMENT)
        Increment = HALP_LA64_MINIMUM_INCREMENT;
    if (Increment > HALP_LA64_MAXIMUM_INCREMENT)
        Increment = HALP_LA64_MAXIMUM_INCREMENT;
    HalpLa64CurrentTimeIncrement = Increment;
    return Increment;
}

VOID
NTAPI
KeStallExecutionProcessor(
    _In_ ULONG MicroSeconds)
{
    ULONG64 Frequency = HalpLa64TimerFrequency;
    ULONG64 Start, Ticks;

    /* No elapsed-time guarantee is possible before CPUCFG4 has been read. */
    if (Frequency == 0)
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x54494D45, 0, 0, 0);
    Ticks = ((ULONG64)MicroSeconds * Frequency + 999999) / 1000000;
    Start = HalpLa64ReadTime();
    while (HalpLa64ReadTime() - Start < Ticks)
        __asm__ __volatile__("nop");
}
