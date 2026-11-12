/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Idle and performance states registered through ProcessorStateHandler2 are used and reported
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/halfuncs.h>

static volatile LONG IdleCalls;
static volatile LONG PerfCalls;
static volatile UCHAR LastPerfLevel;
static volatile ULONG_PTR LastIdleContext;

static
NTSTATUS
FASTCALL
TestIdleHandler(
    _In_ ULONG_PTR Context,
    _Inout_ PPROCESSOR_IDLE_TIMES IdleTimes)
{
    UNREFERENCED_PARAMETER(IdleTimes);

    LastIdleContext = Context;
    InterlockedIncrement(&IdleCalls);
    HalProcessorIdle();
    return STATUS_SUCCESS;
}

static
NTSTATUS
FASTCALL
TestSetPerfLevel(
    _In_ UCHAR Throttle)
{
    LastPerfLevel = Throttle;
    InterlockedIncrement(&PerfCalls);
    return STATUS_SUCCESS;
}

static
VOID
CheckProcessorZero(
    _In_ PPROCESSOR_POWER_INFORMATION Information,
    _In_ ULONG Size,
    _In_ ULONG MaxIdleState)
{
    NTSTATUS Status;

    Status = ZwPowerInformation(ProcessorInformation, NULL, 0, Information, Size);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok_eq_ulong(Information[0].MaxIdleState, MaxIdleState);
    ok_eq_ulong(Information[0].CurrentIdleState, MaxIdleState);
}

START_TEST(PoProcessorStateHandler)
{
    UCHAR Storage[FIELD_OFFSET(PROCESSOR_STATE_HANDLER2, PerfLevel) + 2 * sizeof(PROCESSOR_PERF_LEVEL)];
    PPROCESSOR_STATE_HANDLER2 Handler = (PPROCESSOR_STATE_HANDLER2)Storage;
    ULONG Size = (ULONG)KeNumberProcessors * sizeof(PROCESSOR_POWER_INFORMATION);
    PPROCESSOR_POWER_INFORMATION Information;
    KAFFINITY Previous;
    LARGE_INTEGER Delay;
    ULONG Tries;
    NTSTATUS Status;

    Information = ExAllocatePoolZero(NonPagedPool, Size, 'hsPP');
    ok(Information != NULL, "No memory for the processor information\n");
    if (Information == NULL)
        return;

    IdleCalls = 0;
    PerfCalls = 0;
    LastPerfLevel = 0;
    LastIdleContext = MAXULONG_PTR;

    /* The registration is for the processor the caller runs on */
    Previous = KeSetSystemAffinityThreadEx((KAFFINITY)1);

    RtlZeroMemory(Storage, sizeof(Storage));
    Handler->NumIdleHandlers = 1;
    Handler->IdleHandler[0].HardwareLatency = 1;
    Handler->IdleHandler[0].Handler = TestIdleHandler;
    Handler->SetPerfLevel = TestSetPerfLevel;
    Handler->NumPerfStates = 2;
    Handler->PerfLevel[0].PercentFrequency = 90;
    Handler->PerfLevel[1].PercentFrequency = 45;

    Status = ZwPowerInformation(ProcessorStateHandler2, Handler, sizeof(Storage), NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);

    if (NT_SUCCESS(Status))
    {
        ok(PerfCalls >= 1, "The performance level was never set\n");
        ok_eq_uint(LastPerfLevel, 90);

        CheckProcessorZero(Information, Size, 1);
        ok_eq_ulong(Information[0].MhzLimit, Information[0].MaxMhz * 90 / 100);

        Delay.QuadPart = -2LL * 10000;
        for (Tries = 0; (Tries < 50) && (IdleCalls == 0); Tries++)
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        ok(IdleCalls != 0, "The registered idle handler never ran\n");
        ok_eq_ulongptr(LastIdleContext, 0);

        RtlZeroMemory(Storage, sizeof(Storage));
        Status = ZwPowerInformation(ProcessorStateHandler2, Handler, FIELD_OFFSET(PROCESSOR_STATE_HANDLER2, PerfLevel), NULL, 0);
        ok_eq_hex(Status, STATUS_SUCCESS);

        CheckProcessorZero(Information, Size, 0);
        ok_eq_ulong(Information[0].MhzLimit, Information[0].MaxMhz);
    }

    Status = ZwPowerInformation(ProcessorStateHandler2, Handler, FIELD_OFFSET(PROCESSOR_STATE_HANDLER2, IdleHandler), NULL, 0);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);

    KeRevertToUserAffinityThreadEx(Previous);
    ExFreePoolWithTag(Information, 'hsPP');
}
