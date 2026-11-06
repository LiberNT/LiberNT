/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which socket on a shared UDP port receives a unicast datagram
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"

#define SOCKETS 3

static SOCKET
CreateReuseSocket(void)
{
    SOCKET Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    BOOL Reuse = TRUE;

    ok(Socket != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    ok(!setsockopt(Socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&Reuse, sizeof(Reuse)),
       "setsockopt failed, error %d\n", WSAGetLastError());
    return Socket;
}

static int
BindTo(SOCKET Socket, ULONG Address, USHORT Port)
{
    struct sockaddr_in Local = { 0 };

    Local.sin_family = AF_INET;
    Local.sin_addr.s_addr = Address;
    Local.sin_port = Port;
    return bind(Socket, (struct sockaddr *)&Local, sizeof(Local)) ? WSAGetLastError() : 0;
}

static USHORT
PortOf(SOCKET Socket)
{
    struct sockaddr_in Local = { 0 };
    int Length = sizeof(Local);

    ok(!getsockname(Socket, (struct sockaddr *)&Local, &Length), "getsockname failed, error %d\n", WSAGetLastError());
    return Local.sin_port;
}

static void
SendOne(USHORT Port)
{
    struct sockaddr_in Destination = { 0 };
    SOCKET Sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    Destination.sin_family = AF_INET;
    Destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Destination.sin_port = Port;
    ok(sendto(Sender, "x", 1, 0, (struct sockaddr *)&Destination, sizeof(Destination)) == 1,
       "sendto failed, error %d\n", WSAGetLastError());
    closesocket(Sender);
}

static BOOL
WaitReadable(SOCKET Socket)
{
    struct timeval Timeout = { 5, 0 };
    fd_set Set;

    FD_ZERO(&Set);
    FD_SET(Socket, &Set);
    return select(0, &Set, NULL, NULL, &Timeout) == 1;
}

static u_long
Pending(SOCKET Socket)
{
    u_long Bytes = 0;

    ok(!ioctlsocket(Socket, FIONREAD, &Bytes), "ioctlsocket failed, error %d\n", WSAGetLastError());
    return Bytes;
}

static void
ExpectReceiver(PCSTR Name, SOCKET *Sockets, ULONG Count, ULONG Expected)
{
    ULONG Index;
    BOOL Readable;
    char Byte;

    Readable = WaitReadable(Sockets[Expected]);
    ok(Readable, "%s: socket %lu did not receive the datagram\n", Name, Expected);
    for (Index = 0; Index < Count; Index++)
    {
        if (Index == Expected || Sockets[Index] == INVALID_SOCKET)
            continue;
        if (Pending(Sockets[Index]))
        {
            ok(FALSE, "%s: socket %lu received the datagram\n", Name, Index);
            recv(Sockets[Index], &Byte, 1, 0);
        }
    }
    if (Readable)
        recv(Sockets[Expected], &Byte, 1, 0);
}

static void
Test_SamePort(PCSTR Name, ULONG Address)
{
    SOCKET Sockets[SOCKETS];
    ULONG Index;
    USHORT Port;

    for (Index = 0; Index < SOCKETS; Index++)
        Sockets[Index] = CreateReuseSocket();

    ok(!BindTo(Sockets[0], Address, 0), "%s: first bind failed\n", Name);
    Port = PortOf(Sockets[0]);
    for (Index = 1; Index < SOCKETS; Index++)
        ok(!BindTo(Sockets[Index], Address, Port), "%s: bind %lu failed\n", Name, Index);

    for (Index = 0; Index < 3; Index++)
    {
        SendOne(Port);
        ExpectReceiver(Name, Sockets, SOCKETS, 0);
    }

    closesocket(Sockets[0]);
    Sockets[0] = INVALID_SOCKET;
    SendOne(Port);
    ExpectReceiver(Name, Sockets, SOCKETS, 1);

    for (Index = 1; Index < SOCKETS; Index++)
        closesocket(Sockets[Index]);
}

static void
Test_Specificity(PCSTR Name, ULONG FirstAddress, ULONG SecondAddress, ULONG Expected)
{
    SOCKET Sockets[2];
    USHORT Port;

    Sockets[0] = CreateReuseSocket();
    Sockets[1] = CreateReuseSocket();
    ok(!BindTo(Sockets[0], FirstAddress, 0), "%s: first bind failed\n", Name);
    Port = PortOf(Sockets[0]);
    ok(!BindTo(Sockets[1], SecondAddress, Port), "%s: second bind failed\n", Name);

    SendOne(Port);
    ExpectReceiver(Name, Sockets, 2, Expected);

    closesocket(Sockets[1]);
    closesocket(Sockets[0]);
}

START_TEST(ReuseDelivery)
{
    WSADATA Data;

    ok(!WSAStartup(MAKEWORD(2, 2), &Data), "WSAStartup failed\n");
    Test_SamePort("loopback", htonl(INADDR_LOOPBACK));
    Test_SamePort("wildcard", htonl(INADDR_ANY));
    Test_Specificity("wildcard then loopback", htonl(INADDR_ANY), htonl(INADDR_LOOPBACK), 1);
    Test_Specificity("loopback then wildcard", htonl(INADDR_LOOPBACK), htonl(INADDR_ANY), 0);
    WSACleanup();
}
