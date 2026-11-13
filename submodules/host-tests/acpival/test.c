/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Firmware table validation and fault reporting tests
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <acpival.h>
#include <acpifault.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int Failures;
static char LastPrint[256];
static unsigned Prints;

VOID
DbgPrint(
    PCSTR Format,
    ...)
{
    va_list Arguments;

    va_start(Arguments, Format);
    vsnprintf(LastPrint, sizeof(LastPrint), Format, Arguments);
    va_end(Arguments);
    Prints++;
}

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            Failures++; \
        } \
    } while (0)

static
VOID
MakeTable(
    _Out_writes_(Length) UCHAR *Table,
    _In_ ULONG Length,
    _In_ const char Signature[4])
{
    ULONG Index, Sum = 0;

    memset(Table, 0, Length);
    memcpy(Table, Signature, 4);
    Table[4] = (UCHAR)(Length & 0xFF);
    Table[5] = (UCHAR)((Length >> 8) & 0xFF);
    Table[6] = (UCHAR)((Length >> 16) & 0xFF);
    Table[7] = (UCHAR)((Length >> 24) & 0xFF);
    Table[8] = 2;

    for (Index = 0; Index < Length; Index++)
        Sum += Table[Index];

    Table[9] = (UCHAR)(0 - Sum);
}

static
VOID
TestTableHeader(VOID)
{
    UCHAR Table[64];
    HALP_ACPI_CHECK Check;

    MakeTable(Table, 36, "TEST");
    CHECK(HalpAcpiCheckTable(Table, 36, &Check) == HalpAcpiAccepted);
    CHECK(Check.Signature == 0x54534554);
    CHECK(Check.Length == 36);
    CHECK(Check.Sum == 0);
    CHECK(Check.Revision == 2);

    /* Extra mapped bytes beyond the declared length are not an error */
    CHECK(HalpAcpiCheckTable(Table, sizeof(Table), &Check) == HalpAcpiAccepted);

    /* Nothing readable at all */
    CHECK(HalpAcpiCheckTable(Table, 8, &Check) == HalpAcpiBadLength);

    /* A lowercase fourth byte is not a name character */
    MakeTable(Table, 36, "TESx");
    CHECK(HalpAcpiCheckTable(Table, 36, &Check) == HalpAcpiBadSignature);

    /* The Alert Standard Format table ends its signature with an exclamation mark */
    MakeTable(Table, 36, "ASF!");
    CHECK(HalpAcpiCheckTable(Table, 36, &Check) == HalpAcpiAccepted);
    CHECK(Check.Signature == 0x21465341);

    /* It is accepted in that position only */
    MakeTable(Table, 36, "A!SF");
    CHECK(HalpAcpiCheckTable(Table, 36, &Check) == HalpAcpiBadSignature);
    MakeTable(Table, 36, "!ASF");
    CHECK(HalpAcpiCheckTable(Table, 36, &Check) == HalpAcpiBadSignature);

    /* Declared length below the header size */
    MakeTable(Table, 36, "TEST");
    Table[4] = 20;
    Table[5] = 0;
    Table[9] = 0;
    CHECK(HalpAcpiCheckTable(Table, 36, &Check) == HalpAcpiBadLength);
    CHECK(Check.Length == 20);

    /* Declared length beyond the mapped bytes */
    MakeTable(Table, 36, "TEST");
    Table[4] = 64;
    CHECK(HalpAcpiCheckTable(Table, 36, &Check) == HalpAcpiBadLength);

    /* One byte off in the checksum */
    MakeTable(Table, 36, "TEST");
    Table[9] ^= 1;
    CHECK(HalpAcpiCheckTable(Table, 36, &Check) == HalpAcpiBadChecksum);
    CHECK(Check.Sum != 0);
}

static
VOID
TestSubtables(VOID)
{
    UCHAR Table[32];
    ULONG Offset;
    UCHAR Type;

    memset(Table, 0, sizeof(Table));

    /* No entries at all */
    CHECK(HalpAcpiCheckSubtables(Table, 0, 0, &Offset, &Type) == HalpAcpiAccepted);

    /* Three well-formed entries: 8 + 8 + 4 bytes */
    Table[0] = 1;
    Table[1] = 8;
    Table[8] = 2;
    Table[9] = 8;
    Table[16] = 3;
    Table[17] = 4;
    CHECK(HalpAcpiCheckSubtables(Table, 0, 20, &Offset, &Type) == HalpAcpiAccepted);

    /* An entry cannot be shorter than its own two header bytes */
    Table[17] = 1;
    CHECK(HalpAcpiCheckSubtables(Table, 0, 20, &Offset, &Type) == HalpAcpiBadSubtable);
    CHECK(Offset == 16);
    CHECK(Type == 3);

    /* An entry that runs past the end of the table */
    Table[17] = 30;
    CHECK(HalpAcpiCheckSubtables(Table, 0, 20, &Offset, &Type) == HalpAcpiBadSubtable);
    CHECK(Offset == 16);

    /* A trailing byte that cannot hold an entry header */
    Table[17] = 4;
    CHECK(HalpAcpiCheckSubtables(Table, 0, 21, &Offset, &Type) == HalpAcpiBadSubtable);
    CHECK(Offset == 20);
    CHECK(Type == 0);

    /* A first-entry offset beyond the table */
    CHECK(HalpAcpiCheckSubtables(Table, 21, 20, &Offset, &Type) == HalpAcpiBadSubtable);
    CHECK(Offset == 21);
}

static
VOID
TestFaults(VOID)
{
    HALP_ACPI_FAULT Fault, Faults[HALP_ACPI_FAULT_SLOTS];
    ULONG Copied = 1, Total;
    unsigned Index;

    /* A clean boot reports nothing */
    CHECK(HalpAcpiFaultCount() == 0);
    Total = HalpAcpiQueryFaults(NULL, 0, &Copied);
    CHECK(Total == 0);
    CHECK(Copied == 0);
    Prints = 0;
    HalpAcpiReportFaults();
    CHECK(Prints == 0);

    HalpAcpiRecordFault(0x54534554, HalpAcpiFaultBadChecksum, 0, 0, 0x7F, 36);
    HalpAcpiRecordFault(0x54534554, HalpAcpiFaultProcessorIgnored, 0, 64, 1, 0x100);
    CHECK(HalpAcpiFaultCount() == 2);
    CHECK(HalpAcpiGetFault(0, &Fault) == TRUE);
    CHECK(Fault.Signature == 0x54534554);
    CHECK(Fault.Reason == HalpAcpiFaultBadChecksum);
    CHECK(Fault.Value1 == 0x7F);
    CHECK(HalpAcpiGetFault(1, &Fault) == TRUE);
    CHECK(Fault.Reason == HalpAcpiFaultProcessorIgnored);
    CHECK(Fault.Type == 0);
    CHECK(Fault.Offset == 64);
    CHECK(HalpAcpiGetFault(2, &Fault) == FALSE);

    Copied = 0;
    Total = HalpAcpiQueryFaults(Faults, sizeof(Faults), &Copied);
    CHECK(Total == 2);
    CHECK(Copied == 2);
    CHECK(Faults[0].Signature == 0x54534554);
    CHECK(Faults[0].Reason == HalpAcpiFaultBadChecksum);
    CHECK(Faults[1].Offset == 64);

    /* A buffer that holds one whole record copies exactly one */
    Copied = 0;
    Total = HalpAcpiQueryFaults(Faults, sizeof(HALP_ACPI_FAULT) + 4, &Copied);
    CHECK(Total == 2);
    CHECK(Copied == 1);

    Prints = 0;
    HalpAcpiReportFaults();
    CHECK(Prints == 2);
    CHECK(strstr(LastPrint, "processor ignored") != NULL);

    /* The detail window is bounded; the count is not */
    for (Index = 0; Index < 20; Index++)
        HalpAcpiRecordFault(0x54534554, HalpAcpiFaultBadChecksum, 0, 0, Index, 36);

    CHECK(HalpAcpiFaultCount() == 22);
    CHECK(HalpAcpiGetFault(15, &Fault) == TRUE);
    CHECK(HalpAcpiGetFault(16, &Fault) == FALSE);

    Copied = 0;
    Total = HalpAcpiQueryFaults(Faults, sizeof(Faults), &Copied);
    CHECK(Total == 22);
    CHECK(Copied == HALP_ACPI_FAULT_SLOTS);
    CHECK(Faults[HALP_ACPI_FAULT_SLOTS - 1].Value1 == 13);
    CHECK(HalpAcpiQueryFaults(Faults, sizeof(Faults), NULL) == 22);

    Prints = 0;
    HalpAcpiReportFaults();
    CHECK(Prints == HALP_ACPI_FAULT_SLOTS + 1);
    CHECK(strstr(LastPrint, "6 further fault(s) not retained") != NULL);
}

int
main(VOID)
{
    TestTableHeader();
    TestSubtables();
    TestFaults();

    if (Failures != 0)
    {
        printf("%d check(s) failed\n", Failures);
        return 1;
    }

    printf("all checks passed\n");
    return 0;
}
