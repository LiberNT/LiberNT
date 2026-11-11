/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a TCP connect to the unspecified address fails with WSAEADDRNOTAVAIL
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "ws2_32.h"

#define TEST_PORT 1234
#define SELECT_TIMEOUT_SECONDS 5

static void
TestConnectAny(
    _In_ const struct sockaddr *Address,
    _In_ int AddressLength,
    _In_ BOOL NonBlocking,
    _In_ PCSTR Name)
{
    struct timeval Timeout;
    fd_set WriteSet, ExceptSet;
    u_long Mode = 1;
    SOCKET Socket;
    int Result, Error, SocketError, Length;

    Socket = socket(Address->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (Socket == INVALID_SOCKET)
    {
        skip("%s: socket failed with %d\n", Name, WSAGetLastError());
        return;
    }

    if (NonBlocking)
    {
        Result = ioctlsocket(Socket, FIONBIO, &Mode);
        ok(Result == 0, "%s: ioctlsocket returned %d, error %d\n", Name, Result, WSAGetLastError());
    }

    WSASetLastError(0xdeadbeef);
    Result = connect(Socket, Address, AddressLength);
    Error = WSAGetLastError();
    ok(Result == SOCKET_ERROR, "%s: connect returned %d\n", Name, Result);

    if (NonBlocking && Error == WSAEWOULDBLOCK)
    {
        FD_ZERO(&WriteSet);
        FD_SET(Socket, &WriteSet);
        FD_ZERO(&ExceptSet);
        FD_SET(Socket, &ExceptSet);
        Timeout.tv_sec = SELECT_TIMEOUT_SECONDS;
        Timeout.tv_usec = 0;
        Result = select(0, NULL, &WriteSet, &ExceptSet, &Timeout);
        ok(Result == 1 && FD_ISSET(Socket, &ExceptSet), "%s: select returned %d, except %d\n",
           Name, Result, FD_ISSET(Socket, &ExceptSet));

        SocketError = 0xdeadbeef;
        Length = sizeof(SocketError);
        Result = getsockopt(Socket, SOL_SOCKET, SO_ERROR, (char *)&SocketError, &Length);
        ok(Result == 0, "%s: getsockopt returned %d, error %d\n", Name, Result, WSAGetLastError());
        Error = SocketError;
    }

    ok(Error == WSAEADDRNOTAVAIL, "%s: error %d\n", Name, Error);

    closesocket(Socket);
}

START_TEST(connect_any)
{
    struct sockaddr_in6 Any6;
    struct sockaddr_in Any;
    WSADATA WsaData;
    int Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup returned %d\n", Result);
    if (Result != 0)
        return;

    ZeroMemory(&Any, sizeof(Any));
    Any.sin_family = AF_INET;
    Any.sin_port = htons(TEST_PORT);
    Any.sin_addr.s_addr = htonl(INADDR_ANY);

    ZeroMemory(&Any6, sizeof(Any6));
    Any6.sin6_family = AF_INET6;
    Any6.sin6_port = htons(TEST_PORT);

    TestConnectAny((struct sockaddr *)&Any, sizeof(Any), TRUE, "INADDR_ANY nonblocking");
    TestConnectAny((struct sockaddr *)&Any, sizeof(Any), FALSE, "INADDR_ANY blocking");
    TestConnectAny((struct sockaddr *)&Any6, sizeof(Any6), TRUE, "in6addr_any nonblocking");
    TestConnectAny((struct sockaddr *)&Any6, sizeof(Any6), FALSE, "in6addr_any blocking");

    WSACleanup();
}
