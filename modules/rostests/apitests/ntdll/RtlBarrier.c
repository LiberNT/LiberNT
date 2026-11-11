/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RtlInitBarrier, RtlBarrier and RtlDeleteBarrier
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define BARRIER_JOIN_TIMEOUT 5000

typedef NTSTATUS (NTAPI *FN_RtlInitBarrier)(PRTL_BARRIER, LONG, LONG);
typedef BOOLEAN (NTAPI *FN_RtlBarrier)(PRTL_BARRIER, ULONG);
typedef VOID (NTAPI *FN_RtlDeleteBarrier)(PRTL_BARRIER);

static FN_RtlInitBarrier pRtlInitBarrier;
static FN_RtlBarrier pRtlBarrier;
static FN_RtlDeleteBarrier pRtlDeleteBarrier;

typedef struct _RTL_BARRIER_TEST
{
    RTL_BARRIER Barrier;
    volatile LONG Arrived;
    volatile LONG Winners;
} RTL_BARRIER_TEST, *PRTL_BARRIER_TEST;

typedef struct _RTL_BARRIER_WORKER
{
    PRTL_BARRIER_TEST Test;
    ULONG Flags;
    LONG Rounds;
    HANDLE Ready;
    HANDLE Thread;
    LONG ArrivedAfterFirstRound;
} RTL_BARRIER_WORKER, *PRTL_BARRIER_WORKER;

typedef struct _RTL_BARRIER_DELETER
{
    PRTL_BARRIER Barrier;
    HANDLE Thread;
    DWORD ThreadId;
    HANDLE Started;
    volatile LONG Returned;
} RTL_BARRIER_DELETER, *PRTL_BARRIER_DELETER;

static
DWORD
WINAPI
RtlBarrierWorker(
    _In_ PVOID Parameter)
{
    PRTL_BARRIER_WORKER Worker = Parameter;
    LONG Round;

    for (Round = 0; Round < Worker->Rounds; Round++)
    {
        InterlockedIncrement(&Worker->Test->Arrived);
        if (Round == 0 && Worker->Ready != NULL)
            SetEvent(Worker->Ready);
        if (pRtlBarrier(&Worker->Test->Barrier, Worker->Flags))
            InterlockedIncrement(&Worker->Test->Winners);
        if (Round == 0)
            Worker->ArrivedAfterFirstRound = Worker->Test->Arrived;
    }

    return 0;
}

static
DWORD
WINAPI
RtlBarrierDeleter(
    _In_ PVOID Parameter)
{
    PRTL_BARRIER_DELETER Deleter = Parameter;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetEvent(Deleter->Started);
    pRtlDeleteBarrier(Deleter->Barrier);
    InterlockedExchange(&Deleter->Returned, 1);
    return 0;
}

static
BOOLEAN
IsThreadWaiting(
    _In_ PSYSTEM_PROCESS_INFORMATION Process,
    _In_ DWORD ThreadId)
{
    PSYSTEM_THREAD_INFORMATION Thread;
    ULONG Index;

    while (Process->UniqueProcessId != UlongToHandle(GetCurrentProcessId()))
    {
        if (Process->NextEntryOffset == 0)
            return FALSE;
        Process = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)Process + Process->NextEntryOffset);
    }

    Thread = (PSYSTEM_THREAD_INFORMATION)(Process + 1);
    for (Index = 0; Index < Process->NumberOfThreads; Index++)
    {
        if (Thread[Index].ClientId.UniqueThread == UlongToHandle(ThreadId))
            return Thread[Index].ThreadState == Waiting;
    }

    return FALSE;
}

static
BOOLEAN
WaitForDeleterBlocked(
    _In_ PRTL_BARRIER_DELETER Deleter)
{
    ULONGLONG Deadline = GetTickCount64() + BARRIER_JOIN_TIMEOUT;
    ULONG Size = 0x40000;
    PVOID Buffer = NULL;
    BOOLEAN Blocked = FALSE;
    NTSTATUS Status;

    if (WaitForSingleObject(Deleter->Started, BARRIER_JOIN_TIMEOUT) != WAIT_OBJECT_0)
        return FALSE;

    while (!Blocked && WaitForSingleObject(Deleter->Thread, 0) == WAIT_TIMEOUT && GetTickCount64() < Deadline)
    {
        if (Buffer == NULL)
        {
            Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
            if (Buffer == NULL)
                break;
        }

        Status = NtQuerySystemInformation(SystemProcessInformation, Buffer, Size, NULL);
        if (Status == STATUS_INFO_LENGTH_MISMATCH)
        {
            HeapFree(GetProcessHeap(), 0, Buffer);
            Buffer = NULL;
            Size *= 2;
            continue;
        }
        if (!NT_SUCCESS(Status))
            break;

        Blocked = IsThreadWaiting(Buffer, Deleter->ThreadId) && !ReadAcquire(&Deleter->Returned);
        if (!Blocked)
            SwitchToThread();
    }

    if (Buffer != NULL)
        HeapFree(GetProcessHeap(), 0, Buffer);

    return Blocked;
}

static
BOOLEAN
WaitForBarrierEntry(
    _In_ PRTL_BARRIER Barrier,
    _In_ const RTL_BARRIER *Initial,
    _In_ HANDLE Thread)
{
    ULONGLONG Deadline = GetTickCount64() + BARRIER_JOIN_TIMEOUT;

    while (RtlCompareMemory(Barrier, Initial, sizeof(*Initial)) == sizeof(*Initial))
    {
        if (WaitForSingleObject(Thread, 0) != WAIT_TIMEOUT || GetTickCount64() >= Deadline)
            return FALSE;
        SwitchToThread();
    }

    return TRUE;
}

static
BOOLEAN
StartWorker(
    _Inout_ PRTL_BARRIER_WORKER Worker,
    _In_ PRTL_BARRIER_TEST Test,
    _In_ ULONG Flags,
    _In_ LONG Rounds,
    _In_opt_ HANDLE Ready)
{
    Worker->Test = Test;
    Worker->Flags = Flags;
    Worker->Rounds = Rounds;
    Worker->Ready = Ready;
    Worker->ArrivedAfterFirstRound = -1;
    Worker->Thread = CreateThread(NULL, 0, RtlBarrierWorker, Worker, 0, NULL);
    ok(Worker->Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    return Worker->Thread != NULL;
}

static
VOID
JoinThread(
    _In_opt_ HANDLE Thread)
{
    if (Thread == NULL)
        return;

    ok_eq_ulong(WaitForSingleObject(Thread, BARRIER_JOIN_TIMEOUT), WAIT_OBJECT_0);
    CloseHandle(Thread);
}

static
VOID
TestInit(VOID)
{
    RTL_BARRIER Barrier;

    ok_eq_hex(pRtlInitBarrier(NULL, 1, -1), STATUS_INVALID_PARAMETER);
    ok_eq_hex(pRtlInitBarrier(&Barrier, -2, -1), STATUS_SUCCESS);
    ok_eq_hex(pRtlInitBarrier(&Barrier, INT_MAX, -1), STATUS_SUCCESS);
    ok_eq_hex(pRtlInitBarrier(&Barrier, 0, -1), STATUS_SUCCESS);
    ok_eq_hex(pRtlInitBarrier(&Barrier, 1, -2), STATUS_SUCCESS);
    ok_eq_hex(pRtlInitBarrier(&Barrier, 1, INT_MAX), STATUS_SUCCESS);
}

static
VOID
TestSingleThread(VOID)
{
    RTL_BARRIER Barrier;

    ok_eq_hex(pRtlInitBarrier(&Barrier, 1, -1), STATUS_SUCCESS);
    ok_eq_int(pRtlBarrier(&Barrier, 0), TRUE);
    ok_eq_int(pRtlBarrier(NULL, 0), FALSE);
    ok_eq_int(pRtlBarrier(&Barrier, 0), TRUE);
    pRtlDeleteBarrier(NULL);
    pRtlDeleteBarrier(&Barrier);
    ok_eq_int(pRtlBarrier(&Barrier, 0), TRUE);
}

static
VOID
TestReuse(VOID)
{
    RTL_BARRIER_TEST Test = { 0 };
    RTL_BARRIER_WORKER Workers[2] = { { 0 } };
    ULONG Index;

    ok_eq_hex(pRtlInitBarrier(&Test.Barrier, 2, -1), STATUS_SUCCESS);
    if (!StartWorker(&Workers[0], &Test, 0, 2, NULL))
        return;
    if (!StartWorker(&Workers[1], &Test, 0, 2, NULL))
        return;

    for (Index = 0; Index < 2; Index++)
    {
        JoinThread(Workers[Index].Thread);
        ok(Workers[Index].ArrivedAfterFirstRound >= 2, "Worker %lu left the first phase after %ld arrivals\n",
           Index, Workers[Index].ArrivedAfterFirstRound);
    }
    ok_eq_long(Test.Arrived, 4);
    ok_eq_long(Test.Winners, 2);
}

static
VOID
TestDelete(
    _In_ ULONG WaiterFlags)
{
    RTL_BARRIER_TEST Test = { 0 };
    RTL_BARRIER_WORKER Waiter = { 0 };
    RTL_BARRIER_WORKER Releaser = { 0 };
    RTL_BARRIER_DELETER Deleter = { 0 };
    RTL_BARRIER Initial;
    HANDLE Ready;
    DWORD Wait;

    ok_eq_hex(pRtlInitBarrier(&Test.Barrier, 2, -1), STATUS_SUCCESS);
    Initial = Test.Barrier;

    Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Ready != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (Ready == NULL)
        return;

    if (!StartWorker(&Waiter, &Test, WaiterFlags, 1, Ready))
    {
        CloseHandle(Ready);
        return;
    }

    ok(WaitForBarrierEntry(&Test.Barrier, &Initial, Waiter.Thread),
       "Flags 0x%lx: the waiter did not enter the barrier\n", WaiterFlags);
    ok_eq_ulong(WaitForSingleObject(Ready, BARRIER_JOIN_TIMEOUT), WAIT_OBJECT_0);

    Deleter.Barrier = &Test.Barrier;
    Deleter.Started = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Deleter.Started != NULL, "CreateEventW failed with %lu\n", GetLastError());
    Deleter.Thread = CreateThread(NULL, 0, RtlBarrierDeleter, &Deleter, 0, &Deleter.ThreadId);
    ok(Deleter.Thread != NULL, "CreateThread failed with %lu\n", GetLastError());

    if (WaiterFlags & 0x10000)
    {
        ok(WaitForDeleterBlocked(&Deleter), "Flags 0x%lx: delete did not wait for the waiter (returned %ld)\n",
           WaiterFlags, Deleter.Returned);
    }
    else
    {
        Wait = WaitForSingleObject(Deleter.Thread, BARRIER_JOIN_TIMEOUT);
        ok(Wait == WAIT_OBJECT_0, "Flags 0x%lx: delete wait returned 0x%lx\n", WaiterFlags, Wait);
        Wait = WaitForSingleObject(Waiter.Thread, 0);
        ok(Wait == WAIT_TIMEOUT, "Flags 0x%lx: waiter wait returned 0x%lx\n", WaiterFlags, Wait);
    }

    if (StartWorker(&Releaser, &Test, 0, 1, NULL))
        JoinThread(Releaser.Thread);

    JoinThread(Deleter.Thread);
    JoinThread(Waiter.Thread);
    ok(Waiter.ArrivedAfterFirstRound == 2, "Flags 0x%lx: waiter left after %ld arrivals\n",
       WaiterFlags, Waiter.ArrivedAfterFirstRound);
    ok(Test.Winners == 1, "Flags 0x%lx: %ld winners\n", WaiterFlags, Test.Winners);
    if (Deleter.Started != NULL)
        CloseHandle(Deleter.Started);
    CloseHandle(Ready);
}

START_TEST(RtlBarrier)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");

    pRtlInitBarrier = (FN_RtlInitBarrier)GetProcAddress(Ntdll, "RtlInitBarrier");
    pRtlBarrier = (FN_RtlBarrier)GetProcAddress(Ntdll, "RtlBarrier");
    pRtlDeleteBarrier = (FN_RtlDeleteBarrier)GetProcAddress(Ntdll, "RtlDeleteBarrier");

    ok(pRtlInitBarrier != NULL, "RtlInitBarrier is missing\n");
    ok(pRtlBarrier != NULL, "RtlBarrier is missing\n");
    ok(pRtlDeleteBarrier != NULL, "RtlDeleteBarrier is missing\n");
    if (!pRtlInitBarrier || !pRtlBarrier || !pRtlDeleteBarrier)
        return;

    TestInit();
    TestSingleThread();
    TestReuse();
    TestDelete(0);
    TestDelete(0x10000);
    TestDelete(~0UL);
    TestDelete(1);
}
