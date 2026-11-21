/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that threads run on every active processor and wake each other across processors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define PING_PONG_ROUNDS 200

typedef struct _PING_PONG
{
    HANDLE Ping;
    HANDLE Pong;
    LONG Rounds;
    DWORD Processor;
    BOOL WrongProcessor;
} PING_PONG, *PPING_PONG;

static
DWORD
WINAPI
PongThread(
    _In_ PVOID Parameter)
{
    PPING_PONG Context = Parameter;
    LONG Round;

    for (Round = 0; Round < PING_PONG_ROUNDS; Round++)
    {
        if (WaitForSingleObject(Context->Ping, 10000) != WAIT_OBJECT_0)
            return 1;
        if (GetCurrentProcessorNumber() != Context->Processor)
            Context->WrongProcessor = TRUE;
        InterlockedIncrement(&Context->Rounds);
        SetEvent(Context->Pong);
    }
    return 0;
}

static
DWORD
FirstOtherProcessor(
    _In_ DWORD_PTR ActiveMask,
    _In_ DWORD Self)
{
    DWORD Index;

    for (Index = 0; Index < sizeof(DWORD_PTR) * 8; Index++)
    {
        if ((ActiveMask & ((DWORD_PTR)1 << Index)) && (Index != Self))
            return Index;
    }
    return MAXDWORD;
}

static
VOID
TestEveryProcessor(
    _In_ DWORD_PTR ActiveMask)
{
    DWORD Index, Visited = 0;

    for (Index = 0; Index < sizeof(DWORD_PTR) * 8; Index++)
    {
        if (!(ActiveMask & ((DWORD_PTR)1 << Index)))
            continue;
        ok(SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << Index) != 0,
           "SetThreadAffinityMask(%lu) failed, error %lu\n", Index, GetLastError());
        ok_long(GetCurrentProcessorNumber(), Index);
        Visited++;
    }
    ok(Visited != 0, "No active processor was visited\n");
}

static
VOID
TestCrossProcessorWake(
    _In_ DWORD_PTR ActiveMask,
    _In_ DWORD Count)
{
    PING_PONG Context = { 0 };
    HANDLE Thread;
    DWORD Round, Other, ExitCode = MAXDWORD;

    if (Count < 2)
    {
        skip("A single processor is active\n");
        return;
    }

    ok(SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1) != 0, "SetThreadAffinityMask failed, error %lu\n", GetLastError());
    Other = FirstOtherProcessor(ActiveMask, 0);
    ok(Other != MAXDWORD, "No second processor in mask %Ix\n", (size_t)ActiveMask);
    if (Other == MAXDWORD)
        return;

    Context.Ping = CreateEventW(NULL, FALSE, FALSE, NULL);
    Context.Pong = CreateEventW(NULL, FALSE, FALSE, NULL);
    Context.Processor = Other;
    Thread = CreateThread(NULL, 0, PongThread, &Context, CREATE_SUSPENDED, NULL);
    ok(Context.Ping && Context.Pong && Thread, "Object creation failed, error %lu\n", GetLastError());
    if (!Context.Ping || !Context.Pong || !Thread)
        goto Cleanup;

    ok(SetThreadAffinityMask(Thread, (DWORD_PTR)1 << Other) != 0, "SetThreadAffinityMask failed, error %lu\n", GetLastError());
    ResumeThread(Thread);

    for (Round = 0; Round < PING_PONG_ROUNDS; Round++)
    {
        SetEvent(Context.Ping);
        if (WaitForSingleObject(Context.Pong, 10000) != WAIT_OBJECT_0)
            break;
        if (GetCurrentProcessorNumber() != 0)
            Context.WrongProcessor = TRUE;
    }

    ok_long(Round, PING_PONG_ROUNDS);
    ok_long(WaitForSingleObject(Thread, 10000), WAIT_OBJECT_0);
    ok(GetExitCodeThread(Thread, &ExitCode), "GetExitCodeThread failed, error %lu\n", GetLastError());
    ok_long(ExitCode, 0);
    ok_long(Context.Rounds, PING_PONG_ROUNDS);
    ok(!Context.WrongProcessor, "A thread ran outside its affinity\n");

Cleanup:
    if (Thread)
        CloseHandle(Thread);
    if (Context.Pong)
        CloseHandle(Context.Pong);
    if (Context.Ping)
        CloseHandle(Context.Ping);
}

START_TEST(ProcessorAffinity)
{
    DWORD_PTR ProcessMask, SystemMask, OldMask;
    SYSTEM_INFO Info;
    DWORD Count = 0, Index;

    GetSystemInfo(&Info);
    ok(GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask), "GetProcessAffinityMask failed, error %lu\n", GetLastError());
    ok(Info.dwActiveProcessorMask == SystemMask, "Active mask %Ix, system mask %Ix\n",
       (size_t)Info.dwActiveProcessorMask, (size_t)SystemMask);
    for (Index = 0; Index < sizeof(DWORD_PTR) * 8; Index++)
    {
        if (SystemMask & ((DWORD_PTR)1 << Index))
            Count++;
    }
    ok_long(Info.dwNumberOfProcessors, Count);

    OldMask = SetThreadAffinityMask(GetCurrentThread(), ProcessMask);
    ok(OldMask != 0, "SetThreadAffinityMask failed, error %lu\n", GetLastError());

    TestEveryProcessor(ProcessMask);
    TestCrossProcessorWake(ProcessMask, Count);

    SetThreadAffinityMask(GetCurrentThread(), OldMask ? OldMask : ProcessMask);
}
