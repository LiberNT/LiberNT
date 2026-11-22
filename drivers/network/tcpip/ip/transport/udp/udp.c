/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        transport/udp/udp.c
 * PURPOSE:     User Datagram Protocol routines
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/08-2000 Created
 */

#include "precomp.h"

#include <icmp.h>

BOOLEAN UDPInitialized = FALSE;
PORT_SET UDPPorts;

NTSTATUS AddUDPHeaderIPv4(
    PADDRESS_FILE AddrFile,
    PIP_ADDRESS RemoteAddress,
    USHORT RemotePort,
    PIP_ADDRESS LocalAddress,
    USHORT LocalPort,
    PIP_PACKET IPPacket,
    PVOID Data,
    UINT DataLength)
/*
 * FUNCTION: Adds an IPv4 and UDP header to an IP packet
 * ARGUMENTS:
 *     SendRequest  = Pointer to send request
 *     LocalAddress = Pointer to our local address
 *     LocalPort    = The port we send this datagram from
 *     IPPacket     = Pointer to IP packet
 * RETURNS:
 *     Status of operation
 */
{
    PUDP_HEADER UDPHeader;
    NTSTATUS Status;

    TI_DbgPrint(MID_TRACE, ("Packet: %x NdisPacket %x\n",
			    IPPacket, IPPacket->NdisPacket));

    Status = AddGenericHeaderIPv4
        ( AddrFile, RemoteAddress, RemotePort,
          LocalAddress, LocalPort,
          IPPacket, DataLength, IPPROTO_UDP,
          sizeof(UDP_HEADER), (PVOID *)&UDPHeader );

    if (!NT_SUCCESS(Status))
        return Status;

    /* Port values are already big-endian values */
    UDPHeader->SourcePort = LocalPort;
    UDPHeader->DestPort   = RemotePort;
    UDPHeader->Checksum   = 0;
    /* Length of UDP header and data */
    UDPHeader->Length     = WH2N(DataLength + sizeof(UDP_HEADER));

    TI_DbgPrint(MID_TRACE, ("Copying data (hdr %x data %x (%d))\n",
			    IPPacket->Header, IPPacket->Data,
			    (PCHAR)IPPacket->Data - (PCHAR)IPPacket->Header));

    RtlCopyMemory(IPPacket->Data, Data, DataLength);

    if (!AddrFile->NoChecksum)
    {
        UDPHeader->Checksum = UDPv4ChecksumCalculate((PIPv4_HEADER)IPPacket->Header,
                                                     (PUCHAR)UDPHeader,
                                                     DataLength + sizeof(UDP_HEADER));
        UDPHeader->Checksum = WH2N(UDPHeader->Checksum);
    }

    TI_DbgPrint(MID_TRACE, ("Packet: %d ip %d udp %d payload\n",
			    (PCHAR)UDPHeader - (PCHAR)IPPacket->Header,
			    (PCHAR)IPPacket->Data - (PCHAR)UDPHeader,
			    DataLength));

    return STATUS_SUCCESS;
}


NTSTATUS BuildUDPPacket(
    PADDRESS_FILE AddrFile,
    PIP_PACKET Packet,
    PIP_ADDRESS RemoteAddress,
    USHORT RemotePort,
    PIP_ADDRESS LocalAddress,
    USHORT LocalPort,
    PCHAR DataBuffer,
    UINT DataLen )
/*
 * FUNCTION: Builds an UDP packet
 * ARGUMENTS:
 *     Context      = Pointer to context information (DATAGRAM_SEND_REQUEST)
 *     LocalAddress = Pointer to our local address
 *     LocalPort    = The port we send this datagram from
 *     IPPacket     = Address of pointer to IP packet
 * RETURNS:
 *     Status of operation
 */
{
    NTSTATUS Status;

    TI_DbgPrint(MAX_TRACE, ("Called.\n"));

    /* FIXME: Assumes IPv4 */
    IPInitializePacket(Packet, IP_ADDRESS_V4);

    Packet->TotalSize = sizeof(IPv4_HEADER) + sizeof(UDP_HEADER) + DataLen;

    /* Prepare packet */
    Status = AllocatePacketWithBuffer(&Packet->NdisPacket,
                                      NULL,
                                      Packet->TotalSize );

    if( !NT_SUCCESS(Status) )
    {
        Packet->Free(Packet);
        return Status;
    }

    TI_DbgPrint(MID_TRACE, ("Allocated packet: %x\n", Packet->NdisPacket));
    TI_DbgPrint(MID_TRACE, ("Local Addr : %s\n", A2S(LocalAddress)));
    TI_DbgPrint(MID_TRACE, ("Remote Addr: %s\n", A2S(RemoteAddress)));

    switch (RemoteAddress->Type) {
        case IP_ADDRESS_V4:
            Status = AddUDPHeaderIPv4(AddrFile, RemoteAddress, RemotePort,
                                      LocalAddress, LocalPort, Packet, DataBuffer, DataLen);
            break;
        case IP_ADDRESS_V6:
            /* FIXME: Support IPv6 */
            TI_DbgPrint(MIN_TRACE, ("IPv6 UDP datagrams are not supported.\n"));
        default:
            Status = STATUS_UNSUCCESSFUL;
            break;
    }
    if (!NT_SUCCESS(Status)) {
        TI_DbgPrint(MIN_TRACE, ("Cannot add UDP header. Status = (0x%X)\n",
                                Status));
        Packet->Free(Packet);
        return Status;
    }

    TI_DbgPrint(MID_TRACE, ("Displaying packet\n"));

    DISPLAY_IP_PACKET(Packet);

    TI_DbgPrint(MID_TRACE, ("Leaving\n"));

    return STATUS_SUCCESS;
}

static BOOLEAN UDPClassify(
    PADDRESS_FILE AddrFile,
    BOOLEAN Outbound,
    PIP_INTERFACE Interface,
    PIP_ADDRESS LocalAddress,
    USHORT LocalPort,
    PIP_ADDRESS RemoteAddress,
    USHORT RemotePort,
    PVOID IpHeader,
    ULONG IpHeaderSize,
    PVOID Data,
    ULONG DataSize,
    const WFP_SHIM_TAG *Tag)
{
    WFP_SHIM_DATAGRAM Datagram;
    UDP_HEADER UDPHeader;

    if (!WfpShimDatagramActive())
        return TRUE;

    UDPHeader.SourcePort = Outbound ? LocalPort : RemotePort;
    UDPHeader.DestPort = Outbound ? RemotePort : LocalPort;
    UDPHeader.Length = WH2N((USHORT)(DataSize + sizeof(UDP_HEADER)));
    UDPHeader.Checksum = 0;

    RtlZeroMemory(&Datagram, sizeof(Datagram));
    Datagram.EndpointId = AddrFile->WfpEndpointId;
    Datagram.Outbound = Outbound;
    Datagram.Loopback = (Interface == Loopback);
    Datagram.Protocol = IPPROTO_UDP;
    Datagram.LocalAddress = DN2H(LocalAddress->Address.IPv4Address);
    Datagram.RemoteAddress = DN2H(RemoteAddress->Address.IPv4Address);
    Datagram.LocalPort = WN2H(LocalPort);
    Datagram.RemotePort = WN2H(RemotePort);
    Datagram.InterfaceIndex = Interface->Index;
    Datagram.InterfaceType = (Interface == Loopback) ? IF_TYPE_SOFTWARE_LOOPBACK : IF_TYPE_ETHERNET_CSMACD;
    Datagram.ProcessId = AddrFile->ProcessId;
    Datagram.IpHeader = IpHeader;
    Datagram.IpHeaderSize = IpHeaderSize;
    Datagram.TransportHeader = &UDPHeader;
    Datagram.TransportHeaderSize = sizeof(UDPHeader);
    Datagram.Data = Data;
    Datagram.DataLength = DataSize;
    Datagram.Tag = *Tag;

    return WfpShimClassifyDatagram(&Datagram);
}

static
BOOLEAN
UDPIsBroadcastAddress(
    _In_ PIP_ADDRESS Address)
{
    BOOLEAN IsBroadcast = FALSE;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (Address->Type != IP_ADDRESS_V4)
        return FALSE;

    if (Address->Address.IPv4Address == 0xFFFFFFFF)
        return TRUE;

    TcpipAcquireSpinLock(&NetTableListLock, &OldIrql);

    for (Entry = NetTableListHead.Flink; Entry != &NetTableListHead; Entry = Entry->Flink) {
        PIP_NTE Nte = CONTAINING_RECORD(Entry, IP_NTE, ListEntry);

        if (Nte->Interface != Loopback && AddrIsEqual(&Nte->Broadcast, Address))
        {
            IsBroadcast = TRUE;
            break;
        }
    }

    TcpipReleaseSpinLock(&NetTableListLock, OldIrql);

    return IsBroadcast;
}

#define UDP_MAX_SEND_MSG_SIZE (0xFFFF - sizeof(IPv4_HEADER) - sizeof(UDP_HEADER))
#define UDP_MAX_SEND_MSG_SIZE6 (0xFFFF - sizeof(UDP_HEADER))

static NTSTATUS UDPReadControlInt(
    const TDI_CMSGHDR *Header,
    ULONG DataLength,
    LONG Minimum,
    LONG Maximum,
    PLONG Value)
{
    if (DataLength < sizeof(*Value))
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(Value, TDI_CMSG_DATA(Header), sizeof(*Value));
    if (*Value < Minimum || *Value > Maximum)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

NTSTATUS UDPParseSendControl(
    PTDI_CONNECTION_INFORMATION ConnInfo,
    BOOLEAN IgnorePktInfo,
    BOOLEAN Ipv6,
    PUDP_SEND_CONTROL Control)
{
    const UCHAR *Buffer = ConnInfo->Options;
    const TDI_CMSGHDR *Header, *SendMsgSize = NULL;
    ULONG Length, Offset, DataLength, Space, PktInfoLength;
    LONG Level = Ipv6 ? WS_IPPROTO_IPV6 : WS_IPPROTO_IP;
    NTSTATUS Status;
    LONG Value;

    C_ASSERT(WS_IP_PKTINFO == WS_IPV6_PKTINFO && WS_IP_HOPLIMIT == WS_IPV6_HOPLIMIT);
    C_ASSERT(WS_IP_ECN == WS_IPV6_ECN && WS_IP_DONTFRAGMENT == WS_IPV6_DONTFRAG);

    RtlZeroMemory(Control, sizeof(*Control));
    if (!Buffer || ConnInfo->OptionsLength <= 0)
        return STATUS_SUCCESS;

    PktInfoLength = Ipv6 ? sizeof(IPv6_RAW_ADDRESS) + sizeof(ULONG) : 2 * sizeof(ULONG);
    Length = (ULONG)ConnInfo->OptionsLength;
    for (Offset = 0; Length - Offset >= sizeof(TDI_CMSGHDR); Offset += Space)
    {
        Header = (const TDI_CMSGHDR *)(Buffer + Offset);
        ASSERT(((ULONG_PTR)Header & (TYPE_ALIGNMENT(TDI_CMSGHDR) - 1)) == 0);
        if (Header->cmsg_len < TDI_CMSG_LEN(0) || Header->cmsg_len > Length - Offset)
            return STATUS_INVALID_PARAMETER;
        Space = (ULONG)TDI_CMSGHDR_ALIGN(Header->cmsg_len);
        if (Space > Length - Offset)
            return STATUS_INVALID_PARAMETER;
        DataLength = (ULONG)(Header->cmsg_len - TDI_CMSG_LEN(0));
        Status = STATUS_SUCCESS;

        if (Header->cmsg_level == Level && Header->cmsg_type == WS_IP_PKTINFO)
        {
            if (!Control->PktInfo && !IgnorePktInfo)
            {
                if (DataLength < PktInfoLength)
                    return STATUS_INVALID_PARAMETER;
                if (Ipv6)
                {
                    Control->Source.Type = IP_ADDRESS_V6;
                    RtlCopyMemory(Control->Source.Address.IPv6Address, TDI_CMSG_DATA(Header), sizeof(IPv6_RAW_ADDRESS));
                }
                else
                {
                    RtlCopyMemory(&Value, TDI_CMSG_DATA(Header), sizeof(Value));
                    AddrInitIPv4(&Control->Source, (ULONG)Value);
                }
                RtlCopyMemory(&Control->InterfaceIndex, TDI_CMSG_DATA(Header) + PktInfoLength - sizeof(ULONG), sizeof(ULONG));
                Control->PktInfo = TRUE;
            }
        }
        else if (Header->cmsg_level == Level && Header->cmsg_type == WS_IP_HOPLIMIT)
        {
            if (!Control->HopLimit)
            {
                Status = UDPReadControlInt(Header, DataLength, 0, MAXUCHAR, &Value);
                Control->Ttl = (UCHAR)Value;
                Control->HopLimit = TRUE;
            }
        }
        else if (Header->cmsg_level == Level && Header->cmsg_type == WS_IP_ECN)
        {
            Status = UDPReadControlInt(Header, DataLength, 0, 2, &Value);
            Control->Ecn = (UCHAR)Value;
        }
        else if (Header->cmsg_level == Level && Header->cmsg_type == WS_IP_DONTFRAGMENT)
        {
            Status = UDPReadControlInt(Header, DataLength, MINLONG, MAXLONG, &Value);
            Control->DontFragment = (Value != 0);
            Control->DontFragmentSet = TRUE;
        }
        else if (!Ipv6 && Header->cmsg_level == WS_IPPROTO_IP && Header->cmsg_type == WS_IP_TOS)
        {
            Status = UDPReadControlInt(Header, DataLength, MINLONG, MAXLONG, &Value);
        }
        else if (Header->cmsg_level == WS_IPPROTO_UDP && Header->cmsg_type == WS_UDP_SEND_MSG_SIZE)
        {
            SendMsgSize = Header;
        }
        else
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (SendMsgSize)
    {
        if (SendMsgSize->cmsg_len != TDI_CMSG_LEN(sizeof(ULONG)))
            return STATUS_INVALID_PARAMETER;
        RtlCopyMemory(&Control->SendMsgSize, TDI_CMSG_DATA(SendMsgSize), sizeof(ULONG));
        if (Control->SendMsgSize > (Ipv6 ? UDP_MAX_SEND_MSG_SIZE6 : UDP_MAX_SEND_MSG_SIZE))
            return STATUS_INVALID_PARAMETER;
        Control->SendMsgSizeSet = TRUE;
    }

    ASSERT(Control->Ecn <= 2);
    ASSERT(!Control->PktInfo || Control->Source.Type == (Ipv6 ? IP_ADDRESS_V6 : IP_ADDRESS_V4));
    return STATUS_SUCCESS;
}

static BOOLEAN UDPIsLoopbackAddress(
    PIP_ADDRESS Address)
{
    return Address->Type == IP_ADDRESS_V4 && (DN2H(Address->Address.IPv4Address) >> 24) == 127;
}

static BOOLEAN UDPIsHeldNonUnicast(
    PIP_ADDRESS Address)
{
    ULONG Indexes[16], Count = 0, i;
    KIRQL OldIrql;
    IF_LIST_ITER(CurrentIF);

    if (UDPIsBroadcastAddress(Address))
        return TRUE;
    if ((DN2H(Address->Address.IPv4Address) & 0xf0000000) != 0xe0000000)
        return FALSE;

    TcpipAcquireSpinLock(&InterfaceListLock, &OldIrql);
    ForEachInterface(CurrentIF) {
        if (CurrentIF != Loopback && Count < RTL_NUMBER_OF(Indexes))
            Indexes[Count++] = CurrentIF->Index;
    } EndFor(CurrentIF);
    TcpipReleaseSpinLock(&InterfaceListLock, OldIrql);

    for (i = 0; i < Count; i++)
    {
        if (DN2H(Address->Address.IPv4Address) == 0xE0000001 || IGMPIsJoined(Indexes[i], Address->Address.IPv4Address))
            return TRUE;
    }
    return FALSE;
}

static NTSTATUS UDPLocatePktInfoInterface(
    const UDP_SEND_CONTROL *Control,
    PIP_INTERFACE *Interface)
{
    IP_ADDRESS Source = Control->Source;
    BOOLEAN Unspecified = (Source.Address.IPv4Address == 0);
    PIP_INTERFACE Located;

    ASSERT(Control->PktInfo);
    ASSERT(Source.Type == IP_ADDRESS_V4);

    *Interface = NULL;
    if (Control->InterfaceIndex)
    {
        Located = IPLocateInterfaceByIndex(Control->InterfaceIndex);
        if (!Located)
            return STATUS_INVALID_PARAMETER;
        if (!Unspecified && !IsInterfaceAddress(Located, &Source))
        {
            IPDereferenceInterface(Located);
            return STATUS_INVALID_ADDRESS;
        }
    }
    else if (!Unspecified)
    {
        Located = IPLocateUnicastInterface(&Source);
        if (!Located)
            return UDPIsHeldNonUnicast(&Source) ? STATUS_INVALID_ADDRESS : STATUS_INVALID_PARAMETER;
    }
    else
    {
        return STATUS_SUCCESS;
    }

    *Interface = Located;
    return STATUS_SUCCESS;
}

static NTSTATUS UDPRoutePktInfoOnInterface(
    const UDP_SEND_CONTROL *Control,
    ULONG MulticastSelector,
    PIP_ADDRESS RemoteAddress,
    BOOLEAN IsMulticast,
    BOOLEAN IsBroadcast,
    PIP_INTERFACE Interface,
    PNEIGHBOR_CACHE_ENTRY *Route)
{
    PIP_INTERFACE Selected;
    PNEIGHBOR_CACHE_ENTRY NCE;

    ASSERT(!(IsMulticast && IsBroadcast));

    if (IsMulticast)
    {
        if (!Control->InterfaceIndex && MulticastSelector)
        {
            Selected = GetMulticastInterface(MulticastSelector);
            if (Selected)
                IPDereferenceInterface(Selected);
            if (Selected != Interface)
                return STATUS_INVALID_ADDRESS;
        }
        NCE = RouteGetMulticastRouteOnInterface(RemoteAddress, Interface);
        if (!NCE)
            return STATUS_NETWORK_UNREACHABLE;
        *Route = NCE;
        return STATUS_SUCCESS;
    }

    if (IsBroadcast)
    {
        NCE = RouteGetRouteOnInterface(RemoteAddress, Interface);
        if (!NCE)
            return STATUS_NETWORK_UNREACHABLE;
        *Route = NCE;
        return STATUS_SUCCESS;
    }

    NCE = RouteGetRouteToDestination(RemoteAddress);
    if (!NCE)
        return STATUS_INVALID_ADDRESS;

    if (UDPIsLoopbackAddress(RemoteAddress) != (Interface == Loopback))
    {
        NBDereferenceNeighbor(NCE);
        return Interface == Loopback ? STATUS_NETWORK_UNREACHABLE : STATUS_INVALID_ADDRESS;
    }

    if (Interface != Loopback && NCE->Interface != Interface)
    {
        Selected = IPLocateUnicastInterface(RemoteAddress);
        if (Selected)
            IPDereferenceInterface(Selected);
        if (Selected != Interface)
        {
            NBDereferenceNeighbor(NCE);
            NCE = RouteGetRouteOnInterface(RemoteAddress, Interface);
            if (!NCE)
                return STATUS_NETWORK_UNREACHABLE;
        }
    }

    *Route = NCE;
    return STATUS_SUCCESS;
}

static NTSTATUS UDPRoutePktInfo(
    const UDP_SEND_CONTROL *Control,
    ULONG MulticastSelector,
    PIP_ADDRESS RemoteAddress,
    BOOLEAN IsMulticast,
    BOOLEAN IsBroadcast,
    PIP_ADDRESS LocalAddress,
    PNEIGHBOR_CACHE_ENTRY *Route)
{
    PIP_INTERFACE Interface;
    PNEIGHBOR_CACHE_ENTRY NCE;
    NTSTATUS Status;

    Status = UDPLocatePktInfoInterface(Control, &Interface);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Interface)
    {
        Status = UDPRoutePktInfoOnInterface(Control, MulticastSelector, RemoteAddress,
                                            IsMulticast, IsBroadcast, Interface, &NCE);
        IPDereferenceInterface(Interface);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    else
    {
        ASSERT(Control->Source.Address.IPv4Address == 0 && Control->InterfaceIndex == 0);
        if (IsMulticast)
            NCE = RouteGetMulticastRoute(RemoteAddress, MulticastSelector);
        else
            NCE = RouteGetRouteToDestination(RemoteAddress);
        if (!NCE)
            return STATUS_NETWORK_UNREACHABLE;
    }

    ASSERT(NCE != NULL);
    *LocalAddress = Control->Source;
    *Route = NCE;
    return STATUS_SUCCESS;
}

static VOID UDPApplySendControl(
    PIP_PACKET Packet,
    const UDP_SEND_CONTROL *Control,
    BOOLEAN DontFragment)
{
    PIPv4_HEADER Header = (PIPv4_HEADER)Packet->Header;

    ASSERT(Packet->Type == IP_ADDRESS_V4);
    ASSERT(Header->FlagsFragOfs == 0);
    if (DontFragment)
        Header->FlagsFragOfs = WH2N(IPv4_DF_MASK);
    if (!Control)
        return;
    ASSERT(Control->Ecn <= 2);
    if (Control->HopLimit)
        Header->Ttl = Control->Ttl;
    Header->Tos = (Header->Tos & ~3) | Control->Ecn;
}

static NTSTATUS UDPSendToAddress(
    PADDRESS_FILE AddrFile,
    IP_ADDRESS RemoteAddress,
    USHORT RemotePort,
    PCHAR BufferData,
    ULONG DataSize,
    const WFP_SHIM_TAG *Tag,
    const UDP_SEND_CONTROL *Control)
{
    IP_PACKET Packet;
    IP_PACKET LoopPacket;
    IP_ADDRESS LocalAddress;
    USHORT LocalPort;
    NTSTATUS Status;
    PNEIGHBOR_CACHE_ENTRY NCE;
    PNEIGHBOR_CACHE_ENTRY LoopNCE = NULL;
    BOOLEAN IsBroadcast;
    BOOLEAN IsMulticast;
    BOOLEAN DontFragment;

    IsBroadcast = UDPIsBroadcastAddress(&RemoteAddress);
    IsMulticast = ((DN2H(RemoteAddress.Address.IPv4Address) & 0xf0000000) == 0xe0000000);

    LockObject(AddrFile);

    DontFragment = (Control && Control->DontFragmentSet) ? Control->DontFragment : (AddrFile->DF != 0);
    LocalAddress = AddrFile->Address;
    if (LocalAddress.Type == IP_ADDRESS_V6)
    {
        ASSERT(AddrFile->DualStack);
        ASSERT(AddrIsUnspecified(&LocalAddress));
        AddrInitIPv4(&LocalAddress, 0);
    }
    if (Control && Control->PktInfo && AddrIsUnspecified(&LocalAddress))
    {
        Status = UDPRoutePktInfo(Control, AddrFile->MulticastInterface, &RemoteAddress,
                                 IsMulticast, IsBroadcast, &LocalAddress, &NCE);
        if (!NT_SUCCESS(Status))
        {
            UnlockObject(AddrFile);
            return Status;
        }
    }
    else if ((DN2H(RemoteAddress.Address.IPv4Address) & 0xf0000000) == 0xe0000000)
    {
        ULONG Selector = AddrFile->MulticastInterface;
        if (!Selector) Selector = LocalAddress.Address.IPv4Address;
        NCE = RouteGetMulticastRoute(&RemoteAddress, Selector);
        if (!NCE)
        {
            UnlockObject(AddrFile);
            return STATUS_NETWORK_UNREACHABLE;
        }
        if (AddrIsUnspecified(&LocalAddress)) GetInterfaceSourceAddress(NCE->Interface, &RemoteAddress, &NCE->Address, &LocalAddress);
    }
    else if (AddrFile->UnicastInterface)
    {
        PIP_INTERFACE Interface = GetMulticastInterface(AddrFile->UnicastInterface);

        NCE = Interface ? RouteGetRouteOnInterface(&RemoteAddress, Interface) : NULL;
        if (Interface)
            IPDereferenceInterface(Interface);
        if (!NCE)
        {
            UnlockObject(AddrFile);
            return STATUS_INVALID_ADDRESS;
        }
        if (AddrIsUnspecified(&LocalAddress)) GetInterfaceSourceAddress(NCE->Interface, &RemoteAddress, &NCE->Address, &LocalAddress);
    }
    else if (AddrIsUnspecified(&LocalAddress))
    {
        /* If the local address is unspecified (0),
         * then use the unicast address of the
         * interface we're sending over
         */
        if(!(NCE = RouteGetRouteToDestination( &RemoteAddress ))) {
            UnlockObject(AddrFile);
            return STATUS_NETWORK_UNREACHABLE;
        }

        GetInterfaceSourceAddress(NCE->Interface, &RemoteAddress, &NCE->Address, &LocalAddress);
    }
    else
    {
        PIP_INTERFACE Interface = AddrLocateInterface(&LocalAddress);

        if (!Interface) {
            UnlockObject(AddrFile);
            return STATUS_INVALID_PARAMETER;
        }

        NCE = RouteGetRouteFromSource(&RemoteAddress, Interface);
        IPDereferenceInterface(Interface);
        if (!NCE) {
            UnlockObject(AddrFile);
            return STATUS_NETWORK_UNREACHABLE;
        }
    }

    if (DontFragment && sizeof(IPv4_HEADER) + sizeof(UDP_HEADER) + DataSize > NCE->Interface->MTU)
    {
        UnlockObject(AddrFile);
        NBDereferenceNeighbor(NCE);
        return STATUS_BUFFER_OVERFLOW;
    }

    LocalPort = AddrFile->Port;
    UnlockObject(AddrFile);

    if (!UDPClassify(AddrFile,
                     TRUE,
                     NCE->Interface,
                     &LocalAddress,
                     LocalPort,
                     &RemoteAddress,
                     RemotePort,
                     NULL,
                     0,
                     BufferData,
                     DataSize,
                     Tag))
    {
        NBDereferenceNeighbor(NCE);
        return STATUS_SUCCESS;
    }

    LockObject(AddrFile);

    Status = BuildUDPPacket( AddrFile,
							 &Packet,
							 &RemoteAddress,
							 RemotePort,
							 &LocalAddress,
							 LocalPort,
							 BufferData,
							 DataSize );

    if (NT_SUCCESS(Status) && (IsBroadcast || IsMulticast) && Loopback != NULL && NCE->Interface != Loopback)
    {
        LoopNCE = NBLocateNeighbor(&Loopback->Unicast, Loopback);
        if (LoopNCE != NULL &&
            !NT_SUCCESS(BuildUDPPacket(AddrFile,
                                       &LoopPacket,
                                       &RemoteAddress,
                                       RemotePort,
                                       &LocalAddress,
                                       LocalPort,
                                       BufferData,
                                       DataSize)))
        {
            NBDereferenceNeighbor(LoopNCE);
            LoopNCE = NULL;
        }
    }

    UnlockObject(AddrFile);

    if( !NT_SUCCESS(Status) ) {
        NBDereferenceNeighbor(NCE);
	return Status;
    }

    UDPApplySendControl(&Packet, Control, DontFragment);
    if (LoopNCE != NULL)
        UDPApplySendControl(&LoopPacket, Control, DontFragment);

    Packet.WfpTag = *Tag;
    Status = IPSendDatagram(&Packet, NCE);
    NBDereferenceNeighbor(NCE);

    if (LoopNCE != NULL)
    {
        LoopPacket.WfpTag = *Tag;
        IPSendDatagram(&LoopPacket, LoopNCE);
        NBDereferenceNeighbor(LoopNCE);
    }

    return Status;
}

NTSTATUS UDPSendDatagram(
    PADDRESS_FILE AddrFile,
    PTDI_CONNECTION_INFORMATION ConnInfo,
    PCHAR BufferData,
    ULONG DataSize,
    PULONG DataUsed )
/*
 * FUNCTION: Sends an UDP datagram to a remote address
 * ARGUMENTS:
 *     Request   = Pointer to TDI request
 *     ConnInfo  = Pointer to connection information
 *     Buffer    = Pointer to NDIS buffer with data
 *     DataSize  = Size in bytes of data to be sent
 * RETURNS:
 *     Status of operation
 */
{
    static const WFP_SHIM_TAG NoTag;
    PTA_IP_ADDRESS RemoteAddressTa = (PTA_IP_ADDRESS)ConnInfo->RemoteAddress;
    UDP_SEND_CONTROL Control;
    IP_ADDRESS RemoteAddress;
    USHORT RemotePort;
    NTSTATUS Status;
    ULONG SendMsgSize;
    ULONG Offset;
    ULONG Chunk;

    TI_DbgPrint(MID_TRACE,("Sending Datagram(%x %x %x %d)\n",
						   AddrFile, ConnInfo, BufferData, DataSize));
    TI_DbgPrint(MID_TRACE,("RemoteAddressTa: %x\n", RemoteAddressTa));

    switch( RemoteAddressTa->Address[0].AddressType ) {
    case TDI_ADDRESS_TYPE_IP:
		RemoteAddress.Type = IP_ADDRESS_V4;
		RemoteAddress.Address.IPv4Address =
			RemoteAddressTa->Address[0].Address[0].in_addr;
		RemotePort = RemoteAddressTa->Address[0].Address[0].sin_port;
		break;

    case TDI_ADDRESS_TYPE_IP6:
        if (AddrFile->Family != TDI_ADDRESS_TYPE_IP6)
            return STATUS_UNSUCCESSFUL;
        Status = AddrGetAddress((PTRANSPORT_ADDRESS)RemoteAddressTa, &RemoteAddress, &RemotePort);
        if (!NT_SUCCESS(Status))
            return Status;
        if (!TCPUnmapFromDualStack(&RemoteAddress))
            return STATUS_INVALID_ADDRESS;
        break;

    default:
		return STATUS_UNSUCCESSFUL;
    }

    Status = UDPParseSendControl(ConnInfo, !AddrIsUnspecified(&AddrFile->Address), FALSE, &Control);
    if (!NT_SUCCESS(Status))
        return Status;

    SendMsgSize = Control.SendMsgSizeSet ? Control.SendMsgSize : AddrFile->SendMsgSize;
    if (SendMsgSize == 0 || SendMsgSize >= DataSize)
        SendMsgSize = DataSize;
    if (SendMsgSize > UDP_MAX_SEND_MSG_SIZE)
        return STATUS_BUFFER_OVERFLOW;

    Offset = 0;
    do
    {
        Chunk = min(SendMsgSize, DataSize - Offset);
        Status = UDPSendToAddress(AddrFile, RemoteAddress, RemotePort, BufferData + Offset, Chunk, &NoTag, &Control);
        if (!NT_SUCCESS(Status))
            return Status;
        Offset += Chunk;
    } while (Offset < DataSize);

    *DataUsed = DataSize;

    return STATUS_SUCCESS;
}

typedef struct _UDP_WFP_INJECTION
{
    BOOLEAN Send;
    ULONG64 EndpointId;
    ULONG RemoteAddress;
    ULONG InterfaceIndex;
    WFP_SHIM_TAG Tag;
    PWFP_SHIM_COMPLETE Complete;
    PVOID Context;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} UDP_WFP_INJECTION, *PUDP_WFP_INJECTION;

static NTSTATUS UDPInjectReceive(
    PUDP_WFP_INJECTION Injection)
{
    PIPv4_HEADER IPv4Header = (PIPv4_HEADER)Injection->Data;
    PIP_INTERFACE Interface = NULL;
    PNDIS_PACKET NdisPacket;
    IP_PACKET IPPacket;
    KIRQL OldIrql;
    IF_LIST_ITER(CurrentIF);

    if (Injection->Length < sizeof(IPv4_HEADER))
        return STATUS_INVALID_PARAMETER;

    if ((DN2H(IPv4Header->SrcAddr) >> 24) == 127 || (DN2H(IPv4Header->DstAddr) >> 24) == 127)
        return STATUS_DATA_NOT_ACCEPTED;

    TcpipAcquireSpinLock(&InterfaceListLock, &OldIrql);
    ForEachInterface(CurrentIF) {
        if (CurrentIF->Index == Injection->InterfaceIndex) {
            if (IPReferenceInterface(CurrentIF)) Interface = CurrentIF;
            break;
        }
    } EndFor(CurrentIF);
    TcpipReleaseSpinLock(&InterfaceListLock, OldIrql);
    if (!Interface)
        return STATUS_INVALID_PARAMETER;

    if (!NT_SUCCESS(AllocatePacketWithBuffer(&NdisPacket, (PCHAR)Injection->Data, Injection->Length)))
    {
        IPDereferenceInterface(Interface);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IPInitializePacket(&IPPacket, 0);
    IPPacket.NdisPacket = NdisPacket;
    GetDataPtr(NdisPacket, 0, (PCHAR *)&IPPacket.Header, &IPPacket.TotalSize);
    IPPacket.MappedHeader = TRUE;
    IPPacket.WfpTag = Injection->Tag;

    IPReceive(Interface, &IPPacket);
    IPDereferenceInterface(Interface);

    return IPPacket.WfpAccepted ? STATUS_SUCCESS : STATUS_DATA_NOT_ACCEPTED;
}

static VOID UDPInjectWorker(
    PVOID Context)
{
    PUDP_WFP_INJECTION Injection = Context;
    PUDP_HEADER UDPHeader = (PUDP_HEADER)Injection->Data;
    PADDRESS_FILE AddrFile;
    IP_ADDRESS RemoteAddress;
    NTSTATUS Status;

    if (!Injection->Send)
    {
        Status = UDPInjectReceive(Injection);
    }
    else if ((AddrFile = AddrFindByWfpEndpointId(Injection->EndpointId)) == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    else
    {
        if (AddrFile->Protocol != IPPROTO_UDP || AddrFile->Raw || Injection->Length < sizeof(UDP_HEADER))
        {
            Status = STATUS_INVALID_PARAMETER;
        }
        else
        {
            AddrInitIPv4(&RemoteAddress, DH2N(Injection->RemoteAddress));
            Status = UDPSendToAddress(AddrFile,
                                      RemoteAddress,
                                      UDPHeader->DestPort,
                                      (PCHAR)(UDPHeader + 1),
                                      Injection->Length - sizeof(UDP_HEADER),
                                      &Injection->Tag,
                                      NULL);
        }
        DereferenceObject(AddrFile);
    }

    Injection->Complete(Injection->Context, Status);
    ExFreePoolWithTag(Injection, PACKET_BUFFER_TAG);
}

static NTSTATUS UDPQueueInjection(
    BOOLEAN Send,
    ULONG64 EndpointId,
    ULONG RemoteAddress,
    ULONG InterfaceIndex,
    const VOID *Data,
    ULONG Length,
    const WFP_SHIM_TAG *Tag,
    PWFP_SHIM_COMPLETE Complete,
    PVOID Context)
{
    PUDP_WFP_INJECTION Injection;

    Injection = ExAllocatePoolWithTag(NonPagedPool,
                                      FIELD_OFFSET(UDP_WFP_INJECTION, Data) + Length,
                                      PACKET_BUFFER_TAG);
    if (!Injection)
        return STATUS_INSUFFICIENT_RESOURCES;

    Injection->Send = Send;
    Injection->EndpointId = EndpointId;
    Injection->RemoteAddress = RemoteAddress;
    Injection->InterfaceIndex = InterfaceIndex;
    Injection->Tag = *Tag;
    Injection->Complete = Complete;
    Injection->Context = Context;
    Injection->Length = Length;
    RtlCopyMemory(Injection->Data, Data, Length);

    if (!ChewCreate(UDPInjectWorker, Injection))
    {
        ExFreePoolWithTag(Injection, PACKET_BUFFER_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI UDPWfpInjectSend(
    ULONG64 EndpointId,
    ULONG RemoteAddress,
    const VOID *Datagram,
    ULONG Length,
    const WFP_SHIM_TAG *Tag,
    PWFP_SHIM_COMPLETE Complete,
    PVOID Context)
{
    return UDPQueueInjection(TRUE, EndpointId, RemoteAddress, 0, Datagram, Length, Tag, Complete, Context);
}

static NTSTATUS NTAPI UDPWfpInjectReceive(
    ULONG InterfaceIndex,
    const VOID *Packet,
    ULONG Length,
    const WFP_SHIM_TAG *Tag,
    PWFP_SHIM_COMPLETE Complete,
    PVOID Context)
{
    return UDPQueueInjection(FALSE, 0, 0, InterfaceIndex, Packet, Length, Tag, Complete, Context);
}


static
VOID
UDPDeliverToAddrFile(
    _In_ PADDRESS_FILE AddrFile,
    _In_ PIP_INTERFACE Interface,
    _In_ PIP_PACKET IPPacket,
    _In_ PIPv4_HEADER IPv4Header,
    _In_ PUDP_HEADER UDPHeader,
    _In_ UINT DataSize)
{
    if (!UDPClassify(AddrFile,
                     FALSE,
                     Interface,
                     &IPPacket->DstAddr,
                     UDPHeader->DestPort,
                     &IPPacket->SrcAddr,
                     UDPHeader->SourcePort,
                     IPv4Header,
                     IPPacket->HeaderSize,
                     IPPacket->Data,
                     DataSize,
                     &IPPacket->WfpTag))
    {
        return;
    }

    IPPacket->WfpAccepted = TRUE;
    DGDeliverData(AddrFile,
                  &IPPacket->SrcAddr,
                  &IPPacket->DstAddr,
                  UDPHeader->SourcePort,
                  UDPHeader->DestPort,
                  IPPacket,
                  DataSize,
                  Interface);
}

VOID UDPReceive(PIP_INTERFACE Interface, PIP_PACKET IPPacket)
/*
 * FUNCTION: Receives and queues a UDP datagram
 * ARGUMENTS:
 *     NTE      = Pointer to net table entry which the packet was received on
*     IPPacket = Pointer to an IP packet that was received
* NOTES:
*     This is the low level interface for receiving UDP datagrams. It strips
*     the UDP header from a packet and delivers the data to anyone that wants it
*/
{
  AF_SEARCH SearchContext;
  PIPv4_HEADER IPv4Header;
  PADDRESS_FILE AddrFile, Chosen = NULL;
  PUDP_HEADER UDPHeader;
  PIP_ADDRESS DstAddress, SrcAddress;
  UINT DataSize, i;
  BOOLEAN IsMulticast, DeliverToAll, ChosenSpecific = FALSE;
  ULONG MemberIndex;
  PIP_INTERFACE SourceInterface;

  TI_DbgPrint(MAX_TRACE, ("Called.\n"));

  switch (IPPacket->Type) {
  /* IPv4 packet */
  case IP_ADDRESS_V4:
    IPv4Header = IPPacket->Header;
    DstAddress = &IPPacket->DstAddr;
    SrcAddress = &IPPacket->SrcAddr;
    break;

  /* IPv6 packet */
  case IP_ADDRESS_V6:
    TI_DbgPrint(MIN_TRACE, ("Discarded IPv6 UDP datagram (%i bytes).\n", IPPacket->TotalSize));

    /* FIXME: IPv6 is not supported */
    return;

  default:
    return;
  }

  UDPHeader = (PUDP_HEADER)IPPacket->Data;

  /* Calculate and validate UDP checksum */
  i = UDPv4ChecksumCalculate(IPv4Header,
                             (PUCHAR)UDPHeader,
                             WH2N(UDPHeader->Length));
  if (i != DH2N(0x0000FFFF) && UDPHeader->Checksum != 0)
  {
      TI_DbgPrint(MIN_TRACE, ("Bad checksum on packet received.\n"));
      return;
  }

  /* Sanity checks */
  i = WH2N(UDPHeader->Length);
  if ((i < sizeof(UDP_HEADER)) || (i > IPPacket->TotalSize - IPPacket->Position)) {
    /* Incorrect or damaged packet received, discard it */
    TI_DbgPrint(MIN_TRACE, ("Incorrect or damaged UDP packet received.\n"));
    return;
  }

  DataSize = i - sizeof(UDP_HEADER);

  /* Go to UDP data area */
  IPPacket->Data = (PVOID)((ULONG_PTR)IPPacket->Data + sizeof(UDP_HEADER));

  IsMulticast = ((DN2H(DstAddress->Address.IPv4Address) & 0xf0000000) == 0xe0000000);
  MemberIndex = Interface->Index;
  if (IsMulticast && Interface == Loopback &&
      (SourceInterface = AddrLocateInterface(SrcAddress)))
  {
      MemberIndex = SourceInterface->Index;
      IPDereferenceInterface(SourceInterface);
  }

  /* Locate a receive request on destination address file object
     and deliver the packet if one is found. If there is no receive
     request on the address file object, call the associated receive
     handler. If no receive handler is registered, drop the packet */

  DeliverToAll = IsMulticast || UDPIsBroadcastAddress(DstAddress);

  AddrFile = AddrSearchFirst(DstAddress,
                             UDPHeader->DestPort,
                             IPPROTO_UDP,
                             &SearchContext);
  while (AddrFile != NULL) {
    if (DeliverToAll) {
      if (!IsMulticast || IGMPIsMember(AddrFile, MemberIndex, DstAddress->Address.IPv4Address))
          UDPDeliverToAddrFile(AddrFile, Interface, IPPacket, IPv4Header, UDPHeader, DataSize);
      DereferenceObject(AddrFile);
    } else if (Chosen == NULL || (!ChosenSpecific && !AddrIsUnspecified(&AddrFile->Address))) {
      if (Chosen != NULL)
          DereferenceObject(Chosen);
      Chosen = AddrFile;
      ChosenSpecific = !AddrIsUnspecified(&AddrFile->Address);
    } else {
      DereferenceObject(AddrFile);
    }
    AddrFile = AddrSearchNext(&SearchContext);
  }

  if (Chosen != NULL) {
    UDPDeliverToAddrFile(Chosen, Interface, IPPacket, IPv4Header, UDPHeader, DataSize);
    DereferenceObject(Chosen);
  } else if (!DeliverToAll && !AddrIsUnspecified(SrcAddress) && !UDPIsBroadcastAddress(SrcAddress) &&
             ((DN2H(SrcAddress->Address.IPv4Address) & 0xf0000000) != 0xe0000000)) {
    ICMPSendDestinationUnreachable(Interface, IPPacket, UDPHeader, ICMP_CODE_DU_PORT_UNREACH, 0);
  }
  TI_DbgPrint(MAX_TRACE, ("Leaving.\n"));
}

VOID UDPReceiveUnreachable(PIP_INTERFACE Interface, PIP_PACKET IPPacket)
/*
 * FUNCTION: Reports an ICMP port unreachable message to the socket whose datagram it answers
 * ARGUMENTS:
 *     Interface = Interface the message was received on
 *     IPPacket  = The ICMP message
 */
{
  PUCHAR Icmp = IPPacket->Data;
  UINT DataSize = IPPacket->TotalSize - IPPacket->HeaderSize;
  PADDRESS_FILE AddrFile, Chosen = NULL;
  IP_ADDRESS Local, Remote;
  PIP_INTERFACE Owner;
  AF_SEARCH SearchContext;
  PIPv4_HEADER Quoted;
  PUDP_HEADER QuotedUdp;
  ULONG QuotedHeader;

  UNREFERENCED_PARAMETER(Interface);

  if (DataSize < sizeof(ICMP_HEADER) + sizeof(IPv4_HEADER) + sizeof(UDP_HEADER))
    return;

  Quoted = (PIPv4_HEADER)(Icmp + sizeof(ICMP_HEADER));
  QuotedHeader = (Quoted->VerIHL & 0x0F) * 4;
  if ((Quoted->VerIHL >> 4) != 4 || QuotedHeader < sizeof(IPv4_HEADER) ||
      DataSize < sizeof(ICMP_HEADER) + QuotedHeader + sizeof(UDP_HEADER) || Quoted->Protocol != IPPROTO_UDP)
    return;

  /* The quoted datagram has to be one this host could have sent */
  AddrInitIPv4(&Local, Quoted->SrcAddr);
  AddrInitIPv4(&Remote, Quoted->DstAddr);
  Owner = AddrLocateInterface(&Local);
  if (!Owner)
    return;
  IPDereferenceInterface(Owner);

  QuotedUdp = (PUDP_HEADER)((PUCHAR)Quoted + QuotedHeader);
  AddrFile = AddrSearchFirst(&Local, QuotedUdp->SourcePort, IPPROTO_UDP, &SearchContext);
  while (AddrFile != NULL) {
    if (Chosen == NULL || (AddrIsUnspecified(&Chosen->Address) && !AddrIsUnspecified(&AddrFile->Address))) {
      if (Chosen != NULL)
        DereferenceObject(Chosen);
      Chosen = AddrFile;
    } else {
      DereferenceObject(AddrFile);
    }
    AddrFile = AddrSearchNext(&SearchContext);
  }

  if (Chosen != NULL) {
    DGDeliverError(Chosen, &Remote, QuotedUdp->DestPort, STATUS_PORT_UNREACHABLE);
    DereferenceObject(Chosen);
  }
}


static const WFP_SHIM_DISPATCH UDPWfpDispatch =
{
    UDPWfpInjectSend,
    UDPWfpInjectReceive
};

NTSTATUS UDPStartup(
  VOID)
/*
 * FUNCTION: Initializes the UDP subsystem
 * RETURNS:
 *     Status of operation
 */
{
  NTSTATUS Status;

  RtlZeroMemory(&UDPStats, sizeof(UDP_STATISTICS));

  Status = PortsStartup( &UDPPorts, 1, UDP_STARTING_PORT + UDP_DYNAMIC_PORTS );

  if( !NT_SUCCESS(Status) ) return Status;

  /* Register this protocol with IP layer */
  IPRegisterProtocol(IPPROTO_UDP, UDPReceive);
  WfpShimRegister(&UDPWfpDispatch);

  UDPInitialized = TRUE;

  return STATUS_SUCCESS;
}


NTSTATUS UDPShutdown(
  VOID)
/*
 * FUNCTION: Shuts down the UDP subsystem
 * RETURNS:
 *     Status of operation
 */
{
  if (!UDPInitialized)
      return STATUS_SUCCESS;

  PortsShutdown( &UDPPorts );
  WfpShimRegister(NULL);

  /* Deregister this protocol with IP layer */
  IPRegisterProtocol(IPPROTO_UDP, NULL);

  UDPInitialized = FALSE;

  return STATUS_SUCCESS;
}

UINT UDPAllocatePort( UINT HintPort ) {
    if( HintPort ) {
        if( AllocatePort( &UDPPorts, HintPort ) ) return HintPort;
        else return (UINT)-1;
    } else return AllocatePortFromRange
               ( &UDPPorts, DYNAMIC_PORT_FIRST, DYNAMIC_PORT_LAST );
}

VOID UDPFreePort( UINT Port ) {
    DeallocatePort( &UDPPorts, Port );
}

/* EOF */
