/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device tree lookups for platforms booted without ACPI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <libernt/hal/halext.h>

ULONG
HalpArm64FindDeviceTreePhandle(
    _In_ PFDT Fdt,
    _In_ ULONG Phandle);

VOID
HalpArm64CaptureDeviceTree(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);
