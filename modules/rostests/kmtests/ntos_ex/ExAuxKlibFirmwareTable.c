/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AuxKlib hands drivers the ACPI tables the firmware published
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <aux_klib.h>

#define TEST_PROVIDER_ACPI  0x41435049
#define TEST_TABLE_FADT     0x50434146

START_TEST(ExAuxKlibFirmwareTable)
{
    PULONG Tables;
    PUCHAR Fadt;
    ULONG Length = 0, Needed = 0, Index;
    BOOLEAN Listed = FALSE;
    NTSTATUS Status;

    Status = AuxKlibInitialize();
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = AuxKlibGetSystemFirmwareTable(TEST_PROVIDER_ACPI, TEST_TABLE_FADT, NULL, 0, &Length);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok(Length >= 116, "The FADT is %lu bytes\n", Length);
    if (Length < 116)
        return;

    Fadt = ExAllocatePoolZero(PagedPool, Length, 'tfAT');
    ok(Fadt != NULL, "No memory for the FADT\n");
    if (Fadt == NULL)
        return;

    Status = AuxKlibGetSystemFirmwareTable(TEST_PROVIDER_ACPI, TEST_TABLE_FADT, Fadt, Length, &Needed);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Needed, Length);
    ok(RtlEqualMemory(Fadt, "FACP", 4), "The table returned is not the FADT\n");
    ok_eq_ulong(*(PULONG)(Fadt + 4), Length);
    ExFreePoolWithTag(Fadt, 'tfAT');

    Status = AuxKlibGetSystemFirmwareTable(TEST_PROVIDER_ACPI, 0x58585858, NULL, 0, &Needed);
    ok(!NT_SUCCESS(Status) && (Status != STATUS_BUFFER_TOO_SMALL), "A missing table answered 0x%08lx\n", Status);

    Status = AuxKlibEnumerateSystemFirmwareTables(TEST_PROVIDER_ACPI, NULL, 0, &Length);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok((Length != 0) && ((Length % sizeof(ULONG)) == 0), "The ACPI table list is %lu bytes\n", Length);
    if ((Length == 0) || ((Length % sizeof(ULONG)) != 0))
        return;

    Tables = ExAllocatePoolZero(PagedPool, Length, 'tfAT');
    ok(Tables != NULL, "No memory for the table list\n");
    if (Tables == NULL)
        return;

    Status = AuxKlibEnumerateSystemFirmwareTables(TEST_PROVIDER_ACPI, Tables, Length, &Needed);
    ok_eq_hex(Status, STATUS_SUCCESS);
    for (Index = 0; Index < Needed / sizeof(ULONG); Index++)
    {
        if (Tables[Index] == TEST_TABLE_FADT)
            Listed = TRUE;
    }
    ok(Listed, "The FADT is not in the ACPI table list\n");
    ExFreePoolWithTag(Tables, 'tfAT');
}
