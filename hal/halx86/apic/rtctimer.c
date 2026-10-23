/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GNU GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/apic/rtctimer.c
 * PURPOSE:         HAL APIC Management and Control Code
 * PROGRAMMERS:     Timo Kreuzer (timo.kreuzer@reactos.org)
 * REFERENCES:      https://wiki.osdev.org/RTC
 *                  https://forum.osdev.org/viewtopic.php?f=13&t=20825&start=0
 *                  https://web.archive.org/web/20240119203005/http://www.bioscentral.com/misc/cmosmap.htm
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include "apicp.h"
#include <smp.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

static const UCHAR RtcMinimumClockRate = 6;  /* Minimum rate  6: 1024 Hz / 0.97 ms */
static const UCHAR RtcMaximumClockRate = 10; /* Maximum rate 10: 64 Hz / 15.6 ms */
static UCHAR HalpCurrentClockRate = 10;  /* Initial rate  10: 64 Hz / 15.6 ms */
static ULONG HalpCurrentTimeIncrement;
static ULONG HalpMinimumTimeIncrement;
static ULONG HalpMaximumTimeIncrement;
static ULONG HalpCurrentFractionalIncrement;
static ULONG HalpRunningFraction;
static BOOLEAN HalpSetClockRate;
static UCHAR HalpNextClockRate;

static LONG HalpClockIpiTickOffset;

BOOLEAN HalpApicTimerClock = FALSE;
static ULONG64 HalpApicTimerFrequency;

extern LARGE_INTEGER HalpCpuClockFrequency;

static
BOOLEAN
HalpCanUseApicTimerClock(VOID)
{
    INT CpuInfo[4];

    __cpuid(CpuInfo, 1);

    if (CpuInfo[2] & (1UL << 31))
        return TRUE;

    __cpuid(CpuInfo, 0);
    if (CpuInfo[0] < 6)
        return FALSE;
    __cpuid(CpuInfo, 6);
    return (CpuInfo[0] & (1UL << 2)) != 0;
}

static
VOID
HalpCalibrateApicTimer(VOID)
{
    LVT_REGISTER LvtEntry;
    ULONG64 Start, Ticks;
    ULONG Elapsed;

    /* The timer runs with divide-by-1, so the reported frequency is its rate. */
    if (HalpReadHypervisorFrequency(HV_X64_MSR_APIC_FREQUENCY, &HalpApicTimerFrequency))
    {
        DPRINT1("Local APIC timer clock from the hypervisor: %I64u Hz\n", HalpApicTimerFrequency);
        return;
    }

    LvtEntry.Long = 0;
    LvtEntry.Vector = APIC_CLOCK_VECTOR;
    LvtEntry.Mask = 1;
    ApicWrite(APIC_TMRLVTR, LvtEntry.Long);
    ApicWrite(APIC_TDCR, TIMER_DV_DivideBy1);
    ApicWrite(APIC_TICR, MAXULONG);

    Ticks = HalpCpuClockFrequency.QuadPart / 100;
    Start = __rdtsc();
    while (__rdtsc() - Start < Ticks)
        YieldProcessor();
    Elapsed = MAXULONG - ApicRead(APIC_TCCR);

    ApicWrite(APIC_TICR, 0);
    HalpApicTimerFrequency = (ULONG64)Elapsed * 100;
    DPRINT1("Local APIC timer clock: %I64u Hz\n", HalpApicTimerFrequency);
}

/*!
    \brief Converts the CMOS RTC rate into the time increment in 0.1ns intervals.

    Rate Frequency Interval (ms) Precise increment (0.1ns)
    ------------------------------------------------------
     0   disabled
     1   32768      0.03052            305,175
     2   16384      0.06103            610,351
     3    8192      0.12207          1,220,703
     4    4096      0.24414          2,441,406
     5    2048      0.48828          4,882,812
     6    1024      0.97656          9,765,625 <- minimum
     7     512      1.95313         19,531,250
     8     256      3.90625         39,062,500
     9     128      7.8125          78,125,000
    10      64     15.6250         156,250,000 <- maximum / default
    11      32     31.25           312,500,000
    12      16     62.5            625,000,000
    13       8    125            1,250,000,000
    14       4    250            2,500,000,000
    15       2    500            5,000,000,000

*/
FORCEINLINE
ULONG
RtcClockRateToPreciseIncrement(UCHAR Rate)
{
    /* Calculate frequency */
    ULONG Frequency = 32768 >> (Rate - 1);

    /* Calculate interval in 0.1ns interval: Interval = (1 / Frequency) * 10,000,000,000 */
    return 10000000000ULL / Frequency;
}

static
VOID
HalpProgramApicTimerClock(UCHAR ClockRate)
{
    LVT_REGISTER LvtEntry;
    ULONG64 Count;

    Count = HalpApicTimerFrequency * RtcClockRateToPreciseIncrement(ClockRate) / 10000000000ULL;

    LvtEntry.Long = 0;
    LvtEntry.Vector = APIC_CLOCK_VECTOR;
    LvtEntry.TimerMode = 1;
    LvtEntry.Mask = 0;
    ApicWrite(APIC_TDCR, TIMER_DV_DivideBy1);
    ApicWrite(APIC_TMRLVTR, LvtEntry.Long);
    ApicWrite(APIC_TICR, (ULONG)max(Count, 1));
}

VOID
RtcSetClockRate(UCHAR ClockRate)
{
    UCHAR RegisterA;
    ULONG PreciseIncrement;

    /* Update the global values */
    HalpCurrentClockRate = ClockRate;
    PreciseIncrement = RtcClockRateToPreciseIncrement(ClockRate);
    HalpCurrentTimeIncrement = PreciseIncrement / 1000;
    HalpCurrentFractionalIncrement = PreciseIncrement % 1000;

    if (HalpApicTimerClock)
    {
        HalpProgramApicTimerClock(ClockRate);
        return;
    }

    /* Acquire CMOS lock */
    HalpAcquireCmosSpinLock();

    // TODO: disable NMI

    /* Read value of register A */
    RegisterA = HalpReadCmos(RTC_REGISTER_A);

    /* Change lower 4 bits to new rate */
    RegisterA &= 0xF0;
    RegisterA |= ClockRate;

    /* Write the new value */
    HalpWriteCmos(RTC_REGISTER_A, RegisterA);

    /* Release CMOS lock */
    HalpReleaseCmosSpinLock();
}

CODE_SEG("INIT")
VOID
NTAPI
HalpInitializeClock(VOID)
{
    ULONG_PTR EFlags;
    UCHAR RegisterB;

    /* Save EFlags and disable interrupts */
    EFlags = __readeflags();
    _disable();

    if (HalpCanUseApicTimerClock())
    {
        HalpApicTimerClock = TRUE;
        HalpCalibrateApicTimer();

        RtcSetClockRate(HalpCurrentClockRate);

        HalpMinimumTimeIncrement = RtcClockRateToPreciseIncrement(RtcMinimumClockRate) / 1000;
        HalpMaximumTimeIncrement = RtcClockRateToPreciseIncrement(RtcMaximumClockRate) / 1000;
        KeSetTimeIncrement(HalpMaximumTimeIncrement, HalpMinimumTimeIncrement);

        __writeeflags(EFlags);
        DPRINT1("Clock initialized (local APIC timer)\n");
        return;
    }

    // TODO: disable NMI

    /* Acquire CMOS lock */
    HalpAcquireCmosSpinLock();

    /* Enable the periodic interrupt in the CMOS */
    RegisterB = HalpReadCmos(RTC_REGISTER_B);
    HalpWriteCmos(RTC_REGISTER_B, RegisterB | RTC_REG_B_PI);

    /* Release CMOS lock */
    HalpReleaseCmosSpinLock();

    /* Set initial rate */
    RtcSetClockRate(HalpCurrentClockRate);

    /* Calculate minumum and maximum increment */
    HalpMinimumTimeIncrement = RtcClockRateToPreciseIncrement(RtcMinimumClockRate) / 1000;
    HalpMaximumTimeIncrement = RtcClockRateToPreciseIncrement(RtcMaximumClockRate) / 1000;

    /* Notify the kernel about the maximum and minimum increment */
    KeSetTimeIncrement(HalpMaximumTimeIncrement, HalpMinimumTimeIncrement);

    /* Enable the timer interrupt */
    HalEnableSystemInterrupt(APIC_CLOCK_VECTOR, CLOCK_LEVEL, Latched);

    /*
     * Firmware can leave an RTC interrupt pending, and calibrating the TSC
     * against the PM timer does not acknowledge it. Clear register C after
     * unmasking the IOAPIC route so the next periodic interrupt produces a
     * fresh edge. Keep CPU interrupts disabled until the RTC is rearmed.
     */
    HalpAcquireCmosSpinLock();
    HalpReadCmos(RTC_REGISTER_C);
    HalpReleaseCmosSpinLock();

    /* Restore interrupt state */
    __writeeflags(EFlags);

    DPRINT1("Clock initialized\n");
}

VOID
HalpRestoreClock(VOID)
{
    UCHAR RegisterB;

    if (HalpApicTimerClock)
    {
        RtcSetClockRate(HalpCurrentClockRate);
        return;
    }

    HalpAcquireCmosSpinLock();
    RegisterB = HalpReadCmos(RTC_REGISTER_B);
    HalpWriteCmos(RTC_REGISTER_B, RegisterB | RTC_REG_B_PI);
    HalpReleaseCmosSpinLock();

    RtcSetClockRate(HalpCurrentClockRate);

    HalpAcquireCmosSpinLock();
    HalpReadCmos(RTC_REGISTER_C);
    HalpReleaseCmosSpinLock();
}

VOID
FASTCALL
HalpClockInterruptHandler(IN PKTRAP_FRAME TrapFrame)
{
    ULONG LastIncrement;
    KIRQL Irql;

    /* Enter trap */
    KiEnterInterruptTrap(TrapFrame);
#ifdef _M_AMD64
    /* This is for debugging */
    TrapFrame->ErrorCode = 0xc10c4;
#endif

    /* Start the interrupt */
    if (!HalBeginSystemInterrupt(CLOCK_LEVEL, APIC_CLOCK_VECTOR, &Irql))
    {
        /* Spurious, just end the interrupt */
#ifdef _M_IX86
        KiEoiHelper(TrapFrame);
#endif
        return;
    }

    /* Read register C, so that the next interrupt can happen */
    if (!HalpApicTimerClock)
    {
        HalpAcquireCmosSpinLock();
        HalpReadCmos(RTC_REGISTER_C);
        HalpReleaseCmosSpinLock();
    }

    /* Save increment */
    LastIncrement = HalpCurrentTimeIncrement;

    /* Check if the running fraction has accounted for 100 ns */
    HalpRunningFraction += HalpCurrentFractionalIncrement;
    if (HalpRunningFraction >= 1000)
    {
        LastIncrement++;
        HalpRunningFraction -= 1000;
    }

    /* Check if someone changed the time rate */
    if (HalpSetClockRate)
    {
        /* Set new clock rate */
        RtcSetClockRate(HalpNextClockRate);

        /* We're done */
        HalpSetClockRate = FALSE;
    }

    /* Update AP runtime counters at the same cadence as the boot CPU. */
    HalpClockIpiTickOffset += (LONG)LastIncrement;
    if (HalpClockIpiTickOffset >= (LONG)HalpMaximumTimeIncrement)
    {
        HalpClockIpiTickOffset -= (LONG)HalpMaximumTimeIncrement;
        HalpBroadcastClockIpi(CLOCK_IPI_VECTOR);
    }

    /* Update the system time -- on x86 the kernel will exit this trap  */
    KeUpdateSystemTime(TrapFrame, LastIncrement, Irql);

    /* End the interrupt */
    KiEndInterrupt(Irql, TrapFrame);
}

VOID
FASTCALL
HalpClockIpiHandler(IN PKTRAP_FRAME TrapFrame)
{
    KIRQL Irql;

    /* Enter trap */
    KiEnterInterruptTrap(TrapFrame);
#ifdef _M_AMD64
    /* This is for debugging */
    TrapFrame->ErrorCode = 0xc10c4;
#endif

    /* Start the interrupt */
    if (!HalBeginSystemInterrupt(CLOCK_LEVEL, CLOCK_IPI_VECTOR, &Irql))
    {
        /* Spurious, just end the interrupt */
#ifdef _M_IX86
        KiEoiHelper(TrapFrame);
#endif
        return;
    }

    /* Call the kernel to update runtimes */
    KeUpdateRunTime(TrapFrame, Irql);

    /* End the interrupt */
    KiEndInterrupt(Irql, TrapFrame);
}

ULONG
NTAPI
HalSetTimeIncrement(IN ULONG Increment)
{
    UCHAR Rate;
    ULONG NextIncrement;

    /* Lookup largest value below given Increment */
    for (Rate = RtcMinimumClockRate; Rate < RtcMaximumClockRate; Rate++)
    {
        /* Check if this is the largest rate possible */
        NextIncrement = RtcClockRateToPreciseIncrement(Rate + 1) / 1000;
        if (NextIncrement > Increment)
            break;
    }

    /* Set the rate and tell HAL we want to change it */
    HalpNextClockRate = Rate;
    HalpSetClockRate = TRUE;

    /* Return the real increment */
    return RtcClockRateToPreciseIncrement(Rate) / 1000;
}
