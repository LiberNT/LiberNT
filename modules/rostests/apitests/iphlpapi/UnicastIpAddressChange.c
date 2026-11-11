/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for stable unicast address rows and unicast change notifications
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
static LONG DadAdds;
static LONG DadParameters;
static ULONG DadAddress;
static HANDLE DadDone;

static VOID WINAPI UnicastChanged(PVOID Context, PMIB_UNICASTIPADDRESS_ROW Row, MIB_NOTIFICATION_TYPE Type)
{
    InterlockedIncrement(&UnicastCallbacks);
}

static VOID WINAPI DadChanged(PVOID Context, PMIB_UNICASTIPADDRESS_ROW Row, MIB_NOTIFICATION_TYPE Type)
{
    if (!Row || Row->Address.si_family != AF_INET || Row->Address.Ipv4.sin_addr.s_addr != DadAddress)
        return;
    if (Type == MibAddInstance)
        InterlockedIncrement(&DadAdds);
    else if (Type == MibParameterNotification)
    {
        InterlockedIncrement(&DadParameters);
        SetEvent(DadDone);
    }
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
    Sleep(50);
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
    Sleep(200);
    if (Unicast)
        ok_long(CancelMibChangeNotify2(Unicast), NO_ERROR);
    if (Route)
        ok_long(CancelMibChangeNotify2(Route), NO_ERROR);
    ok_long(UnicastCallbacks, 0);
    ok_long(RouteCallbacks, 0);
}

static VOID TestDadCompletionNotification(VOID)
{
    PMIB_UNICASTIPADDRESS_TABLE Table = NULL;
    MIB_UNICASTIPADDRESS_ROW Row, Current;
    const MIB_UNICASTIPADDRESS_ROW *Lease = NULL;
    HANDLE Notify = NULL;
    ULONGLONG Start, Elapsed;
    ULONG Index, Host;
    DWORD Status, Result;

    ok_long(GetUnicastIpAddressTable(AF_INET, &Table), NO_ERROR);
    for (Index = 0; Table && Index < Table->NumEntries && !Lease; Index++)
    {
        if (Table->Table[Index].PrefixOrigin == IpPrefixOriginDhcp && Table->Table[Index].DadState == IpDadStatePreferred)
            Lease = &Table->Table[Index];
    }
    if (!Lease)
    {
        skip("No preferred DHCP IPv4 address to add a neighbor address beside\n");
        FreeMibTable(Table);
        return;
    }

    Host = ntohl(Lease->Address.Ipv4.sin_addr.s_addr);
    Host = (Host & 0xFFFFFF00) | ((Host & 0xFF) == 0xFA ? 0xFB : 0xFA);
    DadAddress = htonl(Host);
    InitializeUnicastIpAddressEntry(&Row);
    Row.InterfaceLuid = Lease->InterfaceLuid;
    Row.Address.Ipv4.sin_family = AF_INET;
    Row.Address.Ipv4.sin_addr.s_addr = DadAddress;
    Row.OnLinkPrefixLength = Lease->OnLinkPrefixLength;
    FreeMibTable(Table);

    DadDone = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok_long(NotifyUnicastIpAddressChange(AF_INET, DadChanged, NULL, FALSE, &Notify), NO_ERROR);
    Start = GetTickCount64();
    Status = CreateUnicastIpAddressEntry(&Row);
    ok_long(Status, NO_ERROR);
    if (Status == NO_ERROR)
    {
        Result = WaitForSingleObject(DadDone, 5000);
        Elapsed = GetTickCount64() - Start;
        trace("Duplicate address detection completed after %I64u ms\n", Elapsed);
        ok(Result == WAIT_OBJECT_0, "No notification of the completed duplicate address detection (%lu)\n", Result);
        /* CreateUnicastIpAddressEntry documents about 3 seconds for IPv4: three probes one second apart */
        ok(Elapsed <= 3500, "Duplicate address detection took %I64u ms\n", Elapsed);
        Current = Row;
        ok_long(GetUnicastIpAddressEntry(&Current), NO_ERROR);
        ok_long(Current.DadState, IpDadStatePreferred);
        ok_long(DeleteUnicastIpAddressEntry(&Row), NO_ERROR);
    }
    if (Notify)
        ok_long(CancelMibChangeNotify2(Notify), NO_ERROR);
    ok_long(DadAdds, 1);
    ok(DadParameters >= 1, "No parameter notification when duplicate address detection completed\n");
    if (DadDone)
        CloseHandle(DadDone);
}

START_TEST(UnicastIpAddressChange)
{
    TestCreationTimeStamp();
    TestQuietNotifications();
    TestDadCompletionNotification();
}
