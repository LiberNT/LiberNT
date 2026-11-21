/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Raspberry Pi 5 (BCM2712) PCI configuration-space backend
 */

#pragma once

#define BCM2712_ACPI_OEM_ID         "RPIFDN"
#define BCM2712_ACPI_OEM_TABLE_ID   "RPI5"

BOOLEAN
Bcm2712PciProbe(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);
