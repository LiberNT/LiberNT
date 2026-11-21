/*
 * PROJECT:     LiberNT HAL extensions
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     BCM2836 local interrupt controller extension
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "../lib/halextlib.h"
#include "bcm2837.h"

NTSTATUS
NTAPI
HalExtensionInitialize(
    _In_ const HAL_EXTENSION_SERVICES *Services,
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status = HalExtBindServices(Services);

    if (!NT_SUCCESS(Status))
        return Status;

    return Bcm2837InterruptProbe(LoaderBlock) ? STATUS_SUCCESS : STATUS_NO_SUCH_DEVICE;
}
