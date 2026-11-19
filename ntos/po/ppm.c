/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Processor power management: idle state selection and performance control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <pepfx.h>
#include <drivers/ppmproc.h>
#include "ppmcore.h"
#define NDEBUG
#include <debug.h>

#define POP_PPM_TAG                 'mPoP'
#define POP_PPM_MAX_IDLE_STATES     64
#define POP_PPM_HALT                MAXULONG
#define POP_PPM_PREDICTION_LIMIT    10000000ULL
#define POP_PPM_PREDICTION_WEIGHT   8
#define POP_PPM_SINGLE_STEPS        10
#define POP_PPM_CLASSES             2
#define POP_PPM_NO_COUNTER          MAXULONG

typedef struct _POP_PPM_SETTINGS
{
    ULONG ThrottleMinimum;
    ULONG ThrottleMaximum;
    ULONG FrequencyLimit;
    ULONG IncreaseThreshold;
    ULONG DecreaseThreshold;
    ULONG IncreasePolicy;
    ULONG DecreasePolicy;
    ULONG IncreaseTime;
    ULONG DecreaseTime;
    ULONG TimeCheck;
    ULONG BoostPolicy;
    ULONG BoostMode;
    ULONG AutonomousMode;
    ULONG EnergyPreference;
    ULONG ActivityWindow;
    ULONG IdleDisable;
    ULONG IdleStateMaximum;
    ULONG ParkMinCores;
    ULONG ParkMaxCores;
    ULONG ParkIncreaseTime;
    ULONG ParkDecreaseTime;
    ULONG ParkIncreasePolicy;
    ULONG ParkDecreasePolicy;
    ULONG ParkConcurrency;
    ULONG ParkHeadroom;
    ULONG ParkOverUtilization;
} POP_PPM_SETTINGS, *PPOP_PPM_SETTINGS;

/* A setting with no class 1 variant applies to every class */
typedef struct _POP_PPM_SETTING
{
    LPCGUID Guid;
    LPCGUID Guid1;
    ULONG Offset;
    ULONG Minimum;
    ULONG Maximum;
} POP_PPM_SETTING;

typedef struct _POP_PPM_DOMAIN
{
    LIST_ENTRY Link;
    ULONG DomainId;
    ULONG References;
    UCHAR CoordinationType;
    BOOLEAN AffinitizePerfSet;
    ULONG Performance;
    struct _POP_PPM_PROCESSOR *Lead;
} POP_PPM_DOMAIN, *PPOP_PPM_DOMAIN;

typedef struct _POP_PPM_PERF_REQUEST
{
    ULONG Minimum;
    ULONG Maximum;
    ULONG Desired;
    ULONG TimeWindow;
    UCHAR EnergyPreference;
    BOOLEAN Autonomous;
} POP_PPM_PERF_REQUEST, *PPOP_PPM_PERF_REQUEST;

typedef struct _POP_PPM_PROCESSOR
{
    ULONG Index;
    PROCESSOR_NUMBER Number;
    PKPRCB Prcb;
    PEPHANDLE Handle;
    PPEPCALLBACKNOTIFYPPM Notify;
    UCHAR Class;
    BOOLEAN Parked;
    BOOLEAN FeedbackAffinitized;
    ULONG FeedbackCounter;

    volatile LONG IdleSequence;
    ULONGLONG IdleTime;
    ULONGLONG IdleStart;
    ULONGLONG FeedbackNominal;
    ULONGLONG FeedbackActual;
    ULONGLONG IdlePrediction;
    volatile ULONG IdleStateLast;
    ULONG IdleFailures;
    ULONG IdleStateCount;
    PPEP_PROCESSOR_IDLE_STATE_V2 IdleStates;

    BOOLEAN PerfSupported;
    BOOLEAN EppSupported;
    UCHAR AutonomousCapability;
    UCHAR PerfStateCount;
    PEP_PPM_QUERY_PERF_CAPABILITIES PerfCapabilities;
    PPEP_PROCESSOR_PERF_STATE PerfStates;
    PPOP_PPM_DOMAIN Domain;
    ULONG GuaranteedLimit;
    ULONG LimitReasons;

    ULONGLONG CheckTime;
    ULONGLONG CheckIdleTime;
    ULONGLONG CheckNominal;
    ULONGLONG CheckActual;
    ULONG Delivered;
    ULONG Utilization;
    ULONG IncreaseChecks;
    ULONG DecreaseChecks;
    ULONG Target;
    ULONG Minimum;
    ULONG Maximum;
    BOOLEAN Applied;
    POP_PPM_PERF_REQUEST Current;
    POP_PPM_PERF_REQUEST Pending;
    KDPC PerfDpc;
} POP_PPM_PROCESSOR, *PPOP_PPM_PROCESSOR;

/* The Windows 11 Balanced scheme, on AC then on DC power, for efficiency class 0 then class 1 */
static const POP_PPM_SETTINGS PopPpmDefaults[2][POP_PPM_CLASSES] =
{
    {
        { 5, 100, 0, 60, 20, PERFSTATE_POLICY_CHANGE_IDEAL_AGGRESSIVE, PERFSTATE_POLICY_CHANGE_IDEAL, 1, 1, 30, 60,
          PROCESSOR_PERF_BOOST_MODE_AGGRESSIVE, 0, 33, 30000, 0, 0,
          10, 100, 1, 10, CORE_PARKING_POLICY_CHANGE_IDEAL, CORE_PARKING_POLICY_CHANGE_IDEAL, 97, 20, 85 },
        { 5, 100, 0, 60, 20, PERFSTATE_POLICY_CHANGE_IDEAL_AGGRESSIVE, PERFSTATE_POLICY_CHANGE_IDEAL, 1, 2, 30, 60,
          PROCESSOR_PERF_BOOST_MODE_AGGRESSIVE, 0, 33, 30000, 0, 0,
          0, 100, 1, 10, CORE_PARKING_POLICY_CHANGE_IDEAL, CORE_PARKING_POLICY_CHANGE_IDEAL, 97, 20, 85 },
    },
    {
        { 5, 100, 0, 90, 30, PERFSTATE_POLICY_CHANGE_IDEAL_AGGRESSIVE, PERFSTATE_POLICY_CHANGE_IDEAL, 1, 1, 30, 40,
          PROCESSOR_PERF_BOOST_MODE_AGGRESSIVE, 0, 50, 30000, 0, 0,
          10, 100, 1, 10, CORE_PARKING_POLICY_CHANGE_IDEAL, CORE_PARKING_POLICY_CHANGE_IDEAL, 97, 20, 85 },
        { 5, 100, 0, 90, 30, PERFSTATE_POLICY_CHANGE_IDEAL_AGGRESSIVE, PERFSTATE_POLICY_CHANGE_IDEAL, 1, 2, 30, 40,
          PROCESSOR_PERF_BOOST_MODE_AGGRESSIVE, 0, 50, 30000, 0, 0,
          0, 100, 1, 10, CORE_PARKING_POLICY_CHANGE_IDEAL, CORE_PARKING_POLICY_CHANGE_IDEAL, 97, 20, 85 },
    },
};

static const POP_PPM_SETTING PopPpmSettingTable[] =
{
    { &GUID_PROCESSOR_THROTTLE_MINIMUM, &GUID_PROCESSOR_THROTTLE_MINIMUM_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, ThrottleMinimum), 0, POP_PPM_PERCENT },
    { &GUID_PROCESSOR_THROTTLE_MAXIMUM, &GUID_PROCESSOR_THROTTLE_MAXIMUM_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, ThrottleMaximum), 0, POP_PPM_PERCENT },
    { &GUID_PROCESSOR_FREQUENCY_LIMIT, &GUID_PROCESSOR_FREQUENCY_LIMIT_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, FrequencyLimit), 0, MAXULONG },
    { &GUID_PROCESSOR_PERF_INCREASE_THRESHOLD, &GUID_PROCESSOR_PERF_INCREASE_THRESHOLD_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, IncreaseThreshold), 0, POP_PPM_PERCENT },
    { &GUID_PROCESSOR_PERF_DECREASE_THRESHOLD, &GUID_PROCESSOR_PERF_DECREASE_THRESHOLD_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, DecreaseThreshold), 0, POP_PPM_PERCENT },
    { &GUID_PROCESSOR_PERF_INCREASE_POLICY, &GUID_PROCESSOR_PERF_INCREASE_POLICY_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, IncreasePolicy), 0, PERFSTATE_POLICY_CHANGE_INCREASE_MAX },
    { &GUID_PROCESSOR_PERF_DECREASE_POLICY, &GUID_PROCESSOR_PERF_DECREASE_POLICY_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, DecreasePolicy), 0, PERFSTATE_POLICY_CHANGE_DECREASE_MAX },
    { &GUID_PROCESSOR_PERF_INCREASE_TIME, &GUID_PROCESSOR_PERF_INCREASE_TIME_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, IncreaseTime), 0, MAXULONG },
    { &GUID_PROCESSOR_PERF_DECREASE_TIME, &GUID_PROCESSOR_PERF_DECREASE_TIME_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, DecreaseTime), 0, MAXULONG },
    { &GUID_PROCESSOR_PERF_TIME_CHECK, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, TimeCheck), 1, MAXLONG / 10000 },
    { &GUID_PROCESSOR_PERF_BOOST_POLICY, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, BoostPolicy), 0, POP_PPM_PERCENT },
    { &GUID_PROCESSOR_PERF_BOOST_MODE, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, BoostMode), 0, PROCESSOR_PERF_BOOST_MODE_MAX },
    { &GUID_PROCESSOR_PERF_AUTONOMOUS_MODE, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, AutonomousMode), 0, 1 },
    { &GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE, &GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, EnergyPreference), 0, POP_PPM_PERCENT },
    { &GUID_PROCESSOR_PERF_AUTONOMOUS_ACTIVITY_WINDOW, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, ActivityWindow),
      PROCESSOR_PERF_MINIMUM_ACTIVITY_WINDOW, PROCESSOR_PERF_MAXIMUM_ACTIVITY_WINDOW },
    { &GUID_PROCESSOR_IDLE_DISABLE, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, IdleDisable), 0, 1 },
    { &GUID_PROCESSOR_IDLE_STATE_MAXIMUM, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, IdleStateMaximum), 0, MAXULONG },
    { &GUID_PROCESSOR_CORE_PARKING_MIN_CORES, &GUID_PROCESSOR_CORE_PARKING_MIN_CORES_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, ParkMinCores), 0, POP_PPM_PERCENT },
    { &GUID_PROCESSOR_CORE_PARKING_MAX_CORES, &GUID_PROCESSOR_CORE_PARKING_MAX_CORES_1,
      FIELD_OFFSET(POP_PPM_SETTINGS, ParkMaxCores), 0, POP_PPM_PERCENT },
    { &GUID_PROCESSOR_CORE_PARKING_INCREASE_TIME, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, ParkIncreaseTime), 0, MAXULONG },
    { &GUID_PROCESSOR_CORE_PARKING_DECREASE_TIME, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, ParkDecreaseTime), 0, MAXULONG },
    { &GUID_PROCESSOR_CORE_PARKING_INCREASE_POLICY, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, ParkIncreasePolicy),
      0, CORE_PARKING_POLICY_CHANGE_MULTISTEP },
    { &GUID_PROCESSOR_CORE_PARKING_DECREASE_POLICY, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, ParkDecreasePolicy),
      0, CORE_PARKING_POLICY_CHANGE_MULTISTEP },
    { &GUID_PROCESSOR_PARKING_CONCURRENCY_THRESHOLD, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, ParkConcurrency),
      0, POP_PPM_PERCENT },
    { &GUID_PROCESSOR_PARKING_HEADROOM_THRESHOLD, NULL, FIELD_OFFSET(POP_PPM_SETTINGS, ParkHeadroom), 0, POP_PPM_PERCENT },
    { &GUID_PROCESSOR_CORE_PARKING_OVER_UTILIZATION_THRESHOLD, NULL,
      FIELD_OFFSET(POP_PPM_SETTINGS, ParkOverUtilization), 0, POP_PPM_PERCENT },
};

static PPOP_PPM_PROCESSOR *PopPpmProcessors;
static ULONG PopPpmProcessorSlots;
static ULONG PopPpmRegistered;
static ULONG PopPpmTimerPeriod;
static LIST_ENTRY PopPpmDomains;
static KMUTEX PopPpmMutex;
static KSPIN_LOCK PopPpmLock;
static POP_PPM_SETTINGS PopPpmSettings[POP_PPM_CLASSES];
static ULONG PopPpmParkIncreaseChecks[POP_PPM_CLASSES];
static ULONG PopPpmParkDecreaseChecks[POP_PPM_CLASSES];
static KAFFINITY PopPpmParkedSet;
static KTIMER PopPpmTimer;
static KDPC PopPpmCheckDpc;
static KDPC PopPpmReapplyDpc;

/* IDLE ***********************************************************************/

static
ULONGLONG
PopPpmNow(VOID)
{
    ULONG64 Counter;

    return KeQueryInterruptTimePrecise(&Counter);
}

static
BOOLEAN
PopPpmReadFeedback(
    _In_ PPOP_PPM_PROCESSOR Processor,
    _Out_ PULONGLONG Nominal,
    _Out_ PULONGLONG Actual)
{
    PEP_PPM_FEEDBACK_READ Read;

    RtlZeroMemory(&Read, sizeof(Read));
    Read.CounterIndex = Processor->FeedbackCounter;
    if (!Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_FEEDBACK_READ, &Read))
        return FALSE;

    *Nominal = Read.NominalCount;
    *Actual = Read.ActualCount;
    return TRUE;
}

/* An affinitized counter can only be read on its processor, which reads it each time it goes idle */
static
VOID
PopPpmBeginIdle(
    _Inout_ PPOP_PPM_PROCESSOR Processor,
    _In_ ULONGLONG Start)
{
    ULONGLONG Nominal, Actual;
    BOOLEAN Feedback;

    Feedback = (Processor->FeedbackCounter != POP_PPM_NO_COUNTER) && Processor->FeedbackAffinitized &&
               PopPpmReadFeedback(Processor, &Nominal, &Actual);

    InterlockedIncrement(&Processor->IdleSequence);
    Processor->IdleStart = Start;
    if (Feedback)
    {
        Processor->FeedbackNominal = Nominal;
        Processor->FeedbackActual = Actual;
    }
    InterlockedIncrement(&Processor->IdleSequence);
}

static
VOID
PopPpmEndIdle(
    _Inout_ PPOP_PPM_PROCESSOR Processor,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG End)
{
    ULONGLONG Duration = (End > Start) ? End - Start : 0;

    InterlockedIncrement(&Processor->IdleSequence);
    Processor->IdleTime += Duration;
    Processor->IdleStart = 0;
    InterlockedIncrement(&Processor->IdleSequence);

    Duration = min(Duration, POP_PPM_PREDICTION_LIMIT);
    Processor->IdlePrediction = (Processor->IdlePrediction * (POP_PPM_PREDICTION_WEIGHT - 1) + Duration) /
                                POP_PPM_PREDICTION_WEIGHT;
}

/* The owner updates the values between two increments of the sequence, so an even, unchanged sequence brackets a consistent set */
static
ULONGLONG
PopPpmReadIdleTime(
    _In_ PPOP_PPM_PROCESSOR Processor,
    _In_ ULONGLONG Now,
    _Out_opt_ PULONGLONG Nominal,
    _Out_opt_ PULONGLONG Actual)
{
    ULONGLONG Idle, Start, FeedbackNominal, FeedbackActual;
    LONG Sequence;

    for (;;)
    {
        Sequence = InterlockedOr(&Processor->IdleSequence, 0);
        if ((Sequence & 1) == 0)
        {
            Idle = *(volatile ULONGLONG *)&Processor->IdleTime;
            Start = *(volatile ULONGLONG *)&Processor->IdleStart;
            FeedbackNominal = *(volatile ULONGLONG *)&Processor->FeedbackNominal;
            FeedbackActual = *(volatile ULONGLONG *)&Processor->FeedbackActual;
            if (InterlockedOr(&Processor->IdleSequence, 0) == Sequence)
                break;
        }
        YieldProcessor();
    }

    if ((Start != 0) && (Now > Start))
        Idle += Now - Start;

    if (Nominal != NULL)
        *Nominal = FeedbackNominal;
    if (Actual != NULL)
        *Actual = FeedbackActual;

    return Idle;
}

static
ULONG
PopPpmSelectIdleState(
    _In_ PPOP_PPM_PROCESSOR Processor)
{
    PPEP_PROCESSOR_IDLE_STATE_V2 State;
    ULONGLONG Prediction;
    ULONG Best = POP_PPM_HALT;
    ULONG Count = Processor->IdleStateCount;
    ULONG Index;

    /* FIXME: idle states disabled by the power setting fall back to a halt; whether Windows 11 polls instead is not sourced */
    if (PopPpmSettings[0].IdleDisable != 0)
        return POP_PPM_HALT;

    /* FIXME: the deepest-state setting is read as a count of the states allowed; its Windows 11 meaning is not sourced */
    if ((PopPpmSettings[0].IdleStateMaximum != 0) && (Count > PopPpmSettings[0].IdleStateMaximum))
        Count = PopPpmSettings[0].IdleStateMaximum;

    /* TODO: no latency tolerance limits the states chosen */
    Prediction = min(Processor->IdlePrediction, (ULONGLONG)KeTimeIncrement);

    for (Index = 0; Index < Count; Index++)
    {
        State = &Processor->IdleStates[Index];

        /* TODO: platform states and states that need the processor woken explicitly are not entered */
        if (!State->Interruptible || State->PlatformOnly)
            continue;

        if (State->BreakEvenDuration > Prediction)
            continue;

        if ((Best == POP_PPM_HALT) ||
            (State->BreakEvenDuration > Processor->IdleStates[Best].BreakEvenDuration))
        {
            Best = Index;
        }
    }

    return Best;
}

/* Called by the idle loop at DISPATCH_LEVEL with interrupts disabled; returns once the processor woke */
VOID
FASTCALL
PopPpmIdle(
    _In_ PPROCESSOR_POWER_STATE PowerState)
{
    PPOP_PPM_PROCESSOR Processor;
    PEP_PPM_IDLE_EXECUTE Execute;
    PEP_PPM_IDLE_COMPLETE Complete;
    ULONGLONG Start, End;
    ULONG State;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(PowerState == &KeGetCurrentPrcb()->PowerState);
    UNREFERENCED_PARAMETER(PowerState);

    Processor = PopPpmProcessors[KeGetCurrentProcessorNumberEx(NULL)];
    if (Processor == NULL)
    {
        HalProcessorIdle();
        return;
    }

    ASSERT(Processor->Prcb == KeGetCurrentPrcb());

    Start = PopPpmNow();
    State = PopPpmSelectIdleState(Processor);
    PopPpmBeginIdle(Processor, Start);

    if (State != POP_PPM_HALT)
    {
        Execute.Status = STATUS_NOT_SUPPORTED;
        Execute.ProcessorState = State;
        Execute.PlatformState = PEP_PLATFORM_IDLE_STATE_NONE;

        if (Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_IDLE_EXECUTE, &Execute) &&
            NT_SUCCESS(Execute.Status))
        {
            _disable();
            Complete.ProcessorState = State;
            Complete.PlatformState = PEP_PLATFORM_IDLE_STATE_NONE;
            Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_IDLE_COMPLETE, &Complete);
        }
        else
        {
            _disable();
            Processor->IdleFailures++;
            State = POP_PPM_HALT;
        }
    }

    if (State == POP_PPM_HALT)
        HalProcessorIdle();

    Processor->IdleStateLast = State;
    End = PopPpmNow();
    PopPpmEndIdle(Processor, Start, End);
}

/* PERFORMANCE ****************************************************************/

static
UCHAR
PopPpmStateFromPerformance(
    _In_ PPOP_PPM_PROCESSOR Processor,
    _In_ ULONG Performance,
    _In_ BOOLEAN RoundUp)
{
    ULONG Index;

    ASSERT(Processor->PerfStateCount != 0);

    if (RoundUp)
    {
        for (Index = Processor->PerfStateCount; Index-- != 0;)
        {
            if (Processor->PerfStates[Index].Performance >= Performance)
                return (UCHAR)Index;
        }

        return 0;
    }

    for (Index = 0; Index < Processor->PerfStateCount; Index++)
    {
        if (Processor->PerfStates[Index].Performance <= Performance)
            return (UCHAR)Index;
    }

    return (UCHAR)(Processor->PerfStateCount - 1);
}

static
ULONG
PopPpmStep(
    _In_ PPOP_PPM_PROCESSOR Processor,
    _In_ ULONG Performance,
    _In_ BOOLEAN Up)
{
    PPEP_PPM_QUERY_PERF_CAPABILITIES Capabilities = &Processor->PerfCapabilities;
    ULONG Step;
    UCHAR State;

    if (Processor->PerfStateCount != 0)
    {
        State = PopPpmStateFromPerformance(Processor, Performance, TRUE);
        if (Up)
            return Processor->PerfStates[(State != 0) ? State - 1 : 0].Performance;

        if (Processor->PerfStates[State].Performance < Performance)
            return Processor->PerfStates[State].Performance;

        return Processor->PerfStates[min((ULONG)State + 1, (ULONG)Processor->PerfStateCount - 1)].Performance;
    }

    /* FIXME: a single step over a continuous performance range is taken as a tenth of the range */
    Step = max((Capabilities->HighestPerformance - Capabilities->LowestPerformance) / POP_PPM_SINGLE_STEPS, 1UL);
    if (Up)
        return (Performance > MAXULONG - Step) ? MAXULONG : Performance + Step;

    return (Performance > Step) ? Performance - Step : 0;
}

static
VOID
PopPpmComputeLimits(
    _Inout_ PPOP_PPM_PROCESSOR Processor,
    _In_ const POP_PPM_SETTINGS *Settings)
{
    PPEP_PPM_QUERY_PERF_CAPABILITIES Capabilities = &Processor->PerfCapabilities;
    ULONG Nominal = Capabilities->NominalPerformance;
    ULONG Guaranteed = Nominal;
    ULONG Ceiling, Floor, Index;

    if (Processor->GuaranteedLimit != 0)
        Guaranteed = min(Processor->GuaranteedLimit, Capabilities->HighestPerformance);

    /* FIXME: the boost policy only allows or forbids boosting; how Windows 11 scales boosting by it is not sourced */
    if ((Settings->BoostMode == PROCESSOR_PERF_BOOST_MODE_DISABLED) || (Settings->BoostPolicy == 0))
        Ceiling = Nominal;
    else
        Ceiling = Capabilities->HighestPerformance;

    if (Settings->ThrottleMaximum < POP_PPM_PERCENT)
        Ceiling = min(Ceiling, (ULONG)(((ULONGLONG)Nominal * Settings->ThrottleMaximum) / POP_PPM_PERCENT));

    if (Settings->FrequencyLimit != 0)
    {
        if (Processor->PerfStateCount != 0)
        {
            for (Index = 0; Index + 1 < Processor->PerfStateCount; Index++)
            {
                if (Processor->PerfStates[Index].Frequency <= Settings->FrequencyLimit)
                    break;
            }

            Ceiling = min(Ceiling, Processor->PerfStates[Index].Performance);
        }

        /* TODO: a frequency limit is not applied to a continuous performance range */
    }

    if ((Processor->LimitReasons != 0) && (Processor->GuaranteedLimit != 0))
        Ceiling = min(Ceiling, Processor->GuaranteedLimit);

    Ceiling = max(Ceiling, Capabilities->LowestPerformance);

    Floor = max(Capabilities->LowestPerformance,
                (ULONG)(((ULONGLONG)Nominal * Settings->ThrottleMinimum) / POP_PPM_PERCENT));
    Floor = min(Floor, Guaranteed);
    Floor = min(Floor, Ceiling);

    ASSERT(Capabilities->LowestPerformance <= Floor);
    ASSERT(Floor <= Ceiling);
    ASSERT(Ceiling <= Capabilities->HighestPerformance);

    Processor->Minimum = Floor;
    Processor->Maximum = Ceiling;
}

static
ULONG
PopPpmBoost(
    _In_ PPOP_PPM_PROCESSOR Processor,
    _In_ const POP_PPM_SETTINGS *Settings,
    _In_ ULONG Target)
{
    ULONG Threshold = Processor->PerfCapabilities.NominalPerformance;

    switch (Settings->BoostMode)
    {
        case PROCESSOR_PERF_BOOST_MODE_AGGRESSIVE_AT_GUARANTEED:
        case PROCESSOR_PERF_BOOST_MODE_EFFICIENT_AGGRESSIVE_AT_GUARANTEED:
            if (Processor->GuaranteedLimit != 0)
                Threshold = Processor->GuaranteedLimit;
            __fallthrough;

        case PROCESSOR_PERF_BOOST_MODE_AGGRESSIVE:
        case PROCESSOR_PERF_BOOST_MODE_EFFICIENT_AGGRESSIVE:
            /* FIXME: the efficient boost modes boost like the plain ones */
            return (Target > Threshold) ? Processor->Maximum : Target;

        default:
            return Target;
    }
}

/* The load since the last check, and the performance the processor delivered where it reports it */
static
BOOLEAN
PopPpmSampleLoad(
    _Inout_ PPOP_PPM_PROCESSOR Processor,
    _In_ ULONGLONG Now)
{
    ULONGLONG Idle, Elapsed, IdleDelta, Nominal, Actual, Delivered;

    Idle = PopPpmReadIdleTime(Processor, Now, &Nominal, &Actual);
    if ((Processor->FeedbackCounter != POP_PPM_NO_COUNTER) && !Processor->FeedbackAffinitized &&
        !PopPpmReadFeedback(Processor, &Nominal, &Actual))
    {
        Nominal = Processor->CheckNominal;
        Actual = Processor->CheckActual;
    }

    Elapsed = (Now > Processor->CheckTime) ? Now - Processor->CheckTime : 0;
    IdleDelta = (Idle > Processor->CheckIdleTime) ? Idle - Processor->CheckIdleTime : 0;
    Processor->CheckTime = Now;
    Processor->CheckIdleTime = Idle;

    if (Processor->PerfSupported && (Nominal > Processor->CheckNominal) && (Actual >= Processor->CheckActual))
    {
        Delivered = ((Actual - Processor->CheckActual) * Processor->PerfCapabilities.NominalPerformance) /
                    (Nominal - Processor->CheckNominal);
        Processor->Delivered = (ULONG)min(max(Delivered, (ULONGLONG)Processor->PerfCapabilities.LowestPerformance),
                                          (ULONGLONG)Processor->PerfCapabilities.HighestPerformance);
    }
    Processor->CheckNominal = Nominal;
    Processor->CheckActual = Actual;

    if (Elapsed == 0)
        return FALSE;

    IdleDelta = min(IdleDelta, Elapsed);
    Processor->Utilization = (ULONG)(((Elapsed - IdleDelta) * POP_PPM_PERCENT) / Elapsed);
    return TRUE;
}

static
VOID
PopPpmPolicy(
    _Inout_ PPOP_PPM_PROCESSOR Processor,
    _In_ const POP_PPM_SETTINGS *Settings)
{
    ULONGLONG Ideal;
    ULONG Current = Processor->Target;
    ULONG Target = Processor->Target;
    ULONG Middle;

    /* What the processor delivered is what the load was measured at; failing that, what its domain resolved to */
    if (Processor->Delivered != 0)
    {
        Current = Processor->Delivered;
    }
    else if ((Processor->Domain->CoordinationType != PROCESSOR_DOMAIN_COORDINATION_HW_ALL) &&
             (Processor->Domain->Performance != 0))
    {
        Current = Processor->Domain->Performance;
    }

    Middle = max((Settings->IncreaseThreshold + Settings->DecreaseThreshold) / 2, 1UL);
    Ideal = ((ULONGLONG)Current * Processor->Utilization) / Middle;
    Ideal = min(Ideal, (ULONGLONG)MAXULONG);

    if (Processor->Utilization >= Settings->IncreaseThreshold)
    {
        Processor->DecreaseChecks = 0;
        if (++Processor->IncreaseChecks >= max(Settings->IncreaseTime, 1UL))
        {
            Processor->IncreaseChecks = 0;

            switch (Settings->IncreasePolicy)
            {
                case PERFSTATE_POLICY_CHANGE_SINGLE:
                    Target = PopPpmStep(Processor, Current, TRUE);
                    break;

                case PERFSTATE_POLICY_CHANGE_ROCKET:
                    Target = Processor->Maximum;
                    break;

                case PERFSTATE_POLICY_CHANGE_IDEAL_AGGRESSIVE:
                    /* FIXME: the aggressive ideal policy is taken as the ideal target one step higher */
                    Target = PopPpmStep(Processor, max((ULONG)Ideal, Current), TRUE);
                    break;

                default:
                    Target = max((ULONG)Ideal, PopPpmStep(Processor, Current, TRUE));
                    break;
            }

            Target = PopPpmBoost(Processor, Settings, Target);
        }
    }
    else if (Processor->Utilization <= Settings->DecreaseThreshold)
    {
        Processor->IncreaseChecks = 0;
        if (++Processor->DecreaseChecks >= max(Settings->DecreaseTime, 1UL))
        {
            Processor->DecreaseChecks = 0;

            switch (Settings->DecreasePolicy)
            {
                case PERFSTATE_POLICY_CHANGE_SINGLE:
                    Target = PopPpmStep(Processor, Current, FALSE);
                    break;

                case PERFSTATE_POLICY_CHANGE_ROCKET:
                    Target = Processor->Minimum;
                    break;

                default:
                    Target = min((ULONG)Ideal, Current);
                    break;
            }
        }
    }
    else
    {
        Processor->IncreaseChecks = 0;
        Processor->DecreaseChecks = 0;
    }

    Processor->Target = Target;
}

static
VOID
PopPpmSendPerf(
    _Inout_ PPOP_PPM_PROCESSOR Processor,
    _In_ PPOP_PPM_PERF_REQUEST Request)
{
    PEP_PPM_PERF_SET_STATE SetState;
    PEP_PPM_PERF_SET_V2 Set;
    BOOLEAN Handled;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(Request->Minimum <= Request->Maximum);
    ASSERT(Request->Autonomous ||
           ((Request->Minimum <= Request->Desired) && (Request->Desired <= Request->Maximum)));

    if (Processor->PerfStateCount == 0)
    {
        RtlZeroMemory(&Set, sizeof(Set));
        Set.MinimumPerformance = Request->Minimum;
        Set.MaximumPerformance = Request->Maximum;
        Set.DesiredPerformance = Request->Autonomous ? 0 : Request->Desired;
        Set.TimeWindow = Request->TimeWindow;
        Set.PerformanceTolerance = Request->Autonomous ? Request->Minimum : Request->Desired;
        Set.EnergyPerformancePreference = Request->EnergyPreference;
        Handled = Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_PERF_SET, &Set);
    }
    else
    {
        SetState.MinimumPerformanceState = PopPpmStateFromPerformance(Processor, Request->Minimum, TRUE);
        SetState.MaximumPerformanceState = PopPpmStateFromPerformance(Processor, Request->Maximum, FALSE);
        SetState.DesiredPerformanceState = Request->Autonomous ?
                                           PEP_SET_STATE_AUTONOMOUS_MODE :
                                           PopPpmStateFromPerformance(Processor, Request->Desired, TRUE);
        SetState.EnergyPerformancePreference = Request->EnergyPreference;
        Handled = Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_PERF_SET_STATE, &SetState);
    }

    if (Handled)
    {
        Processor->Current = *Request;
        Processor->Applied = TRUE;
    }
}

static
VOID
NTAPI
PopPpmPerfSetDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PPOP_PPM_PROCESSOR Processor = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ASSERT(Processor != NULL);
    ASSERT(KeGetCurrentProcessorNumberEx(NULL) == Processor->Index);

    KeAcquireSpinLockAtDpcLevel(&PopPpmLock);
    if (PopPpmProcessors[Processor->Index] == Processor)
        PopPpmSendPerf(Processor, &Processor->Pending);
    KeReleaseSpinLockFromDpcLevel(&PopPpmLock);
}

_Requires_lock_held_(PopPpmLock)
static
VOID
PopPpmApply(
    _Inout_ PPOP_PPM_PROCESSOR Processor,
    _In_ const POP_PPM_SETTINGS *Settings,
    _In_ ULONG Desired)
{
    POP_PPM_PERF_REQUEST Request;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Minimum = Processor->Minimum;
    Request.Maximum = Processor->Maximum;
    Request.Desired = min(max(Desired, Request.Minimum), Request.Maximum);
    Request.EnergyPreference = Processor->EppSupported ? (UCHAR)Settings->EnergyPreference : 0;
    Request.Autonomous = (Processor->AutonomousCapability == PROCESSOR_AUTONOMOUS_CAPABILITY_EXCLUSIVE) ||
                         ((Processor->AutonomousCapability == PROCESSOR_AUTONOMOUS_CAPABILITY_SUPPORTED) &&
                          (Settings->AutonomousMode != 0));
    if (Request.Autonomous)
    {
        Request.Desired = 0;
        Request.TimeWindow = Settings->ActivityWindow / 1000;
    }

    if (Processor->Applied && RtlEqualMemory(&Request, &Processor->Current, sizeof(Request)))
        return;

    if (Processor->Domain->AffinitizePerfSet &&
        (KeGetCurrentProcessorNumberEx(NULL) != Processor->Index))
    {
        Processor->Pending = Request;
        KeInsertQueueDpc(&Processor->PerfDpc, NULL, NULL);
        return;
    }

    PopPpmSendPerf(Processor, &Request);
}

static
UCHAR
PopPpmClassOf(
    _In_ PPOP_PPM_PROCESSOR Processor)
{
    return (Processor->Class != 0) ? 1 : 0;
}

/* Keeps the busiest processors of each class unparked, as many as the parking settings ask for */
_Requires_lock_held_(PopPpmLock)
static
BOOLEAN
PopPpmParking(
    _In_reads_(POP_PPM_CLASSES) const POP_PPM_SETTINGS *Settings,
    _Out_ PKAFFINITY ParkedSet)
{
    POP_PPM_PARKING_INPUT Input[POP_PPM_CLASSES];
    PPOP_PPM_PROCESSOR Processor, Busiest;
    ULONG Unparked[POP_PPM_CLASSES];
    ULONG Target, Minimum, Maximum, Kept, Index;
    UCHAR Class;
    BOOLEAN Changed = FALSE;

    RtlZeroMemory(Input, sizeof(Input));
    for (Index = 0; Index < PopPpmProcessorSlots; Index++)
    {
        Processor = PopPpmProcessors[Index];
        if (Processor == NULL)
            continue;

        Class = PopPpmClassOf(Processor);
        Input[Class].Count++;
        Input[Class].Load += Processor->Utilization;
        if (Processor->Utilization >= Settings[Class].ParkConcurrency)
            Input[Class].Concurrent++;
        if (!Processor->Parked)
        {
            Input[Class].Unparked++;
            if (Processor->Utilization >= Settings[Class].ParkOverUtilization)
                Input[Class].OverUtilized++;
        }
    }

    for (Class = 0; Class < POP_PPM_CLASSES; Class++)
    {
        Unparked[Class] = 0;
        if (Input[Class].Count == 0)
            continue;

        Input[Class].MinCores = Settings[Class].ParkMinCores;
        Input[Class].MaxCores = Settings[Class].ParkMaxCores;
        Input[Class].Headroom = Settings[Class].ParkHeadroom;
        Target = PopPpmParkingTarget(&Input[Class]);
        Minimum = PopPpmParkingBound(Input[Class].Count, Input[Class].MinCores);
        Maximum = max(PopPpmParkingBound(Input[Class].Count, Input[Class].MaxCores), Minimum);
        ASSERT((Minimum <= Target) && (Target <= Maximum) && (Maximum <= Input[Class].Count));

        if (Target > Input[Class].Unparked)
        {
            PopPpmParkDecreaseChecks[Class] = 0;
            if (++PopPpmParkIncreaseChecks[Class] >= max(Settings[Class].ParkIncreaseTime, 1UL))
            {
                PopPpmParkIncreaseChecks[Class] = 0;
                Input[Class].Unparked = PopPpmParkingStep(Input[Class].Unparked, Target, Minimum, Maximum,
                                                          Settings[Class].ParkIncreasePolicy);
            }
        }
        else if (Target < Input[Class].Unparked)
        {
            PopPpmParkIncreaseChecks[Class] = 0;
            if (++PopPpmParkDecreaseChecks[Class] >= max(Settings[Class].ParkDecreaseTime, 1UL))
            {
                PopPpmParkDecreaseChecks[Class] = 0;
                Input[Class].Unparked = PopPpmParkingStep(Input[Class].Unparked, Target, Minimum, Maximum,
                                                          Settings[Class].ParkDecreasePolicy);
            }
        }
        else
        {
            PopPpmParkIncreaseChecks[Class] = 0;
            PopPpmParkDecreaseChecks[Class] = 0;
        }

        Unparked[Class] = min(max(Input[Class].Unparked, Minimum), Maximum);
        ASSERT(Unparked[Class] <= Input[Class].Count);
    }

    /* One processor always runs, whatever both classes allow */
    if ((Unparked[0] == 0) && (Unparked[1] == 0))
        Unparked[(Input[0].Count != 0) ? 0 : 1] = 1;

    for (Index = 0; Index < PopPpmProcessorSlots; Index++)
    {
        if (PopPpmProcessors[Index] != NULL)
            PopPpmProcessors[Index]->Parked = TRUE;
    }

    for (Class = 0; Class < POP_PPM_CLASSES; Class++)
    {
        for (Kept = 0; Kept < min(Unparked[Class], Input[Class].Count); Kept++)
        {
            Busiest = NULL;
            for (Index = 0; Index < PopPpmProcessorSlots; Index++)
            {
                Processor = PopPpmProcessors[Index];
                if ((Processor != NULL) && Processor->Parked && (PopPpmClassOf(Processor) == Class) &&
                    ((Busiest == NULL) || (Processor->Utilization > Busiest->Utilization)))
                {
                    Busiest = Processor;
                }
            }

            ASSERT(Busiest != NULL);
            Busiest->Parked = FALSE;
        }
    }

    *ParkedSet = 0;
    for (Index = 0; Index < min(PopPpmProcessorSlots, (ULONG)(sizeof(KAFFINITY) * 8)); Index++)
    {
        Processor = PopPpmProcessors[Index];
        if ((Processor != NULL) && Processor->Parked)
            *ParkedSet |= AFFINITY_MASK(Index);
    }

    Changed = (*ParkedSet != PopPpmParkedSet);
    PopPpmParkedSet = *ParkedSet;
    return Changed;
}

/* Samples the load when the timer runs the check, and only applies the limits again when anything else does */
static
VOID
NTAPI
PopPpmCheck(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    POP_PPM_SETTINGS Settings[POP_PPM_CLASSES];
    KAFFINITY ParkedSet = 0;
    PEP_PPM_PERF_CHECK_COMPLETE Complete;
    PPOP_PPM_PROCESSOR Processor;
    PPOP_PPM_SETTINGS Own;
    BOOLEAN Sample = (DeferredContext != NULL);
    BOOLEAN Publish = FALSE;
    BOOLEAN Sampled;
    ULONGLONG Now;
    ULONG Index;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    KeAcquireSpinLockAtDpcLevel(&PopPpmLock);
    RtlCopyMemory(Settings, PopPpmSettings, sizeof(Settings));
    Now = PopPpmNow();

    for (Index = 0; Index < PopPpmProcessorSlots; Index++)
    {
        Processor = PopPpmProcessors[Index];
        if (Processor == NULL)
            continue;

        Own = &Settings[PopPpmClassOf(Processor)];
        Sampled = Sample && PopPpmSampleLoad(Processor, Now);

        if (!Processor->PerfSupported)
            continue;

        PopPpmComputeLimits(Processor, Own);
        if (Sampled)
            PopPpmPolicy(Processor, Own);

        Processor->Target = min(max(Processor->Target, Processor->Minimum), Processor->Maximum);
    }

    for (Index = 0; Index < PopPpmProcessorSlots; Index++)
    {
        Processor = PopPpmProcessors[Index];
        if ((Processor == NULL) || !Processor->PerfSupported)
            continue;

        Processor->Domain->Performance = 0;
        Processor->Domain->Lead = NULL;
    }

    for (Index = 0; Index < PopPpmProcessorSlots; Index++)
    {
        Processor = PopPpmProcessors[Index];
        if ((Processor == NULL) || !Processor->PerfSupported)
            continue;

        Processor->Domain->Performance = max(Processor->Domain->Performance, Processor->Target);
        if (Processor->Domain->Lead == NULL)
            Processor->Domain->Lead = Processor;
    }

    for (Index = 0; Index < PopPpmProcessorSlots; Index++)
    {
        Processor = PopPpmProcessors[Index];
        if ((Processor == NULL) || !Processor->PerfSupported)
            continue;

        Own = &Settings[PopPpmClassOf(Processor)];
        switch (Processor->Domain->CoordinationType)
        {
            case PROCESSOR_DOMAIN_COORDINATION_HW_ALL:
                /* TODO: requests of idle processors are not discounted where the platform does not */
                PopPpmApply(Processor, Own, Processor->Target);
                break;

            case PROCESSOR_DOMAIN_COORDINATION_SW_ANY:
                if (Processor->Domain->Lead == Processor)
                    PopPpmApply(Processor, Own, Processor->Domain->Performance);
                break;

            default:
                PopPpmApply(Processor, Own, Processor->Domain->Performance);
                break;
        }
    }

    if (Sample)
    {
        Complete.EvaluationTime = Now;
        for (Index = 0; Index < PopPpmProcessorSlots; Index++)
        {
            Processor = PopPpmProcessors[Index];
            if ((Processor != NULL) && Processor->PerfSupported)
                Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_PERF_CHECK_COMPLETE, &Complete);
        }

        Publish = PopPpmParking(Settings, &ParkedSet);
    }

    KeReleaseSpinLockFromDpcLevel(&PopPpmLock);

    if (Publish)
        KiSetPowerParkedProcessors(ParkedSet);
}

_Requires_lock_held_(PopPpmMutex)
static
VOID
PopPpmArmTimer(VOID)
{
    LARGE_INTEGER DueTime;
    ULONG Period;

    PAGED_CODE();

    Period = (PopPpmRegistered != 0) ? PopPpmSettings[0].TimeCheck : 0;
    if (Period == PopPpmTimerPeriod)
        return;

    PopPpmTimerPeriod = Period;
    if (Period == 0)
    {
        KeCancelTimer(&PopPpmTimer);
        return;
    }

    DueTime.QuadPart = -(LONGLONG)Period * 10000;
    KeSetTimerEx(&PopPpmTimer, DueTime, (LONG)Period, &PopPpmCheckDpc);
}

/* SETTINGS *******************************************************************/

static
VOID
PopPpmReadSettings(
    _Out_writes_(POP_PPM_CLASSES) PPOP_PPM_SETTINGS Settings)
{
    const POP_PPM_SETTING *Entry;
    ULONG Source = PoAc;
    ULONG Value;
    ULONG Index, Class;

    PAGED_CODE();

    if (!PopReadPowerSetting(&GUID_ACDC_POWER_SOURCE, &Source, sizeof(Source)))
        Source = PoAc;

    for (Class = 0; Class < POP_PPM_CLASSES; Class++)
    {
        Settings[Class] = PopPpmDefaults[(Source == PoAc) ? 0 : 1][Class];

        for (Index = 0; Index < RTL_NUMBER_OF(PopPpmSettingTable); Index++)
        {
            Entry = &PopPpmSettingTable[Index];
            if (PopReadPowerSetting(((Class != 0) && (Entry->Guid1 != NULL)) ? Entry->Guid1 : Entry->Guid,
                                    &Value,
                                    sizeof(Value)) &&
                (Value >= Entry->Minimum) && (Value <= Entry->Maximum))
            {
                *(PULONG)((PUCHAR)&Settings[Class] + Entry->Offset) = Value;
            }
        }
    }
}

_Requires_lock_held_(PopPpmMutex)
static
VOID
PopPpmApplySettings(VOID)
{
    POP_PPM_SETTINGS Settings[POP_PPM_CLASSES];
    KIRQL OldIrql;

    PAGED_CODE();

    PopPpmReadSettings(Settings);

    KeAcquireSpinLock(&PopPpmLock, &OldIrql);
    RtlCopyMemory(PopPpmSettings, Settings, sizeof(Settings));
    KeReleaseSpinLock(&PopPpmLock, OldIrql);

    PopPpmArmTimer();
    KeInsertQueueDpc(&PopPpmReapplyDpc, NULL, NULL);
}

/* Any processor setting, or the power source, changed: every setting is read again for the source in use */
static
NTSTATUS
NTAPI
PopPpmSettingCallback(
    _In_ LPCGUID SettingGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _Inout_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(SettingGuid);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(ValueLength);
    UNREFERENCED_PARAMETER(Context);

    PAGED_CODE();

    KeWaitForSingleObject(&PopPpmMutex, Executive, KernelMode, FALSE, NULL);
    PopPpmApplySettings();
    KeReleaseMutex(&PopPpmMutex, FALSE);

    return STATUS_SUCCESS;
}

/* REGISTRATION ***************************************************************/

static
VOID
PopPpmFreeProcessor(
    _In_ _Post_invalid_ PPOP_PPM_PROCESSOR Processor)
{
    if (Processor->IdleStates != NULL)
        ExFreePoolWithTag(Processor->IdleStates, POP_PPM_TAG);

    if (Processor->PerfStates != NULL)
        ExFreePoolWithTag(Processor->PerfStates, POP_PPM_TAG);

    ExFreePoolWithTag(Processor, POP_PPM_TAG);
}

static
VOID
PopPpmQueryIdleStates(
    _Inout_ PPOP_PPM_PROCESSOR Processor,
    _In_ ULONG Count)
{
    PPEP_PPM_QUERY_IDLE_STATES_V2 Query;
    ULONG Size;

    PAGED_CODE();

    if (Count == 0)
        return;

    if (Count > POP_PPM_MAX_IDLE_STATES)
    {
        DPRINT1("Processor %lu reports %lu idle states, more than %u\n",
                Processor->Index, Count, POP_PPM_MAX_IDLE_STATES);
        return;
    }

    Size = FIELD_OFFSET(PEP_PPM_QUERY_IDLE_STATES_V2, IdleStates[Count]);
    Query = ExAllocatePoolZero(PagedPool, Size, POP_PPM_TAG);
    if (Query == NULL)
        return;

    Query->Count = Count;
    if (Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_QUERY_IDLE_STATES_V2, Query))
    {
        Processor->IdleStates = ExAllocatePoolWithTag(NonPagedPoolNx, Count * sizeof(*Processor->IdleStates), POP_PPM_TAG);
        if (Processor->IdleStates != NULL)
        {
            RtlCopyMemory(Processor->IdleStates, Query->IdleStates, Count * sizeof(*Processor->IdleStates));
            Processor->IdleStateCount = Count;
        }
    }

    ExFreePoolWithTag(Query, POP_PPM_TAG);
}

static
VOID
PopPpmQueryPerf(
    _Inout_ PPOP_PPM_PROCESSOR Processor,
    _In_ UCHAR StateCount)
{
    PPEP_PPM_QUERY_PERF_CAPABILITIES Capabilities = &Processor->PerfCapabilities;
    PEP_PPM_QUERY_PERF_AUTONOMOUS_SUPPORT Autonomous;
    PEP_PPM_QUERY_DISCRETE_PERF_STATES States;
    PEP_PPM_QUERY_PERF_EPP_SUPPORT Epp;
    PEP_PPM_PERF_CONSTRAINTS Constraints;
    ULONG Index;

    PAGED_CODE();

    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    if (!Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_QUERY_PERF_CAPABILITIES, Capabilities))
        return;

    if ((Capabilities->NominalPerformance == 0) ||
        (Capabilities->HighestPerformance < Capabilities->NominalPerformance) ||
        (Capabilities->NominalPerformance < Capabilities->LowestPerformance))
    {
        DPRINT1("Processor %lu reports an inconsistent performance range %lu/%lu/%lu\n",
                Processor->Index,
                Capabilities->HighestPerformance,
                Capabilities->NominalPerformance,
                Capabilities->LowestPerformance);
        return;
    }

    Capabilities->LowestNonlinearPerformance = min(max(Capabilities->LowestNonlinearPerformance,
                                                       Capabilities->LowestPerformance),
                                                   Capabilities->NominalPerformance);

    if (StateCount != 0)
    {
        Processor->PerfStates = ExAllocatePoolZero(NonPagedPoolNx, StateCount * sizeof(*Processor->PerfStates), POP_PPM_TAG);
        if (Processor->PerfStates == NULL)
            return;

        States.Count = StateCount;
        States.States = Processor->PerfStates;
        if (!Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_QUERY_DISCRETE_PERF_STATES, &States))
            return;

        /* The states run from the fastest to the slowest, each with its own performance */
        for (Index = 0; Index < StateCount; Index++)
        {
            if ((Processor->PerfStates[Index].Performance > Capabilities->HighestPerformance) ||
                (Processor->PerfStates[Index].Performance < Capabilities->LowestPerformance) ||
                ((Index != 0) &&
                 (Processor->PerfStates[Index].Performance >= Processor->PerfStates[Index - 1].Performance)))
            {
                DPRINT1("Processor %lu reports performance state %lu out of order\n", Processor->Index, Index);
                return;
            }
        }

        Processor->PerfStateCount = StateCount;
    }

    RtlZeroMemory(&Epp, sizeof(Epp));
    if (Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_QUERY_PERF_EPP_SUPPORT, &Epp))
        Processor->EppSupported = Epp.EnergyPerformancePreferenceSupported;

    RtlZeroMemory(&Autonomous, sizeof(Autonomous));
    if (Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_QUERY_AUTONOMOUS_SUPPORT, &Autonomous))
        Processor->AutonomousCapability = Autonomous.AutonomousCapability;

    RtlZeroMemory(&Constraints, sizeof(Constraints));
    if (Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_PERF_CONSTRAINTS, &Constraints))
    {
        Processor->GuaranteedLimit = Constraints.GuaranteedPerformanceLimit;
        Processor->LimitReasons = Constraints.LimitReasons;
    }

    Processor->Target = Capabilities->NominalPerformance;
    Processor->PerfSupported = TRUE;
}

/* A relative counter of delivered against nominal performance tells what the processor really ran at */
static
VOID
PopPpmQueryFeedback(
    _Inout_ PPOP_PPM_PROCESSOR Processor,
    _In_ ULONG Count)
{
    PPEP_PPM_QUERY_FEEDBACK_COUNTERS Query;
    PPEP_PROCESSOR_FEEDBACK_COUNTER Counter;
    ULONG Index;

    PAGED_CODE();

    if ((Count == 0) || (Count > POP_PPM_MAX_IDLE_STATES))
        return;

    Query = ExAllocatePoolZero(PagedPool, FIELD_OFFSET(PEP_PPM_QUERY_FEEDBACK_COUNTERS, Counters[Count]), POP_PPM_TAG);
    if (Query == NULL)
        return;

    Query->Count = Count;
    if (Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_QUERY_FEEDBACK_COUNTERS, Query))
    {
        for (Index = 0; Index < Count; Index++)
        {
            Counter = &Query->Counters[Index];
            if ((Counter->Type == PROCESSOR_FEEDBACK_TYPE_RELATIVE) &&
                (Counter->Counter == PROCESSOR_FEEDBACK_COUNTER_PERFORMANCE))
            {
                Processor->FeedbackCounter = Index;
                Processor->FeedbackAffinitized = (Counter->Affinitized != 0);
                break;
            }
        }
    }

    ExFreePoolWithTag(Query, POP_PPM_TAG);
}

_Requires_lock_held_(PopPpmMutex)
static
PPOP_PPM_DOMAIN
PopPpmReferenceDomain(
    _In_ PPOP_PPM_PROCESSOR Processor)
{
    PEP_PPM_QUERY_DOMAIN_INFO Information;
    PPOP_PPM_DOMAIN Domain;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    for (Entry = PopPpmDomains.Flink; Entry != &PopPpmDomains; Entry = Entry->Flink)
    {
        Domain = CONTAINING_RECORD(Entry, POP_PPM_DOMAIN, Link);
        if (Domain->DomainId == Processor->PerfCapabilities.DomainId)
        {
            Domain->References++;
            return Domain;
        }
    }

    Domain = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Domain), POP_PPM_TAG);
    if (Domain == NULL)
        return NULL;

    Domain->DomainId = Processor->PerfCapabilities.DomainId;
    Domain->References = 1;
    Domain->CoordinationType = PROCESSOR_DOMAIN_COORDINATION_SW_ALL;

    RtlZeroMemory(&Information, sizeof(Information));
    Information.DomainId = Domain->DomainId;
    if (Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_QUERY_DOMAIN_INFO, &Information) &&
        (Information.CoordinationType <= PROCESSOR_DOMAIN_COORDINATION_HW_ALL))
    {
        Domain->CoordinationType = Information.CoordinationType;
        Domain->AffinitizePerfSet = Information.AffinitizePerfSet;
    }

    InsertTailList(&PopPpmDomains, &Domain->Link);
    return Domain;
}

_Requires_lock_held_(PopPpmMutex)
static
VOID
PopPpmDereferenceDomain(
    _In_ PPOP_PPM_DOMAIN Domain)
{
    PAGED_CODE();

    ASSERT(Domain->References != 0);
    if (--Domain->References != 0)
        return;

    RemoveEntryList(&Domain->Link);
    ExFreePoolWithTag(Domain, POP_PPM_TAG);
}

static
VOID
NTAPI
PopPpmSignalDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeSetEvent(DeferredContext, IO_NO_INCREMENT, FALSE);
}

/* A processor runs a DPC only outside its idle function, so once one ran there it no longer uses the old one */
static
VOID
PopPpmSynchronizeProcessor(
    _In_ PPROCESSOR_NUMBER Number)
{
    KEVENT Event;
    KDPC Dpc;

    PAGED_CODE();

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    KeInitializeDpc(&Dpc, PopPpmSignalDpc, &Event);
    KeSetImportanceDpc(&Dpc, HighImportance);
    NT_VERIFY(NT_SUCCESS(KeSetTargetProcessorDpcEx(&Dpc, Number)));

    KeInsertQueueDpc(&Dpc, NULL, NULL);
    KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    KeFlushQueuedDpcs();
}

static
NTSTATUS
PopPpmRegister(
    _In_ PPPM_PROCESSOR_REQUEST Request,
    _In_ ULONG Index)
{
    PEP_PPM_QUERY_CAPABILITIES Capabilities;
    PPOP_PPM_PROCESSOR Processor;
    KIRQL OldIrql;

    PAGED_CODE();

    if (Request->AcceptProcessorNotification == NULL)
        return STATUS_INVALID_PARAMETER;

    Processor = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Processor), POP_PPM_TAG);
    if (Processor == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Processor->Index = Index;
    Processor->Number = Request->Processor;
    Processor->Prcb = KiProcessorBlock[Index];
    Processor->Handle = Request->Handle;
    Processor->Notify = Request->AcceptProcessorNotification;
    Processor->IdleStateLast = POP_PPM_HALT;
    Processor->FeedbackCounter = POP_PPM_NO_COUNTER;
    Processor->Class = Processor->Prcb->PowerState.ArchitecturalEfficiencyClass;

    RtlZeroMemory(&Capabilities, sizeof(Capabilities));
    if (!Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_QUERY_CAPABILITIES, &Capabilities))
    {
        PopPpmFreeProcessor(Processor);
        return STATUS_NOT_SUPPORTED;
    }

    PopPpmQueryIdleStates(Processor, Capabilities.IdleStateCount);
    if (Capabilities.PerformanceStatesSupported)
        PopPpmQueryPerf(Processor, Capabilities.DiscretePerformanceStateCount);
    if (Processor->PerfSupported)
        PopPpmQueryFeedback(Processor, Capabilities.FeedbackCounterCount);

    /* TODO: parking preferences of the platform and platform idle states are not queried */

    KeInitializeDpc(&Processor->PerfDpc, PopPpmPerfSetDpc, Processor);
    if (!NT_SUCCESS(KeSetTargetProcessorDpcEx(&Processor->PerfDpc, &Processor->Number)))
    {
        PopPpmFreeProcessor(Processor);
        return STATUS_INVALID_PARAMETER;
    }

    KeWaitForSingleObject(&PopPpmMutex, Executive, KernelMode, FALSE, NULL);

    if (PopPpmProcessors[Index] != NULL)
    {
        KeReleaseMutex(&PopPpmMutex, FALSE);
        PopPpmFreeProcessor(Processor);
        return STATUS_ALREADY_REGISTERED;
    }

    if (Processor->PerfSupported)
    {
        Processor->Domain = PopPpmReferenceDomain(Processor);
        if (Processor->Domain == NULL)
        {
            KeReleaseMutex(&PopPpmMutex, FALSE);
            PopPpmFreeProcessor(Processor);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    Processor->CheckTime = PopPpmNow();

    KeAcquireSpinLock(&PopPpmLock, &OldIrql);
    PopPpmProcessors[Index] = Processor;
    PopPpmRegistered++;
    KeReleaseSpinLock(&PopPpmLock, OldIrql);

    InterlockedExchangePointer((PVOID *)&Processor->Prcb->PowerState.IdleFunction, (PVOID)PopPpmIdle);

    PopPpmArmTimer();
    KeInsertQueueDpc(&PopPpmReapplyDpc, NULL, NULL);
    KeReleaseMutex(&PopPpmMutex, FALSE);

    DPRINT1("Processor %lu: class %u, %lu idle states, performance %s (%lu-%lu-%lu, %u states, domain %lu, feedback %s)\n",
            Index,
            Processor->Class,
            Processor->IdleStateCount,
            Processor->PerfSupported ? "controlled" : "not controlled",
            Processor->PerfCapabilities.LowestPerformance,
            Processor->PerfCapabilities.NominalPerformance,
            Processor->PerfCapabilities.HighestPerformance,
            Processor->PerfStateCount,
            Processor->PerfCapabilities.DomainId,
            (Processor->FeedbackCounter != POP_PPM_NO_COUNTER) ? "yes" : "no");

    return STATUS_SUCCESS;
}

static
NTSTATUS
PopPpmUnregister(
    _In_ PPPM_PROCESSOR_REQUEST Request,
    _In_ ULONG Index)
{
    KAFFINITY ParkedSet;
    PPOP_PPM_PROCESSOR Processor;
    KIRQL OldIrql;

    PAGED_CODE();

    KeWaitForSingleObject(&PopPpmMutex, Executive, KernelMode, FALSE, NULL);

    Processor = PopPpmProcessors[Index];
    if ((Processor == NULL) ||
        (Processor->Handle != Request->Handle) ||
        (Processor->Notify != Request->AcceptProcessorNotification))
    {
        KeReleaseMutex(&PopPpmMutex, FALSE);
        return STATUS_INVALID_PARAMETER;
    }

    InterlockedExchangePointer((PVOID *)&Processor->Prcb->PowerState.IdleFunction, (PVOID)PopIdle0);

    KeAcquireSpinLock(&PopPpmLock, &OldIrql);
    PopPpmProcessors[Index] = NULL;
    ASSERT(PopPpmRegistered != 0);
    PopPpmRegistered--;
    if (Index < sizeof(KAFFINITY) * 8)
        PopPpmParkedSet &= ~AFFINITY_MASK(Index);
    ParkedSet = PopPpmParkedSet;
    KiSetPowerParkedProcessors(ParkedSet);
    KeReleaseSpinLock(&PopPpmLock, OldIrql);

    PopPpmSynchronizeProcessor(&Processor->Number);
    KeRemoveQueueDpc(&Processor->PerfDpc);
    KeFlushQueuedDpcs();

    if (Processor->Domain != NULL)
        PopPpmDereferenceDomain(Processor->Domain);

    PopPpmArmTimer();
    KeReleaseMutex(&PopPpmMutex, FALSE);

    PopPpmFreeProcessor(Processor);
    return STATUS_SUCCESS;
}

static
NTSTATUS
PopPpmConstraintsChanged(
    _In_ PPPM_PROCESSOR_REQUEST Request,
    _In_ ULONG Index)
{
    PEP_PPM_PERF_CONSTRAINTS Constraints;
    PPOP_PPM_PROCESSOR Processor;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    PAGED_CODE();

    KeWaitForSingleObject(&PopPpmMutex, Executive, KernelMode, FALSE, NULL);

    Processor = PopPpmProcessors[Index];
    if ((Processor == NULL) || (Processor->Handle != Request->Handle) || !Processor->PerfSupported)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        RtlZeroMemory(&Constraints, sizeof(Constraints));
        if (Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_PERF_CONSTRAINTS, &Constraints))
        {
            KeAcquireSpinLock(&PopPpmLock, &OldIrql);
            Processor->GuaranteedLimit = Constraints.GuaranteedPerformanceLimit;
            Processor->LimitReasons = Constraints.LimitReasons;
            KeReleaseSpinLock(&PopPpmLock, OldIrql);
            KeInsertQueueDpc(&PopPpmReapplyDpc, NULL, NULL);
        }
    }

    KeReleaseMutex(&PopPpmMutex, FALSE);
    return Status;
}

NTSTATUS
NTAPI
PopPpmProcessorRequest(
    _In_reads_bytes_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength)
{
    PPM_PROCESSOR_REQUEST Request;
    ULONG Index;

    PAGED_CODE();

    if ((InputBufferLength < sizeof(Request)) || (PopPpmProcessors == NULL))
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&Request, InputBuffer, sizeof(Request));
    if ((Request.Signature != PPM_PROCESSOR_REQUEST_SIGNATURE) || (Request.Size != sizeof(Request)))
        return STATUS_INVALID_PARAMETER;

    Index = KeGetProcessorIndexFromNumber(&Request.Processor);
    if ((Index == INVALID_PROCESSOR_INDEX) || (Index >= PopPpmProcessorSlots) || (Index >= (ULONG)KeNumberProcessors))
        return STATUS_INVALID_PARAMETER;

    switch (Request.Type)
    {
        case PpmProcessorRegister:
            return PopPpmRegister(&Request, Index);

        case PpmProcessorUnregister:
            return PopPpmUnregister(&Request, Index);

        case PpmProcessorPerfConstraintsChanged:
            return PopPpmConstraintsChanged(&Request, Index);

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

/* SYSTEM STATES **************************************************************/

static
VOID
NTAPI
PopPpmResumeDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PEP_PPM_RESUME_FROM_SYSTEM_STATE Resume;
    PPOP_PPM_PROCESSOR Processor;

    UNREFERENCED_PARAMETER(Dpc);

    KeAcquireSpinLockAtDpcLevel(&PopPpmLock);
    Processor = PopPpmProcessors[KeGetCurrentProcessorNumberEx(NULL)];
    if (Processor != NULL)
    {
        Resume.TargetState = (SYSTEM_POWER_STATE)(ULONG_PTR)DeferredContext;
        Processor->Notify(Processor->Handle, PEP_NOTIFY_PPM_RESUME_FROM_SYSTEM_STATE, &Resume);
        Processor->Applied = FALSE;
        Processor->CheckTime = PopPpmNow();
        Processor->CheckIdleTime = PopPpmReadIdleTime(Processor,
                                                      Processor->CheckTime,
                                                      &Processor->CheckNominal,
                                                      &Processor->CheckActual);
        Processor->Delivered = 0;
    }
    KeReleaseSpinLockFromDpcLevel(&PopPpmLock);

    KeSignalCallDpcSynchronize(SystemArgument2);
    KeSignalCallDpcDone(SystemArgument1);
}

VOID
NTAPI
PopPpmResumeFromSystemState(
    _In_ SYSTEM_POWER_STATE SystemState)
{
    PAGED_CODE();

    if (PopPpmProcessors == NULL)
        return;

    KeWaitForSingleObject(&PopPpmMutex, Executive, KernelMode, FALSE, NULL);
    KeGenericCallDpc(PopPpmResumeDpc, (PVOID)(ULONG_PTR)SystemState);
    KeInsertQueueDpc(&PopPpmReapplyDpc, NULL, NULL);
    KeReleaseMutex(&PopPpmMutex, FALSE);
}

/* INFORMATION ****************************************************************/

VOID
NTAPI
PopPpmQueryProcessorInformation(
    _In_ ULONG Index,
    _Out_ PULONG MaxIdleState,
    _Out_ PULONG CurrentIdleState,
    _Out_ PULONG LimitPercent)
{
    PPOP_PPM_PROCESSOR Processor;
    ULONG Last;
    KIRQL OldIrql;

    /* The halt every processor has is its first idle state */
    *MaxIdleState = 1;
    *CurrentIdleState = 1;
    *LimitPercent = POP_PPM_PERCENT;

    if ((PopPpmProcessors == NULL) || (Index >= PopPpmProcessorSlots))
        return;

    KeAcquireSpinLock(&PopPpmLock, &OldIrql);
    Processor = PopPpmProcessors[Index];
    if (Processor != NULL)
    {
        Last = Processor->IdleStateLast;
        *MaxIdleState = max(Processor->IdleStateCount, 1UL);
        *CurrentIdleState = (Last == POP_PPM_HALT) ? 1 : Last + 1;
        if (Processor->PerfSupported && (Processor->Maximum != 0))
        {
            *LimitPercent = (ULONG)min(((ULONGLONG)Processor->Maximum * POP_PPM_PERCENT) /
                                       Processor->PerfCapabilities.NominalPerformance,
                                       (ULONGLONG)POP_PPM_PERCENT);
        }
    }
    KeReleaseSpinLock(&PopPpmLock, OldIrql);
}

/* INITIALIZATION *************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
PopPpmInitialize(VOID)
{
    LPCGUID Setting;
    NTSTATUS Status;
    ULONG Slots;
    ULONG Index;

    PAGED_CODE();

    InitializeListHead(&PopPpmDomains);
    KeInitializeMutex(&PopPpmMutex, 0);
    KeInitializeSpinLock(&PopPpmLock);
    KeInitializeTimerEx(&PopPpmTimer, SynchronizationTimer);
    KeInitializeDpc(&PopPpmCheckDpc, PopPpmCheck, (PVOID)TRUE);
    KeInitializeDpc(&PopPpmReapplyDpc, PopPpmCheck, NULL);
    RtlCopyMemory(PopPpmSettings, PopPpmDefaults[0], sizeof(PopPpmSettings));

    Slots = KeQueryMaximumProcessorCountEx(ALL_PROCESSOR_GROUPS);
    PopPpmProcessors = ExAllocatePoolZero(NonPagedPoolNx, Slots * sizeof(*PopPpmProcessors), POP_PPM_TAG);
    if (PopPpmProcessors == NULL)
    {
        DPRINT1("No processor power management: the processor table could not be allocated\n");
        return;
    }

    PopPpmProcessorSlots = Slots;

    for (Index = 0; Index < 2 * RTL_NUMBER_OF(PopPpmSettingTable) + 1; Index++)
    {
        if (Index == 2 * RTL_NUMBER_OF(PopPpmSettingTable))
            Setting = &GUID_ACDC_POWER_SOURCE;
        else if ((Index % 2) == 0)
            Setting = PopPpmSettingTable[Index / 2].Guid;
        else
            Setting = PopPpmSettingTable[Index / 2].Guid1;

        if (Setting == NULL)
            continue;

        Status = PoRegisterPowerSettingCallback(NULL, Setting, PopPpmSettingCallback, NULL, NULL);
        if (!NT_SUCCESS(Status))
            DPRINT1("Processor setting %lu is not watched: 0x%08lx\n", Index, Status);
    }

    KeWaitForSingleObject(&PopPpmMutex, Executive, KernelMode, FALSE, NULL);
    PopPpmApplySettings();
    KeReleaseMutex(&PopPpmMutex, FALSE);
}
