/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that datagrams arriving before a receive is posted are kept in order up to the receive buffer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ws2_32.h"

#define BURST_COUNT 64
#define CONCURRENT_COUNT 256
#define FAILURE_TIMEOUT_MS 2000

typedef struct _READER_CONTEXT
{
    SOCKET Socket;
    int Expected;
    int Received;
    int InOrder;
} READER_CONTEXT, *PREADER_CONTEXT;

static
BOOL
OpenLoopbackSocket(
    _Out_ SOCKET *Socket,
    _Out_ struct sockaddr_in *Address)
{
    int Length = sizeof(*Address);

    ZeroMemory(Address, sizeof(*Address));
    Address->sin_family = AF_INET;
    Address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    *Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (*Socket == INVALID_SOCKET)
        return FALSE;

    if (bind(*Socket, (const struct sockaddr *)Address, sizeof(*Address)) != 0 ||
        getsockname(*Socket, (struct sockaddr *)Address, &Length) != 0)
    {
        closesocket(*Socket);
        *Socket = INVALID_SOCKET;
        return FALSE;
    }

    return TRUE;
}

static
BOOL
SetReceiveTimeout(
    _In_ SOCKET Socket,
    _In_ DWORD Milliseconds)
{
    return setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&Milliseconds, sizeof(Milliseconds)) == 0;
}

static
BOOL
SendNumbered(
    _In_ SOCKET Sender,
    _In_ const struct sockaddr_in *To,
    _In_ DWORD Number,
    _In_ int Size)
{
    CHAR Data[1000];

    memset(Data, (UCHAR)Number, Size);
    memcpy(Data, &Number, sizeof(Number));
    return sendto(Sender, Data, Size, 0, (const struct sockaddr *)To, sizeof(*To)) == Size;
}

static
DWORD
WINAPI
ReaderThread(
    _In_ LPVOID Parameter)
{
    PREADER_CONTEXT Context = Parameter;
    CHAR Data[64];
    DWORD Number;

    while (Context->Received < Context->Expected)
    {
        if (recv(Context->Socket, Data, sizeof(Data), 0) != sizeof(Data))
            break;
        memcpy(&Number, Data, sizeof(Number));
        if (Number == (DWORD)Context->Received)
            Context->InOrder++;
        Context->Received++;
    }

    return 0;
}

static
void
TestBurstBeforeReceive(void)
{
    struct sockaddr_in To, From;
    SOCKET Receiver, Sender;
    CHAR Data[100];
    DWORD Number;
    int Index, Sent = 0, Received = 0, InOrder = 0;

    if (!OpenLoopbackSocket(&Receiver, &To))
    {
        ok(0, "burst: receiver socket failed with %d\n", WSAGetLastError());
        return;
    }
    if (!OpenLoopbackSocket(&Sender, &From))
    {
        ok(0, "burst: sender socket failed with %d\n", WSAGetLastError());
        closesocket(Receiver);
        return;
    }

    for (Index = 0; Index < BURST_COUNT; Index++)
        Sent += SendNumbered(Sender, &To, Index, sizeof(Data));
    ok(Sent == BURST_COUNT, "burst: %d of %d sends succeeded, error %d\n", Sent, BURST_COUNT, WSAGetLastError());

    ok(SetReceiveTimeout(Receiver, FAILURE_TIMEOUT_MS), "burst: SO_RCVTIMEO failed with %d\n", WSAGetLastError());
    while (Received < Sent)
    {
        if (recv(Receiver, Data, sizeof(Data), 0) != sizeof(Data))
            break;
        memcpy(&Number, Data, sizeof(Number));
        if (Number == (DWORD)Received)
            InOrder++;
        Received++;
    }
    ok(Received == Sent, "burst: %d of %d datagrams arrived, error %d\n", Received, Sent, WSAGetLastError());
    ok(InOrder == Received, "burst: %d of %d datagrams arrived in order\n", InOrder, Received);

    closesocket(Sender);
    closesocket(Receiver);
}

static
void
TestBurstPastReceiveBuffer(void)
{
    struct sockaddr_in To, From, FenceTo;
    SOCKET Receiver, Sender, Fence;
    CHAR Data[1000];
    DWORD Size = 4096, Number;
    u_long Mode;
    int Index, Sent = 0, Received = 0, InOrder = 0, Error;

    if (!OpenLoopbackSocket(&Receiver, &To))
    {
        ok(0, "overflow: receiver socket failed with %d\n", WSAGetLastError());
        return;
    }
    if (!OpenLoopbackSocket(&Sender, &From))
    {
        ok(0, "overflow: sender socket failed with %d\n", WSAGetLastError());
        closesocket(Receiver);
        return;
    }
    if (!OpenLoopbackSocket(&Fence, &FenceTo))
    {
        ok(0, "overflow: fence socket failed with %d\n", WSAGetLastError());
        closesocket(Sender);
        closesocket(Receiver);
        return;
    }

    ok(setsockopt(Receiver, SOL_SOCKET, SO_RCVBUF, (const char *)&Size, sizeof(Size)) == 0,
       "overflow: SO_RCVBUF failed with %d\n", WSAGetLastError());

    for (Index = 0; Index < BURST_COUNT; Index++)
        Sent += SendNumbered(Sender, &To, Index, sizeof(Data));
    ok(Sent == BURST_COUNT, "overflow: %d of %d sends succeeded, error %d\n", Sent, BURST_COUNT, WSAGetLastError());

    /* The fence is sent after the burst, so once it arrives every datagram of the burst has been delivered or dropped */
    ok(SendNumbered(Sender, &FenceTo, 0, sizeof(Number)), "overflow: fence send failed with %d\n", WSAGetLastError());
    ok(SetReceiveTimeout(Fence, FAILURE_TIMEOUT_MS), "overflow: SO_RCVTIMEO failed with %d\n", WSAGetLastError());
    ok(recv(Fence, Data, sizeof(Data), 0) == sizeof(Number), "overflow: fence recv failed with %d\n", WSAGetLastError());

    Mode = 1;
    ok(ioctlsocket(Receiver, FIONBIO, &Mode) == 0, "overflow: FIONBIO failed with %d\n", WSAGetLastError());
    for (;;)
    {
        if (recv(Receiver, Data, sizeof(Data), 0) != sizeof(Data))
            break;
        memcpy(&Number, Data, sizeof(Number));
        if (Number == (DWORD)Received)
            InOrder++;
        Received++;
    }
    Error = WSAGetLastError();
    ok(Error == WSAEWOULDBLOCK, "overflow: drain ended with %d\n", Error);
    ok(Received > 0 && Received < Sent, "overflow: %d of %d datagrams were kept\n", Received, Sent);
    ok(InOrder == Received, "overflow: %d of %d kept datagrams are the first ones in order\n", InOrder, Received);

    Mode = 0;
    ok(ioctlsocket(Receiver, FIONBIO, &Mode) == 0, "overflow: FIONBIO failed with %d\n", WSAGetLastError());
    ok(SetReceiveTimeout(Receiver, FAILURE_TIMEOUT_MS), "overflow: SO_RCVTIMEO failed with %d\n", WSAGetLastError());
    ok(SendNumbered(Sender, &To, BURST_COUNT, sizeof(Data)), "overflow: recovery send failed with %d\n", WSAGetLastError());
    Number = 0;
    if (recv(Receiver, Data, sizeof(Data), 0) == sizeof(Data))
        memcpy(&Number, Data, sizeof(Number));
    ok(Number == BURST_COUNT, "overflow: recovery recv returned %lu, error %d\n", Number, WSAGetLastError());

    closesocket(Fence);
    closesocket(Sender);
    closesocket(Receiver);
}

static
void
TestBurstDuringReceive(void)
{
    struct sockaddr_in To, From;
    READER_CONTEXT Context = { 0 };
    SOCKET Sender;
    HANDLE Thread;
    int Index, Sent = 0;

    if (!OpenLoopbackSocket(&Context.Socket, &To))
    {
        ok(0, "concurrent: receiver socket failed with %d\n", WSAGetLastError());
        return;
    }
    if (!OpenLoopbackSocket(&Sender, &From))
    {
        ok(0, "concurrent: sender socket failed with %d\n", WSAGetLastError());
        closesocket(Context.Socket);
        return;
    }
    ok(SetReceiveTimeout(Context.Socket, FAILURE_TIMEOUT_MS), "concurrent: SO_RCVTIMEO failed with %d\n", WSAGetLastError());

    Context.Expected = CONCURRENT_COUNT;
    Thread = CreateThread(NULL, 0, ReaderThread, &Context, 0, NULL);
    ok(Thread != NULL, "concurrent: CreateThread failed with %lu\n", GetLastError());
    if (Thread == NULL)
    {
        closesocket(Sender);
        closesocket(Context.Socket);
        return;
    }

    for (Index = 0; Index < CONCURRENT_COUNT; Index++)
        Sent += SendNumbered(Sender, &To, Index, 64);
    ok(Sent == CONCURRENT_COUNT, "concurrent: %d of %d sends succeeded, error %d\n", Sent, CONCURRENT_COUNT, WSAGetLastError());

    ok(WaitForSingleObject(Thread, FAILURE_TIMEOUT_MS * 2) == WAIT_OBJECT_0, "concurrent: reader did not finish\n");
    ok(Context.Received == Sent, "concurrent: %d of %d datagrams arrived\n", Context.Received, Sent);
    ok(Context.InOrder == Context.Received, "concurrent: %d of %d datagrams arrived in order\n", Context.InOrder, Context.Received);

    closesocket(Sender);
    closesocket(Context.Socket);
    WaitForSingleObject(Thread, FAILURE_TIMEOUT_MS);
    CloseHandle(Thread);
}

START_TEST(udp_queue)
{
    WSADATA WsaData;
    int Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok(Result == 0, "WSAStartup failed with %d\n", Result);
    if (Result != 0)
        return;

    TestBurstBeforeReceive();
    TestBurstPastReceiveBuffer();
    TestBurstDuringReceive();

    WSACleanup();
}
