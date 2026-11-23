/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which ends of a thread give its user stack back
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

static DWORD WINAPI ReturnFromThread(PVOID Context)
{
    *(PVOID *)Context = NtCurrentTeb()->DeallocationStack;
    return 0;
}

static DWORD WINAPI ExitFromThread(PVOID Context)
{
    *(PVOID *)Context = NtCurrentTeb()->DeallocationStack;
    ExitThread(0);
}

static DWORD WINAPI WaitInThread(PVOID Context)
{
    *(PVOID *)Context = NtCurrentTeb()->DeallocationStack;
    Sleep(INFINITE);
    return 0;
}

static DWORD StackState(PVOID Stack)
{
    MEMORY_BASIC_INFORMATION Info;

    if (VirtualQuery(Stack, &Info, sizeof(Info)) != sizeof(Info))
        return 0;
    return Info.State;
}

static VOID TestEnd(LPTHREAD_START_ROUTINE Routine, BOOL Terminate, DWORD Expected, PCSTR Name)
{
    PVOID Stack = NULL;
    HANDLE Thread;
    ULONG Wait;

    Thread = CreateThread(NULL, 0, Routine, &Stack, 0, NULL);
    ok(Thread != NULL, "%s: CreateThread failed: %lu\n", Name, GetLastError());
    if (!Thread)
        return;
    for (Wait = 0; Wait < 100 && !*(volatile PVOID *)&Stack; Wait++)
        Sleep(10);
    ok(Stack != NULL, "%s: the thread did not report its stack\n", Name);
    if (Terminate)
        ok(TerminateThread(Thread, 1), "%s: TerminateThread failed: %lu\n", Name, GetLastError());
    ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "%s: the thread did not end\n", Name);
    CloseHandle(Thread);
    if (!Stack)
        return;
    for (Wait = 0; Wait < 100 && StackState(Stack) != Expected; Wait++)
        Sleep(10);
    ok(StackState(Stack) == Expected, "%s: the stack at %p is in state 0x%lx, expected 0x%lx\n", Name, Stack,
       StackState(Stack), Expected);
    if (StackState(Stack) != MEM_FREE)
    {
        SIZE_T Size = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &Stack, &Size, MEM_RELEASE);
    }
}

START_TEST(ThreadStackRelease)
{
    TestEnd(ReturnFromThread, FALSE, MEM_FREE, "return");
    TestEnd(ExitFromThread, FALSE, MEM_FREE, "ExitThread");
    TestEnd(WaitInThread, TRUE, MEM_FREE, "TerminateThread");
}
