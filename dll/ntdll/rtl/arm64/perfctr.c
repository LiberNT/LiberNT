/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 user-mode read of the performance counter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

#define RTL_ARM64_QPC_BYPASS_CONFIGURATION 0x0001

BOOLEAN
RtlpArchQueryPerformanceCounter(
    _Out_ PLARGE_INTEGER Counter)
{
#if defined(__GNUC__) || defined(__clang__)
    ULONGLONG Bias, Value;
    USHORT Configuration;

    Configuration = *(volatile USHORT *)&SharedUserData->QpcData;
    if (Configuration != RTL_ARM64_QPC_BYPASS_CONFIGURATION)
        return FALSE;

    __asm__ __volatile__("dmb ishld" ::: "memory");
    Bias = SharedUserData->QpcBias;
    __asm__ __volatile__("isb\n\tmrs %0, cntpct_el0" : "=r"(Value) :: "memory");
    __asm__ __volatile__("dmb ishld" ::: "memory");

    if (Configuration != *(volatile USHORT *)&SharedUserData->QpcData)
        return FALSE;

    Counter->QuadPart = (LONGLONG)(Value + Bias);
    return TRUE;
#else
    UNREFERENCED_PARAMETER(Counter);
    return FALSE;
#endif
}

BOOLEAN
RtlpArchQueryPerformanceFrequency(
    _Out_ PLARGE_INTEGER Frequency)
{
#if defined(__GNUC__) || defined(__clang__)
    ULONGLONG Value;
    USHORT Configuration;

    Configuration = *(volatile USHORT *)&SharedUserData->QpcData;
    if (Configuration != RTL_ARM64_QPC_BYPASS_CONFIGURATION)
        return FALSE;

    __asm__ __volatile__("dmb ishld" ::: "memory");
    Value = *(volatile ULONGLONG *)&SharedUserData->QpcFrequency;
    __asm__ __volatile__("dmb ishld" ::: "memory");
    if ((Configuration != *(volatile USHORT *)&SharedUserData->QpcData) || !Value)
        return FALSE;

    Frequency->QuadPart = (LONGLONG)Value;
    return TRUE;
#else
    UNREFERENCED_PARAMETER(Frequency);
    return FALSE;
#endif
}
