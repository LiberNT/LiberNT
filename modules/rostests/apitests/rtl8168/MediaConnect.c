/*
 * PROJECT: LiberNT API tests
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Pin RTL8168 adapter presence and media connect query
 * COPYRIGHT: Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
 */

#include <apitest.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

static BOOL
IsRtl8168Description(
    _In_opt_ const WCHAR *Description)
{
    if (!Description)
        return FALSE;
    if (wcsstr(Description, L"RTL8168") ||
        wcsstr(Description, L"RTL8111") ||
        wcsstr(Description, L"Realtek RTL8168") ||
        wcsstr(Description, L"Realtek RTL8111"))
    {
        return TRUE;
    }
    return FALSE;
}

START_TEST(MediaConnect)
{
    ULONG Size = 0;
    ULONG Status;
    IP_ADAPTER_ADDRESSES *Table;
    IP_ADAPTER_ADDRESSES *Entry;
    DWORD Found = 0;
    DWORD Up = 0;

    Status = GetAdaptersAddresses(AF_UNSPEC,
                                  GAA_FLAG_INCLUDE_PREFIX,
                                  NULL,
                                  NULL,
                                  &Size);
    if (Status != ERROR_BUFFER_OVERFLOW)
    {
        skip("GetAdaptersAddresses unavailable (%lu)\n", Status);
        return;
    }

    Table = HeapAlloc(GetProcessHeap(), 0, Size);
    ok(Table != NULL, "alloc adapters\n");
    if (!Table)
        return;

    Status = GetAdaptersAddresses(AF_UNSPEC,
                                  GAA_FLAG_INCLUDE_PREFIX,
                                  NULL,
                                  Table,
                                  &Size);
    ok(Status == ERROR_SUCCESS, "GetAdaptersAddresses %lu\n", Status);
    if (Status != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Table);
        return;
    }

    for (Entry = Table; Entry; Entry = Entry->Next)
    {
        if (!IsRtl8168Description(Entry->Description) &&
            !IsRtl8168Description(Entry->FriendlyName))
        {
            continue;
        }

        Found++;
        if (Entry->OperStatus == IfOperStatusUp)
            Up++;

        ok(Entry->IfType == IF_TYPE_ETHERNET_CSMACD,
           "unexpected IfType %lu\n", Entry->IfType);
        ok(Entry->Mtu >= 1500, "mtu %lu\n", Entry->Mtu);
    }

    if (!Found)
    {
        HeapFree(GetProcessHeap(), 0, Table);
        skip("no RTL8168/8111 adapter bound; needs real hardware\n");
        return;
    }

    ok(Found >= 1, "found %lu adapter(s)\n", Found);
    trace("rtl8168 adapters=%lu up=%lu\n", Found, Up);

    if (Up)
    {
        DWORD Dhcp = 0;
        DWORD Apipa = 0;

        for (Entry = Table; Entry; Entry = Entry->Next)
        {
            IP_ADAPTER_UNICAST_ADDRESS *Unicast;

            if (!IsRtl8168Description(Entry->Description) &&
                !IsRtl8168Description(Entry->FriendlyName))
            {
                continue;
            }
            if (Entry->OperStatus != IfOperStatusUp)
                continue;

            for (Unicast = Entry->FirstUnicastAddress;
                 Unicast;
                 Unicast = Unicast->Next)
            {
                SOCKADDR_IN *Sin;

                if (Unicast->Address.lpSockaddr->sa_family != AF_INET)
                    continue;
                Sin = (SOCKADDR_IN *)Unicast->Address.lpSockaddr;
                if ((ntohl(Sin->sin_addr.s_addr) & 0xffff0000) == 0xa9fe0000)
                    Apipa++;
                else if (Sin->sin_addr.s_addr != 0)
                    Dhcp++;
            }
        }

        trace("rtl8168 ipv4 dhcp-like=%lu apipa=%lu\n", Dhcp, Apipa);
        if (!Dhcp && Apipa)
            trace("rtl8168 still on APIPA; DHCP offer path needs a live lease\n");
    }

    HeapFree(GetProcessHeap(), 0, Table);
}
