/*
 * PROJECT:     LiberNT Processor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Processor power management notifications from the kernel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "processr.h"

#define NDEBUG
#include <debug.h>

#define PROC_PERCENT                100
#define PROC_EPP_MAXIMUM            255
#define PROC_LATENCY_UNITS          10
#define PROC_CPPC_WRITES            5
#define PROC_FEEDBACK_COUNTER       0

static
UCHAR
ProcEnergyPreference(
    _In_ UCHAR Percent)
{
    return (UCHAR)((min((ULONG)Percent, (ULONG)PROC_PERCENT) * PROC_EPP_MAXIMUM) / PROC_PERCENT);
}

static
VOID
ProcQueryConstraints(
    _In_ PPROC_DEVICE Device,
    _Out_ PPEP_PPM_PERF_CONSTRAINTS Constraints)
{
    LONG Thermal = InterlockedCompareExchange(&Device->ThermalLimit, 0, 0);
    ULONG Limit;

    Constraints->GuaranteedPerformanceLimit = Device->Guaranteed;
    Constraints->LimitReasons = 0;

    if ((Device->PerfMethod == ProcPerfPss) && (Device->Ppc != 0))
    {
        Constraints->GuaranteedPerformanceLimit = min(Constraints->GuaranteedPerformanceLimit, Device->Pss[Device->Ppc].Frequency);
        Constraints->LimitReasons |= PERFORMANCE_LIMIT_POWER;
    }

    if ((Thermal >= 0) && (Thermal < PROC_PERCENT))
    {
        Limit = max(Device->Lowest, (ULONG)(((ULONGLONG)Device->Highest * (ULONG)Thermal) / PROC_PERCENT));
        if (Limit < Constraints->GuaranteedPerformanceLimit)
        {
            Constraints->GuaranteedPerformanceLimit = Limit;
            Constraints->LimitReasons |= PERFORMANCE_LIMIT_THERMAL;
        }
    }
}

/* Registers in a communication channel are handed over together; the others are written one by one */
static
VOID
ProcWriteCppc(
    _In_ ULONG Count,
    _In_reads_(Count) PPROC_REGISTER *Registers,
    _In_reads_(Count) const ULONGLONG *Values)
{
    PPROC_REGISTER Pcc[PROC_CPPC_WRITES];
    ULONGLONG PccValues[PROC_CPPC_WRITES];
    ULONG Index, PccCount = 0;

    ASSERT(Count <= PROC_CPPC_WRITES);

    for (Index = 0; Index < Count; Index++)
    {
        if ((Registers[Index]->SpaceId == PROC_SPACE_PCC) &&
            ((PccCount == 0) || (Registers[Index]->AccessSize == Pcc[0]->AccessSize)))
        {
            Pcc[PccCount] = Registers[Index];
            PccValues[PccCount++] = Values[Index];
        }
        else
        {
            (VOID)ProcWriteRegister(Registers[Index], Values[Index]);
        }
    }

    if (!ProcPccWrite(PccCount, Pcc, PccValues))
        DPRINT1("The performance request was not handed over the communication channel\n");
}

static
VOID
ProcSetPerformance(
    _In_ PPROC_DEVICE Device,
    _In_ PPEP_PPM_PERF_SET_V2 Set)
{
    PROC_REGISTER *Registers[PROC_CPPC_WRITES];
    ULONGLONG Values[PROC_CPPC_WRITES];
    PPROC_CPPC Cppc = &Device->Cppc;
    BOOLEAN Autonomous = (Set->DesiredPerformance == 0);
    ULONG Count = 0;

    switch (Device->PerfMethod)
    {
        case ProcPerfHwp:
            ProcArchHwpRequest(&Device->Hwp,
                               (UCHAR)min(Set->MinimumPerformance, 0xFFUL),
                               (UCHAR)min(Set->MaximumPerformance, 0xFFUL),
                               (UCHAR)min(Set->DesiredPerformance, 0xFFUL),
                               ProcEnergyPreference(Set->EnergyPerformancePreference),
                               Device->EppSupported,
                               Autonomous ? (ULONG)min((ULONGLONG)Set->TimeWindow * 1000, (ULONGLONG)MAXULONG) : 0);
            break;

        case ProcPerfCppc:
            if (Cppc->Minimum.Valid)
            {
                Registers[Count] = &Cppc->Minimum;
                Values[Count++] = Set->MinimumPerformance;
            }
            if (Cppc->Maximum.Valid)
            {
                Registers[Count] = &Cppc->Maximum;
                Values[Count++] = Set->MaximumPerformance;
            }
            if (Cppc->EnergyPreference.Valid)
            {
                Registers[Count] = &Cppc->EnergyPreference;
                Values[Count++] = ProcEnergyPreference(Set->EnergyPerformancePreference);
            }
            if (Cppc->Autonomous.Valid)
            {
                Registers[Count] = &Cppc->Autonomous;
                Values[Count++] = Autonomous ? 1 : 0;
            }
            if (!Autonomous)
            {
                Registers[Count] = &Cppc->Desired;
                Values[Count++] = Set->DesiredPerformance;
            }
            ProcWriteCppc(Count, Registers, Values);
            break;

        default:
            ASSERT(FALSE);
            break;
    }
}

static
VOID
ProcSetPerformanceState(
    _In_ PPROC_DEVICE Device,
    _In_ PPEP_PPM_PERF_SET_STATE Set)
{
    ULONG Fastest = max((ULONG)Set->MaximumPerformanceState, Device->Ppc);
    ULONG Slowest = min((ULONG)Set->MinimumPerformanceState, Device->PssCount - 1);
    ULONG State;
    ULONGLONG Status;

    ASSERT(Device->PerfMethod == ProcPerfPss);
    ASSERT(Device->PssCount != 0);

    /* Without hardware that picks its own state, autonomous selection runs at the fastest state allowed */
    State = (Set->DesiredPerformanceState == PEP_SET_STATE_AUTONOMOUS_MODE) ? Fastest : Set->DesiredPerformanceState;
    State = min(max(State, Fastest), max(Slowest, Fastest));

    if (!ProcWriteRegister(&Device->PerfControl, Device->Pss[State].Control))
        return;

    if (!Device->StatusMismatchLogged &&
        ProcReadRegister(&Device->PerfStatus, &Status) &&
        (Status != Device->Pss[State].Status))
    {
        Device->StatusMismatchLogged = TRUE;
        DPRINT1("Processor %lu: state %lu reads back status 0x%I64x, not 0x%lx\n",
                Device->Index, State, Status, Device->Pss[State].Status);
    }
}

static
VOID
ProcExecuteIdle(
    _In_ PPROC_DEVICE Device,
    _Inout_ PPEP_PPM_IDLE_EXECUTE Execute)
{
    PPROC_IDLE_STATE State;

    if (Execute->ProcessorState >= Device->IdleCount)
    {
        Execute->Status = STATUS_INVALID_PARAMETER;
        return;
    }

    State = &Device->Idle[Execute->ProcessorState];
    switch (State->Method)
    {
        case ProcIdleHalt:
            HalProcessorIdle();
            break;

        case ProcIdleFfh:
            ProcArchIdleFfh(State->Hint, &Device->Monitor);
            break;

        case ProcIdleIo:
        case ProcIdleMemory:
            if (State->FlushCaches)
                ProcArchFlushCaches();
            ProcTouchRegister(&State->Register);
            ProcIdleExitWait();
            break;
    }

    Execute->Status = STATUS_SUCCESS;
}

static
VOID
ProcQueryIdleStates(
    _In_ PPROC_DEVICE Device,
    _Inout_ PPEP_PPM_QUERY_IDLE_STATES_V2 Query)
{
    PPEP_PROCESSOR_IDLE_STATE_V2 Out;
    PPROC_IDLE_STATE State;
    ULONG Index;

    ASSERT(Query->Count == Device->IdleCount);

    for (Index = 0; Index < min(Query->Count, Device->IdleCount); Index++)
    {
        State = &Device->Idle[Index];
        Out = &Query->IdleStates[Index];

        RtlZeroMemory(Out, sizeof(*Out));
        Out->Interruptible = 1;
        Out->CacheCoherent = (State->Type < 3);
        Out->ThreadContextRetained = 1;
        Out->CStateType = State->Type;
        Out->Latency = State->Latency * PROC_LATENCY_UNITS;

        /* FIXME: ACPI gives no break-even time; twice the exit latency stands in for it */
        Out->BreakEvenDuration = (State->Method == ProcIdleHalt) ? 0 : 2 * Out->Latency;
    }
}

static
VOID
ProcQueryDomain(
    _In_ PPROC_DEVICE Device,
    _Inout_ PPEP_PPM_QUERY_DOMAIN_INFO Domain)
{
    Domain->CoordinationType = Device->Coordination;
    Domain->IdleProcessorsDiscounted = FALSE;
    Domain->SchedulerDirectedTransitionsSupported = FALSE;
    Domain->AffinitizePerfSet = (Device->PerfMethod == ProcPerfHwp) ||
                                ((Device->PerfMethod == ProcPerfPss) &&
                                 (Device->PerfControl.SpaceId == PROC_SPACE_FIXED_HARDWARE)) ||
                                ((Device->PerfMethod == ProcPerfCppc) &&
                                 (Device->Cppc.Desired.SpaceId == PROC_SPACE_FIXED_HARDWARE));
    Domain->WorstCaseTransitionLatency = Device->TransitionLatency;
    Domain->WorstCaseTransitionOverhead = 0;
}

BOOLEAN
NTAPI
ProcAcceptProcessorNotification(
    _In_ PEPHANDLE Handle,
    _In_ ULONG Notification,
    _Inout_opt_ PVOID Data)
{
    PPROC_DEVICE Device = (PPROC_DEVICE)Handle;
    PPEP_PPM_QUERY_CAPABILITIES Capabilities;
    PPEP_PPM_QUERY_PERF_CAPABILITIES Perf;
    PPEP_PPM_QUERY_DISCRETE_PERF_STATES States;
    PPEP_PPM_QUERY_PERF_EPP_SUPPORT Epp;
    PPEP_PPM_QUERY_PERF_AUTONOMOUS_SUPPORT Autonomous;
    PPEP_PPM_QUERY_FEEDBACK_COUNTERS Counters;
    PPEP_PPM_FEEDBACK_READ Feedback;
    PROC_HWP Hwp;
    ULONG Index;

    ASSERT(Device != NULL);

    if (Data == NULL)
        return FALSE;

    switch (Notification)
    {
        case PEP_NOTIFY_PPM_QUERY_CAPABILITIES:
            Capabilities = Data;
            RtlZeroMemory(Capabilities, sizeof(*Capabilities));
            Capabilities->IdleStateCount = Device->IdleCount;
            Capabilities->PerformanceStatesSupported = (Device->PerfMethod != ProcPerfNone);
            Capabilities->DiscretePerformanceStateCount = (Device->PerfMethod == ProcPerfPss) ? (UCHAR)Device->PssCount : 0;
            Capabilities->FeedbackCounterCount = (Device->Features.Feedback && (Device->PerfMethod != ProcPerfNone)) ? 1 : 0;
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_FEEDBACK_COUNTERS:
            Counters = Data;
            if (!Device->Features.Feedback || (Counters->Count != 1))
                return FALSE;
            RtlZeroMemory(&Counters->Counters[0], sizeof(Counters->Counters[0]));
            Counters->Counters[0].Affinitized = 1;
            Counters->Counters[0].Type = PROCESSOR_FEEDBACK_TYPE_RELATIVE;
            Counters->Counters[0].Counter = PROCESSOR_FEEDBACK_COUNTER_PERFORMANCE;
            Counters->Counters[0].DiscountIdle = 1;
            return TRUE;

        case PEP_NOTIFY_PPM_FEEDBACK_READ:
            Feedback = Data;
            if (!Device->Features.Feedback || (Feedback->CounterIndex != PROC_FEEDBACK_COUNTER))
                return FALSE;
            return ProcArchReadFeedback(&Feedback->NominalCount, &Feedback->ActualCount);

        case PEP_NOTIFY_PPM_PERF_CHECK_COMPLETE:
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_IDLE_STATES_V2:
            if (((PPEP_PPM_QUERY_IDLE_STATES_V2)Data)->Count != Device->IdleCount)
                return FALSE;
            ProcQueryIdleStates(Device, Data);
            return TRUE;

        case PEP_NOTIFY_PPM_IDLE_EXECUTE:
            ProcExecuteIdle(Device, Data);
            return TRUE;

        case PEP_NOTIFY_PPM_IDLE_COMPLETE:
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_PERF_CAPABILITIES:
            if (Device->PerfMethod == ProcPerfNone)
                return FALSE;
            Perf = Data;
            Perf->HighestPerformance = Device->Highest;
            Perf->NominalPerformance = Device->Nominal;
            Perf->LowestNonlinearPerformance = Device->LowestNonlinear;
            Perf->LowestPerformance = Device->Lowest;
            Perf->DomainId = Device->DomainId;
            Perf->DomainMembers = Device->DomainMembers;
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_DISCRETE_PERF_STATES:
            States = Data;
            if ((Device->PerfMethod != ProcPerfPss) || (States->Count != Device->PssCount) || (States->States == NULL))
                return FALSE;
            for (Index = 0; Index < Device->PssCount; Index++)
            {
                RtlZeroMemory(&States->States[Index], sizeof(States->States[Index]));
                States->States[Index].Performance = Device->Pss[Index].Frequency;
                States->States[Index].Frequency = Device->Pss[Index].Frequency;
            }
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_DOMAIN_INFO:
            if (((PPEP_PPM_QUERY_DOMAIN_INFO)Data)->DomainId != Device->DomainId)
                return FALSE;
            ProcQueryDomain(Device, Data);
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_PERF_EPP_SUPPORT:
            Epp = Data;
            Epp->EnergyPerformancePreferenceSupported = Device->EppSupported;
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_AUTONOMOUS_SUPPORT:
            Autonomous = Data;
            Autonomous->AutonomousCapability = Device->AutonomousSupported ?
                                               PROCESSOR_AUTONOMOUS_CAPABILITY_SUPPORTED :
                                               PROCESSOR_AUTONOMOUS_CAPABILITY_UNSUPPORTED;
            return TRUE;

        case PEP_NOTIFY_PPM_PERF_CONSTRAINTS:
            if (Device->PerfMethod == ProcPerfNone)
                return FALSE;
            ProcQueryConstraints(Device, Data);
            return TRUE;

        case PEP_NOTIFY_PPM_PERF_SET:
            if ((Device->PerfMethod != ProcPerfHwp) && (Device->PerfMethod != ProcPerfCppc))
                return FALSE;
            ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
            ProcSetPerformance(Device, Data);
            return TRUE;

        case PEP_NOTIFY_PPM_PERF_SET_STATE:
            if (Device->PerfMethod != ProcPerfPss)
                return FALSE;
            ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
            ProcSetPerformanceState(Device, Data);
            return TRUE;

        case PEP_NOTIFY_PPM_RESUME_FROM_SYSTEM_STATE:
            /* Sleep took the processor's performance configuration away */
            if (Device->PerfMethod == ProcPerfHwp)
                (VOID)ProcArchHwpEnable(&Device->Features, &Hwp);
            else if ((Device->PerfMethod == ProcPerfCppc) && Device->Cppc.Enable.Valid)
            {
                (VOID)ProcWriteRegister(&Device->Cppc.Enable, 1);
            }
            return TRUE;

        default:
            return FALSE;
    }
}

VOID
ProcStartPerformance(
    _Inout_ PPROC_DEVICE Device)
{
    GROUP_AFFINITY Previous;

    PAGED_CODE();

    if ((Device->PerfMethod != ProcPerfCppc) || !Device->Cppc.Enable.Valid)
        return;

    ProcEnterProcessor(Device, &Previous);
    if (!ProcWriteRegister(&Device->Cppc.Enable, 1))
        DPRINT1("Processor %lu: CPPC could not be enabled\n", Device->Index);
    ProcLeaveProcessor(&Previous);
}

VOID
ProcStopPerformance(
    _Inout_ PPROC_DEVICE Device)
{
    GROUP_AFFINITY Previous;

    PAGED_CODE();

    if (Device->PerfMethod != ProcPerfHwp)
        return;

    ProcEnterProcessor(Device, &Previous);
    ProcArchHwpRestore(&Device->Hwp);
    ProcLeaveProcessor(&Previous);
}
