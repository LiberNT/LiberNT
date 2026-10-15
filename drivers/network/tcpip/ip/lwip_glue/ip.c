#include <lwip/netif.h>
#include <lwip/tcpip.h>

#include <lwip/udp.h>
#include <lwip/ip.h>

#include "lwip_glue.h"
#include <address.h>
#include <datagram.h>
#include <udp.h>

typedef struct netif* PNETIF;

void
sys_shutdown(void);

void
LibIPInsertPacket(void *ifarg,
                  const void *const data,
                  const u32_t size)
{
    struct pbuf *p;

    ASSERT(ifarg);
    ASSERT(data);
    ASSERT(size > 0);

    p = pbuf_alloc(PBUF_RAW, size, PBUF_RAM);
    if (p)
    {
        ASSERT(p->tot_len == p->len);
        ASSERT(p->len == size);

        RtlCopyMemory(p->payload, data, p->len);

        ((PNETIF)ifarg)->input(p, (PNETIF)ifarg);
    }
}

void
LibIPAddressToLwip(const IP_ADDRESS *Address, ip_addr_t *IpAddr)
{
    if (Address->Type == IP_ADDRESS_V6)
    {
        IP_SET_TYPE(IpAddr, IPADDR_TYPE_V6);
        RtlCopyMemory(ip_2_ip6(IpAddr)->addr, Address->Address.IPv6Address, sizeof(ip_2_ip6(IpAddr)->addr));
        ip6_addr_clear_zone(ip_2_ip6(IpAddr));
    }
    else
    {
        ip_addr_set_ip4_u32_val(*IpAddr, Address->Address.IPv4Address);
    }
}

void
LibIPAddressFromLwip(const ip_addr_t *IpAddr, PIP_ADDRESS Address)
{
    if (IP_IS_V6(IpAddr))
    {
        Address->Type = IP_ADDRESS_V6;
        RtlCopyMemory(Address->Address.IPv6Address, ip_2_ip6(IpAddr)->addr, sizeof(Address->Address.IPv6Address));
    }
    else
    {
        AddrInitIPv4(Address, ip4_addr_get_u32(ip_2_ip4(IpAddr)));
    }
}

struct lib_ip_msg
{
    const IP_ADDRESS *Address;
    PADDRESS_FILE AddrFile;
    ip_addr_t IpAddress;
    u16_t Port;
    struct pbuf *Packet;
    err_t Error;
    BOOLEAN Result;
};

static
void
LibIPIsLocalAddressCallback(void *arg)
{
    struct lib_ip_msg *msg = arg;
    struct netif *netif;
    s8_t i;

    msg->Result = FALSE;
    NETIF_FOREACH(netif)
    {
        for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
        {
            if (ip6_addr_isvalid(netif_ip6_addr_state(netif, i)) &&
                ip6_addr_zoneless_eq(netif_ip6_addr(netif, i), ip_2_ip6(&msg->IpAddress)))
            {
                msg->Result = TRUE;
                return;
            }
        }
    }
}

BOOLEAN
LibIPIsLocalAddress(const IP_ADDRESS *Address)
{
    struct lib_ip_msg msg;

    if (Address->Type != IP_ADDRESS_V6)
        return FALSE;

    LibIPAddressToLwip(Address, &msg.IpAddress);
    if (tcpip_callback_wait(LibIPIsLocalAddressCallback, &msg) != ERR_OK)
        return FALSE;

    return msg.Result;
}

static
void
LibUDPRecvCallback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    PADDRESS_FILE AddrFile = arg;
    IP_ADDRESS SrcAddress, DstAddress;
    IP_PACKET Packet;
    PCHAR Data;

    if (!p)
        return;

    Data = ExAllocatePoolWithTag(NonPagedPool, max(p->tot_len, 1), LWIP_TAG);
    if (Data)
    {
        pbuf_copy_partial(p, Data, p->tot_len, 0);
        LibIPAddressFromLwip(addr, &SrcAddress);
        LibIPAddressFromLwip(ip_current_dest_addr(), &DstAddress);

        RtlZeroMemory(&Packet, sizeof(Packet));
        Packet.Type = IP_ADDRESS_V6;
        Packet.Header = Data;
        Packet.Data = Data;
        Packet.TotalSize = p->tot_len;
        Packet.SrcAddr = SrcAddress;
        Packet.DstAddr = DstAddress;

        DGDeliverData(AddrFile, &SrcAddress, &DstAddress, lwip_htons(port), AddrFile->Port,
                      &Packet, p->tot_len,
                      ip_current_input_netif() ? ip_current_input_netif()->state : NULL);

        ExFreePoolWithTag(Data, LWIP_TAG);
    }

    pbuf_free(p);
}

static
void
LibUDPOpenCallback(void *arg)
{
    struct lib_ip_msg *msg = arg;
    struct udp_pcb *pcb;

    pcb = udp_new_ip_type(IPADDR_TYPE_V6);
    if (!pcb)
    {
        msg->Error = ERR_MEM;
        return;
    }

    msg->Error = udp_bind(pcb, &msg->IpAddress, msg->Port);
    if (msg->Error != ERR_OK)
    {
        udp_remove(pcb);
        return;
    }

    udp_recv(pcb, LibUDPRecvCallback, msg->AddrFile);
    msg->AddrFile->UdpPcb = pcb;
    msg->Port = pcb->local_port;
}

NTSTATUS
LibUDPOpen(PADDRESS_FILE AddrFile, USHORT Port, PUSHORT AllocatedPort)
{
    struct lib_ip_msg msg;

    msg.AddrFile = AddrFile;
    msg.Port = lwip_ntohs(Port);
    msg.Error = ERR_OK;
    LibIPAddressToLwip(&AddrFile->Address, &msg.IpAddress);

    if (tcpip_callback_wait(LibUDPOpenCallback, &msg) != ERR_OK)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (msg.Error == ERR_USE)
        return STATUS_ADDRESS_ALREADY_EXISTS;
    if (msg.Error != ERR_OK)
        return STATUS_INSUFFICIENT_RESOURCES;

    *AllocatedPort = lwip_htons(msg.Port);
    return STATUS_SUCCESS;
}

static
void
LibUDPCloseCallback(void *arg)
{
    struct lib_ip_msg *msg = arg;

    if (msg->AddrFile->UdpPcb)
    {
        udp_remove(msg->AddrFile->UdpPcb);
        msg->AddrFile->UdpPcb = NULL;
    }
}

VOID
LibUDPClose(PADDRESS_FILE AddrFile)
{
    struct lib_ip_msg msg;

    msg.AddrFile = AddrFile;
    (void)tcpip_callback_wait(LibUDPCloseCallback, &msg);
}

static
void
LibUDPSendCallback(void *arg)
{
    struct lib_ip_msg *msg = arg;

    if (!msg->AddrFile->UdpPcb)
    {
        msg->Error = ERR_CLSD;
        return;
    }

    msg->Error = udp_sendto(msg->AddrFile->UdpPcb, msg->Packet, &msg->IpAddress, msg->Port);
}

NTSTATUS
LibUDPSendDatagram(PADDRESS_FILE AddrFile, PTDI_CONNECTION_INFORMATION ConnInfo,
                   PCHAR BufferData, ULONG DataSize, PULONG DataUsed)
{
    struct lib_ip_msg msg;
    IP_ADDRESS RemoteAddress;
    USHORT RemotePort;
    NTSTATUS Status;

    Status = AddrGetAddress((PTRANSPORT_ADDRESS)ConnInfo->RemoteAddress, &RemoteAddress, &RemotePort);
    if (!NT_SUCCESS(Status))
        return Status;

    if (RemoteAddress.Type != IP_ADDRESS_V6)
        return STATUS_INVALID_ADDRESS;

    if (DataSize > 0xFFFF - 8)
        return STATUS_INVALID_BUFFER_SIZE;

    msg.Packet = pbuf_alloc(PBUF_TRANSPORT, (u16_t)DataSize, PBUF_RAM);
    if (!msg.Packet)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (DataSize)
        RtlCopyMemory(msg.Packet->payload, BufferData, DataSize);

    msg.AddrFile = AddrFile;
    msg.Port = lwip_ntohs(RemotePort);
    msg.Error = ERR_OK;
    LibIPAddressToLwip(&RemoteAddress, &msg.IpAddress);

    if (tcpip_callback_wait(LibUDPSendCallback, &msg) != ERR_OK)
        msg.Error = ERR_MEM;

    pbuf_free(msg.Packet);

    switch (msg.Error)
    {
    case ERR_OK:
        *DataUsed = DataSize;
        return STATUS_SUCCESS;
    case ERR_RTE:
        return STATUS_NETWORK_UNREACHABLE;
    case ERR_MEM:
    case ERR_BUF:
        return STATUS_INSUFFICIENT_RESOURCES;
    default:
        return STATUS_INVALID_CONNECTION;
    }
}

void
LibIPInitialize(void)
{
    /* This completes asynchronously */
    tcpip_init(NULL, NULL);
}

void
LibIPShutdown(void)
{
    /* This is synchronous */
    sys_shutdown();
}
