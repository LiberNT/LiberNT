
#include "precomp.h"

#include "lwip/pbuf.h"
#include "lwip/netifapi.h"
#include "lwip/ip.h"
#include "lwip/api.h"
#include "lwip/tcpip.h"
#include "lwip/ethip6.h"
#include "lwip/mld6.h"
#include "lwip/prot/ethernet.h"
#include <ipifcons.h>

static
VOID
TCPLinkOutputComplete(
    PVOID Context,
    PNDIS_PACKET NdisPacket,
    NDIS_STATUS NdisStatus)
{
    FreeNdisPacket(NdisPacket);
}

static
err_t
TCPLinkOutput(struct netif *netif, struct pbuf *p)
{
    PIP_INTERFACE IF = netif->state;
    struct eth_hdr Header;
    PNDIS_PACKET NdisPacket;
    NDIS_STATUS NdisStatus;
    PCHAR Data;
    UINT Size;
    u16_t Length;

    if (p->tot_len <= SIZEOF_ETH_HDR)
        return ERR_ARG;

    if (pbuf_copy_partial(p, &Header, SIZEOF_ETH_HDR, 0) != SIZEOF_ETH_HDR ||
        Header.type != PP_HTONS(ETHTYPE_IPV6))
        return ERR_ARG;

    Length = p->tot_len - SIZEOF_ETH_HDR;

    NdisStatus = AllocatePacketWithBuffer(&NdisPacket, NULL, Length);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
        return ERR_MEM;

    GetDataPtr(NdisPacket, 0, &Data, &Size);
    if (pbuf_copy_partial(p, Data, Length, SIZEOF_ETH_HDR) != Length)
    {
        FreeNdisPacket(NdisPacket);
        return ERR_BUF;
    }

    PC(NdisPacket)->DLComplete = TCPLinkOutputComplete;

    (*IF->Transmit)(IF->Context, NdisPacket, 0, Header.dest.addr, LAN_PROTO_IPv6);

    return ERR_OK;
}

static
err_t
TCPMldMacFilter(struct netif *netif, const ip6_addr_t *group, enum netif_mac_filter_action action)
{
    PIP_INTERFACE IF = netif->state;
    const u8_t *GroupBytes = (const u8_t *)&group->addr[3];
    UCHAR Address[ETH_HWADDR_LEN];

    Address[0] = 0x33;
    Address[1] = 0x33;
    Address[2] = GroupBytes[0];
    Address[3] = GroupBytes[1];
    Address[4] = GroupBytes[2];
    Address[5] = GroupBytes[3];

    if (LANUpdateMulticastList(IF->Context, Address, action == NETIF_ADD_MAC_FILTER) != NDIS_STATUS_SUCCESS)
        return ERR_IF;

    return ERR_OK;
}

static
err_t
TCPLoopbackOutput6(struct netif *netif, struct pbuf *p, const ip6_addr_t *dest)
{
    return ERR_RTE;
}

err_t
TCPSendDataCallback(struct netif *netif, struct pbuf *p, const ip4_addr_t *dest)
{
    NDIS_STATUS NdisStatus;
    PNEIGHBOR_CACHE_ENTRY NCE;
    IP_PACKET Packet;
    IP_ADDRESS RemoteAddress, LocalAddress;
    PIPv4_HEADER Header;
    ULONG Length;
    ULONG TotalLength;

    /* The caller frees the pbuf struct */

    if (((*(u8_t*)p->payload) & 0xF0) == 0x40)
    {
        Header = p->payload;

        LocalAddress.Type = IP_ADDRESS_V4;
        LocalAddress.Address.IPv4Address = Header->SrcAddr;

        RemoteAddress.Type = IP_ADDRESS_V4;
        RemoteAddress.Address.IPv4Address = Header->DstAddr;
    }
    else
    {
        return ERR_IF;
    }

    IPInitializePacket(&Packet, LocalAddress.Type);

    if (!(NCE = RouteGetRouteToDestination(&RemoteAddress)))
    {
        return ERR_RTE;
    }

    NdisStatus = AllocatePacketWithBuffer(&Packet.NdisPacket, NULL, p->tot_len);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        NBDereferenceNeighbor(NCE);
        return ERR_MEM;
    }

    GetDataPtr(Packet.NdisPacket, 0, (PCHAR*)&Packet.Header, &Packet.TotalSize);
    Packet.MappedHeader = TRUE;

    ASSERT(Packet.TotalSize == p->tot_len);

    TotalLength = p->tot_len;
    Length = 0;
    while (Length < TotalLength)
    {
        ASSERT(p->len <= TotalLength - Length);
        ASSERT(p->tot_len == TotalLength - Length);
        RtlCopyMemory((PCHAR)Packet.Header + Length, p->payload, p->len);
        Length += p->len;
        p = p->next;
    }
    ASSERT(Length == TotalLength);

    Packet.HeaderSize = sizeof(IPv4_HEADER);
    Packet.TotalSize = TotalLength;
    Packet.SrcAddr = LocalAddress;
    Packet.DstAddr = RemoteAddress;

    NdisStatus = IPSendDatagram(&Packet, NCE);
    NBDereferenceNeighbor(NCE);
    if (!NT_SUCCESS(NdisStatus))
        return ERR_RTE;

    return 0;
}

VOID
TCPUpdateInterfaceLinkStatus(PIP_INTERFACE IF)
{
    ULONG OperationalStatus;

    GetInterfaceConnectionStatus(IF, &OperationalStatus);

    if (OperationalStatus == MIB_IF_OPER_STATUS_OPERATIONAL)
        netif_set_link_up(IF->TCPContext);
    else
        netif_set_link_down(IF->TCPContext);
}

err_t
TCPInterfaceInit(struct netif *netif)
{
    PIP_INTERFACE IF = netif->state;

    netif->hwaddr_len = IF->AddressLength;
    RtlCopyMemory(netif->hwaddr, IF->Address, netif->hwaddr_len);

    netif->output = TCPSendDataCallback;
    netif->mtu = IF->MTU;

    if (IF->AddressLength == ETH_HWADDR_LEN)
    {
        netif->output_ip6 = ethip6_output;
        netif->linkoutput = TCPLinkOutput;
        netif->mld_mac_filter = TCPMldMacFilter;
        netif->flags |= NETIF_FLAG_MLD6;
    }
    else
    {
        netif->output_ip6 = TCPLoopbackOutput6;
    }

    netif->name[0] = 'e';
    netif->name[1] = 'n';

    netif->flags |= NETIF_FLAG_BROADCAST;

    TCPUpdateInterfaceLinkStatus(IF);

    TCPUpdateInterfaceIPInformation(IF);

    return 0;
}

VOID
TCPRegisterInterface(PIP_INTERFACE IF)
{
    struct netif *Netif;
    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gw;

    gw.addr = 0;
    ipaddr.addr = 0;
    netmask.addr = 0;

    IF->TCPContext = netif_add(IF->TCPContext,
                               &ipaddr,
                               &netmask,
                               &gw,
                               IF,
                               TCPInterfaceInit,
                               tcpip_input);
    Netif = IF->TCPContext;
    if (!Netif)
        return;

    if (IF->AddressLength == ETH_HWADDR_LEN)
    {
        ip6_addr_t AllNodes;

        ip6_addr_set_allnodes_linklocal(&AllNodes);
        TCPMldMacFilter(Netif, &AllNodes, NETIF_ADD_MAC_FILTER);
        netif_create_ip6_linklocal_address(Netif, 1);
        netif_set_ip6_autoconfig_enabled(Netif, 1);
    }
    else
    {
        ip6_addr_t LoopbackAddress;
        s8_t Index;

        ip6_addr_set_loopback(&LoopbackAddress);
        if (netif_add_ip6_address(Netif, &LoopbackAddress, &Index) == ERR_OK)
            netif_ip6_addr_set_state(Netif, Index, IP6_ADDR_PREFERRED);
    }

    netif_set_up(Netif);
}

VOID
TCPUnregisterInterface(PIP_INTERFACE IF)
{
    netif_remove(IF->TCPContext);
}

VOID
TCPUpdateInterfaceIPInformation(PIP_INTERFACE IF)
{
    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gw;

    gw.addr = 0;

    GetInterfaceIPv4Address(IF,
                            ADE_UNICAST,
                            (PULONG)&ipaddr.addr);

    GetInterfaceIPv4Address(IF,
                            ADE_ADDRMASK,
                            (PULONG)&netmask.addr);

    netif_set_addr(IF->TCPContext, &ipaddr, &netmask, &gw);

    netif_set_up(IF->TCPContext);
    if (ipaddr.addr != 0)
        netif_set_default(IF->TCPContext);
}
