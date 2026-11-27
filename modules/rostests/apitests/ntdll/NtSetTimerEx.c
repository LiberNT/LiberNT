/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for NtSetTimerEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static LONG ApcCount;

static
VOID
NTAPI
TimerApc(
    _In_ PVOID TimerContext,
    _In_ ULONG TimerLowValue,
    _In_ LONG TimerHighValue)
{
    InterlockedIncrement(&ApcCount);
}

static
NTSTATUS
SetTimer(
    _In_ HANDLE Timer,
    _In_ LONGLONG DueTime,
    _In_ ULONG Period,
    _In_ ULONG TolerableDelay,
    _In_opt_ PTIMER_APC_ROUTINE Apc,
    _In_opt_ PBOOLEAN PreviousState)
{
    TIMER_SET_COALESCABLE_TIMER_INFO Info;

    RtlZeroMemory(&Info, sizeof(Info));
    Info.DueTime.QuadPart = DueTime;
    Info.TimerApcRoutine = Apc;
    Info.Period = Period;
    Info.TolerableDelay = TolerableDelay;
    Info.PreviousState = PreviousState;
    return NtSetTimerEx(Timer, TimerSetCoalescableTimer, &Info, sizeof(Info));
}

START_TEST(NtSetTimerEx)
{
    TIMER_SET_COALESCABLE_TIMER_INFO Info;
    HANDLE Timer = NULL, Limited = NULL;
    BOOLEAN Previous;
    NTSTATUS Status;

    Status = NtCreateTimer(&Timer, TIMER_ALL_ACCESS, NULL, NotificationTimer);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Previous = 7;
    ok_ntstatus(SetTimer(Timer, -10000, 0, 0, NULL, &Previous), STATUS_SUCCESS);
    ok_int(Previous, FALSE);
    ok_long(WaitForSingleObject(Timer, 1000), WAIT_OBJECT_0);
    Previous = 7;
    ok_ntstatus(SetTimer(Timer, -10000, 0, 0, NULL, &Previous), STATUS_SUCCESS);
    ok_int(Previous, TRUE);

    RtlZeroMemory(&Info, sizeof(Info));
    Info.DueTime.QuadPart = -10000;
    ok_ntstatus(NtSetTimerEx(Timer, MaxTimerInfoClass, &Info, sizeof(Info)), STATUS_INVALID_INFO_CLASS);
    ok_ntstatus(NtSetTimerEx(Timer, MaxTimerInfoClass + 1, &Info, sizeof(Info)), STATUS_INVALID_INFO_CLASS);
    ok_ntstatus(NtSetTimerEx(Timer, TimerSetCoalescableTimer, &Info, sizeof(Info) - 1), STATUS_INFO_LENGTH_MISMATCH);
    ok_ntstatus(NtSetTimerEx(Timer, TimerSetCoalescableTimer, &Info, sizeof(Info) + 8), STATUS_INFO_LENGTH_MISMATCH);
    ok_ntstatus(NtSetTimerEx(Timer, TimerSetCoalescableTimer, &Info, 0), STATUS_INFO_LENGTH_MISMATCH);
    ok_ntstatus(NtSetTimerEx(Timer, TimerSetCoalescableTimer, NULL, sizeof(Info)), STATUS_ACCESS_VIOLATION);
    ok_ntstatus(NtSetTimerEx(Timer, TimerSetCoalescableTimer, (PVOID)(ULONG_PTR)0x10, sizeof(Info)), STATUS_ACCESS_VIOLATION);
    ok_ntstatus(NtSetTimerEx(NULL, TimerSetCoalescableTimer, &Info, sizeof(Info)), STATUS_INVALID_HANDLE);

    ok_ntstatus(SetTimer(Timer, -10000, 0x80000000, 0, NULL, NULL), STATUS_INVALID_PARAMETER_3);
    ok_ntstatus(SetTimer(Timer, -10000, 0x7fffffff, 0, NULL, NULL), STATUS_SUCCESS);
    ok_ntstatus(SetTimer(Timer, -10000, 0, 0xffffffff, NULL, NULL), STATUS_SUCCESS);
    ok_ntstatus(SetTimer(Timer, -10000, 0, 0, NULL, (PBOOLEAN)(ULONG_PTR)0x10), STATUS_SUCCESS);

    ApcCount = 0;
    ok_ntstatus(SetTimer(Timer, -10000, 0, 0, TimerApc, NULL), STATUS_SUCCESS);
    ok_long(SleepEx(1000, TRUE), WAIT_IO_COMPLETION);
    ok_long(ApcCount, 1);

    ok_ntstatus(SetTimer(Timer, -10000, 5, 0, NULL, NULL), STATUS_SUCCESS);
    ok_long(WaitForSingleObject(Timer, 1000), WAIT_OBJECT_0);
    ok_ntstatus(NtCancelTimer(Timer, NULL), STATUS_SUCCESS);

    ok(DuplicateHandle(GetCurrentProcess(), Timer, GetCurrentProcess(), &Limited, SYNCHRONIZE | TIMER_QUERY_STATE, FALSE, 0),
       "DuplicateHandle failed with %lu\n", GetLastError());
    ok_ntstatus(SetTimer(Limited, -10000, 0, 0, NULL, NULL), STATUS_ACCESS_DENIED);

    if (Limited) NtClose(Limited);
    NtClose(Timer);
}
