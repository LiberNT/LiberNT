/*
 * PROJECT: LiberNT API tests
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Pin the IPv4 link-local address timeout defaults
 * COPYRIGHT: Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

START_TEST(GetIpInterfaceEntry)
{
    PMIB_IPINTERFACE_TABLE Table;
    DWORD Error;
    ULONG Index;
    ULONG Loopbacks = 0;
    ULONG Adapters = 0;

    Error = GetIpInterfaceTable(AF_INET, &Table);
    ok(Error == NO_ERROR, "GetIpInterfaceTable returned %lu\n", Error);
    if (Error != NO_ERROR)
        return;

    for (Index = 0; Index < Table->NumEntries; Index++)
    {
        MIB_IPINTERFACE_ROW Row = Table->Table[Index];
        MIB_IF_ROW2 IfRow;
        ULONG Expected;

        Error = GetIpInterfaceEntry(&Row);
        ok(Error == NO_ERROR, "GetIpInterfaceEntry(%lu) returned %lu\n",
           Table->Table[Index].InterfaceIndex, Error);
        if (Error != NO_ERROR)
            continue;

        ZeroMemory(&IfRow, sizeof(IfRow));
        IfRow.InterfaceLuid = Row.InterfaceLuid;
        IfRow.InterfaceIndex = Row.InterfaceIndex;
        Error = GetIfEntry2(&IfRow);
        ok(Error == NO_ERROR, "GetIfEntry2(%lu) returned %lu\n", Row.InterfaceIndex, Error);
        if (Error != NO_ERROR)
            continue;

        if (IfRow.Type == IF_TYPE_SOFTWARE_LOOPBACK)
        {
            Expected = 6500;
            Loopbacks++;
        }
        else
        {
            Expected = 60000;
            Adapters++;
        }

        ok(Row.LinkLocalAddressTimeout == Expected,
           "Interface %lu type %lu LinkLocalAddressTimeout %lu, expected %lu\n",
           Row.InterfaceIndex, IfRow.Type, Row.LinkLocalAddressTimeout, Expected);
    }

    ok(Loopbacks != 0, "No IPv4 loopback interface to check\n");
    FreeMibTable(Table);

    if (!Adapters)
        skip("No non-loopback IPv4 interface to check for 60000\n");
}
