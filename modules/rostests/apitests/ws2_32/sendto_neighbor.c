/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a datagram send does not wait for its next hop to resolve
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"
#include <iphlpapi.h>
#include <netioapi.h>

static
BOOL
FindOnLinkNeighbor(
    _Out_ IN_ADDR *Neighbor)
{
    PMIB_UNICASTIPADDRESS_TABLE Table = NULL;
    ULONG Index, Address, Mask, Host;
    BOOL Found = FALSE;

    if (GetUnicastIpAddressTable(AF_INET, &Table) != NO_ERROR)
        return FALSE;

    for (Index = 0; Index < Table->NumEntries && !Found; Index++)
    {
        const MIB_UNICASTIPADDRESS_ROW *Row = &Table->Table[Index];

        Address = ntohl(Row->Address.Ipv4.sin_addr.s_addr);
        if (Row->DadState != IpDadStatePreferred || (Address >> 24) == 127 ||
            Row->OnLinkPrefixLength == 0 || Row->OnLinkPrefixLength > 29)
        {
            continue;
        }

        Mask = 0xFFFFFFFF << (32 - Row->OnLinkPrefixLength);
        Host = (Address & Mask) | (~Mask - 1);
        if (Host == Address)
            Host--;
        Neighbor->s_addr = htonl(Host);
        Found = TRUE;
    }

    FreeMibTable(Table);
    return Found;
}

START_TEST(sendto_neighbor)
{
    struct sockaddr_in To = { 0 };
    LARGE_INTEGER Frequency, Start, End;
    char Data[32] = { 0 };
    WSADATA WsaData;
    LONGLONG Elapsed;
    SOCKET Socket;
    int Result, Index;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %d\n", Result);
    if (Result != 0)
        return;

    if (!FindOnLinkNeighbor(&To.sin_addr))
    {
        skip("No preferred IPv4 address with an on-link prefix\n");
        WSACleanup();
        return;
    }
    To.sin_family = AF_INET;
    To.sin_port = htons(9);

    Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Socket != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
    {
        WSACleanup();
        return;
    }

    QueryPerformanceFrequency(&Frequency);
    for (Index = 0; Index < 2; Index++)
    {
        QueryPerformanceCounter(&Start);
        Result = sendto(Socket, Data, sizeof(Data), 0, (const struct sockaddr *)&To, sizeof(To));
        QueryPerformanceCounter(&End);
        Elapsed = (End.QuadPart - Start.QuadPart) * 1000 / Frequency.QuadPart;
        ok(Result == sizeof(Data), "sendto %d returned %d with error %d\n", Index, Result, WSAGetLastError());
        ok(Elapsed < 500, "sendto %d to %s waited %I64d ms for the neighbor to resolve\n",
           Index, inet_ntoa(To.sin_addr), Elapsed);
    }

    closesocket(Socket);
    WSACleanup();
}
