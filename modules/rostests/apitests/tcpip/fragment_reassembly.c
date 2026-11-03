/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test IPv4 fragment reassembly of complete and incomplete datagrams
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <apitest.h>
#include <ws2tcpip.h>

#define UDP_DATA_SIZE 32
#define UDP_SIZE (8 + UDP_DATA_SIZE)

typedef struct _FRAGMENT
{
    USHORT Offset;
    USHORT Length;
    BOOL More;
    UCHAR Xor;
} FRAGMENT;

typedef struct _REASSEMBLY_TEST
{
    PCSTR Name;
    BOOL Delivered;
    UINT Count;
    FRAGMENT Fragments[4];
} REASSEMBLY_TEST;

static const REASSEMBLY_TEST Tests[] =
{
    { "unfragmented", TRUE, 1, { { 0, UDP_SIZE, FALSE } } },
    { "in order", TRUE, 2, { { 0, 16, TRUE }, { 16, 24, FALSE } } },
    { "reverse order", TRUE, 2, { { 16, 24, FALSE }, { 0, 16, TRUE } } },
    { "three fragments", TRUE, 3, { { 8, 16, TRUE }, { 24, 16, FALSE }, { 0, 8, TRUE } } },
    { "duplicate", TRUE, 3, { { 0, 16, TRUE }, { 0, 16, TRUE }, { 16, 24, FALSE } } },
    { "gap", FALSE, 2, { { 0, 16, TRUE }, { 24, 16, FALSE } } },
    { "past end before last", FALSE, 3, { { 0, 16, TRUE }, { 40, 8, TRUE }, { 16, 24, FALSE } } },
    { "past end after last", FALSE, 2, { { 16, 24, FALSE }, { 0, 48, TRUE } } },
    { "overlap later fragment", FALSE, 2, { { 0, 24, TRUE }, { 16, 24, FALSE, 0x5a } } },
    { "overlap earlier fragment", FALSE, 2, { { 16, 24, FALSE }, { 0, 24, TRUE, 0x5a } } },
    { "overlap inside", TRUE, 3, { { 0, 24, TRUE }, { 8, 8, TRUE, 0x5a }, { 24, 16, FALSE } } },
    { "overlap then fill", FALSE, 3, { { 0, 16, TRUE }, { 8, 16, TRUE, 0x5a }, { 16, 24, FALSE } } },
    { "overlap superset", FALSE, 3, { { 8, 8, TRUE }, { 0, 24, TRUE, 0x5a }, { 24, 16, FALSE } } },
    { "overlap same range", TRUE, 3, { { 0, 16, TRUE }, { 0, 16, TRUE, 0x5a }, { 16, 24, FALSE } } },
    { "overlap two fragments", TRUE, 4, { { 0, 16, TRUE }, { 16, 16, TRUE }, { 8, 16, TRUE, 0x5a }, { 32, 8, FALSE } } },
};

static
USHORT
IPv4Checksum(
    const UCHAR *Header,
    UINT Length)
{
    ULONG Sum = 0;
    UINT Index;

    for (Index = 0; Index < Length; Index += 2)
        Sum += ((ULONG)Header[Index] << 8) | Header[Index + 1];

    while (Sum >> 16)
        Sum = (Sum & 0xffff) + (Sum >> 16);

    return htons((USHORT)~Sum);
}

static
BOOL
SendIPv4Fragment(
    SOCKET Socket,
    const SOCKADDR_IN *Destination,
    USHORT Identification,
    USHORT FlagsOffset,
    const UCHAR *Payload,
    USHORT PayloadSize)
{
    UCHAR Packet[20 + 64] = {0};
    USHORT Value;
    INT Result;

    Packet[0] = 0x45;
    Value = htons(20 + PayloadSize);
    memcpy(Packet + 2, &Value, sizeof(Value));
    Value = htons(Identification);
    memcpy(Packet + 4, &Value, sizeof(Value));
    Value = htons(FlagsOffset);
    memcpy(Packet + 6, &Value, sizeof(Value));
    Packet[8] = 64;
    Packet[9] = IPPROTO_UDP;
    memcpy(Packet + 12, &Destination->sin_addr, sizeof(Destination->sin_addr));
    memcpy(Packet + 16, &Destination->sin_addr, sizeof(Destination->sin_addr));
    Value = IPv4Checksum(Packet, 20);
    memcpy(Packet + 10, &Value, sizeof(Value));
    memcpy(Packet + 20, Payload, PayloadSize);

    Result = sendto(Socket, (const CHAR *)Packet, 20 + PayloadSize, 0,
                    (const SOCKADDR *)Destination, sizeof(*Destination));
    return Result == 20 + PayloadSize;
}

static
INT
WaitForUdp(
    SOCKET Socket,
    LONG Seconds)
{
    fd_set ReadSet;
    struct timeval Timeout;

    FD_ZERO(&ReadSet);
    FD_SET(Socket, &ReadSet);
    Timeout.tv_sec = Seconds;
    Timeout.tv_usec = 0;
    return select(0, &ReadSet, NULL, NULL, &Timeout);
}

static
VOID
RunReassemblyTest(
    SOCKET Sender,
    SOCKET Receiver,
    const SOCKADDR_IN *Address,
    USHORT Identification,
    const REASSEMBLY_TEST *Test)
{
    UCHAR Datagram[64] = {0};
    UCHAR Fragmented[64];
    UCHAR Buffer[64];
    USHORT Value;
    UINT Index;
    INT Result;

    Value = htons(40000);
    memcpy(Datagram, &Value, sizeof(Value));
    memcpy(Datagram + 2, &Address->sin_port, sizeof(Address->sin_port));
    Value = htons(UDP_SIZE);
    memcpy(Datagram + 4, &Value, sizeof(Value));
    for (Index = 0; Index < UDP_DATA_SIZE; Index++)
        Datagram[8 + Index] = (UCHAR)(Identification + Index);

    for (Index = 0; Index < Test->Count; Index++)
    {
        const FRAGMENT *Fragment = &Test->Fragments[Index];
        UINT Byte;

        memcpy(Fragmented, Datagram, sizeof(Fragmented));
        for (Byte = max(Fragment->Offset, 8); Byte < Fragment->Offset + Fragment->Length; Byte++)
            Fragmented[Byte] ^= Fragment->Xor;

        Result = SendIPv4Fragment(Sender, Address, Identification,
                                  (Fragment->More ? 0x2000 : 0) | (Fragment->Offset / 8),
                                  Fragmented + Fragment->Offset, Fragment->Length);
        ok(Result, "%s: fragment %u send failed with %d\n", Test->Name, Index, WSAGetLastError());
        if (!Result)
            return;
    }

    Result = WaitForUdp(Receiver, 1);
    ok(Result == (Test->Delivered ? 1 : 0), "%s: select returned %d, error %d\n",
       Test->Name, Result, WSAGetLastError());
    if (Result != 1)
        return;

    Result = recv(Receiver, (CHAR *)Buffer, sizeof(Buffer), 0);
    ok(Result == UDP_DATA_SIZE, "%s: unexpected UDP payload length %d\n", Test->Name, Result);
    if (Result == UDP_DATA_SIZE)
        ok(memcmp(Buffer, Datagram + 8, UDP_DATA_SIZE) == 0,
           "%s: unexpected reassembled UDP payload\n", Test->Name);
}

START_TEST(fragment_reassembly)
{
    WSADATA WsaData;
    SOCKET Receiver = INVALID_SOCKET;
    SOCKET Sender = INVALID_SOCKET;
    SOCKADDR_IN Address;
    INT AddressLength = sizeof(Address);
    BOOL IncludeHeader = TRUE;
    USHORT Identification;
    UINT Index;
    INT Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %d\n", Result);
    if (Result != 0)
        return;

    Receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Sender = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (Receiver == INVALID_SOCKET || Sender == INVALID_SOCKET)
    {
        skip("Raw IPv4 sockets are unavailable, error %d\n", WSAGetLastError());
        goto Cleanup;
    }

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    Result = bind(Receiver, (SOCKADDR *)&Address, sizeof(Address));
    ok(Result == 0, "UDP bind failed with %d\n", WSAGetLastError());
    if (Result != 0)
        goto Cleanup;

    Result = getsockname(Receiver, (SOCKADDR *)&Address, &AddressLength);
    ok(Result == 0, "getsockname failed with %d\n", WSAGetLastError());
    if (Result != 0)
        goto Cleanup;

    Result = setsockopt(Sender, IPPROTO_IP, IP_HDRINCL,
                        (const CHAR *)&IncludeHeader, sizeof(IncludeHeader));
    ok(Result == 0, "IP_HDRINCL failed with %d\n", WSAGetLastError());
    if (Result != 0)
        goto Cleanup;

    Identification = (USHORT)GetTickCount();
    for (Index = 0; Index < ARRAYSIZE(Tests); Index++)
        RunReassemblyTest(Sender, Receiver, &Address, Identification + Index, &Tests[Index]);

Cleanup:
    if (Sender != INVALID_SOCKET)
        closesocket(Sender);
    if (Receiver != INVALID_SOCKET)
        closesocket(Receiver);
    WSACleanup();
}
