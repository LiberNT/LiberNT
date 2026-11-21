/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 PSCI conduits (SMC32 calling convention)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <halacpi_arm64.h>

LONG
HalpPsciCallSmc(
    _In_ ULONGLONG FunctionId,
    _In_ ULONGLONG Arg0,
    _In_ ULONGLONG Arg1,
    _In_ ULONGLONG Arg2)
{
    register ULONG R0 __asm__("r0") = (ULONG)FunctionId;
    register ULONG R1 __asm__("r1") = (ULONG)Arg0;
    register ULONG R2 __asm__("r2") = (ULONG)Arg1;
    register ULONG R3 __asm__("r3") = (ULONG)Arg2;

    __asm__ __volatile__(".arch_extension sec\n\tsmc #0" : "+r"(R0), "+r"(R1), "+r"(R2), "+r"(R3) :: "memory");
    return (LONG)R0;
}

LONG
HalpPsciCallHvc(
    _In_ ULONGLONG FunctionId,
    _In_ ULONGLONG Arg0,
    _In_ ULONGLONG Arg1,
    _In_ ULONGLONG Arg2)
{
    register ULONG R0 __asm__("r0") = (ULONG)FunctionId;
    register ULONG R1 __asm__("r1") = (ULONG)Arg0;
    register ULONG R2 __asm__("r2") = (ULONG)Arg1;
    register ULONG R3 __asm__("r3") = (ULONG)Arg2;

    __asm__ __volatile__(".arch_extension virt\n\thvc #0" : "+r"(R0), "+r"(R1), "+r"(R2), "+r"(R3) :: "memory");
    return (LONG)R0;
}
