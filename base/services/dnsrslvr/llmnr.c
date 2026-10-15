/*
 * PROJECT:     LiberNT DNS Resolver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Link-Local Multicast Name Resolution responder
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

#define NDEBUG
#include <debug.h>

#define LLMNR_PORT 5355
#define LLMNR_GROUP4 0xe00000fc
#define LLMNR_TTL 30
#define LLMNR_MAX_JOINS 16

static const IN6_ADDR LlmnrGroup6 = {{{ 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0x03 }}};

static HANDLE LlmnrStopEvent;
static HANDLE LlmnrThread;

typedef struct _LLMNR_STATE
{
    SOCKET Socket4;
    SOCKET Socket6;
    ULONG Joined4[LLMNR_MAX_JOINS];
    ULONG Joined4Count;
    ULONG Joined6[LLMNR_MAX_JOINS];
    ULONG Joined6Count;
} LLMNR_STATE, *PLLMNR_STATE;

static BOOL LlmnrRemember(ULONG *List, ULONG *Count, ULONG Value)
{
    ULONG i;

    for (i = 0; i < *Count; i++)
    {
        if (List[i] == Value)
            return FALSE;
    }
    return *Count < LLMNR_MAX_JOINS;
}

static VOID LlmnrJoinInterfaces(PLLMNR_STATE State)
{
    IP_ADAPTER_ADDRESSES *Addresses, *Adapter;
    IP_ADAPTER_UNICAST_ADDRESS *Unicast;
    ULONG Size = 0, Flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    struct ip_mreq Request4;
    struct ipv6_mreq Request6;

    if (GetAdaptersAddresses(AF_UNSPEC, Flags, NULL, NULL, &Size) != ERROR_BUFFER_OVERFLOW)
    {
        DPRINT1("LLMNRDIAG gaa size fail\n");
        return;
    }
    DPRINT1("LLMNRDIAG scan\n");
    Addresses = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Addresses)
        return;
    if (GetAdaptersAddresses(AF_UNSPEC, Flags, NULL, Addresses, &Size) == ERROR_SUCCESS)
    {
        for (Adapter = Addresses; Adapter; Adapter = Adapter->Next)
        {
            BOOL HasIpv6 = FALSE;

            if (Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || Adapter->OperStatus != IfOperStatusUp)
                continue;
            for (Unicast = Adapter->FirstUnicastAddress; Unicast; Unicast = Unicast->Next)
            {
                ULONG Address;

                if (Unicast->Address.lpSockaddr->sa_family == AF_INET6)
                {
                    HasIpv6 = TRUE;
                    continue;
                }
                if (Unicast->Address.lpSockaddr->sa_family != AF_INET || State->Socket4 == INVALID_SOCKET)
                    continue;
                Address = ((SOCKADDR_IN *)Unicast->Address.lpSockaddr)->sin_addr.s_addr;
                if (!LlmnrRemember(State->Joined4, &State->Joined4Count, Address))
                    continue;
                Request4.imr_multiaddr.s_addr = htonl(LLMNR_GROUP4);
                Request4.imr_interface.s_addr = Address;
                {
                    int r = setsockopt(State->Socket4, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&Request4, sizeof(Request4));
                    DPRINT1("LLMNRDIAG join4 %08lx -> %d err %d\n", Address, r, r ? WSAGetLastError() : 0);
                    if (!r)
                        State->Joined4[State->Joined4Count++] = Address;
                }
            }
            if (!HasIpv6 || !Adapter->Ipv6IfIndex || State->Socket6 == INVALID_SOCKET ||
                !LlmnrRemember(State->Joined6, &State->Joined6Count, Adapter->Ipv6IfIndex))
                continue;
            Request6.ipv6mr_multiaddr = LlmnrGroup6;
            Request6.ipv6mr_interface = Adapter->Ipv6IfIndex;
            if (!setsockopt(State->Socket6, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (const char *)&Request6, sizeof(Request6)))
                State->Joined6[State->Joined6Count++] = Adapter->Ipv6IfIndex;
        }
    }
    HeapFree(GetProcessHeap(), 0, Addresses);
}

static BOOL LlmnrNameMatches(const UCHAR *Packet, int Length, int *Offset, const char *Host)
{
    int Position = *Offset, HostLength = (int)strlen(Host);
    UCHAR Label;

    if (Position >= Length)
        return FALSE;
    Label = Packet[Position];
    if (Label == 0 || Label > 63 || Position + 1 + Label >= Length)
        return FALSE;
    if (Packet[Position + 1 + Label] != 0)
        return FALSE;
    if (Label != HostLength || _strnicmp((const char *)Packet + Position + 1, Host, HostLength))
        return FALSE;
    *Offset = Position + 1 + Label + 1;
    return TRUE;
}

static BOOL LlmnrInterfaceAddress(ULONG Index, ADDRESS_FAMILY Family, BOOL LinkLocal, SOCKADDR_INET *Result)
{
    MIB_UNICASTIPADDRESS_TABLE *Table;
    BOOL Found = FALSE;
    ULONG i;

    if (GetUnicastIpAddressTable(Family, &Table))
        return FALSE;
    for (i = 0; i < Table->NumEntries; i++)
    {
        const SOCKADDR_INET *Address = &Table->Table[i].Address;

        if (Table->Table[i].InterfaceIndex != Index)
            continue;
        if (Family == AF_INET6 && IN6_IS_ADDR_LINKLOCAL(&Address->Ipv6.sin6_addr) != LinkLocal)
            continue;
        *Result = *Address;
        Found = TRUE;
        break;
    }
    FreeMibTable(Table);
    return Found;
}

static VOID LlmnrRespond(SOCKET Socket, const UCHAR *Query, int Length, const SOCKADDR_INET *From, int FromLength,
                         const char *Host)
{
    UCHAR Reply[512];
    int Offset = 12, Size;
    USHORT Type, Class;
    DWORD Index = 0;
    BOOL LinkLocal = FALSE, Answer = FALSE;
    SOCKADDR_INET Address;

    if (Length < 12 || (Query[2] & 0xf8) || Query[4] != 0 || Query[5] != 1 || Query[6] || Query[7])
        return;
    if (!LlmnrNameMatches(Query, Length, &Offset, Host) || Offset + 4 > Length)
        return;
    Type = (Query[Offset] << 8) | Query[Offset + 1];
    Class = (Query[Offset + 2] << 8) | Query[Offset + 3];
    Offset += 4;
    if (Class != 1)
        return;

    if (From->si_family == AF_INET6)
    {
        LinkLocal = IN6_IS_ADDR_LINKLOCAL(&From->Ipv6.sin6_addr);
        if (LinkLocal)
            Index = From->Ipv6.sin6_scope_id;
        else if (GetBestInterfaceEx((struct sockaddr *)&From->Ipv6, &Index))
            return;
    }
    else if (GetBestInterface(From->Ipv4.sin_addr.s_addr, &Index))
    {
        return;
    }

    if (Type == 1)
        Answer = LlmnrInterfaceAddress(Index, AF_INET, FALSE, &Address);
    else if (Type == 28)
        Answer = LlmnrInterfaceAddress(Index, AF_INET6, LinkLocal, &Address);

    memcpy(Reply, Query, Offset);
    Reply[2] = 0x80;
    Reply[3] = 0;
    Reply[6] = 0;
    Reply[7] = Answer ? 1 : 0;
    Reply[8] = Reply[9] = Reply[10] = Reply[11] = 0;
    Size = Offset;
    if (Answer)
    {
        Reply[Size++] = 0xc0;
        Reply[Size++] = 12;
        Reply[Size++] = 0;
        Reply[Size++] = (UCHAR)Type;
        Reply[Size++] = 0;
        Reply[Size++] = 1;
        Reply[Size++] = 0;
        Reply[Size++] = 0;
        Reply[Size++] = 0;
        Reply[Size++] = LLMNR_TTL;
        Reply[Size++] = 0;
        if (Type == 1)
        {
            Reply[Size++] = 4;
            memcpy(Reply + Size, &Address.Ipv4.sin_addr, 4);
            Size += 4;
        }
        else
        {
            Reply[Size++] = 16;
            memcpy(Reply + Size, &Address.Ipv6.sin6_addr, 16);
            Size += 16;
        }
    }
    sendto(Socket, (const char *)Reply, Size, 0, (const struct sockaddr *)From, FromLength);
}

static SOCKET LlmnrOpenSocket(ADDRESS_FAMILY Family, HANDLE Event)
{
    SOCKADDR_INET Address;
    DWORD V6Only = TRUE;
    SOCKET Socket = socket(Family, SOCK_DGRAM, IPPROTO_UDP);

    if (Socket == INVALID_SOCKET)
        return INVALID_SOCKET;
    memset(&Address, 0, sizeof(Address));
    Address.si_family = Family;
    if (Family == AF_INET6)
    {
        Address.Ipv6.sin6_port = htons(LLMNR_PORT);
        setsockopt(Socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&V6Only, sizeof(V6Only));
    }
    else
    {
        Address.Ipv4.sin_port = htons(LLMNR_PORT);
    }
    if (bind(Socket, (struct sockaddr *)&Address, Family == AF_INET6 ? sizeof(Address.Ipv6) : sizeof(Address.Ipv4)) ||
        WSAEventSelect(Socket, Event, FD_READ))
    {
        closesocket(Socket);
        return INVALID_SOCKET;
    }
    return Socket;
}

static VOID LlmnrDrain(SOCKET Socket, const char *Host)
{
    SOCKADDR_INET From;
    UCHAR Packet[512];
    int Length, FromLength;

    if (Socket == INVALID_SOCKET)
        return;
    for (;;)
    {
        FromLength = sizeof(From);
        Length = recvfrom(Socket, (char *)Packet, sizeof(Packet), 0, (struct sockaddr *)&From, &FromLength);
        if (Length == SOCKET_ERROR)
            break;
        LlmnrRespond(Socket, Packet, Length, &From, FromLength, Host);
    }
}

static DWORD WINAPI LlmnrThreadRoutine(LPVOID Parameter)
{
    LLMNR_STATE State;
    WSADATA Data;
    HANDLE Events[3], Change = NULL;
    OVERLAPPED Overlapped;
    char Host[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD HostLength = sizeof(Host), Wait;

    UNREFERENCED_PARAMETER(Parameter);

    if (!GetComputerNameA(Host, &HostLength))
        return 0;
    if (WSAStartup(MAKEWORD(2, 2), &Data))
        return 0;

    memset(&State, 0, sizeof(State));
    State.Socket4 = State.Socket6 = INVALID_SOCKET;
    Events[0] = LlmnrStopEvent;
    Events[1] = WSACreateEvent();
    Events[2] = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (Events[1] == WSA_INVALID_EVENT || !Events[2])
        goto done;
    State.Socket4 = LlmnrOpenSocket(AF_INET, Events[1]);
    State.Socket6 = LlmnrOpenSocket(AF_INET6, Events[1]);
    if (State.Socket4 == INVALID_SOCKET && State.Socket6 == INVALID_SOCKET)
        goto done;

    memset(&Overlapped, 0, sizeof(Overlapped));
    for (;;)
    {
        memset(&Overlapped, 0, sizeof(Overlapped));
        Overlapped.hEvent = Events[2];
        NotifyAddrChange(&Change, &Overlapped);
        LlmnrJoinInterfaces(&State);

        Wait = WaitForMultipleObjects(3, Events, FALSE, INFINITE);
        if (Wait == WAIT_OBJECT_0)
            break;
        if (Wait == WAIT_OBJECT_0 + 2)
            continue;
        CancelIPChangeNotify(&Overlapped);
        WSAResetEvent(Events[1]);
        LlmnrDrain(State.Socket4, Host);
        LlmnrDrain(State.Socket6, Host);
    }
    CancelIPChangeNotify(&Overlapped);

done:
    if (State.Socket4 != INVALID_SOCKET)
        closesocket(State.Socket4);
    if (State.Socket6 != INVALID_SOCKET)
        closesocket(State.Socket6);
    if (Events[1] != WSA_INVALID_EVENT)
        WSACloseEvent(Events[1]);
    if (Events[2])
        CloseHandle(Events[2]);
    WSACleanup();
    return 0;
}

VOID LlmnrStart(VOID)
{
    LlmnrStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!LlmnrStopEvent)
        return;
    LlmnrThread = CreateThread(NULL, 0, LlmnrThreadRoutine, NULL, 0, NULL);
}

VOID LlmnrStop(VOID)
{
    if (!LlmnrStopEvent)
        return;
    SetEvent(LlmnrStopEvent);
    if (LlmnrThread)
    {
        WaitForSingleObject(LlmnrThread, 5000);
        CloseHandle(LlmnrThread);
        LlmnrThread = NULL;
    }
}
