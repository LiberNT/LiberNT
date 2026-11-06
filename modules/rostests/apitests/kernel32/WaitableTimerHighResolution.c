/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests high-resolution waitable timers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#include "precomp.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define SAMPLES 41

typedef HANDLE (WINAPI *PFN_CREATE_EX)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);

static int __cdecl
CompareDouble(const void *a, const void *b)
{
    double Left = *(const double *)a, Right = *(const double *)b;

    return Left < Right ? -1 : Left > Right;
}

static double
MedianWaitMs(HANDLE Timer, LONGLONG Due100ns)
{
    double Samples[SAMPLES];
    LARGE_INTEGER Frequency, Start, End, Due;
    ULONG Index;

    QueryPerformanceFrequency(&Frequency);
    for (Index = 0; Index < SAMPLES; ++Index)
    {
        Due.QuadPart = Due100ns;
        QueryPerformanceCounter(&Start);
        ok(SetWaitableTimer(Timer, &Due, 0, NULL, NULL, FALSE), "SetWaitableTimer: %lu\n", GetLastError());
        ok(WaitForSingleObject(Timer, 1000) == WAIT_OBJECT_0, "the timer did not fire\n");
        QueryPerformanceCounter(&End);
        Samples[Index] = (double)(End.QuadPart - Start.QuadPart) * 1000.0 / (double)Frequency.QuadPart;
    }
    qsort(Samples, SAMPLES, sizeof(Samples[0]), CompareDouble);
    return Samples[SAMPLES / 2];
}

START_TEST(WaitableTimerHighResolution)
{
    PFN_CREATE_EX Create;
    HANDLE Timer;
    LARGE_INTEGER Due;
    double Median;
    DWORD Index;

    Create = (PFN_CREATE_EX)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateWaitableTimerExW");
    if (!Create)
    {
        skip("No CreateWaitableTimerExW\n");
        return;
    }

    SetLastError(0xdeadbeef);
    Timer = Create(NULL, NULL, 0x80000000u, TIMER_ALL_ACCESS);
    ok(Timer == NULL && GetLastError() == ERROR_INVALID_PARAMETER, "unknown flag: %p, %lu\n", Timer, GetLastError());
    if (Timer) CloseHandle(Timer);

    /* Windows 11 measured 2.05 to 3.2 ms for 2 ms; a normal timer ends on the next 15.6 ms tick. */
    Timer = Create(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    ok(Timer != NULL, "high resolution timer: %lu\n", GetLastError());
    if (!Timer) return;
    Median = MedianWaitMs(Timer, -20000);
    ok(Median >= 1.9 && Median < 8.0, "2 ms took a median of %d us\n", (int)(Median * 1000.0));

    /* An auto-reset timer signals one waiter and clears. */
    ok(WaitForSingleObject(Timer, 0) == WAIT_TIMEOUT, "the fired timer stayed signaled\n");

    Due.QuadPart = -2000000;
    ok(SetWaitableTimer(Timer, &Due, 0, NULL, NULL, FALSE), "SetWaitableTimer: %lu\n", GetLastError());
    ok(CancelWaitableTimer(Timer), "CancelWaitableTimer: %lu\n", GetLastError());
    ok(WaitForSingleObject(Timer, 300) == WAIT_TIMEOUT, "a cancelled timer fired\n");

    Due.QuadPart = -10000;
    ok(SetWaitableTimer(Timer, &Due, 5, NULL, NULL, FALSE), "periodic SetWaitableTimer: %lu\n", GetLastError());
    for (Index = 0; Index < 5; ++Index)
        ok(WaitForSingleObject(Timer, 200) == WAIT_OBJECT_0, "periodic fire %lu did not arrive\n", Index);
    ok(CancelWaitableTimer(Timer), "CancelWaitableTimer: %lu\n", GetLastError());
    CloseHandle(Timer);

    Timer = Create(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION | CREATE_WAITABLE_TIMER_MANUAL_RESET, TIMER_ALL_ACCESS);
    ok(Timer != NULL, "manual-reset high resolution timer: %lu\n", GetLastError());
    if (!Timer) return;
    Median = MedianWaitMs(Timer, -20000);
    ok(Median >= 1.9 && Median < 8.0, "manual-reset 2 ms took a median of %d us\n", (int)(Median * 1000.0));
    ok(WaitForSingleObject(Timer, 0) == WAIT_OBJECT_0, "a manual-reset timer did not stay signaled\n");
    CloseHandle(Timer);

    SetLastError(0xdeadbeef);
    Timer = Create(NULL, L"LiberNTHighResolutionTimerName", CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    trace("named high resolution timer: %p, %lu\n", Timer, GetLastError());
    if (Timer)
    {
        HANDLE Opened = OpenWaitableTimerW(TIMER_ALL_ACCESS, FALSE, L"LiberNTHighResolutionTimerName");

        ok(Opened != NULL, "the named timer did not open: %lu\n", GetLastError());
        if (Opened) CloseHandle(Opened);
        CloseHandle(Timer);
    }
}
