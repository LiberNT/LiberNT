/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a high-resolution waitable timer leaves no finer clock behind once it is done
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/kefuncs.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static ULONG
CurrentResolution(VOID)
{
    ULONG Coarsest, Finest, Current;

    ok_ntstatus(NtQueryTimerResolution(&Coarsest, &Finest, &Current), STATUS_SUCCESS);
    return Current;
}

START_TEST(HighResolutionTimerClock)
{
    ULONG Coarsest, Finest, Before;
    LARGE_INTEGER Due;
    HANDLE Timer;

    ok_ntstatus(NtQueryTimerResolution(&Coarsest, &Finest, &Before), STATUS_SUCCESS);
    if (Before != Coarsest)
    {
        skip("Another process holds a finer resolution (%lu)\n", Before);
        return;
    }

    Timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    ok(Timer != NULL, "CreateWaitableTimerExW failed: %lu\n", GetLastError());
    if (!Timer)
        return;

    ok_eq_ulong(CurrentResolution(), Before);

    Due.QuadPart = -20000;
    ok(SetWaitableTimer(Timer, &Due, 0, NULL, NULL, FALSE), "SetWaitableTimer failed: %lu\n", GetLastError());

    /* A one-shot timer that went off gives the clock back */
    ok_eq_ulong(WaitForSingleObject(Timer, 1000), WAIT_OBJECT_0);
    Sleep(20);
    ok_eq_ulong(CurrentResolution(), Before);

    /* A periodic timer gives it back once it is cancelled */
    ok(SetWaitableTimer(Timer, &Due, 5, NULL, NULL, FALSE), "SetWaitableTimer failed: %lu\n", GetLastError());
    Sleep(20);
    ok(CancelWaitableTimer(Timer), "CancelWaitableTimer failed: %lu\n", GetLastError());
    ok_eq_ulong(CurrentResolution(), Before);

    /* Closing the last handle of a set timer gives it back too */
    ok(SetWaitableTimer(Timer, &Due, 5, NULL, NULL, FALSE), "SetWaitableTimer failed: %lu\n", GetLastError());
    CloseHandle(Timer);
    ok_eq_ulong(CurrentResolution(), Before);
}
