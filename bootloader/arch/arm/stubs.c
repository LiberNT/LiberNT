/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM runtime entries required by the boot applications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>

#ifdef KeGetCurrentIrql
#undef KeGetCurrentIrql
#endif

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return PASSIVE_LEVEL;
}

VOID
NTAPI
RtlUnwind(
    _In_opt_ PVOID TargetFrame,
    _In_opt_ PVOID TargetIp,
    _In_opt_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID ReturnValue)
{
    UNREFERENCED_PARAMETER(TargetFrame);
    UNREFERENCED_PARAMETER(TargetIp);
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ReturnValue);
}

BOOLEAN
FwArchDebugPutChar(
    _In_ UCHAR Character)
{
    UNREFERENCED_PARAMETER(Character);
    return FALSE;
}

static
BOOLEAN
FwArmWritable(
    _In_ ULONG_PTR Address)
{
    ULONG Low, High;

    _MoveToCoprocessor((unsigned int)Address, 15, 0, 7, 8, 1);
    __isb(_ARM_BARRIER_SY);
    __asm__ __volatile__("mrrc p15, 0, %0, %1, c7" : "=r"(Low), "=r"(High));
    UNREFERENCED_PARAMETER(High);
    return (Low & 1) == 0;
}

BOOLEAN
FwArchMemoryUsable(
    _In_ ULONGLONG Address,
    _In_ SIZE_T Size)
{
    ASSERT(Size != 0);
    if (Address + Size - 1 > MAXULONG)
        return FALSE;
    return FwArmWritable((ULONG_PTR)Address) && FwArmWritable((ULONG_PTR)(Address + Size - 1));
}
