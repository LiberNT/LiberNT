/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops in the debugger with a queued timer, a waited-on event and an interrupt object to look at
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

KTIMER KdbTestTimer;
KDPC KdbTestTimerDpc;
KEVENT KdbTestEvent;
KINTERRUPT KdbTestInterrupt;

static
VOID
NTAPI
KdbTestTimerRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
}

static
BOOLEAN
NTAPI
KdbTestInterruptRoutine(
    _In_ PKINTERRUPT Interrupt,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Interrupt);
    UNREFERENCED_PARAMETER(Context);
    return FALSE;
}

static
VOID
NTAPI
KdbTestWaiter(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    KeWaitForSingleObject(&KdbTestEvent, Executive, KernelMode, FALSE, NULL);
}

START_TEST(KdbSchedulerCommands)
{
    LARGE_INTEGER DueTime;
    LARGE_INTEGER Delay;
    PKTHREAD Waiter;
    ULONG Tries;

    KeInitializeDpc(&KdbTestTimerDpc, KdbTestTimerRoutine, NULL);
    KeInitializeTimer(&KdbTestTimer);
    DueTime.QuadPart = -60LL * 1000 * 1000 * 10;
    KeSetTimer(&KdbTestTimer, DueTime, &KdbTestTimerDpc);

    RtlZeroMemory(&KdbTestInterrupt, sizeof(KdbTestInterrupt));
    KdbTestInterrupt.ServiceRoutine = KdbTestInterruptRoutine;
    KdbTestInterrupt.Vector = 0x51;
    KdbTestInterrupt.Irql = 5;
    KdbTestInterrupt.SynchronizeIrql = 5;
    KdbTestInterrupt.Mode = Latched;

    KeInitializeEvent(&KdbTestEvent, NotificationEvent, FALSE);
    Waiter = KmtStartThread(KdbTestWaiter, NULL);

    Delay.QuadPart = -10LL * 1000;
    for (Tries = 0; (Tries < 100) && IsListEmpty(&KdbTestEvent.Header.WaitListHead); Tries++)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    ok(!IsListEmpty(&KdbTestEvent.Header.WaitListHead), "The waiter never waited\n");

    /* The debugger's start-up file looks at the scheduler here */
    DbgBreakPoint();

    KeSetEvent(&KdbTestEvent, IO_NO_INCREMENT, FALSE);
    KmtFinishThread(Waiter, NULL);
    KeCancelTimer(&KdbTestTimer);
}
