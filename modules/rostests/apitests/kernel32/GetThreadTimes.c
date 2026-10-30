/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that GetThreadTimes charges each processor no more than the elapsed time
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define SPIN_MS 200
#define TICK_SLACK_MS 32

typedef NTSTATUS (NTAPI *PNT_SET_TIMER_RESOLUTION)(ULONG, BOOLEAN, PULONG);

static ULONG64
FileTimeToUlong64(_In_ const FILETIME *Time)
{
    return ((ULONG64)Time->dwHighDateTime << 32) | Time->dwLowDateTime;
}

static BOOL
GetChargedTime(_In_ HANDLE Thread, _Out_ PULONG64 Charged)
{
    FILETIME Creation, Exit, Kernel, User;

    if (!GetThreadTimes(Thread, &Creation, &Exit, &Kernel, &User))
        return FALSE;
    *Charged = FileTimeToUlong64(&Kernel) + FileTimeToUlong64(&User);
    return TRUE;
}

START_TEST(GetThreadTimes)
{
    PNT_SET_TIMER_RESOLUTION pNtSetTimerResolution =
        (PNT_SET_TIMER_RESOLUTION)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetTimerResolution");
    HANDLE Thread = GetCurrentThread();
    DWORD_PTR ProcessMask, SystemMask;
    LARGE_INTEGER Frequency, Start, Now;
    ULONG64 Before, After, Charged, Wall;
    ULONG Processor, Actual;
    BOOLEAN Raised = FALSE;

    if (!QueryPerformanceFrequency(&Frequency) ||
        !GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask))
    {
        skip("No performance counter or affinity mask\n");
        return;
    }

    if (pNtSetTimerResolution)
        Raised = NT_SUCCESS(pNtSetTimerResolution(10000, TRUE, &Actual));

    for (Processor = 0; Processor < sizeof(DWORD_PTR) * 8; Processor++)
    {
        if (!(ProcessMask & ((DWORD_PTR)1 << Processor)))
            continue;
        if (!SetThreadAffinityMask(Thread, (DWORD_PTR)1 << Processor))
        {
            ok(FALSE, "SetThreadAffinityMask(%lu) failed: %lu\n", Processor, GetLastError());
            continue;
        }
        Sleep(0);

        ok(GetChargedTime(Thread, &Before), "GetThreadTimes failed: %lu\n", GetLastError());
        QueryPerformanceCounter(&Start);
        do
        {
            QueryPerformanceCounter(&Now);
        } while ((ULONG64)(Now.QuadPart - Start.QuadPart) * 1000 < (ULONG64)Frequency.QuadPart * SPIN_MS);
        ok(GetChargedTime(Thread, &After), "GetThreadTimes failed: %lu\n", GetLastError());

        Wall = (ULONG64)(Now.QuadPart - Start.QuadPart) * 10000000 / (ULONG64)Frequency.QuadPart;
        Charged = After - Before;
        ok(Charged <= Wall + TICK_SLACK_MS * 10000ULL,
           "Processor %lu: charged %lu ms for %lu ms of wall time\n",
           Processor, (ULONG)(Charged / 10000), (ULONG)(Wall / 10000));
        ok(Charged * 4 >= Wall,
           "Processor %lu: charged %lu ms for %lu ms of busy wall time\n",
           Processor, (ULONG)(Charged / 10000), (ULONG)(Wall / 10000));
    }

    SetThreadAffinityMask(Thread, ProcessMask);
    if (Raised)
        pNtSetTimerResolution(10000, FALSE, &Actual);
}
