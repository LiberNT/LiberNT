/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that QueryInterruptTimePrecise moves between clock ticks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define PRECISE_ATTEMPTS 16

typedef VOID (WINAPI *PFN_QUERY_INTERRUPT_TIME)(PULONGLONG);

static VOID
SpinFor100ns(_In_ ULONGLONG Duration, _In_ LONGLONG Frequency)
{
    LARGE_INTEGER Start, Now;

    QueryPerformanceCounter(&Start);
    do
    {
        QueryPerformanceCounter(&Now);
    } while ((ULONGLONG)(Now.QuadPart - Start.QuadPart) * 10000000ULL / (ULONGLONG)Frequency < Duration);
}

START_TEST(QueryInterruptTimePrecise)
{
    PFN_QUERY_INTERRUPT_TIME QueryTick, QueryPrecise;
    ULONGLONG Tick1, Tick2, Precise1, Precise2, Spin;
    DWORD Adjustment, Increment;
    BOOL AdjustmentDisabled;
    LARGE_INTEGER Frequency;
    ULONG Attempt;
    BOOL Checked = FALSE;
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");

    QueryTick = (PFN_QUERY_INTERRUPT_TIME)GetProcAddress(Kernel32, "QueryInterruptTime");
    QueryPrecise = (PFN_QUERY_INTERRUPT_TIME)GetProcAddress(Kernel32, "QueryInterruptTimePrecise");
    if (!QueryTick || !QueryPrecise)
    {
        skip("QueryInterruptTime or QueryInterruptTimePrecise is not exported\n");
        return;
    }

    ok(QueryPerformanceFrequency(&Frequency), "QueryPerformanceFrequency failed\n");
    ok(GetSystemTimeAdjustment(&Adjustment, &Increment, &AdjustmentDisabled), "GetSystemTimeAdjustment failed: %lu\n", GetLastError());
    if (Frequency.QuadPart == 0 || Increment == 0)
        return;

    /* A quarter of a tick fits between two ticks most of the time */
    Spin = Increment / 4;

    for (Attempt = 0; Attempt < PRECISE_ATTEMPTS && !Checked; Attempt++)
    {
        QueryTick(&Tick1);
        QueryPrecise(&Precise1);
        SpinFor100ns(Spin, Frequency.QuadPart);
        QueryPrecise(&Precise2);
        QueryTick(&Tick2);

        ok(Precise1 >= Tick1, "The precise interrupt time %I64u is behind the tick time %I64u\n", Precise1, Tick1);
        if (Tick1 != Tick2)
            continue;

        Checked = TRUE;
        ok(Precise2 - Precise1 >= Spin * 3 / 4,
           "The precise interrupt time moved %I64u within one tick after %I64u had passed\n", Precise2 - Precise1, Spin);
    }

    ok(Checked, "No two readings fell within one clock tick in %u attempts\n", PRECISE_ATTEMPTS);
}
