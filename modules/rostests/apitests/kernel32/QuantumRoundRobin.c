/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that threads of equal priority sharing a processor take turns
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define SPINNER_COUNT 4
#define YIELD_COUNT 20000
#define SHARE_MS 600

static volatile LONG StopSpinning;
static volatile LONG SpinnersStarted;
static volatile LONG StopSharing;
static volatile LONG YielderSpinning;

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

static DWORD WINAPI
YielderThread(_In_ PVOID Parameter)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(Parameter);
    for (Index = 0; Index < YIELD_COUNT; Index++)
        Sleep(0);
    InterlockedExchange(&YielderSpinning, 1);
    while (!ReadAcquire(&StopSharing))
        YieldProcessor();
    return 0;
}

static DWORD WINAPI
PeerThread(_In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    while (!ReadAcquire(&StopSharing))
        YieldProcessor();
    return 0;
}

static ULONG64
RunTimeOf(_In_ HANDLE Thread)
{
    FILETIME Creation, Exit, Kernel, User;
    BOOL Success;

    Success = GetThreadTimes(Thread, &Creation, &Exit, &Kernel, &User);
    ok(Success, "GetThreadTimes failed with %lu\n", GetLastError());
    if (!Success)
        return 0;

    return (((ULONG64)Kernel.dwHighDateTime << 32) | Kernel.dwLowDateTime) +
           (((ULONG64)User.dwHighDateTime << 32) | User.dwLowDateTime);
}

/* A thread that offered its turn many times while nothing else was ready shares the processor like any other once it computes */
static VOID
TestSharesAfterYielding(_In_ DWORD_PTR Processor)
{
    HANDLE Threads[2] = { NULL };
    ULONG64 Before[2], Used[2], Total;
    DWORD Start;
    ULONG Index;

    Threads[0] = CreateThread(NULL, 0, YielderThread, NULL, CREATE_SUSPENDED, NULL);
    ok(Threads[0] != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!Threads[0])
        return;
    ok(SetThreadAffinityMask(Threads[0], Processor) != 0, "SetThreadAffinityMask failed with %lu\n", GetLastError());
    ok(SetThreadPriorityBoost(Threads[0], TRUE), "SetThreadPriorityBoost failed with %lu\n", GetLastError());
    ok(ResumeThread(Threads[0]) == 1, "ResumeThread failed with %lu\n", GetLastError());

    Start = GetTickCount();
    while (!ReadAcquire(&YielderSpinning) && GetTickCount() - Start < 20000)
        Sleep(10);
    ok_long(YielderSpinning, 1);

    Threads[1] = CreateThread(NULL, 0, PeerThread, NULL, CREATE_SUSPENDED, NULL);
    ok(Threads[1] != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Threads[1])
    {
        ok(SetThreadAffinityMask(Threads[1], Processor) != 0, "SetThreadAffinityMask failed with %lu\n", GetLastError());
        ok(SetThreadPriorityBoost(Threads[1], TRUE), "SetThreadPriorityBoost failed with %lu\n", GetLastError());
        ok(ResumeThread(Threads[1]) == 1, "ResumeThread failed with %lu\n", GetLastError());

        Sleep(200);
        for (Index = 0; Index < 2; Index++)
            Before[Index] = RunTimeOf(Threads[Index]);

        Sleep(SHARE_MS);
        for (Index = 0; Index < 2; Index++)
            Used[Index] = RunTimeOf(Threads[Index]) - Before[Index];

        Total = Used[0] + Used[1];
        trace("QUANTUM_ROUND_ROBIN yielder_ms=%lu peer_ms=%lu\n", (ULONG)(Used[0] / 10000), (ULONG)(Used[1] / 10000));
        ok(Total != 0, "Neither thread ran\n");
        ok(Used[0] * 4 >= Total, "The thread that had yielded got %lu of %lu ms\n",
           (ULONG)(Used[0] / 10000), (ULONG)(Total / 10000));
        ok(Used[1] * 4 >= Total, "The thread behind the one that had yielded got %lu of %lu ms\n",
           (ULONG)(Used[1] / 10000), (ULONG)(Total / 10000));
    }

    InterlockedExchange(&StopSharing, 1);
    for (Index = 0; Index < 2; Index++)
    {
        if (!Threads[Index])
            continue;
        WaitForSingleObject(Threads[Index], INFINITE);
        CloseHandle(Threads[Index]);
    }
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
    Sleep(32);

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

    /* The last processor of the process is the one with the least else to do */
    Success = SetProcessAffinityMask(GetCurrentProcess(), ProcessMask);
    ok(Success, "SetProcessAffinityMask failed with %lu\n", GetLastError());
    while ((ProcessMask & (ProcessMask - 1)) != 0)
        ProcessMask &= ProcessMask - 1;
    TestSharesAfterYielding(ProcessMask);
}
