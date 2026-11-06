/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     Tests for QueueUserAPC, SleepEx, WaitForSingleObjectEx etc.
 * COPYRIGHT:   Copyright 2020 Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 */
#include "precomp.h"

#define MAX_RECORD 30

static LONG s_record_count = 0;
static DWORD s_record[MAX_RECORD + 1] = { 0 };
static BOOL s_terminate_all = FALSE;
static HANDLE s_hMainRecorded;

static const DWORD s_expected[] =
{
    0, 1, 7, 8, 4,
    2, 1, 9, 10, 5,
    2, 1, 11, 12, 13,
    6, 2, 3, 14, 15,
    16
};
static const SIZE_T s_expected_count = _countof(s_expected);

static void AddValueToRecord(DWORD dwValue)
{
    LONG next = InterlockedIncrement(&s_record_count) - 1;
    if (next < MAX_RECORD)
        s_record[next] = dwValue;
}

static VOID CheckRecord(void)
{
    SIZE_T i, count = min(s_record_count, s_expected_count);

    for (i = 0; i < count; ++i)
    {
        ok(s_record[i] == s_expected[i], "s_record[%u]: got %lu vs expected %lu\n",
           (INT)i, s_record[i], s_expected[i]);
    }

    count = abs((int)s_record_count - (int)s_expected_count);
    for (i = 0; i < count; ++i)
    {
        ok(s_record_count == s_expected_count,
           "s_record_count: got %u vs expected %u\n",
           (int)s_record_count, (int)s_expected_count);
    }
}

static DWORD WINAPI ThreadFunc1(LPVOID arg)
{
    AddValueToRecord(0);
    while (!s_terminate_all)
    {
        AddValueToRecord(1);
        ok_long(SleepEx(INFINITE, TRUE), WAIT_IO_COMPLETION);
        AddValueToRecord(2);
    }
    AddValueToRecord(3);
    return 0;
}

static DWORD WINAPI ThreadFunc2(LPVOID arg)
{
    AddValueToRecord(0);
    while (!s_terminate_all)
    {
        AddValueToRecord(1);
        ok_long(WaitForSingleObjectEx(GetCurrentThread(), INFINITE, TRUE), WAIT_IO_COMPLETION);
        AddValueToRecord(2);
    }
    AddValueToRecord(3);
    return 0;
}

static VOID NTAPI DoUserAPC1(ULONG_PTR Parameter)
{
    ok_int((int)Parameter, 1);
    ok_long(WaitForSingleObject(s_hMainRecorded, 5 * 1000), WAIT_OBJECT_0);
    AddValueToRecord(4);
}

static VOID NTAPI DoUserAPC2(ULONG_PTR Parameter)
{
    ok_int((int)Parameter, 2);
    ok_long(WaitForSingleObject(s_hMainRecorded, 5 * 1000), WAIT_OBJECT_0);
    AddValueToRecord(5);
}

static VOID NTAPI DoUserAPC3(ULONG_PTR Parameter)
{
    ok_int((int)Parameter, 3);
    ok_long(WaitForSingleObject(s_hMainRecorded, 5 * 1000), WAIT_OBJECT_0);
    AddValueToRecord(6);
    s_terminate_all = TRUE;
}

static void JustDoIt(LPTHREAD_START_ROUTINE fn)
{
    HANDLE hThread;
    DWORD dwThreadId;

    s_terminate_all = FALSE;
    s_record_count = 0;
    ZeroMemory(s_record, sizeof(s_record));

    hThread = CreateThread(NULL, 0, fn, NULL, 0, &dwThreadId);
    ok(hThread != NULL, "hThread was NULL\n");

    Sleep(100);

    AddValueToRecord(7);
    ok_long(QueueUserAPC(DoUserAPC1, hThread, 1), 1);
    AddValueToRecord(8);
    ReleaseSemaphore(s_hMainRecorded, 1, NULL);

    Sleep(100);

    AddValueToRecord(9);
    ok_long(QueueUserAPC(DoUserAPC2, hThread, 2), 1);
    AddValueToRecord(10);
    ReleaseSemaphore(s_hMainRecorded, 1, NULL);

    Sleep(100);

    AddValueToRecord(11);
    ok_long(QueueUserAPC(DoUserAPC3, hThread, 3), 1);
    AddValueToRecord(12);

    AddValueToRecord(13);
    ReleaseSemaphore(s_hMainRecorded, 1, NULL);
    ok_long(WaitForSingleObject(hThread, 5 * 1000), WAIT_OBJECT_0);
    AddValueToRecord(14);

    AddValueToRecord(15);
    ok_int(CloseHandle(hThread), TRUE);
    hThread = NULL;
    AddValueToRecord(16);

    CheckRecord();
}

static void TestForSleepEx(void)
{
    JustDoIt(ThreadFunc1);
}

static void TestForWaitForSingleObjectEx(void)
{
    JustDoIt(ThreadFunc2);
}

static DWORD WINAPI ThreadFunc3(LPVOID arg)
{
    return 0;
}

static void TestMultipleUserAPCs(void)
{
    HANDLE hThread;
    DWORD dwThreadId;

    s_record_count = 0;

    hThread = CreateThread(NULL, 0, ThreadFunc3, NULL, CREATE_SUSPENDED, &dwThreadId);
    ok(hThread != NULL, "hThread was NULL\n");

    ok_long(QueueUserAPC(DoUserAPC1, hThread, 1), 1);
    ok_long(QueueUserAPC(DoUserAPC2, hThread, 2), 1);
    ok_long(QueueUserAPC(DoUserAPC3, hThread, 3), 1);

    ok_long(s_record_count, 0);

    ReleaseSemaphore(s_hMainRecorded, 3, NULL);
    ResumeThread(hThread);

    ok_long(WaitForSingleObject(hThread, 5 * 1000), WAIT_OBJECT_0);
    ok_int(CloseHandle(hThread), TRUE);

    ok_long(s_record_count, 3);
    ok_long(s_record[0], 4);
    ok_long(s_record[1], 5);
    ok_long(s_record[2], 6);
}

#define APC_WAIT_RACE_ITERATIONS 512

typedef struct _APC_WAIT_RACE_CONTEXT
{
    HANDLE Ready;
    HANDLE Done;
    HANDLE Never;
    volatile LONG Delivered;
    volatile LONG Failed;
} APC_WAIT_RACE_CONTEXT;

static APC_WAIT_RACE_CONTEXT RaceContext;

static VOID NTAPI RaceApc(ULONG_PTR Parameter)
{
    APC_WAIT_RACE_CONTEXT *Context = (APC_WAIT_RACE_CONTEXT *)Parameter;
    InterlockedIncrement(&Context->Delivered);
}

static DWORD WINAPI RaceWaitThread(LPVOID Parameter)
{
    APC_WAIT_RACE_CONTEXT *Context = Parameter;
    DWORD Result;
    LONG Index;

    for (Index = 0; Index < APC_WAIT_RACE_ITERATIONS; ++Index)
    {
        SetEvent(Context->Ready);
        Result = WaitForSingleObjectEx(Context->Never, 3000, TRUE);
        if (Result != WAIT_IO_COMPLETION || Context->Delivered != Index + 1)
        {
            InterlockedExchange(&Context->Failed, 1);
            SetEvent(Context->Done);
            return 1;
        }
        SetEvent(Context->Done);
    }
    return 0;
}

static void TestUserApcWaitRace(void)
{
    DWORD_PTR ProcessMask, SystemMask, FirstCpu, SecondCpu, PreviousMask;
    DWORD Result, Index;
    HANDLE Thread;
    BOOL Queued;

    if (!GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask))
    {
        skip("GetProcessAffinityMask failed %lu\n", GetLastError());
        return;
    }
    FirstCpu = ProcessMask & (0 - ProcessMask);
    ProcessMask &= ~FirstCpu;
    SecondCpu = ProcessMask & (0 - ProcessMask);
    if (!FirstCpu || !SecondCpu)
    {
        skip("Two processors are required for the APC wait race\n");
        return;
    }

    RaceContext.Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    RaceContext.Done = CreateEventW(NULL, FALSE, FALSE, NULL);
    RaceContext.Never = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(RaceContext.Ready && RaceContext.Done && RaceContext.Never,
       "CreateEventW failed %lu\n", GetLastError());
    if (!RaceContext.Ready || !RaceContext.Done || !RaceContext.Never)
        goto Cleanup;
    RaceContext.Delivered = 0;
    RaceContext.Failed = 0;
    Thread = CreateThread(NULL, 0, RaceWaitThread, &RaceContext, CREATE_SUSPENDED, NULL);
    ok(Thread != NULL, "CreateThread failed %lu\n", GetLastError());
    if (!Thread) goto Cleanup;
    ok(SetThreadAffinityMask(Thread, SecondCpu) != 0, "Target affinity failed %lu\n", GetLastError());
    PreviousMask = SetThreadAffinityMask(GetCurrentThread(), FirstCpu);
    ok(PreviousMask != 0, "Producer affinity failed %lu\n", GetLastError());
    ResumeThread(Thread);

    for (Index = 0; Index < APC_WAIT_RACE_ITERATIONS; ++Index)
    {
        Result = WaitForSingleObject(RaceContext.Ready, 5000);
        if (Result != WAIT_OBJECT_0) break;
        Queued = QueueUserAPC(RaceApc, Thread, (ULONG_PTR)&RaceContext);
        if (!Queued) break;
        Result = WaitForSingleObject(RaceContext.Done, 5000);
        if (Result != WAIT_OBJECT_0 || RaceContext.Failed) break;
    }
    ok(Index == APC_WAIT_RACE_ITERATIONS, "APC wait race stopped at %lu, result %lu, error %lu\n",
       Index, Result, GetLastError());
    if (PreviousMask) SetThreadAffinityMask(GetCurrentThread(), PreviousMask);
    if (Index != APC_WAIT_RACE_ITERATIONS) SetEvent(RaceContext.Never);
    Result = WaitForSingleObject(Thread, 5000);
    ok(Result == WAIT_OBJECT_0, "Worker did not terminate: %lu\n", Result);
    CloseHandle(Thread);
    if (Result != WAIT_OBJECT_0) return;
    ok(RaceContext.Delivered == APC_WAIT_RACE_ITERATIONS,
       "Delivered %ld APCs\n", RaceContext.Delivered);

Cleanup:
    if (RaceContext.Never) CloseHandle(RaceContext.Never);
    if (RaceContext.Done) CloseHandle(RaceContext.Done);
    if (RaceContext.Ready) CloseHandle(RaceContext.Ready);
}

START_TEST(QueueUserAPC)
{
    s_hMainRecorded = CreateSemaphoreW(NULL, 0, 3, NULL);
    ok(s_hMainRecorded != NULL, "CreateSemaphoreW failed\n");

    TestForSleepEx();
    TestForWaitForSingleObjectEx();
    TestMultipleUserAPCs();
    TestUserApcWaitRace();

    CloseHandle(s_hMainRecorded);
}
