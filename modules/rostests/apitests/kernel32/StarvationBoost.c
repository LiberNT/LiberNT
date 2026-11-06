/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a ready thread starved by a higher-priority thread on its processor still runs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static volatile LONG StopSpinning;
static volatile LONG VictimRan;

static DWORD WINAPI
SpinnerThread(_In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    while (!ReadAcquire(&StopSpinning))
        YieldProcessor();
    return 0;
}

static DWORD WINAPI
VictimThread(_In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    InterlockedExchange(&VictimRan, 1);
    return 0;
}

START_TEST(StarvationBoost)
{
    DWORD_PTR ProcessMask, SystemMask, Cpu;
    HANDLE Spinner, Victim;
    DWORD Start, Elapsed, Wait;
    BOOL Success;

    Success = GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask);
    ok(Success, "GetProcessAffinityMask failed with %lu\n", GetLastError());
    if (!Success)
        return;
    Cpu = ProcessMask & (0 - ProcessMask);

    Success = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    ok(Success, "SetThreadPriority failed with %lu\n", GetLastError());

    Spinner = CreateThread(NULL, 0, SpinnerThread, NULL, CREATE_SUSPENDED, NULL);
    Victim = CreateThread(NULL, 0, VictimThread, NULL, CREATE_SUSPENDED, NULL);
    ok(Spinner != NULL && Victim != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!Spinner || !Victim)
        goto Cleanup;

    ok(SetThreadAffinityMask(Spinner, Cpu) != 0, "SetThreadAffinityMask failed with %lu\n", GetLastError());
    ok(SetThreadAffinityMask(Victim, Cpu) != 0, "SetThreadAffinityMask failed with %lu\n", GetLastError());
    ok(SetThreadPriority(Spinner, THREAD_PRIORITY_NORMAL), "SetThreadPriority failed with %lu\n", GetLastError());
    ok(SetThreadPriority(Victim, THREAD_PRIORITY_LOWEST), "SetThreadPriority failed with %lu\n", GetLastError());

    ResumeThread(Spinner);
    Sleep(100);
    Start = GetTickCount();
    ResumeThread(Victim);
    Wait = WaitForSingleObject(Victim, 10000);
    Elapsed = GetTickCount() - Start;
    InterlockedExchange(&StopSpinning, 1);
    WaitForSingleObject(Spinner, INFINITE);

    ok(Wait == WAIT_OBJECT_0, "The starved thread did not run while the spinner held its processor: %lu\n", Wait);
    ok(VictimRan == 1, "The starved thread did not run\n");
    trace("STARVATION_BOOST elapsed_ms=%lu\n", Elapsed);

Cleanup:
    InterlockedExchange(&StopSpinning, 1);
    if (Spinner)
    {
        ResumeThread(Spinner);
        WaitForSingleObject(Spinner, INFINITE);
        CloseHandle(Spinner);
    }
    if (Victim)
    {
        ResumeThread(Victim);
        WaitForSingleObject(Victim, INFINITE);
        CloseHandle(Victim);
    }
}
