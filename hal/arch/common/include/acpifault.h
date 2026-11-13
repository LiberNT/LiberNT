/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     What the firmware table parsers rejected, kept for the boot report
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

#include <reactos/hal/acpifault.h>

VOID
HalpAcpiRecordFault(
    _In_ ULONG Signature,
    _In_ ULONG Reason,
    _In_ ULONG Type,
    _In_ ULONG Offset,
    _In_ ULONG Value1,
    _In_ ULONG Value2);

ULONG
HalpAcpiFaultCount(VOID);

BOOLEAN
HalpAcpiGetFault(
    _In_ ULONG Index,
    _Out_ PHALP_ACPI_FAULT Fault);

/* Writes every retained fault to the debug log; silent when there is none */
VOID
HalpAcpiReportFaults(VOID);
