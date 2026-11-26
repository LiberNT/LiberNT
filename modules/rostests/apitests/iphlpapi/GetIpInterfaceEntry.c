/*
 * PROJECT: LiberNT API tests
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Pin the IPv4 link-local address timeout default
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
    ULONG Checked = 0;

    Error = GetIpInterfaceTable(AF_INET, &Table);
    ok(Error == NO_ERROR, "GetIpInterfaceTable returned %lu\n", Error);
    if (Error != NO_ERROR)
        return;

    for (Index = 0; Index < Table->NumEntries; Index++)
    {
        MIB_IPINTERFACE_ROW Row = Table->Table[Index];

        Error = GetIpInterfaceEntry(&Row);
        ok(Error == NO_ERROR, "GetIpInterfaceEntry(%lu) returned %lu\n",
           Table->Table[Index].InterfaceIndex, Error);
        if (Error != NO_ERROR)
            continue;

        ok(Row.LinkLocalAddressTimeout == 60000,
           "Interface %lu LinkLocalAddressTimeout %lu, expected 60000\n",
           Row.InterfaceIndex, Row.LinkLocalAddressTimeout);
        Checked++;
    }

    ok(Checked != 0, "No IPv4 interfaces to check\n");
    FreeMibTable(Table);
}
