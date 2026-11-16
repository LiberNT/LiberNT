/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interrupt targeting for the HALs that deliver an interrupt to one processor of the set
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ndk/halfuncs.h>

NTSTATUS
NTAPI
HalGetInterruptTargetInformation(
    _Inout_ PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation)
{
    KAFFINITY Mask;
    ULONG ProcessorNumber;

    if (TargetInformation == NULL ||
        TargetInformation->Version != HAL_INTERRUPT_TARGET_INFORMATION_VERSION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Mask = TargetInformation->TargetProcessors;
    if (Mask == 0)
        Mask = KeQueryActiveProcessors();

    ProcessorNumber = 0;
    while ((Mask & 1) == 0)
    {
        Mask >>= 1;
        ProcessorNumber++;
    }

    TargetInformation->ProcessorNumber = ProcessorNumber;
    TargetInformation->TargetProcessors = ((KAFFINITY)1 << ProcessorNumber);
    TargetInformation->DestinationId = ProcessorNumber;
    return STATUS_SUCCESS;
}

/* EOF */
