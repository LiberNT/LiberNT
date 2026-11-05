/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A yield with nothing else to run lasts long enough for loops that count yields
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define YIELD_COUNT 200000
#define MINIMUM_YIELD_NS 500

typedef NTSTATUS (NTAPI *PFN_NT_YIELD_EXECUTION)(VOID);

static double
MeasureSwitchToThread(void)
{
    LARGE_INTEGER Frequency, Start, End;
    ULONG Index;

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);
    for (Index = 0; Index < YIELD_COUNT; ++Index)
        SwitchToThread();
    QueryPerformanceCounter(&End);
    return (double)(End.QuadPart - Start.QuadPart) * 1000000000.0 / (double)Frequency.QuadPart / YIELD_COUNT;
}

static double
MeasureSleepZero(void)
{
    LARGE_INTEGER Frequency, Start, End;
    ULONG Index;

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);
    for (Index = 0; Index < YIELD_COUNT; ++Index)
        Sleep(0);
    QueryPerformanceCounter(&End);
    return (double)(End.QuadPart - Start.QuadPart) * 1000000000.0 / (double)Frequency.QuadPart / YIELD_COUNT;
}

static double
MeasureNtYield(PFN_NT_YIELD_EXECUTION NtYield)
{
    LARGE_INTEGER Frequency, Start, End;
    ULONG Index;

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);
    for (Index = 0; Index < YIELD_COUNT; ++Index)
        NtYield();
    QueryPerformanceCounter(&End);
    return (double)(End.QuadPart - Start.QuadPart) * 1000000000.0 / (double)Frequency.QuadPart / YIELD_COUNT;
}

START_TEST(SwitchToThreadCost)
{
    PFN_NT_YIELD_EXECUTION NtYield;
    double Cost;

    NtYield = (PFN_NT_YIELD_EXECUTION)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtYieldExecution");
    ok(NtYield != NULL, "NtYieldExecution is not exported\n");

    Cost = MeasureSwitchToThread();
    trace("SwitchToThread %.0f ns\n", Cost);
    ok(Cost >= MINIMUM_YIELD_NS, "SwitchToThread took %.0f ns, expected at least %d\n", Cost, MINIMUM_YIELD_NS);

    Cost = MeasureSleepZero();
    trace("Sleep(0) %.0f ns\n", Cost);
    ok(Cost >= MINIMUM_YIELD_NS, "Sleep(0) took %.0f ns, expected at least %d\n", Cost, MINIMUM_YIELD_NS);

    if (NtYield != NULL)
    {
        Cost = MeasureNtYield(NtYield);
        trace("NtYieldExecution %.0f ns\n", Cost);
        ok(Cost >= MINIMUM_YIELD_NS, "NtYieldExecution took %.0f ns, expected at least %d\n", Cost, MINIMUM_YIELD_NS);
    }
}
