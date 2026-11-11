/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The sleep state handlers, the sleep disable reasons and the last sleep and wake times
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
NTSTATUS
NTAPI
TestSleepHandler(
    _In_opt_ PVOID Context,
    _In_opt_ PENTER_STATE_SYSTEM_HANDLER SystemHandler,
    _In_opt_ PVOID SystemContext,
    _In_ LONG NumberProcessors,
    _In_opt_ LONG volatile *Number)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(SystemHandler);
    UNREFERENCED_PARAMETER(SystemContext);
    UNREFERENCED_PARAMETER(NumberProcessors);
    UNREFERENCED_PARAMETER(Number);
    return STATUS_UNSUCCESSFUL;
}

START_TEST(PoSleepInformation)
{
    POWER_STATE_HANDLER Handler;
    ULONGLONG Time;
    ULONG Output[64];
    ULONG Input = 0;
    NTSTATUS Status;

    RtlZeroMemory(&Handler, sizeof(Handler));
    Handler.Type = PowerStateMaximum;
    Handler.Handler = TestSleepHandler;

    Status = ZwPowerInformation(SystemPowerStateHandler, NULL, 0, NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = ZwPowerInformation(SystemPowerStateHandler, &Handler, sizeof(Handler) - 1, NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = ZwPowerInformation(SystemPowerStateHandler, &Handler, sizeof(Handler), Output, sizeof(ULONG));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = ZwPowerInformation(SystemPowerStateHandler, &Handler, sizeof(Handler), NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Handler.Type = PowerStateSleeping2;
    Handler.Handler = NULL;
    Status = ZwPowerInformation(SystemPowerStateHandler, &Handler, sizeof(Handler), NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = ZwPowerInformation(SystemPowerStateLogging, NULL, 0, NULL, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = ZwPowerInformation(SystemPowerStateLogging, &Input, sizeof(Input), Output, sizeof(Output));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = ZwPowerInformation(SystemPowerStateLogging, NULL, 0, Output, sizeof(ULONG));
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    RtlFillMemory(Output, sizeof(Output), 0xCC);
    Status = ZwPowerInformation(SystemPowerStateLogging, NULL, 0, Output, sizeof(Output));
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Output[0] >= sizeof(ULONG) && Output[0] <= sizeof(Output), "logging length %lu\n", Output[0]);

    Time = 0xCCCCCCCCCCCCCCCCULL;
    Status = ZwPowerInformation(LastSleepTime, NULL, 0, &Time, sizeof(Time));
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Time <= (ULONGLONG)KeQueryInterruptTime(), "last sleep time %I64u is in the future\n", Time);
    Time = 0xCCCCCCCCCCCCCCCCULL;
    Status = ZwPowerInformation(LastWakeTime, NULL, 0, &Time, sizeof(Time));
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Time <= (ULONGLONG)KeQueryInterruptTime(), "last wake time %I64u is in the future\n", Time);
    Status = ZwPowerInformation(LastWakeTime, &Input, sizeof(Input), &Time, sizeof(Time));
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = ZwPowerInformation(LastSleepTime, NULL, 0, &Time, sizeof(ULONG));
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
}
