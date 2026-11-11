/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Holds off the clock on one processor until the system stops with CLOCK_WATCHDOG_TIMEOUT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

START_TEST(KeClockWatchdog)
{
    KAFFINITY PreviousAffinity;
    ULONG Seconds;
    KIRQL OldIrql;

    if (KeNumberProcessors < 2)
    {
        skip(FALSE, "One processor: nothing else watches its clock\n");
        return;
    }

    PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << (KeNumberProcessors - 1));

    /* The output keeps the run harness from taking the silence for a hang */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    for (Seconds = 0; Seconds < 20; Seconds++)
    {
        DbgPrint("KeClockWatchdog: clock held off for %lu s\n", Seconds);
        KeStallExecutionProcessor(1000 * 1000);
    }
    KeLowerIrql(OldIrql);

    KeRevertToUserAffinityThreadEx(PreviousAffinity);
    ok(FALSE, "The system did not stop after %lu s without clock interrupts\n", Seconds);
}
