/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 address space and loader conventions of the ARM HAL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define HAL_ARM64_SYSTEM_RANGE_BASE 0xFFFF800000000000ULL
#define HAL_ARM64_PHYS_MAP_BASE 0xFFFFFC0000000000ULL
#define HAL_ARM64_PHYS_ADDR_MASK 0x0000FFFFFFFFFFFFULL

#define PSCI_FN_CPU_ON_64 0xC4000003ULL

FORCEINLINE
PVOID
HalpPhysicalAlias(
    _In_ ULONGLONG PhysicalAddress)
{
    return (PVOID)(ULONG_PTR)(HAL_ARM64_PHYS_MAP_BASE | (PhysicalAddress & HAL_ARM64_PHYS_ADDR_MASK));
}

FORCEINLINE
BOOLEAN
HalpLoaderDeviceTree(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Outptr_result_maybenull_ const VOID **Blob,
    _Out_ PSIZE_T Size)
{
    *Blob = (const VOID *)(ULONG_PTR)LoaderBlock->u.Arm64.DeviceTree;
    *Size = (SIZE_T)LoaderBlock->u.Arm64.DeviceTreeSize;
    return *Blob != NULL;
}
