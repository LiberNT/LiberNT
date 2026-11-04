/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the timer of a registered wait
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _PERIODIC_WAIT_CONTEXT
{
    volatile LONG Timeouts;
    volatile LONG Signals;
    HANDLE TimeoutSemaphore;
    HANDLE SignalSemaphore;
} PERIODIC_WAIT_CONTEXT, *PPERIODIC_WAIT_CONTEXT;

static VOID CALLBACK
PeriodicWaitCallback(
    _In_ PVOID Parameter,
    _In_ BOOLEAN TimedOut)
{
    PPERIODIC_WAIT_CONTEXT Context = Parameter;

    if (TimedOut)
    {
        InterlockedIncrement(&Context->Timeouts);
        ReleaseSemaphore(Context->TimeoutSemaphore, 1, NULL);
    }
    else
    {
        InterlockedIncrement(&Context->Signals);
        ReleaseSemaphore(Context->SignalSemaphore, 1, NULL);
    }
}

static BOOL
StartPeriodicWait(
    _Out_ PHANDLE Wait,
    _Out_ PHANDLE Event,
    _Out_ PPERIODIC_WAIT_CONTEXT Context,
    _In_ ULONG Milliseconds,
    _In_ ULONG Flags)
{
    ZeroMemory(Context, sizeof(*Context));
    *Wait = NULL;
    *Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    Context->TimeoutSemaphore = CreateSemaphoreW(NULL, 0, 0x10000, NULL);
    Context->SignalSemaphore = CreateSemaphoreW(NULL, 0, 0x10000, NULL);
    ok(*Event != NULL && Context->TimeoutSemaphore != NULL && Context->SignalSemaphore != NULL,
       "Failed to create the synchronization objects, error %lu\n", GetLastError());
    if (!*Event || !Context->TimeoutSemaphore || !Context->SignalSemaphore)
        return FALSE;

    ok(RegisterWaitForSingleObject(Wait, *Event, PeriodicWaitCallback, Context, Milliseconds, Flags),
       "RegisterWaitForSingleObject failed, error %lu\n", GetLastError());
    return *Wait != NULL;
}

static VOID
StopPeriodicWait(
    _In_ HANDLE Wait,
    _In_ HANDLE Event,
    _In_ PPERIODIC_WAIT_CONTEXT Context)
{
    if (Wait)
        ok(UnregisterWaitEx(Wait, INVALID_HANDLE_VALUE), "UnregisterWaitEx failed, error %lu\n", GetLastError());
    if (Event)
        CloseHandle(Event);
    if (Context->TimeoutSemaphore)
        CloseHandle(Context->TimeoutSemaphore);
    if (Context->SignalSemaphore)
        CloseHandle(Context->SignalSemaphore);
}

static VOID
TestTimeoutRestartsTimer(
    _In_ ULONG Flags)
{
    PERIODIC_WAIT_CONTEXT Context;
    HANDLE Wait, Event;

    if (StartPeriodicWait(&Wait, &Event, &Context, 100, Flags))
    {
        ok(WaitForSingleObject(Context.TimeoutSemaphore, 5000) == WAIT_OBJECT_0,
           "Flags 0x%lx: the first timeout callback did not run\n", Flags);
        ok(WaitForSingleObject(Context.TimeoutSemaphore, 5000) == WAIT_OBJECT_0,
           "Flags 0x%lx: the timer did not restart after the first timeout\n", Flags);
        SetEvent(Event);
        ok(WaitForSingleObject(Context.SignalSemaphore, 5000) == WAIT_OBJECT_0,
           "Flags 0x%lx: the object was no longer waited on after a timeout\n", Flags);
    }
    StopPeriodicWait(Wait, Event, &Context);
}

static VOID
TestSignalRestartsTimer(
    _In_ ULONG Flags)
{
    PERIODIC_WAIT_CONTEXT Context;
    HANDLE Wait, Event;

    if (StartPeriodicWait(&Wait, &Event, &Context, 1500, Flags))
    {
        Sleep(600);
        SetEvent(Event);
        ok(WaitForSingleObject(Context.SignalSemaphore, 5000) == WAIT_OBJECT_0,
           "Flags 0x%lx: the signal callback did not run\n", Flags);
        Sleep(1100);
        ok(Context.Timeouts == 0,
           "Flags 0x%lx: the timeout ran %ld times after the signal restarted the timer\n", Flags, Context.Timeouts);
        ok(Context.Signals == 1, "Flags 0x%lx: expected 1 signal callback, got %ld\n", Flags, Context.Signals);
    }
    StopPeriodicWait(Wait, Event, &Context);
}

static VOID
TestZeroTimeout(
    _In_ ULONG Flags)
{
    PERIODIC_WAIT_CONTEXT Context;
    HANDLE Wait, Event;
    LONG Timeouts;

    if (StartPeriodicWait(&Wait, &Event, &Context, 0, Flags))
    {
        Sleep(200);
        ok(UnregisterWaitEx(Wait, INVALID_HANDLE_VALUE), "UnregisterWaitEx failed, error %lu\n", GetLastError());
        Wait = NULL;
        Timeouts = Context.Timeouts;
        ok(Timeouts >= 2 && Timeouts <= 400,
           "Flags 0x%lx: expected one zero timeout callback per clock tick, got %ld in 200 ms\n", Flags, Timeouts);
    }
    StopPeriodicWait(Wait, Event, &Context);
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

static BOOL
ShiftSystemTime(
    _In_ LONGLONG Delta)
{
    FILETIME FileTime;
    SYSTEMTIME SystemTime;
    ULARGE_INTEGER Time;

    GetSystemTimeAsFileTime(&FileTime);
    Time.LowPart = FileTime.dwLowDateTime;
    Time.HighPart = FileTime.dwHighDateTime;
    Time.QuadPart += Delta;
    FileTime.dwLowDateTime = Time.LowPart;
    FileTime.dwHighDateTime = Time.HighPart;
    return FileTimeToSystemTime(&FileTime, &SystemTime) && SetSystemTime(&SystemTime);
}

static VOID
TestSystemTimeChange(
    _In_ ULONG Flags)
{
    static const LONGLONG Hour = 36000000000LL;
    PERIODIC_WAIT_CONTEXT Context, OtherContext;
    HANDLE Wait, Event, OtherWait, OtherEvent;

    if (!EnableSystemTimePrivilege())
    {
        skip("SeSystemtimePrivilege is not available\n");
        return;
    }

    if (StartPeriodicWait(&Wait, &Event, &Context, 1500, Flags))
    {
        Sleep(100);
        if (ShiftSystemTime(Hour))
        {
            Sleep(200);
            if (StartPeriodicWait(&OtherWait, &OtherEvent, &OtherContext, INFINITE, WT_EXECUTEDEFAULT))
                Sleep(200);
            StopPeriodicWait(OtherWait, OtherEvent, &OtherContext);
            ok(Context.Timeouts == 0,
               "Flags 0x%lx: the timeout ran %ld times after the system time moved forward\n", Flags, Context.Timeouts);
            ok(ShiftSystemTime(-Hour), "Restoring the system time failed, error %lu\n", GetLastError());
            ok(WaitForSingleObject(Context.TimeoutSemaphore, 5000) == WAIT_OBJECT_0,
               "Flags 0x%lx: the timeout did not run\n", Flags);
        }
        else
        {
            skip("SetSystemTime failed, error %lu\n", GetLastError());
        }
    }
    StopPeriodicWait(Wait, Event, &Context);
}

START_TEST(RegisterWaitPeriodic)
{
    TestTimeoutRestartsTimer(WT_EXECUTEDEFAULT);
    TestTimeoutRestartsTimer(WT_EXECUTEINWAITTHREAD);
    TestSignalRestartsTimer(WT_EXECUTEDEFAULT);
    TestZeroTimeout(WT_EXECUTEINWAITTHREAD);
    TestSystemTimeChange(WT_EXECUTEDEFAULT);
    TestSystemTimeChange(WT_EXECUTEONLYONCE | WT_EXECUTEINWAITTHREAD);
}
