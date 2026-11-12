/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The clock tick: what time it is, and who spent it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#define NDEBUG
#include <debug.h>

#undef KeQueryTickCount
#undef KeQueryInterruptTime
#undef KeQuerySystemTime

/* GLOBALS ********************************************************************/

/*
 * How many ticks of the default length have gone by since the system started.
 * This is the coarse clock the scheduler ages threads against: it does not
 * speed up when a caller asks for a finer timer resolution, so a wait time
 * recorded against it stays comparable.
 */
volatile KSYSTEM_TIME KeTickCount;

/* The system time the machine started at, and the bias between the clock the
   firmware keeps and the one the system reports. */
LARGE_INTEGER KeBootTime;
ULONGLONG KeBootTimeBias;

/* The longest and shortest clock period the platform offers, and the one it is
   running at now, all in hundreds of nanoseconds. */
ULONG KeMaximumIncrement;
ULONG KeMinimumIncrement;
ULONG KeTimeIncrement;

/* How much system time each tick is worth. It differs from the clock period
   while the time is being slewed to match an outside reference. */
ULONG KeTimeAdjustment;
BOOLEAN KiTimeAdjustmentEnabled;

/* Set where the platform's timing cannot be trusted to the usual tolerances,
   which makes the watchdogs and the deadline checks more forgiving. */
BOOLEAN KiRelaxedTiming;

/* How much of the current default-length tick is left to run. */
LONG KiTickOffset;

/* What is left of the default period on each processor before its run time is charged again */
static LONG KiRunTimeOffset[MAXIMUM_PROCESSORS];

/* Where in a default-length tick the clock currently stands, so that a timer
   rounded to the default resolution lands on the same boundary as the tick. */
volatile ULONG KiTickPhase;

/* The one processor that keeps the system clock. Every processor takes the
   clock interrupt, but only this one may move the time forward. */
PKPRCB KiClockOwnerPrcb;

/* Counter cycles in one quantum unit, used where a thread's turn is measured
   in cycles rather than ticks. */
ULONG KiCyclesPerClockQuantum = 1;

/* The clock periods the cycle counter is timed over before a turn is given its real length */
#define KI_QUANTUM_CALIBRATION_PERIODS 32

static ULONG64 KiCalibrationCycles;
static ULONGLONG KiCalibrationTime;
static BOOLEAN KiCalibrationStarted;
static BOOLEAN KiQuantumCalibrated;

/* PRIVATE DATA ***************************************************************/

/*
 * The kernel's own copies of the two clocks. The shared user data page is a
 * mirror of these for user mode to read without a call, and it may not exist
 * yet when the first ticks arrive, so these and not the page are the truth.
 */
static volatile KSYSTEM_TIME KiInterruptTimeValue;
static volatile KSYSTEM_TIME KiSystemTimeValue;

/* The clock owner's tick reads and writes the system time back; a set from another processor must not fall in between */
static KSPIN_LOCK KiSystemTimeLock;

/* How much interrupt time has been left out of the bias-free reading, which is
   what a caller measuring elapsed time while the machine slept has to exclude. */
static ULONGLONG KiInterruptTimeBias;

/* The interrupt time last written, the counter read with it and the length of the period it starts, under a sequence that is odd while they change */
static volatile LONG KiTickSnapshotSequence;
static volatile ULONGLONG KiTickSnapshotTime;
static volatile LONGLONG KiTickSnapshotCounter;
static volatile ULONG KiTickSnapshotIncrement;

/* Interrupt time stops moving while the debugger has the machine. */
static BOOLEAN KiInterruptTimeFrozen;

/* The bucket of the timer table the clock has checked up to. */
static ULONG KiTimerScanHand;

static LONGLONG KiQpcScaleFrequency;
static ULONGLONG KiQpcSystemTimeIncrement;
static UCHAR KiQpcSystemTimeIncrementShift;

/* PRIVATE FUNCTIONS **********************************************************/

static __inline
ULONGLONG
KiGetInterruptTime(VOID)
{
    return (ULONGLONG)KiReadSystemTime(&KiInterruptTimeValue).QuadPart;
}

static __inline
ULONGLONG
KiGetSystemTime(VOID)
{
    return (ULONGLONG)KiReadSystemTime(&KiSystemTimeValue).QuadPart;
}

/*
 * Mirrors one of the clocks into the page user mode reads. The page is written
 * through its own writable mapping; the mapping user mode sees is read-only,
 * which is why the kernel cannot simply write where it reads.
 */
FORCEINLINE
VOID
KiPublishSharedTime(
    _In_ ULONG Offset,
    _In_ LARGE_INTEGER Value)
{
    PKUSER_SHARED_DATA Shared = MmWriteableSharedUserData;

    if (Shared == NULL)
        return;

    KiWriteSystemTime((volatile KSYSTEM_TIME *)((PUCHAR)Shared + Offset),
                      Value);
}

/* Counter ticks become hundreds of nanoseconds as (ticks * Increment) >> (64 - Shift) */
static
VOID
KiComputeQpcScale(
    _In_ LONGLONG Frequency)
{
    ULONGLONG Remainder;
    ULONGLONG Quotient = 0;
    ULONGLONG Low;
    UCHAR Shift = 0;
    ULONG Bit;

    while (Shift < 63 && ((ULONGLONG)Frequency << Shift) <= 10000000ULL)
        Shift++;

    Remainder = 10000000ULL >> Shift;
    Low = (Shift != 0) ? (10000000ULL << (64 - Shift)) : 0;

    for (Bit = 0; Bit < 64; Bit++)
    {
        Remainder = (Remainder << 1) | (Low >> 63);
        Low <<= 1;
        Quotient <<= 1;

        if (Remainder >= (ULONGLONG)Frequency)
        {
            Remainder -= (ULONGLONG)Frequency;
            Quotient |= 1;
        }
    }

    KiQpcSystemTimeIncrement = Quotient;
    KiQpcSystemTimeIncrementShift = Shift;
    KiQpcScaleFrequency = Frequency;
}

/* Only the clock owner calls this, so it is the one writer of the update lock */
static
VOID
KiPublishSystemTimeWithBaseline(
    _In_ LARGE_INTEGER Interrupt,
    _In_ LARGE_INTEGER System)
{
    PKUSER_SHARED_DATA Shared = MmWriteableSharedUserData;
    LARGE_INTEGER Counter, Frequency;

    if (Shared == NULL)
        return;

    Counter = KeQueryPerformanceCounter(&Frequency);
    if (Frequency.QuadPart > 0 && Frequency.QuadPart != KiQpcScaleFrequency)
        KiComputeQpcScale(Frequency.QuadPart);

    InterlockedIncrement64((volatile LONG64 *)&Shared->TimeUpdateLock);

    Shared->BaselineSystemTimeQpc = (ULONGLONG)Counter.QuadPart;
    Shared->QpcSystemTimeIncrement = (Frequency.QuadPart > 0) ? KiQpcSystemTimeIncrement : 0;
    Shared->QpcSystemTimeIncrementShift = KiQpcSystemTimeIncrementShift;
    KiWriteSystemTime(&Shared->SystemTime, System);

    /* The interrupt time goes with the counter read when the clock wrote it, which user mode measures from */
    Shared->BaselineInterruptTimeQpc = (ULONGLONG)KiTickSnapshotCounter;
    Shared->QpcInterruptTimeIncrement = (Frequency.QuadPart > 0) ? KiQpcSystemTimeIncrement : 0;
    Shared->QpcInterruptTimeIncrementShift = KiQpcSystemTimeIncrementShift;
    KiWriteSystemTime(&Shared->InterruptTime, Interrupt);

    InterlockedIncrement64((volatile LONG64 *)&Shared->TimeUpdateLock);
}

/*
 * Whether the interrupted code was running in user mode. The program counter
 * answers it without the kernel having to know how any one architecture lays
 * out a trap frame.
 */
FORCEINLINE
BOOLEAN
KiWasInUserMode(
    _In_opt_ PKTRAP_FRAME TrapFrame)
{
    if (TrapFrame == NULL)
        return FALSE;

    return KeGetTrapFramePc(TrapFrame) < (ULONG_PTR)MM_SYSTEM_RANGE_START;
}

/*
 * Charges a clock period to the deferred routine this processor is running and
 * stops the system where it has run for too long.
 *
 * A routine that overruns has either deadlocked or is doing work that belongs
 * in a thread. Either way the processor it is on is lost to everything else,
 * including the dispatcher, so saying so is the only way it is ever found.
 *
 * Nothing is reported while a debugger is attached: a routine stopped at a
 * breakpoint has not overrun, it has been stopped.
 */
static
VOID
KiChargeDpcWatchdog(
    _Inout_ PKPRCB Prcb,
    _In_ ULONG Ticks)
{
    KiPrcbDpcTimeCount(Prcb) += Ticks;
    KiPrcbDpcWatchdogCount(Prcb) += (LONG)Ticks;

    if (KdDebuggerEnabled && !KdDebuggerNotPresent)
        return;

    if ((KiPrcbDpcTimeLimit(Prcb) != 0) &&
        (KiPrcbDpcTimeCount(Prcb) > KiPrcbDpcTimeLimit(Prcb)))
    {
        KeBugCheckEx(DPC_WATCHDOG_VIOLATION,
                     0,
                     KiPrcbDpcTimeCount(Prcb),
                     KiPrcbDpcTimeLimit(Prcb),
                     (ULONG_PTR)Prcb->DpcData[DPC_NORMAL].ActiveDpc);
    }

    if ((KiPrcbDpcWatchdogPeriod(Prcb) != 0) &&
        (KiPrcbDpcWatchdogCount(Prcb) > KiPrcbDpcWatchdogPeriod(Prcb)))
    {
        KeBugCheckEx(DPC_WATCHDOG_VIOLATION,
                     1,
                     (ULONG_PTR)KiPrcbDpcWatchdogCount(Prcb),
                     (ULONG_PTR)KiPrcbDpcWatchdogPeriod(Prcb),
                     (ULONG_PTR)Prcb->DpcData[DPC_NORMAL].ActiveDpc);
    }
}

/* Ten seconds without a clock interrupt of its own stops the system: the processor is hung */
#define KI_CLOCK_WATCHDOG_INTERVAL 100000000ULL

static
VOID
KiCountClockKeepAlive(
    _In_ PKPRCB TargetPrcb,
    _In_ ULONG Ticks)
{
    ULONG Timeout = (ULONG)(KI_CLOCK_WATCHDOG_INTERVAL / KeMaximumIncrement);

    if ((ULONG)InterlockedExchangeAdd(&TargetPrcb->ClockKeepAlive, (LONG)Ticks) + Ticks > Timeout)
    {
        KeBugCheckEx(CLOCK_WATCHDOG_TIMEOUT,
                     Timeout,
                     0,
                     (ULONG_PTR)TargetPrcb,
                     TargetPrcb->Number);
    }
}

static
VOID
KiCheckClockWatchdog(
    _In_ PKPRCB Prcb,
    _In_ ULONG Ticks)
{
    PKPRCB OwnerPrcb = KiClockOwnerPrcb;
    KAFFINITY Watchers;
    ULONG Number;

    InterlockedExchange(&Prcb->ClockKeepAlive, 0);

    if ((Ticks == 0) || (KeMaximumIncrement == 0) || (OwnerPrcb == NULL) ||
        KdEnteredDebugger || KeBugCheckActive || KiInterruptTimeFrozen)
    {
        return;
    }

    if (Prcb == OwnerPrcb)
    {
        for (Number = 0; Number < (ULONG)(UCHAR)KeNumberProcessors; Number++)
        {
            PKPRCB TargetPrcb = KiProcessorBlock[Number];

            if ((TargetPrcb != NULL) && (TargetPrcb != Prcb) &&
                (KeActiveProcessors & TargetPrcb->SetMember) &&
                !KiClockTickSuspended(TargetPrcb))
            {
                KiCountClockKeepAlive(TargetPrcb, Ticks);
            }
        }

        return;
    }

    /* The lowest of the other active processors watches the one that owns the clock */
    Watchers = KeActiveProcessors & ~OwnerPrcb->SetMember;
    if ((Watchers & (~Watchers + 1)) == Prcb->SetMember)
        KiCountClockKeepAlive(OwnerPrcb, Ticks);
}

/* PUBLIC FUNCTIONS: THE CLOCK PERIOD *****************************************/

/*
 * The number of default periods an increment completes, measured against an
 * offset that holds what is left of the current one. A clock interrupt that
 * was held off brings several at once.
 */
static
ULONG
KiTakeElapsedTicks(
    _Inout_ PLONG Offset,
    _In_ ULONG Increment)
{
    ULONG Ticks;

    *Offset -= (LONG)Increment;
    if (*Offset > 0)
        return 0;

    Ticks = 1 + ((ULONG)(-*Offset) / KeMaximumIncrement);
    *Offset += (LONG)(Ticks * KeMaximumIncrement);
    return Ticks;
}

/*
 * Told by the platform what its clock can do. The system runs at the longest
 * period until something asks for a finer one, because every tick costs an
 * interrupt on every processor.
 */
VOID
NTAPI
KeSetTimeIncrement(
    _In_ ULONG MaxIncrement,
    _In_ ULONG MinIncrement)
{
    ULONG Index;

    ASSERT(MaxIncrement >= MinIncrement);

    KeMaximumIncrement = MaxIncrement;
    KeMinimumIncrement = MinIncrement;
    KeTimeIncrement = MaxIncrement;
    KeTimeAdjustment = MaxIncrement;
    KiTickOffset = (LONG)MaxIncrement;
    for (Index = 0; Index < MAXIMUM_PROCESSORS; Index++)
        KiRunTimeOffset[Index] = (LONG)MaxIncrement;
}

/*
 * The length of a tick, which is what a caller converting a tick count into a
 * time needs. It is the default period and not the one the clock is running
 * at: the tick count itself follows the default.
 */
ULONG
NTAPI
KeQueryTimeIncrement(VOID)
{
    return KeMaximumIncrement;
}

/* PUBLIC FUNCTIONS: READING THE CLOCKS ***************************************/

/*
 * How many default-length ticks have gone by. Read without a lock: the write
 * side publishes the halves in an order that lets a reader tell a torn read
 * from a whole one.
 */
VOID
NTAPI
KeQueryTickCount(
    _Out_ PLARGE_INTEGER CurrentCount)
{
    *CurrentCount = KiReadSystemTime(&KeTickCount);
}

ULONGLONG
NTAPI
KeQueryInterruptTime(VOID)
{
    return KiGetInterruptTime();
}

VOID
NTAPI
KeQuerySystemTime(
    _Out_ PLARGE_INTEGER CurrentTime)
{
    CurrentTime->QuadPart = (LONGLONG)KiGetSystemTime();
}

/*
 * How far into the current clock period the counter says the time is,
 * measured from the counter reading taken when the clock last moved; the
 * answer is held inside one period so that a time read from it is never behind
 * the clock nor ahead of the next tick.
 */
static
ULONGLONG
KiCounterOffsetInPeriod(
    _In_ ULONGLONG Interrupt,
    _In_ LARGE_INTEGER Counter,
    _In_ LARGE_INTEGER Frequency)
{
    ULONGLONG SnapshotTime;
    LONGLONG SnapshotCounter;
    ULONG SnapshotIncrement;
    ULONGLONG Elapsed;
    LONG Sequence;

    do
    {
        Sequence = KiTickSnapshotSequence;
        KeMemoryBarrier();
        SnapshotTime = KiTickSnapshotTime;
        SnapshotCounter = KiTickSnapshotCounter;
        SnapshotIncrement = KiTickSnapshotIncrement;
        KeMemoryBarrier();
    } while ((Sequence & 1) || (Sequence != KiTickSnapshotSequence));

    if (Sequence == 0) return 0;

    /* The caller's tick has been overtaken since it read the counter, so the clock has moved on as far as the ticks took it */
    if (SnapshotTime != Interrupt)
        return (SnapshotTime > Interrupt) ? (SnapshotTime - Interrupt) : 0;

    if (Counter.QuadPart <= SnapshotCounter) return 0;

    Elapsed = (ULONGLONG)(Counter.QuadPart - SnapshotCounter) * 10000000ULL /
              (ULONGLONG)Frequency.QuadPart;

    return min(Elapsed, (ULONGLONG)SnapshotIncrement);
}

static
VOID
KiWriteInterruptTime(
    _In_ LARGE_INTEGER Interrupt)
{
    LARGE_INTEGER Counter = KeQueryPerformanceCounter(NULL);

    InterlockedIncrement(&KiTickSnapshotSequence);
    KiTickSnapshotTime = (ULONGLONG)Interrupt.QuadPart;
    KiTickSnapshotCounter = Counter.QuadPart;
    KiTickSnapshotIncrement = KeTimeIncrement;
    InterlockedIncrement(&KiTickSnapshotSequence);

    KiWriteSystemTime(&KiInterruptTimeValue, Interrupt);
}

/*
 * The system time, to the resolution the clock actually has rather than to the
 * resolution of the last tick. Where the platform offers a counter finer than
 * the clock, the time between ticks is interpolated from it.
 */
VOID
NTAPI
KeQuerySystemTimePrecise(
    _Out_ PLARGE_INTEGER CurrentTime)
{
    LARGE_INTEGER Counter, Frequency;
    ULONGLONG Interrupt, System;

    /*
     * Both clocks are read, and then the counter, so that the interpolation is
     * made against the same tick the system time was taken from.
     */
    for (;;)
    {
        Interrupt = KiGetInterruptTime();
        System = KiGetSystemTime();

        Counter = KeQueryPerformanceCounter(&Frequency);

        if (KiGetInterruptTime() == Interrupt)
            break;
    }

    if ((Frequency.QuadPart == 0) || KiInterruptTimeFrozen)
    {
        CurrentTime->QuadPart = (LONGLONG)System;
        return;
    }

    System += KiCounterOffsetInPeriod(Interrupt, Counter, Frequency);

    CurrentTime->QuadPart = (LONGLONG)System;
}

/*
 * The interrupt time, which counts only the time the machine has been awake
 * and never goes backwards. Unbiased leaves out the time the machine spent
 * asleep, which is what a caller measuring how long its own work took wants.
 */
ULONGLONG
NTAPI
KiQueryInterruptTimePrecise(
    _Out_opt_ PULONG64 QpcTimeStamp,
    _In_ BOOLEAN Unbiased)
{
    LARGE_INTEGER Counter, Frequency;
    ULONGLONG Interrupt;

    for (;;)
    {
        Interrupt = KiGetInterruptTime();
        Counter = KeQueryPerformanceCounter(&Frequency);

        if (KiGetInterruptTime() == Interrupt)
            break;
    }

    if (QpcTimeStamp != NULL)
        *QpcTimeStamp = (ULONG64)Counter.QuadPart;

    if ((Frequency.QuadPart != 0) && !KiInterruptTimeFrozen)
    {
        Interrupt += KiCounterOffsetInPeriod(Interrupt, Counter, Frequency);
    }

    if (Unbiased && (Interrupt > KiInterruptTimeBias))
        Interrupt -= KiInterruptTimeBias;

    return Interrupt;
}

ULONGLONG
NTAPI
KeQueryInterruptTimePrecise(
    _Out_ PULONG64 QpcTimeStamp)
{
    return KiQueryInterruptTimePrecise(QpcTimeStamp, FALSE);
}

/* PUBLIC FUNCTIONS: MOVING THE CLOCKS ****************************************/

/*
 * Moves both clocks on by one period and answers the new interrupt time. Only
 * the processor that owns the clock passes UpdateSystem, so the system time and
 * the tick count move exactly once per period however many processors tick.
 */
ULONGLONG
NTAPI
KiUpdateSharedTime(
    _In_ ULONG Increment,
    _In_ BOOLEAN UpdateSystem)
{
    LARGE_INTEGER Interrupt, System, Tick;
    ULONGLONG InterruptTime;
    ULONG Ticks;

    if (KiInterruptTimeFrozen)
        return KiGetInterruptTime();

    InterruptTime = KiGetInterruptTime() + Increment;

    Interrupt.QuadPart = (LONGLONG)InterruptTime;
    KiWriteInterruptTime(Interrupt);

    if (!UpdateSystem)
    {
        KiPublishSharedTime(FIELD_OFFSET(KUSER_SHARED_DATA, InterruptTime),
                            Interrupt);
        return InterruptTime;
    }

    /*
     * System time moves by the adjusted amount, which is how a slew towards an
     * outside reference is applied without the clock period changing. With no
     * slew in force it moves by exactly the period that just elapsed.
     */
    KxAcquireSpinLock(&KiSystemTimeLock);
    System.QuadPart = (LONGLONG)(KiGetSystemTime() +
                                 (KiTimeAdjustmentEnabled ? KeTimeAdjustment
                                                          : Increment));
    KiWriteSystemTime(&KiSystemTimeValue, System);
    KiPublishSystemTimeWithBaseline(Interrupt, System);
    KxReleaseSpinLock(&KiSystemTimeLock);

    /* The tick count only advances on a whole default-length period */
    if (KeMaximumIncrement == 0)
        return InterruptTime;

    Ticks = KiTakeElapsedTicks(&KiTickOffset, Increment);
    if (Ticks != 0)
    {
        KiTickPhase = (ULONG)(InterruptTime % KeMaximumIncrement);

        Tick = KiReadSystemTime(&KeTickCount);
        Tick.QuadPart += Ticks;
        KiWriteSystemTime(&KeTickCount, Tick);
        KiPublishSharedTime(FIELD_OFFSET(KUSER_SHARED_DATA, TickCount), Tick);

        if (MmWriteableSharedUserData != NULL)
            MmWriteableSharedUserData->TickCountLowDeprecated = Tick.LowPart;
    }

    return InterruptTime;
}

/*
 * A counter the processor reads for itself where there is one; otherwise the
 * platform's performance counter, which is slower but is the only monotonic
 * reading available. It is the counter a thread's turn is charged in.
 */
static
ULONG64
KiReadCycleCounter(VOID)
{
#ifdef ReadTimeStampCounter
    return ReadTimeStampCounter();
#else
    return (ULONG64)KeQueryPerformanceCounter(NULL).QuadPart;
#endif
}

/*
 * Times the cycle counter against the clock, once, so that a quantum unit is
 * the third of a clock period it is defined as whatever the counter's rate.
 */
static
VOID
KiCalibrateCycleQuantum(
    _In_ ULONGLONG InterruptTime)
{
    ULONG64 Cycles;
    ULONGLONG Elapsed, PerUnit;

    if (KiQuantumCalibrated)
        return;

    Cycles = KiReadCycleCounter();

    if (!KiCalibrationStarted || (Cycles <= KiCalibrationCycles))
    {
        KiCalibrationCycles = Cycles;
        KiCalibrationTime = InterruptTime;
        KiCalibrationStarted = TRUE;
        return;
    }

    Elapsed = InterruptTime - KiCalibrationTime;
    if (Elapsed < (ULONGLONG)KI_QUANTUM_CALIBRATION_PERIODS * KeMaximumIncrement)
        return;

    PerUnit = ((Cycles - KiCalibrationCycles) / Elapsed) * KeMaximumIncrement +
              (((Cycles - KiCalibrationCycles) % Elapsed) * KeMaximumIncrement) / Elapsed;
    PerUnit /= CLOCK_QUANTUM_DECREMENT;

    KiCyclesPerClockQuantum = (ULONG)min(max(PerUnit, 1ULL), (ULONGLONG)MAXULONG);
    KiQuantumCalibrated = TRUE;
}

static
VOID
KiTickClockOwner(
    _In_ ULONGLONG InterruptTime);

/*
 * The clock tick. Every processor arrives here from its own clock interrupt,
 * charges the period to whatever it was doing, and the one that owns the clock
 * also moves the time on and looks for timers that have come due.
 *
 * OldIrql describes the code the interrupt arrived in, not a level to restore:
 * it is how the period is charged to the right account. The caller keeps the
 * IRQL it raised.
 */
VOID
FASTCALL
KeUpdateSystemTime(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Increment,
    _In_ KIRQL OldIrql)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONGLONG InterruptTime;
    BOOLEAN ClockOwner;

    /*
     * Exactly one processor moves the clock. The first one to tick claims the
     * job, rather than it being given to processor zero: a processor that is
     * parked or has not started yet would otherwise own a clock it never ticks.
     */
    if (KiClockOwnerPrcb == NULL)
    {
        InterlockedCompareExchangePointer((PVOID volatile *)&KiClockOwnerPrcb,
                                          Prcb,
                                          NULL);
    }

    ClockOwner = (Prcb == KiClockOwnerPrcb);

    InterruptTime = KiUpdateSharedTime(Increment, ClockOwner);

    KiUpdateRunTime(TrapFrame,
                    OldIrql,
                    (KeMaximumIncrement != 0) ?
                        KiTakeElapsedTicks(&KiRunTimeOffset[Prcb->Number], Increment) : 1);

    if (SmpDbgEnabled)
        SmpDbgRuntimeTick(Prcb->Number);

    if (!ClockOwner)
        return;

    KiTickClockOwner(InterruptTime);
}

static
VOID
KiTickClockOwner(
    _In_ ULONGLONG InterruptTime)
{
    ULONG Hand, Span, Index, Bucket;
    BOOLEAN Due = FALSE;

    ASSERT(KiClockOwnerPrcb == KeGetCurrentPrcb());

    KdpLogWatchdogCheck(InterruptTime);

    if (KdDebuggerEnabled && KdPollBreakIn())
        DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C);

    if (KeMaximumIncrement == 0)
        return;

    KiCalibrateCycleQuantum(InterruptTime);

    /*
     * One bucket of the timer table covers one default-length period, so the
     * clock only has to look at the buckets it has moved past since last time.
     */
    Hand = KiComputeTimerTableIndex(InterruptTime);
    Span = (Hand - KiTimerScanHand) & (TIMER_TABLE_SIZE - 1);

    for (Index = 0; Index <= Span; Index++)
    {
        Bucket = (KiTimerScanHand + Index) & (TIMER_TABLE_SIZE - 1);
        if ((ULONGLONG)KiTimerTableListHead[Bucket].Time.QuadPart <=
            InterruptTime)
        {
            Due = TRUE;
            break;
        }
    }

    /*
     * The current bucket can still hold timers due later in this period, so the
     * hand stays on it and the next tick looks at it again.
     */
    if (!Due)
    {
        KiTimerScanHand = Hand;
        return;
    }

    /*
     * The expiry runs as a routine of its own rather than here: the clock
     * interrupt is above the level a timer routine may run at.
     *
     * A refusal means the previous expiry has not run yet, and the hand is
     * left where it is so that the same buckets are offered again.
     */
    if (KeInsertQueueDpc(&KiTimerExpireDpc,
                         UlongToPtr(KiTimerScanHand),
                         UlongToPtr(Span)))
    {
        KiTimerScanHand = Hand;
    }
}

VOID
FASTCALL
KiUpdateClockTime(
    _In_ PKPRCB Prcb,
    _In_ ULONG Increment)
{
    ASSERT(Prcb == KeGetCurrentPrcb());
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    if (KiClockOwnerPrcb != Prcb)
        InterlockedExchangePointer((PVOID volatile *)&KiClockOwnerPrcb, Prcb);

    KiTickClockOwner(KiUpdateSharedTime(Increment, TRUE));
}

/*
 * Charges one clock period to whatever this processor was doing when the
 * interrupt arrived, and ends the running thread's turn if it is over.
 */
VOID
NTAPI
KiUpdateRunTime(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_ KIRQL Irql,
    _In_ ULONG Ticks)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD Thread = Prcb->CurrentThread;
    PKPROCESS Process;

    KiCheckClockWatchdog(Prcb, Ticks);

    if (Thread == NULL)
        return;

    Process = Thread->ApcState.Process;

    if (Irql > DISPATCH_LEVEL)
    {
        /* Above the dispatcher: this is interrupt service, not thread time */
        Prcb->InterruptTime += Ticks;
        Prcb->KernelTime += Ticks;
    }
    else if ((Irql == DISPATCH_LEVEL) || Prcb->DpcRoutineActive)
    {
        Prcb->DpcTime += Ticks;
        Prcb->KernelTime += Ticks;

        if (Prcb->DpcRoutineActive)
            KiChargeDpcWatchdog(Prcb, Ticks);
    }
    else if (KiWasInUserMode(TrapFrame))
    {
        Thread->UserTime += Ticks;
        Prcb->UserTime += Ticks;
        if (Process != NULL)
            Process->UserTime += Ticks;
    }
    else
    {
        Thread->KernelTime += Ticks;
        Prcb->KernelTime += Ticks;
        if (Process != NULL)
            Process->KernelTime += Ticks;
    }

    /* DPCs queued here without an interrupt of their own are drained on the tick */
    if ((Prcb->DpcData[DPC_NORMAL].DpcQueueDepth != 0) &&
        !Prcb->DpcRoutineActive &&
        !KiIsDpcInterruptRequested(Prcb))
    {
        /* TODO: adapt MaximumDpcQueueDepth to IdealDpcRate and AdjustDpcThreshold */
        KiSetDpcInterruptRequested(Prcb);
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }

    /* The idle thread has no turn to use up */
    if (Thread == Prcb->IdleThread)
        return;

    /*
     * Where the turn is counted in cycles the processor charges it as it goes
     * and the clock has nothing to subtract; otherwise this is where a turn
     * is spent.
     */
    if (KiGetQuantumUnitCycles() == 0)
    {
        if (!KiDecrementThreadQuantum(Thread, CLOCK_QUANTUM_DECREMENT * Ticks))
            return;
    }
    else
    {
        /* A turn in progress is otherwise only charged at the next switch */
        KiChargeThreadCycleTime(Prcb, Thread);

        if (!KiIsThreadQuantumExpired(Thread))
            return;
    }

    /* Asking for it rather than doing it here: this is above the dispatcher */
    Prcb->QuantumEnd = TRUE;
    KiRequestDispatchInterrupt(Prcb);

    if (SmpDbgEnabled)
        SmpDbgQuantumRequest(Prcb->Number);
}

VOID
NTAPI
KeUpdateRunTime(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_ KIRQL Irql)
{
    KiUpdateRunTime(TrapFrame, Irql, 1);
}

/*
 * Charges the cycles a thread has just used against its turn. Called by the
 * architecture layer around a context switch, where the counter reading for
 * the outgoing thread is still the right one.
 */
VOID
NTAPI
KiChargeThreadCycleTime(
    _Inout_ PKPRCB Prcb,
    _Inout_ PKTHREAD Thread)
{
    ULONG64 Cycles;
    ULONG64 Charge;

    Cycles = KiReadCycleCounter();

    if (Cycles <= Prcb->StartCycles)
    {
        Prcb->StartCycles = Cycles;
        return;
    }

    Charge = Cycles - Prcb->StartCycles;
    Prcb->StartCycles = Cycles;

    Thread->CycleTime += Charge;
    Prcb->CycleTime += Charge;

    if (Thread->ApcState.Process != NULL)
        InterlockedExchangeAdd64((PLONG64)&Thread->ApcState.Process->CycleTime, (LONG64)Charge);

    if (SmpDbgEnabled)
        SmpDbgCycleCharge(Prcb->Number, Charge, FALSE);
}

/* PUBLIC FUNCTIONS: SETTING THE CLOCK ****************************************/

/*
 * Moves the system time to a new value and carries every armed timer with it.
 * An absolute timer was armed for a moment on the old clock, so the same
 * moment on the new one is a different interrupt time; a relative timer is
 * unaffected and must not be moved.
 */
VOID
NTAPI
KeSetSystemTime(
    _In_ PLARGE_INTEGER NewSystemTime,
    _Out_ PLARGE_INTEGER OldSystemTime,
    _In_ BOOLEAN FixInterruptTime,
    _In_opt_ PLARGE_INTEGER HalTime)
{
    KIRQL OldIrql, ClockIrql;
    PKSPIN_LOCK_QUEUE LockQueue;
    LIST_ENTRY AbsoluteList;
    LARGE_INTEGER Difference, Previous, NewTime;
    PKTIMER Timer;
    PLIST_ENTRY Entry;
    ULONG Bucket;
    ULONG Hand;

    ASSERT_IRQL_LESS_OR_EQUAL(APC_LEVEL);

    InitializeListHead(&AbsoluteList);

    OldIrql = KiAcquireDispatcherLock();

    /* Above the clock, so the tick on this processor cannot wait for the lock it holds */
    ClockIrql = KfRaiseIrql(CLOCK_LEVEL);
    KxAcquireSpinLock(&KiSystemTimeLock);

    Previous.QuadPart = (LONGLONG)KiGetSystemTime();
    NewTime = *NewSystemTime;
    Difference.QuadPart = NewTime.QuadPart - Previous.QuadPart;

    KiWriteSystemTime(&KiSystemTimeValue, NewTime);
    KiPublishSharedTime(FIELD_OFFSET(KUSER_SHARED_DATA, SystemTime), NewTime);

    KxReleaseSpinLock(&KiSystemTimeLock);
    KeLowerIrql(ClockIrql);

    /* The moment the machine started moves with the clock it is measured on */
    KeBootTime.QuadPart += Difference.QuadPart;
    KeBootTimeBias += (ULONGLONG)Difference.QuadPart;

    /*
     * Every absolute timer has to be taken out of the table, recomputed and
     * put back: its bucket is derived from an interrupt time that the change
     * has just invalidated.
     */
    for (Bucket = 0; Bucket < TIMER_TABLE_SIZE; Bucket++)
    {
        LockQueue = KiAcquireTimerLock(Bucket);

        Entry = KiTimerTableListHead[Bucket].Entry.Flink;
        while (Entry != &KiTimerTableListHead[Bucket].Entry)
        {
            Timer = CONTAINING_RECORD(Entry, KTIMER, TimerListEntry);
            Entry = Entry->Flink;

            if (!Timer->Header.Absolute)
                continue;

            KiRemoveEntryTimer(Timer);
            Timer->Header.Inserted = FALSE;
            InsertTailList(&AbsoluteList, &Timer->TimerListEntry);
        }

        KiReleaseTimerLock(LockQueue);
    }

    if (FixInterruptTime)
    {
        /* TODO: no platform here reports the interrupt time the firmware kept */
        UNREFERENCED_PARAMETER(HalTime);
    }

    /* Put the absolute timers back where the new clock says they belong */
    while (!IsListEmpty(&AbsoluteList))
    {
        Entry = RemoveHeadList(&AbsoluteList);
        Timer = CONTAINING_RECORD(Entry, KTIMER, TimerListEntry);

        Timer->TimerListEntry.Flink = NULL;
        Timer->TimerListEntry.Blink = NULL;

        /* The due time was an absolute system time; shift it and re-derive */
        if ((Difference.QuadPart > 0) &&
            (Timer->DueTime.QuadPart <= (ULONGLONG)Difference.QuadPart))
        {
            /* A due time the change moves before the machine started has already passed */
            Timer->DueTime.QuadPart = 0;
        }
        else
        {
            Timer->DueTime.QuadPart -= (ULONGLONG)Difference.QuadPart;
        }
        Hand = KiComputeTimerTableIndex(Timer->DueTime.QuadPart);
        Timer->Header.Hand = (UCHAR)Hand;
        Timer->Header.Inserted = TRUE;

        KiAcquireDispatcherObject(&Timer->Header);
        KxInsertTimer(Timer, Hand);
    }

    KiReleaseDispatcherLock(OldIrql);

    *OldSystemTime = Previous;
}

/*
 * Sets how much system time each clock period is worth, which is how the
 * system is slewed towards an outside reference without the clock itself
 * changing rate.
 */
VOID
NTAPI
KiSetTimeAdjustment(
    _In_ ULONG Adjustment,
    _In_ BOOLEAN Enabled)
{
    KIRQL OldIrql;

    OldIrql = KeRaiseIrqlToSynchLevel();

    KeTimeAdjustment = Enabled ? Adjustment : KeMaximumIncrement;
    KiTimeAdjustmentEnabled = Enabled;

    KeLowerIrql(OldIrql);
}

/*
 * Stops and restarts the interrupt clock. Used where the machine is not
 * running: while the debugger owns it, time must not appear to pass, or every
 * timer in the system would be found overdue the moment it is let go.
 */
VOID
NTAPI
KiFreezeInterruptTime(VOID)
{
    KiInterruptTimeFrozen = TRUE;
}

VOID
NTAPI
KiThawInterruptTime(VOID)
{
    KiInterruptTimeFrozen = FALSE;
}

/*
 * The same, for a machine that is going to sleep. What differs is that the
 * time the machine was away did pass, so the interrupt time is moved on by it
 * and the amount is recorded as a bias for callers that must leave it out.
 */
VOID
NTAPI
KiSuspendClockTime(VOID)
{
    KiInterruptTimeFrozen = TRUE;
}

VOID
NTAPI
KiResumeClockTime(
    _In_ ULONGLONG SleepTime)
{
    LARGE_INTEGER Interrupt, System;

    Interrupt.QuadPart = (LONGLONG)(KiGetInterruptTime() + SleepTime);
    KiWriteInterruptTime(Interrupt);

    System.QuadPart = (LONGLONG)(KiGetSystemTime() + SleepTime);
    KiWriteSystemTime(&KiSystemTimeValue, System);
    KiPublishSystemTimeWithBaseline(Interrupt, System);

    KiInterruptTimeBias += SleepTime;
    KiInterruptTimeFrozen = FALSE;
}

/* EOF */
