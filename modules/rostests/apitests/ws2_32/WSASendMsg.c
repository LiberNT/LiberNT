/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the WSASendMsg extension function
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "ws2_32.h"
#include <mswsock.h>

static LPFN_WSASENDMSG
GetSendMsg(
    _In_ SOCKET Socket,
    _In_ PCSTR Tag)
{
    GUID SendMsgGuid = WSAID_WSASENDMSG;
    LPFN_WSASENDMSG SendMsg = NULL;
    DWORD Bytes = 0xdeadbeef;
    int Result;

    Result = WSAIoctl(Socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &SendMsgGuid, sizeof(SendMsgGuid),
                      &SendMsg, sizeof(SendMsg), &Bytes, NULL, NULL);
    ok(Result == 0, "%s: WSAIoctl returned %d, error %d\n", Tag, Result, WSAGetLastError());
    ok(SendMsg != NULL, "%s: WSASendMsg pointer is NULL\n", Tag);
    ok(Bytes == sizeof(SendMsg), "%s: WSAIoctl returned %lu bytes\n", Tag, Bytes);
    return Result == 0 ? SendMsg : NULL;
}

static SOCKET
BoundUdpSocket(
    _Out_ struct sockaddr_in *Address)
{
    SOCKET Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int Length = sizeof(*Address);

    ok(Socket != INVALID_SOCKET, "UDP socket failed, error %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
        return INVALID_SOCKET;
    ZeroMemory(Address, sizeof(*Address));
    Address->sin_family = AF_INET;
    Address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(!bind(Socket, (struct sockaddr *)Address, sizeof(*Address)), "bind failed, error %d\n", WSAGetLastError());
    ok(!getsockname(Socket, (struct sockaddr *)Address, &Length), "getsockname failed, error %d\n", WSAGetLastError());
    return Socket;
}

static VOID
ExpectDatagram(
    _In_ SOCKET Receiver,
    _In_ CHAR Expected,
    _In_ const struct sockaddr_in *From,
    _In_ PCSTR Tag)
{
    struct timeval Timeout = { 2, 0 };
    struct sockaddr_in Source;
    int Length = sizeof(Source), Result;
    fd_set ReadSet;
    CHAR Data[4];

    FD_ZERO(&ReadSet);
    FD_SET(Receiver, &ReadSet);
    Result = select(0, &ReadSet, NULL, NULL, &Timeout);
    ok(Result == 1, "%s: no datagram arrived\n", Tag);
    if (Result != 1)
        return;
    Result = recvfrom(Receiver, Data, sizeof(Data), 0, (struct sockaddr *)&Source, &Length);
    ok(Result == 1 && Data[0] == Expected, "%s: recvfrom returned %d, error %d, byte %d\n",
       Tag, Result, WSAGetLastError(), Result > 0 ? Data[0] : -1);
    if (From)
    {
        ok(Source.sin_addr.s_addr == From->sin_addr.s_addr && Source.sin_port == From->sin_port,
           "%s: datagram came from %08lx:%u\n", Tag, (ULONG)ntohl(Source.sin_addr.s_addr), ntohs(Source.sin_port));
    }
}

static VOID
TestQueryOnTcpSocket(VOID)
{
    SOCKET Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    ok(Socket != INVALID_SOCKET, "TCP socket failed, error %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
        return;
    GetSendMsg(Socket, "tcp query");
    closesocket(Socket);
}

static VOID
TestUdp(VOID)
{
    struct sockaddr_in ReceiverAddress, SenderAddress;
    SOCKET Receiver, Sender, Unbound;
    LPFN_WSASENDMSG SendMsg;
    WSABUF Buffer;
    WSAMSG Message;
    DWORD Bytes;
    int Result;

    Receiver = BoundUdpSocket(&ReceiverAddress);
    Sender = BoundUdpSocket(&SenderAddress);
    Unbound = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Unbound != INVALID_SOCKET, "UDP socket failed, error %d\n", WSAGetLastError());
    if (Receiver == INVALID_SOCKET || Sender == INVALID_SOCKET || Unbound == INVALID_SOCKET)
        goto Cleanup;
    SendMsg = GetSendMsg(Sender, "udp query");
    if (!SendMsg)
        goto Cleanup;

    Buffer.buf = "A";
    Buffer.len = 1;
    ZeroMemory(&Message, sizeof(Message));
    Message.name = (LPSOCKADDR)&ReceiverAddress;
    Message.namelen = sizeof(ReceiverAddress);
    Message.lpBuffers = &Buffer;
    Message.dwBufferCount = 1;
    Bytes = 0;
    Result = SendMsg(Sender, &Message, 0, &Bytes, NULL, NULL);
    ok(Result == 0 && Bytes == 1, "unconnected: returned %d, error %d, bytes %lu\n", Result, WSAGetLastError(), Bytes);
    ExpectDatagram(Receiver, 'A', &SenderAddress, "unconnected");

    Buffer.buf = "B";
    Bytes = 0;
    Result = SendMsg(Unbound, &Message, 0, &Bytes, NULL, NULL);
    ok(Result == 0 && Bytes == 1, "unbound: returned %d, error %d, bytes %lu\n", Result, WSAGetLastError(), Bytes);
    ExpectDatagram(Receiver, 'B', NULL, "unbound");

    ok(!connect(Sender, (struct sockaddr *)&ReceiverAddress, sizeof(ReceiverAddress)),
       "connect failed, error %d\n", WSAGetLastError());
    Buffer.buf = "C";
    Message.name = NULL;
    Message.namelen = 0;
    Bytes = 0;
    Result = SendMsg(Sender, &Message, 0, &Bytes, NULL, NULL);
    ok(Result == 0 && Bytes == 1, "connected: returned %d, error %d, bytes %lu\n", Result, WSAGetLastError(), Bytes);
    ExpectDatagram(Receiver, 'C', &SenderAddress, "connected");

    Result = sendto(Sender, "D", 1, 0, NULL, 0);
    ok(Result == 1, "connected sendto: returned %d, error %d\n", Result, WSAGetLastError());
    ExpectDatagram(Receiver, 'D', &SenderAddress, "connected sendto");

    Result = SendMsg(Sender, NULL, 0, &Bytes, NULL, NULL);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEFAULT, "NULL message: returned %d, error %d\n",
       Result, WSAGetLastError());

    Result = SendMsg(Sender, &Message, 0, NULL, NULL, NULL);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEFAULT, "NULL byte count: returned %d, error %d\n",
       Result, WSAGetLastError());

Cleanup:
    if (Receiver != INVALID_SOCKET)
        closesocket(Receiver);
    if (Sender != INVALID_SOCKET)
        closesocket(Sender);
    if (Unbound != INVALID_SOCKET)
        closesocket(Unbound);
}

static VOID
TestTcp(VOID)
{
    struct sockaddr_in Address;
    SOCKET Listener, Client = INVALID_SOCKET, Server = INVALID_SOCKET;
    int Length = sizeof(Address), Result;
    LPFN_WSASENDMSG SendMsg;
    WSABUF Buffer;
    WSAMSG Message;
    DWORD Bytes;
    CHAR Data[4];

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "TCP socket failed, error %d\n", WSAGetLastError());
    if (Listener == INVALID_SOCKET)
        return;
    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(!bind(Listener, (struct sockaddr *)&Address, sizeof(Address)), "bind failed, error %d\n", WSAGetLastError());
    ok(!getsockname(Listener, (struct sockaddr *)&Address, &Length), "getsockname failed, error %d\n", WSAGetLastError());
    ok(!listen(Listener, 1), "listen failed, error %d\n", WSAGetLastError());
    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Client != INVALID_SOCKET, "TCP socket failed, error %d\n", WSAGetLastError());
    if (Client == INVALID_SOCKET)
        goto Cleanup;
    SendMsg = GetSendMsg(Client, "tcp");
    if (!SendMsg)
        goto Cleanup;

    Buffer.buf = "T";
    Buffer.len = 1;
    ZeroMemory(&Message, sizeof(Message));
    Message.lpBuffers = &Buffer;
    Message.dwBufferCount = 1;
    Bytes = 0;
    Result = SendMsg(Client, &Message, 0, &Bytes, NULL, NULL);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEINVAL, "unconnected TCP: returned %d, error %d\n",
       Result, WSAGetLastError());

    ok(!connect(Client, (struct sockaddr *)&Address, sizeof(Address)), "connect failed, error %d\n", WSAGetLastError());
    Server = accept(Listener, NULL, NULL);
    ok(Server != INVALID_SOCKET, "accept failed, error %d\n", WSAGetLastError());
    Bytes = 0;
    Result = SendMsg(Client, &Message, 0, &Bytes, NULL, NULL);
    ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEINVAL, "connected TCP: returned %d, error %d\n",
       Result, WSAGetLastError());
    if (Server != INVALID_SOCKET)
    {
        u_long NonBlocking = 1;

        ok(!ioctlsocket(Server, FIONBIO, &NonBlocking), "ioctlsocket failed, error %d\n", WSAGetLastError());
        Result = recv(Server, Data, sizeof(Data), 0);
        ok(Result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK, "connected TCP: recv returned %d, error %d\n",
           Result, WSAGetLastError());
    }

Cleanup:
    if (Server != INVALID_SOCKET)
        closesocket(Server);
    if (Client != INVALID_SOCKET)
        closesocket(Client);
    closesocket(Listener);
}

START_TEST(WSASendMsg)
{
    WSADATA WsaData;

    if (WSAStartup(MAKEWORD(2, 2), &WsaData))
    {
        skip("WSAStartup failed\n");
        return;
    }
    TestQueryOnTcpSocket();
    TestUdp();
    TestTcp();
    WSACleanup();
}
