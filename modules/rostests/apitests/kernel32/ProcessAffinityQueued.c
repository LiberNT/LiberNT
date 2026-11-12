/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests moving a process off a processor its threads are queued on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define SPINNER_COUNT 2
#define MOVE_WAIT_MS 5000

static volatile LONG StopSpinners;
static DWORD TargetProcessor;

static DWORD WINAPI
Spinner(_In_ PVOID Parameter)
{
    while (!ReadAcquire(&StopSpinners))
    {
        if (GetCurrentProcessorNumber() == TargetProcessor)
        {
            SetEvent((HANDLE)Parameter);
            return 0;
        }
    }

    return 1;
}

START_TEST(ProcessAffinityQueued)
{
    DWORD_PTR ProcessMask, SystemMask, FirstMask, SecondMask;
    HANDLE Spinners[SPINNER_COUNT] = { NULL };
    HANDLE Moved[SPINNER_COUNT] = { NULL };
    DWORD Result;
    INT OldPriority;
    ULONG Index;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask))
    {
        ok(0, "GetProcessAffinityMask failed with %lu\n", GetLastError());
        return;
    }

    FirstMask = ProcessMask & (0 - ProcessMask);
    SecondMask = ProcessMask & ~FirstMask;
    SecondMask &= 0 - SecondMask;
    if (SecondMask == 0)
    {
        skip("Moving a process between processors needs two of them\n");
        return;
    }

    TargetProcessor = 0;
    while ((SecondMask >> TargetProcessor) != 1)
        TargetProcessor++;

    OldPriority = GetThreadPriority(GetCurrentThread());
    ok(SetProcessAffinityMask(GetCurrentProcess(), FirstMask), "SetProcessAffinityMask failed with %lu\n", GetLastError());
    ok(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL), "SetThreadPriority failed with %lu\n", GetLastError());

    /* Below this thread on the only processor allowed, the spinners stay queued there */
    for (Index = 0; Index < SPINNER_COUNT; Index++)
    {
        Moved[Index] = CreateEventW(NULL, TRUE, FALSE, NULL);
        ok(Moved[Index] != NULL, "CreateEvent failed with %lu\n", GetLastError());
        if (Moved[Index] == NULL)
            break;

        Spinners[Index] = CreateThread(NULL, 0, Spinner, Moved[Index], 0, NULL);
        ok(Spinners[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (Spinners[Index] == NULL)
            break;
    }

    if (Index == SPINNER_COUNT)
    {
        ok(SetProcessAffinityMask(GetCurrentProcess(), SecondMask), "SetProcessAffinityMask failed with %lu\n", GetLastError());

        Result = WaitForMultipleObjects(SPINNER_COUNT, Moved, TRUE, MOVE_WAIT_MS);
        ok(Result == WAIT_OBJECT_0, "The queued threads did not run on processor %lu: %lu\n", TargetProcessor, Result);
    }

    InterlockedExchange(&StopSpinners, 1);
    for (Index = 0; Index < SPINNER_COUNT; Index++)
    {
        if (Spinners[Index] != NULL)
        {
            ok(WaitForSingleObject(Spinners[Index], MOVE_WAIT_MS) == WAIT_OBJECT_0, "Spinner %lu did not exit\n", Index);
            CloseHandle(Spinners[Index]);
        }

        if (Moved[Index] != NULL)
            CloseHandle(Moved[Index]);
    }

    SetThreadPriority(GetCurrentThread(), OldPriority);
    ok(SetProcessAffinityMask(GetCurrentProcess(), ProcessMask), "SetProcessAffinityMask failed with %lu\n", GetLastError());
}
