/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DPC watchdog information tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
VOID
TestTraceWatchdog(
    _In_ PCSTR Label,
    _In_ NTSTATUS Status,
    _In_ PKDPC_WATCHDOG_INFORMATION Info)
{
    trace("%s -> %lx time %lu/%lu watchdog %lu/%lu reserved %lx\n",
          Label, Status, Info->DpcTimeCount, Info->DpcTimeLimit,
          Info->DpcWatchdogCount, Info->DpcWatchdogLimit, Info->Reserved);
}

START_TEST(KeDpcWatchdog)
{
    KDPC_WATCHDOG_INFORMATION Info;
    NTSTATUS Status;
    KIRQL OldIrql;

    RtlFillMemory(&Info, sizeof(Info), 0xCC);
    Status = KeQueryDpcWatchdogInformation(&Info);
    TestTraceWatchdog("passive", Status, &Info);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
    ok_eq_hex(Info.DpcTimeLimit, 0xCCCCCCCC);
    ok_eq_hex(Info.DpcWatchdogLimit, 0xCCCCCCCC);

    RtlFillMemory(&Info, sizeof(Info), 0xCC);
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Status = KeQueryDpcWatchdogInformation(&Info);
    KeLowerIrql(OldIrql);
    TestTraceWatchdog("dispatch", Status, &Info);
    ok_eq_hex(Status, STATUS_SUCCESS);
}
