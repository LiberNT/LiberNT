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

typedef struct _WATCHDOG_TEST_CONTEXT
{
    KEVENT Complete;
    KDPC_WATCHDOG_INFORMATION Before;
    KDPC_WATCHDOG_INFORMATION After;
    NTSTATUS BeforeStatus;
    NTSTATUS AfterStatus;
} WATCHDOG_TEST_CONTEXT;

static
VOID
NTAPI
TestWatchdogDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    WATCHDOG_TEST_CONTEXT *Context = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Context->BeforeStatus = KeQueryDpcWatchdogInformation(&Context->Before);
    Context->AfterStatus = KeQueryDpcWatchdogInformation(&Context->After);
    KeSetEvent(&Context->Complete, IO_NO_INCREMENT, FALSE);
}

START_TEST(KeDpcWatchdog)
{
    KDPC_WATCHDOG_INFORMATION Info;
    NTSTATUS Status;
    KIRQL OldIrql;
    WATCHDOG_TEST_CONTEXT Context;
    KDPC Dpc;

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

    RtlZeroMemory(&Context, sizeof(Context));
    KeInitializeEvent(&Context.Complete, NotificationEvent, FALSE);
    KeInitializeDpc(&Dpc, TestWatchdogDpc, &Context);
    if (!KeInsertQueueDpc(&Dpc, NULL, NULL))
    {
        ok(FALSE, "Failed to queue watchdog test DPC\n");
        return;
    }
    KeWaitForSingleObject(&Context.Complete, Executive, KernelMode, FALSE, NULL);
    KeFlushQueuedDpcs();

    TestTraceWatchdog("DPC start", Context.BeforeStatus, &Context.Before);
    TestTraceWatchdog("DPC end", Context.AfterStatus, &Context.After);
    ok_eq_hex(Context.BeforeStatus, STATUS_SUCCESS);
    ok_eq_hex(Context.AfterStatus, STATUS_SUCCESS);
    if (Context.Before.DpcTimeLimit != 0)
    {
        ok(Context.Before.DpcTimeCount >= Context.After.DpcTimeCount,
           "Single-DPC budget increased: %lu -> %lu\n",
           Context.Before.DpcTimeCount, Context.After.DpcTimeCount);
    }
    if (Context.Before.DpcWatchdogLimit != 0)
    {
        ok(Context.Before.DpcWatchdogCount >= Context.After.DpcWatchdogCount,
           "Cumulative budget increased: %lu -> %lu\n",
           Context.Before.DpcWatchdogCount, Context.After.DpcWatchdogCount);
    }
}
