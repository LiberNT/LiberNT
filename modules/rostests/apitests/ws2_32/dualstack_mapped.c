/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests IPv6 sockets bound to and connecting to IPv4-mapped addresses
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "ws2_32.h"

static const UCHAR MappedLoopback[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 127, 0, 0, 1 };
static const UCHAR MappedRemote[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 192, 0, 2, 1 };

static SOCKET
CreateIPv6(
    _In_ int Type,
    _In_ BOOL V6Only)
{
    DWORD Value = V6Only;
    SOCKET Socket;

    Socket = socket(AF_INET6, Type, Type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP);
    ok(Socket != INVALID_SOCKET, "socket failed, error %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
        return INVALID_SOCKET;

    ok(setsockopt(Socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&Value, sizeof(Value)) == 0,
       "IPV6_V6ONLY %lu failed, error %d\n", Value, WSAGetLastError());
    return Socket;
}

static void
InitMapped(
    _Out_ SOCKADDR_IN6 *Address,
    _In_ const UCHAR *Mapped,
    _In_ USHORT Port)
{
    ZeroMemory(Address, sizeof(*Address));
    Address->sin6_family = AF_INET6;
    Address->sin6_port = Port;
    memcpy(&Address->sin6_addr, Mapped, sizeof(Address->sin6_addr));
}

static int
BindResult(
    _In_ SOCKET Socket,
    _In_ const SOCKADDR *Address,
    _In_ int Length)
{
    return bind(Socket, Address, Length) == 0 ? 0 : WSAGetLastError();
}

static void
CheckMappedName(
    _In_ SOCKET Socket,
    _In_ BOOL Peer,
    _In_ USHORT ExpectedPort,
    _In_ PCSTR What)
{
    SOCKADDR_IN6 Address;
    int Length = sizeof(Address);
    int Result;

    ZeroMemory(&Address, sizeof(Address));
    Result = Peer ? getpeername(Socket, (SOCKADDR *)&Address, &Length)
                  : getsockname(Socket, (SOCKADDR *)&Address, &Length);
    ok(Result == 0, "%s: name query failed, error %d\n", What, WSAGetLastError());
    ok(Length == sizeof(Address), "%s: length %d\n", What, Length);
    ok(Address.sin6_family == AF_INET6, "%s: family %u\n", What, Address.sin6_family);
    ok(!memcmp(&Address.sin6_addr, MappedLoopback, sizeof(MappedLoopback)), "%s: address is not ::ffff:127.0.0.1\n", What);
    if (ExpectedPort)
        ok(Address.sin6_port == ExpectedPort, "%s: port %u, expected %u\n", What, ntohs(Address.sin6_port), ntohs(ExpectedPort));
    else
        ok(Address.sin6_port != 0, "%s: port is 0\n", What);
}

static USHORT
LocalPort(
    _In_ SOCKET Socket)
{
    SOCKADDR_STORAGE Address;
    int Length = sizeof(Address);

    ZeroMemory(&Address, sizeof(Address));
    if (getsockname(Socket, (SOCKADDR *)&Address, &Length) != 0)
        return 0;
    return Address.ss_family == AF_INET6 ? ((SOCKADDR_IN6 *)&Address)->sin6_port : ((SOCKADDR_IN *)&Address)->sin_port;
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

static SOCKET
AcceptWithin(
    _In_ SOCKET Listener,
    _In_ PCSTR What)
{
    if (!Readable(Listener, 5000))
    {
        ok(FALSE, "%s: no connection to accept\n", What);
        return INVALID_SOCKET;
    }
    return accept(Listener, NULL, NULL);
}

static void
CheckExchange(
    _In_ SOCKET Client,
    _In_ SOCKET Server,
    _In_ PCSTR What)
{
    char Data[4] = { 0 };

    ok(send(Client, "ping", 4, 0) == 4, "%s: send failed, error %d\n", What, WSAGetLastError());
    ok(Readable(Server, 5000), "%s: nothing to receive\n", What);
    ok(recv(Server, Data, sizeof(Data), 0) == 4, "%s: recv failed, error %d\n", What, WSAGetLastError());
    ok(!memcmp(Data, "ping", 4), "%s: wrong data\n", What);
}

static void
TestTcpMappedListener(void)
{
    SOCKADDR_IN6 Address;
    SOCKADDR_IN Address4;
    SOCKET Listener, Client, Server;
    USHORT Port;
    int Result;

    Listener = CreateIPv6(SOCK_STREAM, FALSE);
    if (Listener == INVALID_SOCKET)
        return;

    InitMapped(&Address, MappedLoopback, 0);
    Result = BindResult(Listener, (SOCKADDR *)&Address, sizeof(Address));
    ok(Result == 0, "bind ::ffff:127.0.0.1 failed, error %d\n", Result);
    if (Result != 0)
    {
        closesocket(Listener);
        return;
    }
    CheckMappedName(Listener, FALSE, 0, "listener");
    Port = LocalPort(Listener);
    ok(listen(Listener, 4) == 0, "listen failed, error %d\n", WSAGetLastError());

    Client = CreateIPv6(SOCK_STREAM, FALSE);
    if (Client != INVALID_SOCKET)
    {
        InitMapped(&Address, MappedLoopback, Port);
        ok(connect(Client, (SOCKADDR *)&Address, sizeof(Address)) == 0,
           "dual-stack connect failed, error %d\n", WSAGetLastError());
        Server = AcceptWithin(Listener, "dual-stack");
        ok(Server != INVALID_SOCKET, "accept failed, error %d\n", WSAGetLastError());
        if (Server != INVALID_SOCKET)
        {
            CheckMappedName(Client, FALSE, 0, "dual-stack client local");
            CheckMappedName(Client, TRUE, Port, "dual-stack client peer");
            CheckMappedName(Server, FALSE, Port, "accepted local");
            CheckMappedName(Server, TRUE, LocalPort(Client), "accepted peer");
            CheckExchange(Client, Server, "dual-stack");
            closesocket(Server);
        }
        closesocket(Client);
    }

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Client != INVALID_SOCKET, "IPv4 socket failed, error %d\n", WSAGetLastError());
    if (Client != INVALID_SOCKET)
    {
        ZeroMemory(&Address4, sizeof(Address4));
        Address4.sin_family = AF_INET;
        Address4.sin_port = Port;
        Address4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ok(connect(Client, (SOCKADDR *)&Address4, sizeof(Address4)) == 0,
           "IPv4 connect failed, error %d\n", WSAGetLastError());
        Server = AcceptWithin(Listener, "IPv4");
        ok(Server != INVALID_SOCKET, "accept failed, error %d\n", WSAGetLastError());
        if (Server != INVALID_SOCKET)
        {
            CheckMappedName(Server, TRUE, LocalPort(Client), "accepted IPv4 peer");
            CheckExchange(Client, Server, "IPv4");
            closesocket(Server);
        }
        closesocket(Client);
    }

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Client != INVALID_SOCKET)
    {
        ZeroMemory(&Address4, sizeof(Address4));
        Address4.sin_family = AF_INET;
        Address4.sin_port = Port;
        Address4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Result = BindResult(Client, (SOCKADDR *)&Address4, sizeof(Address4));
        ok(Result == WSAEADDRINUSE, "IPv4 bind on the listener's port gave %d\n", Result);
        closesocket(Client);
    }

    closesocket(Listener);
}

static void
TestTcpV6OnlyMapped(void)
{
    SOCKADDR_IN6 Address;
    SOCKET Socket, Listener;
    USHORT Port;
    int Result;

    Socket = CreateIPv6(SOCK_STREAM, TRUE);
    if (Socket != INVALID_SOCKET)
    {
        InitMapped(&Address, MappedLoopback, 0);
        Result = BindResult(Socket, (SOCKADDR *)&Address, sizeof(Address));
        ok(Result == WSAEADDRNOTAVAIL, "IPv6-only bind ::ffff:127.0.0.1 gave %d\n", Result);
        closesocket(Socket);
    }

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "IPv4 socket failed, error %d\n", WSAGetLastError());
    if (Listener == INVALID_SOCKET)
        return;
    {
        SOCKADDR_IN Address4;

        ZeroMemory(&Address4, sizeof(Address4));
        Address4.sin_family = AF_INET;
        Address4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ok(bind(Listener, (SOCKADDR *)&Address4, sizeof(Address4)) == 0, "IPv4 bind failed, error %d\n", WSAGetLastError());
        ok(listen(Listener, 4) == 0, "listen failed, error %d\n", WSAGetLastError());
    }
    Port = LocalPort(Listener);

    Socket = CreateIPv6(SOCK_STREAM, TRUE);
    if (Socket != INVALID_SOCKET)
    {
        InitMapped(&Address, MappedLoopback, Port);
        Result = connect(Socket, (SOCKADDR *)&Address, sizeof(Address)) == 0 ? 0 : WSAGetLastError();
        ok(Result == WSAEADDRNOTAVAIL, "IPv6-only connect to ::ffff:127.0.0.1 gave %d\n", Result);
        closesocket(Socket);
    }

    Socket = CreateIPv6(SOCK_STREAM, FALSE);
    if (Socket != INVALID_SOCKET)
    {
        SOCKET Server;

        InitMapped(&Address, MappedLoopback, Port);
        ok(connect(Socket, (SOCKADDR *)&Address, sizeof(Address)) == 0,
           "dual-stack connect to an IPv4 listener failed, error %d\n", WSAGetLastError());
        Server = AcceptWithin(Listener, "IPv4 listener");
        ok(Server != INVALID_SOCKET, "accept failed, error %d\n", WSAGetLastError());
        if (Server != INVALID_SOCKET)
        {
            CheckMappedName(Socket, TRUE, Port, "IPv4 listener peer");
            CheckExchange(Socket, Server, "IPv4 listener");
            closesocket(Server);
        }
        closesocket(Socket);
    }

    closesocket(Listener);
}

static SOCKET
BindWildcard(
    _In_ int Family,
    _In_ USHORT Port,
    _Out_ int *Result)
{
    SOCKADDR_STORAGE Address;
    SOCKET Socket;

    *Result = 0;
    ZeroMemory(&Address, sizeof(Address));
    if (Family == AF_INET6)
    {
        Socket = CreateIPv6(SOCK_STREAM, FALSE);
        ((SOCKADDR_IN6 *)&Address)->sin6_family = AF_INET6;
        ((SOCKADDR_IN6 *)&Address)->sin6_port = Port;
    }
    else
    {
        Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ok(Socket != INVALID_SOCKET, "IPv4 socket failed, error %d\n", WSAGetLastError());
        ((SOCKADDR_IN *)&Address)->sin_family = AF_INET;
        ((SOCKADDR_IN *)&Address)->sin_port = Port;
    }
    if (Socket != INVALID_SOCKET)
        *Result = BindResult(Socket, (SOCKADDR *)&Address, Family == AF_INET6 ? sizeof(SOCKADDR_IN6) : sizeof(SOCKADDR_IN));
    return Socket;
}

static USHORT
EphemeralPort(
    _In_ int Family)
{
    SOCKET Socket;
    USHORT Port;
    int Result;

    Socket = BindWildcard(Family, 0, &Result);
    if (Socket == INVALID_SOCKET)
        return 0;
    Port = Result == 0 ? ntohs(LocalPort(Socket)) : 0;
    closesocket(Socket);
    return Port;
}

static void
AlignEphemeralPorts(void)
{
    USHORT Port4, Port6;
    ULONG Round, Extra;

    /* Sequential allocators hand out the same next port to both families after this. */
    for (Round = 0; Round < 8; Round++)
    {
        Port4 = EphemeralPort(AF_INET);
        Port6 = EphemeralPort(AF_INET6);
        if (!Port4 || !Port6 || Port4 == Port6)
            return;
        for (Extra = 0; Extra < min(abs((int)Port6 - (int)Port4) - 1, 512); Extra++)
            EphemeralPort(Port4 < Port6 ? AF_INET : AF_INET6);
        EphemeralPort(Port4 < Port6 ? AF_INET : AF_INET6);
    }
}

static void
TestTcpDualStackPorts(void)
{
    SOCKADDR_IN Address4;
    SOCKADDR_IN6 Address;
    SOCKET First, Second, Listener, Dual, Client, Server;
    USHORT Port;
    int Result;

    First = BindWildcard(AF_INET, 0, &Result);
    ok(Result == 0, "IPv4 bind failed, error %d\n", Result);
    Port = LocalPort(First);
    Second = BindWildcard(AF_INET6, Port, &Result);
    ok(Result == 0, "dual-stack bind on an IPv4 port failed, error %d\n", Result);
    closesocket(Second);
    closesocket(First);

    First = BindWildcard(AF_INET6, 0, &Result);
    ok(Result == 0, "dual-stack bind failed, error %d\n", Result);
    Port = LocalPort(First);
    Second = BindWildcard(AF_INET, Port, &Result);
    ok(Result == 0, "IPv4 bind on a dual-stack port failed, error %d\n", Result);
    closesocket(Second);
    closesocket(First);

    First = BindWildcard(AF_INET6, 0, &Result);
    ok(Result == 0, "dual-stack bind failed, error %d\n", Result);
    Port = LocalPort(First);
    ok(listen(First, 4) == 0, "listen failed, error %d\n", WSAGetLastError());
    closesocket(First);
    Second = BindWildcard(AF_INET6, Port, &Result);
    ok(Result == 0, "bind on a closed dual-stack listener's port failed, error %d\n", Result);
    closesocket(Second);

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET, "IPv4 socket failed, error %d\n", WSAGetLastError());
    if (Listener == INVALID_SOCKET)
        return;
    ZeroMemory(&Address4, sizeof(Address4));
    Address4.sin_family = AF_INET;
    Address4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Listener, (SOCKADDR *)&Address4, sizeof(Address4)) == 0, "IPv4 bind failed, error %d\n", WSAGetLastError());
    ok(listen(Listener, 4) == 0, "listen failed, error %d\n", WSAGetLastError());
    Port = LocalPort(Listener);

    AlignEphemeralPorts();
    Dual = CreateIPv6(SOCK_STREAM, FALSE);
    if (Dual != INVALID_SOCKET)
    {
        InitMapped(&Address, MappedLoopback, Port);
        ok(connect(Dual, (SOCKADDR *)&Address, sizeof(Address)) == 0,
           "dual-stack connect failed, error %d\n", WSAGetLastError());
        Server = AcceptWithin(Listener, "dual-stack client");
        if (Server != INVALID_SOCKET)
            closesocket(Server);

        Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ok(Client != INVALID_SOCKET, "IPv4 socket failed, error %d\n", WSAGetLastError());
        if (Client != INVALID_SOCKET)
        {
            Address4.sin_port = Port;
            ok(connect(Client, (SOCKADDR *)&Address4, sizeof(Address4)) == 0,
               "IPv4 connect beside a dual-stack client failed, error %d\n", WSAGetLastError());
            ok(LocalPort(Client) != LocalPort(Dual), "IPv4 client got the dual-stack client's port %u\n", ntohs(LocalPort(Dual)));
            Server = AcceptWithin(Listener, "IPv4 client");
            if (Server != INVALID_SOCKET)
                closesocket(Server);
            closesocket(Client);
        }
        closesocket(Dual);
    }
    closesocket(Listener);
}

static void
TestNonLocalMapped(void)
{
    SOCKADDR_IN6 Address;
    SOCKET Socket;
    int Result;

    Socket = CreateIPv6(SOCK_STREAM, FALSE);
    if (Socket == INVALID_SOCKET)
        return;

    InitMapped(&Address, MappedRemote, 0);
    Result = BindResult(Socket, (SOCKADDR *)&Address, sizeof(Address));
    ok(Result == WSAEADDRNOTAVAIL, "bind ::ffff:192.0.2.1 gave %d\n", Result);
    closesocket(Socket);
}

static void
TestUdpMapped(void)
{
    SOCKADDR_IN6 Address, From;
    SOCKADDR_IN Address4;
    SOCKET Socket, Sender, V6Only;
    int FromLength = sizeof(From);
    USHORT Port;
    char Data;
    int Result;

    Socket = CreateIPv6(SOCK_DGRAM, FALSE);
    if (Socket == INVALID_SOCKET)
        return;

    InitMapped(&Address, MappedLoopback, 0);
    Result = BindResult(Socket, (SOCKADDR *)&Address, sizeof(Address));
    ok(Result == 0, "UDP bind ::ffff:127.0.0.1 failed, error %d\n", Result);
    if (Result != 0)
    {
        closesocket(Socket);
        return;
    }
    CheckMappedName(Socket, FALSE, 0, "UDP local");
    Port = LocalPort(Socket);

    Sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Sender != INVALID_SOCKET, "IPv4 socket failed, error %d\n", WSAGetLastError());
    if (Sender != INVALID_SOCKET)
    {
        ZeroMemory(&Address4, sizeof(Address4));
        Address4.sin_family = AF_INET;
        Address4.sin_port = Port;
        Address4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ok(sendto(Sender, "x", 1, 0, (SOCKADDR *)&Address4, sizeof(Address4)) == 1,
           "IPv4 sendto failed, error %d\n", WSAGetLastError());

        if (!Readable(Socket, 2000))
        {
            ok(FALSE, "UDP datagram not received\n");
            closesocket(Sender);
            closesocket(Socket);
            return;
        }
        ZeroMemory(&From, sizeof(From));
        ok(recvfrom(Socket, &Data, 1, 0, (SOCKADDR *)&From, &FromLength) == 1,
           "recvfrom failed, error %d\n", WSAGetLastError());
        ok(From.sin6_family == AF_INET6, "source family %u\n", From.sin6_family);
        ok(!memcmp(&From.sin6_addr, MappedLoopback, sizeof(MappedLoopback)), "source is not ::ffff:127.0.0.1\n");
        ok(From.sin6_port == LocalPort(Sender), "source port %u\n", ntohs(From.sin6_port));

        ok(sendto(Socket, "y", 1, 0, (SOCKADDR *)&From, sizeof(From)) == 1,
           "mapped sendto failed, error %d\n", WSAGetLastError());
        if (Readable(Sender, 2000))
            ok(recv(Sender, &Data, 1, 0) == 1 && Data == 'y', "reply recv failed, error %d\n", WSAGetLastError());
        else
            ok(FALSE, "reply not received\n");

        V6Only = CreateIPv6(SOCK_DGRAM, TRUE);
        if (V6Only != INVALID_SOCKET)
        {
            Result = sendto(V6Only, "z", 1, 0, (SOCKADDR *)&From, sizeof(From)) == 1 ? 0 : WSAGetLastError();
            ok(Result == WSAEADDRNOTAVAIL, "IPv6-only sendto ::ffff:127.0.0.1 gave %d\n", Result);
            closesocket(V6Only);
        }
        closesocket(Sender);
    }

    closesocket(Socket);
}

START_TEST(dualstack_mapped)
{
    WSADATA WsaData;
    SOCKET Socket;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");

    Socket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (Socket == INVALID_SOCKET)
    {
        skip("No IPv6 socket, error %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }
    closesocket(Socket);

    TestTcpV6OnlyMapped();
    TestTcpDualStackPorts();
    TestTcpMappedListener();
    TestNonLocalMapped();
    TestUdpMapped();

    WSACleanup();
}
