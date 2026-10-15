/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IPv4, DHCP, TCP and HTTP download over the UEFI Simple Network Protocol
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>

#include "uefinetp.h"

#define SNP_FRAME_BYTES         2048
#define SNP_FRAME_PAD           2
#define SNP_TX_BUFFERS          8
#define SNP_MAC_BYTES           6
#define SNP_IP_BYTES            4
#define SNP_ETH_HEADER          14
#define SNP_ETH_MIN_FRAME       60
#define SNP_ETHERTYPE_IPV4      0x0800
#define SNP_ETHERTYPE_ARP       0x0806
#define SNP_ARP_BYTES           28
#define SNP_ARP_REQUEST         1
#define SNP_ARP_REPLY           2
#define SNP_IP_HEADER           20
#define SNP_IP_TTL              64
#define SNP_IP_DONT_FRAGMENT    0x4000
#define SNP_IP_FRAGMENT_MASK    0x3FFF
#define SNP_IP_ICMP             1
#define SNP_IP_TCP              6
#define SNP_IP_UDP              17
#define SNP_ICMP_ECHO_REPLY     0
#define SNP_ICMP_ECHO_REQUEST   8
#define SNP_ICMP_HEADER         8
#define SNP_UDP_HEADER          8
#define SNP_TCP_HEADER          20
#define SNP_TCP_SYN_OPTIONS     8
#define SNP_TCP_FIN             0x01
#define SNP_TCP_SYN             0x02
#define SNP_TCP_RST             0x04
#define SNP_TCP_PSH             0x08
#define SNP_TCP_ACK             0x10
#define SNP_TCP_DEFAULT_MSS     536
#define SNP_ETH_MTU             1500
#define SNP_TCP_WINDOW          (384 * 1024)
#define SNP_TCP_WINDOW_SCALE    4
#define SNP_TCP_ACK_SEGMENTS    8
#define SNP_TCP_IDLE_POLLS      32
#define SNP_TCP_SYN_ATTEMPTS    5
#define SNP_TCP_REQUEST_RETRIES 5
#define SNP_TCP_PORT_BASE       49152
#define SNP_TCP_PORT_COUNT      16384
#define SNP_POLL_BUDGET         32
#define SNP_DHCP_CLIENT_PORT    68
#define SNP_DHCP_SERVER_PORT    67
#define SNP_DHCP_BYTES          300
#define SNP_DHCP_OPTIONS        240
#define SNP_DHCP_COOKIE         0x63825363
#define SNP_DHCP_DISCOVER       1
#define SNP_DHCP_OFFER          2
#define SNP_DHCP_REQUEST        3
#define SNP_DHCP_ACK            5
#define SNP_DHCP_NAK            6
#define SNP_DHCP_ATTEMPTS       6
#define SNP_ARP_ATTEMPTS        4
#define SNP_TICK_PERIOD         100000
#define SNP_TICKS_PER_SECOND    100
#define SNP_DATA_TIMEOUT        (10 * SNP_TICKS_PER_SECOND)
#define SNP_CLOSE_TIMEOUT       (SNP_TICKS_PER_SECOND / 5)
#define SNP_HTTP_HEADER_BYTES   4096
#define SNP_HTTP_REQUEST_BYTES  1400
#define SNP_HTTP_ATTEMPTS       4
#define SNP_AUTHORITY_CHARS     64

#define SNP_SEQ_LT(a, b)        ((LONG)((a) - (b)) < 0)
#define SNP_SEQ_LEQ(a, b)       ((LONG)((a) - (b)) <= 0)
#define SNP_SEQ_GT(a, b)        ((LONG)((a) - (b)) > 0)
#define SNP_TICK_BEFORE(a, b)   ((LONG)((a) - (b)) < 0)

typedef enum _SNP_TCP_STATE
{
    SnpTcpClosed,
    SnpTcpSynSent,
    SnpTcpEstablished
} SNP_TCP_STATE;

typedef struct _SNP_TCP
{
    SNP_TCP_STATE State;
    UCHAR RemoteAddress[SNP_IP_BYTES];
    USHORT RemotePort;
    USHORT LocalPort;
    ULONG Iss;
    ULONG SndUna;
    ULONG SndNxt;
    ULONG RcvNxt;
    UCHAR RcvScale;
    BOOLEAN PeerFin;
    BOOLEAN PeerReset;
    BOOLEAN AckNow;
    BOOLEAN ResendSyn;
    ULONG PendingSegments;
    ULONG DataTick;
} SNP_TCP;

typedef struct _SNP_HTTP
{
    CHAR Header[SNP_HTTP_HEADER_BYTES + 1];
    ULONG HeaderLength;
    BOOLEAN HeaderDone;
    BOOLEAN Failed;
    ULONG StatusCode;
    PUCHAR Image;
    ULONG ImageSize;
    ULONG Received;
} SNP_HTTP;

typedef struct _SNP_NET
{
    EFI_SIMPLE_NETWORK_PROTOCOL* Snp;
    UINT32 InitialState;
    EFI_EVENT TickEvent;
    ULONG Ticks;
    PUCHAR Buffers;
    PUCHAR Rx;
    PUCHAR Tx[SNP_TX_BUFFERS];
    BOOLEAN TxBusy[SNP_TX_BUFFERS];
    UCHAR Mac[SNP_MAC_BYTES];
    UCHAR Address[SNP_IP_BYTES];
    UCHAR Mask[SNP_IP_BYTES];
    UCHAR Gateway[SNP_IP_BYTES];
    UCHAR HopAddress[SNP_IP_BYTES];
    UCHAR HopMac[SNP_MAC_BYTES];
    BOOLEAN HopResolved;
    USHORT IpId;
    USHORT Mss;
    ULONG Seed;
    ULONG DhcpXid;
    UCHAR DhcpWanted;
    UCHAR DhcpReply;
    UCHAR DhcpOffer[SNP_IP_BYTES];
    UCHAR DhcpServer[SNP_IP_BYTES];
    UCHAR DhcpMask[SNP_IP_BYTES];
    UCHAR DhcpRouter[SNP_IP_BYTES];
    SNP_TCP Tcp;
    SNP_HTTP Http;
} SNP_NET;

static EFI_GUID SnpGuid = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
static const UCHAR SnpBroadcastMac[SNP_MAC_BYTES] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const UCHAR SnpBroadcastAddress[SNP_IP_BYTES] = {0xFF, 0xFF, 0xFF, 0xFF};
static const UCHAR SnpZeroAddress[SNP_IP_BYTES] = {0, 0, 0, 0};
static SNP_NET SnpNet;

static
USHORT
SnpGet16(
    _In_reads_bytes_(2) const UCHAR* Data)
{
    return (USHORT)((Data[0] << 8) | Data[1]);
}

static
ULONG
SnpGet32(
    _In_reads_bytes_(4) const UCHAR* Data)
{
    return ((ULONG)Data[0] << 24) | ((ULONG)Data[1] << 16) | ((ULONG)Data[2] << 8) | Data[3];
}

static
VOID
SnpPut16(
    _Out_writes_bytes_(2) PUCHAR Data,
    _In_ ULONG Value)
{
    Data[0] = (UCHAR)(Value >> 8);
    Data[1] = (UCHAR)Value;
}

static
VOID
SnpPut32(
    _Out_writes_bytes_(4) PUCHAR Data,
    _In_ ULONG Value)
{
    Data[0] = (UCHAR)(Value >> 24);
    Data[1] = (UCHAR)(Value >> 16);
    Data[2] = (UCHAR)(Value >> 8);
    Data[3] = (UCHAR)Value;
}

static
ULONGLONG
SnpChecksumAdd(
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ ULONG Length,
    _In_ ULONGLONG Sum)
{
    const UCHAR* Bytes = Data;
    const ULONG* Words;
    USHORT Half = 0;

    if (((ULONG_PTR)Bytes & 3) == 0)
    {
        for (Words = (const ULONG*)Bytes; Length >= 16; Words += 4, Length -= 16)
            Sum += (ULONGLONG)Words[0] + Words[1] + Words[2] + Words[3];

        for (; Length >= 4; Words++, Length -= 4)
            Sum += *Words;

        Bytes = (const UCHAR*)Words;
    }

    for (; Length >= 2; Bytes += 2, Length -= 2)
    {
        RtlCopyMemory(&Half, Bytes, sizeof(Half));
        Sum += Half;
    }

    if (Length)
    {
        Half = 0;
        *(PUCHAR)&Half = *Bytes;
        Sum += Half;
    }

    return Sum;
}

static
USHORT
SnpChecksumFold(
    _In_ ULONGLONG Sum)
{
    Sum = (Sum & 0xFFFFFFFF) + (Sum >> 32);
    Sum = (Sum & 0xFFFFFFFF) + (Sum >> 32);
    Sum = (Sum & 0xFFFF) + (Sum >> 16);
    Sum = (Sum & 0xFFFF) + (Sum >> 16);
    Sum = (Sum & 0xFFFF) + (Sum >> 16);
    return (USHORT)Sum;
}

static
ULONGLONG
SnpPseudoSum(
    _In_reads_bytes_(SNP_IP_BYTES) const UCHAR* Source,
    _In_reads_bytes_(SNP_IP_BYTES) const UCHAR* Destination,
    _In_ UCHAR Protocol,
    _In_ ULONG Length)
{
    UCHAR Pseudo[12];

    RtlCopyMemory(Pseudo, Source, SNP_IP_BYTES);
    RtlCopyMemory(Pseudo + 4, Destination, SNP_IP_BYTES);
    Pseudo[8] = 0;
    Pseudo[9] = Protocol;
    SnpPut16(Pseudo + 10, Length);
    return SnpChecksumAdd(Pseudo, sizeof(Pseudo), 0);
}

static
VOID
SnpStoreChecksum(
    _Out_writes_bytes_(2) PUCHAR Field,
    _In_ ULONGLONG Sum)
{
    USHORT Checksum = (USHORT)~SnpChecksumFold(Sum);

    RtlCopyMemory(Field, &Checksum, sizeof(Checksum));
}

static
BOOLEAN
SnpAddressIsZero(
    _In_reads_bytes_(SNP_IP_BYTES) const UCHAR* Address)
{
    return RtlEqualMemory(Address, SnpZeroAddress, SNP_IP_BYTES);
}

static
ULONG
SnpRandom(VOID)
{
    SnpNet.Seed ^= SnpNet.Seed << 13;
    SnpNet.Seed ^= SnpNet.Seed >> 17;
    SnpNet.Seed ^= SnpNet.Seed << 5;
    return SnpNet.Seed;
}

static
ULONG
SnpNow(VOID)
{
    while (GlobalSystemTable->BootServices->CheckEvent(SnpNet.TickEvent) == EFI_SUCCESS)
        SnpNet.Ticks++;

    return SnpNet.Ticks;
}

static
VOID
SnpReclaimTx(VOID)
{
    VOID* Done;
    ULONG i;

    for (;;)
    {
        Done = NULL;
        if (EFI_ERROR(SnpNet.Snp->GetStatus(SnpNet.Snp, NULL, &Done)) || !Done)
            return;

        for (i = 0; i < SNP_TX_BUFFERS; i++)
        {
            if (Done == SnpNet.Tx[i] + SNP_FRAME_PAD)
                SnpNet.TxBusy[i] = FALSE;
        }
    }
}

static
PUCHAR
SnpAcquireTx(
    _Out_ PULONG Index)
{
    ULONG Attempt, i;

    for (Attempt = 0; Attempt < 100000; Attempt++)
    {
        for (i = 0; i < SNP_TX_BUFFERS; i++)
        {
            if (!SnpNet.TxBusy[i])
            {
                *Index = i;
                return SnpNet.Tx[i] + SNP_FRAME_PAD;
            }
        }

        SnpReclaimTx();
    }

    return NULL;
}

static
BOOLEAN
SnpTransmit(
    _In_ ULONG Index,
    _In_ ULONG Length)
{
    PUCHAR Frame = SnpNet.Tx[Index] + SNP_FRAME_PAD;
    EFI_STATUS Status = EFI_NOT_READY;
    ULONG Attempt;

    if (Length < SNP_ETH_MIN_FRAME)
    {
        RtlZeroMemory(Frame + Length, SNP_ETH_MIN_FRAME - Length);
        Length = SNP_ETH_MIN_FRAME;
    }

    SnpNet.TxBusy[Index] = TRUE;
    for (Attempt = 0; Attempt < 100000 && Status == EFI_NOT_READY; Attempt++)
    {
        Status = SnpNet.Snp->Transmit(SnpNet.Snp, 0, Length, Frame, NULL, NULL, NULL);
        if (Status == EFI_NOT_READY)
            SnpReclaimTx();
    }

    if (EFI_ERROR(Status))
    {
        SnpNet.TxBusy[Index] = FALSE;
        return FALSE;
    }

    return TRUE;
}

static
VOID
SnpFillEthernet(
    _Out_writes_bytes_(SNP_ETH_HEADER) PUCHAR Frame,
    _In_reads_bytes_(SNP_MAC_BYTES) const UCHAR* Destination,
    _In_ ULONG Type)
{
    RtlCopyMemory(Frame, Destination, SNP_MAC_BYTES);
    RtlCopyMemory(Frame + SNP_MAC_BYTES, SnpNet.Mac, SNP_MAC_BYTES);
    SnpPut16(Frame + 12, Type);
}

static
VOID
SnpFillIp(
    _Out_writes_bytes_(SNP_IP_HEADER) PUCHAR Ip,
    _In_ UCHAR Protocol,
    _In_reads_bytes_(SNP_IP_BYTES) const UCHAR* Source,
    _In_reads_bytes_(SNP_IP_BYTES) const UCHAR* Destination,
    _In_ ULONG PayloadLength)
{
    Ip[0] = 0x45;
    Ip[1] = 0;
    SnpPut16(Ip + 2, SNP_IP_HEADER + PayloadLength);
    SnpPut16(Ip + 4, SnpNet.IpId++);
    SnpPut16(Ip + 6, SNP_IP_DONT_FRAGMENT);
    Ip[8] = SNP_IP_TTL;
    Ip[9] = Protocol;
    Ip[10] = 0;
    Ip[11] = 0;
    RtlCopyMemory(Ip + 12, Source, SNP_IP_BYTES);
    RtlCopyMemory(Ip + 16, Destination, SNP_IP_BYTES);
    SnpStoreChecksum(Ip + 10, SnpChecksumAdd(Ip, SNP_IP_HEADER, 0));
}

static
VOID
SnpSendArp(
    _In_ ULONG Operation,
    _In_reads_bytes_opt_(SNP_MAC_BYTES) const UCHAR* TargetMac,
    _In_reads_bytes_(SNP_IP_BYTES) const UCHAR* TargetAddress)
{
    PUCHAR Frame, Arp;
    ULONG Index;

    Frame = SnpAcquireTx(&Index);
    if (!Frame)
        return;

    SnpFillEthernet(Frame, TargetMac ? TargetMac : SnpBroadcastMac, SNP_ETHERTYPE_ARP);
    Arp = Frame + SNP_ETH_HEADER;
    SnpPut16(Arp, 1);
    SnpPut16(Arp + 2, SNP_ETHERTYPE_IPV4);
    Arp[4] = SNP_MAC_BYTES;
    Arp[5] = SNP_IP_BYTES;
    SnpPut16(Arp + 6, Operation);
    RtlCopyMemory(Arp + 8, SnpNet.Mac, SNP_MAC_BYTES);
    RtlCopyMemory(Arp + 14, SnpNet.Address, SNP_IP_BYTES);
    if (TargetMac)
        RtlCopyMemory(Arp + 18, TargetMac, SNP_MAC_BYTES);
    else
        RtlZeroMemory(Arp + 18, SNP_MAC_BYTES);
    RtlCopyMemory(Arp + 24, TargetAddress, SNP_IP_BYTES);
    SnpTransmit(Index, SNP_ETH_HEADER + SNP_ARP_BYTES);
}

static
VOID
SnpArpInput(
    _In_reads_bytes_(Length) const UCHAR* Arp,
    _In_ ULONG Length)
{
    if (Length < SNP_ARP_BYTES ||
        SnpGet16(Arp) != 1 ||
        SnpGet16(Arp + 2) != SNP_ETHERTYPE_IPV4 ||
        Arp[4] != SNP_MAC_BYTES ||
        Arp[5] != SNP_IP_BYTES ||
        SnpAddressIsZero(SnpNet.Address))
    {
        return;
    }

    if (RtlEqualMemory(Arp + 14, SnpNet.HopAddress, SNP_IP_BYTES))
    {
        RtlCopyMemory(SnpNet.HopMac, Arp + 8, SNP_MAC_BYTES);
        SnpNet.HopResolved = TRUE;
    }

    if (SnpGet16(Arp + 6) == SNP_ARP_REQUEST && RtlEqualMemory(Arp + 24, SnpNet.Address, SNP_IP_BYTES))
        SnpSendArp(SNP_ARP_REPLY, Arp + 8, Arp + 14);
}

static
VOID
SnpIcmpInput(
    _In_reads_bytes_(SNP_ETH_HEADER) const UCHAR* Frame,
    _In_ const UCHAR* Ip,
    _In_reads_bytes_(Length) const UCHAR* Icmp,
    _In_ ULONG Length)
{
    PUCHAR Reply, Echo;
    ULONG Index;

    if (Length < SNP_ICMP_HEADER ||
        Icmp[0] != SNP_ICMP_ECHO_REQUEST ||
        SnpAddressIsZero(SnpNet.Address) ||
        !RtlEqualMemory(Ip + 16, SnpNet.Address, SNP_IP_BYTES) ||
        SnpChecksumFold(SnpChecksumAdd(Icmp, Length, 0)) != 0xFFFF)
    {
        return;
    }

    Reply = SnpAcquireTx(&Index);
    if (!Reply)
        return;

    SnpFillEthernet(Reply, Frame + SNP_MAC_BYTES, SNP_ETHERTYPE_IPV4);
    Echo = Reply + SNP_ETH_HEADER + SNP_IP_HEADER;
    RtlCopyMemory(Echo, Icmp, Length);
    Echo[0] = SNP_ICMP_ECHO_REPLY;
    Echo[2] = 0;
    Echo[3] = 0;
    SnpStoreChecksum(Echo + 2, SnpChecksumAdd(Echo, Length, 0));
    SnpFillIp(Reply + SNP_ETH_HEADER, SNP_IP_ICMP, SnpNet.Address, Ip + 12, Length);
    SnpTransmit(Index, SNP_ETH_HEADER + SNP_IP_HEADER + Length);
}

static
VOID
SnpSendDhcp(
    _In_ UCHAR Type)
{
    PUCHAR Frame, Udp, Dhcp, Option;
    ULONG Index;

    Frame = SnpAcquireTx(&Index);
    if (!Frame)
        return;

    SnpFillEthernet(Frame, SnpBroadcastMac, SNP_ETHERTYPE_IPV4);
    Udp = Frame + SNP_ETH_HEADER + SNP_IP_HEADER;
    Dhcp = Udp + SNP_UDP_HEADER;
    RtlZeroMemory(Dhcp, SNP_DHCP_BYTES);
    Dhcp[0] = 1;
    Dhcp[1] = 1;
    Dhcp[2] = SNP_MAC_BYTES;
    SnpPut32(Dhcp + 4, SnpNet.DhcpXid);
    SnpPut16(Dhcp + 10, 0x8000);
    RtlCopyMemory(Dhcp + 28, SnpNet.Mac, SNP_MAC_BYTES);
    SnpPut32(Dhcp + 236, SNP_DHCP_COOKIE);

    Option = Dhcp + SNP_DHCP_OPTIONS;
    *Option++ = 53;
    *Option++ = 1;
    *Option++ = Type;
    *Option++ = 61;
    *Option++ = 1 + SNP_MAC_BYTES;
    *Option++ = 1;
    RtlCopyMemory(Option, SnpNet.Mac, SNP_MAC_BYTES);
    Option += SNP_MAC_BYTES;
    *Option++ = 57;
    *Option++ = 2;
    SnpPut16(Option, 1500);
    Option += 2;
    if (Type == SNP_DHCP_REQUEST)
    {
        *Option++ = 50;
        *Option++ = SNP_IP_BYTES;
        RtlCopyMemory(Option, SnpNet.DhcpOffer, SNP_IP_BYTES);
        Option += SNP_IP_BYTES;
        *Option++ = 54;
        *Option++ = SNP_IP_BYTES;
        RtlCopyMemory(Option, SnpNet.DhcpServer, SNP_IP_BYTES);
        Option += SNP_IP_BYTES;
    }
    *Option++ = 55;
    *Option++ = 2;
    *Option++ = 1;
    *Option++ = 3;
    *Option = 255;

    SnpPut16(Udp, SNP_DHCP_CLIENT_PORT);
    SnpPut16(Udp + 2, SNP_DHCP_SERVER_PORT);
    SnpPut16(Udp + 4, SNP_UDP_HEADER + SNP_DHCP_BYTES);
    Udp[6] = 0;
    Udp[7] = 0;
    SnpStoreChecksum(Udp + 6,
                     SnpChecksumAdd(Udp,
                                    SNP_UDP_HEADER + SNP_DHCP_BYTES,
                                    SnpPseudoSum(SnpZeroAddress,
                                                 SnpBroadcastAddress,
                                                 SNP_IP_UDP,
                                                 SNP_UDP_HEADER + SNP_DHCP_BYTES)));
    SnpFillIp(Frame + SNP_ETH_HEADER, SNP_IP_UDP, SnpZeroAddress, SnpBroadcastAddress, SNP_UDP_HEADER + SNP_DHCP_BYTES);
    SnpTransmit(Index, SNP_ETH_HEADER + SNP_IP_HEADER + SNP_UDP_HEADER + SNP_DHCP_BYTES);
}

static
VOID
SnpDhcpInput(
    _In_reads_bytes_(Length) const UCHAR* Dhcp,
    _In_ ULONG Length)
{
    UCHAR Type = 0, Mask[SNP_IP_BYTES], Router[SNP_IP_BYTES], Server[SNP_IP_BYTES];
    ULONG Offset = SNP_DHCP_OPTIONS;
    UCHAR Code, Size;

    if (Length < SNP_DHCP_OPTIONS ||
        Dhcp[0] != 2 ||
        SnpGet32(Dhcp + 4) != SnpNet.DhcpXid ||
        !RtlEqualMemory(Dhcp + 28, SnpNet.Mac, SNP_MAC_BYTES) ||
        SnpGet32(Dhcp + 236) != SNP_DHCP_COOKIE)
    {
        return;
    }

    RtlZeroMemory(Mask, sizeof(Mask));
    RtlZeroMemory(Router, sizeof(Router));
    RtlZeroMemory(Server, sizeof(Server));

    while (Offset < Length)
    {
        Code = Dhcp[Offset++];
        if (Code == 255)
            break;
        if (Code == 0)
            continue;
        if (Offset >= Length)
            break;

        Size = Dhcp[Offset++];
        if (Offset + Size > Length)
            break;

        if (Code == 53 && Size == 1)
            Type = Dhcp[Offset];
        else if (Code == 1 && Size == SNP_IP_BYTES)
            RtlCopyMemory(Mask, Dhcp + Offset, SNP_IP_BYTES);
        else if (Code == 3 && Size >= SNP_IP_BYTES)
            RtlCopyMemory(Router, Dhcp + Offset, SNP_IP_BYTES);
        else if (Code == 54 && Size == SNP_IP_BYTES)
            RtlCopyMemory(Server, Dhcp + Offset, SNP_IP_BYTES);

        Offset += Size;
    }

    if (SnpNet.DhcpWanted == SNP_DHCP_OFFER && Type == SNP_DHCP_OFFER)
    {
        if (SnpAddressIsZero(Dhcp + 16) || SnpAddressIsZero(Server))
            return;

        RtlCopyMemory(SnpNet.DhcpOffer, Dhcp + 16, SNP_IP_BYTES);
        RtlCopyMemory(SnpNet.DhcpServer, Server, SNP_IP_BYTES);
    }
    else if (SnpNet.DhcpWanted != SNP_DHCP_ACK ||
             (Type != SNP_DHCP_ACK && Type != SNP_DHCP_NAK) ||
             !RtlEqualMemory(Server, SnpNet.DhcpServer, SNP_IP_BYTES))
    {
        return;
    }
    else if (Type == SNP_DHCP_ACK)
    {
        RtlCopyMemory(SnpNet.DhcpOffer, Dhcp + 16, SNP_IP_BYTES);
    }

    RtlCopyMemory(SnpNet.DhcpMask, Mask, SNP_IP_BYTES);
    RtlCopyMemory(SnpNet.DhcpRouter, Router, SNP_IP_BYTES);
    SnpNet.DhcpReply = Type;
}

static
VOID
SnpUdpInput(
    _In_ const UCHAR* Ip,
    _In_reads_bytes_(Length) const UCHAR* Udp,
    _In_ ULONG Length)
{
    ULONG UdpLength;

    if (Length < SNP_UDP_HEADER)
        return;

    UdpLength = SnpGet16(Udp + 4);
    if (UdpLength < SNP_UDP_HEADER || UdpLength > Length)
        return;

    if ((Udp[6] | Udp[7]) &&
        SnpChecksumFold(SnpChecksumAdd(Udp, UdpLength, SnpPseudoSum(Ip + 12, Ip + 16, SNP_IP_UDP, UdpLength))) != 0xFFFF)
    {
        return;
    }

    if (SnpGet16(Udp) == SNP_DHCP_SERVER_PORT && SnpGet16(Udp + 2) == SNP_DHCP_CLIENT_PORT)
        SnpDhcpInput(Udp + SNP_UDP_HEADER, UdpLength - SNP_UDP_HEADER);
}

static
BOOLEAN
SnpTcpSend(
    _In_ UCHAR Flags,
    _In_ ULONG Sequence,
    _In_reads_bytes_opt_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    SNP_TCP* Connection = &SnpNet.Tcp;
    PUCHAR Frame, Ip, Tcp;
    ULONG Index, Options = 0, TcpLength, Window;

    Frame = SnpAcquireTx(&Index);
    if (!Frame)
        return FALSE;

    SnpFillEthernet(Frame, SnpNet.HopMac, SNP_ETHERTYPE_IPV4);
    Ip = Frame + SNP_ETH_HEADER;
    Tcp = Ip + SNP_IP_HEADER;

    if (Flags & SNP_TCP_SYN)
    {
        Tcp[20] = 2;
        Tcp[21] = 4;
        SnpPut16(Tcp + 22, SnpNet.Mss);
        Tcp[24] = 1;
        Tcp[25] = 3;
        Tcp[26] = 3;
        Tcp[27] = SNP_TCP_WINDOW_SCALE;
        Options = SNP_TCP_SYN_OPTIONS;
        Window = MAXUSHORT;
    }
    else
    {
        Window = min(SNP_TCP_WINDOW >> Connection->RcvScale, MAXUSHORT);
    }

    TcpLength = SNP_TCP_HEADER + Options + Length;
    SnpPut16(Tcp, Connection->LocalPort);
    SnpPut16(Tcp + 2, Connection->RemotePort);
    SnpPut32(Tcp + 4, Sequence);
    SnpPut32(Tcp + 8, (Flags & SNP_TCP_ACK) ? Connection->RcvNxt : 0);
    Tcp[12] = (UCHAR)(((SNP_TCP_HEADER + Options) / 4) << 4);
    Tcp[13] = Flags;
    SnpPut16(Tcp + 14, Window);
    Tcp[16] = 0;
    Tcp[17] = 0;
    Tcp[18] = 0;
    Tcp[19] = 0;
    if (Length)
        RtlCopyMemory(Tcp + SNP_TCP_HEADER + Options, Data, Length);

    SnpStoreChecksum(Tcp + 16,
                     SnpChecksumAdd(Tcp,
                                    TcpLength,
                                    SnpPseudoSum(SnpNet.Address, Connection->RemoteAddress, SNP_IP_TCP, TcpLength)));
    SnpFillIp(Ip, SNP_IP_TCP, SnpNet.Address, Connection->RemoteAddress, TcpLength);

    if (Flags & SNP_TCP_ACK)
    {
        Connection->PendingSegments = 0;
        Connection->AckNow = FALSE;
    }

    return SnpTransmit(Index, SNP_ETH_HEADER + SNP_IP_HEADER + TcpLength);
}

static
VOID
SnpTcpParseOptions(
    _In_reads_bytes_(Length) const UCHAR* Option,
    _In_ ULONG Length)
{
    UCHAR Kind, Size;

    SnpNet.Tcp.RcvScale = 0;

    while (Length)
    {
        Kind = Option[0];
        if (Kind == 0)
            break;

        if (Kind == 1)
        {
            Option++;
            Length--;
            continue;
        }

        if (Length < 2)
            break;

        Size = Option[1];
        if (Size < 2 || Size > Length)
            break;

        if (Kind == 3 && Size == 3)
            SnpNet.Tcp.RcvScale = SNP_TCP_WINDOW_SCALE;

        Option += Size;
        Length -= Size;
    }
}

static
BOOLEAN
SnpParseDecimal(
    _In_ PCSTR Text,
    _Out_ PULONGLONG Value)
{
    ULONGLONG Result = 0;

    while (*Text == ' ' || *Text == '\t')
        Text++;

    if (*Text < '0' || *Text > '9')
        return FALSE;

    for (; *Text >= '0' && *Text <= '9'; Text++)
    {
        if (Result > (MAXULONGLONG - 9) / 10)
            return FALSE;

        Result = Result * 10 + (*Text - '0');
    }

    *Value = Result;
    return TRUE;
}

static
BOOLEAN
SnpHttpParseHeader(VOID)
{
    SNP_HTTP* Http = &SnpNet.Http;
    ULONGLONG ContentLength = MAXULONGLONG, RangeStart = MAXULONGLONG, RangeTotal = 0, Status;
    PCHAR Line, Next, Text;

    if (_strnicmp(Http->Header, "HTTP/1.", 7) != 0)
        return FALSE;

    Text = strchr(Http->Header, ' ');
    if (!Text || !SnpParseDecimal(Text, &Status))
        return FALSE;

    Http->StatusCode = (ULONG)Status;

    for (Line = strstr(Http->Header, "\r\n"); Line && Line[2] != ANSI_NULL; Line = Next)
    {
        Line += 2;
        Next = strstr(Line, "\r\n");
        if (Next)
            *Next = ANSI_NULL;

        if (_strnicmp(Line, "Content-Length:", 15) == 0)
        {
            if (!SnpParseDecimal(Line + 15, &ContentLength))
                return FALSE;
        }
        else if (_strnicmp(Line, "Transfer-Encoding:", 18) == 0)
        {
            return FALSE;
        }
        else if (_strnicmp(Line, "Content-Range:", 14) == 0)
        {
            Text = strstr(Line + 14, "bytes");
            if (!Text || !SnpParseDecimal(Text + 5, &RangeStart))
                return FALSE;

            Text = strchr(Text, '/');
            if (!Text || !SnpParseDecimal(Text + 1, &RangeTotal))
                return FALSE;
        }

        if (!Next)
            break;

        *Next = '\r';
    }

    if (Http->StatusCode == 200)
    {
        if (ContentLength == 0 || ContentLength > MAXULONG)
            return FALSE;

        if (!Http->Image)
        {
            Http->Image = FwAllocatePages((SIZE_T)ContentLength);
            if (!Http->Image)
            {
                FwPrint(L"Not enough memory for a %I64u-byte download.\r\n", ContentLength);
                return FALSE;
            }

            Http->ImageSize = (ULONG)ContentLength;
        }
        else if (ContentLength != Http->ImageSize)
        {
            return FALSE;
        }

        Http->Received = 0;
        return TRUE;
    }

    if (Http->StatusCode == 206 &&
        Http->Image &&
        RangeStart == Http->Received &&
        RangeTotal == Http->ImageSize)
    {
        return TRUE;
    }

    FwPrint(L"The HTTP server answered with status %lu.\r\n", Http->StatusCode);
    return FALSE;
}

static
VOID
SnpHttpConsume(
    _In_reads_bytes_(Length) const UCHAR* Data,
    _In_ ULONG Length)
{
    SNP_HTTP* Http = &SnpNet.Http;
    ULONG Copy, Start, Used, i;

    if (Http->Failed)
        return;

    if (!Http->HeaderDone)
    {
        Start = Http->HeaderLength;
        Copy = min(Length, SNP_HTTP_HEADER_BYTES - Start);
        RtlCopyMemory(Http->Header + Start, Data, Copy);
        Http->HeaderLength += Copy;

        for (i = Start > 3 ? Start - 3 : 0; i + 4 <= Http->HeaderLength; i++)
        {
            if (RtlEqualMemory(Http->Header + i, "\r\n\r\n", 4))
                break;
        }

        if (i + 4 > Http->HeaderLength)
        {
            if (Http->HeaderLength == SNP_HTTP_HEADER_BYTES)
                Http->Failed = TRUE;
            return;
        }

        Used = i + 4 - Start;
        Http->Header[i + 2] = ANSI_NULL;
        Http->HeaderDone = TRUE;
        if (!SnpHttpParseHeader())
        {
            Http->Failed = TRUE;
            return;
        }

        Data += Used;
        Length -= Used;
    }

    Copy = min(Length, Http->ImageSize - Http->Received);
    RtlCopyMemory(Http->Image + Http->Received, Data, Copy);
    Http->Received += Copy;
}

static
VOID
SnpTcpInput(
    _In_ const UCHAR* Ip,
    _In_reads_bytes_(Length) const UCHAR* Tcp,
    _In_ ULONG Length)
{
    SNP_TCP* Connection = &SnpNet.Tcp;
    ULONG HeaderLength, Sequence, Acknowledgment, PayloadLength, End;
    UCHAR Flags;

    if (Connection->State == SnpTcpClosed ||
        Length < SNP_TCP_HEADER ||
        SnpGet16(Tcp) != Connection->RemotePort ||
        SnpGet16(Tcp + 2) != Connection->LocalPort ||
        !RtlEqualMemory(Ip + 12, Connection->RemoteAddress, SNP_IP_BYTES))
    {
        return;
    }

    HeaderLength = (Tcp[12] >> 4) * 4;
    if (HeaderLength < SNP_TCP_HEADER ||
        HeaderLength > Length ||
        SnpChecksumFold(SnpChecksumAdd(Tcp, Length, SnpPseudoSum(Ip + 12, Ip + 16, SNP_IP_TCP, Length))) != 0xFFFF)
    {
        return;
    }

    Flags = Tcp[13];
    Sequence = SnpGet32(Tcp + 4);
    Acknowledgment = SnpGet32(Tcp + 8);
    PayloadLength = Length - HeaderLength;

    if (Connection->State == SnpTcpSynSent)
    {
        if ((Flags & SNP_TCP_ACK) && Acknowledgment != Connection->Iss + 1)
        {
            if (!(Flags & SNP_TCP_RST))
            {
                SnpTcpSend(SNP_TCP_RST, Acknowledgment, NULL, 0);
                Connection->ResendSyn = TRUE;
            }
            return;
        }

        if (Flags & SNP_TCP_RST)
        {
            if (Flags & SNP_TCP_ACK)
                Connection->PeerReset = TRUE;
            return;
        }

        if ((Flags & (SNP_TCP_SYN | SNP_TCP_ACK)) != (SNP_TCP_SYN | SNP_TCP_ACK))
            return;

        Connection->RcvNxt = Sequence + 1;
        Connection->SndUna = Acknowledgment;
        SnpTcpParseOptions(Tcp + SNP_TCP_HEADER, HeaderLength - SNP_TCP_HEADER);
        Connection->State = SnpTcpEstablished;
        Connection->DataTick = SnpNet.Ticks;
        return;
    }

    if (Flags & SNP_TCP_RST)
    {
        if (!SNP_SEQ_LT(Sequence, Connection->RcvNxt) &&
            SNP_SEQ_LT(Sequence, Connection->RcvNxt + SNP_TCP_WINDOW))
        {
            Connection->PeerReset = TRUE;
        }
        return;
    }

    if (Flags & SNP_TCP_SYN)
    {
        Connection->AckNow = TRUE;
        return;
    }

    if ((Flags & SNP_TCP_ACK) &&
        SNP_SEQ_GT(Acknowledgment, Connection->SndUna) &&
        SNP_SEQ_LEQ(Acknowledgment, Connection->SndNxt))
    {
        Connection->SndUna = Acknowledgment;
    }

    if (PayloadLength == 0 && !(Flags & SNP_TCP_FIN))
        return;

    if (SNP_SEQ_GT(Sequence, Connection->RcvNxt))
    {
        Connection->AckNow = TRUE;
        return;
    }

    End = Sequence + PayloadLength;
    if (SNP_SEQ_GT(End, Connection->RcvNxt))
    {
        SnpHttpConsume(Tcp + HeaderLength + (Connection->RcvNxt - Sequence), End - Connection->RcvNxt);
        Connection->RcvNxt = End;
        Connection->DataTick = SnpNet.Ticks;
        if (++Connection->PendingSegments >= SNP_TCP_ACK_SEGMENTS)
            Connection->AckNow = TRUE;
    }
    else
    {
        Connection->AckNow = TRUE;
    }

    if ((Flags & SNP_TCP_FIN) && End == Connection->RcvNxt && !Connection->PeerFin)
    {
        Connection->RcvNxt++;
        Connection->PeerFin = TRUE;
        Connection->AckNow = TRUE;
    }
}

static
VOID
SnpIpInput(
    _In_reads_bytes_(SNP_ETH_HEADER) const UCHAR* Frame,
    _In_reads_bytes_(Length) const UCHAR* Ip,
    _In_ ULONG Length)
{
    ULONG HeaderLength, TotalLength;

    if (Length < SNP_IP_HEADER || (Ip[0] >> 4) != 4)
        return;

    HeaderLength = (Ip[0] & 0x0F) * 4;
    TotalLength = SnpGet16(Ip + 2);
    if (HeaderLength < SNP_IP_HEADER ||
        TotalLength < HeaderLength ||
        TotalLength > Length ||
        (SnpGet16(Ip + 6) & SNP_IP_FRAGMENT_MASK) ||
        SnpChecksumFold(SnpChecksumAdd(Ip, HeaderLength, 0)) != 0xFFFF)
    {
        return;
    }

    if (!SnpAddressIsZero(SnpNet.Address) &&
        !RtlEqualMemory(Ip + 16, SnpNet.Address, SNP_IP_BYTES) &&
        !RtlEqualMemory(Ip + 16, SnpBroadcastAddress, SNP_IP_BYTES))
    {
        return;
    }

    switch (Ip[9])
    {
        case SNP_IP_TCP:
            SnpTcpInput(Ip, Ip + HeaderLength, TotalLength - HeaderLength);
            break;

        case SNP_IP_UDP:
            SnpUdpInput(Ip, Ip + HeaderLength, TotalLength - HeaderLength);
            break;

        case SNP_IP_ICMP:
            SnpIcmpInput(Frame, Ip, Ip + HeaderLength, TotalLength - HeaderLength);
            break;
    }
}

static
ULONG
SnpPoll(
    _In_ ULONG Budget)
{
    PUCHAR Frame = SnpNet.Rx + SNP_FRAME_PAD;
    UINTN Size;
    ULONG Count;

    for (Count = 0;
         Count < Budget && !(SnpNet.Tcp.AckNow && SnpNet.Tcp.State == SnpTcpEstablished);
         Count++)
    {
        Size = SNP_FRAME_BYTES - SNP_FRAME_PAD;
        if (SnpNet.Snp->Receive(SnpNet.Snp, NULL, &Size, Frame, NULL, NULL, NULL) != EFI_SUCCESS)
            break;

        if (Size < SNP_ETH_HEADER)
            continue;

        switch (SnpGet16(Frame + 12))
        {
            case SNP_ETHERTYPE_IPV4:
                SnpIpInput(Frame, Frame + SNP_ETH_HEADER, (ULONG)Size - SNP_ETH_HEADER);
                break;

            case SNP_ETHERTYPE_ARP:
                SnpArpInput(Frame + SNP_ETH_HEADER, (ULONG)Size - SNP_ETH_HEADER);
                break;
        }
    }

    return Count;
}

static
BOOLEAN
SnpDhcpExchange(
    _In_ UCHAR Type,
    _In_ UCHAR Wanted,
    _In_ ULONG Ticks)
{
    ULONG Deadline;

    SnpNet.DhcpWanted = Wanted;
    SnpNet.DhcpReply = 0;
    SnpSendDhcp(Type);

    for (Deadline = SnpNow() + Ticks;
         !SnpNet.DhcpReply && SNP_TICK_BEFORE(SnpNow(), Deadline);
         SnpPoll(SNP_POLL_BUDGET))
    {
    }

    return SnpNet.DhcpReply != 0;
}

static
BOOLEAN
SnpDhcp(VOID)
{
    ULONG Attempt;

    for (Attempt = 0; Attempt < SNP_DHCP_ATTEMPTS; Attempt++)
    {
        SnpNet.DhcpXid = SnpRandom() ^ SnpNow();
        if (!SnpDhcpExchange(SNP_DHCP_DISCOVER,
                             SNP_DHCP_OFFER,
                             SNP_TICKS_PER_SECOND << min(Attempt, 2)))
        {
            continue;
        }

        if ((SnpDhcpExchange(SNP_DHCP_REQUEST, SNP_DHCP_ACK, 2 * SNP_TICKS_PER_SECOND) ||
             SnpDhcpExchange(SNP_DHCP_REQUEST, SNP_DHCP_ACK, 2 * SNP_TICKS_PER_SECOND)) &&
            SnpNet.DhcpReply == SNP_DHCP_ACK)
        {
            RtlCopyMemory(SnpNet.Address, SnpNet.DhcpOffer, SNP_IP_BYTES);
            RtlCopyMemory(SnpNet.Mask, SnpNet.DhcpMask, SNP_IP_BYTES);
            RtlCopyMemory(SnpNet.Gateway, SnpNet.DhcpRouter, SNP_IP_BYTES);
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
SnpResolve(
    _In_reads_bytes_(SNP_IP_BYTES) const UCHAR* Destination)
{
    const UCHAR* Hop = Destination;
    ULONG Attempt, Deadline, i;

    for (i = 0; i < SNP_IP_BYTES; i++)
    {
        if ((Destination[i] ^ SnpNet.Address[i]) & SnpNet.Mask[i])
        {
            if (!SnpAddressIsZero(SnpNet.Gateway))
                Hop = SnpNet.Gateway;
            break;
        }
    }

    if (SnpNet.HopResolved && RtlEqualMemory(SnpNet.HopAddress, Hop, SNP_IP_BYTES))
        return TRUE;

    RtlCopyMemory(SnpNet.HopAddress, Hop, SNP_IP_BYTES);
    SnpNet.HopResolved = FALSE;

    for (Attempt = 0; Attempt < SNP_ARP_ATTEMPTS && !SnpNet.HopResolved; Attempt++)
    {
        SnpSendArp(SNP_ARP_REQUEST, NULL, Hop);
        for (Deadline = SnpNow() + SNP_TICKS_PER_SECOND;
             !SnpNet.HopResolved && SNP_TICK_BEFORE(SnpNow(), Deadline);
             SnpPoll(SNP_POLL_BUDGET))
        {
        }
    }

    return SnpNet.HopResolved;
}

static
BOOLEAN
SnpTcpConnect(
    _In_reads_bytes_(SNP_IP_BYTES) const UCHAR* Server,
    _In_ USHORT Port)
{
    SNP_TCP* Connection = &SnpNet.Tcp;
    ULONG Attempt, Deadline;

    RtlZeroMemory(Connection, sizeof(*Connection));
    RtlCopyMemory(Connection->RemoteAddress, Server, SNP_IP_BYTES);
    Connection->RemotePort = Port;
    Connection->LocalPort = (USHORT)(SNP_TCP_PORT_BASE + SnpRandom() % SNP_TCP_PORT_COUNT);
    Connection->Iss = SnpRandom();
    Connection->SndUna = Connection->Iss;
    Connection->SndNxt = Connection->Iss + 1;
    Connection->State = SnpTcpSynSent;

    for (Attempt = 0;
         Attempt < SNP_TCP_SYN_ATTEMPTS && Connection->State == SnpTcpSynSent && !Connection->PeerReset;
         Attempt++)
    {
        BOOLEAN Resent = FALSE;

        SnpTcpSend(SNP_TCP_SYN, Connection->Iss, NULL, 0);
        for (Deadline = SnpNow() + (SNP_TICKS_PER_SECOND << min(Attempt, 3));
             Connection->State == SnpTcpSynSent && !Connection->PeerReset && SNP_TICK_BEFORE(SnpNow(), Deadline);
             SnpPoll(SNP_POLL_BUDGET))
        {
            if (Connection->ResendSyn && !Resent)
            {
                SnpTcpSend(SNP_TCP_SYN, Connection->Iss, NULL, 0);
                Resent = TRUE;
            }
            Connection->ResendSyn = FALSE;
        }
    }

    if (Connection->State == SnpTcpEstablished)
        return TRUE;

    Connection->State = SnpTcpClosed;
    Connection->AckNow = FALSE;
    return FALSE;
}

static
VOID
SnpTcpClose(
    _In_ BOOLEAN Graceful)
{
    SNP_TCP* Connection = &SnpNet.Tcp;
    ULONG Deadline;

    if (Connection->State != SnpTcpEstablished)
    {
        Connection->State = SnpTcpClosed;
        return;
    }

    if (!Graceful || Connection->PeerReset)
    {
        if (!Connection->PeerReset)
            SnpTcpSend(SNP_TCP_RST | SNP_TCP_ACK, Connection->SndNxt, NULL, 0);
        Connection->State = SnpTcpClosed;
        return;
    }

    SnpTcpSend(SNP_TCP_FIN | SNP_TCP_ACK, Connection->SndNxt, NULL, 0);
    Connection->SndNxt++;

    for (Deadline = SnpNow() + SNP_CLOSE_TIMEOUT;
         !(Connection->PeerFin && Connection->SndUna == Connection->SndNxt) &&
         !Connection->PeerReset &&
         SNP_TICK_BEFORE(SnpNow(), Deadline);)
    {
        SnpPoll(SNP_POLL_BUDGET);
        if (Connection->AckNow)
            SnpTcpSend(SNP_TCP_ACK, Connection->SndNxt, NULL, 0);
    }

    Connection->State = SnpTcpClosed;
}

static
BOOLEAN
SnpHttpTransfer(
    _In_reads_bytes_(SNP_IP_BYTES) const UCHAR* Server,
    _In_ USHORT Port,
    _In_ PCSTR Authority,
    _In_ PCSTR Path)
{
    SNP_TCP* Connection = &SnpNet.Tcp;
    SNP_HTTP* Http = &SnpNet.Http;
    CHAR Request[SNP_HTTP_REQUEST_BYTES];
    CHAR Range[48] = "";
    ULONG RequestLength, RequestTick, Retries = 0, Idle = 0, Now, Polled, Acked;
    ULONG ProgressTick, ProgressBytes;
    BOOLEAN Complete;

    if (Http->Image && Http->Received)
        RtlStringCbPrintfA(Range, sizeof(Range), "Range: bytes=%lu-\r\n", Http->Received);

    if (!NT_SUCCESS(RtlStringCbPrintfA(Request,
                                       sizeof(Request),
                                       "GET %s HTTP/1.1\r\n"
                                       "Host: %s\r\n"
                                       "User-Agent: LiberNT\r\n"
                                       "Accept: */*\r\n"
                                       "Connection: close\r\n"
                                       "%s\r\n",
                                       Path,
                                       Authority,
                                       Range)))
    {
        Http->Failed = TRUE;
        return FALSE;
    }

    RequestLength = (ULONG)strlen(Request);
    Http->HeaderLength = 0;
    Http->HeaderDone = FALSE;

    if (!SnpTcpConnect(Server, Port))
    {
        FwPrint(L"The HTTP server %u.%u.%u.%u:%u %s.\r\n",
                Server[0], Server[1], Server[2], Server[3], Port,
                SnpNet.Tcp.PeerReset ? L"refused the connection" : L"did not answer");
        return FALSE;
    }

    SnpTcpSend(SNP_TCP_ACK | SNP_TCP_PSH, Connection->SndNxt, Request, RequestLength);
    Connection->SndNxt += RequestLength;
    RequestTick = SnpNow();
    ProgressTick = RequestTick;
    ProgressBytes = Http->Received;

    while (!Http->Failed && !Connection->PeerReset && !Connection->PeerFin)
    {
        Polled = SnpPoll(SNP_POLL_BUDGET);
        Idle = Polled ? 0 : Idle + 1;

        if (Connection->AckNow || (Connection->PendingSegments && Idle >= SNP_TCP_IDLE_POLLS))
            SnpTcpSend(SNP_TCP_ACK, Connection->SndNxt, NULL, 0);

        if (Http->HeaderDone && Http->Received == Http->ImageSize)
            break;

        Now = SnpNow();
        if (SNP_SEQ_LT(Connection->SndUna, Connection->SndNxt) &&
            Now - RequestTick >= SNP_TICKS_PER_SECOND)
        {
            if (++Retries > SNP_TCP_REQUEST_RETRIES)
                break;

            Acked = Connection->SndUna - (Connection->Iss + 1);
            SnpTcpSend(SNP_TCP_ACK | SNP_TCP_PSH, Connection->SndUna, Request + Acked, RequestLength - Acked);
            RequestTick = Now;
        }

        if (Now - Connection->DataTick >= SNP_DATA_TIMEOUT)
            break;

        if (Http->HeaderDone && Now - ProgressTick >= SNP_TICKS_PER_SECOND)
        {
            FwPrint(L"\r%lu of %lu MiB, %lu MiB/s   ",
                    Http->Received >> 20,
                    Http->ImageSize >> 20,
                    (ULONG)(((ULONGLONG)(Http->Received - ProgressBytes) * SNP_TICKS_PER_SECOND /
                             (Now - ProgressTick)) >> 20));
            ProgressTick = Now;
            ProgressBytes = Http->Received;
        }
    }

    Complete = Http->HeaderDone && !Http->Failed && Http->ImageSize && Http->Received == Http->ImageSize;
    if (Complete && (Connection->AckNow || Connection->PendingSegments))
        SnpTcpSend(SNP_TCP_ACK, Connection->SndNxt, NULL, 0);

    SnpTcpClose(Complete);
    return Complete;
}

static
BOOLEAN
SnpParseUrl(
    _In_ PCSTR Url,
    _Out_writes_z_(SNP_AUTHORITY_CHARS) PSTR Authority,
    _Out_ PCSTR* Path,
    _Out_writes_bytes_(SNP_IP_BYTES) PUCHAR Server,
    _Out_ PUSHORT Port)
{
    EFI_IPv4_ADDRESS Address;
    PCSTR Cursor, End;
    ULONG Value = 80;

    if (_strnicmp(Url, "http://", 7) != 0)
        return FALSE;

    Cursor = Url + 7;
    for (End = Cursor; *End != ANSI_NULL && *End != '/'; End++)
    {
    }

    if (End - Cursor >= SNP_AUTHORITY_CHARS || !UefiParseIpv4Address(&Cursor, &Address))
        return FALSE;

    if (*Cursor == ':')
    {
        for (Value = 0, Cursor++; Cursor < End && *Cursor >= '0' && *Cursor <= '9'; Cursor++)
        {
            Value = Value * 10 + (*Cursor - '0');
            if (Value > MAXUSHORT)
                return FALSE;
        }
    }

    if (Cursor != End || Value == 0)
        return FALSE;

    RtlCopyMemory(Authority, Url + 7, End - (Url + 7));
    Authority[End - (Url + 7)] = ANSI_NULL;
    RtlCopyMemory(Server, Address.Addr, SNP_IP_BYTES);
    *Port = (USHORT)Value;
    *Path = *End ? End : "/";
    return TRUE;
}

static
VOID
SnpClose(VOID)
{
    EFI_SIMPLE_NETWORK_PROTOCOL* Snp = SnpNet.Snp;
    ULONG i;

    if (SnpNet.TickEvent)
        GlobalSystemTable->BootServices->CloseEvent(SnpNet.TickEvent);

    if (SnpNet.Buffers)
        SnpReclaimTx();

    if (SnpNet.InitialState != EfiSimpleNetworkInitialized)
    {
        Snp->Shutdown(Snp);
        if (SnpNet.InitialState == EfiSimpleNetworkStopped)
            Snp->Stop(Snp);
    }

    for (i = 0; i < SNP_TX_BUFFERS && !SnpNet.TxBusy[i]; i++)
    {
    }

    if (SnpNet.Buffers && i == SNP_TX_BUFFERS)
        FwFreePages(SnpNet.Buffers, (SNP_TX_BUFFERS + 1) * SNP_FRAME_BYTES);
}

static
EFI_STATUS
SnpOpen(VOID)
{
    EFI_BOOT_SERVICES* BootServices = GlobalSystemTable->BootServices;
    EFI_SIMPLE_NETWORK_PROTOCOL* Snp;
    EFI_HANDLE* Handles;
    EFI_STATUS Status;
    UINTN Count, i;
    TIME_FIELDS Time;
    ULONG Byte;

    Status = BootServices->LocateHandleBuffer(ByProtocol, &SnpGuid, NULL, &Count, &Handles);
    if (EFI_ERROR(Status) || Count == 0)
        return EFI_NOT_FOUND;

    FwPrint(L"Starting the network.\r\n");

    for (i = 0; i < Count && !SnpNet.Snp; i++)
    {
        if (EFI_ERROR(BootServices->HandleProtocol(Handles[i], &SnpGuid, (VOID**)&Snp)) ||
            Snp->Mode->HwAddressSize != SNP_MAC_BYTES ||
            Snp->Mode->MediaHeaderSize != SNP_ETH_HEADER)
        {
            continue;
        }

        SnpNet.InitialState = Snp->Mode->State;
        if (Snp->Mode->State == EfiSimpleNetworkStopped && EFI_ERROR(Snp->Start(Snp)))
            continue;

        if (Snp->Mode->State == EfiSimpleNetworkStarted && EFI_ERROR(Snp->Initialize(Snp, 0, 0)))
        {
            if (SnpNet.InitialState == EfiSimpleNetworkStopped && Snp->Mode->State == EfiSimpleNetworkStarted)
                Snp->Stop(Snp);
            continue;
        }

        if (Snp->Mode->State != EfiSimpleNetworkInitialized)
            continue;

        if (Snp->Mode->MediaPresentSupported)
        {
            Snp->GetStatus(Snp, NULL, NULL);
            if (!Snp->Mode->MediaPresent && SnpNet.InitialState != EfiSimpleNetworkInitialized)
            {
                Snp->Shutdown(Snp);
                if (SnpNet.InitialState == EfiSimpleNetworkStopped)
                    Snp->Stop(Snp);
                continue;
            }
        }

        Snp->ReceiveFilters(Snp,
                            Snp->Mode->ReceiveFilterMask &
                                (EFI_SIMPLE_NETWORK_RECEIVE_UNICAST | EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST),
                            0,
                            FALSE,
                            0,
                            NULL);
        SnpNet.Snp = Snp;
    }

    BootServices->FreePool(Handles);
    if (!SnpNet.Snp)
    {
        FwPrint(L"No network adapter has a link.\r\n");
        return EFI_NO_MEDIA;
    }

    RtlCopyMemory(SnpNet.Mac, SnpNet.Snp->Mode->CurrentAddress.Addr, SNP_MAC_BYTES);
    SnpNet.Mss = (USHORT)(min(SnpNet.Snp->Mode->MaxPacketSize, SNP_ETH_MTU) - SNP_IP_HEADER - SNP_TCP_HEADER);
    if (SnpNet.Snp->Mode->MaxPacketSize <= SNP_IP_HEADER + SNP_TCP_HEADER + SNP_TCP_DEFAULT_MSS)
        SnpNet.Mss = SNP_TCP_DEFAULT_MSS;

    SnpNet.Buffers = FwAllocatePages((SNP_TX_BUFFERS + 1) * SNP_FRAME_BYTES);
    if (!SnpNet.Buffers ||
        EFI_ERROR(BootServices->CreateEvent(EVT_TIMER, 0, NULL, NULL, &SnpNet.TickEvent)) ||
        EFI_ERROR(BootServices->SetTimer(SnpNet.TickEvent, TimerPeriodic, SNP_TICK_PERIOD)))
    {
        SnpClose();
        return EFI_OUT_OF_RESOURCES;
    }

    SnpNet.Rx = SnpNet.Buffers;
    for (i = 0; i < SNP_TX_BUFFERS; i++)
        SnpNet.Tx[i] = SnpNet.Buffers + (i + 1) * SNP_FRAME_BYTES;

    SnpNet.Seed = 0x9E3779B9;
    for (Byte = 0; Byte < SNP_MAC_BYTES; Byte++)
        SnpNet.Seed = SnpNet.Seed * 31 + SnpNet.Mac[Byte];
    if (FwGetTime(&Time))
        SnpNet.Seed ^= (((ULONG)Time.Hour * 60 + Time.Minute) * 60 + Time.Second) * 2654435761u;
    SnpNet.Seed ^= (ULONG)(ULONG_PTR)SnpNet.Buffers;
    if (!SnpNet.Seed)
        SnpNet.Seed = 1;
    SnpNet.IpId = (USHORT)SnpRandom();

    FwPrint(L"Network adapter %02X:%02X:%02X:%02X:%02X:%02X is up.\r\n",
            SnpNet.Mac[0], SnpNet.Mac[1], SnpNet.Mac[2],
            SnpNet.Mac[3], SnpNet.Mac[4], SnpNet.Mac[5]);
    return EFI_SUCCESS;
}

EFI_STATUS
SnpHttpDownload(
    _In_ PCSTR Url,
    _In_opt_ PCSTR StaticIp,
    _Out_ PVOID* Image,
    _Out_ PULONG ImageSize)
{
    CHAR Authority[SNP_AUTHORITY_CHARS];
    UCHAR Server[SNP_IP_BYTES];
    UEFI_NET_CONTEXT Static;
    EFI_STATUS Status;
    EFI_TPL OldTpl;
    PCSTR Path;
    USHORT Port;
    ULONG Attempt, Start, Elapsed;

    *Image = NULL;
    *ImageSize = 0;

    if (!SnpParseUrl(Url, Authority, &Path, Server, &Port))
        return EFI_UNSUPPORTED;

    RtlZeroMemory(&SnpNet, sizeof(SnpNet));
    if (StaticIp && *StaticIp)
    {
        if (!UefiParseStaticIpConfiguration(StaticIp, &Static))
        {
            FwPrint(L"The static IPv4 configuration %hs is not valid.\r\n", StaticIp);
            return EFI_INVALID_PARAMETER;
        }

        RtlCopyMemory(SnpNet.Address, Static.LocalAddress.Addr, SNP_IP_BYTES);
        RtlCopyMemory(SnpNet.Mask, Static.SubnetMask.Addr, SNP_IP_BYTES);
        RtlCopyMemory(SnpNet.Gateway, Static.Gateway.Addr, SNP_IP_BYTES);
    }

    Status = SnpOpen();
    if (EFI_ERROR(Status))
        return Status;

    OldTpl = GlobalSystemTable->BootServices->RaiseTPL(TPL_CALLBACK);

    if (SnpAddressIsZero(SnpNet.Address) && !SnpDhcp())
    {
        FwPrint(L"No DHCP server answered.\r\n");
        Status = EFI_NO_RESPONSE;
    }
    else if (!SnpResolve(Server))
    {
        FwPrint(L"The address %u.%u.%u.%u could not be resolved.\r\n",
                SnpNet.HopAddress[0], SnpNet.HopAddress[1], SnpNet.HopAddress[2], SnpNet.HopAddress[3]);
        Status = EFI_NO_MAPPING;
    }
    else
    {
        FwPrint(L"Address %u.%u.%u.%u, downloading %hs\r\n",
                SnpNet.Address[0], SnpNet.Address[1], SnpNet.Address[2], SnpNet.Address[3], Url);

        Start = SnpNow();
        for (Attempt = 0;
             Attempt < SNP_HTTP_ATTEMPTS && !SnpNet.Http.Failed && !SnpHttpTransfer(Server, Port, Authority, Path);
             Attempt++)
        {
        }

        Elapsed = SnpNow() - Start;
        if (Attempt < SNP_HTTP_ATTEMPTS && !SnpNet.Http.Failed)
        {
            FwPrint(L"\rDownloaded %lu MiB in %lu.%02lu s (%lu MiB/s).\r\n",
                    SnpNet.Http.ImageSize >> 20,
                    Elapsed / SNP_TICKS_PER_SECOND,
                    Elapsed % SNP_TICKS_PER_SECOND,
                    (ULONG)(((ULONGLONG)SnpNet.Http.ImageSize * SNP_TICKS_PER_SECOND / max(Elapsed, 1)) >> 20));
            *Image = SnpNet.Http.Image;
            *ImageSize = SnpNet.Http.ImageSize;
            SnpNet.Http.Image = NULL;
        }
        else
        {
            FwPrint(L"\rThe download of %hs failed.\r\n", Url);
            Status = EFI_ABORTED;
        }
    }

    GlobalSystemTable->BootServices->RestoreTPL(OldTpl);

    if (SnpNet.Http.Image)
        FwFreePages(SnpNet.Http.Image, SnpNet.Http.ImageSize);

    SnpClose();
    return Status;
}
