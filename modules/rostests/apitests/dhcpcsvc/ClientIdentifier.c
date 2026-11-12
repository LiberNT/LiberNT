/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the client identifier the DHCP client sends
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <iphlpapi.h>
#include <stdio.h>

static PIP_ADAPTER_ADDRESSES
QueryAddresses(VOID)
{
    PIP_ADAPTER_ADDRESSES Addresses;
    ULONG Size = 0;

    if (GetAdaptersAddresses(AF_INET, 0, NULL, NULL, &Size) != ERROR_BUFFER_OVERFLOW)
        return NULL;
    Addresses = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Addresses && GetAdaptersAddresses(AF_INET, 0, NULL, Addresses, &Size) != NO_ERROR)
    {
        HeapFree(GetProcessHeap(), 0, Addresses);
        Addresses = NULL;
    }
    return Addresses;
}

static BOOL
IsEchoAdapter(PIP_ADAPTER_ADDRESSES Adapter)
{
    return (Adapter->Flags & IP_ADAPTER_DHCP_ENABLED) && !wcsncmp(Adapter->DnsSuffix, L"cid-", 4);
}

static BOOL
IsLeasePending(PIP_ADAPTER_ADDRESSES Adapter)
{
    PIP_ADAPTER_UNICAST_ADDRESS Unicast;
    PUCHAR Bytes;

    if (!(Adapter->Flags & IP_ADAPTER_DHCP_ENABLED) || Adapter->OperStatus != IfOperStatusUp ||
        Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
        return FALSE;
    for (Unicast = Adapter->FirstUnicastAddress; Unicast; Unicast = Unicast->Next)
    {
        Bytes = (PUCHAR)&((struct sockaddr_in *)Unicast->Address.lpSockaddr)->sin_addr;
        if (Bytes[0] && !(Bytes[0] == 169 && Bytes[1] == 254))
            return FALSE;
    }
    return TRUE;
}

static PIP_ADAPTER_ADDRESSES
WaitForLeases(VOID)
{
    PIP_ADAPTER_ADDRESSES Addresses, Adapter;
    ULONGLONG Start = GetTickCount64();
    BOOL Pending;

    for (;;)
    {
        Addresses = QueryAddresses();
        Pending = FALSE;
        for (Adapter = Addresses; Adapter && !Pending; Adapter = Adapter->Next)
            Pending = IsLeasePending(Adapter);
        if (!Pending || GetTickCount64() - Start >= 15000)
            return Addresses;
        if (Addresses)
            HeapFree(GetProcessHeap(), 0, Addresses);
        Sleep(500);
    }
}

START_TEST(ClientIdentifier)
{
    PIP_ADAPTER_ADDRESSES Addresses, Adapter;
    WCHAR Expected[4 + 2 * (1 + MAX_ADAPTER_ADDRESS_LENGTH) + 1];
    ULONG Byte, Tested = 0;

    Addresses = WaitForLeases();
    ok(Addresses != NULL, "GetAdaptersAddresses failed\n");
    for (Adapter = Addresses; Adapter; Adapter = Adapter->Next)
    {
        if (!IsEchoAdapter(Adapter))
            continue;
        ok(Adapter->PhysicalAddressLength == 6, "Adapter %lu has a %lu-byte address\n",
           Adapter->IfIndex, Adapter->PhysicalAddressLength);
        wcscpy(Expected, L"cid-01");
        for (Byte = 0; Byte < Adapter->PhysicalAddressLength && Byte < MAX_ADAPTER_ADDRESS_LENGTH; Byte++)
            _snwprintf(Expected + wcslen(Expected), 3, L"%02x", Adapter->PhysicalAddress[Byte]);
        ok(!wcscmp(Adapter->DnsSuffix, Expected), "Adapter %lu sent %ls, expected %ls\n",
           Adapter->IfIndex, Adapter->DnsSuffix, Expected);
        Tested++;
    }
    if (Addresses)
        HeapFree(GetProcessHeap(), 0, Addresses);
    if (!Tested)
        skip("No DHCP server echoes the client identifier\n");
}
