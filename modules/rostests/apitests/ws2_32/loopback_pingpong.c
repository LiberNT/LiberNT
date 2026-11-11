/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test two loopback UDP sockets that send to each other at the same time
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"

#define PINGPONG_COUNT 200

typedef struct _PINGPONG_SENDER
{
    SOCKET Socket;
    struct sockaddr_in To;
    HANDLE Start;
    LONG Sent;
    int Error;
} PINGPONG_SENDER;

static DWORD WINAPI
PingPongSendThread(PVOID Parameter)
{
    PINGPONG_SENDER *Sender = Parameter;
    ULONG Sequence, Data[16];

    WaitForSingleObject(Sender->Start, 5000);
    for (Sequence = 0; Sequence < PINGPONG_COUNT; Sequence++)
    {
        Data[0] = Sequence;
        if (sendto(Sender->Socket, (const char *)Data, sizeof(Data), 0,
                   (const struct sockaddr *)&Sender->To, sizeof(Sender->To)) != sizeof(Data))
        {
            Sender->Error = WSAGetLastError();
            break;
        }
        Sender->Sent++;
    }
    return 0;
}

static SOCKET
OpenLoopbackSocket(_Out_ struct sockaddr_in *Address)
{
    SOCKET Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    DWORD Size = 1024 * 1024;
    int Length = sizeof(*Address);

    if (Socket == INVALID_SOCKET)
        return Socket;

    ZeroMemory(Address, sizeof(*Address));
    Address->sin_family = AF_INET;
    Address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    setsockopt(Socket, SOL_SOCKET, SO_RCVBUF, (const char *)&Size, sizeof(Size));
    if (bind(Socket, (const struct sockaddr *)Address, sizeof(*Address)) ||
        getsockname(Socket, (struct sockaddr *)Address, &Length))
    {
        closesocket(Socket);
        return INVALID_SOCKET;
    }
    return Socket;
}

static VOID
CheckArrivalOrder(_In_ SOCKET Socket, _In_ PCSTR Name)
{
    DWORD Timeout = 2000;
    ULONG Data[16], Expected = 0;
    int Result = 0;

    setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&Timeout, sizeof(Timeout));
    while (Expected < PINGPONG_COUNT)
    {
        Result = recv(Socket, (char *)Data, sizeof(Data), 0);
        if (Result != sizeof(Data))
            break;
        ok(Data[0] == Expected, "%s: datagram %lu arrived where %lu was expected\n", Name, Data[0], Expected);
        Expected = Data[0] + 1;
    }
    ok(Expected == PINGPONG_COUNT, "%s: %lu of %u datagrams arrived, last recv %d error %d\n",
       Name, Expected, PINGPONG_COUNT, Result, WSAGetLastError());
}

static VOID
TestLoopbackDelivery(VOID)
{
    struct sockaddr_in Address;
    struct timeval Timeout = { 1, 0 };
    SOCKET Sender, Receiver;
    fd_set ReadSet;
    char Data[8] = "ping";

    Receiver = OpenLoopbackSocket(&Address);
    Sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Receiver != INVALID_SOCKET && Sender != INVALID_SOCKET, "socket failed with %d\n", WSAGetLastError());
    if (Receiver != INVALID_SOCKET && Sender != INVALID_SOCKET)
    {
        ok(sendto(Sender, Data, sizeof(Data), 0, (const struct sockaddr *)&Address, sizeof(Address)) == sizeof(Data),
           "sendto failed with %d\n", WSAGetLastError());
        FD_ZERO(&ReadSet);
        FD_SET(Receiver, &ReadSet);
        ok(select(0, &ReadSet, NULL, NULL, &Timeout) == 1, "The datagram did not become readable\n");
    }
    if (Sender != INVALID_SOCKET) closesocket(Sender);
    if (Receiver != INVALID_SOCKET) closesocket(Receiver);
}

START_TEST(loopback_pingpong)
{
    PINGPONG_SENDER Senders[2] = { { 0 } };
    struct sockaddr_in Addresses[2];
    HANDLE Threads[2], Start;
    WSADATA WsaData;
    DWORD Result;
    int Index;

    ok(WSAStartup(MAKEWORD(2, 2), &WsaData) == 0, "WSAStartup failed\n");

    TestLoopbackDelivery();

    Start = CreateEventW(NULL, TRUE, FALSE, NULL);
    for (Index = 0; Index < 2; Index++)
    {
        Senders[Index].Socket = OpenLoopbackSocket(&Addresses[Index]);
        ok(Senders[Index].Socket != INVALID_SOCKET, "socket %d failed with %d\n", Index, WSAGetLastError());
    }
    if (Senders[0].Socket == INVALID_SOCKET || Senders[1].Socket == INVALID_SOCKET)
    {
        WSACleanup();
        return;
    }

    for (Index = 0; Index < 2; Index++)
    {
        Senders[Index].To = Addresses[1 - Index];
        Senders[Index].Start = Start;
        Threads[Index] = CreateThread(NULL, 0, PingPongSendThread, &Senders[Index], 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread %d failed with %lu\n", Index, GetLastError());
    }
    SetEvent(Start);

    Result = WaitForMultipleObjects(2, Threads, TRUE, 5000);
    ok(Result == WAIT_OBJECT_0, "The two senders did not finish: %lu\n", Result);
    for (Index = 0; Index < 2; Index++)
    {
        ok(Senders[Index].Sent == PINGPONG_COUNT, "sender %d sent %ld datagrams, error %d\n",
           Index, Senders[Index].Sent, Senders[Index].Error);
        CloseHandle(Threads[Index]);
    }

    CheckArrivalOrder(Senders[1].Socket, "socket 1");
    CheckArrivalOrder(Senders[0].Socket, "socket 0");

    closesocket(Senders[0].Socket);
    closesocket(Senders[1].Socket);
    CloseHandle(Start);
    WSACleanup();
}
