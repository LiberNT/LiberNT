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
    static const ULONG Periods[] = { 4, 17 };
    KAFFINITY PreviousAffinity;
    LARGE_INTEGER Frequency, Start, Now;
    ULONGLONG InterruptStart, InterruptEnd, Elapsed;
    ULONG Increment = KeQueryTimeIncrement();
    ULONG Index;
    KIRQL OldIrql;

    ok(Increment != 0, "No clock increment\n");
    if (Increment == 0)
        return;

    /* Only the processor that takes the clock interrupt is checked: on the others Windows 11 leaves interrupt time several periods stale */
    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    for (Index = 0; Index < RTL_NUMBER_OF(Periods); Index++)
    {
        /* Each hold starts on a tick: started later in a period, it would also measure how late that period's tick is delivered */
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        InterruptStart = KeQueryInterruptTime();
        while (KeQueryInterruptTime() == InterruptStart)
            YieldProcessor();
        KfRaiseIrql(HIGH_LEVEL);
        InterruptStart = KeQueryInterruptTime();
        Start = KeQueryPerformanceCounter(&Frequency);
        do
        {
            Now = KeQueryPerformanceCounter(NULL);
        } while (ElapsedTime(Start, Now, Frequency) < (ULONGLONG)Periods[Index] * Increment);
        KeLowerIrql(OldIrql);

        KeStallExecutionProcessor(100);
        InterruptEnd = KeQueryInterruptTime();
        Now = KeQueryPerformanceCounter(NULL);
        Elapsed = ElapsedTime(Start, Now, Frequency);

        ok(InterruptEnd - InterruptStart + 2ULL * Increment >= Elapsed,
           "%lu periods held: interrupt time moved %I64u while %I64u passed\n",
           Periods[Index], InterruptEnd - InterruptStart, Elapsed);
        ok(InterruptEnd - InterruptStart <= Elapsed + Increment,
           "%lu periods held: interrupt time moved %I64u, ahead of the %I64u that passed\n",
           Periods[Index], InterruptEnd - InterruptStart, Elapsed);
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}
