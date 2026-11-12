/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a one-millisecond timer resolution request makes the clock and a short sleep that fine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/kefuncs.h>

#define SLEEP_SAMPLES 10

typedef UINT (WINAPI *PFN_TIME_PERIOD)(UINT);

START_TEST(TimerResolutionSleep)
{
    PFN_TIME_PERIOD pTimeBeginPeriod, pTimeEndPeriod;
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    ULONG Coarsest, Finest, Before, During, After;
    LARGE_INTEGER Frequency, Start, End;
    double ElapsedMs;
    NTSTATUS Status;
    ULONG Sample;

    pTimeBeginPeriod = (PFN_TIME_PERIOD)GetProcAddress(Kernel32, "timeBeginPeriod");
    pTimeEndPeriod = (PFN_TIME_PERIOD)GetProcAddress(Kernel32, "timeEndPeriod");
    if (!pTimeBeginPeriod || !pTimeEndPeriod)
    {
        skip("timeBeginPeriod is not exported by kernel32\n");
        return;
    }

    Status = NtQueryTimerResolution(&Coarsest, &Finest, &Before);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (Finest > 10000)
    {
        skip("The clock cannot run at a millisecond (finest %lu)\n", Finest);
        return;
    }

    ok_eq_uint(pTimeBeginPeriod(1), 0);

    /* The granted period is reported, and a millisecond request gets one, or the finest the clock has */
    Status = NtQueryTimerResolution(&Coarsest, &Finest, &During);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(During <= 10000 && During >= Finest, "current resolution %lu after timeBeginPeriod(1), finest %lu\n", During, Finest);

    QueryPerformanceFrequency(&Frequency);
    Sleep(1);
    QueryPerformanceCounter(&Start);
    for (Sample = 0; Sample < SLEEP_SAMPLES; Sample++)
        Sleep(1);
    QueryPerformanceCounter(&End);

    ElapsedMs = (End.QuadPart - Start.QuadPart) * 1000.0 / Frequency.QuadPart / SLEEP_SAMPLES;
    ok(ElapsedMs < 3.0, "Sleep(1) took %.2f ms on average with a %lu resolution\n", ElapsedMs, During);

    ok_eq_uint(pTimeEndPeriod(1), 0);

    /* Letting go of the request gives back the resolution there was before it */
    Status = NtQueryTimerResolution(&Coarsest, &Finest, &After);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_eq_ulong(After, Before);
}
