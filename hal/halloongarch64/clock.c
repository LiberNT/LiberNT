/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Timer line (ESTAT.IS[11]) clock tick and ProfileTime sampling
 *
 * The core timer counts down TVAL at the constant timer rate and raises the TI
 * line when it reaches zero [V1-7.6.2]. In periodic mode it reloads InitVal
 * itself, so the handler does not reprogram a deadline on every tick; it only
 * clears the pending TI through TICLR.TI [V1-7.6.5], which is the only way to
 * drop the level-triggered line.
 */

#include <ntifs.h>
#include "halp.h"

typedef struct _HALP_LA64_CLOCK
{
    ULONG64 Period;         /* ticks programmed into TCFG.InitVal */
    ULONG Increment;        /* 100 ns units credited per tick */
    ULONG RunTimeElapsed;   /* non-boot CPUs accumulate to one increment */
    ULONG64 ProfileElapsed;
    BOOLEAN Suspended;
    ULONG64 SuspendedAt;
} HALP_LA64_CLOCK;

static HALP_LA64_CLOCK HalpLa64Clocks[MAXIMUM_PROCESSORS];
static BOOLEAN HalpProfileEnabled;
ULONG HalpLa64ProfileInterval = HALP_LA64_DEFAULT_PROFILE_INTERVAL;

/* 100 ns units to timer ticks, rounded up and then up again to the multiple of
 * four TCFG.InitVal requires [V1-7.6.2]. */
static
ULONG64
HalpLa64IncrementToTicks(
    _In_ ULONG Increment)
{
    ULONG64 Ticks = ((ULONG64)Increment * HalpLa64TimerFrequency + 9999999ULL) / 10000000ULL;

    if (Ticks < 4)
        Ticks = 4;
    return (Ticks + 3) & ~(ULONG64)3;
}

static
VOID
HalpLa64ClearTimerInterrupt(VOID)
{
    __asm__ __volatile__("csrwr %0, %1"
                         :
                         : "r"((ULONG)HALP_LA64_TICLR_CLR),
                           "i"(HALP_LA64_CSR_TICLR)
                         : "memory");
}

static
VOID
HalpLa64ProgramTimer(
    _In_ ULONG64 Period)
{
    ULONG Value = (ULONG)(Period | HALP_LA64_TCFG_EN | HALP_LA64_TCFG_PERIODIC);

    /* TCFG [V1-7.6.2]: En 0, Periodic 1, InitVal n-1:2. The hardware appends
     * the two low zero bits the field does not store, so a 4-aligned period
     * written here is the countdown the timer uses. */
    __asm__ __volatile__("csrwr %0, %1"
                         :
                         : "r"(Value), "i"(HALP_LA64_CSR_TCFG)
                         : "memory");
}

VOID
HalpLa64StartClock(VOID)
{
    HALP_LA64_CLOCK *Clock = &HalpLa64Clocks[KeGetCurrentProcessorNumber()];

    Clock->Increment = HalpLa64CurrentTimeIncrement;
    Clock->Period = HalpLa64IncrementToTicks(Clock->Increment);
    HalpLa64ClearTimerInterrupt();
    HalpLa64ProgramTimer(Clock->Period);
}

VOID
NTAPI
HalpLa64SuspendClockTick(VOID)
{
    ULONG Number = KeGetCurrentProcessorNumber();
    HALP_LA64_CLOCK *Clock = &HalpLa64Clocks[Number];

    /* The boot CPU's tick is the system time source and may not stop. */
    if ((Number == 0) || Clock->Suspended || HalpProfileEnabled)
        return;
    Clock->Suspended = TRUE;
    Clock->SuspendedAt = HalpLa64ReadTime();
    HalpLa64ClearTimerInterrupt();
    __asm__ __volatile__("csrwr %0, %1"
                         :
                         : "r"((ULONG)0), "i"(HALP_LA64_CSR_TCFG)
                         : "memory");
}

ULONG
NTAPI
HalpLa64ResumeClockTick(VOID)
{
    ULONG Number = KeGetCurrentProcessorNumber();
    HALP_LA64_CLOCK *Clock = &HalpLa64Clocks[Number];
    ULONG64 Skipped = 0;

    if (!Clock->Suspended)
        return 0;
    Clock->Suspended = FALSE;
    /* The countdown was stopped, so restarting it is always a full period.
     * The ticks the core slept through are reported to the caller from the
     * stable counter, which kept running. */
    if (Clock->Period)
    {
        ULONG64 Now = HalpLa64ReadTime();

        if (Now > Clock->SuspendedAt)
            Skipped = (Now - Clock->SuspendedAt) / Clock->Period;
    }
    HalpLa64ClearTimerInterrupt();
    HalpLa64ProgramTimer(Clock->Period);
    return (ULONG)min(Skipped, (ULONG64)MAXULONG);
}

/* ProfileTime samples the interrupted PC on the ordinary clock tick. The
 * PMU-based sources are not advertised: profile.c reports them unsupported. */
VOID
NTAPI
HalStartProfileInterrupt(
    _In_ KPROFILE_SOURCE Source)
{
    if (Source == ProfileTime)
    {
        HalpLa64Clocks[KeGetCurrentProcessorNumber()].ProfileElapsed = 0;
        HalpProfileEnabled = TRUE;
    }
}

VOID
NTAPI
HalStopProfileInterrupt(
    _In_ KPROFILE_SOURCE Source)
{
    if (Source == ProfileTime)
        HalpProfileEnabled = FALSE;
}

ULONG_PTR
NTAPI
HalSetProfileInterval(
    _In_ ULONG_PTR Interval)
{
    KIRQL OldIrql;

    /* The periodic clock cannot sample more often than one tick. */
    Interval = max(Interval, HALP_LA64_DEFAULT_PROFILE_INTERVAL);
    Interval = min(Interval, (ULONG_PTR)MAXULONG - 99999);
    Interval = ((Interval + 99999) / HALP_LA64_DEFAULT_PROFILE_INTERVAL) *
               HALP_LA64_DEFAULT_PROFILE_INTERVAL;
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    HalpLa64ProfileInterval = (ULONG)Interval;
    HalpLa64Clocks[KeGetCurrentProcessorNumber()].ProfileElapsed = 0;
    KeLowerIrql(OldIrql);
    return Interval;
}

/* Called by the kernel's IS[11] path at CLOCK_LEVEL with interrupts masked.
 * The TI line is level-triggered and stays asserted until TICLR.TI is written;
 * that write must happen before the kernel re-enables interrupts or the same
 * tick is delivered again. */
VOID
NTAPI
HalpLa64ClockInterrupt(
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG Number = KeGetCurrentProcessorNumber();
    HALP_LA64_CLOCK *Clock = &HalpLa64Clocks[Number];
    ULONG Increment = Clock->Increment;

    HalpLa64ClearTimerInterrupt();

    /* A HalSetTimeIncrement change applies to the period that starts now. */
    if (HalpLa64CurrentTimeIncrement != Increment)
    {
        Clock->Increment = HalpLa64CurrentTimeIncrement;
        Clock->Period = HalpLa64IncrementToTicks(Clock->Increment);
        HalpLa64ProgramTimer(Clock->Period);
    }

    if (HalpProfileEnabled)
    {
        Clock->ProfileElapsed += Increment;
        if (Clock->ProfileElapsed >= HalpLa64ProfileInterval)
        {
            KIRQL OldIrql;

            Clock->ProfileElapsed %= HalpLa64ProfileInterval;
            KeRaiseIrql(PROFILE_LEVEL, &OldIrql);
            KeProfileInterruptWithSource(TrapFrame, ProfileTime);
            KeLowerIrql(OldIrql);
        }
    }
    if (Number == 0)
    {
        KeUpdateSystemTime(TrapFrame, Increment, TrapFrame->PreviousIrql);
    }
    else
    {
        Clock->RunTimeElapsed += Increment;
        if (Clock->RunTimeElapsed >= HALP_LA64_MAXIMUM_INCREMENT)
        {
            Clock->RunTimeElapsed -= HALP_LA64_MAXIMUM_INCREMENT;
            KeUpdateRunTime(TrapFrame, TrapFrame->PreviousIrql);
        }
    }
}
