/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stresses Sleep(0) taking a thread another processor is still switching away from
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define STRESS_WINDOW_MS 400
#define JOIN_WAIT_MS 5000

static HANDLE WakeEvent;
static DWORD_PTR HomeMask, AnyMask;
static volatile LONG StopStress;
static volatile LONG Mirror;
static volatile LONG Mismatches;
static volatile LONG Wakes;

static DWORD WINAPI
Waiter(_In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);

    while (!ReadAcquire(&StopStress))
    {
        if (WaitForSingleObject(WakeEvent, 50) == WAIT_OBJECT_0)
            InterlockedIncrement(&Wakes);
    }

    return 0;
}

static DWORD WINAPI
Setter(_In_ PVOID Parameter)
{
    volatile LONG Local = 0;

    UNREFERENCED_PARAMETER(Parameter);

    while (!ReadAcquire(&StopStress))
    {
        SetThreadAffinityMask(GetCurrentThread(), HomeMask);
        SetThreadAffinityMask(GetCurrentThread(), AnyMask);
        Local++;
        SetEvent(WakeEvent);
        if (Local != InterlockedIncrement(&Mirror))
            InterlockedIncrement(&Mismatches);
    }

    return 0;
}

static DWORD WINAPI
Yielder(_In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);

    while (!ReadAcquire(&StopStress))
        Sleep(0);

    return 0;
}

START_TEST(YieldStealSwitch)
{
    HANDLE Threads[MAXIMUM_WAIT_OBJECTS];
    DWORD_PTR ProcessMask, SystemMask, FirstMask;
    DWORD Count = 0, Index, Bit;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask))
    {
        ok(0, "GetProcessAffinityMask failed with %lu\n", GetLastError());
        return;
    }

    FirstMask = ProcessMask & (0 - ProcessMask);
    HomeMask = FirstMask;
    AnyMask = ProcessMask;
    if ((ProcessMask & ~FirstMask) == 0)
    {
        skip("Taking a thread from another processor needs two of them\n");
        return;
    }

    WakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(WakeEvent != NULL, "CreateEvent failed with %lu\n", GetLastError());
    if (WakeEvent == NULL)
        return;

    /* Each round the setter returns to the first processor and is preempted there by the wake it causes, while yielders elsewhere look for work */
    Threads[Count] = CreateThread(NULL, 0, Waiter, NULL, CREATE_SUSPENDED, NULL);
    if (Threads[Count] != NULL)
    {
        SetThreadAffinityMask(Threads[Count], FirstMask);
        SetThreadPriority(Threads[Count], THREAD_PRIORITY_ABOVE_NORMAL);
        Count++;
    }

    Threads[Count] = CreateThread(NULL, 0, Setter, NULL, CREATE_SUSPENDED, NULL);
    if (Threads[Count] != NULL)
        Count++;

    for (Bit = 0; Bit < sizeof(DWORD_PTR) * 8 && Count < MAXIMUM_WAIT_OBJECTS; Bit++)
    {
        DWORD_PTR Mask = (DWORD_PTR)1 << Bit;

        if (!(ProcessMask & Mask) || Mask == FirstMask)
            continue;

        Threads[Count] = CreateThread(NULL, 0, Yielder, NULL, CREATE_SUSPENDED, NULL);
        if (Threads[Count] != NULL)
        {
            SetThreadAffinityMask(Threads[Count], Mask);
            Count++;
        }
    }

    ok(Count >= 3, "Only %lu of the stress threads were created\n", Count);

    for (Index = 0; Index < Count; Index++)
        ResumeThread(Threads[Index]);

    Sleep(STRESS_WINDOW_MS);
    InterlockedExchange(&StopStress, 1);
    SetEvent(WakeEvent);

    ok(WaitForMultipleObjects(Count, Threads, TRUE, JOIN_WAIT_MS) == WAIT_OBJECT_0, "The stress threads did not stop\n");
    for (Index = 0; Index < Count; Index++)
        CloseHandle(Threads[Index]);
    CloseHandle(WakeEvent);

    ok(Mismatches == 0, "The setter saw its own count %ld times out of step\n", Mismatches);
    ok(Wakes > 0, "The waiter was never woken in %u ms\n", STRESS_WINDOW_MS);
}
