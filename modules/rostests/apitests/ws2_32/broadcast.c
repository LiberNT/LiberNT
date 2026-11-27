/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test UDP broadcast delivery and limited-broadcast source address
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *              Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
 */

#include "ws2_32.h"
#include <iphlpapi.h>

static
BOOLEAN
ReceiveBroadcast(
    _In_ SOCKET RecvSock,
    _In_ ULONG Destination,
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ INT BufferSize,
    _Out_ PULONG SourceAddress)
{
    fd_set Fds;
    struct timeval Timeout = { 2, 0 };
    SOCKADDR_IN From;
    INT FromLen = sizeof(From);
    INT Result;

    *SourceAddress = 0xFFFFFFFF;

    FD_ZERO(&Fds);
    FD_SET(RecvSock, &Fds);
    Result = select(0, &Fds, NULL, NULL, &Timeout);
    ok(Result == 1, "select returned %d for 0x%08lx\n", Result, ntohl(Destination));
    if (Result != 1)
        return FALSE;

    ZeroMemory(&From, sizeof(From));
    Result = recvfrom(RecvSock, Buffer, BufferSize, 0, (SOCKADDR *)&From, &FromLen);
    ok(Result == 4 && !memcmp(Buffer, "ping", 4), "recvfrom returned %d\n", Result);
    if (Result != 4)
        return FALSE;

    *SourceAddress = From.sin_addr.s_addr;
    return TRUE;
}

static
VOID
TestBroadcastLoopback(
    _In_ ULONG Destination,
    _In_ ULONG UnicastInterface,
    _In_ BOOLEAN ExpectUnspecifiedSource)
{
    SOCKET RecvSock;
    SOCKET SendSock;
    SOCKADDR_IN Addr;
    INT AddrLen = sizeof(Addr);
    BOOL Enable = TRUE;
    CHAR Buffer[16];
    ULONG SourceAddress;
    INT Result;

    RecvSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(RecvSock != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    if (RecvSock == INVALID_SOCKET)
        return;

    ZeroMemory(&Addr, sizeof(Addr));
    Addr.sin_family = AF_INET;
    Addr.sin_addr.s_addr = htonl(INADDR_ANY);
    Result = bind(RecvSock, (SOCKADDR *)&Addr, sizeof(Addr));
    ok(Result == 0, "bind failed, error %d\n", WSAGetLastError());

    Result = getsockname(RecvSock, (SOCKADDR *)&Addr, &AddrLen);
    ok(Result == 0, "getsockname failed, error %d\n", WSAGetLastError());

    SendSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(SendSock != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    if (SendSock == INVALID_SOCKET)
    {
        closesocket(RecvSock);
        return;
    }

    Result = setsockopt(SendSock, SOL_SOCKET, SO_BROADCAST, (PCHAR)&Enable, sizeof(Enable));
    ok(Result == 0, "setsockopt(SO_BROADCAST) failed, error %d\n", WSAGetLastError());

    if (UnicastInterface)
    {
        Result = setsockopt(SendSock, IPPROTO_IP, IP_UNICAST_IF, (PCHAR)&UnicastInterface,
                            sizeof(UnicastInterface));
        ok(Result == 0, "setsockopt(IP_UNICAST_IF) failed, error %d\n", WSAGetLastError());
    }

    Addr.sin_addr.s_addr = Destination;
    Result = sendto(SendSock, "ping", 4, 0, (SOCKADDR *)&Addr, sizeof(Addr));
    ok(Result == 4, "sendto returned %d, error %d\n", Result, WSAGetLastError());

    if (ReceiveBroadcast(RecvSock, Destination, Buffer, sizeof(Buffer), &SourceAddress) &&
        ExpectUnspecifiedSource)
    {
        ok(SourceAddress == htonl(INADDR_ANY),
           "limited broadcast source 0x%08lx, expected 0.0.0.0 (unicast-if %lu)\n",
           ntohl(SourceAddress), ntohl(UnicastInterface));
    }

    closesocket(SendSock);
    closesocket(RecvSock);
}

static
ULONG
GetDirectedBroadcast(VOID)
{
    PMIB_IPADDRTABLE Table;
    ULONG TableSize = 0;
    ULONG Broadcast = 0;
    ULONG Index;

    if (GetIpAddrTable(NULL, &TableSize, FALSE) != ERROR_INSUFFICIENT_BUFFER)
        return 0;

    Table = HeapAlloc(GetProcessHeap(), 0, TableSize);
    if (!Table)
        return 0;

    if (GetIpAddrTable(Table, &TableSize, FALSE) == NO_ERROR)
    {
        for (Index = 0; Index < Table->dwNumEntries; Index++)
        {
            if (Table->table[Index].dwAddr == 0 ||
                Table->table[Index].dwAddr == htonl(INADDR_LOOPBACK))
            {
                continue;
            }

            Broadcast = Table->table[Index].dwAddr | ~Table->table[Index].dwMask;
            break;
        }
    }

    HeapFree(GetProcessHeap(), 0, Table);
    return Broadcast;
}

static
ULONG
GetUnicastInterface(VOID)
{
    PMIB_IPADDRTABLE Table;
    ULONG TableSize = 0;
    ULONG Interface = 0;
    ULONG Index;

    if (GetIpAddrTable(NULL, &TableSize, FALSE) != ERROR_INSUFFICIENT_BUFFER)
        return 0;

    Table = HeapAlloc(GetProcessHeap(), 0, TableSize);
    if (!Table)
        return 0;

    if (GetIpAddrTable(Table, &TableSize, FALSE) == NO_ERROR)
    {
        for (Index = 0; Index < Table->dwNumEntries; Index++)
        {
            if (Table->table[Index].dwAddr == 0 ||
                Table->table[Index].dwAddr == htonl(INADDR_LOOPBACK))
            {
                continue;
            }

            Interface = htonl(Table->table[Index].dwIndex);
            break;
        }
    }

    HeapFree(GetProcessHeap(), 0, Table);
    return Interface;
}

START_TEST(broadcast)
{
    WSADATA WsaData;
    ULONG Directed;
    ULONG UnicastInterface;

    if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
    {
        skip("WSAStartup failed\n");
        return;
    }

    TestBroadcastLoopback(htonl(INADDR_BROADCAST), 0, FALSE);

    Directed = GetDirectedBroadcast();
    if (Directed)
        TestBroadcastLoopback(Directed, 0, FALSE);
    else
        skip("No configured adapter for directed broadcast\n");

    UnicastInterface = GetUnicastInterface();
    if (UnicastInterface)
        TestBroadcastLoopback(htonl(INADDR_BROADCAST), UnicastInterface, TRUE);
    else
        skip("No configured adapter for IP_UNICAST_IF limited-broadcast source\n");

    WSACleanup();
}
