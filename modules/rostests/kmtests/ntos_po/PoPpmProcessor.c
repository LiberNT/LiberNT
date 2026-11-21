/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A processor registered with the processor power manager is idled and run through its states
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/halfuncs.h>
#include <ndk/ketypes.h>
#include <ntpoapi.h>
#include <pepfx.h>
#include <drivers/ppmproc.h>
#include "../../../../ntos/po/ppmcore.h"

#define TEST_DOMAIN             0x7E000000
#define TEST_STATES             3
#define TEST_WAIT_STEPS         50
#define TEST_WAIT_STEP_MS       20
#define TEST_PARKING_MAX        3
#define TEST_PARKING_WAIT_STEPS 100

static const ULONG TestPerformance[TEST_STATES] = { 2001, 2000, 1000 };

static const GUID TestThrottleMaximum =
    { 0xbc5038f7, 0x23e0, 0x4960, { 0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec } };

static const GUID TestParkingMinCores =
    { 0x0cc5b647, 0xc1df, 0x4637, { 0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83 } };

static const GUID TestParkingMaxCores =
    { 0xea062031, 0x0e34, 0x4ff1, { 0x9b, 0x6d, 0xeb, 0x10, 0x59, 0x33, 0x40, 0x28 } };

typedef struct _TEST_PROCESSOR
{
    ULONG Index;
    volatile LONG Queries;
    volatile LONG QueriesNotPassive;
    volatile LONG IdleExecutes;
    volatile LONG IdleCompletes;
    volatile LONG IdleWrongState;
    volatile LONG WrongProcessor;
    volatile LONG NotDispatch;
    volatile LONG PerfSets;
    volatile LONG LastSet;
    volatile LONG ConstraintQueries;
    ULONG GuaranteedLimit;
    ULONG LimitReasons;
} TEST_PROCESSOR, *PTEST_PROCESSOR;

static TEST_PROCESSOR TestProcessor;

static
VOID
TestQuery(
    _Inout_ PTEST_PROCESSOR Test)
{
    InterlockedIncrement(&Test->Queries);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        InterlockedIncrement(&Test->QueriesNotPassive);
}

static
VOID
TestOnProcessor(
    _Inout_ PTEST_PROCESSOR Test)
{
    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        InterlockedIncrement(&Test->NotDispatch);
    if (KeGetCurrentProcessorNumberEx(NULL) != Test->Index)
        InterlockedIncrement(&Test->WrongProcessor);
}

static
BOOLEAN
NTAPI
TestAcceptProcessorNotification(
    _In_ PEPHANDLE Handle,
    _In_ ULONG Notification,
    _Inout_opt_ PVOID Data)
{
    PTEST_PROCESSOR Test = (PTEST_PROCESSOR)Handle;
    PPEP_PPM_QUERY_CAPABILITIES Capabilities;
    PPEP_PPM_QUERY_IDLE_STATES_V2 IdleStates;
    PPEP_PPM_IDLE_EXECUTE Execute;
    PPEP_PPM_IDLE_COMPLETE Complete;
    PPEP_PPM_QUERY_PERF_CAPABILITIES Perf;
    PPEP_PPM_QUERY_DISCRETE_PERF_STATES States;
    PPEP_PPM_QUERY_DOMAIN_INFO Domain;
    PPEP_PPM_PERF_CONSTRAINTS Constraints;
    PEP_PPM_PERF_SET_STATE Set;
    LONG Packed;
    ULONG Index;

    if (Data == NULL)
        return FALSE;

    switch (Notification)
    {
        case PEP_NOTIFY_PPM_QUERY_CAPABILITIES:
            TestQuery(Test);
            Capabilities = Data;
            Capabilities->IdleStateCount = 1;
            Capabilities->PerformanceStatesSupported = TRUE;
            Capabilities->DiscretePerformanceStateCount = TEST_STATES;
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_IDLE_STATES_V2:
            TestQuery(Test);
            IdleStates = Data;
            if (IdleStates->Count != 1)
                return FALSE;
            RtlZeroMemory(&IdleStates->IdleStates[0], sizeof(IdleStates->IdleStates[0]));
            IdleStates->IdleStates[0].Interruptible = 1;
            IdleStates->IdleStates[0].CacheCoherent = 1;
            IdleStates->IdleStates[0].ThreadContextRetained = 1;
            IdleStates->IdleStates[0].CStateType = 1;
            IdleStates->IdleStates[0].Latency = 1;
            return TRUE;

        case PEP_NOTIFY_PPM_IDLE_EXECUTE:
            TestOnProcessor(Test);
            Execute = Data;
            if ((Execute->ProcessorState != 0) || (Execute->PlatformState != PEP_PLATFORM_IDLE_STATE_NONE))
                InterlockedIncrement(&Test->IdleWrongState);
            InterlockedIncrement(&Test->IdleExecutes);
            HalProcessorIdle();
            Execute->Status = STATUS_SUCCESS;
            return TRUE;

        case PEP_NOTIFY_PPM_IDLE_COMPLETE:
            TestOnProcessor(Test);
            Complete = Data;
            if (Complete->ProcessorState != 0)
                InterlockedIncrement(&Test->IdleWrongState);
            InterlockedIncrement(&Test->IdleCompletes);
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_PERF_CAPABILITIES:
            TestQuery(Test);
            Perf = Data;
            Perf->HighestPerformance = TestPerformance[0];
            Perf->NominalPerformance = TestPerformance[1];
            Perf->LowestNonlinearPerformance = TestPerformance[TEST_STATES - 1];
            Perf->LowestPerformance = TestPerformance[TEST_STATES - 1];
            Perf->DomainId = TEST_DOMAIN | Test->Index;
            Perf->DomainMembers = 1;
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_DISCRETE_PERF_STATES:
            TestQuery(Test);
            States = Data;
            if ((States->Count != TEST_STATES) || (States->States == NULL))
                return FALSE;
            for (Index = 0; Index < TEST_STATES; Index++)
            {
                RtlZeroMemory(&States->States[Index], sizeof(States->States[Index]));
                States->States[Index].Performance = TestPerformance[Index];
                States->States[Index].Frequency = TestPerformance[Index];
            }
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_DOMAIN_INFO:
            TestQuery(Test);
            Domain = Data;
            Domain->CoordinationType = PROCESSOR_DOMAIN_COORDINATION_HW_ALL;
            Domain->AffinitizePerfSet = TRUE;
            return TRUE;

        case PEP_NOTIFY_PPM_PERF_CONSTRAINTS:
            TestQuery(Test);
            InterlockedIncrement(&Test->ConstraintQueries);
            Constraints = Data;
            Constraints->GuaranteedPerformanceLimit = Test->GuaranteedLimit;
            Constraints->LimitReasons = Test->LimitReasons;
            return TRUE;

        case PEP_NOTIFY_PPM_PERF_SET_STATE:
            TestOnProcessor(Test);
            Set = *(PPEP_PPM_PERF_SET_STATE)Data;
            RtlCopyMemory(&Packed, &Set, sizeof(Packed));
            InterlockedExchange(&Test->LastSet, Packed);
            InterlockedIncrement(&Test->PerfSets);
            return TRUE;

        default:
            return FALSE;
    }
}

static
NTSTATUS
TestRequest(
    _In_ PPM_PROCESSOR_REQUEST_TYPE Type,
    _In_ ULONG Index,
    _In_opt_ PEPHANDLE Handle,
    _In_opt_ PPEPCALLBACKNOTIFYPPM Callback)
{
    PPM_PROCESSOR_REQUEST Request;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Signature = PPM_PROCESSOR_REQUEST_SIGNATURE;
    Request.Size = sizeof(Request);
    Request.Type = Type;
    Request.Handle = Handle;
    Request.AcceptProcessorNotification = Callback;
    if (!NT_SUCCESS(KeGetProcessorNumberFromIndex(Index, &Request.Processor)))
        Request.Processor.Group = MAXUSHORT;

    return ZwPowerInformation(PowerInformationInternal, &Request, sizeof(Request), NULL, 0);
}

static
NTSTATUS
TestSetSetting(
    _In_ const GUID *Guid,
    _In_ ULONG Percent)
{
    UCHAR Storage[FIELD_OFFSET(SET_POWER_SETTING_VALUE, Data) + sizeof(ULONG)];
    PSET_POWER_SETTING_VALUE Setting = (PSET_POWER_SETTING_VALUE)Storage;
    SYSTEM_POWER_CONDITION Condition;
    NTSTATUS Status = STATUS_SUCCESS;

    for (Condition = PoAc; (Condition <= PoDc) && NT_SUCCESS(Status); Condition++)
    {
        RtlZeroMemory(Storage, sizeof(Storage));
        Setting->Version = POWER_SETTING_VALUE_VERSION;
        Setting->Guid = *Guid;
        Setting->PowerCondition = Condition;
        Setting->DataLength = sizeof(ULONG);
        RtlCopyMemory(Setting->Data, &Percent, sizeof(Percent));
        Status = ZwPowerInformation(SetPowerSettingValue, Setting, sizeof(Storage), NULL, 0);
    }

    return Status;
}

static
NTSTATUS
TestSetThrottleMaximum(
    _In_ ULONG Percent)
{
    return TestSetSetting(&TestThrottleMaximum, Percent);
}

static
PEP_PPM_PERF_SET_STATE
TestLastSet(VOID)
{
    PEP_PPM_PERF_SET_STATE Set;
    LONG Packed = InterlockedCompareExchange(&TestProcessor.LastSet, 0, 0);

    RtlCopyMemory(&Set, &Packed, sizeof(Set));
    return Set;
}

/* The performance check runs every few tens of milliseconds and only sends what changed */
static
BOOLEAN
TestWaitForMaximumState(
    _In_ UCHAR State)
{
    LARGE_INTEGER Delay;
    ULONG Step;

    Delay.QuadPart = -(LONGLONG)TEST_WAIT_STEP_MS * 10000;
    for (Step = 0; Step < TEST_WAIT_STEPS; Step++)
    {
        if ((TestProcessor.PerfSets != 0) && (TestLastSet().MaximumPerformanceState == State))
            return TRUE;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    return FALSE;
}

static
VOID
TestProcessorInformation(
    _In_ ULONG Index,
    _In_ ULONG MaxIdleState,
    _In_ ULONG LimitPercent)
{
    ULONG Size = (ULONG)KeNumberProcessors * sizeof(PROCESSOR_POWER_INFORMATION);
    PPROCESSOR_POWER_INFORMATION Information;
    NTSTATUS Status;

    Information = ExAllocatePoolZero(NonPagedPool, Size, 'mpPT');
    ok(Information != NULL, "No memory for the processor information\n");
    if (Information == NULL)
        return;

    Status = ZwPowerInformation(ProcessorInformation, NULL, 0, Information, Size);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_ulong(Information[Index].MaxIdleState, MaxIdleState);
        ok_eq_ulong(Information[Index].MhzLimit, Information[Index].MaxMhz * LimitPercent / 100);
    }

    ExFreePoolWithTag(Information, 'mpPT');
}

static
VOID
TestMalformedRequests(VOID)
{
    PPM_PROCESSOR_REQUEST Request;
    NTSTATUS Status;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Signature = PPM_PROCESSOR_REQUEST_SIGNATURE;
    Request.Size = sizeof(Request);
    Request.Type = PpmProcessorRegister;
    Request.Handle = (PEPHANDLE)&TestProcessor;
    Request.AcceptProcessorNotification = TestAcceptProcessorNotification;
    Status = KeGetProcessorNumberFromIndex(0, &Request.Processor);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Request.Signature = 0;
    Status = ZwPowerInformation(PowerInformationInternal, &Request, sizeof(Request), NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Request.Signature = PPM_PROCESSOR_REQUEST_SIGNATURE;

    Request.Size = sizeof(Request) - 1;
    Status = ZwPowerInformation(PowerInformationInternal, &Request, sizeof(Request), NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Request.Size = sizeof(Request);

    Status = ZwPowerInformation(PowerInformationInternal, &Request, sizeof(Request) - 1, NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = ZwPowerInformation(PowerInformationInternal, &Request, sizeof(Request), &Request, sizeof(Request));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = ZwPowerInformation(PowerInformationInternal, NULL, 0, NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Request.Type = 0;
    Status = ZwPowerInformation(PowerInformationInternal, &Request, sizeof(Request), NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Request.Type = PpmProcessorRequestMaximum;
    Status = ZwPowerInformation(PowerInformationInternal, &Request, sizeof(Request), NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = TestRequest(PpmProcessorRegister, (ULONG)KeNumberProcessors, (PEPHANDLE)&TestProcessor,
                         TestAcceptProcessorNotification);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = TestRequest(PpmProcessorRegister, 0, (PEPHANDLE)&TestProcessor, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}

static
VOID
TestParkingDecision(VOID)
{
    POP_PPM_PARKING_INPUT Input;

    ok_eq_ulong(PopPpmParkingBound(4, 10), 1UL);
    ok_eq_ulong(PopPpmParkingBound(8, 10), 1UL);
    ok_eq_ulong(PopPpmParkingBound(16, 10), 2UL);
    ok_eq_ulong(PopPpmParkingBound(4, 0), 0UL);
    ok_eq_ulong(PopPpmParkingBound(4, 150), 4UL);

    RtlZeroMemory(&Input, sizeof(Input));
    Input.Count = 4;
    Input.Unparked = 4;
    Input.MinCores = 10;
    Input.MaxCores = 100;
    Input.Headroom = 20;
    ok_eq_ulong(PopPpmParkingTarget(&Input), 1UL);

    Input.Load = 150;
    ok_eq_ulong(PopPpmParkingTarget(&Input), 2UL);

    Input.Load = 400;
    ok_eq_ulong(PopPpmParkingTarget(&Input), 4UL);

    Input.Load = 100;
    Input.Concurrent = 3;
    ok_eq_ulong(PopPpmParkingTarget(&Input), 3UL);

    Input.Concurrent = 0;
    Input.Load = 90;
    Input.Unparked = 2;
    Input.OverUtilized = 1;
    ok_eq_ulong(PopPpmParkingTarget(&Input), 3UL);

    Input.OverUtilized = 0;
    Input.Load = 0;
    Input.MinCores = 100;
    ok_eq_ulong(PopPpmParkingTarget(&Input), 4UL);

    Input.MinCores = 0;
    ok_eq_ulong(PopPpmParkingTarget(&Input), 0UL);

    ok_eq_ulong(PopPpmParkingStep(2, 4, 1, 4, CORE_PARKING_POLICY_CHANGE_IDEAL), 4UL);
    ok_eq_ulong(PopPpmParkingStep(2, 4, 1, 4, CORE_PARKING_POLICY_CHANGE_SINGLE), 3UL);
    ok_eq_ulong(PopPpmParkingStep(2, 4, 1, 4, CORE_PARKING_POLICY_CHANGE_MULTISTEP), 3UL);
    ok_eq_ulong(PopPpmParkingStep(2, 3, 1, 4, CORE_PARKING_POLICY_CHANGE_ROCKET), 4UL);
    ok_eq_ulong(PopPpmParkingStep(3, 2, 1, 4, CORE_PARKING_POLICY_CHANGE_SINGLE), 2UL);
    ok_eq_ulong(PopPpmParkingStep(3, 2, 1, 4, CORE_PARKING_POLICY_CHANGE_ROCKET), 1UL);
}

static
BOOLEAN
NTAPI
TestParkingAcceptProcessorNotification(
    _In_ PEPHANDLE Handle,
    _In_ ULONG Notification,
    _Inout_opt_ PVOID Data)
{
    PPEP_PPM_QUERY_IDLE_STATES_V2 IdleStates;

    UNREFERENCED_PARAMETER(Handle);

    if (Data == NULL)
        return FALSE;

    switch (Notification)
    {
        case PEP_NOTIFY_PPM_QUERY_CAPABILITIES:
            ((PPEP_PPM_QUERY_CAPABILITIES)Data)->IdleStateCount = 1;
            return TRUE;

        case PEP_NOTIFY_PPM_QUERY_IDLE_STATES_V2:
            IdleStates = Data;
            if (IdleStates->Count != 1)
                return FALSE;
            RtlZeroMemory(&IdleStates->IdleStates[0], sizeof(IdleStates->IdleStates[0]));
            IdleStates->IdleStates[0].Interruptible = 1;
            IdleStates->IdleStates[0].CacheCoherent = 1;
            IdleStates->IdleStates[0].ThreadContextRetained = 1;
            IdleStates->IdleStates[0].CStateType = 1;
            return TRUE;

        case PEP_NOTIFY_PPM_IDLE_EXECUTE:
            HalProcessorIdle();
            ((PPEP_PPM_IDLE_EXECUTE)Data)->Status = STATUS_SUCCESS;
            return TRUE;

        case PEP_NOTIFY_PPM_IDLE_COMPLETE:
            return TRUE;

        default:
            return FALSE;
    }
}

static
BOOLEAN
TestWaitForUnparked(
    _In_ USHORT Expected)
{
    LARGE_INTEGER Delay;
    ULONG Step;

    Delay.QuadPart = -(LONGLONG)TEST_WAIT_STEP_MS * 10000;
    for (Step = 0; Step < TEST_PARKING_WAIT_STEPS; Step++)
    {
        if (SharedUserData->UnparkedProcessorCount == Expected)
            return TRUE;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    return FALSE;
}

/* Idle processors registered with the power manager are parked down to the minimum share of cores, and come back when they leave */
static
VOID
TestParking(VOID)
{
    ULONG Registered[TEST_PARKING_MAX];
    ULONG Count = 0, Index;
    USHORT Before;
    NTSTATUS Status;

    Before = SharedUserData->UnparkedProcessorCount;
    Status = TestSetSetting(&TestParkingMinCores, 10);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = TestSetSetting(&TestParkingMaxCores, 10);
    ok_eq_hex(Status, STATUS_SUCCESS);

    for (Index = (ULONG)KeNumberProcessors; (Index-- != 0) && (Count < TEST_PARKING_MAX);)
    {
        Status = TestRequest(PpmProcessorRegister, Index, (PEPHANDLE)(ULONG_PTR)(Index + 1),
                             TestParkingAcceptProcessorNotification);
        if (NT_SUCCESS(Status))
            Registered[Count++] = Index;
    }

    if (Count < 2)
    {
        skip(FALSE, "Only %lu processors are free to park\n", Count);
    }
    else
    {
        ok(TestWaitForUnparked((USHORT)(Before - (Count - 1))),
           "%u of %u processors are unparked with %lu idle ones registered\n",
           SharedUserData->UnparkedProcessorCount, Before, Count);
    }

    for (Index = 0; Index < Count; Index++)
    {
        Status = TestRequest(PpmProcessorUnregister, Registered[Index], (PEPHANDLE)(ULONG_PTR)(Registered[Index] + 1),
                             TestParkingAcceptProcessorNotification);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    ok(TestWaitForUnparked(Before), "%u of %u processors are unparked after the registrations ended\n",
       SharedUserData->UnparkedProcessorCount, Before);

    Status = TestSetSetting(&TestParkingMaxCores, 100);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

START_TEST(PoPpmProcessor)
{
    GROUP_AFFINITY Affinity, Previous;
    PROCESSOR_NUMBER Number;
    PEP_PPM_PERF_SET_STATE Set;
    LARGE_INTEGER Delay;
    LONG Executes, Completes, PerfSets;
    ULONG Index;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    TestMalformedRequests();
    TestParkingDecision();
    TestParking();

    /* The processor driver may already manage every processor; one it does not is needed */
    RtlZeroMemory(&TestProcessor, sizeof(TestProcessor));
    for (Index = (ULONG)KeNumberProcessors; Index-- != 0;)
    {
        TestProcessor.Index = Index;
        TestProcessor.GuaranteedLimit = TestPerformance[1];
        Status = TestRequest(PpmProcessorRegister, Index, (PEPHANDLE)&TestProcessor, TestAcceptProcessorNotification);
        if (Status != STATUS_ALREADY_REGISTERED)
            break;
    }

    if (Status == STATUS_ALREADY_REGISTERED)
    {
        skip(FALSE, "Every processor is managed by the processor driver\n");
        return;
    }

    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    trace("Testing processor %lu\n", TestProcessor.Index);

    /* A thread can be pinned to the processor by group; the export exists on every architecture */
    Status = KeGetProcessorNumberFromIndex(TestProcessor.Index, &Number);
    ok_eq_hex(Status, STATUS_SUCCESS);
    RtlZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Group = Number.Group;
    Affinity.Mask = (KAFFINITY)1 << Number.Number;
    KeSetSystemGroupAffinityThread(&Affinity, &Previous);
    ok_eq_ulong(KeGetCurrentProcessorNumberEx(NULL), TestProcessor.Index);
    KeRevertToUserGroupAffinityThread(&Previous);

    ok(TestProcessor.Queries >= 5, "Only %ld queries were made\n", TestProcessor.Queries);
    ok_eq_long(TestProcessor.QueriesNotPassive, 0L);

    Status = TestRequest(PpmProcessorRegister, TestProcessor.Index, (PEPHANDLE)&TestProcessor,
                         TestAcceptProcessorNotification);
    ok_eq_hex(Status, STATUS_ALREADY_REGISTERED);

    Status = TestRequest(PpmProcessorUnregister, TestProcessor.Index, (PEPHANDLE)&Previous,
                         TestAcceptProcessorNotification);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = TestSetThrottleMaximum(100);
    ok_eq_hex(Status, STATUS_SUCCESS);

    ok(TestWaitForMaximumState(0), "The fastest state was never allowed\n");
    Set = TestLastSet();
    ok_eq_uint(Set.MinimumPerformanceState, TEST_STATES - 1);
    ok(Set.DesiredPerformanceState < TEST_STATES, "Desired state %u is out of range\n", Set.DesiredPerformanceState);

    Delay.QuadPart = -(LONGLONG)TEST_WAIT_STEP_MS * 10000;
    for (Index = 0; (Index < TEST_WAIT_STEPS) && (TestProcessor.IdleCompletes == 0); Index++)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    ok(TestProcessor.IdleExecutes != 0, "The processor never idled through its registration\n");
    ok(TestProcessor.IdleCompletes != 0, "No idle transition was completed\n");
    ok_eq_long(TestProcessor.IdleWrongState, 0L);
    TestProcessorInformation(TestProcessor.Index, 1, 100);

    /* 99% of nominal performance leaves only the states below it, and no boosting */
    Status = TestSetThrottleMaximum(99);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(TestWaitForMaximumState(TEST_STATES - 1), "The maximum processor state was not applied\n");
    TestProcessorInformation(TestProcessor.Index, 1, 99);

    Status = TestSetThrottleMaximum(100);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(TestWaitForMaximumState(0), "The maximum processor state was not lifted\n");

    TestProcessor.GuaranteedLimit = TestPerformance[TEST_STATES - 1];
    TestProcessor.LimitReasons = PERFORMANCE_LIMIT_THERMAL;
    Status = TestRequest(PpmProcessorPerfConstraintsChanged, TestProcessor.Index, (PEPHANDLE)&TestProcessor,
                         TestAcceptProcessorNotification);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(TestProcessor.ConstraintQueries >= 2, "The constraints were not queried again\n");
    ok(TestWaitForMaximumState(TEST_STATES - 1), "The thermal limit was not applied\n");
    TestProcessorInformation(TestProcessor.Index, 1, 50);

    TestProcessor.GuaranteedLimit = TestPerformance[1];
    TestProcessor.LimitReasons = 0;
    Status = TestRequest(PpmProcessorPerfConstraintsChanged, TestProcessor.Index, (PEPHANDLE)&TestProcessor,
                         TestAcceptProcessorNotification);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(TestWaitForMaximumState(0), "The thermal limit was not lifted\n");

    Status = TestRequest(PpmProcessorUnregister, TestProcessor.Index, (PEPHANDLE)&TestProcessor,
                         TestAcceptProcessorNotification);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Executes = TestProcessor.IdleExecutes;
    Completes = TestProcessor.IdleCompletes;
    PerfSets = TestProcessor.PerfSets;
    ok_eq_long(Completes, Executes);

    Delay.QuadPart = -200LL * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    ok_eq_long(TestProcessor.IdleExecutes, Executes);
    ok_eq_long(TestProcessor.IdleCompletes, Completes);
    ok_eq_long(TestProcessor.PerfSets, PerfSets);
    ok_eq_long(TestProcessor.WrongProcessor, 0L);
    ok_eq_long(TestProcessor.NotDispatch, 0L);
    TestProcessorInformation(TestProcessor.Index, 1, 100);

    Status = TestRequest(PpmProcessorUnregister, TestProcessor.Index, (PEPHANDLE)&TestProcessor,
                         TestAcceptProcessorNotification);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = TestRequest(PpmProcessorPerfConstraintsChanged, TestProcessor.Index, (PEPHANDLE)&TestProcessor,
                         TestAcceptProcessorNotification);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}
