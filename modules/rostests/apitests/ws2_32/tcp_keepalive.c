/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the TCP_KEEPALIVE, TCP_KEEPINTVL and TCP_KEEPCNT socket options: defaults, limits, sizes and socket types
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"

#define MAX_KEEPALIVE_SECONDS (0xFFFFFFFFUL / 1000)

static
int
SetOption(
    _In_ SOCKET Socket,
    _In_ int Option,
    _In_ DWORD Value,
    _In_ int Length)
{
    DWORD Buffer[2] = { Value, 0 };

    if (setsockopt(Socket, IPPROTO_TCP, Option, (char *)Buffer, Length) == 0)
        return 0;
    return WSAGetLastError();
}

static
int
GetOption(
    _In_ SOCKET Socket,
    _In_ int Option,
    _Out_ DWORD *Value)
{
    int Length = sizeof(*Value);

    *Value = 0xdeadbeef;
    if (getsockopt(Socket, IPPROTO_TCP, Option, (char *)Value, &Length) != 0)
        return WSAGetLastError();
    ok(Length == sizeof(*Value), "option %d: length %d\n", Option, Length);
    return 0;
}

#define expect_value(Socket, Option, Expected) \
    do { \
        DWORD _Value; \
        int _Error = GetOption(Socket, Option, &_Value); \
        ok(_Error == 0, "option %d: get failed with %d\n", Option, _Error); \
        ok(_Value == (Expected), "option %d: got %lu, expected %lu\n", Option, _Value, (DWORD)(Expected)); \
    } while (0)

#define expect_set(Socket, Option, Value, Length, Error) \
    do { \
        int _Error = SetOption(Socket, Option, Value, Length); \
        ok(_Error == (Error), "option %d = %lu (length %d): got %d, expected %d\n", Option, (DWORD)(Value), Length, _Error, Error); \
    } while (0)

static
void
TestUnconnected(void)
{
    SOCKET Socket;
    DWORD Value = 40;
    char Small = 1;
    int Length = 1;
    int Result;

    Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Socket != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
        return;

    expect_value(Socket, TCP_KEEPALIVE, 7200);
    expect_value(Socket, TCP_KEEPCNT, 10);
    expect_value(Socket, TCP_KEEPINTVL, 1);

    expect_set(Socket, TCP_KEEPALIVE, 30, sizeof(DWORD), 0);
    expect_value(Socket, TCP_KEEPALIVE, 30);
    expect_set(Socket, TCP_KEEPINTVL, 5, sizeof(DWORD), 0);
    expect_value(Socket, TCP_KEEPINTVL, 5);
    expect_set(Socket, TCP_KEEPCNT, 4, sizeof(DWORD), 0);
    expect_value(Socket, TCP_KEEPCNT, 4);

    expect_set(Socket, TCP_KEEPCNT, 255, sizeof(DWORD), 0);
    expect_value(Socket, TCP_KEEPCNT, 255);
    expect_set(Socket, TCP_KEEPCNT, 256, sizeof(DWORD), WSAEINVAL);
    expect_value(Socket, TCP_KEEPCNT, 255);
    expect_set(Socket, TCP_KEEPCNT, 0, sizeof(DWORD), 0);
    expect_value(Socket, TCP_KEEPCNT, 0);

    expect_set(Socket, TCP_KEEPALIVE, 0, sizeof(DWORD), WSAEINVAL);
    expect_value(Socket, TCP_KEEPALIVE, 30);
    expect_set(Socket, TCP_KEEPINTVL, 0, sizeof(DWORD), WSAEINVAL);
    expect_value(Socket, TCP_KEEPINTVL, 5);

    expect_set(Socket, TCP_KEEPALIVE, 0xFFFFFFFF, sizeof(DWORD), 0);
    expect_value(Socket, TCP_KEEPALIVE, MAX_KEEPALIVE_SECONDS);
    expect_set(Socket, TCP_KEEPALIVE, 0x80000000, sizeof(DWORD), 0);
    expect_value(Socket, TCP_KEEPALIVE, MAX_KEEPALIVE_SECONDS);
    expect_set(Socket, TCP_KEEPINTVL, 0xFFFFFFFF, sizeof(DWORD), 0);
    expect_value(Socket, TCP_KEEPINTVL, MAX_KEEPALIVE_SECONDS);

    expect_set(Socket, TCP_KEEPALIVE, 40, 2, WSAEFAULT);
    expect_value(Socket, TCP_KEEPALIVE, MAX_KEEPALIVE_SECONDS);
    expect_set(Socket, TCP_KEEPALIVE, 40, 8, 0);
    expect_value(Socket, TCP_KEEPALIVE, 40);

    Result = setsockopt(Socket, IPPROTO_TCP, TCP_KEEPALIVE, &Small, sizeof(Small));
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEFAULT, "1-byte set: %d, error %d\n", Result, WSAGetLastError());
    Result = getsockopt(Socket, IPPROTO_TCP, TCP_KEEPALIVE, &Small, &Length);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEFAULT, "1-byte get: %d, error %d\n", Result, WSAGetLastError());
    Result = setsockopt(Socket, IPPROTO_TCP, TCP_KEEPALIVE, NULL, sizeof(DWORD));
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEFAULT, "NULL set: %d, error %d\n", Result, WSAGetLastError());

    Value = TRUE;
    Result = setsockopt(Socket, SOL_SOCKET, SO_KEEPALIVE, (char *)&Value, sizeof(Value));
    ok(Result == 0, "SO_KEEPALIVE failed with %d\n", WSAGetLastError());
    expect_value(Socket, TCP_KEEPALIVE, 40);
    expect_value(Socket, TCP_KEEPINTVL, MAX_KEEPALIVE_SECONDS);
    expect_value(Socket, TCP_KEEPCNT, 0);

    closesocket(Socket);
}

static
void
TestDatagram(void)
{
    SOCKET Socket;
    DWORD Value;

    Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Socket != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
        return;

    expect_set(Socket, TCP_KEEPALIVE, 30, sizeof(DWORD), WSAEINVAL);
    expect_set(Socket, TCP_KEEPINTVL, 5, sizeof(DWORD), WSAEINVAL);
    expect_set(Socket, TCP_KEEPCNT, 3, sizeof(DWORD), WSAEINVAL);
    ok(GetOption(Socket, TCP_KEEPALIVE, &Value) == WSAEINVAL, "UDP TCP_KEEPALIVE get did not fail with WSAEINVAL\n");
    ok(GetOption(Socket, TCP_KEEPCNT, &Value) == WSAEINVAL, "UDP TCP_KEEPCNT get did not fail with WSAEINVAL\n");

    closesocket(Socket);
}

static
void
TestConnected(void)
{
    struct sockaddr_in Address = { 0 };
    int AddressLength = sizeof(Address);
    SOCKET Listener, Client, Server;

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET && Client != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    if (Listener == INVALID_SOCKET || Client == INVALID_SOCKET)
        return;

    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed with %d\n", WSAGetLastError());
    ok(listen(Listener, 1) == 0, "listen failed with %d\n", WSAGetLastError());
    ok(getsockname(Listener, (struct sockaddr *)&Address, &AddressLength) == 0, "getsockname failed with %d\n", WSAGetLastError());

    expect_set(Client, TCP_KEEPALIVE, 90, sizeof(DWORD), 0);
    ok(connect(Client, (struct sockaddr *)&Address, sizeof(Address)) == 0, "connect failed with %d\n", WSAGetLastError());
    expect_value(Client, TCP_KEEPALIVE, 90);

    expect_set(Client, TCP_KEEPALIVE, 60, sizeof(DWORD), 0);
    expect_value(Client, TCP_KEEPALIVE, 60);
    expect_set(Client, TCP_KEEPINTVL, 2, sizeof(DWORD), 0);
    expect_value(Client, TCP_KEEPINTVL, 2);
    expect_set(Client, TCP_KEEPCNT, 7, sizeof(DWORD), 0);
    expect_value(Client, TCP_KEEPCNT, 7);

    Server = accept(Listener, NULL, NULL);
    ok(Server != INVALID_SOCKET, "accept failed with %d\n", WSAGetLastError());
    if (Server != INVALID_SOCKET)
    {
        expect_set(Server, TCP_KEEPALIVE, 45, sizeof(DWORD), 0);
        expect_value(Server, TCP_KEEPALIVE, 45);
        closesocket(Server);
    }

    closesocket(Client);
    closesocket(Listener);
}

START_TEST(tcp_keepalive)
{
    WSADATA WsaData;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");

    TestUnconnected();
    TestDatagram();
    TestConnected();

    WSACleanup();
}
