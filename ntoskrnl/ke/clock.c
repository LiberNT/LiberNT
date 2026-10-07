/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/clock.c
 * PURPOSE:         System Clock Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <reactos/precisetime.h>
#define NDEBUG
#include <debug.h>

C_ASSERT(FIELD_OFFSET(KUSER_SHARED_DATA, TimeUpdateLock) == 0x340);
C_ASSERT(FIELD_OFFSET(KUSER_SHARED_DATA, BaselineSystemTimeQpc) == 0x348);
C_ASSERT(FIELD_OFFSET(KUSER_SHARED_DATA, BaselineInterruptTimeQpc) == 0x350);
C_ASSERT(FIELD_OFFSET(KUSER_SHARED_DATA, QpcSystemTimeIncrement) == 0x358);
C_ASSERT(FIELD_OFFSET(KUSER_SHARED_DATA, QpcInterruptTimeIncrement) == 0x360);
C_ASSERT(FIELD_OFFSET(KUSER_SHARED_DATA, QpcSystemTimeIncrementShift) == 0x368);
C_ASSERT(FIELD_OFFSET(KUSER_SHARED_DATA, QpcInterruptTimeIncrementShift) == 0x369);

/* GLOBALS *******************************************************************/

LARGE_INTEGER KeBootTime;
ULONGLONG KeBootTimeBias;
volatile KSYSTEM_TIME KeTickCount = { 0, 0, 0 };
ULONG KeMaximumIncrement;
ULONG KeMinimumIncrement;
ULONG KeTimeIncrement;
static volatile ULONG KiTimeGeneration;
static volatile ULONGLONG KiSystemTimeBase[2];
static volatile ULONGLONG KiInterruptTimeBase[2];
static volatile ULONGLONG KiSystemTimeQpc[2];
static volatile ULONGLONG KiInterruptTimeQpc[2];
static volatile ULONGLONG KiSystemTimeIncrement[2];
static volatile ULONGLONG KiInterruptTimeIncrement[2];
static volatile ULONGLONG KiSystemTimeFraction[2];
static volatile ULONGLONG KiInterruptTimeFraction[2];
static volatile ULONGLONG KiInterruptTimeBias[2];
static volatile UCHAR KiSystemTimeShift[2];
static volatile UCHAR KiInterruptTimeShift[2];
static ULONGLONG KiTimeCounterFrequency;
static ULONGLONG KiFrozenCounter;
static BOOLEAN KiTimeInitialized;
#ifdef KI_CYCLE_QUANTUM
ULONG KiCyclesPerClockQuantum = 1;
#endif

/* PRIVATE FUNCTIONS *********************************************************/

static
ULONGLONG
KiComputeTimeIncrement(ULONG Adjustment, UCHAR *Shift)
{
    ULONGLONG DivisorHigh, DivisorLow, RemainderHigh = 0;
    ULONGLONG RemainderLow = 10000000ULL * Adjustment;
    ULONGLONG Result = 0, PreviousLow;
    ULONG Bit;

    DivisorLow = RtlpMultiplyTimeValues(KiTimeCounterFrequency,
                                      KeMaximumIncrement,
                                      &DivisorHigh);
    *Shift = 0;
    while (!DivisorHigh && RemainderLow >= DivisorLow)
    {
        DivisorHigh = DivisorLow >> 63;
        DivisorLow <<= 1;
        ++*Shift;
    }

    for (Bit = 0; Bit < 64; ++Bit)
    {
        RemainderHigh = (RemainderHigh << 1) | (RemainderLow >> 63);
        RemainderLow <<= 1;
        Result <<= 1;
        if (RemainderHigh > DivisorHigh ||
            (RemainderHigh == DivisorHigh && RemainderLow >= DivisorLow))
        {
            PreviousLow = RemainderLow;
            RemainderLow -= DivisorLow;
            RemainderHigh -= DivisorHigh + (PreviousLow < DivisorLow);
            Result |= 1;
        }
    }

    if (RemainderLow || RemainderHigh)
    {
        if (Result == MAXULONGLONG)
        {
            ++*Shift;
            return 1ULL << 63;
        }
        ++Result;
    }
    return Result;
}

static
ULONGLONG
KiAdvanceTime(ULONGLONG Base,
              ULONGLONG Delta,
              ULONGLONG Increment,
              UCHAR Shift,
              ULONGLONG Fraction,
              PULONGLONG NewFraction)
{
    ULONGLONG Part, Result;

    Result = Base + RtlpScaleTimeDelta(Delta, Increment, Shift, &Part);
    *NewFraction = Part + Fraction;
    return Result + (*NewFraction < Part);
}

static
ULONGLONG
KiPublishTime(ULONG Increment,
              BOOLEAN UpdateSystem,
              PLARGE_INTEGER NewSystemTime,
              ULONG NewAdjustment,
              ULONGLONG FrozenCounter)
{
    LARGE_INTEGER Counter, Frequency, Value;
    ULONGLONG SystemTime, InterruptTime, SystemFraction, InterruptFraction, InterruptCounter;
    ULONGLONG SystemQpc, InterruptQpc, SystemIncrement, InterruptIncrement;
    UCHAR SystemShift, InterruptShift;
    ULONG Generation, OldSlot, NewSlot;
    BOOLEAN InterruptsEnabled;

    InterruptsEnabled = KeDisableInterrupts();
    Counter = KeQueryPerformanceCounter(&Frequency);
    Generation = KiTimeGeneration;
    OldSlot = Generation & 1;
    NewSlot = (Generation + 1) & 1;

    if (!KiTimeInitialized)
    {
        ASSERT(Frequency.QuadPart > 0 && KeMaximumIncrement != 0);
        KiTimeCounterFrequency = Frequency.QuadPart;
        SystemTime = KiReadSystemTime(&SharedUserData->SystemTime).QuadPart;
        InterruptTime = KiReadSystemTime(&SharedUserData->InterruptTime).QuadPart + Increment;
        SystemQpc = InterruptQpc = Counter.QuadPart;
        SystemFraction = InterruptFraction = 0;
        SystemIncrement = KiComputeTimeIncrement(KeTimeAdjustment, &SystemShift);
        InterruptIncrement = KiComputeTimeIncrement(KeMaximumIncrement, &InterruptShift);
    }
    else
    {
        SystemIncrement = KiSystemTimeIncrement[OldSlot];
        InterruptIncrement = KiInterruptTimeIncrement[OldSlot];
        SystemShift = KiSystemTimeShift[OldSlot];
        InterruptShift = KiInterruptTimeShift[OldSlot];
        SystemQpc = KiSystemTimeQpc[OldSlot];
        InterruptQpc = Counter.QuadPart;
        InterruptCounter = FrozenCounter ? max(FrozenCounter, KiInterruptTimeQpc[OldSlot]) : InterruptQpc;
        SystemTime = KiSystemTimeBase[OldSlot];
        SystemFraction = KiSystemTimeFraction[OldSlot];
        InterruptTime = KiAdvanceTime(KiInterruptTimeBase[OldSlot],
                                      InterruptCounter - KiInterruptTimeQpc[OldSlot],
                                      InterruptIncrement,
                                      InterruptShift,
                                      KiInterruptTimeFraction[OldSlot],
                                      &InterruptFraction);
        if (UpdateSystem || NewAdjustment)
        {
            SystemTime = KiAdvanceTime(SystemTime,
                                       Counter.QuadPart - SystemQpc,
                                       SystemIncrement,
                                       SystemShift,
                                       SystemFraction,
                                       &SystemFraction);
            SystemQpc = Counter.QuadPart;
        }
    }

    if (NewSystemTime)
    {
        SystemTime = NewSystemTime->QuadPart;
        SystemQpc = Counter.QuadPart;
        SystemFraction = 0;
    }
    if (NewAdjustment)
    {
        KeTimeAdjustment = NewAdjustment;
        SystemIncrement = KiComputeTimeIncrement(NewAdjustment, &SystemShift);
    }

    KiSystemTimeBase[NewSlot] = SystemTime;
    KiInterruptTimeBase[NewSlot] = InterruptTime;
    KiSystemTimeQpc[NewSlot] = SystemQpc;
    KiInterruptTimeQpc[NewSlot] = InterruptQpc;
    KiSystemTimeIncrement[NewSlot] = SystemIncrement;
    KiInterruptTimeIncrement[NewSlot] = InterruptIncrement;
    KiSystemTimeShift[NewSlot] = SystemShift;
    KiInterruptTimeShift[NewSlot] = InterruptShift;
    KiSystemTimeFraction[NewSlot] = SystemFraction;
    KiInterruptTimeFraction[NewSlot] = InterruptFraction;
    KiInterruptTimeBias[NewSlot] = SharedUserData->InterruptTimeBias;
    KeMemoryBarrier();
    WriteULongRelease(&KiTimeGeneration, Generation + 1);

    InterlockedIncrement64((PLONG64)&MmWriteableSharedUserData->TimeUpdateLock);
    Value.QuadPart = SystemTime;
    KiWriteSystemTime(&MmWriteableSharedUserData->SystemTime, Value);
    Value.QuadPart = InterruptTime;
    KiWriteSystemTime(&MmWriteableSharedUserData->InterruptTime, Value);
    MmWriteableSharedUserData->BaselineSystemTimeQpc = SystemQpc;
    MmWriteableSharedUserData->BaselineInterruptTimeQpc = InterruptQpc;
    MmWriteableSharedUserData->QpcSystemTimeIncrement = SystemIncrement;
    MmWriteableSharedUserData->QpcInterruptTimeIncrement = InterruptIncrement;
    MmWriteableSharedUserData->QpcSystemTimeIncrementShift = SystemShift;
    MmWriteableSharedUserData->QpcInterruptTimeIncrementShift = InterruptShift;
    KeMemoryBarrier();
    InterlockedIncrement64((PLONG64)&MmWriteableSharedUserData->TimeUpdateLock);
    KiTimeInitialized = TRUE;
    KeRestoreInterrupts(InterruptsEnabled);
    return InterruptTime;
}

ULONGLONG
NTAPI
KiUpdateSharedTime(ULONG Increment, BOOLEAN UpdateSystem)
{
    return KiPublishTime(Increment, UpdateSystem, NULL, 0, 0);
}

VOID
NTAPI
KiFreezeInterruptTime(VOID)
{
    if (KiTimeInitialized)
        KiFrozenCounter = KeQueryPerformanceCounter(NULL).QuadPart;
}

VOID
NTAPI
KiThawInterruptTime(VOID)
{
    if (KiFrozenCounter)
        KiPublishTime(0, FALSE, NULL, 0, KiFrozenCounter);

    KiFrozenCounter = 0;
}

VOID
NTAPI
KiSetTimeAdjustment(ULONG Adjustment, BOOLEAN Enabled)
{
    KIRQL OldIrql;

    KeSetSystemAffinityThread(1);
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    KiPublishTime(0, TRUE, NULL, Adjustment, 0);
    KiTimeAdjustmentEnabled = Enabled;
    KeLowerIrql(OldIrql);
    KeRevertToUserAffinityThread();
}

static
ULONGLONG
KiQueryTimePrecise(BOOLEAN System, BOOLEAN Unbiased, PULONG64 QpcTimeStamp)
{
    ULONGLONG Base, Baseline, Increment, Bias, Counter;
    UCHAR Shift;
    ULONG Generation, Slot;

    for (;;)
    {
        Generation = ReadULongAcquire(&KiTimeGeneration);
        Slot = Generation & 1;
        Base = System ? KiSystemTimeBase[Slot] : KiInterruptTimeBase[Slot];
        Baseline = System ? KiSystemTimeQpc[Slot] : KiInterruptTimeQpc[Slot];
        Increment = System ? KiSystemTimeIncrement[Slot] : KiInterruptTimeIncrement[Slot];
        Shift = System ? KiSystemTimeShift[Slot] : KiInterruptTimeShift[Slot];
        Bias = Unbiased ? KiInterruptTimeBias[Slot] : 0;
        Counter = KeQueryPerformanceCounter(NULL).QuadPart;
        KeMemoryBarrier();
        if (Generation == ReadULongAcquire(&KiTimeGeneration)) break;
    }
    if (QpcTimeStamp) *QpcTimeStamp = Counter;
    return Base + RtlpScaleTimeDelta(Counter - Baseline, Increment, Shift, NULL) - Bias;
}

ULONGLONG
NTAPI
KiQueryInterruptTimePrecise(PULONG64 QpcTimeStamp, BOOLEAN Unbiased)
{
    return KiQueryTimePrecise(FALSE, Unbiased, QpcTimeStamp);
}

VOID
NTAPI
KeSetSystemTime(IN PLARGE_INTEGER NewTime,
                OUT PLARGE_INTEGER OldTime,
                IN BOOLEAN FixInterruptTime,
                IN PLARGE_INTEGER HalTime OPTIONAL)
{
    TIME_FIELDS TimeFields;
    KIRQL OldIrql, OldIrql2;
    LARGE_INTEGER DeltaTime;
    PLIST_ENTRY ListHead, NextEntry;
    PKTIMER Timer;
    PKSPIN_LOCK_QUEUE LockQueue;
    LIST_ENTRY TempList, TempList2;
    ULONG Hand, i;

    /* Sanity checks */
    ASSERT((NewTime->HighPart & 0xF0000000) == 0);
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    /* Check if this is for the HAL */
    if (HalTime) RtlTimeToTimeFields(HalTime, &TimeFields);

    /* Set affinity to this CPU and raise IRQL to synchronization level */
    KeSetSystemAffinityThread(1);
    OldIrql = KeRaiseIrqlToSynchLevel();
    KeRaiseIrql(HIGH_LEVEL, &OldIrql2);

    /* Query the system time now */
    KeQuerySystemTime(OldTime);

    /* Set the new system time (ordering of these operations is critical) */
    KiPublishTime(0, TRUE, NewTime, 0, 0);

    /* Check if this was for the HAL and set the RTC time */
    if (HalTime) ExCmosClockIsSane = HalSetRealTimeClock(&TimeFields);

    /* Calculate the difference between the new and the old time */
    DeltaTime.QuadPart = NewTime->QuadPart - OldTime->QuadPart;

    /* Update system boot time */
    KeBootTime.QuadPart += DeltaTime.QuadPart;
    KeBootTimeBias = KeBootTimeBias + DeltaTime.QuadPart;

    /* Lower IRQL back */
    KeLowerIrql(OldIrql2);

    /* Check if we need to adjust interrupt time */
    if (FixInterruptTime) ASSERT(FALSE);

    /* Setup a temporary list of absolute timers */
    InitializeListHead(&TempList);

    /* Loop current timers */
    for (i = 0; i < TIMER_TABLE_SIZE; i++)
    {
        /* Loop the entries in this table and lock the timers */
        ListHead = &KiTimerTableListHead[i].Entry;
        LockQueue = KiAcquireTimerLock(i);
        NextEntry = ListHead->Flink;
        while (NextEntry != ListHead)
        {
            /* Get the timer */
            Timer = CONTAINING_RECORD(NextEntry, KTIMER, TimerListEntry);
            NextEntry = NextEntry->Flink;

            /* Is it absolute? */
            if (Timer->Header.Absolute)
            {
                /* Remove it from the timer list */
                KiRemoveEntryTimer(Timer);

                /* Insert it into our temporary list */
                InsertTailList(&TempList, &Timer->TimerListEntry);
            }
        }

        /* Release the lock */
        KiReleaseTimerLock(LockQueue);
    }

    /* Setup a temporary list of expired timers */
    InitializeListHead(&TempList2);

    /* Loop absolute timers */
    while (TempList.Flink != &TempList)
    {
        /* Get the timer */
        Timer = CONTAINING_RECORD(TempList.Flink, KTIMER, TimerListEntry);
        RemoveEntryList(&Timer->TimerListEntry);

        /* Update the due time and handle */
        Timer->DueTime.QuadPart -= DeltaTime.QuadPart;
        Hand = KiComputeTimerTableIndex(Timer->DueTime.QuadPart);
        Timer->Header.Hand = (UCHAR)Hand;

        /* Lock the timer and re-insert it */
        LockQueue = KiAcquireTimerLock(Hand);
        if (KiInsertTimerTable(Timer, Hand))
        {
            /* Remove it from the timer list */
            KiRemoveEntryTimer(Timer);

            /* Insert it into our temporary list */
            InsertTailList(&TempList2, &Timer->TimerListEntry);
        }

        /* Release the lock */
        KiReleaseTimerLock(LockQueue);
    }

    /* Process expired timers. This releases the dispatcher lock. */
    KiTimerListExpire(&TempList2, OldIrql);

    /* Revert affinity */
    KeRevertToUserAffinityThread();
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
ULONG
NTAPI
KeQueryTimeIncrement(VOID)
{
    /* Return the increment */
    return KeMaximumIncrement;
}

/*
 * @implemented
 */
#undef KeQueryTickCount
VOID
NTAPI
KeQueryTickCount(_Out_ PLARGE_INTEGER TickCount)
{
    *TickCount = KiReadSystemTime(&KeTickCount);
}

#ifndef _M_AMD64
/*
 * @implemented
 */
VOID
NTAPI
KeQuerySystemTime(OUT PLARGE_INTEGER CurrentTime)
{
    /* Loop until we get a perfect match */
    for (;;)
    {
        /* Read the time value */
        CurrentTime->HighPart = ReadAcquire(&SharedUserData->SystemTime.High1Time);
        CurrentTime->LowPart = ReadAcquire((const volatile LONG *)&SharedUserData->SystemTime.LowPart);
        if (CurrentTime->HighPart ==
            SharedUserData->SystemTime.High2Time) break;
        YieldProcessor();
    }
}

/*
 * @implemented
 */
ULONGLONG
NTAPI
KeQueryInterruptTime(VOID)
{
    LARGE_INTEGER CurrentTime;

    /* Loop until we get a perfect match */
    for (;;)
    {
        /* Read the time value */
        CurrentTime.HighPart = ReadAcquire(&SharedUserData->InterruptTime.High1Time);
        CurrentTime.LowPart = ReadAcquire((const volatile LONG *)&SharedUserData->InterruptTime.LowPart);
        if (CurrentTime.HighPart ==
            SharedUserData->InterruptTime.High2Time) break;
        YieldProcessor();
    }

    /* Return the time value */
    return CurrentTime.QuadPart;
}
#endif

/*
 * @implemented
 */
ULONGLONG
NTAPI
KeQueryInterruptTimePrecise(OUT PULONG64 QpcTimeStamp)
{
    return KiQueryInterruptTimePrecise(QpcTimeStamp, FALSE);
}

/*
 * @implemented
 */
VOID
NTAPI
KeQuerySystemTimePrecise(OUT PLARGE_INTEGER CurrentTime)
{
    CurrentTime->QuadPart = KiQueryTimePrecise(TRUE, FALSE, NULL);
}

/*
 * @implemented
 */
VOID
NTAPI
KeSetTimeIncrement(IN ULONG MaxIncrement,
                   IN ULONG MinIncrement)
{
#ifdef KI_CYCLE_QUANTUM
    LARGE_INTEGER Frequency;
    ULONGLONG CyclesPerQuantum;
#endif

    /* Set some Internal Variables */
    KeMaximumIncrement = MaxIncrement;
    KeMinimumIncrement = MinIncrement;
    KeTimeAdjustment = MaxIncrement;
    KeTimeIncrement = MaxIncrement;
    KiTickOffset = MaxIncrement;

#ifdef KI_CYCLE_QUANTUM
    /* Convert clock quantum units to counter cycles: the performance counter
     * runs on the same time base as ReadTimeStampCounter. */
    KeQueryPerformanceCounter(&Frequency);
    CyclesPerQuantum = ((ULONGLONG)Frequency.QuadPart * MaxIncrement) /
                       (10000000ULL * CLOCK_QUANTUM_DECREMENT);
    KiCyclesPerClockQuantum = CyclesPerQuantum ?
                              (ULONG)min(CyclesPerQuantum, MAXULONG) : 1;
#endif
}

/* EOF */
