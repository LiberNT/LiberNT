/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that threads of equal priority sharing a processor take turns
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define SPINNER_COUNT 8

static volatile LONG StopSpinning;
static volatile LONG SpinnersStarted;

static DWORD WINAPI
SpinnerThread(_In_ PVOID Parameter)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(Parameter);
    for (Index = 0; Index < 4; Index++)
        Sleep(1);
    InterlockedIncrement(&SpinnersStarted);
    while (!ReadAcquire(&StopSpinning))
        YieldProcessor();
    return 0;
}

static DWORD WINAPI
LatecomerThread(_In_ PVOID Parameter)
{
    SetEvent((HANDLE)Parameter);
    return 0;
}

START_TEST(QuantumRoundRobin)
{
    HANDLE Spinners[SPINNER_COUNT] = { NULL };
    DWORD_PTR ProcessMask, SystemMask;
    DWORD Start, Elapsed, Wait;
    HANDLE Event, Latecomer;
    ULONG Index, Round;
    BOOL Success;

    Success = GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask);
    ok(Success, "GetProcessAffinityMask failed with %lu\n", GetLastError());
    if (!Success)
        return;
    Success = SetProcessAffinityMask(GetCurrentProcess(), ProcessMask & (0 - ProcessMask));
    ok(Success, "SetProcessAffinityMask failed with %lu\n", GetLastError());

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (!Event)
        goto Cleanup;

    for (Index = 0; Index < SPINNER_COUNT; Index++)
    {
        Spinners[Index] = CreateThread(NULL, 0, SpinnerThread, NULL, 0, NULL);
        ok(Spinners[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (!Spinners[Index])
            goto Cleanup;
    }

    Start = GetTickCount();
    while (ReadAcquire(&SpinnersStarted) < SPINNER_COUNT && GetTickCount() - Start < 10000)
        Sleep(10);
    ok_long(SpinnersStarted, SPINNER_COUNT);
    Sleep(200);

    for (Round = 0; Round < 3; Round++)
    {
        Start = GetTickCount();
        Latecomer = CreateThread(NULL, 0, LatecomerThread, Event, 0, NULL);
        ok(Latecomer != NULL, "CreateThread failed with %lu\n", GetLastError());
        if (!Latecomer)
            break;
        Wait = WaitForSingleObject(Event, 10000);
        Elapsed = GetTickCount() - Start;
        ok(Wait == WAIT_OBJECT_0, "The new thread did not run: %lu\n", Wait);
        ok(Elapsed < 2000, "The new thread waited %lu ms behind %u threads of its priority\n", Elapsed, SPINNER_COUNT);
        trace("QUANTUM_ROUND_ROBIN round=%lu elapsed_ms=%lu\n", Round, Elapsed);
        WaitForSingleObject(Latecomer, 10000);
        CloseHandle(Latecomer);

        Start = GetTickCount();
        Sleep(1);
        Elapsed = GetTickCount() - Start;
        ok(Elapsed < 1000, "Sleep(1) returned after %lu ms\n", Elapsed);
    }

Cleanup:
    InterlockedExchange(&StopSpinning, 1);
    for (Index = 0; Index < SPINNER_COUNT; Index++)
    {
        if (!Spinners[Index])
            continue;
        WaitForSingleObject(Spinners[Index], INFINITE);
        CloseHandle(Spinners[Index]);
    }
    if (Event)
        CloseHandle(Event);
}
