/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a thread yielding with SwitchToThread goes behind the other ready threads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define YIELDER_COUNT 3
#define ROUNDS 8
#define TURN_WINDOW_MS 500

static volatile LONG StopYielding;
static volatile LONG Turns[YIELDER_COUNT];

static VOID
YieldInTurn(_In_ LONG Index)
{
    while (!ReadAcquire(&StopYielding))
    {
        InterlockedIncrement(&Turns[Index]);
        SwitchToThread();
    }
}

static DWORD WINAPI
Yielder(_In_ PVOID Parameter)
{
    YieldInTurn((LONG)(LONG_PTR)Parameter);
    return 0;
}

START_TEST(SwitchToThreadRoundRobin)
{
    DWORD_PTR ProcessMask, SystemMask, FirstMask;
    HANDLE Threads[YIELDER_COUNT] = { NULL };
    BOOL OldDisableBoost = FALSE;
    DWORD Start;
    LONG Index;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask))
    {
        ok(0, "GetProcessAffinityMask failed with %lu\n", GetLastError());
        return;
    }

    FirstMask = ProcessMask & (0 - ProcessMask);
    GetProcessPriorityBoost(GetCurrentProcess(), &OldDisableBoost);
    ok(SetProcessPriorityBoost(GetCurrentProcess(), TRUE), "SetProcessPriorityBoost failed with %lu\n", GetLastError());
    ok(SetProcessAffinityMask(GetCurrentProcess(), FirstMask), "SetProcessAffinityMask failed with %lu\n", GetLastError());

    for (Index = 1; Index < YIELDER_COUNT; Index++)
    {
        Threads[Index] = CreateThread(NULL, 0, Yielder, (PVOID)(LONG_PTR)Index, CREATE_SUSPENDED, NULL);
        ok(Threads[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
    }

    for (Index = 1; Index < YIELDER_COUNT; Index++)
    {
        if (Threads[Index] != NULL)
            ResumeThread(Threads[Index]);
    }

    /* Every yielder shares the one processor; this thread is the first of them */
    Start = GetTickCount();
    while (!ReadAcquire(&StopYielding))
    {
        InterlockedIncrement(&Turns[0]);
        SwitchToThread();

        if ((ReadAcquire(&Turns[0]) >= ROUNDS) || (GetTickCount() - Start >= TURN_WINDOW_MS))
            InterlockedExchange(&StopYielding, 1);
    }

    for (Index = 1; Index < YIELDER_COUNT; Index++)
    {
        if (Threads[Index] != NULL)
        {
            ok(WaitForSingleObject(Threads[Index], 5000) == WAIT_OBJECT_0, "Yielder %ld did not exit\n", Index);
            CloseHandle(Threads[Index]);
        }
    }

    for (Index = 0; Index < YIELDER_COUNT; Index++)
    {
        ok(Turns[Index] >= ROUNDS / 2, "Yielder %ld had %ld turns while yielder 0 had %ld\n",
           Index, Turns[Index], Turns[0]);
    }

    SetProcessAffinityMask(GetCurrentProcess(), ProcessMask);
    SetProcessPriorityBoost(GetCurrentProcess(), OldDisableBoost);
}
