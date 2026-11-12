/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A thread's execution state is its own and leaves its windowing state alone
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <ndk/pofuncs.h>

static
EXECUTION_STATE
SetState(
    _In_ EXECUTION_STATE Flags)
{
    EXECUTION_STATE Previous = 0;
    NTSTATUS Status;

    Status = NtSetThreadExecutionState(Flags, &Previous);
    ok_eq_hex(Status, STATUS_SUCCESS);
    return Previous;
}

static
DWORD
WINAPI
ContinuousThread(
    _In_ PVOID Parameter)
{
    EXECUTION_STATE Previous;

    UNREFERENCED_PARAMETER(Parameter);

    Previous = SetState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    ok_eq_hex(Previous, ES_CONTINUOUS);

    Previous = SetState(ES_CONTINUOUS);
    ok_eq_hex(Previous, ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

    Previous = SetState(ES_CONTINUOUS);
    ok_eq_hex(Previous, ES_CONTINUOUS);

    /* The thread leaves with a state still set, which its exit has to let go */
    Previous = SetState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    ok_eq_hex(Previous, ES_CONTINUOUS);
    return 0;
}

static
DWORD
WINAPI
WindowThread(
    _In_ PVOID Parameter)
{
    EXECUTION_STATE Previous;
    WCHAR Text[8];
    HWND Window;

    UNREFERENCED_PARAMETER(Parameter);

    Window = CreateWindowExW(0, L"STATIC", L"k", 0, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (Window == NULL) return 0;

    Previous = SetState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
    ok_eq_hex(Previous, ES_CONTINUOUS);

    ok(IsWindow(Window), "the window is gone\n");
    ok_eq_int(GetWindowTextW(Window, Text, RTL_NUMBER_OF(Text)), 1);
    ok(DestroyWindow(Window), "DestroyWindow failed with %lu\n", GetLastError());

    Previous = SetState(ES_CONTINUOUS);
    ok_eq_hex(Previous, ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
    return 0;
}

static
DWORD
WINAPI
AwayModeThread(
    _In_ PVOID Parameter)
{
    EXECUTION_STATE Previous = 0xdeadbeef;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Parameter);

    Status = NtSetThreadExecutionState(ES_AWAYMODE_REQUIRED, &Previous);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = NtSetThreadExecutionState(ES_CONTINUOUS | ES_USER_PRESENT, &Previous);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_hex(Previous, 0xdeadbeef);

    Previous = SetState(ES_CONTINUOUS | ES_AWAYMODE_REQUIRED);
    ok_eq_hex(Previous, ES_CONTINUOUS);

    Previous = SetState(ES_CONTINUOUS);
    ok_eq_hex(Previous, ES_CONTINUOUS | ES_AWAYMODE_REQUIRED);
    return 0;
}

static
VOID
RunThread(
    _In_ LPTHREAD_START_ROUTINE Routine)
{
    HANDLE Thread;
    DWORD Wait;

    Thread = CreateThread(NULL, 0, Routine, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread == NULL) return;

    Wait = WaitForSingleObject(Thread, 10000);
    ok_eq_ulong(Wait, WAIT_OBJECT_0);
    CloseHandle(Thread);
}

START_TEST(PoExecutionState)
{
    RunThread(ContinuousThread);
    RunThread(WindowThread);
    RunThread(AwayModeThread);
}
