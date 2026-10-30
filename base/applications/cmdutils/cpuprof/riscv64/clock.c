/*
 * PROJECT:     LiberNT CPU profiler
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 time counter reads
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include "../clock.h"

ULONGLONG ReadClockTicks(void)
{
    ULONGLONG Value;

    __asm__ __volatile__("rdtime %0" : "=r"(Value) :: "memory");
    return Value;
}

ULONGLONG ReadClockCycles(void)
{
    /* TODO: user mode has no cycle counter access (scounteren grants TM only) */
    return 0;
}

ULONGLONG ReadClockFrequency(void)
{
    LARGE_INTEGER Frequency;

    return QueryPerformanceFrequency(&Frequency) ? (ULONGLONG)Frequency.QuadPart : 0;
}
