/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for DhcpReleaseParameters and DhcpAcquireParameters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <winnls.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <icmpapi.h>
#include <stdio.h>

typedef DWORD (WINAPI *PFN_DHCP_CAPI_INITIALIZE)(LPDWORD Version);
typedef VOID (WINAPI *PFN_DHCP_CAPI_CLEANUP)(VOID);
typedef DWORD (WINAPI *PFN_DHCP_ADAPTER_REQUEST)(PWSTR AdapterName);

static BOOL
IsLeaseAddress(PCSTR Address)
{
    return strcmp(Address, "0.0.0.0") != 0 && strncmp(Address, "169.254.", 8) != 0;
}

static PIP_ADAPTER_INFO
QueryAdapters(VOID)
{
    PIP_ADAPTER_INFO Info;
    ULONG Size = 0;

    if (GetAdaptersInfo(NULL, &Size) != ERROR_BUFFER_OVERFLOW)
        return NULL;
    Info = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Info && GetAdaptersInfo(Info, &Size) != NO_ERROR)
    {
        HeapFree(GetProcessHeap(), 0, Info);
        Info = NULL;
    }
    return Info;
}

static BOOL
QueryAdapterAddress(DWORD Index, PSTR Address, INT AddressSize)
{
    PIP_ADAPTER_INFO Info, Adapter;
    BOOL Found = FALSE;

    Info = QueryAdapters();
    for (Adapter = Info; Adapter && !Found; Adapter = Adapter->Next)
    {
        if (Adapter->Index != Index)
            continue;
        lstrcpynA(Address, Adapter->IpAddressList.IpAddress.String, AddressSize);
        Found = TRUE;
    }
    if (Info)
        HeapFree(GetProcessHeap(), 0, Info);
    return Found;
}

static BOOL
IsAddressPreferred(DWORD Index, PCSTR Address)
{
    PMIB_UNICASTIPADDRESS_TABLE Table;
    BOOL Preferred = FALSE;
    CHAR Text[16];
    PUCHAR Bytes;
    ULONG Entry;

    if (GetUnicastIpAddressTable(AF_INET, &Table) != NO_ERROR)
        return FALSE;
    for (Entry = 0; Entry < Table->NumEntries; Entry++)
    {
        Bytes = (PUCHAR)&Table->Table[Entry].Address.Ipv4.sin_addr;
        sprintf(Text, "%u.%u.%u.%u", Bytes[0], Bytes[1], Bytes[2], Bytes[3]);
        if (Table->Table[Entry].InterfaceIndex == Index && !strcmp(Text, Address))
            Preferred = Table->Table[Entry].DadState == IpDadStatePreferred;
    }
    FreeMibTable(Table);
    return Preferred;
}

static BOOL
PingGateway(DWORD Index)
{
    PIP_ADAPTER_INFO Info, Adapter;
    UCHAR Reply[sizeof(ICMP_ECHO_REPLY) + 32 + 8];
    CHAR Data[32] = { 0 };
    IPAddr Gateway = INADDR_NONE;
    HANDLE Icmp;
    BOOL Reached = FALSE;

    Info = QueryAdapters();
    for (Adapter = Info; Adapter; Adapter = Adapter->Next)
    {
        if (Adapter->Index == Index)
            Gateway = inet_addr(Adapter->GatewayList.IpAddress.String);
    }
    if (Info)
        HeapFree(GetProcessHeap(), 0, Info);
    if (Gateway == INADDR_NONE || Gateway == 0)
        return FALSE;

    Icmp = IcmpCreateFile();
    if (IcmpSendEcho(Icmp, Gateway, Data, sizeof(Data), NULL, Reply, sizeof(Reply), 1000) != 0)
        Reached = ((PICMP_ECHO_REPLY)Reply)->Status == IP_SUCCESS;
    IcmpCloseHandle(Icmp);
    return Reached;
}

static BOOL
FindLeasedAdapter(PDWORD Index, PWSTR Name, INT NameCount, PSTR Address, INT AddressSize)
{
    PIP_ADAPTER_INFO Info, Adapter;
    BOOL Found = FALSE;

    Info = QueryAdapters();
    for (Adapter = Info; Adapter && !Found; Adapter = Adapter->Next)
    {
        if (!Adapter->DhcpEnabled || !IsLeaseAddress(Adapter->IpAddressList.IpAddress.String))
            continue;
        if (!MultiByteToWideChar(CP_ACP, 0, Adapter->AdapterName, -1, Name, NameCount))
            continue;
        *Index = Adapter->Index;
        lstrcpynA(Address, Adapter->IpAddressList.IpAddress.String, AddressSize);
        Found = TRUE;
    }
    if (Info)
        HeapFree(GetProcessHeap(), 0, Info);
    return Found;
}

START_TEST(DhcpAcquireParameters)
{
    PFN_DHCP_CAPI_INITIALIZE pDhcpCApiInitialize;
    PFN_DHCP_CAPI_CLEANUP pDhcpCApiCleanup;
    PFN_DHCP_ADAPTER_REQUEST pDhcpReleaseParameters;
    PFN_DHCP_ADAPTER_REQUEST pDhcpAcquireParameters;
    WCHAR Name[MAX_ADAPTER_NAME_LENGTH + 4];
    CHAR Address[16];
    ULONGLONG Start, Elapsed;
    HMODULE Module;
    DWORD Index, Version, Status;

    Module = LoadLibraryW(L"dhcpcsvc.dll");
    if (!Module)
    {
        skip("dhcpcsvc.dll failed to load: %lu\n", GetLastError());
        return;
    }
    pDhcpCApiInitialize = (PFN_DHCP_CAPI_INITIALIZE)GetProcAddress(Module, "DhcpCApiInitialize");
    pDhcpCApiCleanup = (PFN_DHCP_CAPI_CLEANUP)GetProcAddress(Module, "DhcpCApiCleanup");
    pDhcpReleaseParameters = (PFN_DHCP_ADAPTER_REQUEST)GetProcAddress(Module, "DhcpReleaseParameters");
    pDhcpAcquireParameters = (PFN_DHCP_ADAPTER_REQUEST)GetProcAddress(Module, "DhcpAcquireParameters");
    if (!pDhcpCApiInitialize || !pDhcpCApiCleanup || !pDhcpReleaseParameters || !pDhcpAcquireParameters)
    {
        skip("dhcpcsvc.dll lacks the DHCP client exports\n");
        FreeLibrary(Module);
        return;
    }
    if (!FindLeasedAdapter(&Index, Name, ARRAYSIZE(Name), Address, sizeof(Address)))
    {
        skip("No adapter holds a DHCP lease\n");
        FreeLibrary(Module);
        return;
    }
    trace("Adapter %lu %ls holds %s\n", Index, Name, Address);

    Status = pDhcpCApiInitialize(&Version);
    ok(Status == ERROR_SUCCESS, "DhcpCApiInitialize returned %lu\n", Status);

    Status = pDhcpReleaseParameters(Name);
    ok(Status == ERROR_SUCCESS, "DhcpReleaseParameters returned %lu\n", Status);
    ok(QueryAdapterAddress(Index, Address, sizeof(Address)), "Adapter %lu is gone after release\n", Index);
    ok(strcmp(Address, "0.0.0.0") == 0, "Address after release is %s\n", Address);

    Sleep(3000);
    ok(QueryAdapterAddress(Index, Address, sizeof(Address)), "Adapter %lu is gone 3 s after release\n", Index);
    ok(strcmp(Address, "0.0.0.0") == 0, "Address 3 s after release is %s\n", Address);

    trace("Acquiring a lease for adapter %lu\n", Index);
    Start = GetTickCount64();
    Status = pDhcpAcquireParameters(Name);
    ok(Status == ERROR_SUCCESS, "DhcpAcquireParameters returned %lu\n", Status);
    ok(QueryAdapterAddress(Index, Address, sizeof(Address)), "Adapter %lu is gone after acquire\n", Index);
    ok(IsLeaseAddress(Address), "Address when DhcpAcquireParameters returned after %lu ms is %s\n",
       (ULONG)(GetTickCount64() - Start), Address);
    Elapsed = GetTickCount64() - Start;
    ok(IsAddressPreferred(Index, Address), "Address %s is not preferred when DhcpAcquireParameters returned\n", Address);
    ok(PingGateway(Index), "The gateway of %s does not answer when DhcpAcquireParameters returned\n", Address);
    ok(Elapsed < 3000, "DhcpAcquireParameters waited %lu ms, past duplicate address detection\n", (ULONG)Elapsed);
    trace("DhcpAcquireParameters took %lu ms, address %s\n", (ULONG)Elapsed, Address);

    pDhcpCApiCleanup();
    FreeLibrary(Module);
}
