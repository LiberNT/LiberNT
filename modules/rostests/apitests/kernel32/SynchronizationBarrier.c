/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the synchronization barrier APIs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define BARRIER_JOIN_TIMEOUT 5000
#define BARRIER_VALID_FLAGS (SYNCHRONIZATION_BARRIER_FLAGS_SPIN_ONLY | \
                             SYNCHRONIZATION_BARRIER_FLAGS_BLOCK_ONLY | \
                             SYNCHRONIZATION_BARRIER_FLAGS_NO_DELETE)

typedef BOOL (WINAPI *FN_InitializeSynchronizationBarrier)(LPSYNCHRONIZATION_BARRIER, LONG, LONG);
typedef BOOL (WINAPI *FN_EnterSynchronizationBarrier)(LPSYNCHRONIZATION_BARRIER, DWORD);
typedef BOOL (WINAPI *FN_DeleteSynchronizationBarrier)(LPSYNCHRONIZATION_BARRIER);

static FN_InitializeSynchronizationBarrier pInitializeSynchronizationBarrier;
static FN_EnterSynchronizationBarrier pEnterSynchronizationBarrier;
static FN_DeleteSynchronizationBarrier pDeleteSynchronizationBarrier;

typedef struct _BARRIER_TEST
{
    SYNCHRONIZATION_BARRIER Barrier;
    volatile LONG Arrived;
    volatile LONG Winners;
} BARRIER_TEST, *PBARRIER_TEST;

typedef struct _BARRIER_WORKER
{
    PBARRIER_TEST Test;
    DWORD Flags;
    LONG Rounds;
    LONG ReinitThreads;
    HANDLE Ready;
    HANDLE Thread;
    LONG ArrivedAfterFirstRound;
} BARRIER_WORKER, *PBARRIER_WORKER;

typedef struct _BARRIER_DELETER
{
    LPSYNCHRONIZATION_BARRIER Barrier;
    HANDLE Thread;
    DWORD ThreadId;
    HANDLE Started;
    volatile LONG Returned;
} BARRIER_DELETER, *PBARRIER_DELETER;

static
DWORD
WINAPI
BarrierWorker(
    _In_ PVOID Parameter)
{
    PBARRIER_WORKER Worker = Parameter;
    LONG Round;

    for (Round = 0; Round < Worker->Rounds; Round++)
    {
        InterlockedIncrement(&Worker->Test->Arrived);
        if (Round == 0 && Worker->Ready != NULL)
            SetEvent(Worker->Ready);
        if (pEnterSynchronizationBarrier(&Worker->Test->Barrier, Worker->Flags))
            InterlockedIncrement(&Worker->Test->Winners);
        if (Round == 0)
            Worker->ArrivedAfterFirstRound = Worker->Test->Arrived;
    }

    if (Worker->ReinitThreads != 0)
    {
        pDeleteSynchronizationBarrier(&Worker->Test->Barrier);
        pInitializeSynchronizationBarrier(&Worker->Test->Barrier, Worker->ReinitThreads, -1);
    }

    return 0;
}

static
DWORD
WINAPI
BarrierDeleter(
    _In_ PVOID Parameter)
{
    PBARRIER_DELETER Deleter = Parameter;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetEvent(Deleter->Started);
    pDeleteSynchronizationBarrier(Deleter->Barrier);
    InterlockedExchange(&Deleter->Returned, 1);
    return 0;
}

static
BOOL
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
BOOL
WaitForDeleterBlocked(
    _In_ PBARRIER_DELETER Deleter)
{
    ULONGLONG Deadline = GetTickCount64() + BARRIER_JOIN_TIMEOUT;
    ULONG Size = 0x40000;
    PVOID Buffer = NULL;
    BOOL Blocked = FALSE;
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
BOOL
WaitForBarrierEntry(
    _In_ LPSYNCHRONIZATION_BARRIER Barrier,
    _In_ const SYNCHRONIZATION_BARRIER *Initial,
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
BOOL
StartWorker(
    _Inout_ PBARRIER_WORKER Worker,
    _In_ PBARRIER_TEST Test,
    _In_ DWORD Flags,
    _In_ LONG Rounds,
    _In_opt_ HANDLE Ready)
{
    Worker->Test = Test;
    Worker->Flags = Flags;
    Worker->Rounds = Rounds;
    Worker->Ready = Ready;
    Worker->ArrivedAfterFirstRound = -1;
    Worker->Thread = CreateThread(NULL, 0, BarrierWorker, Worker, 0, NULL);
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
TestParameters(VOID)
{
    SYNCHRONIZATION_BARRIER Barrier;
    BOOL Result;

    SetLastError(0xdeadbeef);
    Result = pInitializeSynchronizationBarrier(&Barrier, 1, -1);
    ok_eq_bool(Result, TRUE);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);

    SetLastError(0xdeadbeef);
    Result = pInitializeSynchronizationBarrier(NULL, 1, -1);
    ok_eq_bool(Result, FALSE);
    ok_eq_ulong(GetLastError(), ERROR_INVALID_PARAMETER);

    SetLastError(0xdeadbeef);
    Result = pInitializeSynchronizationBarrier(&Barrier, -2, -1);
    ok_eq_bool(Result, FALSE);
    ok_eq_ulong(GetLastError(), ERROR_INVALID_PARAMETER);

    SetLastError(0xdeadbeef);
    Result = pInitializeSynchronizationBarrier(&Barrier, 1, -2);
    ok_eq_bool(Result, FALSE);
    ok_eq_ulong(GetLastError(), ERROR_INVALID_PARAMETER);

    SetLastError(0xdeadbeef);
    Result = pInitializeSynchronizationBarrier(&Barrier, 0, -1);
    ok_eq_bool(Result, FALSE);
    ok_eq_ulong(GetLastError(), ERROR_INVALID_PARAMETER);

    SetLastError(0xdeadbeef);
    Result = pInitializeSynchronizationBarrier(&Barrier, 1, INT_MAX);
    ok_eq_bool(Result, TRUE);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);

    SetLastError(0xdeadbeef);
    Result = pInitializeSynchronizationBarrier(&Barrier, INT_MAX, 0);
    ok_eq_bool(Result, TRUE);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);
}

static
VOID
TestSingleThread(VOID)
{
    SYNCHRONIZATION_BARRIER Barrier;
    BOOL Result;

    ok_eq_bool(pInitializeSynchronizationBarrier(&Barrier, 1, -1), TRUE);

    SetLastError(0xdeadbeef);
    Result = pEnterSynchronizationBarrier(&Barrier, 0);
    ok_eq_bool(Result, TRUE);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);

    SetLastError(0xdeadbeef);
    Result = pEnterSynchronizationBarrier(&Barrier, 0);
    ok_eq_bool(Result, TRUE);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);

    SetLastError(0xdeadbeef);
    Result = pDeleteSynchronizationBarrier(&Barrier);
    ok_eq_bool(Result, TRUE);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);
}

static
VOID
TestFlags(VOID)
{
    SYNCHRONIZATION_BARRIER Barrier;
    NTSTATUS ExceptionCode;
    DWORD Flags;
    BOOL Result;
    ULONG Bit;

    ok_eq_bool(pInitializeSynchronizationBarrier(&Barrier, 1, -1), TRUE);

    for (Bit = 0; Bit < 32; Bit++)
    {
        Flags = 1UL << Bit;
        Result = -1;
        ExceptionCode = STATUS_SUCCESS;
        SetLastError(0xdeadbeef);
        _SEH2_TRY
        {
            Result = pEnterSynchronizationBarrier(&Barrier, Flags);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ExceptionCode = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (Flags & ~BARRIER_VALID_FLAGS)
        {
            ok(ExceptionCode == STATUS_INVALID_PARAMETER, "Flags 0x%lx: exception 0x%08lx\n", Flags, ExceptionCode);
            ok(Result == -1, "Flags 0x%lx: returned %d\n", Flags, Result);
        }
        else
        {
            ok(ExceptionCode == STATUS_SUCCESS, "Flags 0x%lx: exception 0x%08lx\n", Flags, ExceptionCode);
            ok(Result == TRUE, "Flags 0x%lx: returned %d\n", Flags, Result);
            ok(GetLastError() == 0xdeadbeef, "Flags 0x%lx: last error %lu\n", Flags, GetLastError());
        }
    }

    ok_eq_bool(pEnterSynchronizationBarrier(&Barrier, BARRIER_VALID_FLAGS), TRUE);
}

static
VOID
TestPhase(VOID)
{
    BARRIER_TEST Test = { 0 };
    BARRIER_WORKER Workers[4] = { { 0 } };
    BOOL Started = TRUE;
    ULONG Index;

    ok_eq_bool(pInitializeSynchronizationBarrier(&Test.Barrier, 4, -1), TRUE);
    ok_eq_bool(pDeleteSynchronizationBarrier(&Test.Barrier), TRUE);

    Workers[0].ReinitThreads = 4;
    for (Index = 0; Index < 4 && Started; Index++)
        Started = StartWorker(&Workers[Index], &Test, 0, 1, NULL);
    if (!Started)
        return;

    for (Index = 0; Index < 4; Index++)
    {
        JoinThread(Workers[Index].Thread);
        ok(Workers[Index].ArrivedAfterFirstRound == 4, "Worker %lu left after %ld arrivals\n",
           Index, Workers[Index].ArrivedAfterFirstRound);
    }
    ok_eq_long(Test.Winners, 1);
}

static
VOID
TestReuse(VOID)
{
    BARRIER_TEST Test = { 0 };
    BARRIER_WORKER Workers[2] = { { 0 } };
    ULONG Index;

    ok_eq_bool(pInitializeSynchronizationBarrier(&Test.Barrier, 2, -1), TRUE);

    if (!StartWorker(&Workers[0], &Test, SYNCHRONIZATION_BARRIER_FLAGS_NO_DELETE, 2, NULL))
        return;
    if (!StartWorker(&Workers[1], &Test, SYNCHRONIZATION_BARRIER_FLAGS_NO_DELETE, 2, NULL))
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
TestOneThreadBarrier(VOID)
{
    BARRIER_TEST Test = { 0 };
    BARRIER_WORKER Workers[4] = { { 0 } };
    ULONG Started = 0;
    ULONG Index;

    ok_eq_bool(pInitializeSynchronizationBarrier(&Test.Barrier, 1, -1), TRUE);

    for (Index = 0; Index < 4; Index++)
    {
        if (StartWorker(&Workers[Index], &Test, 0, 1, NULL))
            Started++;
    }

    for (Index = 0; Index < 4; Index++)
        JoinThread(Workers[Index].Thread);
    ok_eq_long(Test.Winners, (LONG)Started);
    ok_eq_ulong(Started, 4);
}

static
VOID
TestDelete(
    _In_ DWORD WaiterFlags)
{
    BARRIER_TEST Test = { 0 };
    BARRIER_WORKER Waiter = { 0 };
    BARRIER_WORKER Releaser = { 0 };
    BARRIER_DELETER Deleter = { 0 };
    SYNCHRONIZATION_BARRIER Initial;
    HANDLE Ready;
    DWORD Wait;

    ok_eq_bool(pInitializeSynchronizationBarrier(&Test.Barrier, 2, -1), TRUE);
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
    Deleter.Thread = CreateThread(NULL, 0, BarrierDeleter, &Deleter, 0, &Deleter.ThreadId);
    ok(Deleter.Thread != NULL, "CreateThread failed with %lu\n", GetLastError());

    if (WaiterFlags & SYNCHRONIZATION_BARRIER_FLAGS_NO_DELETE)
    {
        Wait = WaitForSingleObject(Deleter.Thread, BARRIER_JOIN_TIMEOUT);
        ok(Wait == WAIT_OBJECT_0, "Flags 0x%lx: delete wait returned 0x%lx\n", WaiterFlags, Wait);
        Wait = WaitForSingleObject(Waiter.Thread, 0);
        ok(Wait == WAIT_TIMEOUT, "Flags 0x%lx: waiter wait returned 0x%lx\n", WaiterFlags, Wait);
    }
    else
    {
        ok(WaitForDeleterBlocked(&Deleter), "Flags 0x%lx: delete did not wait for the waiter (returned %ld)\n",
           WaiterFlags, Deleter.Returned);
    }

    if (StartWorker(&Releaser, &Test, SYNCHRONIZATION_BARRIER_FLAGS_NO_DELETE, 1, NULL))
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

static
VOID
TestSpinAndBlock(VOID)
{
    static const DWORD Flags[3] =
    {
        SYNCHRONIZATION_BARRIER_FLAGS_SPIN_ONLY,
        SYNCHRONIZATION_BARRIER_FLAGS_BLOCK_ONLY,
        SYNCHRONIZATION_BARRIER_FLAGS_SPIN_ONLY | SYNCHRONIZATION_BARRIER_FLAGS_BLOCK_ONLY,
    };
    BARRIER_TEST Test = { 0 };
    BARRIER_WORKER Workers[3] = { { 0 } };
    BOOL Started = TRUE;
    ULONG Index;

    ok_eq_bool(pInitializeSynchronizationBarrier(&Test.Barrier, 3, 0), TRUE);

    for (Index = 0; Index < 3 && Started; Index++)
        Started = StartWorker(&Workers[Index], &Test, Flags[Index], 1, NULL);
    if (!Started)
        return;

    for (Index = 0; Index < 3; Index++)
    {
        JoinThread(Workers[Index].Thread);
        ok(Workers[Index].ArrivedAfterFirstRound == 3, "Flags 0x%lx: left after %ld arrivals\n",
           Flags[Index], Workers[Index].ArrivedAfterFirstRound);
    }
    ok_eq_long(Test.Winners, 1);
    ok_eq_bool(pDeleteSynchronizationBarrier(&Test.Barrier), TRUE);
}

START_TEST(SynchronizationBarrier)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");

    pInitializeSynchronizationBarrier = (FN_InitializeSynchronizationBarrier)
        GetProcAddress(Kernel32, "InitializeSynchronizationBarrier");
    pEnterSynchronizationBarrier = (FN_EnterSynchronizationBarrier)
        GetProcAddress(Kernel32, "EnterSynchronizationBarrier");
    pDeleteSynchronizationBarrier = (FN_DeleteSynchronizationBarrier)
        GetProcAddress(Kernel32, "DeleteSynchronizationBarrier");

    ok(pInitializeSynchronizationBarrier != NULL, "kernel32!InitializeSynchronizationBarrier is missing\n");
    ok(pEnterSynchronizationBarrier != NULL, "kernel32!EnterSynchronizationBarrier is missing\n");
    ok(pDeleteSynchronizationBarrier != NULL, "kernel32!DeleteSynchronizationBarrier is missing\n");

    ok(KernelBase != NULL, "kernelbase.dll is not loaded\n");
    if (KernelBase != NULL)
    {
        ok(GetProcAddress(KernelBase, "InitializeSynchronizationBarrier") != NULL,
           "kernelbase!InitializeSynchronizationBarrier is missing\n");
        ok(GetProcAddress(KernelBase, "EnterSynchronizationBarrier") != NULL,
           "kernelbase!EnterSynchronizationBarrier is missing\n");
        ok(GetProcAddress(KernelBase, "DeleteSynchronizationBarrier") != NULL,
           "kernelbase!DeleteSynchronizationBarrier is missing\n");
    }

    if (!pInitializeSynchronizationBarrier || !pEnterSynchronizationBarrier || !pDeleteSynchronizationBarrier)
        return;

    TestParameters();
    TestSingleThread();
    TestFlags();
    TestPhase();
    TestReuse();
    TestOneThreadBarrier();
    TestDelete(0);
    TestDelete(SYNCHRONIZATION_BARRIER_FLAGS_SPIN_ONLY);
    TestDelete(SYNCHRONIZATION_BARRIER_FLAGS_NO_DELETE);
    TestSpinAndBlock();
}
