/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a datagram sent to a closed port fails the next receive with WSAECONNRESET unless SIO_UDP_CONNRESET turns it off
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"
#include <mswsock.h>

#define ARRIVAL_TIMEOUT_MS 1000
#define SILENCE_TIMEOUT_MS 100

static
BOOL
OpenLoopbackSocket(
    _Out_ SOCKET *Socket,
    _Out_ struct sockaddr_in *Address)
{
    int Length = sizeof(*Address);
    u_long NonBlocking = 1;

    ZeroMemory(Address, sizeof(*Address));
    Address->sin_family = AF_INET;
    Address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    *Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (*Socket == INVALID_SOCKET)
        return FALSE;

    if (bind(*Socket, (const struct sockaddr *)Address, sizeof(*Address)) != 0 ||
        getsockname(*Socket, (struct sockaddr *)Address, &Length) != 0 ||
        ioctlsocket(*Socket, FIONBIO, &NonBlocking) != 0)
    {
        closesocket(*Socket);
        *Socket = INVALID_SOCKET;
        return FALSE;
    }

    return TRUE;
}

/* A loopback port that was bound a moment ago and is closed now */
static
BOOL
ClosedPort(
    _Out_ struct sockaddr_in *Address)
{
    SOCKET Socket;

    if (!OpenLoopbackSocket(&Socket, Address))
        return FALSE;
    closesocket(Socket);
    return TRUE;
}

static
int
WaitReadable(
    _In_ SOCKET Socket,
    _In_ DWORD Milliseconds)
{
    struct timeval Timeout;
    fd_set Readable;

    FD_ZERO(&Readable);
    FD_SET(Socket, &Readable);
    Timeout.tv_sec = Milliseconds / 1000;
    Timeout.tv_usec = (Milliseconds % 1000) * 1000;
    return select(0, &Readable, NULL, NULL, &Timeout);
}

static
BOOL
SetReporting(
    _In_ SOCKET Socket,
    _In_ BOOL Report)
{
    DWORD Returned = 0;

    return WSAIoctl(Socket, SIO_UDP_CONNRESET, &Report, sizeof(Report), NULL, 0, &Returned, NULL, NULL) == 0;
}

static
void
ExpectReset(
    _In_ SOCKET Socket,
    _In_ const struct sockaddr_in *Closed,
    _In_ const char *Step)
{
    struct sockaddr_in From;
    int FromLength = sizeof(From);
    char Byte = 'x';
    int Result;

    Result = sendto(Socket, &Byte, 1, 0, (const struct sockaddr *)Closed, sizeof(*Closed));
    ok(Result == 1, "%s: sendto returned %d, error %d\n", Step, Result, WSAGetLastError());

    Result = WaitReadable(Socket, ARRIVAL_TIMEOUT_MS);
    ok(Result == 1, "%s: select returned %d, error %d\n", Step, Result, WSAGetLastError());

    WSASetLastError(0xdeadbeef);
    Result = recvfrom(Socket, &Byte, 1, 0, (struct sockaddr *)&From, &FromLength);
    ok(Result == SOCKET_ERROR, "%s: recvfrom returned %d\n", Step, Result);
    ok(WSAGetLastError() == WSAECONNRESET, "%s: recvfrom error %d\n", Step, WSAGetLastError());

    WSASetLastError(0xdeadbeef);
    Result = recvfrom(Socket, &Byte, 1, 0, NULL, NULL);
    ok(Result == SOCKET_ERROR, "%s: second recvfrom returned %d\n", Step, Result);
    ok(WSAGetLastError() == WSAEWOULDBLOCK, "%s: second recvfrom error %d\n", Step, WSAGetLastError());
}

static
void
ExpectSilence(
    _In_ SOCKET Socket,
    _In_ const struct sockaddr_in *Closed,
    _In_ const char *Step)
{
    char Byte = 'x';
    int Result;

    Result = sendto(Socket, &Byte, 1, 0, (const struct sockaddr *)Closed, sizeof(*Closed));
    ok(Result == 1, "%s: sendto returned %d, error %d\n", Step, Result, WSAGetLastError());

    Result = WaitReadable(Socket, SILENCE_TIMEOUT_MS);
    ok(Result == 0, "%s: select returned %d, error %d\n", Step, Result, WSAGetLastError());

    WSASetLastError(0xdeadbeef);
    Result = recvfrom(Socket, &Byte, 1, 0, NULL, NULL);
    ok(Result == SOCKET_ERROR, "%s: recvfrom returned %d\n", Step, Result);
    ok(WSAGetLastError() == WSAEWOULDBLOCK, "%s: recvfrom error %d\n", Step, WSAGetLastError());
}

static
void
TestReportedByDefault(void)
{
    struct sockaddr_in Local, Closed, From;
    int FromLength = sizeof(From);
    char Byte = 'y';
    SOCKET Socket;
    int Result;

    if (!ClosedPort(&Closed) || !OpenLoopbackSocket(&Socket, &Local))
    {
        skip("default: no loopback datagram socket, error %d\n", WSAGetLastError());
        return;
    }

    ExpectReset(Socket, &Closed, "default");

    /* The socket stays usable */
    Result = sendto(Socket, &Byte, 1, 0, (const struct sockaddr *)&Local, sizeof(Local));
    ok(Result == 1, "default: sendto to self returned %d, error %d\n", Result, WSAGetLastError());
    Result = WaitReadable(Socket, ARRIVAL_TIMEOUT_MS);
    ok(Result == 1, "default: select for own datagram returned %d, error %d\n", Result, WSAGetLastError());
    Byte = 0;
    Result = recvfrom(Socket, &Byte, 1, 0, (struct sockaddr *)&From, &FromLength);
    ok(Result == 1, "default: recvfrom of own datagram returned %d, error %d\n", Result, WSAGetLastError());
    ok(Byte == 'y', "default: received 0x%02x\n", Byte);
    ok(From.sin_port == Local.sin_port, "default: datagram from port %u, expected %u\n",
       ntohs(From.sin_port), ntohs(Local.sin_port));

    closesocket(Socket);
}

static
void
TestTurnedOff(void)
{
    struct sockaddr_in Local, Closed;
    SOCKET Socket;

    if (!ClosedPort(&Closed) || !OpenLoopbackSocket(&Socket, &Local))
    {
        skip("off: no loopback datagram socket, error %d\n", WSAGetLastError());
        return;
    }

    ok(SetReporting(Socket, FALSE), "off: SIO_UDP_CONNRESET failed with %d\n", WSAGetLastError());
    ExpectSilence(Socket, &Closed, "off");

    ok(SetReporting(Socket, TRUE), "on again: SIO_UDP_CONNRESET failed with %d\n", WSAGetLastError());
    ExpectReset(Socket, &Closed, "on again");

    closesocket(Socket);
}

static
void
TestTurnedOffBeforeBind(void)
{
    struct sockaddr_in Local, Closed;
    u_long NonBlocking = 1;
    SOCKET Socket;

    if (!ClosedPort(&Closed))
    {
        skip("before bind: no loopback datagram socket, error %d\n", WSAGetLastError());
        return;
    }

    Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Socket != INVALID_SOCKET, "before bind: socket failed with %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
        return;

    ok(SetReporting(Socket, FALSE), "before bind: SIO_UDP_CONNRESET failed with %d\n", WSAGetLastError());

    ZeroMemory(&Local, sizeof(Local));
    Local.sin_family = AF_INET;
    Local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok(bind(Socket, (const struct sockaddr *)&Local, sizeof(Local)) == 0, "before bind: bind failed with %d\n", WSAGetLastError());
    ok(ioctlsocket(Socket, FIONBIO, &NonBlocking) == 0, "before bind: FIONBIO failed with %d\n", WSAGetLastError());

    ExpectSilence(Socket, &Closed, "before bind");

    closesocket(Socket);
}

START_TEST(udp_connreset)
{
    WSADATA WsaData;
    int Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %d\n", Result);
    if (Result != 0)
        return;

    TestReportedByDefault();
    TestTurnedOff();
    TestTurnedOffBeforeBind();

    WSACleanup();
}
