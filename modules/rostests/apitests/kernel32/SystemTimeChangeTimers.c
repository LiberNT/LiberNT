/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that absolute timeouts expire when the system time moves past them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/obfuncs.h>

#define DAY_100NS (864000000000LL)
#define RELATIVE_DUE_100NS (4000000LL)

typedef struct _ABSOLUTE_WAIT
{
    HANDLE Event;
    LARGE_INTEGER DueTime;
    volatile NTSTATUS Status;
} ABSOLUTE_WAIT, *PABSOLUTE_WAIT;

static LONGLONG
QuerySystemTime100ns(VOID)
{
    FILETIME FileTime;
    ULARGE_INTEGER Time;

    GetSystemTimeAsFileTime(&FileTime);
    Time.LowPart = FileTime.dwLowDateTime;
    Time.HighPart = FileTime.dwHighDateTime;
    return (LONGLONG)Time.QuadPart;
}

static BOOL
ShiftSystemTime(
    _In_ LONGLONG Delta)
{
    FILETIME FileTime;
    SYSTEMTIME SystemTime;
    ULARGE_INTEGER Time;

    Time.QuadPart = (ULONGLONG)(QuerySystemTime100ns() + Delta);
    FileTime.dwLowDateTime = Time.LowPart;
    FileTime.dwHighDateTime = Time.HighPart;
    return FileTimeToSystemTime(&FileTime, &SystemTime) && SetSystemTime(&SystemTime);
}

static BOOL
EnableSystemTimePrivilege(VOID)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE Token;
    BOOL Result;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &Token))
        return FALSE;
    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    Result = LookupPrivilegeValue(NULL, SE_SYSTEMTIME_NAME, &Privileges.Privileges[0].Luid) &&
             AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL) &&
             GetLastError() == ERROR_SUCCESS;
    CloseHandle(Token);
    return Result;
}

static DWORD WINAPI
AbsoluteWaitThread(
    _In_ PVOID Parameter)
{
    PABSOLUTE_WAIT Wait = Parameter;

    Wait->Status = NtWaitForSingleObject(Wait->Event, FALSE, &Wait->DueTime);
    return 0;
}

START_TEST(SystemTimeChangeTimers)
{
    HANDLE AbsoluteTimer, RelativeTimer, Thread;
    LARGE_INTEGER DueTime;
    ABSOLUTE_WAIT Wait;

    if (!EnableSystemTimePrivilege())
    {
        skip("SeSystemtimePrivilege is not available\n");
        return;
    }

    AbsoluteTimer = CreateWaitableTimerW(NULL, TRUE, NULL);
    RelativeTimer = CreateWaitableTimerW(NULL, TRUE, NULL);
    Wait.Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    Wait.Status = STATUS_PENDING;
    ok(AbsoluteTimer && RelativeTimer && Wait.Event, "Failed to create the objects, error %lu\n", GetLastError());
    if (!AbsoluteTimer || !RelativeTimer || !Wait.Event)
        return;

    DueTime.QuadPart = QuerySystemTime100ns() + 30000000;
    ok(SetWaitableTimer(AbsoluteTimer, &DueTime, 0, NULL, NULL, FALSE), "SetWaitableTimer failed, error %lu\n", GetLastError());
    DueTime.QuadPart = -RELATIVE_DUE_100NS;
    ok(SetWaitableTimer(RelativeTimer, &DueTime, 0, NULL, NULL, FALSE), "SetWaitableTimer failed, error %lu\n", GetLastError());
    Wait.DueTime.QuadPart = QuerySystemTime100ns() + 30000000;
    Thread = CreateThread(NULL, 0, AbsoluteWaitThread, &Wait, 0, NULL);
    ok(Thread != NULL, "CreateThread failed, error %lu\n", GetLastError());

    Sleep(32);
    if (!ShiftSystemTime(DAY_100NS))
    {
        skip("SetSystemTime failed, error %lu\n", GetLastError());
        SetEvent(Wait.Event);
    }
    else
    {
        ok(WaitForSingleObject(AbsoluteTimer, 1000) == WAIT_OBJECT_0,
           "The absolute waitable timer did not expire when the system time moved past it\n");
        ok(Thread && WaitForSingleObject(Thread, 1000) == WAIT_OBJECT_0,
           "The absolute wait did not end when the system time moved past it\n");
        ok_hex(Wait.Status, STATUS_TIMEOUT);
        ok(WaitForSingleObject(RelativeTimer, 0) == WAIT_TIMEOUT,
           "The relative waitable timer expired when the system time moved\n");
        ok(ShiftSystemTime(-DAY_100NS), "Restoring the system time failed, error %lu\n", GetLastError());
        ok(WaitForSingleObject(RelativeTimer, 5000) == WAIT_OBJECT_0, "The relative waitable timer did not expire\n");
    }

    if (Thread)
    {
        WaitForSingleObject(Thread, 5000);
        CloseHandle(Thread);
    }
    CancelWaitableTimer(AbsoluteTimer);
    CancelWaitableTimer(RelativeTimer);
    CloseHandle(AbsoluteTimer);
    CloseHandle(RelativeTimer);
    CloseHandle(Wait.Event);
}
