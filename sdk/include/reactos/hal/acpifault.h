/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Firmware table faults the debugger reads back
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 *
 * NOTE:        LiberNT-specific. Windows has no such interface.
 */

#pragma once

#ifdef HALP_ACPI_HOST
/* The host test supplies the base types. */
#else
#include <ntdef.h>
#endif

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

C_ASSERT(sizeof(HALP_ACPI_FAULT) == 6 * sizeof(ULONG));

/*
 * Copies the retained faults, oldest first, and returns the number recorded
 * since boot. Callable at any IRQL without allocation, locks or paging.
 */
ULONG
NTAPI
HalpAcpiQueryFaults(
    _Out_writes_bytes_opt_(Size) PHALP_ACPI_FAULT Buffer,
    _In_ ULONG Size,
    _Out_opt_ PULONG Copied);
