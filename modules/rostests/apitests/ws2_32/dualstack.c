/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests IPv4 datagram delivery to IPv6-only and dual-stack UDP sockets
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"

static SOCKET
BindIPv6(
    _In_ BOOL V6Only,
    _Out_ USHORT *Port)
{
    SOCKADDR_IN6 Address;
    DWORD Value = V6Only;
    int Length = sizeof(Address);
    SOCKET Socket;

    Socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (Socket == INVALID_SOCKET)
        return INVALID_SOCKET;

    ok(setsockopt(Socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&Value, sizeof(Value)) == 0,
       "IPV6_V6ONLY %lu failed, error %d\n", Value, WSAGetLastError());
    ZeroMemory(&Address, sizeof(Address));
    Address.sin6_family = AF_INET6;
    ok(bind(Socket, (SOCKADDR *)&Address, sizeof(Address)) == 0, "bind failed, error %d\n", WSAGetLastError());
    ok(getsockname(Socket, (SOCKADDR *)&Address, &Length) == 0, "getsockname failed, error %d\n", WSAGetLastError());
    *Port = Address.sin6_port;
    return Socket;
}

static void
SendIPv4(
    _In_ USHORT Port)
{
    SOCKADDR_IN Address;
    SOCKET Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    ok(Socket != INVALID_SOCKET, "IPv4 socket failed, error %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
        return;
    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_port = Port;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(sendto(Socket, "x", 1, 0, (SOCKADDR *)&Address, sizeof(Address)) == 1, "sendto failed, error %d\n", WSAGetLastError());
    closesocket(Socket);
}

static BOOL
Readable(
    _In_ SOCKET Socket,
    _In_ LONG Milliseconds)
{
    struct timeval Timeout = { Milliseconds / 1000, (Milliseconds % 1000) * 1000 };
    fd_set ReadSet;

    FD_ZERO(&ReadSet);
    FD_SET(Socket, &ReadSet);
    return select(0, &ReadSet, NULL, NULL, &Timeout) == 1;
}

START_TEST(dualstack)
{
    static const UCHAR Mapped[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 127, 0, 0, 1 };
    SOCKADDR_IN6 From;
    int FromLength = sizeof(From);
    WSADATA WsaData;
    SOCKET Socket;
    USHORT Port;
    char Data;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");

    Socket = BindIPv6(TRUE, &Port);
    if (Socket == INVALID_SOCKET)
    {
        skip("No IPv6 socket, error %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }
    SendIPv4(Port);
    ok(!Readable(Socket, 500), "IPv6-only socket received an IPv4 datagram\n");
    closesocket(Socket);

    Socket = BindIPv6(FALSE, &Port);
    ok(Socket != INVALID_SOCKET, "Dual-stack socket failed, error %d\n", WSAGetLastError());
    if (Socket != INVALID_SOCKET)
    {
        SendIPv4(Port);
        if (Readable(Socket, 2000))
        {
            ZeroMemory(&From, sizeof(From));
            ok(recvfrom(Socket, &Data, 1, 0, (SOCKADDR *)&From, &FromLength) == 1,
               "recvfrom failed, error %d\n", WSAGetLastError());
            ok(From.sin6_family == AF_INET6, "Source family is %u\n", From.sin6_family);
            ok(!memcmp(&From.sin6_addr, Mapped, sizeof(Mapped)), "Source is not ::ffff:127.0.0.1\n");
        }
        else
        {
            ok(FALSE, "Dual-stack socket did not receive the IPv4 datagram\n");
        }
        closesocket(Socket);
    }

    WSACleanup();
}
