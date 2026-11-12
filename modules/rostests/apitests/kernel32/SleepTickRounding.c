/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a short sleep ends on the first clock tick after its interval
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define SLEEP_SAMPLES 5
#define SLEEP_INTERVAL_MS 10
#define SLEEP_LONG_SAMPLES 3
#define SLEEP_LONG_INTERVAL_MS 20

static
double
ShortestSleep(
    _In_ DWORD Interval,
    _In_ ULONG Samples,
    _In_ double FrequencyMs)
{
    LARGE_INTEGER Before, After;
    double ElapsedMs, Shortest = 0;
    ULONG Sample;

    for (Sample = 0; Sample < Samples; Sample++)
    {
        QueryPerformanceCounter(&Before);
        Sleep(Interval);
        QueryPerformanceCounter(&After);

        ElapsedMs = (After.QuadPart - Before.QuadPart) / FrequencyMs;
        if ((Sample == 0) || (ElapsedMs < Shortest))
            Shortest = ElapsedMs;
    }

    return Shortest;
}

START_TEST(SleepTickRounding)
{
    LARGE_INTEGER Frequency;
    DWORD Adjustment, Increment;
    BOOL AdjustmentDisabled;
    double TickMs, ElapsedMs;

    ok(QueryPerformanceFrequency(&Frequency), "QueryPerformanceFrequency failed\n");
    ok(GetSystemTimeAdjustment(&Adjustment, &Increment, &AdjustmentDisabled), "GetSystemTimeAdjustment failed: %lu\n", GetLastError());
    if (Frequency.QuadPart == 0 || Increment == 0)
        return;

    TickMs = Increment / 10000.0;
    if (TickMs <= SLEEP_INTERVAL_MS)
    {
        skip("The clock ticks every %.3f ms, which a %u ms sleep does not stay under\n", TickMs, SLEEP_INTERVAL_MS);
        return;
    }

    /* A wake from a one-millisecond sleep lands on a tick, so each timed sleep starts just after one */
    Sleep(1);

    /* A virtual machine's host delivers some clock interrupts late, so the shortest of several sleeps is the one that shows the rounding */
    ElapsedMs = ShortestSleep(SLEEP_INTERVAL_MS, SLEEP_SAMPLES, Frequency.QuadPart / 1000.0);
    ok(ElapsedMs < TickMs * 1.5, "Sleep(%u) took at least %.2f ms with a %.3f ms tick\n", SLEEP_INTERVAL_MS, ElapsedMs, TickMs);

    ElapsedMs = ShortestSleep(SLEEP_LONG_INTERVAL_MS, SLEEP_LONG_SAMPLES, Frequency.QuadPart / 1000.0);
    ok(ElapsedMs < TickMs * 2.5, "Sleep(%u) took at least %.2f ms with a %.3f ms tick\n", SLEEP_LONG_INTERVAL_MS, ElapsedMs, TickMs);
}
