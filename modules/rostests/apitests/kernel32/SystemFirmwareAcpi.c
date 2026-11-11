/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The ACPI provider of EnumSystemFirmwareTables and GetSystemFirmwareTable
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define ACPI_PROVIDER 'ACPI'
#define FADT_TABLE_ID 'PCAF'
#define ABSENT_TABLE_ID 'XXXX'

START_TEST(SystemFirmwareAcpi)
{
    DWORD Ids[512];
    BYTE Header[36];
    UINT Size, Listed, Length, Index;
    BOOL FadtListed = FALSE;
    BYTE *Table;

    SetLastError(0xdeadbeef);
    Size = EnumSystemFirmwareTables(ACPI_PROVIDER, NULL, 0);
    ok(Size >= sizeof(DWORD) && (Size % sizeof(DWORD)) == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "enumeration size %u, error %lu\n", Size, GetLastError());

    Listed = EnumSystemFirmwareTables(ACPI_PROVIDER, Ids, sizeof(Ids));
    ok(Listed == Size, "enumerated %u bytes, sized %u\n", Listed, Size);
    for (Index = 0; Index < Listed / sizeof(DWORD) && Index < ARRAYSIZE(Ids); Index++)
    {
        if (Ids[Index] == FADT_TABLE_ID) FadtListed = TRUE;
    }
    ok(FadtListed, "FACP not among %u tables\n", Listed / (UINT)sizeof(DWORD));

    SetLastError(0xdeadbeef);
    Length = GetSystemFirmwareTable(ACPI_PROVIDER, FADT_TABLE_ID, NULL, 0);
    ok(Length >= sizeof(Header) && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "FACP size %u, error %lu\n", Length, GetLastError());

    SetLastError(0xdeadbeef);
    ok(GetSystemFirmwareTable(ACPI_PROVIDER, FADT_TABLE_ID, Header, sizeof(Header) - 1) == Length &&
       GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "short FACP buffer error %lu\n", GetLastError());

    Table = HeapAlloc(GetProcessHeap(), 0, max(Length, 1));
    ok(Table != NULL, "allocation failed\n");
    if (Table != NULL)
    {
        ok(GetSystemFirmwareTable(ACPI_PROVIDER, FADT_TABLE_ID, Table, Length) == Length,
           "FACP read failed %lu\n", GetLastError());
        ok(Length >= sizeof(Header) && memcmp(Table, "FACP", 4) == 0 && *(DWORD *)(Table + 4) == Length,
           "FACP header does not describe the table\n");
        HeapFree(GetProcessHeap(), 0, Table);
    }

    SetLastError(0xdeadbeef);
    ok(GetSystemFirmwareTable(ACPI_PROVIDER, ABSENT_TABLE_ID, Header, sizeof(Header)) == 0 &&
       GetLastError() == ERROR_NOT_FOUND,
       "absent table error %lu\n", GetLastError());
}
