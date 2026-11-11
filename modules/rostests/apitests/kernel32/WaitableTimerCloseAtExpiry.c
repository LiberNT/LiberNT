/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A waitable timer closed right as it goes off is torn down safely
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define CLOSE_ROUNDS 12
#define CLOSE_BATCH 24

typedef HANDLE (WINAPI *PFN_CREATE_WAITABLE_TIMER_EX)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);

static LARGE_INTEGER Frequency;
static DWORD_PTR LastProcessor;

static LONGLONG
Microseconds(VOID)
{
    LARGE_INTEGER Now;

    QueryPerformanceCounter(&Now);
    return Now.QuadPart * 1000000 / Frequency.QuadPart;
}

static VOID
SpinUntilSignaled(HANDLE Timer)
{
    LONGLONG Start = Microseconds();

    while (WaitForSingleObject(Timer, 0) == WAIT_TIMEOUT && Microseconds() - Start < 20000)
    {
        YieldProcessor();
    }
}

static VOID
PinToProcessor(ULONG Round)
{
    SetThreadAffinityMask(GetCurrentThread(), (Round & 1) ? LastProcessor : 1);
}

static VOID
TestCloseAtExpiry(VOID)
{
    PFN_CREATE_WAITABLE_TIMER_EX CreateTimerEx;
    HANDLE Timers[CLOSE_BATCH];
    LARGE_INTEGER DueTime;
    LONG Failures = 0;
    ULONG Round, Index;

    CreateTimerEx = (PFN_CREATE_WAITABLE_TIMER_EX)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                  "CreateWaitableTimerExW");
    if (CreateTimerEx == NULL)
    {
        skip("No CreateWaitableTimerExW\n");
        return;
    }

    for (Round = 0; Round < CLOSE_ROUNDS; Round++)
    {
        PinToProcessor(Round);

        for (Index = 0; Index < CLOSE_BATCH; Index++)
        {
            Timers[Index] = CreateTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION | CREATE_WAITABLE_TIMER_MANUAL_RESET,
                                          TIMER_ALL_ACCESS);
            if (Timers[Index] == NULL)
            {
                skip("No high-resolution waitable timers (%lu)\n", GetLastError());
                while (Index-- != 0) CloseHandle(Timers[Index]);
                return;
            }
        }

        DueTime.QuadPart = -5000;
        for (Index = 0; Index < CLOSE_BATCH; Index++)
        {
            if (!SetWaitableTimer(Timers[Index], &DueTime, 0, NULL, NULL, FALSE)) Failures++;
        }

        SpinUntilSignaled(Timers[0]);
        for (Index = 0; Index < CLOSE_BATCH; Index++)
        {
            if (!CloseHandle(Timers[Index])) Failures++;
        }
    }

    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)-1);
    ok(Failures == 0, "%ld timer calls failed\n", Failures);
}

START_TEST(WaitableTimerCloseAtExpiry)
{
    DWORD_PTR ProcessMask, SystemMask;

    QueryPerformanceFrequency(&Frequency);

    LastProcessor = 1;
    if (GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask))
    {
        while ((LastProcessor << 1) != 0 && (ProcessMask & (LastProcessor << 1)) != 0)
            LastProcessor <<= 1;
    }

    TestCloseAtExpiry();
}
