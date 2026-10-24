/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Architecture hooks, ARM64: ARMv8.5 RNDR and no device tree
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../kdnet.h"
#include <reactos/arm64/sysreg.h>

#if defined(_MSC_VER) && !defined(__clang__)
BOOLEAN
KdNetArchReadRndr(
    _Out_ PULONG64 Number);
#endif

BOOLEAN
KdNetArchRandom(
    _Out_ PULONG Value)
{
    ULONG64 Features, Number, Valid;

    Features = ARM64_READ_SYSREG(ID_AA64ISAR0_EL1);
    if (!((Features >> 60) & 0xF))
        return FALSE;

#if defined(_MSC_VER) && !defined(__clang__)
    Valid = KdNetArchReadRndr(&Number);
#else
    __asm__ __volatile__("mrs %0, s3_3_c2_c4_0; cset %1, ne" : "=r"(Number), "=r"(Valid) : : "cc");
#endif
    *Value = (ULONG)Number;
    return Valid != 0;
}

BOOLEAN
KdNetArchGetDeviceTree(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_ const VOID **Blob,
    _Out_ PULONG Size)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    *Blob = NULL;
    *Size = 0;
    return FALSE;
}
