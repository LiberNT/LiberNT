/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     UEFI runtime services shared by every HAL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

VOID
NTAPI
HalpInitializeEfiRuntime(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

BOOLEAN
NTAPI
HalpEfiHasFirmwareResources(VOID);

NTSTATUS
NTAPI
HalpEfiQueryTime(
    _Out_ PTIME_FIELDS TimeFields);

NTSTATUS
NTAPI
HalpEfiSetTime(
    _In_ PTIME_FIELDS TimeFields);
