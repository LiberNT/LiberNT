/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the WSASendMsg extension function
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "ws2_32.h"
#include <mswsock.h>
#include <iphlpapi.h>

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

typedef struct _CMSG_BUFFER
{
    union
    {
        WSACMSGHDR Align;
        CHAR Buffer[256];
    };
    ULONG Length;
} CMSG_BUFFER, *PCMSG_BUFFER;

typedef struct _RECEIVED
{
    BOOL Fenced;
    ULONG Count;
    DWORD Length[8];
    INT Ecn[8];
    INT Ttl;
    struct sockaddr_in From;
} RECEIVED, *PRECEIVED;

static LPFN_WSASENDMSG ControlSendMsg;
static LPFN_WSARECVMSG ControlRecvMsg;

static VOID
AddControl(
    _Inout_ PCMSG_BUFFER Control,
    _In_ INT Level,
    _In_ INT Type,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    WSACMSGHDR *Header = (WSACMSGHDR *)(Control->Buffer + Control->Length);

    ok(Control->Length + WSA_CMSG_SPACE(Length) <= sizeof(Control->Buffer), "control buffer overflow\n");
    ZeroMemory(Header, WSA_CMSG_SPACE(Length));
    Header->cmsg_len = WSA_CMSG_LEN(Length);
    Header->cmsg_level = Level;
    Header->cmsg_type = Type;
    if (Length)
        CopyMemory(WSA_CMSG_DATA(Header), Data, Length);
    Control->Length += (ULONG)WSA_CMSG_SPACE(Length);
}

static VOID
AddInt(
    _Inout_ PCMSG_BUFFER Control,
    _In_ INT Level,
    _In_ INT Type,
    _In_ INT Value)
{
    AddControl(Control, Level, Type, &Value, sizeof(Value));
}

static VOID
AddPktInfo(
    _Inout_ PCMSG_BUFFER Control,
    _In_ ULONG Address,
    _In_ ULONG Index)
{
    IN_PKTINFO Info;

    Info.ipi_addr.s_addr = Address;
    Info.ipi_ifindex = Index;
    AddControl(Control, IPPROTO_IP, IP_PKTINFO, &Info, sizeof(Info));
}

static int
SendControlBuffers(
    _In_ SOCKET Socket,
    _In_opt_ const struct sockaddr_in *To,
    _In_reads_bytes_opt_(ControlLength) CHAR *ControlBuffer,
    _In_ ULONG ControlLength,
    _In_ LPWSABUF Buffers,
    _In_ DWORD BufferCount,
    _Out_ PDWORD Bytes)
{
    WSAMSG Message;

    ZeroMemory(&Message, sizeof(Message));
    Message.name = (LPSOCKADDR)To;
    Message.namelen = To ? sizeof(*To) : 0;
    Message.lpBuffers = Buffers;
    Message.dwBufferCount = BufferCount;
    Message.Control.buf = ControlBuffer;
    Message.Control.len = ControlLength;
    *Bytes = 0xdeadbeef;
    return ControlSendMsg(Socket, &Message, 0, Bytes, NULL, NULL) ? WSAGetLastError() : 0;
}

static int
SendControlRaw(
    _In_ SOCKET Socket,
    _In_opt_ const struct sockaddr_in *To,
    _In_reads_bytes_opt_(ControlLength) CHAR *ControlBuffer,
    _In_ ULONG ControlLength,
    _In_ ULONG Payload,
    _Out_ PDWORD Bytes)
{
    static CHAR Data[1500];
    WSABUF Buffer;

    FillMemory(Data, sizeof(Data), 'x');
    Buffer.buf = Data;
    Buffer.len = Payload;
    return SendControlBuffers(Socket, To, ControlBuffer, ControlLength, &Buffer, 1, Bytes);
}

static int
SendControl(
    _In_ SOCKET Socket,
    _In_opt_ const struct sockaddr_in *To,
    _In_opt_ PCMSG_BUFFER Control,
    _In_ ULONG Payload,
    _Out_ PDWORD Bytes)
{
    return SendControlRaw(Socket, To, Control ? Control->Buffer : NULL, Control ? Control->Length : 0, Payload, Bytes);
}

static VOID
ReceiveFenced(
    _In_ SOCKET Receiver,
    _In_ SOCKET Sender,
    _In_ const struct sockaddr_in *To,
    _Out_ PRECEIVED Received)
{
    struct timeval Timeout = { 2, 0 };
    CHAR Data[1500], ControlBuffer[256];
    WSACMSGHDR *Header;
    WSABUF Buffer;
    WSAMSG Message;
    fd_set ReadSet;
    DWORD Bytes;
    INT Ecn, Ttl;

    ZeroMemory(Received, sizeof(*Received));
    Received->Ttl = -1;
    if (sendto(Sender, "F", 1, 0, (const struct sockaddr *)To, sizeof(*To)) != 1)
        return;
    for (;;)
    {
        FD_ZERO(&ReadSet);
        FD_SET(Receiver, &ReadSet);
        if (select(0, &ReadSet, NULL, NULL, &Timeout) != 1)
            return;
        Buffer.buf = Data;
        Buffer.len = sizeof(Data);
        ZeroMemory(&Message, sizeof(Message));
        Message.name = (LPSOCKADDR)&Received->From;
        Message.namelen = sizeof(Received->From);
        Message.lpBuffers = &Buffer;
        Message.dwBufferCount = 1;
        Message.Control.buf = ControlBuffer;
        Message.Control.len = sizeof(ControlBuffer);
        if (ControlRecvMsg(Receiver, &Message, &Bytes, NULL, NULL))
            return;
        if (Bytes == 1 && Data[0] == 'F')
        {
            Received->Fenced = TRUE;
            return;
        }
        Ecn = Ttl = -1;
        for (Header = WSA_CMSG_FIRSTHDR(&Message); Header; Header = WSA_CMSG_NXTHDR(&Message, Header))
        {
            if (Header->cmsg_level == IPPROTO_IP && Header->cmsg_type == IP_ECN)
                Ecn = *(INT *)WSA_CMSG_DATA(Header);
            else if (Header->cmsg_level == IPPROTO_IP && Header->cmsg_type == IP_TTL)
                Ttl = *(INT *)WSA_CMSG_DATA(Header);
        }
        if (Received->Count < ARRAYSIZE(Received->Length))
        {
            Received->Length[Received->Count] = Bytes;
            Received->Ecn[Received->Count] = Ecn;
        }
        if (!Received->Count)
            Received->Ttl = Ttl;
        Received->Count++;
    }
}

static SOCKET
ControlReceiver(
    _In_ ULONG Address,
    _Out_ struct sockaddr_in *Bound)
{
    SOCKET Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    DWORD One = 1;
    int Length = sizeof(*Bound);

    ok(Socket != INVALID_SOCKET, "receiver socket failed, error %d\n", WSAGetLastError());
    ZeroMemory(Bound, sizeof(*Bound));
    Bound->sin_family = AF_INET;
    Bound->sin_addr.s_addr = Address;
    ok(!bind(Socket, (struct sockaddr *)Bound, sizeof(*Bound)), "receiver bind failed, error %d\n", WSAGetLastError());
    ok(!getsockname(Socket, (struct sockaddr *)Bound, &Length), "receiver getsockname failed, error %d\n", WSAGetLastError());
    ok(!setsockopt(Socket, IPPROTO_IP, IP_PKTINFO, (char *)&One, sizeof(One)), "IP_PKTINFO failed, error %d\n", WSAGetLastError());
    ok(!setsockopt(Socket, IPPROTO_IP, IP_RECVECN, (char *)&One, sizeof(One)), "IP_RECVECN failed, error %d\n", WSAGetLastError());
    ok(!setsockopt(Socket, IPPROTO_IP, IP_RECVTTL, (char *)&One, sizeof(One)), "IP_RECVTTL failed, error %d\n", WSAGetLastError());
    if (Address == htonl(INADDR_ANY))
        Bound->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return Socket;
}

static SOCKET
ControlSender(
    _In_ ULONG Address)
{
    struct sockaddr_in Local;
    SOCKET Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    ok(Socket != INVALID_SOCKET, "sender socket failed, error %d\n", WSAGetLastError());
    ZeroMemory(&Local, sizeof(Local));
    Local.sin_family = AF_INET;
    Local.sin_addr.s_addr = Address;
    ok(!bind(Socket, (struct sockaddr *)&Local, sizeof(Local)), "sender bind failed, error %d\n", WSAGetLastError());
    return Socket;
}

#define EXPECT_SEND(Tag, Error, ExpectedError) \
    ok((Error) == (int)(ExpectedError), "%s: send error %d, expected %d\n", Tag, Error, (int)(ExpectedError))

static VOID
ExpectDelivered(
    _In_ PCSTR Tag,
    _In_ SOCKET Receiver,
    _In_ SOCKET Sender,
    _In_ const struct sockaddr_in *To,
    _In_ ULONG ExpectedCount,
    _In_ ULONG SegmentLength,
    _In_ ULONG Total,
    _In_ INT ExpectedEcn)
{
    RECEIVED Received;
    ULONG i, Expected;

    ReceiveFenced(Receiver, Sender, To, &Received);
    ok(Received.Fenced, "%s: the fence did not arrive\n", Tag);
    ok(Received.Count == ExpectedCount, "%s: %lu datagrams arrived, expected %lu\n", Tag, Received.Count, ExpectedCount);
    for (i = 0; i < min(Received.Count, ARRAYSIZE(Received.Length)); i++)
    {
        Expected = SegmentLength ? min(SegmentLength, Total - i * SegmentLength) : Total;
        ok(Received.Length[i] == Expected, "%s: datagram %lu has %lu bytes, expected %lu\n", Tag, i, Received.Length[i], Expected);
        ok(Received.Ecn[i] == ExpectedEcn, "%s: datagram %lu has ECN %d, expected %d\n", Tag, i, Received.Ecn[i], ExpectedEcn);
    }
}

static VOID
ExpectNothing(
    _In_ PCSTR Tag,
    _In_ SOCKET Receiver,
    _In_ SOCKET Sender,
    _In_ const struct sockaddr_in *To)
{
    RECEIVED Received;

    ReceiveFenced(Receiver, Sender, To, &Received);
    ok(Received.Fenced, "%s: the fence did not arrive\n", Tag);
    ok(Received.Count == 0, "%s: %lu datagrams arrived, expected none\n", Tag, Received.Count);
}

static VOID
ExpectSource(
    _In_ PCSTR Tag,
    _In_ SOCKET Receiver,
    _In_ SOCKET Sender,
    _In_ const struct sockaddr_in *To,
    _In_ ULONG Source)
{
    RECEIVED Received;

    ReceiveFenced(Receiver, Sender, To, &Received);
    ok(Received.Fenced, "%s: the fence did not arrive\n", Tag);
    ok(Received.Count == 1, "%s: %lu datagrams arrived, expected 1\n", Tag, Received.Count);
    ok(Received.From.sin_addr.s_addr == Source, "%s: source %08lx, expected %08lx\n",
       Tag, (ULONG)ntohl(Received.From.sin_addr.s_addr), (ULONG)ntohl(Source));
}

static VOID
TraceSourceLate(
    _In_ PCSTR Tag,
    _In_ SOCKET Receiver)
{
    struct timeval Timeout = { 1, 0 };
    struct sockaddr_in From;
    int Length = sizeof(From), Result;
    fd_set ReadSet;
    CHAR Data[16];

    FD_ZERO(&ReadSet);
    FD_SET(Receiver, &ReadSet);
    Result = select(0, &ReadSet, NULL, NULL, &Timeout);
    ZeroMemory(&From, sizeof(From));
    if (Result == 1)
        Result = recvfrom(Receiver, Data, sizeof(Data), 0, (struct sockaddr *)&From, &Length);
    trace("%s: delivery %d from %08lx\n", Tag, Result, (ULONG)ntohl(From.sin_addr.s_addr));
}

static ULONG
ExpectTtl(
    _In_ PCSTR Tag,
    _In_ SOCKET Receiver,
    _In_ SOCKET Sender,
    _In_ const struct sockaddr_in *To,
    _In_ INT ExpectedTtl)
{
    RECEIVED Received;

    ReceiveFenced(Receiver, Sender, To, &Received);
    ok(Received.Fenced, "%s: the fence did not arrive\n", Tag);
    ok(Received.Count == 1, "%s: %lu datagrams arrived, expected 1\n", Tag, Received.Count);
    if (ExpectedTtl >= 0)
        ok(Received.Ttl == ExpectedTtl, "%s: TTL %d, expected %d\n", Tag, Received.Ttl, ExpectedTtl);
    return Received.Ttl;
}

static VOID
TestControlFormat(
    _In_ SOCKET Receiver,
    _In_ const struct sockaddr_in *To,
    _In_ SOCKET Sender)
{
    static const struct { INT Level, Type; } Unknown[] =
    {
        { SOL_SOCKET, 1234 }, { SOL_SOCKET, SO_BROADCAST }, { IPPROTO_IP, IP_TTL }, { IPPROTO_IP, IP_TCLASS },
        { IPPROTO_IP, 1234 }, { IPPROTO_IPV6, IPV6_PKTINFO }, { IPPROTO_IPV6, IP_ECN }, { IPPROTO_IPV6, IPV6_TCLASS },
        { IPPROTO_UDP, UDP_RECV_MAX_COALESCED_SIZE }, { IPPROTO_UDP, 1234 },
    };
    CMSG_BUFFER Control;
    WSACMSGHDR *Header;
    CHAR Zeros[sizeof(WSACMSGHDR)];
    CHAR Tag[64];
    DWORD Bytes;
    BOOL Wow64 = FALSE;
    int Error;
    ULONG i;

    ok(IsWow64Process(GetCurrentProcess(), &Wow64), "IsWow64Process failed, error %lu\n", GetLastError());
    ZeroMemory(Zeros, sizeof(Zeros));
    Error = SendControlRaw(Sender, To, NULL, sizeof(WSACMSGHDR), 1, &Bytes);
    EXPECT_SEND("NULL control buffer", Error, WSAEFAULT);
    Error = SendControlRaw(Sender, To, NULL, 1, 1, &Bytes);
    EXPECT_SEND("NULL control buffer of 1 byte", Error, WSAEFAULT);
    Error = SendControlRaw(Sender, To, (CHAR *)(ULONG_PTR)0x10, sizeof(WSACMSGHDR), 1, &Bytes);
    EXPECT_SEND("bad control pointer", Error, WSAEFAULT);
    Error = SendControlRaw(Sender, To, (CHAR *)(ULONG_PTR)0x10, 1, 1, &Bytes);
    EXPECT_SEND("bad control pointer of 1 byte", Error, Wow64 ? WSAEINVAL : WSAEFAULT);
    Error = SendControlRaw(Sender, To, Zeros, 0, 1, &Bytes);
    EXPECT_SEND("empty control buffer", Error, WSAEFAULT);
    ExpectNothing("control errors", Receiver, Sender, To);

    Error = SendControlRaw(Sender, To, Zeros, 1, 1, &Bytes);
    EXPECT_SEND("1 byte of control", Error, Wow64 ? WSAEINVAL : 0);
    if (!Error)
    {
        ok(Bytes == 1, "1 byte of control: %lu bytes sent\n", Bytes);
        ExpectDelivered("1 byte of control", Receiver, Sender, To, 1, 0, 1, 0);
    }
    Error = SendControlRaw(Sender, To, Zeros, sizeof(WSACMSGHDR) - 1, 1, &Bytes);
    EXPECT_SEND("control shorter than a header", Error, Wow64 ? WSAEINVAL : 0);
    if (!Error)
        ExpectDelivered("control shorter than a header", Receiver, Sender, To, 1, 0, 1, 0);
    Error = SendControlRaw(Sender, To, Zeros, sizeof(WSACMSGHDR), 1, &Bytes);
    EXPECT_SEND("zero cmsg_len", Error, WSAEINVAL);

    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_ECN, 1);
    Header = (WSACMSGHDR *)Control.Buffer;
    Header->cmsg_len = sizeof(WSACMSGHDR) - 1;
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("cmsg_len shorter than a header", Error, WSAEINVAL);
    Header->cmsg_len = Control.Length + 1;
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("cmsg_len past the buffer", Error, WSAEINVAL);
    Header->cmsg_len = 0x7fffffff;
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("huge cmsg_len", Error, WSAEINVAL);
    Header->cmsg_len = WSA_CMSG_LEN(sizeof(INT)) + 1;
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("cmsg_len padded within its space", Error,
                WSA_CMSG_LEN(sizeof(INT)) + 1 <= WSA_CMSG_SPACE(sizeof(INT)) ? 0 : WSAEINVAL);
    if (!Error)
        ExpectDelivered("cmsg_len padded within its space", Receiver, Sender, To, 1, 0, 1, 1);
    Header->cmsg_len = WSA_CMSG_LEN(sizeof(INT));
    Error = SendControlRaw(Sender, To, Control.Buffer, (ULONG)WSA_CMSG_LEN(sizeof(INT)), 1, &Bytes);
    EXPECT_SEND("control ending at cmsg_len", Error,
                WSA_CMSGHDR_ALIGN(WSA_CMSG_LEN(sizeof(INT))) > WSA_CMSG_LEN(sizeof(INT)) ? WSAEINVAL : 0);
    if (!Error)
        ExpectDelivered("control ending at cmsg_len", Receiver, Sender, To, 1, 0, 1, 1);
    Error = SendControlRaw(Sender, To, Control.Buffer, Control.Length + sizeof(WSACMSGHDR) - 1, 1, &Bytes);
    EXPECT_SEND("trailing bytes shorter than a header", Error, Wow64 ? WSAEINVAL : 0);
    if (!Error)
        ExpectDelivered("trailing bytes shorter than a header", Receiver, Sender, To, 1, 0, 1, 1);
    ZeroMemory(Control.Buffer + Control.Length, sizeof(WSACMSGHDR));
    Error = SendControlRaw(Sender, To, Control.Buffer, Control.Length + sizeof(WSACMSGHDR), 1, &Bytes);
    EXPECT_SEND("second header with zero cmsg_len", Error, WSAEINVAL);
    AddInt(&Control, IPPROTO_IP, IP_ECN, 2);
    Error = SendControlRaw(Sender, To, Control.Buffer, Control.Length - 1, 1, &Bytes);
    EXPECT_SEND("second message cut short", Error, WSAEINVAL);

    for (i = 0; i < ARRAYSIZE(Unknown); i++)
    {
        sprintf(Tag, "unknown control %d/%d", Unknown[i].Level, Unknown[i].Type);
        Control.Length = 0;
        AddInt(&Control, Unknown[i].Level, Unknown[i].Type, 1);
        Error = SendControl(Sender, To, &Control, 1, &Bytes);
        EXPECT_SEND(Tag, Error, WSAEINVAL);
    }
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_ECN, 1);
    AddInt(&Control, IPPROTO_IP, 1234, 1);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("ECN then unknown control", Error, WSAEINVAL);
    ExpectNothing("format errors", Receiver, Sender, To);
}

static VOID
TestControlEcnTos(
    _In_ SOCKET Receiver,
    _In_ const struct sockaddr_in *To,
    _In_ SOCKET Sender)
{
    static const INT Bad[] = { 3, 4, -1, 0x101 };
    CMSG_BUFFER Control;
    CHAR Tag[64];
    DWORD Bytes;
    ULONGLONG Wide;
    USHORT Narrow;
    UCHAR Byte;
    int Error;
    INT i;

    for (i = 0; i <= 2; i++)
    {
        sprintf(Tag, "IP_ECN %d", i);
        Control.Length = 0;
        AddInt(&Control, IPPROTO_IP, IP_ECN, i);
        Error = SendControl(Sender, To, &Control, 1, &Bytes);
        EXPECT_SEND(Tag, Error, 0);
        ok(Bytes == 1, "%s: %lu bytes sent\n", Tag, Bytes);
        ExpectDelivered(Tag, Receiver, Sender, To, 1, 0, 1, i);
    }
    for (i = 0; i < (INT)ARRAYSIZE(Bad); i++)
    {
        sprintf(Tag, "IP_ECN %d", Bad[i]);
        Control.Length = 0;
        AddInt(&Control, IPPROTO_IP, IP_ECN, Bad[i]);
        Error = SendControl(Sender, To, &Control, 1, &Bytes);
        EXPECT_SEND(Tag, Error, WSAEINVAL);
    }
    Narrow = 2;
    Control.Length = 0;
    AddControl(&Control, IPPROTO_IP, IP_ECN, &Narrow, sizeof(Narrow));
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("2-byte IP_ECN", Error, WSAEINVAL);
    Byte = 2;
    Control.Length = 0;
    AddControl(&Control, IPPROTO_IP, IP_ECN, &Byte, sizeof(Byte));
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("1-byte IP_ECN", Error, WSAEINVAL);
    ExpectNothing("bad IP_ECN", Receiver, Sender, To);

    Wide = 0x100000002ull;
    Control.Length = 0;
    AddControl(&Control, IPPROTO_IP, IP_ECN, &Wide, sizeof(Wide));
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("8-byte IP_ECN", Error, 0);
    ExpectDelivered("8-byte IP_ECN", Receiver, Sender, To, 1, 0, 1, 2);

    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_ECN, 1);
    AddInt(&Control, IPPROTO_IP, IP_ECN, 2);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_ECN 1 then 2", Error, 0);
    ExpectDelivered("IP_ECN 1 then 2", Receiver, Sender, To, 1, 0, 1, 2);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_ECN, 3);
    AddInt(&Control, IPPROTO_IP, IP_ECN, 1);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_ECN 3 then 1", Error, WSAEINVAL);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_ECN, 1);
    AddInt(&Control, IPPROTO_IP, IP_ECN, 3);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_ECN 1 then 3", Error, WSAEINVAL);
    ExpectNothing("bad IP_ECN pairs", Receiver, Sender, To);

    Error = SendControl(Sender, To, NULL, 1, &Bytes);
    EXPECT_SEND("ECN does not stick", Error, 0);
    ExpectDelivered("ECN does not stick", Receiver, Sender, To, 1, 0, 1, 0);

    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_TOS, 0x2a);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_TOS 0x2a", Error, 0);
    ExpectDelivered("IP_TOS 0x2a", Receiver, Sender, To, 1, 0, 1, 0);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_TOS, -1);
    AddInt(&Control, IPPROTO_IP, IP_ECN, 1);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_TOS -1 with IP_ECN 1", Error, 0);
    ExpectDelivered("IP_TOS -1 with IP_ECN 1", Receiver, Sender, To, 1, 0, 1, 1);
    Byte = 0x28;
    Control.Length = 0;
    AddControl(&Control, IPPROTO_IP, IP_TOS, &Byte, sizeof(Byte));
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("1-byte IP_TOS", Error, WSAEINVAL);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_TOS, 1);
    AddControl(&Control, IPPROTO_IP, IP_TOS, &Byte, sizeof(Byte));
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_TOS then 1-byte IP_TOS", Error, WSAEINVAL);
    ExpectNothing("bad IP_TOS", Receiver, Sender, To);
}

static VOID
TestControlHopLimit(
    _In_ SOCKET Receiver,
    _In_ const struct sockaddr_in *To,
    _In_ SOCKET Sender)
{
    static const INT Good[] = { 5, 0, 255 };
    static const INT Bad[] = { -1, -2, 256 };
    CMSG_BUFFER Control;
    CHAR Tag[64];
    DWORD Bytes, Ttl;
    ULONGLONG Wide;
    UCHAR Byte;
    ULONG DefaultTtl;
    int Error;
    ULONG i;

    Error = SendControl(Sender, To, NULL, 1, &Bytes);
    EXPECT_SEND("default TTL", Error, 0);
    DefaultTtl = ExpectTtl("default TTL", Receiver, Sender, To, -1);
    ok(DefaultTtl == 128, "default TTL is %lu\n", DefaultTtl);

    for (i = 0; i < ARRAYSIZE(Good); i++)
    {
        sprintf(Tag, "IP_HOPLIMIT %d", Good[i]);
        Control.Length = 0;
        AddInt(&Control, IPPROTO_IP, IP_HOPLIMIT, Good[i]);
        Error = SendControl(Sender, To, &Control, 1, &Bytes);
        EXPECT_SEND(Tag, Error, 0);
        ExpectTtl(Tag, Receiver, Sender, To, Good[i]);
    }
    for (i = 0; i < ARRAYSIZE(Bad); i++)
    {
        sprintf(Tag, "IP_HOPLIMIT %d", Bad[i]);
        Control.Length = 0;
        AddInt(&Control, IPPROTO_IP, IP_HOPLIMIT, Bad[i]);
        Error = SendControl(Sender, To, &Control, 1, &Bytes);
        EXPECT_SEND(Tag, Error, WSAEINVAL);
    }
    Byte = 5;
    Control.Length = 0;
    AddControl(&Control, IPPROTO_IP, IP_HOPLIMIT, &Byte, sizeof(Byte));
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("1-byte IP_HOPLIMIT", Error, WSAEINVAL);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_HOPLIMIT, 300);
    AddInt(&Control, IPPROTO_IP, IP_HOPLIMIT, 5);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_HOPLIMIT 300 then 5", Error, WSAEINVAL);
    ExpectNothing("bad IP_HOPLIMIT", Receiver, Sender, To);

    Wide = 5;
    Control.Length = 0;
    AddControl(&Control, IPPROTO_IP, IP_HOPLIMIT, &Wide, sizeof(Wide));
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("8-byte IP_HOPLIMIT", Error, 0);
    ExpectTtl("8-byte IP_HOPLIMIT", Receiver, Sender, To, 5);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_HOPLIMIT, 5);
    AddInt(&Control, IPPROTO_IP, IP_HOPLIMIT, 6);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_HOPLIMIT 5 then 6", Error, 0);
    ExpectTtl("IP_HOPLIMIT 5 then 6", Receiver, Sender, To, 5);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_HOPLIMIT, 5);
    AddInt(&Control, IPPROTO_IP, IP_HOPLIMIT, 300);
    AddControl(&Control, IPPROTO_IP, IP_HOPLIMIT, &Byte, sizeof(Byte));
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_HOPLIMIT 5 then bad ones", Error, 0);
    ExpectTtl("IP_HOPLIMIT 5 then bad ones", Receiver, Sender, To, 5);

    Ttl = 9;
    ok(!setsockopt(Sender, IPPROTO_IP, IP_TTL, (char *)&Ttl, sizeof(Ttl)), "IP_TTL failed, error %d\n", WSAGetLastError());
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_HOPLIMIT, 5);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_HOPLIMIT over IP_TTL", Error, 0);
    ExpectTtl("IP_HOPLIMIT over IP_TTL", Receiver, Sender, To, 5);
    Error = SendControl(Sender, To, NULL, 1, &Bytes);
    EXPECT_SEND("IP_TTL after IP_HOPLIMIT", Error, 0);
    ExpectTtl("IP_TTL after IP_HOPLIMIT", Receiver, Sender, To, 9);
    Ttl = DefaultTtl;
    ok(!setsockopt(Sender, IPPROTO_IP, IP_TTL, (char *)&Ttl, sizeof(Ttl)), "IP_TTL failed, error %d\n", WSAGetLastError());
}

static VOID
TestControlSendMsgSize(
    _In_ SOCKET Receiver,
    _In_ const struct sockaddr_in *To,
    _In_ SOCKET Sender)
{
    static const INT Bad[] = { 65508, 0xFFFF, 0x10000, -1 };
    CMSG_BUFFER Control;
    CHAR Tag[64];
    WSABUF Buffers[2];
    DWORD Bytes, Size;
    ULONGLONG Wide;
    USHORT Narrow;
    int Error;
    ULONG i;

    Control.Length = 0;
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 2);
    Error = SendControl(Sender, To, &Control, 5, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE 2 over 5", Error, 0);
    ok(Bytes == 5, "UDP_SEND_MSG_SIZE 2 over 5: %lu bytes sent\n", Bytes);
    ExpectDelivered("UDP_SEND_MSG_SIZE 2 over 5", Receiver, Sender, To, 3, 2, 5, 0);
    Error = SendControl(Sender, To, &Control, 4, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE 2 over 4", Error, 0);
    ExpectDelivered("UDP_SEND_MSG_SIZE 2 over 4", Receiver, Sender, To, 2, 2, 4, 0);
    Error = SendControl(Sender, To, &Control, 0, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE 2 over 0", Error, 0);
    ok(Bytes == 0, "UDP_SEND_MSG_SIZE 2 over 0: %lu bytes sent\n", Bytes);
    ExpectDelivered("UDP_SEND_MSG_SIZE 2 over 0", Receiver, Sender, To, 1, 0, 0, 0);

    Buffers[0].buf = "abc";
    Buffers[0].len = 3;
    Buffers[1].buf = "de";
    Buffers[1].len = 2;
    Error = SendControlBuffers(Sender, To, Control.Buffer, Control.Length, Buffers, 2, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE 2 over two buffers", Error, 0);
    ok(Bytes == 5, "UDP_SEND_MSG_SIZE 2 over two buffers: %lu bytes sent\n", Bytes);
    ExpectDelivered("UDP_SEND_MSG_SIZE 2 over two buffers", Receiver, Sender, To, 3, 2, 5, 0);

    AddInt(&Control, IPPROTO_IP, IP_ECN, 1);
    Error = SendControl(Sender, To, &Control, 5, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE with IP_ECN", Error, 0);
    ExpectDelivered("UDP_SEND_MSG_SIZE with IP_ECN", Receiver, Sender, To, 3, 2, 5, 1);

    Control.Length = 0;
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 0);
    Error = SendControl(Sender, To, &Control, 5, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE 0", Error, 0);
    ExpectDelivered("UDP_SEND_MSG_SIZE 0", Receiver, Sender, To, 1, 0, 5, 0);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 65507);
    Error = SendControl(Sender, To, &Control, 5, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE 65507", Error, 0);
    ExpectDelivered("UDP_SEND_MSG_SIZE 65507", Receiver, Sender, To, 1, 0, 5, 0);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 1000);
    Error = SendControl(Sender, To, &Control, 1400, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE 1000 over 1400", Error, 0);
    ExpectDelivered("UDP_SEND_MSG_SIZE 1000 over 1400", Receiver, Sender, To, 2, 1000, 1400, 0);

    for (i = 0; i < ARRAYSIZE(Bad); i++)
    {
        sprintf(Tag, "UDP_SEND_MSG_SIZE %d", Bad[i]);
        Control.Length = 0;
        AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, Bad[i]);
        Error = SendControl(Sender, To, &Control, 5, &Bytes);
        EXPECT_SEND(Tag, Error, WSAEINVAL);
    }
    Narrow = 2;
    Control.Length = 0;
    AddControl(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, &Narrow, sizeof(Narrow));
    Error = SendControl(Sender, To, &Control, 5, &Bytes);
    EXPECT_SEND("2-byte UDP_SEND_MSG_SIZE", Error, WSAEINVAL);
    Wide = 2;
    Control.Length = 0;
    AddControl(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, &Wide, sizeof(Wide));
    Error = SendControl(Sender, To, &Control, 5, &Bytes);
    EXPECT_SEND("8-byte UDP_SEND_MSG_SIZE", Error, WSAEINVAL);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 2);
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 0x10000);
    Error = SendControl(Sender, To, &Control, 5, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE 2 then bad", Error, WSAEINVAL);
    ExpectNothing("bad UDP_SEND_MSG_SIZE", Receiver, Sender, To);

    Control.Length = 0;
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 0x10000);
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 2);
    Error = SendControl(Sender, To, &Control, 5, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE bad then 2", Error, 0);
    ExpectDelivered("UDP_SEND_MSG_SIZE bad then 2", Receiver, Sender, To, 3, 2, 5, 0);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 2);
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 3);
    Error = SendControl(Sender, To, &Control, 5, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE 2 then 3", Error, 0);
    ExpectDelivered("UDP_SEND_MSG_SIZE 2 then 3", Receiver, Sender, To, 2, 3, 5, 0);

    Size = 3;
    ok(!setsockopt(Sender, IPPROTO_UDP, UDP_SEND_MSG_SIZE, (char *)&Size, sizeof(Size)),
       "UDP_SEND_MSG_SIZE failed, error %d\n", WSAGetLastError());
    Control.Length = 0;
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 2);
    Error = SendControl(Sender, To, &Control, 5, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE 2 over the socket's 3", Error, 0);
    ExpectDelivered("UDP_SEND_MSG_SIZE 2 over the socket's 3", Receiver, Sender, To, 3, 2, 5, 0);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 0);
    Error = SendControl(Sender, To, &Control, 5, &Bytes);
    EXPECT_SEND("UDP_SEND_MSG_SIZE 0 over the socket's 3", Error, 0);
    ExpectDelivered("UDP_SEND_MSG_SIZE 0 over the socket's 3", Receiver, Sender, To, 1, 0, 5, 0);
    Error = SendControl(Sender, To, NULL, 5, &Bytes);
    EXPECT_SEND("the socket's 3 alone", Error, 0);
    ExpectDelivered("the socket's 3 alone", Receiver, Sender, To, 2, 3, 5, 0);
    Size = 0;
    ok(!setsockopt(Sender, IPPROTO_UDP, UDP_SEND_MSG_SIZE, (char *)&Size, sizeof(Size)),
       "UDP_SEND_MSG_SIZE failed, error %d\n", WSAGetLastError());
}

static BOOL
FindAdapterAddress(
    _Out_ IN_ADDR *Address,
    _Out_ PULONG Index,
    _Out_ PULONG PrefixLength)
{
    PIP_ADAPTER_ADDRESSES List, Adapter;
    PIP_ADAPTER_UNICAST_ADDRESS Unicast;
    ULONG Size = 0, Flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    BOOL Found = FALSE;

    if (GetAdaptersAddresses(AF_INET, Flags, NULL, NULL, &Size) != ERROR_BUFFER_OVERFLOW)
        return FALSE;
    List = HeapAlloc(GetProcessHeap(), 0, Size);
    if (List && !GetAdaptersAddresses(AF_INET, Flags, NULL, List, &Size))
    {
        for (Adapter = List; Adapter && !Found; Adapter = Adapter->Next)
        {
            if (Adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || Adapter->OperStatus != IfOperStatusUp)
                continue;
            for (Unicast = Adapter->FirstUnicastAddress; Unicast && !Found; Unicast = Unicast->Next)
            {
                struct sockaddr_in *Sin = (struct sockaddr_in *)Unicast->Address.lpSockaddr;

                if (Sin->sin_family == AF_INET && (ntohl(Sin->sin_addr.s_addr) >> 16) != 0xa9fe)
                {
                    *Address = Sin->sin_addr;
                    *Index = Adapter->IfIndex;
                    *PrefixLength = Unicast->OnLinkPrefixLength;
                    Found = TRUE;
                }
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, List);
    return Found;
}

static ULONG
TestControlPktInfo(
    _In_ SOCKET Receiver,
    _In_ const struct sockaddr_in *To)
{
    static const struct { PCSTR Address; ULONG Index; int Error; } Bad[] =
    {
        { "127.0.0.2", 0, WSAEINVAL }, { "192.0.2.1", 0, WSAEINVAL }, { "224.0.0.2", 0, WSAEINVAL },
        { "255.255.255.255", 0, WSAEADDRNOTAVAIL }, { "0.0.0.0", 9999, WSAEINVAL }, { "127.0.0.1", 9999, WSAEINVAL },
    };
    struct sockaddr_in AnyTo, Local;
    SOCKET AnyReceiver, Sender, Bound, Connected, Unbound;
    CMSG_BUFFER Control;
    CHAR Tag[64], Wide[16];
    IN_PKTINFO Info;
    RECEIVED Received;
    ULONG Loopback = htonl(INADDR_LOOPBACK), LoopbackIndex, i;
    DWORD Bytes;
    int Error, Length;

    AnyReceiver = ControlReceiver(htonl(INADDR_ANY), &AnyTo);
    Sender = ControlSender(htonl(INADDR_ANY));

    Error = SendControl(Sender, To, NULL, 1, &Bytes);
    EXPECT_SEND("plain send", Error, 0);
    ExpectSource("plain send", Receiver, Sender, To, Loopback);
    {
        CHAR Data[4], ControlBuffer[256];
        struct sockaddr_in From;
        WSACMSGHDR *Header;
        WSABUF Buffer;
        WSAMSG Message;

        LoopbackIndex = 0;
        ok(sendto(Sender, "x", 1, 0, (const struct sockaddr *)To, sizeof(*To)) == 1, "sendto failed, error %d\n", WSAGetLastError());
        Buffer.buf = Data;
        Buffer.len = sizeof(Data);
        ZeroMemory(&Message, sizeof(Message));
        Message.name = (LPSOCKADDR)&From;
        Message.namelen = sizeof(From);
        Message.lpBuffers = &Buffer;
        Message.dwBufferCount = 1;
        Message.Control.buf = ControlBuffer;
        Message.Control.len = sizeof(ControlBuffer);
        ok(!ControlRecvMsg(Receiver, &Message, &Bytes, NULL, NULL), "WSARecvMsg failed, error %d\n", WSAGetLastError());
        for (Header = WSA_CMSG_FIRSTHDR(&Message); Header; Header = WSA_CMSG_NXTHDR(&Message, Header))
        {
            if (Header->cmsg_level == IPPROTO_IP && Header->cmsg_type == IP_PKTINFO)
                LoopbackIndex = ((IN_PKTINFO *)WSA_CMSG_DATA(Header))->ipi_ifindex;
        }
        ok(LoopbackIndex != 0, "no loopback interface index\n");
    }

    Control.Length = 0;
    AddPktInfo(&Control, Loopback, 0);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_PKTINFO 127.0.0.1", Error, 0);
    ok(Bytes == 1, "IP_PKTINFO 127.0.0.1: %lu bytes sent\n", Bytes);
    ExpectSource("IP_PKTINFO 127.0.0.1", Receiver, Sender, To, Loopback);
    Control.Length = 0;
    AddPktInfo(&Control, Loopback, LoopbackIndex);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_PKTINFO 127.0.0.1 on loopback", Error, 0);
    ExpectSource("IP_PKTINFO 127.0.0.1 on loopback", Receiver, Sender, To, Loopback);
    Unbound = ControlSender(htonl(INADDR_ANY));
    Control.Length = 0;
    AddPktInfo(&Control, 0, LoopbackIndex);
    Error = SendControl(Unbound, &AnyTo, &Control, 1, &Bytes);
    EXPECT_SEND("IP_PKTINFO 0.0.0.0 on loopback", Error, 0);
    TraceSourceLate("IP_PKTINFO 0.0.0.0 on loopback", AnyReceiver);
    closesocket(Unbound);
    Error = SendControl(Sender, To, NULL, 1, &Bytes);
    EXPECT_SEND("IP_PKTINFO does not stick", Error, 0);
    ExpectSource("IP_PKTINFO does not stick", Receiver, Sender, To, Loopback);

    for (i = 0; i < ARRAYSIZE(Bad); i++)
    {
        sprintf(Tag, "IP_PKTINFO %s/%lu", Bad[i].Address, Bad[i].Index);
        Control.Length = 0;
        AddPktInfo(&Control, inet_addr(Bad[i].Address), Bad[i].Index);
        Error = SendControl(Sender, To, &Control, 1, &Bytes);
        EXPECT_SEND(Tag, Error, Bad[i].Error);
    }
    {
        IN_ADDR Adapter;
        ULONG Index, PrefixLength;

        Control.Length = 0;
        AddPktInfo(&Control, inet_addr("224.0.0.1"), 0);
        Error = SendControl(Sender, To, &Control, 1, &Bytes);
        EXPECT_SEND("IP_PKTINFO 224.0.0.1", Error, FindAdapterAddress(&Adapter, &Index, &PrefixLength) ? WSAEADDRNOTAVAIL : WSAEINVAL);
    }
    Control.Length = 0;
    AddPktInfo(&Control, inet_addr("192.0.2.1"), LoopbackIndex);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_PKTINFO 192.0.2.1 on loopback", Error, WSAEADDRNOTAVAIL);
    Control.Length = 0;
    AddPktInfo(&Control, inet_addr("127.0.0.2"), LoopbackIndex);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_PKTINFO 127.0.0.2 on loopback", Error, WSAEADDRNOTAVAIL);

    Info.ipi_addr.s_addr = Loopback;
    Info.ipi_ifindex = 0;
    Control.Length = 0;
    AddControl(&Control, IPPROTO_IP, IP_PKTINFO, &Info, 4);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("4-byte IP_PKTINFO", Error, WSAEINVAL);
    Control.Length = 0;
    AddPktInfo(&Control, inet_addr("192.0.2.1"), 0);
    AddPktInfo(&Control, Loopback, 0);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_PKTINFO bad then good", Error, WSAEINVAL);
    ExpectNothing("bad IP_PKTINFO", Receiver, Sender, To);

    ZeroMemory(Wide, sizeof(Wide));
    CopyMemory(Wide, &Info, sizeof(Info));
    Control.Length = 0;
    AddControl(&Control, IPPROTO_IP, IP_PKTINFO, Wide, sizeof(Wide));
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("16-byte IP_PKTINFO", Error, 0);
    ExpectSource("16-byte IP_PKTINFO", Receiver, Sender, To, Loopback);
    Control.Length = 0;
    AddPktInfo(&Control, Loopback, 0);
    AddPktInfo(&Control, inet_addr("192.0.2.1"), 9999);
    AddControl(&Control, IPPROTO_IP, IP_PKTINFO, &Info, 4);
    Error = SendControl(Sender, To, &Control, 1, &Bytes);
    EXPECT_SEND("IP_PKTINFO good then bad ones", Error, 0);
    ExpectSource("IP_PKTINFO good then bad ones", Receiver, Sender, To, Loopback);

    Bound = ControlSender(Loopback);
    Control.Length = 0;
    AddPktInfo(&Control, inet_addr("192.0.2.1"), 0);
    Error = SendControl(Bound, To, &Control, 1, &Bytes);
    EXPECT_SEND("bound IP_PKTINFO 192.0.2.1", Error, 0);
    ExpectSource("bound IP_PKTINFO 192.0.2.1", Receiver, Bound, To, Loopback);
    Control.Length = 0;
    AddPktInfo(&Control, 0, 9999);
    Error = SendControl(Bound, To, &Control, 1, &Bytes);
    EXPECT_SEND("bound IP_PKTINFO index 9999", Error, 0);
    ExpectSource("bound IP_PKTINFO index 9999", Receiver, Bound, To, Loopback);
    Control.Length = 0;
    AddControl(&Control, IPPROTO_IP, IP_PKTINFO, &Info, 4);
    Error = SendControl(Bound, To, &Control, 1, &Bytes);
    EXPECT_SEND("bound 4-byte IP_PKTINFO", Error, 0);
    ExpectSource("bound 4-byte IP_PKTINFO", Receiver, Bound, To, Loopback);
    closesocket(Bound);

    Connected = ControlSender(htonl(INADDR_ANY));
    ok(!connect(Connected, (const struct sockaddr *)To, sizeof(*To)), "connect failed, error %d\n", WSAGetLastError());
    Length = sizeof(Local);
    ok(!getsockname(Connected, (struct sockaddr *)&Local, &Length), "getsockname failed, error %d\n", WSAGetLastError());
    ok(Local.sin_addr.s_addr == Loopback, "connected local address %08lx\n", (ULONG)ntohl(Local.sin_addr.s_addr));
    Control.Length = 0;
    AddPktInfo(&Control, inet_addr("192.0.2.1"), 9999);
    Error = SendControl(Connected, NULL, &Control, 1, &Bytes);
    EXPECT_SEND("connected IP_PKTINFO 192.0.2.1", Error, 0);
    ExpectSource("connected IP_PKTINFO 192.0.2.1", Receiver, Connected, To, Loopback);
    Error = SendControl(Connected, To, &Control, 1, &Bytes);
    EXPECT_SEND("connected IP_PKTINFO with a name", Error, WSAEINVAL);
    Control.Length = 0;
    AddPktInfo(&Control, Loopback, 0);
    Error = SendControl(Connected, To, &Control, 1, &Bytes);
    EXPECT_SEND("connected IP_PKTINFO 127.0.0.1 with a name", Error, 0);
    ExpectSource("connected IP_PKTINFO 127.0.0.1 with a name", Receiver, Connected, To, Loopback);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_ECN, 2);
    AddInt(&Control, IPPROTO_IP, IP_HOPLIMIT, 7);
    Error = SendControl(Connected, NULL, &Control, 1, &Bytes);
    EXPECT_SEND("connected IP_ECN", Error, 0);
    ReceiveFenced(Receiver, Connected, To, &Received);
    ok(Received.Fenced && Received.Count == 1 && Received.Ecn[0] == 2 && Received.Ttl == 7,
       "connected IP_ECN: fenced %d, %lu datagrams, ECN %d, TTL %d\n", Received.Fenced, Received.Count, Received.Ecn[0], Received.Ttl);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 2);
    Error = SendControl(Connected, NULL, &Control, 5, &Bytes);
    EXPECT_SEND("connected UDP_SEND_MSG_SIZE", Error, 0);
    ExpectDelivered("connected UDP_SEND_MSG_SIZE", Receiver, Connected, To, 3, 2, 5, 0);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, 1234, 0);
    Error = SendControl(Connected, NULL, &Control, 1, &Bytes);
    EXPECT_SEND("connected unknown control", Error, WSAEINVAL);
    ExpectNothing("connected errors", Receiver, Connected, To);
    Control.Length = 0;
    AddControl(&Control, IPPROTO_IP, IP_PKTINFO, &Info, 4);
    Error = SendControl(Connected, NULL, &Control, 1, &Bytes);
    EXPECT_SEND("connected 4-byte IP_PKTINFO", Error, 0);
    if (!Error)
        ExpectSource("connected 4-byte IP_PKTINFO", Receiver, Connected, To, Loopback);
    closesocket(Connected);

    Unbound = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Error = SendControl(Unbound, NULL, NULL, 1, &Bytes);
    EXPECT_SEND("unconnected NULL name", Error, WSAEINVAL);
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, IP_ECN, 1);
    Error = SendControl(Unbound, NULL, &Control, 1, &Bytes);
    EXPECT_SEND("unconnected NULL name with IP_ECN", Error, WSAEINVAL);
    Control.Length = 0;
    AddPktInfo(&Control, inet_addr("192.0.2.1"), 0);
    Error = SendControl(Unbound, To, &Control, 1, &Bytes);
    EXPECT_SEND("unbound bad IP_PKTINFO", Error, WSAEINVAL);
    Length = sizeof(Local);
    ok(!getsockname(Unbound, (struct sockaddr *)&Local, &Length) && Local.sin_port != 0,
       "unbound bad IP_PKTINFO did not bind the socket, error %d\n", WSAGetLastError());
    closesocket(Unbound);

    closesocket(Sender);
    closesocket(AnyReceiver);
    return LoopbackIndex;
}

static VOID
TestControlOverlapped(
    _In_ SOCKET Receiver,
    _In_ const struct sockaddr_in *To)
{
    WSAOVERLAPPED Overlapped;
    CMSG_BUFFER Control;
    WSABUF Buffer;
    WSAMSG Message;
    DWORD Bytes, Flags;
    RECEIVED Received;
    int Result, Error;
    SOCKET Socket = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);

    ok(Socket != INVALID_SOCKET, "overlapped socket failed, error %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
        return;
    ZeroMemory(&Overlapped, sizeof(Overlapped));
    Overlapped.hEvent = WSACreateEvent();
    Buffer.buf = "o";
    Buffer.len = 1;
    ZeroMemory(&Message, sizeof(Message));
    Message.name = (LPSOCKADDR)To;
    Message.namelen = sizeof(*To);
    Message.lpBuffers = &Buffer;
    Message.dwBufferCount = 1;

    Control.Length = 0;
    AddPktInfo(&Control, htonl(INADDR_LOOPBACK), 0);
    AddInt(&Control, IPPROTO_IP, IP_ECN, 1);
    Message.Control.buf = Control.Buffer;
    Message.Control.len = Control.Length;
    Result = ControlSendMsg(Socket, &Message, 0, &Bytes, &Overlapped, NULL);
    Error = Result ? WSAGetLastError() : 0;
    ok(Result == 0 || Error == WSA_IO_PENDING, "overlapped: returned %d, error %d\n", Result, Error);
    Flags = 0;
    ok(WSAGetOverlappedResult(Socket, &Overlapped, &Bytes, TRUE, &Flags) && Bytes == 1,
       "overlapped: completion failed, error %d, bytes %lu\n", WSAGetLastError(), Bytes);
    ReceiveFenced(Receiver, Socket, To, &Received);
    ok(Received.Fenced && Received.Count == 1 && Received.Ecn[0] == 1,
       "overlapped: fenced %d, %lu datagrams, ECN %d\n", Received.Fenced, Received.Count, Received.Ecn[0]);

    Control.Length = 0;
    AddPktInfo(&Control, 0, 9999);
    Message.Control.len = Control.Length;
    Result = ControlSendMsg(Socket, &Message, 0, &Bytes, &Overlapped, NULL);
    Error = Result ? WSAGetLastError() : 0;
    ok(Result == SOCKET_ERROR && Error == WSA_IO_PENDING, "overlapped bad IP_PKTINFO: returned %d, error %d\n", Result, Error);
    if (Error == WSA_IO_PENDING)
    {
        Flags = 0;
        Result = WSAGetOverlappedResult(Socket, &Overlapped, &Bytes, TRUE, &Flags);
        Error = Result ? 0 : WSAGetLastError();
        ok(!Result && Error == WSAEINVAL, "overlapped bad IP_PKTINFO: completed %d, error %d\n", Result, Error);
    }
    Control.Length = 0;
    AddInt(&Control, IPPROTO_IP, 1234, 1);
    Message.Control.len = Control.Length;
    Result = ControlSendMsg(Socket, &Message, 0, &Bytes, &Overlapped, NULL);
    Error = Result ? WSAGetLastError() : 0;
    ok(Result == SOCKET_ERROR && Error == WSA_IO_PENDING, "overlapped unknown control: returned %d, error %d\n", Result, Error);
    if (Error == WSA_IO_PENDING)
    {
        Flags = 0;
        Result = WSAGetOverlappedResult(Socket, &Overlapped, &Bytes, TRUE, &Flags);
        Error = Result ? 0 : WSAGetLastError();
        ok(!Result && Error == WSAEINVAL, "overlapped unknown control: completed %d, error %d\n", Result, Error);
    }
    Message.Control.buf = NULL;
    Message.Control.len = sizeof(WSACMSGHDR);
    Result = ControlSendMsg(Socket, &Message, 0, &Bytes, &Overlapped, NULL);
    Error = Result ? WSAGetLastError() : 0;
    ok(Result == SOCKET_ERROR && Error == WSAEFAULT, "overlapped NULL control: returned %d, error %d\n", Result, Error);
    ExpectNothing("overlapped errors", Receiver, Socket, To);

    WSACloseEvent(Overlapped.hEvent);
    closesocket(Socket);
}

static VOID
TestControlAdapter(
    _In_ ULONG LoopbackIndex)
{
    struct sockaddr_in AnyTo, AdapterTo, LoopbackTo;
    SOCKET AnyReceiver, Sender, Bound;
    CMSG_BUFFER Control;
    IN_ADDR Adapter;
    ULONG Index, PrefixLength, Loopback = htonl(INADDR_LOOPBACK), Broadcast;
    DWORD Bytes;
    int Error;

    if (!FindAdapterAddress(&Adapter, &Index, &PrefixLength))
    {
        skip("no network adapter with an IPv4 address\n");
        return;
    }
    trace("adapter address %s, index %lu\n", inet_ntoa(Adapter), Index);
    AnyReceiver = ControlReceiver(htonl(INADDR_ANY), &AnyTo);
    Sender = ControlSender(htonl(INADDR_ANY));
    LoopbackTo = AnyTo;
    AdapterTo = AnyTo;
    AdapterTo.sin_addr = Adapter;

    Error = SendControl(Sender, &AdapterTo, NULL, 1, &Bytes);
    EXPECT_SEND("to the adapter", Error, 0);
    ExpectSource("to the adapter", AnyReceiver, Sender, &AdapterTo, Adapter.s_addr);
    Control.Length = 0;
    AddPktInfo(&Control, Adapter.s_addr, 0);
    Error = SendControl(Sender, &AdapterTo, &Control, 1, &Bytes);
    EXPECT_SEND("to the adapter from it", Error, 0);
    ExpectSource("to the adapter from it", AnyReceiver, Sender, &AdapterTo, Adapter.s_addr);
    Bound = ControlSender(htonl(INADDR_ANY));
    Control.Length = 0;
    AddPktInfo(&Control, 0, Index);
    Error = SendControl(Bound, &AdapterTo, &Control, 1, &Bytes);
    EXPECT_SEND("to the adapter on it", Error, 0);
    TraceSourceLate("to the adapter on it", AnyReceiver);
    closesocket(Bound);
    Broadcast = Adapter.s_addr | htonl(PrefixLength && PrefixLength < 32 ? 0xFFFFFFFF >> PrefixLength : 0);
    Control.Length = 0;
    AddPktInfo(&Control, Broadcast, 0);
    Error = SendControl(Sender, &AdapterTo, &Control, 1, &Bytes);
    EXPECT_SEND("from the subnet broadcast address", Error, WSAEADDRNOTAVAIL);
    Control.Length = 0;
    AddPktInfo(&Control, Loopback, 0);
    Error = SendControl(Sender, &AdapterTo, &Control, 1, &Bytes);
    EXPECT_SEND("to the adapter from loopback", Error, WSAENETUNREACH);
    Control.Length = 0;
    AddPktInfo(&Control, 0, LoopbackIndex);
    Error = SendControl(Sender, &AdapterTo, &Control, 1, &Bytes);
    EXPECT_SEND("to the adapter on loopback", Error, WSAENETUNREACH);
    Control.Length = 0;
    AddPktInfo(&Control, Adapter.s_addr, LoopbackIndex);
    Error = SendControl(Sender, &AdapterTo, &Control, 1, &Bytes);
    EXPECT_SEND("adapter address on loopback", Error, WSAEADDRNOTAVAIL);
    Control.Length = 0;
    AddPktInfo(&Control, Loopback, Index);
    Error = SendControl(Sender, &AdapterTo, &Control, 1, &Bytes);
    EXPECT_SEND("loopback address on the adapter", Error, WSAEADDRNOTAVAIL);
    Control.Length = 0;
    AddPktInfo(&Control, Adapter.s_addr, 0);
    Error = SendControl(Sender, &LoopbackTo, &Control, 1, &Bytes);
    EXPECT_SEND("to loopback from the adapter", Error, WSAEADDRNOTAVAIL);
    Control.Length = 0;
    AddPktInfo(&Control, 0, Index);
    Error = SendControl(Sender, &LoopbackTo, &Control, 1, &Bytes);
    EXPECT_SEND("to loopback on the adapter", Error, WSAEADDRNOTAVAIL);
    Control.Length = 0;
    AddPktInfo(&Control, Adapter.s_addr, 0);
    AddPktInfo(&Control, Loopback, 0);
    Error = SendControl(Sender, &AdapterTo, &Control, 1, &Bytes);
    EXPECT_SEND("adapter then loopback", Error, 0);
    ExpectSource("adapter then loopback", AnyReceiver, Sender, &AdapterTo, Adapter.s_addr);
    ExpectNothing("adapter errors", AnyReceiver, Sender, &AdapterTo);

    Bound = ControlSender(Adapter.s_addr);
    Control.Length = 0;
    AddPktInfo(&Control, Loopback, 0);
    Error = SendControl(Bound, &AdapterTo, &Control, 1, &Bytes);
    EXPECT_SEND("adapter-bound from loopback", Error, 0);
    ExpectSource("adapter-bound from loopback", AnyReceiver, Bound, &AdapterTo, Adapter.s_addr);
    closesocket(Bound);
    closesocket(Sender);
    closesocket(AnyReceiver);
}

static VOID
TestControl(VOID)
{
    GUID SendGuid = WSAID_WSASENDMSG, RecvGuid = WSAID_WSARECVMSG;
    struct sockaddr_in To;
    SOCKET Receiver, Sender;
    ULONG LoopbackIndex;
    DWORD Bytes;

    Receiver = ControlReceiver(htonl(INADDR_LOOPBACK), &To);
    Sender = ControlSender(htonl(INADDR_ANY));
    ok(!WSAIoctl(Receiver, SIO_GET_EXTENSION_FUNCTION_POINTER, &SendGuid, sizeof(SendGuid),
                 &ControlSendMsg, sizeof(ControlSendMsg), &Bytes, NULL, NULL), "WSASendMsg lookup failed\n");
    ok(!WSAIoctl(Receiver, SIO_GET_EXTENSION_FUNCTION_POINTER, &RecvGuid, sizeof(RecvGuid),
                 &ControlRecvMsg, sizeof(ControlRecvMsg), &Bytes, NULL, NULL), "WSARecvMsg lookup failed\n");
    if (ControlSendMsg && ControlRecvMsg)
    {
        TestControlFormat(Receiver, &To, Sender);
        TestControlEcnTos(Receiver, &To, Sender);
        TestControlHopLimit(Receiver, &To, Sender);
        TestControlSendMsgSize(Receiver, &To, Sender);
        LoopbackIndex = TestControlPktInfo(Receiver, &To);
        TestControlOverlapped(Receiver, &To);
        TestControlAdapter(LoopbackIndex);
    }
    closesocket(Sender);
    closesocket(Receiver);
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
    TestControl();
    WSACleanup();
}
