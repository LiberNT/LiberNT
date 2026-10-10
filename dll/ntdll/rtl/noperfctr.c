/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Performance counter hooks for architectures without a user-mode read
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

BOOLEAN
RtlpArchQueryPerformanceCounter(
    _Out_ PLARGE_INTEGER Counter)
{
    UNREFERENCED_PARAMETER(Counter);
    return FALSE;
}

BOOLEAN
RtlpArchQueryPerformanceFrequency(
    _Out_ PLARGE_INTEGER Frequency)
{
    UNREFERENCED_PARAMETER(Frequency);
    return FALSE;
}
