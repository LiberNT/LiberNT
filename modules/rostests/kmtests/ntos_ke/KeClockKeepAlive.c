/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the clock counts the periods a processor goes without its own clock interrupt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

START_TEST(KeClockKeepAlive)
{
    KAFFINITY PreviousAffinity;
    ULONG Processor;
    LONG Held;
    LONG Released;
    KIRQL OldIrql;
    PKPRCB Prcb;

    if (KeNumberProcessors < 2)
    {
        skip(FALSE, "One processor: nothing else counts its clock periods\n");
        return;
    }

    /* The clock owner counts the periods; the processor held off must be another one */
    Processor = (ULONG)(KeNumberProcessors - 1);
    PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Processor);
    Prcb = KeGetCurrentPrcb();
#if defined(_M_ARM) || defined(_M_ARM64)
    while (Prcb->ClockOwner && Processor > 0)
    {
        Processor--;
        KeSetSystemAffinityThreadEx((KAFFINITY)1 << Processor);
        Prcb = KeGetCurrentPrcb();
    }
    ok(!Prcb->ClockOwner, "Only the clock owner is left to hold off\n");
#endif

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    KeStallExecutionProcessor(100 * 1000);
    Held = Prcb->ClockKeepAlive;
    KeLowerIrql(OldIrql);

    KeStallExecutionProcessor(50 * 1000);
    Released = Prcb->ClockKeepAlive;

    KeRevertToUserAffinityThreadEx(PreviousAffinity);

    ok(Held >= 1, "Periods counted while the clock was held off: %ld\n", Held);
    ok(Released <= 1, "Periods counted once the clock was taken again: %ld\n", Released);
}
