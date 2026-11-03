/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        transport/rawip/rawip.c
 * PURPOSE:     User Datagram Protocol routines
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/08-2000 Created
 */

#include "precomp.h"

NTSTATUS AddGenericHeaderIPv4(
    PADDRESS_FILE AddrFile,
    PIP_ADDRESS RemoteAddress,
    USHORT RemotePort,
    PIP_ADDRESS LocalAddress,
    USHORT LocalPort,
    PIP_PACKET IPPacket,
    UINT DataLength,
    UINT Protocol,
    UINT ExtraLength,
    PVOID *NextHeader )
/*
 * FUNCTION: Adds an IPv4 and RawIp header to an IP packet
 * ARGUMENTS:
 *     SendRequest  = Pointer to send request
 *     LocalAddress = Pointer to our local address
 *     LocalPort    = The port we send this datagram from
 *     IPPacket     = Pointer to IP packet
 * RETURNS:
 *     Status of operation
 */
{
    PIPv4_HEADER IPHeader;
    ULONG BufferSize;

    TI_DbgPrint(MID_TRACE, ("Packet: %x NdisPacket %x\n",
			    IPPacket, IPPacket->NdisPacket));

    BufferSize = sizeof(IPv4_HEADER) + ExtraLength;

    GetDataPtr( IPPacket->NdisPacket,
		0,
		(PCHAR *)&IPPacket->Header,
		&IPPacket->TotalSize );
    IPPacket->MappedHeader = TRUE;

    IPPacket->HeaderSize = sizeof(IPv4_HEADER);

    TI_DbgPrint(MAX_TRACE, ("Allocated %d bytes for headers at 0x%X.\n",
			    BufferSize, IPPacket->Header));
    TI_DbgPrint(MAX_TRACE, ("Packet total length %d\n", IPPacket->TotalSize));

    /* Build IPv4 header */
    IPHeader = (PIPv4_HEADER)IPPacket->Header;
    /* Version = 4, Length = 5 DWORDs */
    IPHeader->VerIHL = 0x45;
    /* Normal Type-of-Service */
    IPHeader->Tos = 0;
    /* Length of header and data */
    IPHeader->TotalLength = WH2N((USHORT)IPPacket->TotalSize);
    /* Identification */
    IPHeader->Id = (USHORT)Random();
    /* One fragment at offset 0 */
    IPHeader->FlagsFragOfs = 0;
    /* Time-to-Live */
    IPHeader->Ttl = (DN2H(RemoteAddress->Address.IPv4Address) & 0xf0000000) == 0xe0000000 ?
                   AddrFile->MulticastTTL : AddrFile->TTL;
    /* Protocol */
    IPHeader->Protocol = Protocol;
    /* Checksum is 0 (for later calculation of this) */
    IPHeader->Checksum = 0;
    /* Source address */
    IPHeader->SrcAddr = LocalAddress->Address.IPv4Address;
    /* Destination address. FIXME: IPv4 only */
    IPHeader->DstAddr = RemoteAddress->Address.IPv4Address;

    /* Build RawIp header */
    *NextHeader = (((PCHAR)IPHeader) + sizeof(IPv4_HEADER));
    IPPacket->Data = ((PCHAR)*NextHeader) + ExtraLength;

    return STATUS_SUCCESS;
}


NTSTATUS BuildRawIpPacket(
    PADDRESS_FILE AddrFile,
    PIP_PACKET Packet,
    PIP_ADDRESS RemoteAddress,
    USHORT RemotePort,
    PIP_ADDRESS LocalAddress,
    USHORT LocalPort,
    PCHAR DataBuffer,
    UINT DataLen )
/*
 * FUNCTION: Builds an RawIp packet
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
    PCHAR Payload;
    PCHAR PacketData;
    PIPv4_HEADER IPv4Header;
    UINT AvailableSize;
    UCHAR HeaderSize;

    TI_DbgPrint(MAX_TRACE, ("Called.\n"));

    /* FIXME: Assumes IPv4 */
    IPInitializePacket(Packet, IP_ADDRESS_V4);

    if (AddrFile->HeaderIncl)
    {
        if (RemoteAddress->Type != IP_ADDRESS_V4 ||
            DataLen < sizeof(IPv4_HEADER) ||
            DataLen > 65535)
            return STATUS_INVALID_PARAMETER;

        Packet->TotalSize = DataLen;
        Status = AllocatePacketWithBuffer(&Packet->NdisPacket, DataBuffer, DataLen);
        if (!NT_SUCCESS(Status))
            return Status;

        AvailableSize = 0;
        PacketData = NULL;
        GetDataPtr(Packet->NdisPacket, 0, &PacketData, &AvailableSize);
        if (!PacketData || AvailableSize < DataLen)
        {
            Packet->Free(Packet);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        IPv4Header = (PIPv4_HEADER)PacketData;
        HeaderSize = (IPv4Header->VerIHL & 0x0f) * 4;
        if ((IPv4Header->VerIHL >> 4) != 4 ||
            HeaderSize < sizeof(IPv4_HEADER) ||
            HeaderSize > DataLen ||
            ((WN2H(IPv4Header->FlagsFragOfs) & IPv4_FRAGOFS_MASK) << 3) + DataLen > 65535)
        {
            Packet->Free(Packet);
            return STATUS_INVALID_PARAMETER;
        }

        Packet->Header = PacketData;
        Packet->HeaderSize = HeaderSize;
        Packet->Data = PacketData + HeaderSize;
        Packet->MappedHeader = TRUE;
        Packet->Flags |= IP_PACKET_FLAG_RAW;
        return STATUS_SUCCESS;
    }

    Packet->TotalSize = sizeof(IPv4_HEADER) + DataLen;

    /* Prepare packet */
    Status = AllocatePacketWithBuffer( &Packet->NdisPacket,
				       NULL,
				       Packet->TotalSize );

    if( !NT_SUCCESS(Status) ) return Status;

    TI_DbgPrint(MID_TRACE, ("Allocated packet: %x\n", Packet->NdisPacket));
    TI_DbgPrint(MID_TRACE, ("Local Addr : %s\n", A2S(LocalAddress)));
    TI_DbgPrint(MID_TRACE, ("Remote Addr: %s\n", A2S(RemoteAddress)));

    switch (RemoteAddress->Type) {
    case IP_ADDRESS_V4:
	Status = AddGenericHeaderIPv4
            (AddrFile, RemoteAddress, RemotePort,
             LocalAddress, LocalPort, Packet, DataLen,
             AddrFile->Protocol,
             0, (PVOID *)&Payload );
	break;
    case IP_ADDRESS_V6:
	/* FIXME: Support IPv6 */
        Status = STATUS_UNSUCCESSFUL;
	TI_DbgPrint(MIN_TRACE, ("IPv6 RawIp datagrams are not supported.\n"));
        break;

    default:
	Status = STATUS_UNSUCCESSFUL;
        TI_DbgPrint(MIN_TRACE, ("Bad Address Type %d\n", RemoteAddress->Type));
	break;
    }

    if( !NT_SUCCESS(Status) ) {
        TI_DbgPrint(MIN_TRACE, ("Cannot add header. Status = (0x%X)\n",
                                Status));
        Packet->Free(Packet);
        return Status;
    }

    TI_DbgPrint(MID_TRACE, ("Copying data (hdr %x data %x (%d))\n",
			    Packet->Header, Packet->Data,
			    (PCHAR)Packet->Data - (PCHAR)Packet->Header));

    RtlCopyMemory( Packet->Data, DataBuffer, DataLen );

    Packet->Flags |= IP_PACKET_FLAG_RAW;

    TI_DbgPrint(MID_TRACE, ("Displaying packet\n"));

    DISPLAY_IP_PACKET(Packet);

    TI_DbgPrint(MID_TRACE, ("Leaving\n"));

    return STATUS_SUCCESS;
}

NTSTATUS RawIPSendDatagram(
    PADDRESS_FILE AddrFile,
    PTDI_CONNECTION_INFORMATION ConnInfo,
    PCHAR BufferData,
    ULONG DataSize,
    PULONG DataUsed )
/*
 * FUNCTION: Sends an RawIp datagram to a remote address
 * ARGUMENTS:
 *     Request   = Pointer to TDI request
 *     ConnInfo  = Pointer to connection information
 *     Buffer    = Pointer to NDIS buffer with data
 *     DataSize  = Size in bytes of data to be sent
 * RETURNS:
 *     Status of operation
 */
{
    IP_PACKET Packet;
    PTA_IP_ADDRESS RemoteAddressTa = (PTA_IP_ADDRESS)ConnInfo->RemoteAddress;
    IP_ADDRESS RemoteAddress,  LocalAddress;
    USHORT RemotePort;
    NTSTATUS Status;
    PNEIGHBOR_CACHE_ENTRY NCE;

    LockObject(AddrFile);

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

        default:
            UnlockObject(AddrFile);
            return STATUS_UNSUCCESSFUL;
    }

    TI_DbgPrint(MID_TRACE,("About to get route to destination\n"));

    LocalAddress = AddrFile->Address;
    if ((DN2H(RemoteAddress.Address.IPv4Address) & 0xf0000000) == 0xe0000000)
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
        NCE = RouteGetRouteToDestination( &RemoteAddress );
        if(!NCE) {
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

    Status = BuildRawIpPacket( AddrFile,
                               &Packet,
                               &RemoteAddress,
                               RemotePort,
                               &LocalAddress,
                               AddrFile->Port,
                               BufferData,
                               DataSize );

    UnlockObject(AddrFile);

    if( !NT_SUCCESS(Status) ) {
        NBDereferenceNeighbor(NCE);
        return Status;
    }

    TI_DbgPrint(MID_TRACE,("About to send datagram\n"));

    Status = IPSendDatagram(&Packet, NCE);
    NBDereferenceNeighbor(NCE);
    if (!NT_SUCCESS(Status))
        return Status;

    *DataUsed = DataSize;

    TI_DbgPrint(MID_TRACE,("Leaving\n"));

    return STATUS_SUCCESS;
}


BOOLEAN RawIpReceive(PIP_INTERFACE Interface, PIP_PACKET IPPacket)
/*
 * FUNCTION: Receives and queues a RawIp datagram
 * ARGUMENTS:
 *     NTE      = Pointer to net table entry which the packet was received on
*     IPPacket = Pointer to an IP packet that was received
* NOTES:
*     This is the low level interface for receiving RawIp datagrams. It strips
*     the RawIp header from a packet and delivers the data to anyone that wants it
*/
{
  AF_SEARCH SearchContext;
  PIPv4_HEADER IPv4Header;
  PADDRESS_FILE AddrFile;
  PIP_ADDRESS DstAddress, SrcAddress;
  UINT DataSize;
  BOOLEAN Delivered;

  TI_DbgPrint(MAX_TRACE, ("Called.\n"));

  switch (IPPacket->Type) {
  /* IPv4 packet */
  case IP_ADDRESS_V4:
    IPv4Header = IPPacket->Header;
    DstAddress = &IPPacket->DstAddr;
    SrcAddress = &IPPacket->SrcAddr;
    DataSize = IPPacket->TotalSize;
    break;

  /* IPv6 packet */
  case IP_ADDRESS_V6:
    TI_DbgPrint(MIN_TRACE, ("Discarded IPv6 datagram (%i bytes).\n", IPPacket->TotalSize));

    /* FIXME: IPv6 is not supported */
    return FALSE;

  default:
    return FALSE;
  }

  /* Locate a receive request on destination address file object
     and deliver the packet if one is found. If there is no receive
     request on the address file object, call the associated receive
     handler. If no receive handler is registered, drop the packet */

  AddrFile = AddrSearchFirst(DstAddress,
                             0,
                             IPv4Header->Protocol,
                             &SearchContext);
  Delivered = AddrFile != NULL;
  if (AddrFile) {
    do {
      DGDeliverData(AddrFile,
		    SrcAddress,
                    DstAddress,
                    0,
                    0,
                    IPPacket,
                    DataSize,
                    Interface);
      DereferenceObject(AddrFile);
    } while ((AddrFile = AddrSearchNext(&SearchContext)) != NULL);
  } else {
    /* There are no open address files that will take this datagram */
    /* FIXME: IPv4 only */
    TI_DbgPrint(MID_TRACE, ("Cannot deliver IPv4 raw datagram to address (0x%X).\n",
                            DN2H(DstAddress->Address.IPv4Address)));

    /* FIXME: Send ICMP reply */
  }
  TI_DbgPrint(MAX_TRACE, ("Leaving.\n"));
  return Delivered;
}


NTSTATUS RawIPStartup(VOID)
/*
 * FUNCTION: Initializes the UDP subsystem
 * RETURNS:
 *     Status of operation
 */
{
  RtlZeroMemory(&UDPStats, sizeof(UDP_STATISTICS));

  return STATUS_SUCCESS;
}


NTSTATUS RawIPShutdown(VOID)
/*
 * FUNCTION: Shuts down the UDP subsystem
 * RETURNS:
 *     Status of operation
 */
{
  return STATUS_SUCCESS;
}

/* EOF */
