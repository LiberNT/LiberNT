/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that Sleep(0) hands the processor to a lower-priority ready thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define YIELD_WINDOW_MS 500
#define LOWER_SLEEP_MS  50

static volatile LONG LowerRan;

static DWORD WINAPI
LowerThread(_In_ PVOID Parameter)
{
    DWORD SleepFirst = (DWORD)(ULONG_PTR)Parameter;

    if (SleepFirst != 0)
        Sleep(SleepFirst);
    InterlockedExchange(&LowerRan, 1);
    return 0;
}

static VOID
TestSleepZero(_In_ DWORD_PTR Mask, _In_ DWORD LowerSleep, _In_ PCSTR What)
{
    HANDLE Lower;
    DWORD Start;

    InterlockedExchange(&LowerRan, 0);
    Lower = CreateThread(NULL, 0, LowerThread, (PVOID)(ULONG_PTR)LowerSleep, CREATE_SUSPENDED, NULL);
    ok(Lower != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!Lower)
        return;

    ok(SetThreadAffinityMask(Lower, Mask) != 0, "SetThreadAffinityMask failed with %lu\n", GetLastError());
    ok(SetThreadPriority(Lower, THREAD_PRIORITY_BELOW_NORMAL), "SetThreadPriority failed with %lu\n", GetLastError());
    ok(SetThreadPriorityBoost(Lower, TRUE), "SetThreadPriorityBoost failed with %lu\n", GetLastError());
    ResumeThread(Lower);

    /* Only this thread's yields give the lower-priority thread its processor */
    Start = GetTickCount();
    while (!ReadAcquire(&LowerRan) && (GetTickCount() - Start < LowerSleep + YIELD_WINDOW_MS))
        Sleep(0);

    ok(LowerRan == 1, "A lower-priority thread %s did not run while this thread called Sleep(0) for %lu ms\n", What, LowerSleep + YIELD_WINDOW_MS);

    ok(WaitForSingleObject(Lower, 15000) == WAIT_OBJECT_0, "The lower-priority thread did not exit\n");
    CloseHandle(Lower);
}

START_TEST(SleepZeroPriority)
{
    DWORD_PTR ProcessMask, SystemMask, OldMask, Mask;
    int OldPriority;
    BOOL Success;

    Success = GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask);
    ok(Success, "GetProcessAffinityMask failed with %lu\n", GetLastError());
    if (!Success)
        return;

    Mask = ProcessMask & (0 - ProcessMask);
    OldMask = SetThreadAffinityMask(GetCurrentThread(), Mask);
    ok(OldMask != 0, "SetThreadAffinityMask failed with %lu\n", GetLastError());
    OldPriority = GetThreadPriority(GetCurrentThread());
    ok(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL), "SetThreadPriority failed with %lu\n", GetLastError());

    TestSleepZero(Mask, 0, "that was ready");
    TestSleepZero(Mask, LOWER_SLEEP_MS, "that woke from a sleep");

    SetThreadPriority(GetCurrentThread(), OldPriority);
    if (OldMask != 0)
        SetThreadAffinityMask(GetCurrentThread(), OldMask);
}
