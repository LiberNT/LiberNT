/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test delivery of received IPv4 datagrams to raw sockets
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <apitest.h>
#include <ws2tcpip.h>

#define EXPERIMENTAL_PROTOCOL 253

static
SOCKET
OpenBound(
    INT Type,
    INT Protocol,
    SOCKADDR_IN *Address)
{
    SOCKET Socket;
    INT Length = sizeof(*Address);

    Socket = socket(AF_INET, Type, Protocol);
    if (Socket == INVALID_SOCKET)
        return INVALID_SOCKET;

    ZeroMemory(Address, sizeof(*Address));
    Address->sin_family = AF_INET;
    Address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(Socket, (SOCKADDR *)Address, sizeof(*Address)) != 0 ||
        getsockname(Socket, (SOCKADDR *)Address, &Length) != 0)
    {
        ok(0, "bind of type %d protocol %d failed with %d\n", Type, Protocol, WSAGetLastError());
        closesocket(Socket);
        return INVALID_SOCKET;
    }

    return Socket;
}

static
INT
ReceiveWithin(
    SOCKET Socket,
    UCHAR *Buffer,
    INT Size)
{
    fd_set ReadSet;
    struct timeval Timeout = { 1, 0 };

    FD_ZERO(&ReadSet);
    FD_SET(Socket, &ReadSet);
    if (select(0, &ReadSet, NULL, NULL, &Timeout) != 1)
        return -1;

    return recv(Socket, (CHAR *)Buffer, Size, 0);
}

static
VOID
CheckRawDatagram(
    PCSTR Name,
    const UCHAR *Buffer,
    INT Length,
    UCHAR Protocol,
    const VOID *Payload,
    INT PayloadSize)
{
    ok(Length == 20 + PayloadSize, "%s: received %d bytes\n", Name, Length);
    if (Length != 20 + PayloadSize)
        return;

    ok(Buffer[0] == 0x45, "%s: version and header length are 0x%x\n", Name, Buffer[0]);
    ok(Buffer[9] == Protocol, "%s: protocol is %u\n", Name, Buffer[9]);
    ok(memcmp(Buffer + 20, Payload, PayloadSize) == 0, "%s: unexpected payload\n", Name);
}

START_TEST(raw_receive)
{
    WSADATA WsaData;
    SOCKET Raw = INVALID_SOCKET, Sender = INVALID_SOCKET, Udp = INVALID_SOCKET;
    SOCKADDR_IN RawAddress, UdpAddress;
    UCHAR Datagram[16];
    UCHAR Buffer[128];
    USHORT Value;
    INT Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %d\n", Result);
    if (Result != 0)
        return;

    Raw = OpenBound(SOCK_RAW, EXPERIMENTAL_PROTOCOL, &RawAddress);
    Sender = socket(AF_INET, SOCK_RAW, EXPERIMENTAL_PROTOCOL);
    if (Raw == INVALID_SOCKET || Sender == INVALID_SOCKET)
    {
        skip("Raw IPv4 sockets are unavailable, error %d\n", WSAGetLastError());
        goto Cleanup;
    }

    Result = sendto(Sender, "LiberNT!", 8, 0, (const SOCKADDR *)&RawAddress, sizeof(RawAddress));
    ok(Result == 8, "raw sendto returned %d, error %d\n", Result, WSAGetLastError());
    Result = ReceiveWithin(Raw, Buffer, sizeof(Buffer));
    CheckRawDatagram("unknown protocol", Buffer, Result, EXPERIMENTAL_PROTOCOL, "LiberNT!", 8);
    closesocket(Raw);
    closesocket(Sender);

    Raw = OpenBound(SOCK_RAW, IPPROTO_UDP, &RawAddress);
    Udp = OpenBound(SOCK_DGRAM, IPPROTO_UDP, &UdpAddress);
    Sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (Raw == INVALID_SOCKET || Udp == INVALID_SOCKET || Sender == INVALID_SOCKET)
        goto Cleanup;

    Result = sendto(Sender, "LiberNT!", 8, 0, (const SOCKADDR *)&UdpAddress, sizeof(UdpAddress));
    ok(Result == 8, "UDP sendto returned %d, error %d\n", Result, WSAGetLastError());
    Result = ReceiveWithin(Udp, Buffer, sizeof(Buffer));
    ok(Result == 8, "UDP socket received %d bytes\n", Result);
    Result = ReceiveWithin(Raw, Buffer, sizeof(Buffer));
    ok(Result == -1, "raw UDP socket received %d bytes\n", Result);

    Value = htons(40000);
    memcpy(Datagram, &Value, sizeof(Value));
    memcpy(Datagram + 2, &UdpAddress.sin_port, sizeof(UdpAddress.sin_port));
    Value = htons(sizeof(Datagram));
    memcpy(Datagram + 4, &Value, sizeof(Value));
    ZeroMemory(Datagram + 6, 2);
    memcpy(Datagram + 8, "LiberNT!", 8);
    Result = sendto(Raw, (const CHAR *)Datagram, sizeof(Datagram), 0, (const SOCKADDR *)&UdpAddress, sizeof(UdpAddress));
    ok(Result == sizeof(Datagram), "raw UDP sendto returned %d, error %d\n", Result, WSAGetLastError());
    Result = ReceiveWithin(Udp, Buffer, sizeof(Buffer));
    ok(Result == 8, "UDP socket received %d bytes from the raw UDP socket\n", Result);
    if (Result == 8)
        ok(memcmp(Buffer, "LiberNT!", 8) == 0, "UDP payload from the raw UDP socket mismatch\n");

Cleanup:
    if (Raw != INVALID_SOCKET)
        closesocket(Raw);
    if (Sender != INVALID_SOCKET)
        closesocket(Sender);
    if (Udp != INVALID_SOCKET)
        closesocket(Udp);
    WSACleanup();
}
