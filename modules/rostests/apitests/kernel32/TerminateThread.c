/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for TerminateThread on threads that are blocked or never ran
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static LONG s_ApcRuns;
static HANDLE s_Event;
static CRITICAL_SECTION s_Lock;

static VOID CALLBACK CountApc(ULONG_PTR Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    InterlockedIncrement(&s_ApcRuns);
}

static DWORD WINAPI ReturnOne(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    return 1;
}

static DWORD WINAPI WaitOnEvent(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    WaitForSingleObject(s_Event, INFINITE);
    return 1;
}

static DWORD WINAPI WaitAlertable(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    WaitForSingleObjectEx(s_Event, INFINITE, TRUE);
    return 1;
}

static DWORD WINAPI EnterLock(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    EnterCriticalSection(&s_Lock);
    LeaveCriticalSection(&s_Lock);
    return 1;
}

static void CheckTerminated(HANDLE Thread, DWORD ExitCode)
{
    DWORD Code = 0;

    ok(TerminateThread(Thread, ExitCode), "TerminateThread failed with %lu\n", GetLastError());
    ok_long(WaitForSingleObject(Thread, 5000), WAIT_OBJECT_0);
    ok(GetExitCodeThread(Thread, &Code), "GetExitCodeThread failed with %lu\n", GetLastError());
    ok_hex(Code, ExitCode);
}

static void Test_QueuedApcNeverRuns(void)
{
    HANDLE Thread;

    s_ApcRuns = 0;
    Thread = CreateThread(NULL, 0, ReturnOne, NULL, CREATE_SUSPENDED, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread == NULL)
        return;

    ok(QueueUserAPC(CountApc, Thread, 0), "QueueUserAPC failed with %lu\n", GetLastError());
    CheckTerminated(Thread, 0x55);
    ok_long(s_ApcRuns, 0);
    CloseHandle(Thread);
}

static void Test_BlockedThread(LPTHREAD_START_ROUTINE Routine, DWORD ExitCode)
{
    HANDLE Thread;

    Thread = CreateThread(NULL, 0, Routine, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread == NULL)
        return;

    ok_long(WaitForSingleObject(Thread, 200), WAIT_TIMEOUT);
    CheckTerminated(Thread, ExitCode);
    CloseHandle(Thread);
}

START_TEST(TerminateThread)
{
    s_Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(s_Event != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (s_Event == NULL)
        return;

    Test_QueuedApcNeverRuns();
    Test_BlockedThread(WaitOnEvent, 0x1234);
    Test_BlockedThread(WaitAlertable, 0x4321);

    ok(InitializeCriticalSectionEx(&s_Lock, 0, 0), "InitializeCriticalSectionEx failed with %lu\n", GetLastError());
    EnterCriticalSection(&s_Lock);
    Test_BlockedThread(EnterLock, 0x77);

    CloseHandle(s_Event);
}
