/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 address space and loader conventions of the ARM HAL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define HAL_ARM64_SYSTEM_RANGE_BASE ((ULONG_PTR)ARM_LOADER_KSEG0_BASE)

#define PSCI_FN_CPU_ON_32 0x84000003UL

FORCEINLINE
PVOID
HalpPhysicalAlias(
    _In_ ULONGLONG PhysicalAddress)
{
    PHYSICAL_ADDRESS Physical;

    Physical.QuadPart = (LONGLONG)PhysicalAddress;
    return MmGetVirtualForPhysical(Physical);
}

FORCEINLINE
BOOLEAN
HalpLoaderDeviceTree(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Outptr_result_maybenull_ const VOID **Blob,
    _Out_ PSIZE_T Size)
{
    *Blob = (const VOID *)(ULONG_PTR)LoaderBlock->u.Arm.DeviceTree;
    *Size = (SIZE_T)LoaderBlock->u.Arm.DeviceTreeSize;
    return *Blob != NULL;
}
