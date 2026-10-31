/*
 * PROJECT:     LiberNT ACPI Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SystemCMOS operation region access window
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define ACPI_CMOS_TYPE_STD_PCAT     0
#define ACPI_CMOS_TYPE_INTEL_PIIX4  1
#define ACPI_CMOS_TYPE_DALLAS_1501  2
#define ACPI_CMOS_STD_PCAT_LIMIT    0x40

ULONG
AcpiCmosCoreTypeFromHardwareId(
    _In_z_ PCSTR HardwareId);

ULONG
AcpiCmosCoreWindow(
    _In_ ULONG CmosType,
    _In_ ULONG Offset,
    _In_ ULONG Length);
