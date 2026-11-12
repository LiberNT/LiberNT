/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Validation of firmware tables before their contents are consumed
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

/* ACPI specification 6.5, 21.2.1: description header */
#define HALP_ACPI_HEADER_SIZE 36

/* Why a table, or one of its subtables, was rejected */
typedef enum _HALP_ACPI_VERDICT
{
    HalpAcpiAccepted = 0,
    HalpAcpiBadSignature,
    HalpAcpiBadLength,
    HalpAcpiBadChecksum,
    HalpAcpiBadSubtable
} HALP_ACPI_VERDICT;

typedef struct _HALP_ACPI_CHECK
{
    ULONG Signature;
    ULONG Length;
    UCHAR Revision;
    UCHAR Sum;          /* Bytes summed; zero when the checksum is good */
    ULONG FaultOffset;  /* Offset of the first bad subtable, when there is one */
} HALP_ACPI_CHECK, *PHALP_ACPI_CHECK;

/*
 * Checks one table against the bytes that are actually mapped for it.
 * Available is the number of bytes the caller can read, which may be larger
 * than the length the table declares. The header fields are read from the
 * mapped bytes, never from a structure overlaid on them.
 */
HALP_ACPI_VERDICT
HalpAcpiCheckTable(
    _In_reads_bytes_(Available) const UCHAR *Table,
    _In_ ULONG Available,
    _Out_ PHALP_ACPI_CHECK Check);

/*
 * Walks the subtable chain that starts at First and ends at Length. Every
 * entry must be two bytes long at least and must fit before Length. Returns
 * the first offending offset and entry type through the optional outputs.
 */
HALP_ACPI_VERDICT
HalpAcpiCheckSubtables(
    _In_reads_bytes_(Length) const UCHAR *Table,
    _In_ ULONG First,
    _In_ ULONG Length,
    _Out_opt_ PULONG FaultOffset,
    _Out_opt_ PUCHAR FaultType);
