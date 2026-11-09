/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SetIpInterfaceEntry with IPv4 rows
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

static BOOL
GetRow(ULONG Index, MIB_IPINTERFACE_ROW *Row)
{
    DWORD Error;

    InitializeIpInterfaceEntry(Row);
    Row->Family = AF_INET;
    Row->InterfaceIndex = Index;
    Error = GetIpInterfaceEntry(Row);
    ok(Error == NO_ERROR, "GetIpInterfaceEntry(%lu) returned %lu\n", Index, Error);
    return Error == NO_ERROR;
}

/* Windows 11 refuses an IPv4 row with a site prefix length other than 0, also the one it reads back */
static void
Restore(MIB_IPINTERFACE_ROW *Original)
{
    MIB_IPINTERFACE_ROW Row = *Original;
    DWORD Error;

    Row.SitePrefixLength = 0;
    Error = SetIpInterfaceEntry(&Row);
    ok(Error == NO_ERROR, "Restoring the row returned %lu\n", Error);
}

static void
test_SitePrefixLength(void)
{
    MIB_IPINTERFACE_ROW Original, Row, ReadBack;
    IP_ADAPTER_ADDRESSES *Addresses, *Adapter;
    ULONG Size = 0;
    DWORD Error;

    if (!GetRow(1, &Original))
        return;
    trace("Loopback IPv4 SitePrefixLength %lu\n", Original.SitePrefixLength);

    Row = Original;
    Row.SitePrefixLength = 64;
    Error = SetIpInterfaceEntry(&Row);
    ok(Error == ERROR_INVALID_PARAMETER, "Setting SitePrefixLength 64 returned %lu\n", Error);

    Row = Original;
    Row.SitePrefixLength = 48;
    Error = SetIpInterfaceEntry(&Row);
    ok(Error == ERROR_INVALID_PARAMETER, "Setting SitePrefixLength 48 returned %lu\n", Error);

    Row = Original;
    Row.SitePrefixLength = 0;
    Error = SetIpInterfaceEntry(&Row);
    ok(Error == NO_ERROR, "Setting SitePrefixLength 0 returned %lu\n", Error);
    if (GetRow(1, &ReadBack))
        ok(ReadBack.SitePrefixLength == 0, "SitePrefixLength is %lu after setting 0\n", ReadBack.SitePrefixLength);

    GetAdaptersAddresses(AF_INET, 0, NULL, NULL, &Size);
    Addresses = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Addresses && GetAdaptersAddresses(AF_INET, 0, NULL, Addresses, &Size) == NO_ERROR)
    {
        for (Adapter = Addresses; Adapter; Adapter = Adapter->Next)
        {
            if (Adapter->IfType != IF_TYPE_SOFTWARE_LOOPBACK && GetRow(Adapter->IfIndex, &ReadBack))
            {
                trace("Interface %lu (type %lu) IPv4 SitePrefixLength %lu\n", Adapter->IfIndex, Adapter->IfType,
                      ReadBack.SitePrefixLength);
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, Addresses);
}

static void
test_Dhcpv6Flags(void)
{
    MIB_IPINTERFACE_ROW Original, Row, ReadBack;
    DWORD Error;

    if (!GetRow(1, &Original))
        return;

    Row = Original;
    Row.SitePrefixLength = 0;
    Row.ManagedAddressConfigurationSupported = !Row.ManagedAddressConfigurationSupported;
    Row.OtherStatefulConfigurationSupported = !Row.OtherStatefulConfigurationSupported;
    Error = SetIpInterfaceEntry(&Row);
    ok(Error == NO_ERROR, "Setting other DHCPv6 flags on IPv4 returned %lu\n", Error);
    if (GetRow(1, &ReadBack))
    {
        ok(ReadBack.ManagedAddressConfigurationSupported == Row.ManagedAddressConfigurationSupported &&
           ReadBack.OtherStatefulConfigurationSupported == Row.OtherStatefulConfigurationSupported,
           "The flags read back as %u %u\n", ReadBack.ManagedAddressConfigurationSupported,
           ReadBack.OtherStatefulConfigurationSupported);
    }
    Restore(&Original);
    if (GetRow(1, &ReadBack))
    {
        ok(ReadBack.ManagedAddressConfigurationSupported == Original.ManagedAddressConfigurationSupported,
           "The flags were not restored\n");
    }
}

START_TEST(SetIpInterfaceEntry)
{
    test_SitePrefixLength();
    test_Dhcpv6Flags();
}
