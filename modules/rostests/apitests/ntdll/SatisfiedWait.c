/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that waits satisfied without blocking keep the thread running
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define SATISFIED_WAITS 500
#define SLOW_WAIT_MS 10
#define ALLOWED_SLOW_WAITS 4

static volatile LONG StopSpinning;

static DWORD WINAPI
SpinThread(PVOID Parameter)
{
    volatile ULONG Count = 0;

    UNREFERENCED_PARAMETER(Parameter);

    while (!StopSpinning)
        ++Count;
    return 0;
}

static ULONG
CountSlowWaits(PCSTR What, HANDLE Event, HANDLE Mutex, BOOLEAN Multiple)
{
    LARGE_INTEGER Frequency, Start, End;
    HANDLE Objects[2] = { Event, Mutex };
    ULONG Index, Slow = 0;
    DWORD Result;

    QueryPerformanceFrequency(&Frequency);
    for (Index = 0; Index < SATISFIED_WAITS; Index++)
    {
        QueryPerformanceCounter(&Start);
        if (Multiple)
            Result = WaitForMultipleObjects(2, Objects, TRUE, INFINITE);
        else
            Result = WaitForSingleObject(Mutex ? Mutex : Event, INFINITE);
        QueryPerformanceCounter(&End);
        if (Result != WAIT_OBJECT_0)
        {
            ok(FALSE, "%s: wait %lu returned %lu, error %lu\n", What, Index, Result, GetLastError());
            break;
        }
        if (Mutex)
            ReleaseMutex(Mutex);
        if ((End.QuadPart - Start.QuadPart) * 1000 >= SLOW_WAIT_MS * Frequency.QuadPart)
            ++Slow;
    }
    return Slow;
}

START_TEST(SatisfiedWait)
{
    HANDLE Threads[MAXIMUM_WAIT_OBJECTS], Event, Mutex;
    SYSTEM_INFO Info;
    ULONG Count, Created = 0, Index, Slow;

    GetSystemInfo(&Info);
    Count = min(Info.dwNumberOfProcessors, MAXIMUM_WAIT_OBJECTS);

    Event = CreateEventW(NULL, TRUE, TRUE, NULL);
    ok(Event != NULL, "CreateEvent failed: %lu\n", GetLastError());
    Mutex = CreateMutexW(NULL, TRUE, NULL);
    ok(Mutex != NULL, "CreateMutex failed: %lu\n", GetLastError());
    if (!Event || !Mutex)
        return;

    StopSpinning = 0;
    for (Index = 0; Index < Count; Index++)
    {
        Threads[Index] = CreateThread(NULL, 0, SpinThread, NULL, 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread %lu failed: %lu\n", Index, GetLastError());
        if (!Threads[Index])
            break;
        ++Created;
    }
    Sleep(50);

    Slow = CountSlowWaits("signaled event", Event, NULL, FALSE);
    ok(Slow <= ALLOWED_SLOW_WAITS, "%lu of %u signaled event waits took %u ms or more\n",
       Slow, SATISFIED_WAITS, SLOW_WAIT_MS);

    Slow = CountSlowWaits("owned mutex", NULL, Mutex, FALSE);
    ok(Slow <= ALLOWED_SLOW_WAITS, "%lu of %u owned mutex waits took %u ms or more\n",
       Slow, SATISFIED_WAITS, SLOW_WAIT_MS);

    Slow = CountSlowWaits("event and mutex", Event, Mutex, TRUE);
    ok(Slow <= ALLOWED_SLOW_WAITS, "%lu of %u satisfied multiple-object waits took %u ms or more\n",
       Slow, SATISFIED_WAITS, SLOW_WAIT_MS);

    InterlockedExchange(&StopSpinning, 1);
    for (Index = 0; Index < Created; Index++)
    {
        WaitForSingleObject(Threads[Index], INFINITE);
        CloseHandle(Threads[Index]);
    }
    ReleaseMutex(Mutex);
    CloseHandle(Mutex);
    CloseHandle(Event);
}
