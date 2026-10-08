/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests select, event selection and accept on loopback sockets, as a socket client of either bitness uses them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appsmoke.h"

#include <winsock2.h>

static int SelectOne(SOCKET Socket, int Set, DWORD Milliseconds)
{
    fd_set Sockets;
    struct timeval Timeout;
    int Result;

    FD_ZERO(&Sockets);
    FD_SET(Socket, &Sockets);
    Timeout.tv_sec = Milliseconds / 1000;
    Timeout.tv_usec = (Milliseconds % 1000) * 1000;
    Result = select(0, Set == 0 ? &Sockets : NULL, Set == 1 ? &Sockets : NULL, Set == 2 ? &Sockets : NULL, &Timeout);
    if (Result < 0)
        return -1;

    return Result > 0 && FD_ISSET(Socket, &Sockets);
}

START_TEST(sockets)
{
    WSADATA Data;
    SOCKET Listener, Client, Accepted;
    struct sockaddr_in Address, Peer;
    WSAEVENT ListenerEvent, ClientEvent;
    WSANETWORKEVENTS Events;
    fd_set Readable, Writable, Failed;
    struct timeval Timeout;
    u_long NonBlocking = 1;
    char Buffer[16];
    DWORD Start, Elapsed, Wait;
    int Length, Result, Error;

    Result = WSAStartup(MAKEWORD(2, 2), &Data);
    ok(Result == 0, "WSAStartup returned %d\n", Result);
    if (Result != 0)
        return;

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    memset(&Address, 0, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed with %d\n", WSAGetLastError());
    ok(listen(Listener, 4) == 0, "listen failed with %d\n", WSAGetLastError());
    Length = sizeof(Address);
    ok(getsockname(Listener, (struct sockaddr *)&Address, &Length) == 0, "getsockname failed with %d\n", WSAGetLastError());

    ok(SelectOne(Listener, 0, 0) == 0, "An idle listener is readable\n");
    ok(SelectOne(Listener, 2, 0) == 0, "An idle listener is in the exception set\n");
    Start = GetTickCount();
    ok(SelectOne(Listener, 0, 200) == 0, "An idle listener became readable\n");
    Elapsed = GetTickCount() - Start;
    ok(Elapsed >= 150 && Elapsed < 2000, "A 200 ms select lasted %lu ms\n", Elapsed);

    ListenerEvent = WSACreateEvent();
    Result = WSAEventSelect(Listener, ListenerEvent, FD_ACCEPT);
    ok(Result == 0, "WSAEventSelect failed with %d\n", WSAGetLastError());
    ok(WaitForSingleObject(ListenerEvent, 0) == WAIT_TIMEOUT, "The listener event is set without a connection\n");

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Client != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    ok(ioctlsocket(Client, FIONBIO, &NonBlocking) == 0, "ioctlsocket failed with %d\n", WSAGetLastError());
    ok(SelectOne(Client, 1, 0) == 0, "An unconnected socket is writable\n");
    ok(SelectOne(Client, 2, 0) == 0, "An unconnected socket is in the exception set\n");
    Result = connect(Client, (struct sockaddr *)&Address, sizeof(Address));
    Error = WSAGetLastError();
    ok(Result == SOCKET_ERROR && Error == WSAEWOULDBLOCK, "connect returned %d, error %d\n", Result, Error);

    Wait = WaitForSingleObject(ListenerEvent, 3000);
    ok(Wait == WAIT_OBJECT_0, "Waiting for the listener event returned %lu\n", Wait);
    memset(&Events, 0, sizeof(Events));
    Result = WSAEnumNetworkEvents(Listener, ListenerEvent, &Events);
    ok(Result == 0, "WSAEnumNetworkEvents failed with %d\n", WSAGetLastError());
    ok(Events.lNetworkEvents & FD_ACCEPT, "Listener events are %#lx\n", Events.lNetworkEvents);
    ok(Events.iErrorCode[FD_ACCEPT_BIT] == 0, "FD_ACCEPT error is %d\n", Events.iErrorCode[FD_ACCEPT_BIT]);

    ok(SelectOne(Listener, 0, 1000) == 1, "A listener with a pending connection is not readable\n");
    Length = sizeof(Peer);
    memset(&Peer, 0, sizeof(Peer));
    Accepted = accept(Listener, (struct sockaddr *)&Peer, &Length);
    ok(Accepted != INVALID_SOCKET, "accept failed with %d\n", WSAGetLastError());
    ok(Peer.sin_addr.s_addr == htonl(INADDR_LOOPBACK), "The accepted peer is %#lx\n", ntohl(Peer.sin_addr.s_addr));

    ok(SelectOne(Client, 1, 2000) == 1, "The connected client is not writable\n");
    ok(SelectOne(Client, 2, 0) == 0, "The connected client is in the exception set\n");
    ok(SelectOne(Client, 0, 0) == 0, "The connected client is readable without data\n");

    FD_ZERO(&Readable);
    FD_ZERO(&Writable);
    FD_ZERO(&Failed);
    FD_SET(Client, &Readable);
    FD_SET(Accepted, &Readable);
    FD_SET(Client, &Writable);
    FD_SET(Accepted, &Writable);
    FD_SET(Client, &Failed);
    FD_SET(Accepted, &Failed);
    Timeout.tv_sec = 1;
    Timeout.tv_usec = 0;
    Result = select(0, &Readable, &Writable, &Failed, &Timeout);
    ok(Result == 2, "select on two sockets returned %d\n", Result);
    ok(Readable.fd_count == 0 && Writable.fd_count == 2 && Failed.fd_count == 0,
       "select on two sockets set %u readable, %u writable, %u failed\n",
       Readable.fd_count, Writable.fd_count, Failed.fd_count);

    ClientEvent = WSACreateEvent();
    Result = WSAEventSelect(Client, ClientEvent, FD_READ | FD_CLOSE);
    ok(Result == 0, "WSAEventSelect failed with %d\n", WSAGetLastError());
    ok(WaitForSingleObject(ClientEvent, 0) == WAIT_TIMEOUT, "The client event is set without data\n");
    Result = send(Accepted, "hello", 5, 0);
    ok(Result == 5, "send returned %d, error %d\n", Result, WSAGetLastError());
    Wait = WaitForSingleObject(ClientEvent, 3000);
    ok(Wait == WAIT_OBJECT_0, "Waiting for the client event returned %lu\n", Wait);
    memset(&Events, 0, sizeof(Events));
    Result = WSAEnumNetworkEvents(Client, ClientEvent, &Events);
    ok(Result == 0, "WSAEnumNetworkEvents failed with %d\n", WSAGetLastError());
    ok(Events.lNetworkEvents & FD_READ, "Client events are %#lx\n", Events.lNetworkEvents);
    ok(SelectOne(Client, 0, 1000) == 1, "A client with data is not readable\n");
    Result = recv(Client, Buffer, sizeof(Buffer), 0);
    ok(Result == 5 && !memcmp(Buffer, "hello", 5), "recv returned %d, error %d\n", Result, WSAGetLastError());

    closesocket(Accepted);
    Wait = WaitForSingleObject(ClientEvent, 3000);
    ok(Wait == WAIT_OBJECT_0, "Waiting for the close event returned %lu\n", Wait);
    memset(&Events, 0, sizeof(Events));
    WSAEnumNetworkEvents(Client, ClientEvent, &Events);
    ok(Events.lNetworkEvents & FD_CLOSE, "Client events after the peer closed are %#lx\n", Events.lNetworkEvents);
    closesocket(Client);
    closesocket(Listener);
    WSACloseEvent(ClientEvent);
    WSACloseEvent(ListenerEvent);

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ioctlsocket(Client, FIONBIO, &NonBlocking);
    connect(Client, (struct sockaddr *)&Address, sizeof(Address));
    FD_ZERO(&Writable);
    FD_ZERO(&Failed);
    FD_SET(Client, &Writable);
    FD_SET(Client, &Failed);
    Timeout.tv_sec = 5;
    Timeout.tv_usec = 0;
    Result = select(0, NULL, &Writable, &Failed, &Timeout);
    ok(Result == 1, "select on a refused connection returned %d\n", Result);
    ok(Writable.fd_count == 0 && Failed.fd_count == 1, "A refused connection set %u writable, %u failed\n",
       Writable.fd_count, Failed.fd_count);
    Error = 0;
    Length = sizeof(Error);
    getsockopt(Client, SOL_SOCKET, SO_ERROR, (char *)&Error, &Length);
    ok(Error == WSAECONNREFUSED, "The error of a refused connection is %d\n", Error);
    closesocket(Client);

    WSACleanup();
}
