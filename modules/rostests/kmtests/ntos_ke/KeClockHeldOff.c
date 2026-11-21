/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that interrupt time keeps real time while clock interrupts are held off
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
ULONGLONG
ElapsedTime(
    _In_ LARGE_INTEGER Start,
    _In_ LARGE_INTEGER End,
    _In_ LARGE_INTEGER Frequency)
{
    return (ULONGLONG)(End.QuadPart - Start.QuadPart) * 10000000ULL / (ULONGLONG)Frequency.QuadPart;
}

START_TEST(KeClockHeldOff)
{
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Frequency, Start, Now;
    ULONGLONG InterruptStart, InterruptEnd, Elapsed;
    ULONG Increment = KeQueryTimeIncrement();
    ULONG Processor;
    KIRQL OldIrql;

    ok(Increment != 0, "No clock increment\n");
    if (Increment == 0)
        return;

    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    for (Processor = 0; Processor < (ULONG)KeNumberProcessors; Processor++)
    {
        KeSetSystemAffinityThreadEx((KAFFINITY)1 << Processor);

        KeRaiseIrql(HIGH_LEVEL, &OldIrql);
        InterruptStart = KeQueryInterruptTime();
        Start = KeQueryPerformanceCounter(&Frequency);
        do
        {
            Now = KeQueryPerformanceCounter(NULL);
        } while (ElapsedTime(Start, Now, Frequency) < 4ULL * Increment);
        KeLowerIrql(OldIrql);

        KeStallExecutionProcessor(100);
        InterruptEnd = KeQueryInterruptTime();
        Now = KeQueryPerformanceCounter(NULL);
        Elapsed = ElapsedTime(Start, Now, Frequency);

        ok(InterruptEnd - InterruptStart + 2ULL * Increment >= Elapsed,
           "Processor %lu: interrupt time moved %I64u while %I64u passed\n",
           Processor, InterruptEnd - InterruptStart, Elapsed);
        ok(InterruptEnd - InterruptStart <= Elapsed + Increment,
           "Processor %lu: interrupt time moved %I64u, ahead of the %I64u that passed\n",
           Processor, InterruptEnd - InterruptStart, Elapsed);
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}
