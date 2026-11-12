/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for NtWaitForKeyedEvent and NtReleaseKeyedEvent
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TEST_KEY ((PVOID)(ULONG_PTR)0x1000)

typedef struct _KEYED_WAITER
{
    HANDLE KeyedEvent;
    HANDLE Started;
    NTSTATUS Status;
} KEYED_WAITER, *PKEYED_WAITER;

static DWORD WINAPI
KeyedWaiterThread(PVOID Context)
{
    PKEYED_WAITER Waiter = Context;
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -30000000LL;
    SetEvent(Waiter->Started);
    Waiter->Status = NtWaitForKeyedEvent(Waiter->KeyedEvent, TEST_KEY, FALSE, &Timeout);
    return 0;
}

static DWORD WINAPI
UntimedKeyedWaiterThread(PVOID Context)
{
    PKEYED_WAITER Waiter = Context;

    SetEvent(Waiter->Started);
    Waiter->Status = NtWaitForKeyedEvent(Waiter->KeyedEvent, TEST_KEY, FALSE, NULL);
    return 0;
}

static LONG ApcRan;

static VOID NTAPI
KeyedWaitApc(ULONG_PTR Parameter)
{
    InterlockedIncrement(&ApcRan);
}

static DWORD WINAPI
AlertableKeyedWaiterThread(PVOID Context)
{
    PKEYED_WAITER Waiter = Context;
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -30000000LL;
    SetEvent(Waiter->Started);
    Waiter->Status = NtWaitForKeyedEvent(Waiter->KeyedEvent, TEST_KEY, TRUE, &Timeout);
    return 0;
}

static ULONG_PTR ProcessInformation[0x20000];

/* The thread's state, wait reason and context switch count; FALSE if it was not found */
static BOOLEAN
QueryThreadWait(DWORD ThreadId, PULONG State, PULONG WaitReason, PULONG ContextSwitches)
{
    PSYSTEM_PROCESS_INFORMATION Process = (PSYSTEM_PROCESS_INFORMATION)ProcessInformation;
    NTSTATUS Status;
    ULONG i;

    Status = NtQuerySystemInformation(SystemProcessInformation, ProcessInformation, sizeof(ProcessInformation), NULL);
    if (!NT_SUCCESS(Status))
        return FALSE;

    for (;;)
    {
        if (HandleToUlong(Process->UniqueProcessId) == GetCurrentProcessId())
        {
            PSYSTEM_THREAD_INFORMATION Thread = (PSYSTEM_THREAD_INFORMATION)(Process + 1);

            for (i = 0; i < Process->NumberOfThreads; i++)
            {
                if (HandleToUlong(Thread[i].ClientId.UniqueThread) == ThreadId)
                {
                    *State = Thread[i].ThreadState;
                    *WaitReason = Thread[i].WaitReason;
                    *ContextSwitches = Thread[i].ContextSwitches;
                    return TRUE;
                }
            }
            return FALSE;
        }

        if (!Process->NextEntryOffset)
            return FALSE;

        Process = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)Process + Process->NextEntryOffset);
    }
}

/* Whether the thread has stayed blocked in one keyed event wait for a while */
static BOOLEAN
IsWaitingOnKeyedEvent(DWORD ThreadId)
{
    ULONG State, WaitReason, Switches, LastSwitches;

    if (!QueryThreadWait(ThreadId, &State, &WaitReason, &LastSwitches) ||
        (State != Waiting) || (WaitReason != WrKeyedEvent))
    {
        return FALSE;
    }

    Sleep(50);

    return QueryThreadWait(ThreadId, &State, &WaitReason, &Switches) &&
           (State == Waiting) && (WaitReason == WrKeyedEvent) && (Switches == LastSwitches);
}

static VOID
Test_TimedOutWaitLeavesOtherWaiter(VOID)
{
    KEYED_WAITER Waiter = { NULL, NULL, STATUS_PENDING };
    LARGE_INTEGER Timeout;
    HANDLE Thread;
    DWORD ThreadId, Result;
    NTSTATUS Status;
    ULONG i;

    Status = NtCreateKeyedEvent(&Waiter.KeyedEvent, KEYEDEVENT_ALL_ACCESS, NULL, 0);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Waiter.Started = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Waiter.Started != NULL, "CreateEventW failed %lu\n", GetLastError());
    Thread = CreateThread(NULL, 0, KeyedWaiterThread, &Waiter, 0, &ThreadId);
    ok(Thread != NULL, "CreateThread failed %lu\n", GetLastError());
    if (!Thread || !Waiter.Started)
    {
        if (Thread)
            CloseHandle(Thread);
        if (Waiter.Started)
            CloseHandle(Waiter.Started);
        NtClose(Waiter.KeyedEvent);
        return;
    }

    ok(WaitForSingleObject(Waiter.Started, 5000) == WAIT_OBJECT_0, "The first waiter did not start\n");
    for (i = 0; i < 200 && !IsWaitingOnKeyedEvent(ThreadId); i++)
        Sleep(10);
    ok(IsWaitingOnKeyedEvent(ThreadId), "The first waiter is not waiting on the keyed event\n");

    /* A second waiter on the same key gives up: it must take only its own wait away */
    Timeout.QuadPart = -100000LL;
    Status = NtWaitForKeyedEvent(Waiter.KeyedEvent, TEST_KEY, FALSE, &Timeout);
    ok_ntstatus(Status, STATUS_TIMEOUT);
    ok(IsWaitingOnKeyedEvent(ThreadId), "The first waiter stopped waiting\n");

    Timeout.QuadPart = -10000000LL;
    Status = NtReleaseKeyedEvent(Waiter.KeyedEvent, TEST_KEY, FALSE, &Timeout);
    ok_ntstatus(Status, STATUS_SUCCESS);

    Result = WaitForSingleObject(Thread, 1000);
    ok(Result == WAIT_OBJECT_0, "The release did not wake the first waiter: %lu\n", Result);
    if (Result != WAIT_OBJECT_0)
        WaitForSingleObject(Thread, 5000);
    ok_ntstatus(Waiter.Status, STATUS_SUCCESS);

    CloseHandle(Thread);
    CloseHandle(Waiter.Started);
    NtClose(Waiter.KeyedEvent);
}

static VOID
Test_TimedOutReleaseIsForgotten(VOID)
{
    LARGE_INTEGER Timeout;
    HANDLE KeyedEvent;
    NTSTATUS Status;

    Status = NtCreateKeyedEvent(&KeyedEvent, KEYEDEVENT_ALL_ACCESS, NULL, 0);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Timeout.QuadPart = -100000LL;
    Status = NtReleaseKeyedEvent(KeyedEvent, TEST_KEY, FALSE, &Timeout);
    ok_ntstatus(Status, STATUS_TIMEOUT);

    Status = NtWaitForKeyedEvent(KeyedEvent, TEST_KEY, FALSE, &Timeout);
    ok_ntstatus(Status, STATUS_TIMEOUT);

    NtClose(KeyedEvent);
}

/* Starts a waiter thread and returns once it blocks on the keyed event; NULL on failure */
static HANDLE
StartKeyedWaiter(PKEYED_WAITER Waiter, LPTHREAD_START_ROUTINE Routine)
{
    HANDLE Thread;
    DWORD ThreadId;
    ULONG i;

    Waiter->Status = STATUS_PENDING;
    Waiter->Started = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Waiter->Started != NULL, "CreateEventW failed %lu\n", GetLastError());
    if (!Waiter->Started)
        return NULL;

    Thread = CreateThread(NULL, 0, Routine, Waiter, 0, &ThreadId);
    ok(Thread != NULL, "CreateThread failed %lu\n", GetLastError());
    if (!Thread)
    {
        CloseHandle(Waiter->Started);
        return NULL;
    }

    ok(WaitForSingleObject(Waiter->Started, 5000) == WAIT_OBJECT_0, "The waiter did not start\n");
    for (i = 0; i < 200 && !IsWaitingOnKeyedEvent(ThreadId); i++)
        Sleep(10);
    ok(IsWaitingOnKeyedEvent(ThreadId), "The waiter is not waiting on the keyed event\n");

    return Thread;
}

static VOID
Test_TerminatedWaiterIsForgotten(VOID)
{
    KEYED_WAITER Waiter = { NULL, NULL, STATUS_PENDING };
    LARGE_INTEGER Timeout;
    HANDLE Thread;
    NTSTATUS Status;
    DWORD Result;

    Status = NtCreateKeyedEvent(&Waiter.KeyedEvent, KEYEDEVENT_ALL_ACCESS, NULL, 0);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Thread = StartKeyedWaiter(&Waiter, UntimedKeyedWaiterThread);
    if (!Thread)
    {
        NtClose(Waiter.KeyedEvent);
        return;
    }

    ok(TerminateThread(Thread, 0xdead), "TerminateThread failed %lu\n", GetLastError());
    Result = WaitForSingleObject(Thread, 5000);
    ok(Result == WAIT_OBJECT_0, "The terminated waiter did not exit: %lu\n", Result);

    /* The terminated waiter's wait is gone, so nobody is left to release */
    Timeout.QuadPart = -100000LL;
    Status = NtReleaseKeyedEvent(Waiter.KeyedEvent, TEST_KEY, FALSE, &Timeout);
    ok_ntstatus(Status, STATUS_TIMEOUT);

    CloseHandle(Thread);
    CloseHandle(Waiter.Started);
    NtClose(Waiter.KeyedEvent);
}

static VOID
Test_AlertableWaitTakesUserApc(VOID)
{
    KEYED_WAITER Waiter = { NULL, NULL, STATUS_PENDING };
    LARGE_INTEGER Timeout;
    HANDLE Thread;
    NTSTATUS Status;
    DWORD Result;

    Status = NtCreateKeyedEvent(&Waiter.KeyedEvent, KEYEDEVENT_ALL_ACCESS, NULL, 0);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ApcRan = 0;
    Thread = StartKeyedWaiter(&Waiter, AlertableKeyedWaiterThread);
    if (!Thread)
    {
        NtClose(Waiter.KeyedEvent);
        return;
    }

    ok(QueueUserAPC(KeyedWaitApc, Thread, 0), "QueueUserAPC failed %lu\n", GetLastError());
    Result = WaitForSingleObject(Thread, 5000);
    ok(Result == WAIT_OBJECT_0, "The alertable waiter did not return: %lu\n", Result);
    ok_ntstatus(Waiter.Status, STATUS_USER_APC);
    ok(ApcRan == 1, "The APC ran %ld times\n", ApcRan);

    Timeout.QuadPart = -100000LL;
    Status = NtReleaseKeyedEvent(Waiter.KeyedEvent, TEST_KEY, FALSE, &Timeout);
    ok_ntstatus(Status, STATUS_TIMEOUT);

    CloseHandle(Thread);
    CloseHandle(Waiter.Started);
    NtClose(Waiter.KeyedEvent);
}

START_TEST(NtKeyedEvent)
{
    Test_TimedOutReleaseIsForgotten();
    Test_TimedOutWaitLeavesOtherWaiter();
    Test_AlertableWaitTakesUserApc();
    Test_TerminatedWaiterIsForgotten();
}
