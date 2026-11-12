/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that SwitchToThread hands the processor to a lower-priority ready thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define YIELD_WINDOW_MS 500

static volatile LONG LowerRan;

static DWORD WINAPI
LowerThread(_In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    InterlockedExchange(&LowerRan, 1);
    return 0;
}

START_TEST(SwitchToThreadPriority)
{
    DWORD_PTR ProcessMask, SystemMask, OldMask;
    HANDLE Lower;
    DWORD Start;
    BOOL Yielded = FALSE;
    BOOL Success;

    Success = GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask);
    ok(Success, "GetProcessAffinityMask failed with %lu\n", GetLastError());
    if (!Success)
        return;

    OldMask = SetThreadAffinityMask(GetCurrentThread(), ProcessMask & (0 - ProcessMask));
    ok(OldMask != 0, "SetThreadAffinityMask failed with %lu\n", GetLastError());

    Lower = CreateThread(NULL, 0, LowerThread, NULL, CREATE_SUSPENDED, NULL);
    ok(Lower != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!Lower)
    {
        if (OldMask != 0)
            SetThreadAffinityMask(GetCurrentThread(), OldMask);
        return;
    }

    ok(SetThreadAffinityMask(Lower, ProcessMask & (0 - ProcessMask)) != 0,
       "SetThreadAffinityMask failed with %lu\n", GetLastError());
    ok(SetThreadPriority(Lower, THREAD_PRIORITY_BELOW_NORMAL), "SetThreadPriority failed with %lu\n", GetLastError());
    ok(SetThreadPriorityBoost(Lower, TRUE), "SetThreadPriorityBoost failed with %lu\n", GetLastError());
    ResumeThread(Lower);

    /* Only this thread's yields give the lower-priority thread its processor */
    Start = GetTickCount();
    while (!ReadAcquire(&LowerRan) && (GetTickCount() - Start < YIELD_WINDOW_MS))
    {
        if (SwitchToThread())
            Yielded = TRUE;
    }

    ok(LowerRan == 1, "A lower-priority ready thread did not run while this thread yielded for %u ms\n", YIELD_WINDOW_MS);
    ok(Yielded, "SwitchToThread never reported that it yielded\n");

    ok(WaitForSingleObject(Lower, 5000) == WAIT_OBJECT_0, "The lower-priority thread did not exit\n");
    CloseHandle(Lower);

    if (OldMask != 0)
        SetThreadAffinityMask(GetCurrentThread(), OldMask);
}
