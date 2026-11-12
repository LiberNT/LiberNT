/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     What the firmware table parsers rejected, kept for the boot report
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

/* Records kept; the total count survives an overflow of this window */
#define HALP_ACPI_FAULT_SLOTS 16

typedef enum _HALP_ACPI_FAULT_REASON
{
    HalpAcpiFaultTableMissing = 1,
    HalpAcpiFaultBadSignature,
    HalpAcpiFaultBadLength,
    HalpAcpiFaultBadChecksum,
    HalpAcpiFaultSubtableLength,
    HalpAcpiFaultSubtableOverrun,
    HalpAcpiFaultEntryLength,
    HalpAcpiFaultDuplicateId,
    HalpAcpiFaultUnsupportedEntry,
    HalpAcpiFaultProcessorIgnored,
    HalpAcpiFaultProcessorOverflow,
    HalpAcpiFaultUnusableEntry
} HALP_ACPI_FAULT_REASON;

typedef struct _HALP_ACPI_FAULT
{
    ULONG Signature;
    ULONG Reason;
    ULONG Type;      /* Subtable type, 0 when the fault is the table itself */
    ULONG Offset;    /* Byte offset inside the table, 0 when not applicable */
    ULONG Value1;
    ULONG Value2;
} HALP_ACPI_FAULT, *PHALP_ACPI_FAULT;

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
