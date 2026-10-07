/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Polled Ethernet, ARP, IPv4, ICMP echo, UDP and DHCP client
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdnet.h"

#define ETH_HEADER_SIZE         14
#define ETH_TYPE_IPV4           0x0800
#define ETH_TYPE_ARP            0x0806

#define ARP_SIZE                28
#define ARP_REQUEST             1
#define ARP_REPLY               2
#define ARP_ENTRIES             4

#define IP_HEADER_SIZE          20
#define IP_PROTOCOL_ICMP        1
#define IP_PROTOCOL_UDP         17
#define IP_FLAG_MORE            0x2000
#define IP_OFFSET_MASK          0x1FFF

#define UDP_HEADER_SIZE         8
#define ICMP_ECHO_REQUEST       8
#define ICMP_ECHO_REPLY         0

#define DHCP_CLIENT_PORT        68
#define DHCP_SERVER_PORT        67
#define DHCP_FIXED_SIZE         236
#define DHCP_MAGIC              0x63825363
#define DHCP_DISCOVER           1
#define DHCP_OFFER              2
#define DHCP_REQUEST            3
#define DHCP_ACK                5
#define DHCP_NAK                6
#define DHCP_OPTION_MASK        1
#define DHCP_OPTION_ROUTER      3
#define DHCP_OPTION_DNS         6
#define DHCP_OPTION_REQUESTED   50
#define DHCP_OPTION_LEASE       51
#define DHCP_OPTION_TYPE        53
#define DHCP_OPTION_SERVER      54
#define DHCP_OPTION_PARAMETERS  55
#define DHCP_OPTION_RENEWAL     58
#define DHCP_OPTION_CLIENT_ID   61
#define DHCP_OPTION_END         255

#define DNS_PORT                53
#define DNS_HEADER_SIZE         12
#define DNS_FLAG_RESPONSE       0x8000
#define DNS_FLAG_RECURSE        0x0100
#define DNS_FLAG_ERROR          0x000F
#define DNS_TYPE_A              1
#define DNS_CLASS_IN            1
#define DNS_RETRY               (2 * KDNET_SECOND)
#define DNS_REFRESH             (300 * KDNET_SECOND)

typedef enum _KDNET_DHCP_STATE
{
    DhcpInit,
    DhcpSelecting,
    DhcpRequesting,
    DhcpBound,
    DhcpRenewing
} KDNET_DHCP_STATE;

typedef struct _KDNET_ARP_ENTRY
{
    ULONG Ip;
    UCHAR Address[MAC_ADDRESS_SIZE];
    ULONG64 RequestTime;
    BOOLEAN Valid;
} KDNET_ARP_ENTRY, *PKDNET_ARP_ENTRY;

ULONG KdIpAddress;
ULONG KdIpGateway;
ULONG KdIpMask;

static const UCHAR KdpBroadcast[MAC_ADDRESS_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static KDNET_ARP_ENTRY KdpArp[ARP_ENTRIES];
static ULONG KdpArpNext;
static USHORT KdpIpIdentification;

static KDNET_DHCP_STATE KdpDhcpState;
static ULONG KdpDhcpXid;
static ULONG KdpDhcpServer;
static ULONG KdpDhcpOffered;
static ULONG64 KdpDhcpNext;
static ULONG64 KdpDhcpRenew;
static ULONG64 KdpDhcpExpire;
static ULONG KdpDhcpBackoff;

static ULONG KdpDnsServer;
static USHORT KdpDnsIdentifier;
static USHORT KdpDnsPort;
static ULONG64 KdpDnsNext;

static
USHORT
KdpChecksum(
    _In_ ULONG Sum,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    while (Length > 1)
    {
        Sum += KdNetGet16(Data);
        Data += 2;
        Length -= 2;
    }

    if (Length)
        Sum += (ULONG)Data[0] << 8;

    while (Sum >> 16)
        Sum = (Sum & 0xFFFF) + (Sum >> 16);

    return (USHORT)~Sum;
}

static
ULONG
KdpPseudoSum(
    _In_ ULONG Source,
    _In_ ULONG Destination,
    _In_ ULONG Length)
{
    return (Source >> 16) + (Source & 0xFFFF) + (Destination >> 16) + (Destination & 0xFFFF) +
           IP_PROTOCOL_UDP + Length;
}

static
PKDNET_ARP_ENTRY
KdpArpFind(
    _In_ ULONG Ip)
{
    ULONG i;

    for (i = 0; i < ARP_ENTRIES; i++)
    {
        if (KdpArp[i].Ip == Ip)
            return &KdpArp[i];
    }

    return NULL;
}

static
VOID
KdpArpLearn(
    _In_ ULONG Ip,
    _In_reads_(MAC_ADDRESS_SIZE) const UCHAR *Address,
    _In_ BOOLEAN Create)
{
    PKDNET_ARP_ENTRY Entry = KdpArpFind(Ip);

    if (!Entry)
    {
        if (!Create)
            return;

        Entry = &KdpArp[KdpArpNext++ % ARP_ENTRIES];
        Entry->Ip = Ip;
        Entry->RequestTime = 0;
    }

    RtlCopyMemory(Entry->Address, Address, MAC_ADDRESS_SIZE);
    Entry->Valid = TRUE;
}

static
VOID
KdpArpSend(
    _In_ USHORT Operation,
    _In_reads_(MAC_ADDRESS_SIZE) const UCHAR *TargetAddress,
    _In_ ULONG TargetIp)
{
    PUCHAR Frame, Arp;
    ULONG Handle;

    Frame = KdNicGetTxBuffer(&Handle);
    if (!Frame)
        return;

    RtlCopyMemory(Frame, Operation == ARP_REQUEST ? KdpBroadcast : TargetAddress, MAC_ADDRESS_SIZE);
    RtlCopyMemory(Frame + 6, KdNicAddress, MAC_ADDRESS_SIZE);
    KdNetPut16(Frame + 12, ETH_TYPE_ARP);

    Arp = Frame + ETH_HEADER_SIZE;
    KdNetPut16(Arp, 1);
    KdNetPut16(Arp + 2, ETH_TYPE_IPV4);
    Arp[4] = MAC_ADDRESS_SIZE;
    Arp[5] = 4;
    KdNetPut16(Arp + 6, Operation);
    RtlCopyMemory(Arp + 8, KdNicAddress, MAC_ADDRESS_SIZE);
    KdNetPut32(Arp + 14, KdIpAddress);
    if (Operation == ARP_REQUEST)
        RtlZeroMemory(Arp + 18, MAC_ADDRESS_SIZE);
    else
        RtlCopyMemory(Arp + 18, TargetAddress, MAC_ADDRESS_SIZE);
    KdNetPut32(Arp + 24, TargetIp);

    KdNicSend(Handle, ETH_HEADER_SIZE + ARP_SIZE);
}

static
VOID
KdpArpInput(
    _In_reads_bytes_(Length) PUCHAR Arp,
    _In_ ULONG Length)
{
    ULONG SenderIp, TargetIp;
    USHORT Operation;

    if (Length < ARP_SIZE || KdNetGet16(Arp) != 1 || KdNetGet16(Arp + 2) != ETH_TYPE_IPV4 ||
        Arp[4] != MAC_ADDRESS_SIZE || Arp[5] != 4)
    {
        return;
    }

    Operation = KdNetGet16(Arp + 6);
    SenderIp = KdNetGet32(Arp + 14);
    TargetIp = KdNetGet32(Arp + 24);
    if (!KdIpAddress || TargetIp != KdIpAddress)
        return;

    KdpArpLearn(SenderIp, Arp + 8, Operation == ARP_REQUEST);
    if (Operation == ARP_REQUEST)
        KdpArpSend(ARP_REPLY, Arp + 8, SenderIp);
}

static
BOOLEAN
KdpResolve(
    _In_ ULONG DestinationIp,
    _Out_writes_(MAC_ADDRESS_SIZE) PUCHAR Address)
{
    PKDNET_ARP_ENTRY Entry;
    ULONG NextHop = DestinationIp;
    ULONG64 Now;

    if (DestinationIp == KDNET_IP_BROADCAST ||
        (KdIpMask && (DestinationIp | KdIpMask) == KDNET_IP_BROADCAST &&
         ((DestinationIp ^ KdIpAddress) & KdIpMask) == 0))
    {
        RtlCopyMemory(Address, KdpBroadcast, MAC_ADDRESS_SIZE);
        return TRUE;
    }

    if (!KdIpAddress)
        return FALSE;

    if ((DestinationIp ^ KdIpAddress) & KdIpMask)
    {
        if (!KdIpGateway)
            return FALSE;

        NextHop = KdIpGateway;
    }

    Entry = KdpArpFind(NextHop);
    if (Entry && Entry->Valid)
    {
        RtlCopyMemory(Address, Entry->Address, MAC_ADDRESS_SIZE);
        return TRUE;
    }

    if (!Entry)
    {
        Entry = &KdpArp[KdpArpNext++ % ARP_ENTRIES];
        Entry->Ip = NextHop;
        Entry->Valid = FALSE;
        Entry->RequestTime = 0;
    }

    Now = KdNetTime();
    if (!Entry->RequestTime || Now - Entry->RequestTime >= 200 * KDNET_MILLISECOND)
    {
        Entry->RequestTime = Now;
        KdpArpSend(ARP_REQUEST, KdpBroadcast, NextHop);
    }

    return FALSE;
}

static
VOID
KdpIpHeader(
    _Out_writes_(ETH_HEADER_SIZE + IP_HEADER_SIZE) PUCHAR Frame,
    _In_reads_(MAC_ADDRESS_SIZE) const UCHAR *DestinationAddress,
    _In_ ULONG SourceIp,
    _In_ ULONG DestinationIp,
    _In_ UCHAR Protocol,
    _In_ ULONG PayloadLength)
{
    PUCHAR Ip = Frame + ETH_HEADER_SIZE;

    RtlCopyMemory(Frame, DestinationAddress, MAC_ADDRESS_SIZE);
    RtlCopyMemory(Frame + 6, KdNicAddress, MAC_ADDRESS_SIZE);
    KdNetPut16(Frame + 12, ETH_TYPE_IPV4);

    Ip[0] = 0x45;
    Ip[1] = 0;
    KdNetPut16(Ip + 2, (USHORT)(IP_HEADER_SIZE + PayloadLength));
    KdNetPut16(Ip + 4, KdpIpIdentification++);
    KdNetPut16(Ip + 6, 0x4000);
    Ip[8] = 64;
    Ip[9] = Protocol;
    KdNetPut16(Ip + 10, 0);
    KdNetPut32(Ip + 12, SourceIp);
    KdNetPut32(Ip + 16, DestinationIp);
    KdNetPut16(Ip + 10, KdpChecksum(0, Ip, IP_HEADER_SIZE));
}

PUCHAR
KdUdpBegin(
    _In_ ULONG DestinationIp,
    _Out_ PULONG Handle)
{
    UCHAR Address[MAC_ADDRESS_SIZE];
    PUCHAR Frame;

    if (!KdNicLinkState || !KdpResolve(DestinationIp, Address))
        return NULL;

    Frame = KdNicGetTxBuffer(Handle);
    if (!Frame)
        return NULL;

    RtlCopyMemory(Frame, Address, MAC_ADDRESS_SIZE);
    return Frame + KDNET_UDP_HEADERS;
}

VOID
KdUdpEnd(
    _In_ ULONG Handle,
    _In_ ULONG DestinationIp,
    _In_ USHORT DestinationPort,
    _In_ USHORT SourcePort,
    _In_ ULONG Length)
{
    UCHAR Address[MAC_ADDRESS_SIZE];
    PUCHAR Frame, Udp;
    USHORT Sum;

    Frame = KdNicGetTxBufferAddress(Handle);
    RtlCopyMemory(Address, Frame, MAC_ADDRESS_SIZE);
    KdpIpHeader(Frame, Address, KdIpAddress, DestinationIp, IP_PROTOCOL_UDP, UDP_HEADER_SIZE + Length);

    Udp = Frame + ETH_HEADER_SIZE + IP_HEADER_SIZE;
    KdNetPut16(Udp, SourcePort);
    KdNetPut16(Udp + 2, DestinationPort);
    KdNetPut16(Udp + 4, (USHORT)(UDP_HEADER_SIZE + Length));
    KdNetPut16(Udp + 6, 0);
    Sum = KdpChecksum(KdpPseudoSum(KdIpAddress, DestinationIp, UDP_HEADER_SIZE + Length),
                      Udp, UDP_HEADER_SIZE + Length);
    KdNetPut16(Udp + 6, Sum ? Sum : 0xFFFF);

    KdNicSend(Handle, KDNET_UDP_HEADERS + Length);
}

static
PUCHAR
KdpDhcpOption(
    _Inout_ PUCHAR Cursor,
    _In_ UCHAR Option,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ UCHAR Length)
{
    *Cursor++ = Option;
    *Cursor++ = Length;
    RtlCopyMemory(Cursor, Data, Length);
    return Cursor + Length;
}

static
VOID
KdpDhcpSend(
    _In_ UCHAR Type)
{
    static const UCHAR Parameters[] =
    {
        DHCP_OPTION_MASK, DHCP_OPTION_ROUTER, DHCP_OPTION_DNS, DHCP_OPTION_LEASE, DHCP_OPTION_SERVER,
        DHCP_OPTION_RENEWAL
    };
    UCHAR Value[1 + MAC_ADDRESS_SIZE];
    PUCHAR Frame, Udp, Dhcp, Cursor;
    ULONG Handle, Length, Source;
    USHORT Sum;

    Frame = KdNicGetTxBuffer(&Handle);
    if (!Frame)
        return;

    Source = KdpDhcpState == DhcpRenewing ? KdIpAddress : 0;
    Udp = Frame + ETH_HEADER_SIZE + IP_HEADER_SIZE;
    Dhcp = Udp + UDP_HEADER_SIZE;
    RtlZeroMemory(Dhcp, DHCP_FIXED_SIZE);
    Dhcp[0] = 1;
    Dhcp[1] = 1;
    Dhcp[2] = MAC_ADDRESS_SIZE;
    KdNetPut32(Dhcp + 4, KdpDhcpXid);
    KdNetPut16(Dhcp + 10, Source ? 0 : 0x8000);
    KdNetPut32(Dhcp + 12, Source);
    RtlCopyMemory(Dhcp + 28, KdNicAddress, MAC_ADDRESS_SIZE);

    Cursor = Dhcp + DHCP_FIXED_SIZE;
    KdNetPut32(Cursor, DHCP_MAGIC);
    Cursor += 4;
    Cursor = KdpDhcpOption(Cursor, DHCP_OPTION_TYPE, &Type, 1);
    Value[0] = 1;
    RtlCopyMemory(Value + 1, KdNicAddress, MAC_ADDRESS_SIZE);
    Cursor = KdpDhcpOption(Cursor, DHCP_OPTION_CLIENT_ID, Value, sizeof(Value));
    if (Type == DHCP_REQUEST && !Source)
    {
        KdNetPut32(Value, KdpDhcpOffered);
        Cursor = KdpDhcpOption(Cursor, DHCP_OPTION_REQUESTED, Value, 4);
        KdNetPut32(Value, KdpDhcpServer);
        Cursor = KdpDhcpOption(Cursor, DHCP_OPTION_SERVER, Value, 4);
    }

    Cursor = KdpDhcpOption(Cursor, DHCP_OPTION_PARAMETERS, Parameters, sizeof(Parameters));
    *Cursor++ = DHCP_OPTION_END;
    while ((ULONG)(Cursor - Dhcp) < 300)
        *Cursor++ = 0;

    Length = (ULONG)(Cursor - Dhcp);
    KdpIpHeader(Frame, KdpBroadcast, Source, KDNET_IP_BROADCAST, IP_PROTOCOL_UDP, UDP_HEADER_SIZE + Length);
    KdNetPut16(Udp, DHCP_CLIENT_PORT);
    KdNetPut16(Udp + 2, DHCP_SERVER_PORT);
    KdNetPut16(Udp + 4, (USHORT)(UDP_HEADER_SIZE + Length));
    KdNetPut16(Udp + 6, 0);
    Sum = KdpChecksum(KdpPseudoSum(Source, KDNET_IP_BROADCAST, UDP_HEADER_SIZE + Length),
                      Udp, UDP_HEADER_SIZE + Length);
    KdNetPut16(Udp + 6, Sum ? Sum : 0xFFFF);
    KdNicSend(Handle, KDNET_UDP_HEADERS + Length);
}

static
VOID
KdpDhcpInput(
    _In_reads_bytes_(Length) PUCHAR Dhcp,
    _In_ ULONG Length)
{
    ULONG Offered, Server = 0, Mask = 0, Router = 0, Lease = 0, Renewal = 0, Dns = 0, Position;
    ULONG64 Now = KdNetTime();
    UCHAR Type = 0, Option, Size;

    if (Length < DHCP_FIXED_SIZE + 4 || Dhcp[0] != 2 || KdNetGet32(Dhcp + 4) != KdpDhcpXid ||
        RtlCompareMemory(Dhcp + 28, KdNicAddress, MAC_ADDRESS_SIZE) != MAC_ADDRESS_SIZE ||
        KdNetGet32(Dhcp + DHCP_FIXED_SIZE) != DHCP_MAGIC)
    {
        return;
    }

    Offered = KdNetGet32(Dhcp + 16);
    Position = DHCP_FIXED_SIZE + 4;
    while (Position < Length)
    {
        Option = Dhcp[Position++];
        if (Option == DHCP_OPTION_END)
            break;

        if (Option == 0)
            continue;

        if (Position >= Length)
            break;

        Size = Dhcp[Position++];
        if (Size > Length - Position)
            break;

        if (Option == DHCP_OPTION_TYPE && Size >= 1)
            Type = Dhcp[Position];
        else if (Option == DHCP_OPTION_SERVER && Size >= 4)
            Server = KdNetGet32(Dhcp + Position);
        else if (Option == DHCP_OPTION_MASK && Size >= 4)
            Mask = KdNetGet32(Dhcp + Position);
        else if (Option == DHCP_OPTION_ROUTER && Size >= 4)
            Router = KdNetGet32(Dhcp + Position);
        else if (Option == DHCP_OPTION_DNS && Size >= 4)
            Dns = KdNetGet32(Dhcp + Position);
        else if (Option == DHCP_OPTION_LEASE && Size >= 4)
            Lease = KdNetGet32(Dhcp + Position);
        else if (Option == DHCP_OPTION_RENEWAL && Size >= 4)
            Renewal = KdNetGet32(Dhcp + Position);

        Position += Size;
    }

    if (Type == DHCP_OFFER && KdpDhcpState == DhcpSelecting && Offered)
    {
        KdpDhcpOffered = Offered;
        KdpDhcpServer = Server;
        KdpDhcpState = DhcpRequesting;
        KdpDhcpBackoff = 1;
        KdpDhcpNext = Now + KDNET_SECOND;
        KdpDhcpSend(DHCP_REQUEST);
    }
    else if (Type == DHCP_ACK && (KdpDhcpState == DhcpRequesting || KdpDhcpState == DhcpRenewing) && Offered)
    {
        if (!Lease)
            Lease = 3600;
        if (!Renewal || Renewal >= Lease)
            Renewal = Lease / 2;

        KdIpAddress = Offered;
        KdIpMask = Mask ? Mask : KDNET_IP(255, 255, 255, 0);
        KdIpGateway = Router;
        KdpDnsServer = Dns;
        KdpDhcpState = DhcpBound;
        KdpDhcpRenew = Now + (ULONG64)Renewal * KDNET_SECOND;
        KdpDhcpExpire = Now + (ULONG64)Lease * KDNET_SECOND;
    }
    else if (Type == DHCP_NAK && (KdpDhcpState == DhcpRequesting || KdpDhcpState == DhcpRenewing))
    {
        KdIpAddress = 0;
        KdpDhcpState = DhcpInit;
        KdpDhcpNext = Now + KDNET_SECOND;
    }
}

static
VOID
KdpDnsSend(VOID)
{
    PCSTR Name = KdNetOptions.HostName, Label;
    PUCHAR Dns, Cursor;
    ULONG Handle, Length;

    Dns = KdUdpBegin(KdpDnsServer, &Handle);
    if (!Dns)
        return;

    KdCryptoRandom((PUCHAR)&KdpDnsIdentifier, sizeof(KdpDnsIdentifier));
    KdCryptoRandom((PUCHAR)&KdpDnsPort, sizeof(KdpDnsPort));
    KdpDnsPort |= 0xC000;

    RtlZeroMemory(Dns, DNS_HEADER_SIZE);
    KdNetPut16(Dns, KdpDnsIdentifier);
    KdNetPut16(Dns + 2, DNS_FLAG_RECURSE);
    KdNetPut16(Dns + 4, 1);
    Cursor = Dns + DNS_HEADER_SIZE;
    while (*Name)
    {
        Label = Name;
        while (*Name && *Name != '.')
            Name++;

        Length = (ULONG)(Name - Label);
        if (!Length || Length > 63)
            return;

        *Cursor++ = (UCHAR)Length;
        RtlCopyMemory(Cursor, Label, Length);
        Cursor += Length;
        if (*Name == '.')
            Name++;
    }

    *Cursor++ = 0;
    KdNetPut16(Cursor, DNS_TYPE_A);
    KdNetPut16(Cursor + 2, DNS_CLASS_IN);
    Cursor += 4;
    KdUdpEnd(Handle, KdpDnsServer, DNS_PORT, KdpDnsPort, (ULONG)(Cursor - Dns));
}

static
ULONG
KdpDnsSkipName(
    _In_reads_bytes_(Length) const UCHAR *Dns,
    _In_ ULONG Length,
    _In_ ULONG Position)
{
    while (Position < Length)
    {
        if ((Dns[Position] & 0xC0) == 0xC0)
            return Position + 2;

        if (!Dns[Position])
            return Position + 1;

        Position += Dns[Position] + 1;
    }

    return Length;
}

static
VOID
KdpDnsInput(
    _In_reads_bytes_(Length) PUCHAR Dns,
    _In_ ULONG Length)
{
    ULONG Position, Count, Size;
    USHORT Flags, Type;

    if (Length < DNS_HEADER_SIZE || KdNetGet16(Dns) != KdpDnsIdentifier)
        return;

    Flags = KdNetGet16(Dns + 2);
    if (!(Flags & DNS_FLAG_RESPONSE) || (Flags & DNS_FLAG_ERROR) || KdNetGet16(Dns + 4) != 1)
        return;

    Position = KdpDnsSkipName(Dns, Length, DNS_HEADER_SIZE) + 4;
    for (Count = KdNetGet16(Dns + 6); Count; Count--)
    {
        Position = KdpDnsSkipName(Dns, Length, Position);
        if (Position + 10 > Length)
            return;

        Type = KdNetGet16(Dns + Position);
        Size = KdNetGet16(Dns + Position + 8);
        Position += 10;
        if (Size > Length - Position)
            return;

        if (Type == DNS_TYPE_A && KdNetGet16(Dns + Position - 8) == DNS_CLASS_IN && Size == 4)
        {
            KdNetOptions.HostIp = KdNetGet32(Dns + Position);
            KdpDnsPort = 0;
            KdpDnsNext = KdNetTime() + DNS_REFRESH;
            return;
        }

        Position += Size;
    }
}

static
VOID
KdpDnsTimer(
    _In_ ULONG64 Now)
{
    if (!KdNetOptions.HostName[0] || !KdIpAddress || !KdpDnsServer || Now < KdpDnsNext)
        return;

    KdpDnsNext = Now + DNS_RETRY;
    KdpDnsSend();
}

static
VOID
KdpIcmpInput(
    _In_reads_bytes_(ETH_HEADER_SIZE + IP_HEADER_SIZE + Length) PUCHAR Frame,
    _In_ ULONG SourceIp,
    _In_ ULONG Length)
{
    PUCHAR Icmp = Frame + ETH_HEADER_SIZE + IP_HEADER_SIZE, Reply;
    ULONG Handle;

    if (Length < 8 || Length > KDNET_DATAGRAM_SIZE || Icmp[0] != ICMP_ECHO_REQUEST ||
        KdpChecksum(0, Icmp, Length) != 0)
    {
        return;
    }

    Reply = KdNicGetTxBuffer(&Handle);
    if (!Reply)
        return;

    RtlCopyMemory(Reply + ETH_HEADER_SIZE + IP_HEADER_SIZE, Icmp, Length);
    KdpIpHeader(Reply, Frame + 6, KdIpAddress, SourceIp, IP_PROTOCOL_ICMP, Length);
    Icmp = Reply + ETH_HEADER_SIZE + IP_HEADER_SIZE;
    Icmp[0] = ICMP_ECHO_REPLY;
    KdNetPut16(Icmp + 2, 0);
    KdNetPut16(Icmp + 2, KdpChecksum(0, Icmp, Length));
    KdNicSend(Handle, ETH_HEADER_SIZE + IP_HEADER_SIZE + Length);
}

VOID
KdIpInput(
    _In_reads_bytes_(Length) PUCHAR Frame,
    _In_ ULONG Length)
{
    PUCHAR Ip, Udp;
    ULONG HeaderLength, TotalLength, SourceIp, DestinationIp, UdpLength;
    USHORT Type, Fragment, DestinationPort;

    if (Length < ETH_HEADER_SIZE)
        return;

    Type = KdNetGet16(Frame + 12);
    if (Type == ETH_TYPE_ARP)
    {
        KdpArpInput(Frame + ETH_HEADER_SIZE, Length - ETH_HEADER_SIZE);
        return;
    }

    if (Type != ETH_TYPE_IPV4 || Length < ETH_HEADER_SIZE + IP_HEADER_SIZE)
        return;

    Ip = Frame + ETH_HEADER_SIZE;
    HeaderLength = (Ip[0] & 0x0F) * 4;
    TotalLength = KdNetGet16(Ip + 2);
    Fragment = KdNetGet16(Ip + 6);
    if ((Ip[0] >> 4) != 4 || HeaderLength != IP_HEADER_SIZE || TotalLength < HeaderLength ||
        TotalLength > Length - ETH_HEADER_SIZE || (Fragment & (IP_FLAG_MORE | IP_OFFSET_MASK)) ||
        KdpChecksum(0, Ip, HeaderLength) != 0)
    {
        return;
    }

    SourceIp = KdNetGet32(Ip + 12);
    DestinationIp = KdNetGet32(Ip + 16);
    if (Ip[9] == IP_PROTOCOL_ICMP)
    {
        if (KdIpAddress && DestinationIp == KdIpAddress)
            KdpIcmpInput(Frame, SourceIp, TotalLength - HeaderLength);

        return;
    }

    if (Ip[9] != IP_PROTOCOL_UDP || TotalLength < HeaderLength + UDP_HEADER_SIZE)
        return;

    Udp = Ip + HeaderLength;
    UdpLength = KdNetGet16(Udp + 4);
    if (UdpLength < UDP_HEADER_SIZE || UdpLength > TotalLength - HeaderLength)
        return;

    if (KdNetGet16(Udp + 6) &&
        KdpChecksum(KdpPseudoSum(SourceIp, DestinationIp, UdpLength), Udp, UdpLength) != 0)
    {
        return;
    }

    DestinationPort = KdNetGet16(Udp + 2);
    if (DestinationPort == DHCP_CLIENT_PORT)
    {
        KdpDhcpInput(Udp + UDP_HEADER_SIZE, UdpLength - UDP_HEADER_SIZE);
        return;
    }

    if (!KdIpAddress || DestinationIp != KdIpAddress)
        return;

    if (KdpDnsPort && DestinationPort == KdpDnsPort && SourceIp == KdpDnsServer &&
        KdNetGet16(Udp) == DNS_PORT)
    {
        KdpDnsInput(Udp + UDP_HEADER_SIZE, UdpLength - UDP_HEADER_SIZE);
        return;
    }

    if (DestinationPort != KdNetOptions.HostPort)
        return;

    KdSessionInput(SourceIp, KdNetGet16(Udp), Udp + UDP_HEADER_SIZE, UdpLength - UDP_HEADER_SIZE);
}

VOID
KdIpTimer(
    _In_ ULONG64 Now)
{
    if (!KdNicLinkState)
        return;

    KdpDnsTimer(Now);
    switch (KdpDhcpState)
    {
        case DhcpInit:
            if (Now < KdpDhcpNext)
                break;

            KdCryptoRandom((PUCHAR)&KdpDhcpXid, sizeof(KdpDhcpXid));
            KdpDhcpState = DhcpSelecting;
            KdpDhcpBackoff = 1;
            KdpDhcpNext = Now + KDNET_SECOND;
            KdpDhcpSend(DHCP_DISCOVER);
            break;

        case DhcpSelecting:
        case DhcpRequesting:
            if (Now < KdpDhcpNext)
                break;

            if (KdpDhcpBackoff >= 8)
            {
                KdpDhcpState = DhcpInit;
                KdpDhcpNext = Now;
                break;
            }

            KdpDhcpBackoff *= 2;
            KdpDhcpNext = Now + KdpDhcpBackoff * KDNET_SECOND;
            KdpDhcpSend(KdpDhcpState == DhcpSelecting ? DHCP_DISCOVER : DHCP_REQUEST);
            break;

        case DhcpBound:
            if (Now < KdpDhcpRenew)
                break;

            KdpDhcpState = DhcpRenewing;
            KdpDhcpNext = Now + 10 * KDNET_SECOND;
            KdpDhcpSend(DHCP_REQUEST);
            break;

        case DhcpRenewing:
            if (Now >= KdpDhcpExpire)
            {
                KdIpAddress = 0;
                KdpDhcpState = DhcpInit;
                KdpDhcpNext = Now;
                break;
            }

            if (Now < KdpDhcpNext)
                break;

            KdpDhcpNext = Now + 10 * KDNET_SECOND;
            KdpDhcpSend(DHCP_REQUEST);
            break;
    }
}

VOID
KdIpInitialize(VOID)
{
    KdCryptoRandom((PUCHAR)&KdpIpIdentification, sizeof(KdpIpIdentification));
    KdpDhcpState = DhcpInit;
    KdpDhcpNext = 0;
}
