/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test connect on a nonblocking socket that is connecting or connected
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "ws2_32.h"

static SOCKET
NonBlockingSocket(VOID)
{
    SOCKET Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    u_long On = 1;

    ok(Socket != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    if (Socket != INVALID_SOCKET)
        ok(!ioctlsocket(Socket, FIONBIO, &On), "ioctlsocket failed, error %d\n", WSAGetLastError());
    return Socket;
}

static BOOL
InProgress(
    _In_ SOCKET Socket)
{
    struct timeval Zero = {0};
    fd_set Write, Except;

    FD_ZERO(&Write);
    FD_ZERO(&Except);
    FD_SET(Socket, &Write);
    FD_SET(Socket, &Except);
    return select(0, NULL, &Write, &Except, &Zero) == 0;
}

static VOID
TestConnecting(VOID)
{
    struct sockaddr_in Address = {0};
    SOCKET Socket;
    ULONG Checked = 0, i;
    int Result;

    /* TEST-NET-1 (RFC 5737): nothing answers the SYN, so the attempt stays in progress */
    Address.sin_family = AF_INET;
    Address.sin_port = htons(9);
    Address.sin_addr.s_addr = htonl(0xC0000201);
    Socket = NonBlockingSocket();
    if (Socket == INVALID_SOCKET)
        return;
    Result = connect(Socket, (struct sockaddr *)&Address, sizeof(Address));
    if (Result != SOCKET_ERROR || WSAGetLastError() != WSAEWOULDBLOCK)
    {
        skip("connect returned %d, error %d\n", Result, WSAGetLastError());
        closesocket(Socket);
        return;
    }
    for (i = 0; i < 20 && InProgress(Socket); i++)
    {
        Result = connect(Socket, (struct sockaddr *)&Address, sizeof(Address));
        ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEALREADY,
           "connect %lu while connecting returned %d, error %d\n", i, Result, WSAGetLastError());
        Checked++;
        Sleep(10);
    }
    if (!Checked)
        skip("the connection attempt ended at once\n");
    closesocket(Socket);
}

static VOID
TestConnected(VOID)
{
    struct sockaddr_in Address = {0};
    int Length = sizeof(Address), Result, Error;
    SOCKET Listener, Socket;
    ULONG i;

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    if (Listener == INVALID_SOCKET)
        return;
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(!bind(Listener, (struct sockaddr *)&Address, sizeof(Address)), "bind failed, error %d\n", WSAGetLastError());
    ok(!getsockname(Listener, (struct sockaddr *)&Address, &Length), "getsockname failed, error %d\n", WSAGetLastError());
    ok(!listen(Listener, 1), "listen failed, error %d\n", WSAGetLastError());

    Socket = NonBlockingSocket();
    if (Socket != INVALID_SOCKET)
    {
        Result = connect(Socket, (struct sockaddr *)&Address, sizeof(Address));
        ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK,
           "connect returned %d, error %d\n", Result, WSAGetLastError());
        for (i = 0; i < 500; i++)
        {
            Result = connect(Socket, (struct sockaddr *)&Address, sizeof(Address));
            Error = WSAGetLastError();
            if (Result != SOCKET_ERROR || Error != WSAEALREADY)
                break;
            Sleep(10);
        }
        ok(Result == SOCKET_ERROR && Error == WSAEISCONN,
           "connect when connected returned %d, error %d\n", Result, Error);
        closesocket(Socket);
    }
    closesocket(Listener);
}

START_TEST(connect_already)
{
    WSADATA Data;

    ok(!WSAStartup(MAKEWORD(2, 2), &Data), "WSAStartup failed\n");
    TestConnecting();
    TestConnected();
    WSACleanup();
}
