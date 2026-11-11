/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a short sleep ends on the first clock tick after its interval
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define SLEEP_SAMPLES 3
#define SLEEP_INTERVAL_MS 10
#define SLEEP_LONG_SAMPLES 2
#define SLEEP_LONG_INTERVAL_MS 20

START_TEST(SleepTickRounding)
{
    LARGE_INTEGER Frequency, Before, After;
    DWORD Adjustment, Increment;
    BOOL AdjustmentDisabled;
    double TickMs, ElapsedMs;
    ULONG Sample;

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

    for (Sample = 0; Sample < SLEEP_SAMPLES; Sample++)
    {
        QueryPerformanceCounter(&Before);
        Sleep(SLEEP_INTERVAL_MS);
        QueryPerformanceCounter(&After);

        ElapsedMs = (After.QuadPart - Before.QuadPart) * 1000.0 / Frequency.QuadPart;
        ok(ElapsedMs < TickMs * 1.5, "Sleep(%u) took %.2f ms with a %.3f ms tick\n", SLEEP_INTERVAL_MS, ElapsedMs, TickMs);
    }

    for (Sample = 0; Sample < SLEEP_LONG_SAMPLES; Sample++)
    {
        QueryPerformanceCounter(&Before);
        Sleep(SLEEP_LONG_INTERVAL_MS);
        QueryPerformanceCounter(&After);

        ElapsedMs = (After.QuadPart - Before.QuadPart) * 1000.0 / Frequency.QuadPart;
        ok(ElapsedMs < TickMs * 2.5, "Sleep(%u) took %.2f ms with a %.3f ms tick\n", SLEEP_LONG_INTERVAL_MS, ElapsedMs, TickMs);
    }
}
