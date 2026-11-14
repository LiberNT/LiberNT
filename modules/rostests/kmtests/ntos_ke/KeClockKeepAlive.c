/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the clock counts the periods a processor goes without its own clock interrupt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
VOID
NTAPI
WakeWatcherDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    InterlockedExchange((PLONG)DeferredContext, 1);
}

static
VOID
TestSleepingWatcher(VOID)
{
#if defined(_M_ARM) || defined(_M_ARM64)
    PKPRCB Prcbs[MAXIMUM_PROCESSORS];
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Frequency, Start, Now;
    ULONG Processor, Owner, Watcher, Runner;
    volatile LONG Woken = 0;
    LONG Count, Highest = 0;
    BOOLEAN Queued = FALSE;
    KDPC Dpc;

    if (KeNumberProcessors < 3)
    {
        skip(FALSE, "Fewer than three processors: no idle watcher next to a busy one\n");
        return;
    }

    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    Owner = MAXULONG;
    for (Processor = 0; Processor < (ULONG)KeNumberProcessors; Processor++)
    {
        KeSetSystemAffinityThreadEx((KAFFINITY)1 << Processor);
        Prcbs[Processor] = KeGetCurrentPrcb();
        if (Prcbs[Processor]->ClockOwner)
            Owner = Processor;
    }
    if (Owner == MAXULONG)
    {
        KeRevertToUserAffinityThreadEx(PreviousAffinity);
        skip(FALSE, "No processor owns the clock\n");
        return;
    }

    /* The lowest processor besides the owner watches it; another one stays busy so the owner keeps ticking */
    Watcher = (Owner == 0) ? 1 : 0;
    Runner = (Owner == (ULONG)KeNumberProcessors - 1) ? Owner - 1 : (ULONG)KeNumberProcessors - 1;
    ok(Runner != Watcher, "Runner %lu and watcher %lu differ\n", Runner, Watcher);
    KeSetSystemAffinityThreadEx((KAFFINITY)1 << Runner);

    KeInitializeDpc(&Dpc, WakeWatcherDpc, (PVOID)&Woken);
    KeSetImportanceDpc(&Dpc, HighImportance);
    KeSetTargetProcessorDpc(&Dpc, (CCHAR)Watcher);

    Start = KeQueryPerformanceCounter(&Frequency);
    do
    {
        Count = ReadNoFence(&Prcbs[Owner]->ClockKeepAlive);
        if (Count > Highest)
            Highest = Count;
        Now = KeQueryPerformanceCounter(NULL);
        if (!Queued && (Now.QuadPart - Start.QuadPart >= Frequency.QuadPart))
            Queued = KeInsertQueueDpc(&Dpc, NULL, NULL);
    } while (Now.QuadPart - Start.QuadPart < Frequency.QuadPart + Frequency.QuadPart / 10);

    KeFlushQueuedDpcs();
    KeRevertToUserAffinityThreadEx(PreviousAffinity);

    ok(Queued, "The wake-up was queued to processor %lu\n", Watcher);
    ok(Woken != 0, "Processor %lu ran the wake-up\n", Watcher);
    ok(Prcbs[Owner]->ClockOwner, "Processor %lu still owns the clock\n", Owner);
    ok(Highest < 8, "Periods counted against the clock owner when its watcher woke after a second asleep: %ld\n", Highest);
#endif
}

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

    TestSleepingWatcher();
}
