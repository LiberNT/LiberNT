/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for stable unicast address rows and quiet change notifications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

static LONG UnicastCallbacks;
static LONG RouteCallbacks;

static VOID WINAPI UnicastChanged(PVOID Context, PMIB_UNICASTIPADDRESS_ROW Row, MIB_NOTIFICATION_TYPE Type)
{
    InterlockedIncrement(&UnicastCallbacks);
}

static VOID WINAPI RouteChanged(PVOID Context, PMIB_IPFORWARD_ROW2 Row, MIB_NOTIFICATION_TYPE Type)
{
    InterlockedIncrement(&RouteCallbacks);
}

static const MIB_UNICASTIPADDRESS_ROW *FindRow(const MIB_UNICASTIPADDRESS_TABLE *Table,
                                               const MIB_UNICASTIPADDRESS_ROW *Row)
{
    ULONG Index;

    for (Index = 0; Index < Table->NumEntries; Index++)
    {
        const MIB_UNICASTIPADDRESS_ROW *Other = &Table->Table[Index];

        if (Other->InterfaceLuid.Value != Row->InterfaceLuid.Value ||
            Other->Address.si_family != Row->Address.si_family)
            continue;
        if (Row->Address.si_family == AF_INET &&
            Other->Address.Ipv4.sin_addr.s_addr == Row->Address.Ipv4.sin_addr.s_addr)
            return Other;
        if (Row->Address.si_family == AF_INET6 &&
            !memcmp(&Other->Address.Ipv6.sin6_addr, &Row->Address.Ipv6.sin6_addr, sizeof(IN6_ADDR)))
            return Other;
    }
    return NULL;
}

static VOID TestCreationTimeStamp(VOID)
{
    PMIB_UNICASTIPADDRESS_TABLE First = NULL, Second = NULL;
    ULONG Index, Compared = 0, Ipv6 = 0;

    ok_long(GetUnicastIpAddressTable(AF_UNSPEC, &First), NO_ERROR);
    Sleep(1100);
    ok_long(GetUnicastIpAddressTable(AF_UNSPEC, &Second), NO_ERROR);
    if (!First || !Second)
    {
        FreeMibTable(First);
        FreeMibTable(Second);
        return;
    }

    for (Index = 0; Index < First->NumEntries; Index++)
    {
        const MIB_UNICASTIPADDRESS_ROW *Row = &First->Table[Index];
        const MIB_UNICASTIPADDRESS_ROW *Later = FindRow(Second, Row);

        if (!Later)
            continue;
        Compared++;
        if (Row->Address.si_family == AF_INET6)
            Ipv6++;
        ok(Row->CreationTimeStamp.QuadPart == Later->CreationTimeStamp.QuadPart,
           "Row %lu (family %u) creation time changed from %I64d to %I64d\n", Index, Row->Address.si_family,
           Row->CreationTimeStamp.QuadPart, Later->CreationTimeStamp.QuadPart);
    }
    ok(Compared != 0, "No unicast address was present in both tables\n");
    ok(Ipv6 != 0, "No IPv6 unicast address was present in both tables\n");

    FreeMibTable(First);
    FreeMibTable(Second);
}

static VOID TestQuietNotifications(VOID)
{
    HANDLE Unicast = NULL, Route = NULL;

    ok_long(NotifyUnicastIpAddressChange(AF_UNSPEC, UnicastChanged, NULL, FALSE, &Unicast), NO_ERROR);
    ok_long(NotifyRouteChange2(AF_UNSPEC, RouteChanged, NULL, FALSE, &Route), NO_ERROR);
    Sleep(2500);
    if (Unicast)
        ok_long(CancelMibChangeNotify2(Unicast), NO_ERROR);
    if (Route)
        ok_long(CancelMibChangeNotify2(Route), NO_ERROR);
    ok_long(UnicastCallbacks, 0);
    ok_long(RouteCallbacks, 0);
}

START_TEST(UnicastIpAddressChange)
{
    TestCreationTimeStamp();
    TestQuietNotifications();
}
