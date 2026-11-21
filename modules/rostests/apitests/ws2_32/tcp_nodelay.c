/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the TCP_NODELAY socket option: the value read back, the sizes taken and the socket types refused
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"

static
void
ExpectNoDelay(
    _In_ SOCKET Socket,
    _In_ int Length,
    _In_ UCHAR Expected)
{
    UCHAR Buffer[8];
    int Result;

    memset(Buffer, 0xcc, sizeof(Buffer));
    Result = getsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, (char *)Buffer, &Length);
    ok(Result == 0, "getsockopt failed with %d\n", WSAGetLastError());
    ok(Length == 1, "length %d\n", Length);
    ok(Buffer[0] == Expected, "got %u, expected %u\n", Buffer[0], Expected);
    ok(Buffer[1] == 0xcc, "byte 1 written: 0x%02x\n", Buffer[1]);
}

static
int
SetNoDelay(
    _In_ SOCKET Socket,
    _In_ DWORD Value,
    _In_ int Length)
{
    if (setsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, (char *)&Value, Length) == 0)
        return 0;
    return WSAGetLastError();
}

START_TEST(tcp_nodelay)
{
    WSADATA WsaData;
    SOCKET Tcp, Udp;
    UCHAR Buffer[4];
    int Length, Result;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");

    Tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Tcp != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    if (Tcp != INVALID_SOCKET)
    {
        ExpectNoDelay(Tcp, 4, 0);
        ExpectNoDelay(Tcp, 1, 0);
        ExpectNoDelay(Tcp, 8, 0);

        Length = 0;
        Result = getsockopt(Tcp, IPPROTO_TCP, TCP_NODELAY, (char *)Buffer, &Length);
        ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEFAULT, "0-byte get: %d, error %d\n", Result, WSAGetLastError());

        ok(SetNoDelay(Tcp, 1, sizeof(DWORD)) == 0, "set 1 failed\n");
        ExpectNoDelay(Tcp, 4, 1);
        ok(SetNoDelay(Tcp, 0, sizeof(DWORD)) == 0, "set 0 failed\n");
        ExpectNoDelay(Tcp, 4, 0);
        ok(SetNoDelay(Tcp, 5, sizeof(DWORD)) == 0, "set 5 failed\n");
        ExpectNoDelay(Tcp, 4, 1);
        ok(SetNoDelay(Tcp, 0x100, sizeof(DWORD)) == 0, "set 0x100 failed\n");
        ExpectNoDelay(Tcp, 4, 0);
        ok(SetNoDelay(Tcp, 1, 1) == 0, "1-byte set failed\n");
        ExpectNoDelay(Tcp, 4, 1);
        ok(SetNoDelay(Tcp, 0, 0) == WSAEFAULT, "0-byte set did not fail with WSAEFAULT\n");
        ExpectNoDelay(Tcp, 4, 1);

        closesocket(Tcp);
    }

    Udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Udp != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    if (Udp != INVALID_SOCKET)
    {
        ok(SetNoDelay(Udp, 1, sizeof(DWORD)) == WSAEINVAL, "UDP set did not fail with WSAEINVAL\n");
        Length = sizeof(Buffer);
        Result = getsockopt(Udp, IPPROTO_TCP, TCP_NODELAY, (char *)Buffer, &Length);
        ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEINVAL, "UDP get: %d, error %d\n", Result, WSAGetLastError());
        closesocket(Udp);
    }

    WSACleanup();
}
