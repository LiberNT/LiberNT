/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The sleep state information levels from user mode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/pofuncs.h>

START_TEST(PoSleepInformation)
{
    ULONG_PTR Handler[8] = { 0 };
    ULONG Logging[64];
    ULONGLONG Time;
    NTSTATUS Status;

    Status = NtPowerInformation(SystemPowerStateHandler, Handler, sizeof(Handler), NULL, 0);
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);
    Status = NtPowerInformation(SystemPowerLoggingEntry, NULL, 0, NULL, 0);
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);

    Status = NtPowerInformation(SystemPowerStateLogging, NULL, 0, Logging, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Logging[0] = 0;
    Status = NtPowerInformation(SystemPowerStateLogging, NULL, 0, Logging, sizeof(Logging));
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Logging[0] >= sizeof(ULONG) && Logging[0] <= sizeof(Logging), "logging length %lu\n", Logging[0]);

    Time = 0;
    Status = NtPowerInformation(LastSleepTime, NULL, 0, &Time, sizeof(Time));
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = NtPowerInformation(LastWakeTime, NULL, 0, &Time, sizeof(Time));
    ok_eq_hex(Status, STATUS_SUCCESS);

    KmtRunKernelTest("PoSleepInformation");
}
