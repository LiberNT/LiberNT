/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops in the debugger with a waiting thread and a script in memory to look at
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

ULONG_PTR KdbTestWaiterId;
PVOID KdbTestWaiterThread;
CHAR KdbTestScript[] = "echo first line of the script\r\n# a remark\r\n\r\necho last line of the script";
ULONG_PTR KdbTestScriptLength = sizeof(KdbTestScript) - 1;

static KEVENT KdbTestWakeEvent;

static
VOID
NTAPI
KdbTestWaitingThread(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    KeWaitForSingleObject(&KdbTestWakeEvent, Executive, KernelMode, FALSE, NULL);
}

START_TEST(KdbCliCommands)
{
    LARGE_INTEGER Delay;
    PKTHREAD Waiter;
    ULONG Tries;

    KeInitializeEvent(&KdbTestWakeEvent, NotificationEvent, FALSE);
    Waiter = KmtStartThread(KdbTestWaitingThread, NULL);
    ok(Waiter != NULL, "The waiting thread did not start\n");
    if (Waiter == NULL)
        return;

    Delay.QuadPart = -10LL * 1000;
    for (Tries = 0; (Tries < 100) && IsListEmpty(&KdbTestWakeEvent.Header.WaitListHead); Tries++)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    ok(!IsListEmpty(&KdbTestWakeEvent.Header.WaitListHead), "The thread never waited\n");

    KdbTestWaiterThread = Waiter;
    KdbTestWaiterId = (ULONG_PTR)PsGetThreadId((PETHREAD)Waiter);
    trace("Waiting thread %p, id %Ix\n", KdbTestWaiterThread, KdbTestWaiterId);

    /* The debugger's start-up file looks at the waiting thread and runs the script here */
    DbgBreakPoint();

    KeSetEvent(&KdbTestWakeEvent, IO_NO_INCREMENT, FALSE);
    KmtFinishThread(Waiter, NULL);
}
