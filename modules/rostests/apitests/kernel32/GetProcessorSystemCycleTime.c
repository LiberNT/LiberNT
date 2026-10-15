/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for GetProcessorSystemCycleTime
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_GETPROCESSORSYSTEMCYCLETIME)(USHORT, PSYSTEM_PROCESSOR_CYCLE_TIME_INFORMATION, PDWORD);

START_TEST(GetProcessorSystemCycleTime)
{
    PFN_GETPROCESSORSYSTEMCYCLETIME pGetProcessorSystemCycleTime;
    SYSTEM_PROCESSOR_CYCLE_TIME_INFORMATION First[64], Second[64], Small;
    DWORD Count, Expected, Length, Index;
    BOOL Result;

    pGetProcessorSystemCycleTime = (PFN_GETPROCESSORSYSTEMCYCLETIME)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                                   "GetProcessorSystemCycleTime");
    ok(pGetProcessorSystemCycleTime != NULL, "GetProcessorSystemCycleTime is missing\n");
    if (!pGetProcessorSystemCycleTime)
        return;

    Count = GetActiveProcessorCount(0);
    ok(Count != 0 && Count <= _countof(First), "processor count %lu\n", Count);
    if (Count == 0 || Count > _countof(First))
        return;
    Expected = Count * sizeof(SYSTEM_PROCESSOR_CYCLE_TIME_INFORMATION);

    Length = 0;
    SetLastError(0xdeadbeef);
    Result = pGetProcessorSystemCycleTime(0, NULL, &Length);
    ok(!Result, "size query: result %d\n", Result);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "size query: error %lu\n", GetLastError());
    ok(Length == Expected, "size query: length %lu, expected %lu\n", Length, Expected);

    if (Count > 1)
    {
        Length = sizeof(Small);
        SetLastError(0xdeadbeef);
        Result = pGetProcessorSystemCycleTime(0, &Small, &Length);
        ok(!Result, "small buffer: result %d\n", Result);
        ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "small buffer: error %lu\n", GetLastError());
        ok(Length == Expected, "small buffer: length %lu, expected %lu\n", Length, Expected);
    }

    Length = sizeof(First);
    SetLastError(0xdeadbeef);
    Result = pGetProcessorSystemCycleTime(0, First, &Length);
    ok(Result, "full buffer: result %d, error %lu\n", Result, GetLastError());
    ok(GetLastError() == 0xdeadbeef, "full buffer: error %lu\n", GetLastError());
    ok(Length == Expected, "full buffer: length %lu, expected %lu\n", Length, Expected);

    Sleep(20);

    Length = Expected;
    Result = pGetProcessorSystemCycleTime(0, Second, &Length);
    ok(Result, "exact buffer: result %d, error %lu\n", Result, GetLastError());
    ok(Length == Expected, "exact buffer: length %lu, expected %lu\n", Length, Expected);
    for (Index = 0; Index < Count; Index++)
    {
        ok(Second[Index].CycleTime >= First[Index].CycleTime, "processor %lu: cycle time went back\n", Index);
    }
    ok(Second[0].CycleTime != 0, "processor 0: zero cycle time\n");

    Length = sizeof(First);
    SetLastError(0xdeadbeef);
    Result = pGetProcessorSystemCycleTime((USHORT)GetActiveProcessorGroupCount(), First, &Length);
    ok(!Result, "bad group: result %d\n", Result);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "bad group: error %lu\n", GetLastError());
    ok(Length == sizeof(First), "bad group: length %lu\n", Length);
}
