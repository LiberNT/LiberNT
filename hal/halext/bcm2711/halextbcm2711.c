/*
 * PROJECT:     LiberNT HAL extensions
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Raspberry Pi 4 extension: BCM2711 PCIe and VideoCore
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "../lib/halextlib.h"
#include "bcm2711_pci.h"
#include "bcm2711_vc.h"

NTSTATUS
NTAPI
HalExtensionInitialize(
    _In_ const HAL_EXTENSION_SERVICES *Services,
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status = HalExtBindServices(Services);

    if (!NT_SUCCESS(Status))
        return Status;

    return Bcm2711PciProbe(LoaderBlock) ? STATUS_SUCCESS : STATUS_NO_SUCH_DEVICE;
}

VOID
NTAPI
HalExtensionPhase1Initialize(VOID)
{
    HalpBcm2711VcInit();
}
