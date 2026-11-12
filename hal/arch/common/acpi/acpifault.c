/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     What the firmware table parsers rejected, kept for the boot report
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifdef HALP_ACPI_HOST
/* The host test supplies the base types and DbgPrint. */
#else
#include <ntifs.h>
#endif
#include <acpifault.h>

static HALP_ACPI_FAULT HalpAcpiFaults[HALP_ACPI_FAULT_SLOTS];

/* Written only by the boot processor while it parses; read after boot */
static volatile ULONG HalpAcpiFaultTotal;

static const PCSTR HalpAcpiFaultNames[] =
{
    "unspecified",
    "table missing",
    "bad signature",
    "bad length",
    "bad checksum",
    "subtable too short",
    "subtable overruns table",
    "entry has an unexpected length",
    "duplicate identifier",
    "entry type not implemented",
    "processor ignored",
    "processor table full",
    "entry disabled"
};

VOID
HalpAcpiRecordFault(
    _In_ ULONG Signature,
    _In_ ULONG Reason,
    _In_ ULONG Type,
    _In_ ULONG Offset,
    _In_ ULONG Value1,
    _In_ ULONG Value2)
{
    ULONG Index = HalpAcpiFaultTotal;
    PHALP_ACPI_FAULT Fault;

    /* The count keeps growing so an overflow stays visible in the report */
    HalpAcpiFaultTotal = Index + 1;
    if (Index >= HALP_ACPI_FAULT_SLOTS)
        return;

    Fault = &HalpAcpiFaults[Index];
    Fault->Signature = Signature;
    Fault->Reason = Reason;
    Fault->Type = Type;
    Fault->Offset = Offset;
    Fault->Value1 = Value1;
    Fault->Value2 = Value2;
}

ULONG
HalpAcpiFaultCount(VOID)
{
    return HalpAcpiFaultTotal;
}

BOOLEAN
HalpAcpiGetFault(
    _In_ ULONG Index,
    _Out_ PHALP_ACPI_FAULT Fault)
{
    if ((Index >= HalpAcpiFaultTotal) || (Index >= HALP_ACPI_FAULT_SLOTS))
        return FALSE;

    *Fault = HalpAcpiFaults[Index];
    return TRUE;
}

ULONG
NTAPI
HalpAcpiQueryFaults(
    _Out_writes_bytes_opt_(Size) PHALP_ACPI_FAULT Buffer,
    _In_ ULONG Size,
    _Out_opt_ PULONG Copied)
{
    ULONG Total = HalpAcpiFaultTotal;
    ULONG Index, Count = 0;

    if (Copied != NULL) *Copied = 0;
    if (Buffer == NULL) return Total;

    Count = Size / sizeof(HALP_ACPI_FAULT);
    if (Count > HALP_ACPI_FAULT_SLOTS) Count = HALP_ACPI_FAULT_SLOTS;
    if (Count > Total) Count = Total;

    for (Index = 0; Index < Count; Index++)
    {
        Buffer[Index] = HalpAcpiFaults[Index];
    }

    if (Copied != NULL) *Copied = Count;
    return Total;
}

VOID
HalpAcpiReportFaults(VOID)
{
    ULONG Count = HalpAcpiFaultTotal, Index, Retained;
    const HALP_ACPI_FAULT *Fault;
    PCSTR Reason;

    if (Count == 0)
        return;

    Retained = (Count < HALP_ACPI_FAULT_SLOTS) ? Count : HALP_ACPI_FAULT_SLOTS;
    for (Index = 0; Index < Retained; Index++)
    {
        Fault = &HalpAcpiFaults[Index];
        Reason = (Fault->Reason < RTL_NUMBER_OF(HalpAcpiFaultNames)) ?
                 HalpAcpiFaultNames[Fault->Reason] : HalpAcpiFaultNames[0];

        if (Fault->Type != 0)
        {
            DbgPrint("HAL: ACPI %.4s: %s, type %lu, offset %lu, %08lx %08lx\n",
                     (PCSTR)&Fault->Signature,
                     Reason,
                     Fault->Type,
                     Fault->Offset,
                     Fault->Value1,
                     Fault->Value2);
        }
        else
        {
            DbgPrint("HAL: ACPI %.4s: %s, offset %lu, %08lx %08lx\n",
                     (PCSTR)&Fault->Signature,
                     Reason,
                     Fault->Offset,
                     Fault->Value1,
                     Fault->Value2);
        }
    }

    if (Count > Retained)
    {
        DbgPrint("HAL: ACPI: %lu further fault(s) not retained\n", Count - Retained);
    }
}
