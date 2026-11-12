/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A waitable timer set with a completion routine is cancelled when the thread that set it exits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _TIMER_THREAD
{
    HANDLE Timer;
    BOOL WithApc;
    BOOL SetResult;
    DWORD SetError;
} TIMER_THREAD, *PTIMER_THREAD;

static volatile LONG TimerApcRuns;

static VOID CALLBACK
TimerApc(LPVOID Context, DWORD TimerLowValue, DWORD TimerHighValue)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(TimerLowValue);
    UNREFERENCED_PARAMETER(TimerHighValue);
    InterlockedIncrement(&TimerApcRuns);
}

static DWORD WINAPI
SetTimerAndExit(LPVOID Parameter)
{
    PTIMER_THREAD Data = Parameter;
    LARGE_INTEGER DueTime;

    DueTime.QuadPart = -50 * 10000;
    SetLastError(0xdeadbeef);
    Data->SetResult = SetWaitableTimer(Data->Timer,
                                       &DueTime,
                                       0,
                                       Data->WithApc ? TimerApc : NULL,
                                       NULL,
                                       FALSE);
    Data->SetError = GetLastError();
    return 0;
}

static VOID
RunSetter(PTIMER_THREAD Data)
{
    HANDLE Thread;

    Thread = CreateThread(NULL, 0, SetTimerAndExit, Data, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (Thread == NULL) return;

    ok_long(WaitForSingleObject(Thread, 5000), WAIT_OBJECT_0);
    ok(Data->SetResult, "SetWaitableTimer failed: %lu\n", Data->SetError);
    CloseHandle(Thread);
}

START_TEST(WaitableTimerThreadExit)
{
    TIMER_THREAD Data;
    HANDLE Timer;

    Timer = CreateWaitableTimerW(NULL, TRUE, NULL);
    ok(Timer != NULL, "CreateWaitableTimerW failed: %lu\n", GetLastError());
    if (Timer == NULL) return;

    TimerApcRuns = 0;
    ZeroMemory(&Data, sizeof(Data));
    Data.Timer = Timer;
    Data.WithApc = TRUE;
    RunSetter(&Data);
    ok_long(WaitForSingleObject(Timer, 150), WAIT_TIMEOUT);
    ok_long(TimerApcRuns, 0);

    ZeroMemory(&Data, sizeof(Data));
    Data.Timer = Timer;
    Data.WithApc = FALSE;
    RunSetter(&Data);
    ok_long(WaitForSingleObject(Timer, 1000), WAIT_OBJECT_0);

    ok(CloseHandle(Timer), "CloseHandle failed: %lu\n", GetLastError());
}
