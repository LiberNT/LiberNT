/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 user-mode read of the performance counter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>
#include <reactos/arm64/sysreg.h>

#define RTL_ARM64_QPC_BYPASS_CONFIGURATION 0x0001

BOOLEAN
RtlpArchQueryPerformanceCounter(
    _Out_ PLARGE_INTEGER Counter)
{
    ULONGLONG Bias, Value;
    USHORT Configuration;

    Configuration = *(volatile USHORT *)&SharedUserData->QpcData;
    if (Configuration != RTL_ARM64_QPC_BYPASS_CONFIGURATION)
        return FALSE;

    ARM64_DMB(ISHLD);
    Bias = SharedUserData->QpcBias;
    ARM64_ISB();
    Value = ARM64_READ_SYSREG(CNTVCT_EL0);
    ARM64_DMB(ISHLD);

    if (Configuration != *(volatile USHORT *)&SharedUserData->QpcData)
        return FALSE;

    Counter->QuadPart = (LONGLONG)(Value + Bias);
    return TRUE;
}

BOOLEAN
RtlpArchQueryPerformanceFrequency(
    _Out_ PLARGE_INTEGER Frequency)
{
    ULONGLONG Value;
    USHORT Configuration;

    Configuration = *(volatile USHORT *)&SharedUserData->QpcData;
    if (Configuration != RTL_ARM64_QPC_BYPASS_CONFIGURATION)
        return FALSE;

    ARM64_DMB(ISHLD);
    Value = *(volatile ULONGLONG *)&SharedUserData->QpcFrequency;
    ARM64_DMB(ISHLD);
    if ((Configuration != *(volatile USHORT *)&SharedUserData->QpcData) || !Value)
        return FALSE;

    Frequency->QuadPart = (LONGLONG)Value;
    return TRUE;
}
