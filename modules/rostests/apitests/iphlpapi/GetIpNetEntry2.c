/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests GetIpNetEntry2 and ResolveIpNetEntry2
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

typedef DWORD (WINAPI *PFN_GETIPNETENTRY2)(PMIB_IPNET_ROW2);
typedef DWORD (WINAPI *PFN_RESOLVEIPNETENTRY2)(PMIB_IPNET_ROW2, const SOCKADDR_INET *);
typedef DWORD (WINAPI *PFN_GETIPNETTABLE2)(ADDRESS_FAMILY, PMIB_IPNET_TABLE2 *);
typedef VOID (WINAPI *PFN_FREEMIBTABLE)(PVOID);

static VOID
SetTestNetAddress(PMIB_IPNET_ROW2 Row)
{
    Row->Address.si_family = AF_INET;
    Row->Address.Ipv4.sin_family = AF_INET;
    Row->Address.Ipv4.sin_addr.S_un.S_un_b.s_b1 = 192;
    Row->Address.Ipv4.sin_addr.S_un.S_un_b.s_b2 = 0;
    Row->Address.Ipv4.sin_addr.S_un.S_un_b.s_b3 = 2;
    Row->Address.Ipv4.sin_addr.S_un.S_un_b.s_b4 = 1;
}

START_TEST(GetIpNetEntry2)
{
    PFN_GETIPNETENTRY2 pGetIpNetEntry2;
    PFN_RESOLVEIPNETENTRY2 pResolveIpNetEntry2;
    PFN_GETIPNETTABLE2 pGetIpNetTable2;
    PFN_FREEMIBTABLE pFreeMibTable;
    PMIB_IPNET_TABLE2 Table = NULL;
    MIB_IPNET_ROW2 Row;
    HMODULE Iphlpapi;
    DWORD Error;

    Iphlpapi = LoadLibraryW(L"iphlpapi.dll");
    ok(Iphlpapi != NULL, "iphlpapi.dll failed to load, error %lu\n", GetLastError());
    if (!Iphlpapi)
        return;

    pGetIpNetEntry2 = (PFN_GETIPNETENTRY2)GetProcAddress(Iphlpapi, "GetIpNetEntry2");
    pResolveIpNetEntry2 = (PFN_RESOLVEIPNETENTRY2)GetProcAddress(Iphlpapi, "ResolveIpNetEntry2");
    pGetIpNetTable2 = (PFN_GETIPNETTABLE2)GetProcAddress(Iphlpapi, "GetIpNetTable2");
    pFreeMibTable = (PFN_FREEMIBTABLE)GetProcAddress(Iphlpapi, "FreeMibTable");
    ok(pGetIpNetEntry2 != NULL, "GetIpNetEntry2 is missing\n");
    ok(pResolveIpNetEntry2 != NULL, "ResolveIpNetEntry2 is missing\n");
    if (!pGetIpNetEntry2 || !pResolveIpNetEntry2 || !pGetIpNetTable2 || !pFreeMibTable)
        return;

    ZeroMemory(&Row, sizeof(Row));
    Error = pGetIpNetEntry2(&Row);
    ok(Error == ERROR_INVALID_PARAMETER, "empty row: error %lu\n", Error);

    ZeroMemory(&Row, sizeof(Row));
    SetTestNetAddress(&Row);
    Error = pGetIpNetEntry2(&Row);
    ok(Error == ERROR_NOT_FOUND, "no interface: error %lu\n", Error);

    ZeroMemory(&Row, sizeof(Row));
    SetTestNetAddress(&Row);
    Row.InterfaceIndex = 0x7ffffff0;
    Error = pGetIpNetEntry2(&Row);
    ok(Error == ERROR_FILE_NOT_FOUND, "unknown interface: error %lu\n", Error);

    ZeroMemory(&Row, sizeof(Row));
    Error = pResolveIpNetEntry2(&Row, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "resolve empty row: error %lu\n", Error);

    Error = pGetIpNetTable2(AF_INET, &Table);
    ok(Error == ERROR_SUCCESS, "GetIpNetTable2: error %lu\n", Error);
    if (Error != ERROR_SUCCESS || !Table || !Table->NumEntries)
    {
        skip("No IPv4 neighbor entry\n");
        if (Table)
            pFreeMibTable(Table);
        return;
    }

    ZeroMemory(&Row, sizeof(Row));
    Row.InterfaceLuid = Table->Table[0].InterfaceLuid;
    Row.Address = Table->Table[0].Address;
    Error = pGetIpNetEntry2(&Row);
    ok(Error == ERROR_SUCCESS, "by LUID: error %lu\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        ok(Row.InterfaceIndex == Table->Table[0].InterfaceIndex, "by LUID: index %lu\n", Row.InterfaceIndex);
        ok(Row.PhysicalAddressLength == Table->Table[0].PhysicalAddressLength, "by LUID: length %lu\n", Row.PhysicalAddressLength);
        ok(!memcmp(Row.PhysicalAddress, Table->Table[0].PhysicalAddress, Row.PhysicalAddressLength), "by LUID: physical address differs\n");
    }

    ZeroMemory(&Row, sizeof(Row));
    Row.InterfaceIndex = Table->Table[0].InterfaceIndex;
    Row.Address = Table->Table[0].Address;
    Error = pGetIpNetEntry2(&Row);
    ok(Error == ERROR_SUCCESS, "by index: error %lu\n", Error);
    if (Error == ERROR_SUCCESS)
        ok(Row.InterfaceLuid.Value == Table->Table[0].InterfaceLuid.Value, "by index: LUID differs\n");

    ZeroMemory(&Row, sizeof(Row));
    Row.InterfaceIndex = Table->Table[0].InterfaceIndex;
    SetTestNetAddress(&Row);
    Error = pGetIpNetEntry2(&Row);
    ok(Error == ERROR_NOT_FOUND, "missing neighbor: error %lu\n", Error);

    pFreeMibTable(Table);
}
