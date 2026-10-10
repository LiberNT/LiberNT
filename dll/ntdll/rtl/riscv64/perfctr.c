/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V user-mode read of the performance counter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

#define RTLP_RISCV_QPC_BYPASS_ENABLED 0x0001

BOOLEAN
RtlpArchQueryPerformanceCounter(
    _Out_ PLARGE_INTEGER Counter)
{
    ULONGLONG Value;

    if (*(volatile USHORT *)&SharedUserData->QpcData != RTLP_RISCV_QPC_BYPASS_ENABLED)
        return FALSE;

    __asm__ __volatile__("rdtime %0" : "=r"(Value));
    Counter->QuadPart = (LONGLONG)(Value + SharedUserData->QpcBias);
    return TRUE;
}

BOOLEAN
RtlpArchQueryPerformanceFrequency(
    _Out_ PLARGE_INTEGER Frequency)
{
    if (*(volatile USHORT *)&SharedUserData->QpcData != RTLP_RISCV_QPC_BYPASS_ENABLED)
        return FALSE;

    Frequency->QuadPart = (LONGLONG)SharedUserData->QpcFrequency;
    return Frequency->QuadPart != 0;
}
