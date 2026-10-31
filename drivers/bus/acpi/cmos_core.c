/*
 * PROJECT:     LiberNT ACPI Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SystemCMOS operation region access window
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdef.h>
#include <string.h>
#include "cmos_core.h"

ULONG
AcpiCmosCoreTypeFromHardwareId(
    _In_z_ PCSTR HardwareId)
{
    if (strcmp(HardwareId, "PNP0B01") == 0)
        return ACPI_CMOS_TYPE_INTEL_PIIX4;
    if (strcmp(HardwareId, "PNP0B02") == 0)
        return ACPI_CMOS_TYPE_DALLAS_1501;
    return ACPI_CMOS_TYPE_STD_PCAT;
}

ULONG
AcpiCmosCoreWindow(
    _In_ ULONG CmosType,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    /* TODO: PIIX4 and Dallas 1501 CMOS banks; they are not served, as on Windows 11 */
    if (CmosType != ACPI_CMOS_TYPE_STD_PCAT || Offset >= ACPI_CMOS_STD_PCAT_LIMIT)
        return 0;
    return min(Length, ACPI_CMOS_STD_PCAT_LIMIT - Offset);
}
