/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device tree lookups for platforms booted without ACPI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <libernt/fdtlib.h>

BOOLEAN
HalpArm64OpenDeviceTree(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_ PFDT Fdt);

BOOLEAN
HalpArm64FindDeviceTreeNode(
    _In_ PFDT Fdt,
    _In_reads_(Count) const PCSTR *Compatibles,
    _In_ ULONG Count,
    _Out_ PULONG Node,
    _Out_ PULONG Parent,
    _Out_ PULONG Match);

ULONG
HalpArm64FindDeviceTreePhandle(
    _In_ PFDT Fdt,
    _In_ ULONG Phandle);

VOID
HalpArm64CaptureDeviceTree(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

BOOLEAN
HalpArm64GetDeviceTree(
    _Outptr_result_maybenull_ const VOID **Blob,
    _Out_ PULONG Size);

ULONG
HalpAppleAicIrqsPerDie(VOID);
