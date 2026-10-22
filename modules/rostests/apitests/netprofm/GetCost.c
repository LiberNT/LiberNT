/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests INetworkCostManager::GetCost for the default route and a destination
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <initguid.h>
#include <netlistmgr.h>

static DWORD
ExpectedCost(
    _In_ NET_IFINDEX Index)
{
    MIB_IF_ROW2 Row;
    const WCHAR *Name = L"Default";
    DWORD Value, Size = sizeof(Value);

    ZeroMemory(&Row, sizeof(Row));
    Row.InterfaceIndex = Index;
    if (GetIfEntry2(&Row) == NO_ERROR)
    {
        if (Row.Type == IF_TYPE_ETHERNET_CSMACD)
            Name = L"Ethernet";
        else if (Row.Type == IF_TYPE_IEEE80211)
            Name = L"WiFi";
    }
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\NetworkList\\DefaultMediaCost",
                     Name, RRF_RT_REG_DWORD, NULL, &Value, &Size) != ERROR_SUCCESS)
        Value = 1;
    return Value == 2 ? NLM_CONNECTION_COST_FIXED : NLM_CONNECTION_COST_UNRESTRICTED;
}

static BOOL
DefaultRouteInterface(
    _Out_ NET_IFINDEX *Index)
{
    MIB_IPFORWARD_TABLE2 *Table;
    ULONG Metric = ~0u, Entry;
    BOOL Found = FALSE;

    if (GetIpForwardTable2(AF_UNSPEC, &Table) != NO_ERROR)
        return FALSE;
    for (Entry = 0; Entry < Table->NumEntries; Entry++)
    {
        if (Table->Table[Entry].DestinationPrefix.PrefixLength || Table->Table[Entry].Metric >= Metric)
            continue;
        Metric = Table->Table[Entry].Metric;
        *Index = Table->Table[Entry].InterfaceIndex;
        Found = TRUE;
    }
    FreeMibTable(Table);
    return Found;
}

START_TEST(GetCost)
{
    INetworkListManager *Manager;
    INetworkCostManager *Costs;
    NLM_SOCKADDR Destination;
    SOCKADDR_IN *Loopback = (SOCKADDR_IN *)&Destination;
    NET_IFINDEX Index;
    DWORD Cost;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_NetworkListManager, NULL, CLSCTX_INPROC_SERVER,
                          &IID_INetworkListManager, (void **)&Manager);
    ok(hr == S_OK, "CoCreateInstance failed, hr %#lx\n", hr);
    if (FAILED(hr))
    {
        CoUninitialize();
        return;
    }
    hr = INetworkListManager_QueryInterface(Manager, &IID_INetworkCostManager, (void **)&Costs);
    ok(hr == S_OK, "No INetworkCostManager, hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(INetworkCostManager_GetCost(Costs, NULL, NULL) == E_POINTER, "NULL cost accepted\n");

        ZeroMemory(&Destination, sizeof(Destination));
        Loopback->sin_family = AF_INET;
        Loopback->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        hr = INetworkCostManager_GetCost(Costs, &Cost, &Destination);
        ok(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "GetCost(127.0.0.1) returned hr %#lx\n", hr);

        Cost = 0xdead;
        hr = INetworkCostManager_GetCost(Costs, &Cost, NULL);
        ok(hr == S_OK, "GetCost(NULL) failed, hr %#lx\n", hr);
        if (DefaultRouteInterface(&Index))
            ok(Cost == ExpectedCost(Index), "GetCost(NULL) is %#lx\n", Cost);
        else
            ok(Cost == NLM_CONNECTION_COST_UNKNOWN, "GetCost(NULL) without a default route is %#lx\n", Cost);

        INetworkCostManager_Release(Costs);
    }
    INetworkListManager_Release(Manager);
    CoUninitialize();
}
