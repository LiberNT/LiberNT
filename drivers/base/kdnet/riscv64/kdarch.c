/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Architecture hooks, RISC-V64: the device tree handed over by the boot loader
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../kdnet.h"

BOOLEAN
KdNetArchRandom(
    _Out_ PULONG Value)
{
    *Value = 0;
    return FALSE;
}

BOOLEAN
KdNetArchGetDeviceTree(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_ const VOID **Blob,
    _Out_ PULONG Size)
{
    *Blob = (const VOID *)(ULONG_PTR)LoaderBlock->u.Riscv64.DeviceTree;
    *Size = (ULONG)LoaderBlock->u.Riscv64.DeviceTreeSize;
    return *Blob != NULL && *Size != 0;
}
