/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Setting a timer again while its completion routine is pending does not keep the timer alive
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/obfuncs.h>

#define RESET_TIMERS 64

static VOID CALLBACK
TimerApc(LPVOID Context, DWORD TimerLowValue, DWORD TimerHighValue)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(TimerLowValue);
    UNREFERENCED_PARAMETER(TimerHighValue);
}

static ULONG
QueryTimerObjects(HANDLE Timer)
{
    union
    {
        OBJECT_TYPE_INFORMATION Info;
        UCHAR Buffer[sizeof(OBJECT_TYPE_INFORMATION) + 64 * sizeof(WCHAR)];
    } Type;
    NTSTATUS Status;

    Status = NtQueryObject(Timer, ObjectTypeInformation, &Type, sizeof(Type), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status) ? Type.Info.TotalNumberOfObjects : 0;
}

START_TEST(WaitableTimerApcReset)
{
    LARGE_INTEGER DueTime;
    HANDLE Probe;
    HANDLE Timer;
    ULONG Before;
    ULONG After;
    ULONG Index;

    Probe = CreateWaitableTimerW(NULL, TRUE, NULL);
    ok(Probe != NULL, "CreateWaitableTimerW failed: %lu\n", GetLastError());
    if (Probe == NULL) return;

    Before = QueryTimerObjects(Probe);

    DueTime.QuadPart = -60LL * 1000 * 10000;
    for (Index = 0; Index < RESET_TIMERS; Index++)
    {
        Timer = CreateWaitableTimerW(NULL, TRUE, NULL);
        ok(Timer != NULL, "CreateWaitableTimerW failed: %lu\n", GetLastError());
        if (Timer == NULL) break;

        ok(SetWaitableTimer(Timer, &DueTime, 0, TimerApc, NULL, FALSE), "first set failed: %lu\n", GetLastError());
        ok(SetWaitableTimer(Timer, &DueTime, 0, TimerApc, NULL, FALSE), "second set failed: %lu\n", GetLastError());
        ok(CancelWaitableTimer(Timer), "CancelWaitableTimer failed: %lu\n", GetLastError());
        ok(CloseHandle(Timer), "CloseHandle failed: %lu\n", GetLastError());
    }

    After = QueryTimerObjects(Probe);
    ok(After < Before + RESET_TIMERS / 2, "%ld timers outlived their last handle\n", (LONG)(After - Before));

    CloseHandle(Probe);
}
