/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test receiving data already buffered on a connected TCP socket
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"

static BOOL
CreateTcpPair(SOCKET *Client, SOCKET *Server)
{
    struct sockaddr_in Address = { 0 };
    int Length = sizeof(Address);
    SOCKET Listener;

    *Client = INVALID_SOCKET;
    *Server = INVALID_SOCKET;

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Listener == INVALID_SOCKET)
        return FALSE;

    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) ||
        getsockname(Listener, (struct sockaddr *)&Address, &Length) ||
        listen(Listener, 1))
    {
        closesocket(Listener);
        return FALSE;
    }

    *Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (*Client != INVALID_SOCKET &&
        !connect(*Client, (struct sockaddr *)&Address, sizeof(Address)))
    {
        *Server = accept(Listener, NULL, NULL);
    }
    closesocket(Listener);

    if (*Server == INVALID_SOCKET)
    {
        if (*Client != INVALID_SOCKET)
            closesocket(*Client);
        *Client = INVALID_SOCKET;
        return FALSE;
    }

    return TRUE;
}

static void
WaitForBytes(SOCKET Socket, u_long Expected)
{
    DWORD Start = GetTickCount();
    u_long Available = 0;

    while (GetTickCount() - Start < 5000)
    {
        if (!ioctlsocket(Socket, FIONREAD, &Available) && Available >= Expected)
            break;
        Sleep(10);
    }

    ok(Available == Expected, "FIONREAD returned %lu, expected %lu\n", Available, Expected);
}

START_TEST(BufferedRecv)
{
    WSANETWORKEVENTS NetworkEvents;
    SOCKET Client, Server;
    char Buffer[16], First[3], Second[2];
    WSABUF Buffers[2];
    DWORD Received, Flags;
    WSADATA Data;
    WSAEVENT Event;
    volatile int Length;
    int Result;

    ok(!WSAStartup(MAKEWORD(2, 2), &Data), "WSAStartup failed\n");
    if (!CreateTcpPair(&Client, &Server))
    {
        skip("Could not create a loopback TCP connection, error %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }

    Result = send(Server, "0123456789", 10, 0);
    ok(Result == 10, "send returned %d, error %d\n", Result, WSAGetLastError());
    WaitForBytes(Client, 10);

    Event = WSACreateEvent();
    ok(!WSAEventSelect(Client, Event, FD_READ), "WSAEventSelect failed, error %d\n", WSAGetLastError());
    ok(WaitForSingleObject(Event, 1000) == WAIT_OBJECT_0, "FD_READ was not signaled\n");
    ok(!WSAEnumNetworkEvents(Client, Event, &NetworkEvents), "WSAEnumNetworkEvents failed\n");
    ok(NetworkEvents.lNetworkEvents == FD_READ, "Network events 0x%lx\n", NetworkEvents.lNetworkEvents);

    memset(Buffer, 0, sizeof(Buffer));
    Result = recv(Client, Buffer, 1, 0);
    ok(Result == 1, "recv returned %d, error %d\n", Result, WSAGetLastError());
    ok(Buffer[0] == '0', "Received 0x%02x\n", (UCHAR)Buffer[0]);
    ok(WaitForSingleObject(Event, 1000) == WAIT_OBJECT_0, "FD_READ was not signaled again while data remains\n");
    ok(!WSAEnumNetworkEvents(Client, Event, &NetworkEvents), "WSAEnumNetworkEvents failed\n");
    ok(NetworkEvents.lNetworkEvents == FD_READ, "Network events 0x%lx\n", NetworkEvents.lNetworkEvents);

    Buffers[0].buf = First;
    Buffers[0].len = sizeof(First);
    Buffers[1].buf = Second;
    Buffers[1].len = sizeof(Second);
    Received = 0xdeadbeef;
    Flags = 0;
    Result = WSARecv(Client, Buffers, 2, &Received, &Flags, NULL, NULL);
    ok(Result == 0, "WSARecv returned %d, error %d\n", Result, WSAGetLastError());
    ok(Received == 5, "WSARecv received %lu bytes\n", Received);
    ok(Flags == 0, "WSARecv returned flags 0x%lx\n", Flags);
    ok(!memcmp(First, "123", 3) && !memcmp(Second, "45", 2), "WSARecv received '%.3s' '%.2s'\n", First, Second);

    Length = -1;
    Result = recv(Client, Buffer, Length, 0);
    ok(Result == SOCKET_ERROR, "recv with length -1 returned %d\n", Result);
    ok(WSAGetLastError() == WSAEFAULT, "recv with length -1 failed with %d\n", WSAGetLastError());
    WaitForBytes(Client, 4);

    Result = recv(Client, (char *)1, 0, 0);
    ok(Result == 0, "recv of zero bytes returned %d, error %d\n", Result, WSAGetLastError());
    WaitForBytes(Client, 4);

    memset(Buffer, 0, sizeof(Buffer));
    Result = recv(Client, Buffer, 2, MSG_PEEK);
    ok(Result == 2, "recv MSG_PEEK returned %d, error %d\n", Result, WSAGetLastError());
    ok(!memcmp(Buffer, "67", 2), "recv MSG_PEEK received '%.2s'\n", Buffer);
    WaitForBytes(Client, 4);

    memset(Buffer, 0, sizeof(Buffer));
    Result = recv(Client, Buffer, sizeof(Buffer), 0);
    ok(Result == 4, "recv returned %d, error %d\n", Result, WSAGetLastError());
    ok(!memcmp(Buffer, "6789", 4), "recv received '%.4s'\n", Buffer);

    Result = recv(Client, Buffer, 1, 0);
    ok(Result == SOCKET_ERROR, "recv without data returned %d\n", Result);
    ok(WSAGetLastError() == WSAEWOULDBLOCK, "recv without data failed with %d\n", WSAGetLastError());

    WSAEventSelect(Client, Event, 0);
    WSACloseEvent(Event);
    closesocket(Client);
    closesocket(Server);
    WSACleanup();
}
