/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A waitable timer's completion routine is told when the timer was signaled
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static ULONGLONG SignalTime;
static LONG Runs;

static VOID CALLBACK Completion(LPVOID Context, DWORD LowValue, DWORD HighValue)
{
    UNREFERENCED_PARAMETER(Context);

    SignalTime = ((ULONGLONG)HighValue << 32) | LowValue;
    InterlockedIncrement(&Runs);
}

static ULONGLONG FileTimeValue(const FILETIME *Time)
{
    return ((ULONGLONG)Time->dwHighDateTime << 32) | Time->dwLowDateTime;
}

START_TEST(WaitableTimerSignalTime)
{
    FILETIME Before, After;
    LARGE_INTEGER Due;
    HANDLE Timer;

    Timer = CreateWaitableTimerW(NULL, TRUE, NULL);
    ok(Timer != NULL, "CreateWaitableTimer failed: %lu\n", GetLastError());
    if (Timer == NULL) return;

    GetSystemTimeAsFileTime(&Before);
    Due.QuadPart = -10000;
    ok(SetWaitableTimer(Timer, &Due, 0, Completion, NULL, FALSE), "SetWaitableTimer failed: %lu\n", GetLastError());
    ok_hex(SleepEx(5000, TRUE), WAIT_IO_COMPLETION);
    GetSystemTimePreciseAsFileTime(&After);

    ok(Runs == 1, "The completion routine ran %ld times\n", Runs);
    ok(SignalTime >= FileTimeValue(&Before) && SignalTime <= FileTimeValue(&After),
       "The completion routine was told %I64x, not a time between %I64x and %I64x\n",
       SignalTime, FileTimeValue(&Before), FileTimeValue(&After));

    CloseHandle(Timer);
}
