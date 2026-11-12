/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Validation of firmware tables before their contents are consumed
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifdef HALP_ACPI_HOST
/* The host test supplies the base types. */
#else
#include <hal.h>
#endif
#include <acpival.h>

static
ULONG
HalpAcpiRead32(
    _In_reads_bytes_(4) const UCHAR *Bytes)
{
    return (ULONG)Bytes[0] |
           ((ULONG)Bytes[1] << 8) |
           ((ULONG)Bytes[2] << 16) |
           ((ULONG)Bytes[3] << 24);
}

/* ACPI specification 6.5, 20.2.2: NameChar and LeadNameChar */
static
BOOLEAN
HalpAcpiNameCharacter(
    _In_ UCHAR Character,
    _In_ ULONG Position)
{
    /* The Alert Standard Format table is named "ASF!" */
    if ((Character == '!') && (Position == 3))
        return TRUE;

    return ((Character >= 'A' && Character <= 'Z') ||
            (Character >= '0' && Character <= '9') ||
            (Character == '_'));
}

HALP_ACPI_VERDICT
HalpAcpiCheckTable(
    _In_reads_bytes_(Available) const UCHAR *Table,
    _In_ ULONG Available,
    _Out_ PHALP_ACPI_CHECK Check)
{
    ULONG Index, Length, Sum = 0;

    Check->Signature = 0;
    Check->Length = 0;
    Check->Revision = 0;
    Check->Sum = 0;
    Check->FaultOffset = 0;

    if (Available < HALP_ACPI_HEADER_SIZE)
        return HalpAcpiBadLength;

    Check->Signature = HalpAcpiRead32(Table);
    for (Index = 0; Index < 4; Index++)
    {
        if (!HalpAcpiNameCharacter(Table[Index], Index))
            return HalpAcpiBadSignature;
    }

    Length = HalpAcpiRead32(Table + 4);
    Check->Length = Length;
    Check->Revision = Table[8];
    if ((Length < HALP_ACPI_HEADER_SIZE) || (Length > Available))
        return HalpAcpiBadLength;

    for (Index = 0; Index < Length; Index++)
        Sum += Table[Index];

    Check->Sum = (UCHAR)Sum;
    if (Check->Sum != 0)
        return HalpAcpiBadChecksum;

    return HalpAcpiAccepted;
}

HALP_ACPI_VERDICT
HalpAcpiCheckSubtables(
    _In_reads_bytes_(Length) const UCHAR *Table,
    _In_ ULONG First,
    _In_ ULONG Length,
    _Out_opt_ PULONG FaultOffset,
    _Out_opt_ PUCHAR FaultType)
{
    ULONG Position = First, EntryLength;

    if (FaultOffset)
        *FaultOffset = 0;
    if (FaultType)
        *FaultType = 0;

    if (First > Length)
    {
        if (FaultOffset)
            *FaultOffset = First;
        return HalpAcpiBadSubtable;
    }

    while (Position < Length)
    {
        /* An entry cannot be shorter than its own type and length bytes */
        if ((Length - Position) < 2)
        {
            if (FaultType)
                *FaultType = 0;
            if (FaultOffset)
                *FaultOffset = Position;
            return HalpAcpiBadSubtable;
        }

        if (FaultType)
            *FaultType = Table[Position];

        EntryLength = Table[Position + 1];
        if ((EntryLength < 2) || (EntryLength > Length - Position))
        {
            if (FaultOffset)
                *FaultOffset = Position;
            return HalpAcpiBadSubtable;
        }

        Position += EntryLength;
    }

    return HalpAcpiAccepted;
}
