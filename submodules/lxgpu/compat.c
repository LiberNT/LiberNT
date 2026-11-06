/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     POSIX clock for virglrenderer on the ReactOS CRT
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include "compat.h"

int
clock_gettime(int Clock, struct timespec *Time)
{
    LARGE_INTEGER Counter, Frequency;

    if ((Clock != CLOCK_MONOTONIC) || (Time == NULL))
        return -1;
    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Counter);
    Time->tv_sec = (time_t)(Counter.QuadPart / Frequency.QuadPart);
    Time->tv_nsec = (long)((Counter.QuadPart % Frequency.QuadPart) * 1000000000LL / Frequency.QuadPart);
    return 0;
}
