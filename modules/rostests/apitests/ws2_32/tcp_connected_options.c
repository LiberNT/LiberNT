/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test TCP options on sockets connected through ConnectEx, accept and AcceptEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"
#include <mswsock.h>

#define WAIT_TIMEOUT_MS 2000

static
void
CheckOptions(
    _In_ SOCKET Socket,
    _In_ PCSTR Kind)
{
    DWORD One = 1, Time = 30;
    UCHAR NoDelay = 0xcc;
    int Length = sizeof(NoDelay);
    int Result;

    Result = setsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, (char *)&One, sizeof(One));
    ok(Result == 0, "%s: TCP_NODELAY set failed with %d\n", Kind, WSAGetLastError());
    Result = getsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, (char *)&NoDelay, &Length);
    ok(Result == 0, "%s: TCP_NODELAY get failed with %d\n", Kind, WSAGetLastError());
    ok(NoDelay == 1, "%s: TCP_NODELAY reads %u\n", Kind, NoDelay);
    Result = setsockopt(Socket, IPPROTO_TCP, TCP_KEEPALIVE, (char *)&Time, sizeof(Time));
    ok(Result == 0, "%s: TCP_KEEPALIVE set failed with %d\n", Kind, WSAGetLastError());
    Result = setsockopt(Socket, SOL_SOCKET, SO_KEEPALIVE, (char *)&One, sizeof(One));
    ok(Result == 0, "%s: SO_KEEPALIVE set failed with %d\n", Kind, WSAGetLastError());
}

static
BOOL
Listen(
    _Out_ SOCKET *Listener,
    _Out_ struct sockaddr_in *Address)
{
    int Length = sizeof(*Address);

    *Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(*Listener != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    if (*Listener == INVALID_SOCKET)
        return FALSE;

    ZeroMemory(Address, sizeof(*Address));
    Address->sin_family = AF_INET;
    Address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(*Listener, (struct sockaddr *)Address, sizeof(*Address)) == 0, "bind failed with %d\n", WSAGetLastError());
    ok(listen(*Listener, 4) == 0, "listen failed with %d\n", WSAGetLastError());
    ok(getsockname(*Listener, (struct sockaddr *)Address, &Length) == 0, "getsockname failed with %d\n", WSAGetLastError());
    return TRUE;
}

static
void
TestConnectExAndAccept(void)
{
    GUID ConnectExId = WSAID_CONNECTEX;
    LPFN_CONNECTEX pConnectEx = NULL;
    struct sockaddr_in Address, Any = { 0 };
    OVERLAPPED Overlapped = { 0 };
    SOCKET Listener, Client, Server;
    DWORD Bytes = 0, Flags = 0;
    BOOL Done;

    if (!Listen(&Listener, &Address))
        return;

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Client != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    ok(WSAIoctl(Client, SIO_GET_EXTENSION_FUNCTION_POINTER, &ConnectExId, sizeof(ConnectExId),
                &pConnectEx, sizeof(pConnectEx), &Bytes, NULL, NULL) == 0,
       "SIO_GET_EXTENSION_FUNCTION_POINTER failed with %d\n", WSAGetLastError());
    Any.sin_family = AF_INET;
    ok(bind(Client, (struct sockaddr *)&Any, sizeof(Any)) == 0, "bind failed with %d\n", WSAGetLastError());

    Overlapped.hEvent = WSACreateEvent();
    Done = pConnectEx != NULL &&
           pConnectEx(Client, (struct sockaddr *)&Address, sizeof(Address), NULL, 0, &Bytes, &Overlapped);
    if (!Done && WSAGetLastError() == ERROR_IO_PENDING)
    {
        ok(WaitForSingleObject(Overlapped.hEvent, WAIT_TIMEOUT_MS) == WAIT_OBJECT_0, "ConnectEx did not complete\n");
        Done = WSAGetOverlappedResult(Client, &Overlapped, &Bytes, FALSE, &Flags);
    }
    ok(Done, "ConnectEx failed with %d\n", WSAGetLastError());
    ok(setsockopt(Client, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == 0,
       "SO_UPDATE_CONNECT_CONTEXT failed with %d\n", WSAGetLastError());
    CheckOptions(Client, "ConnectEx");

    Server = accept(Listener, NULL, NULL);
    ok(Server != INVALID_SOCKET, "accept failed with %d\n", WSAGetLastError());
    if (Server != INVALID_SOCKET)
    {
        CheckOptions(Server, "accept");
        closesocket(Server);
    }

    WSACloseEvent(Overlapped.hEvent);
    closesocket(Client);
    closesocket(Listener);
}

static
void
TestAcceptEx(void)
{
    GUID AcceptExId = WSAID_ACCEPTEX;
    LPFN_ACCEPTEX pAcceptEx = NULL;
    struct sockaddr_in Address;
    OVERLAPPED Overlapped = { 0 };
    SOCKET Listener, Accepted, Client;
    char Buffer[2 * (sizeof(struct sockaddr_in) + 16)];
    DWORD Bytes = 0, Flags = 0;
    BOOL Done;

    if (!Listen(&Listener, &Address))
        return;

    ok(WSAIoctl(Listener, SIO_GET_EXTENSION_FUNCTION_POINTER, &AcceptExId, sizeof(AcceptExId),
                &pAcceptEx, sizeof(pAcceptEx), &Bytes, NULL, NULL) == 0,
       "SIO_GET_EXTENSION_FUNCTION_POINTER failed with %d\n", WSAGetLastError());

    Accepted = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Overlapped.hEvent = WSACreateEvent();
    Done = pAcceptEx != NULL &&
           pAcceptEx(Listener, Accepted, Buffer, 0, sizeof(struct sockaddr_in) + 16, sizeof(struct sockaddr_in) + 16,
                     &Bytes, &Overlapped);
    ok(Done || WSAGetLastError() == ERROR_IO_PENDING, "AcceptEx failed with %d\n", WSAGetLastError());

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(connect(Client, (struct sockaddr *)&Address, sizeof(Address)) == 0, "connect failed with %d\n", WSAGetLastError());
    if (!Done)
    {
        ok(WaitForSingleObject(Overlapped.hEvent, WAIT_TIMEOUT_MS) == WAIT_OBJECT_0, "AcceptEx did not complete\n");
        Done = WSAGetOverlappedResult(Listener, &Overlapped, &Bytes, FALSE, &Flags);
        ok(Done, "AcceptEx failed with %d\n", WSAGetLastError());
    }
    ok(setsockopt(Accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&Listener, sizeof(Listener)) == 0,
       "SO_UPDATE_ACCEPT_CONTEXT failed with %d\n", WSAGetLastError());
    CheckOptions(Accepted, "AcceptEx");

    WSACloseEvent(Overlapped.hEvent);
    closesocket(Client);
    closesocket(Accepted);
    closesocket(Listener);
}

START_TEST(tcp_connected_options)
{
    WSADATA WsaData;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");

    TestConnectExAndAccept();
    TestAcceptEx();

    WSACleanup();
}
