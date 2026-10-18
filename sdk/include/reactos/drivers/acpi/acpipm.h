/*
 * PROJECT:     LiberNT ACPI and HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PM_DISPATCH_TABLE contents exchanged through HalInitPowerManagement
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define ACPI_PM_DISPATCH_SIGNATURE  0x49504341
#define ACPI_PM_DISPATCH_VERSION    1

typedef enum _ACPI_PM_DISPATCH_FUNCTION
{
    AcpiPmGetSleepType,
    AcpiPmMaximumFunction
} ACPI_PM_DISPATCH_FUNCTION;

typedef
NTSTATUS
(NTAPI *PACPI_PM_GET_SLEEP_TYPE)(
    _In_ SYSTEM_POWER_STATE SystemState,
    _Out_ PUCHAR SleepTypeA,
    _Out_ PUCHAR SleepTypeB);
