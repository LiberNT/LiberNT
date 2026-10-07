/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Architecture hooks, x86: RDRAND and no device tree
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../kdnet.h"

BOOLEAN
KdNetArchRandom(
    _Out_ PULONG Value)
{
    int Information[4];
    unsigned char Valid;
    ULONG Number;

    __cpuid(Information, 0);
    if (Information[0] < 1)
        return FALSE;

    __cpuid(Information, 1);
    if (!(Information[2] & (1 << 30)))
        return FALSE;

    __asm__ __volatile__("rdrand %0; setc %1" : "=r"(Number), "=qm"(Valid) : : "cc");
    *Value = Number;
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
