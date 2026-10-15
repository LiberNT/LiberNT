#include <lwip/netif.h>
#include <lwip/tcpip.h>

#include <lwip/udp.h>
#include <lwip/ip.h>
#include <lwip/nd6.h>
#include <lwip/stats.h>
#include <lwip/priv/nd6_priv.h>
#include <lwip/ip6_frag.h>
#include <lwip/raw.h>
#include <lwip/prot/icmp6.h>
#include <lwip/mld6.h>

#include "lwip_glue.h"
#include <address.h>
#include <datagram.h>
#include <udp.h>
#include <nldef.h>
#include <tcpip_undoc.h>
#include <psdk/ipexport.h>

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

struct lib_ip6_query
{
    ULONG TableId;
    PUCHAR Buffer;
    ULONG Length;
    ULONG Count;
    ULONG Needed;
    PIP6_STATISTICS Statistics;
};

static
VOID
LibIP6Emit(struct lib_ip6_query *Query, const VOID *Row, ULONG Size)
{
    ULONG Offset = sizeof(IP6_TABLE_HEADER) + Query->Needed;

    if (Query->Buffer && Offset + Size <= Query->Length)
        RtlCopyMemory(Query->Buffer + Offset, Row, Size);

    Query->Needed += Size;
    Query->Count++;
}

static
BOOLEAN
LibIP6NetifIsLoopback(struct netif *netif)
{
    PIP_INTERFACE IF = netif->state;

    return IF->AddressLength == 0;
}

static
VOID
LibIP6EmitRoute(struct lib_ip6_query *Query, struct netif *netif, const ip6_addr_t *Prefix, ULONG PrefixLength,
                const ip6_addr_t *NextHop, ULONG Protocol, ULONG Origin, ULONG Lifetime, BOOLEAN Loopback)
{
    PIP_INTERFACE IF = netif->state;
    IP6_ROUTE_ROW Row;

    RtlZeroMemory(&Row, sizeof(Row));
    Row.Index = IF->Index;
    RtlCopyMemory(Row.Prefix, Prefix->addr, sizeof(Row.Prefix));
    Row.PrefixLength = PrefixLength;
    if (NextHop)
        RtlCopyMemory(Row.NextHop, NextHop->addr, sizeof(Row.NextHop));
    Row.Metric = 256;
    Row.Protocol = Protocol;
    Row.Origin = Origin;
    Row.ValidLifetime = Lifetime;
    Row.PreferredLifetime = Lifetime;
    Row.Loopback = Loopback;
    Row.Autoconfigure = TRUE;
    Row.Publish = FALSE;
    Row.Immortal = FALSE;

    if (Query->Statistics)
        Query->Statistics->NumRoutes++;
    else
        LibIP6Emit(Query, &Row, sizeof(Row));
}

static
VOID
LibIP6QueryRoutes(struct lib_ip6_query *Query, struct netif *netif)
{
    ip6_addr_t Prefix;
    s8_t i;

    if (LibIP6NetifIsLoopback(netif))
    {
        ip6_addr_set_loopback(&Prefix);
        LibIP6EmitRoute(Query, netif, &Prefix, 128, NULL, MIB_IPPROTO_LOCAL, NlroManual, 0xFFFFFFFF, TRUE);
    }
    else
    {
        IP6_ADDR(&Prefix, PP_HTONL(0xfe800000UL), 0, 0, 0);
        LibIP6EmitRoute(Query, netif, &Prefix, 64, NULL, MIB_IPPROTO_LOCAL, NlroManual, 0xFFFFFFFF, FALSE);

        for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
        {
            if (!ip6_addr_isvalid(netif_ip6_addr_state(netif, i)))
                continue;
            LibIP6EmitRoute(Query, netif, netif_ip6_addr(netif, i), 128, NULL, MIB_IPPROTO_LOCAL,
                            NlroManual, 0xFFFFFFFF, TRUE);
        }

        for (i = 0; i < LWIP_ND6_NUM_PREFIXES; i++)
        {
            if (prefix_list[i].netif != netif || !prefix_list[i].invalidation_timer)
                continue;
            LibIP6EmitRoute(Query, netif, &prefix_list[i].prefix, 64, NULL, MIB_IPPROTO_NETMGMT,
                            NlroRouterAdvertisement, prefix_list[i].invalidation_timer, FALSE);
        }

        for (i = 0; i < LWIP_ND6_NUM_ROUTERS; i++)
        {
            ip6_addr_t DefaultPrefix;

            if (!default_router_list[i].neighbor_entry ||
                default_router_list[i].neighbor_entry->netif != netif)
                continue;
            ip6_addr_set_zero(&DefaultPrefix);
            LibIP6EmitRoute(Query, netif, &DefaultPrefix, 0,
                            &default_router_list[i].neighbor_entry->next_hop_address,
                            MIB_IPPROTO_NETMGMT, NlroRouterAdvertisement,
                            default_router_list[i].invalidation_timer, FALSE);
        }
    }

    IP6_ADDR(&Prefix, PP_HTONL(0xff000000UL), 0, 0, 0);
    LibIP6EmitRoute(Query, netif, &Prefix, 8, NULL, MIB_IPPROTO_LOCAL, NlroWellKnown, 0xFFFFFFFF, FALSE);
}

#define LIB_IP6_MCAST_NEIGHBORS 32

typedef struct _LIB_IP6_MCAST_NEIGHBOR
{
    struct netif *Netif;
    ip6_addr_t Address;
} LIB_IP6_MCAST_NEIGHBOR;

static LIB_IP6_MCAST_NEIGHBOR LibIP6McastNeighbors[LIB_IP6_MCAST_NEIGHBORS];

void
LibIP6NoteMulticastDestination(struct netif *netif, const ip6_addr_t *Address)
{
    ULONG i, Free = LIB_IP6_MCAST_NEIGHBORS;
    SYS_ARCH_DECL_PROTECT(Level);

    SYS_ARCH_PROTECT(Level);
    for (i = 0; i < LIB_IP6_MCAST_NEIGHBORS; i++)
    {
        if (!LibIP6McastNeighbors[i].Netif)
        {
            if (Free == LIB_IP6_MCAST_NEIGHBORS)
                Free = i;
            continue;
        }
        if (LibIP6McastNeighbors[i].Netif == netif &&
            !memcmp(LibIP6McastNeighbors[i].Address.addr, Address->addr, sizeof(Address->addr)))
            break;
    }
    if (i == LIB_IP6_MCAST_NEIGHBORS && Free < LIB_IP6_MCAST_NEIGHBORS)
    {
        LibIP6McastNeighbors[Free].Netif = netif;
        ip6_addr_copy(LibIP6McastNeighbors[Free].Address, *Address);
    }
    SYS_ARCH_UNPROTECT(Level);
}

#define LIB_IP6_MEMBERSHIPS 64

typedef struct _LIB_IP6_MEMBERSHIP
{
    PVOID Owner;
    struct netif *Netif;
    ip6_addr_t Group;
} LIB_IP6_MEMBERSHIP;

static LIB_IP6_MEMBERSHIP LibIP6Memberships[LIB_IP6_MEMBERSHIPS];
static volatile LONG LibIP6MembershipCount;

struct lib_ip6_membership_msg
{
    PVOID Owner;
    ULONG Index;
    ip6_addr_t Group;
    BOOLEAN Join;
    BOOLEAN All;
    NTSTATUS Status;
};

static
VOID
LibIP6DropMembership(LIB_IP6_MEMBERSHIP *Membership)
{
    if (Membership->Netif->flags & NETIF_FLAG_MLD6)
        mld6_leavegroup_netif(Membership->Netif, &Membership->Group);
    Membership->Owner = NULL;
    Membership->Netif = NULL;
    InterlockedDecrement(&LibIP6MembershipCount);
}

static
void
LibIP6MembershipCallback(void *arg)
{
    struct lib_ip6_membership_msg *msg = arg;
    struct netif *netif, *target = NULL;
    ULONG i, Free = LIB_IP6_MEMBERSHIPS;

    if (msg->All)
    {
        for (i = 0; i < LIB_IP6_MEMBERSHIPS; i++)
        {
            if (LibIP6Memberships[i].Owner == msg->Owner)
                LibIP6DropMembership(&LibIP6Memberships[i]);
        }
        msg->Status = STATUS_SUCCESS;
        return;
    }

    NETIF_FOREACH(netif)
    {
        PIP_INTERFACE IF = netif->state;

        if (msg->Index ? (IF && IF->Index == msg->Index) : (netif->flags & NETIF_FLAG_MLD6) != 0)
        {
            target = netif;
            break;
        }
    }
    if (!target)
    {
        msg->Status = STATUS_INVALID_ADDRESS;
        return;
    }

    for (i = 0; i < LIB_IP6_MEMBERSHIPS; i++)
    {
        if (!LibIP6Memberships[i].Owner)
        {
            if (Free == LIB_IP6_MEMBERSHIPS)
                Free = i;
            continue;
        }
        if (LibIP6Memberships[i].Owner == msg->Owner && LibIP6Memberships[i].Netif == target &&
            ip6_addr_zoneless_eq(&LibIP6Memberships[i].Group, &msg->Group))
            break;
    }

    if (!msg->Join)
    {
        if (i == LIB_IP6_MEMBERSHIPS)
        {
            msg->Status = STATUS_INVALID_ADDRESS;
            return;
        }
        LibIP6DropMembership(&LibIP6Memberships[i]);
        msg->Status = STATUS_SUCCESS;
        return;
    }

    if (i < LIB_IP6_MEMBERSHIPS)
    {
        msg->Status = STATUS_ADDRESS_ALREADY_EXISTS;
        return;
    }
    if (Free == LIB_IP6_MEMBERSHIPS)
    {
        msg->Status = STATUS_INSUFFICIENT_RESOURCES;
        return;
    }
    if ((target->flags & NETIF_FLAG_MLD6) && mld6_joingroup_netif(target, &msg->Group) != ERR_OK)
    {
        msg->Status = STATUS_INSUFFICIENT_RESOURCES;
        return;
    }
    LibIP6Memberships[Free].Owner = msg->Owner;
    LibIP6Memberships[Free].Netif = target;
    ip6_addr_copy(LibIP6Memberships[Free].Group, msg->Group);
    InterlockedIncrement(&LibIP6MembershipCount);
    msg->Status = STATUS_SUCCESS;
}

NTSTATUS
LibIP6ChangeMembership(PVOID Owner, ULONG Index, const UCHAR *Group, BOOLEAN Join)
{
    struct lib_ip6_membership_msg msg;

    RtlZeroMemory(&msg, sizeof(msg));
    msg.Owner = Owner;
    msg.Index = Index;
    msg.Join = Join;
    RtlCopyMemory(msg.Group.addr, Group, sizeof(msg.Group.addr));
    ip6_addr_clear_zone(&msg.Group);
    if (!ip6_addr_ismulticast(&msg.Group))
        return STATUS_INVALID_PARAMETER;
    if (tcpip_callback_wait(LibIP6MembershipCallback, &msg) != ERR_OK)
        return STATUS_INSUFFICIENT_RESOURCES;
    return msg.Status;
}

VOID
LibIP6LeaveAllGroups(PVOID Owner)
{
    struct lib_ip6_membership_msg msg;

    if (!LibIP6MembershipCount)
        return;
    RtlZeroMemory(&msg, sizeof(msg));
    msg.Owner = Owner;
    msg.All = TRUE;
    (void)tcpip_callback_wait(LibIP6MembershipCallback, &msg);
}

void
LibIP6ForgetNetif(struct netif *netif)
{
    ULONG i;
    SYS_ARCH_DECL_PROTECT(Level);

    SYS_ARCH_PROTECT(Level);
    for (i = 0; i < LIB_IP6_MCAST_NEIGHBORS; i++)
    {
        if (LibIP6McastNeighbors[i].Netif == netif)
            LibIP6McastNeighbors[i].Netif = NULL;
    }
    for (i = 0; i < LIB_IP6_MEMBERSHIPS; i++)
    {
        if (LibIP6Memberships[i].Netif == netif)
        {
            LibIP6Memberships[i].Owner = NULL;
            LibIP6Memberships[i].Netif = NULL;
            InterlockedDecrement(&LibIP6MembershipCount);
        }
    }
    SYS_ARCH_UNPROTECT(Level);
}

static
VOID
LibIP6QueryCallback(void *arg)
{
    struct lib_ip6_query *Query = arg;
    struct netif *netif;
    s8_t i;

    NETIF_FOREACH(netif)
    {
        PIP_INTERFACE IF = netif->state;

        if (!IF)
            continue;

        switch (Query->TableId)
        {
        case Ip6TableInterfaces:
        {
            IP6_INTERFACE_ROW Row;

            RtlZeroMemory(&Row, sizeof(Row));
            Row.Index = IF->Index;
            Row.Mtu = netif_mtu6(netif);
            Row.HopLimit = IP_DEFAULT_TTL;
            Row.BaseReachableTime = LWIP_ND6_REACHABLE_TIME;
            Row.ReachableTime = reachable_time;
            Row.RetransmitTime = retrans_timer;
            Row.DadTransmits = LibIP6NetifIsLoopback(netif) ? 0 : LWIP_IPV6_DUP_DETECT_ATTEMPTS;
            Row.Connected = netif_is_link_up(netif) || LibIP6NetifIsLoopback(netif);
            Row.Loopback = LibIP6NetifIsLoopback(netif);
            Row.Autoconfiguration = netif->ip6_autoconfig_enabled ? TRUE : FALSE;
            Row.LinkSpeed = (ULONG64)IF->Speed * 100;
            LibIP6Emit(Query, &Row, sizeof(Row));
            break;
        }

        case Ip6TableAddresses:
        case Ip6TableStatistics:
            for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
            {
                u8_t State = netif_ip6_addr_state(netif, i);
                const ip6_addr_t *Address = netif_ip6_addr(netif, i);
                IP6_ADDRESS_ROW Row;
                u32_t ValidLife, PreferredLife;

                if (ip6_addr_isinvalid(State))
                    continue;

                if (Query->Statistics)
                {
                    Query->Statistics->NumAddresses++;
                    continue;
                }

                RtlZeroMemory(&Row, sizeof(Row));
                Row.Index = IF->Index;
                RtlCopyMemory(Row.Address, Address->addr, sizeof(Row.Address));
                Row.PrefixLength = ip6_addr_isloopback(Address) ? 128 : 64;

                if (ip6_addr_istentative(State))
                    Row.DadState = IpDadStateTentative;
                else if (ip6_addr_isduplicated(State))
                    Row.DadState = IpDadStateDuplicate;
                else if (ip6_addr_isdeprecated(State))
                    Row.DadState = IpDadStateDeprecated;
                else
                    Row.DadState = IpDadStatePreferred;

                ValidLife = netif_ip6_addr_valid_life(netif, i);
                PreferredLife = netif_ip6_addr_pref_life(netif, i);
                if (ip6_addr_life_isstatic(ValidLife))
                {
                    ValidLife = 0xFFFFFFFF;
                    PreferredLife = 0xFFFFFFFF;
                }
                else if (ip6_addr_life_isinfinite(PreferredLife))
                {
                    PreferredLife = 0xFFFFFFFF;
                }
                Row.ValidLifetime = ip6_addr_life_isinfinite(ValidLife) ? 0xFFFFFFFF : ValidLife;
                Row.PreferredLifetime = PreferredLife;

                if (ip6_addr_isloopback(Address))
                {
                    Row.PrefixOrigin = IpPrefixOriginWellKnown;
                    Row.SuffixOrigin = IpSuffixOriginWellKnown;
                }
                else if (ip6_addr_islinklocal(Address))
                {
                    Row.PrefixOrigin = IpPrefixOriginWellKnown;
                    Row.SuffixOrigin = IpSuffixOriginLinkLayerAddress;
                }
                else if (Row.ValidLifetime != 0xFFFFFFFF)
                {
                    Row.PrefixOrigin = IpPrefixOriginRouterAdvertisement;
                    Row.SuffixOrigin = IpSuffixOriginLinkLayerAddress;
                }
                else
                {
                    Row.PrefixOrigin = IpPrefixOriginManual;
                    Row.SuffixOrigin = IpSuffixOriginManual;
                }

                LibIP6Emit(Query, &Row, sizeof(Row));
            }

            if (Query->Statistics)
            {
                Query->Statistics->NumInterfaces++;
                LibIP6QueryRoutes(Query, netif);
            }
            break;

        case Ip6TableRoutes:
            LibIP6QueryRoutes(Query, netif);
            break;

        default:
            break;
        }
    }

    if (Query->TableId == Ip6TableNeighbors)
    {
        for (i = 0; i < LWIP_ND6_NUM_NEIGHBORS; i++)
        {
            struct nd6_neighbor_cache_entry *Entry = &neighbor_cache[i];
            PIP_INTERFACE IF;
            IP6_NEIGHBOR_ROW Row;

            if (Entry->state == ND6_NO_ENTRY || !Entry->netif || !(IF = Entry->netif->state))
                continue;

            RtlZeroMemory(&Row, sizeof(Row));
            Row.Index = IF->Index;
            RtlCopyMemory(Row.Address, Entry->next_hop_address.addr, sizeof(Row.Address));
            Row.PhysicalAddressLength = Entry->netif->hwaddr_len;
            RtlCopyMemory(Row.PhysicalAddress, Entry->lladdr, min(sizeof(Row.PhysicalAddress), sizeof(Entry->lladdr)));
            switch (Entry->state)
            {
            case ND6_INCOMPLETE: Row.State = NlnsIncomplete; break;
            case ND6_REACHABLE:  Row.State = NlnsReachable; break;
            case ND6_STALE:      Row.State = NlnsStale; break;
            case ND6_DELAY:      Row.State = NlnsDelay; break;
            case ND6_PROBE:      Row.State = NlnsProbe; break;
            default:             Row.State = NlnsUnreachable; break;
            }
            Row.IsRouter = Entry->isrouter ? TRUE : FALSE;
            Row.IsUnreachable = (Entry->state == ND6_INCOMPLETE);
            LibIP6Emit(Query, &Row, sizeof(Row));
        }

        for (i = 0; i < LIB_IP6_MCAST_NEIGHBORS; i++)
        {
            struct netif *Netif = LibIP6McastNeighbors[i].Netif;
            const u8_t *Group = (const u8_t *)&LibIP6McastNeighbors[i].Address.addr[3];
            PIP_INTERFACE IF;
            IP6_NEIGHBOR_ROW Row;

            if (!Netif || !(IF = Netif->state))
                continue;

            RtlZeroMemory(&Row, sizeof(Row));
            Row.Index = IF->Index;
            RtlCopyMemory(Row.Address, LibIP6McastNeighbors[i].Address.addr, sizeof(Row.Address));
            Row.PhysicalAddressLength = Netif->hwaddr_len;
            Row.PhysicalAddress[0] = 0x33;
            Row.PhysicalAddress[1] = 0x33;
            RtlCopyMemory(&Row.PhysicalAddress[2], Group, 4);
            Row.State = NlnsPermanent;
            LibIP6Emit(Query, &Row, sizeof(Row));
        }
    }

    if (Query->Statistics)
    {
        PIP6_STATISTICS Stats = Query->Statistics;

        Stats->DefaultHopLimit = IP_DEFAULT_TTL;
        Stats->InReceives = lwip_stats.ip6.recv;
        Stats->OutRequests = lwip_stats.ip6.xmit;
        Stats->ForwardedDatagrams = lwip_stats.ip6.fw;
        Stats->InDelivers = lwip_stats.ip6.recv >= lwip_stats.ip6.drop ? lwip_stats.ip6.recv - lwip_stats.ip6.drop : 0;
        Stats->InHeaderErrors = lwip_stats.ip6.chkerr + lwip_stats.ip6.lenerr + lwip_stats.ip6.opterr;
        Stats->InAddressErrors = 0;
        Stats->InUnknownProtocols = lwip_stats.ip6.proterr;
        Stats->InDiscards = lwip_stats.ip6.drop;
        Stats->OutNoRoutes = lwip_stats.ip6.rterr;
        Stats->OutDiscards = lwip_stats.ip6.memerr + lwip_stats.ip6.err;
        Stats->ReassemblyRequired = lwip_stats.ip6_frag.recv;
        Stats->ReassemblyOk = lwip_stats.ip6_frag.recv >= lwip_stats.ip6_frag.drop ? lwip_stats.ip6_frag.recv - lwip_stats.ip6_frag.drop : 0;
        Stats->ReassemblyFailures = lwip_stats.ip6_frag.drop;
        Stats->FragmentsCreated = lwip_stats.ip6_frag.xmit;
        Stats->ReassemblyTimeout = IPV6_REASS_MAXAGE * IP6_REASS_TMR_INTERVAL / 1000;
        Stats->IcmpInMessages = lwip_stats.icmp6.recv;
        Stats->IcmpInErrors = lwip_stats.icmp6.chkerr + lwip_stats.icmp6.lenerr + lwip_stats.icmp6.proterr;
        Stats->IcmpOutMessages = lwip_stats.icmp6.xmit;
        Stats->IcmpOutErrors = lwip_stats.icmp6.err + lwip_stats.icmp6.memerr + lwip_stats.icmp6.rterr;
        Stats->UdpInDatagrams = lwip_stats.udp.recv;
        Stats->UdpOutDatagrams = lwip_stats.udp.xmit;
        Stats->UdpNoPorts = lwip_stats.udp.proterr;
        Stats->UdpInErrors = lwip_stats.udp.chkerr + lwip_stats.udp.lenerr;
    }
}

NTSTATUS
LibIP6QueryTable(ULONG TableId, PVOID Buffer, ULONG Length, PULONG Count, PULONG Needed, PVOID Statistics)
{
    struct lib_ip6_query Query;

    RtlZeroMemory(&Query, sizeof(Query));
    Query.TableId = TableId;
    Query.Buffer = Buffer;
    Query.Length = Length;
    Query.Statistics = Statistics;

    if (tcpip_callback_wait(LibIP6QueryCallback, &Query) != ERR_OK)
        return STATUS_INSUFFICIENT_RESOURCES;

    *Count = Query.Count;
    *Needed = sizeof(IP6_TABLE_HEADER) + Query.Needed;
    return STATUS_SUCCESS;
}

typedef struct _ICMP6_ECHO_CONTEXT
{
    PIRP Irp;
    PIO_WORKITEM WorkItem;
    struct raw_pcb *Pcb;
    KTIMER Timer;
    KDPC Dpc;
    LONG RefCount;
    LONG Finished;
    u16_t Identifier;
    u16_t Sequence;
    LARGE_INTEGER Frequency;
    LONGLONG StartTicks;
    ip_addr_t Destination;
    ip_addr_t Source;
    BOOLEAN HasSource;
    BOOLEAN DefaultHopLimit;
    u8_t HopLimit;
    PUCHAR Data;
    u16_t DataSize;
    err_t SendError;
    PICMPV6_ECHO_REPLY Reply;
    ULONG OutputLength;
} ICMP6_ECHO_CONTEXT, *PICMP6_ECHO_CONTEXT;

static volatile LONG Icmp6Sequence;

static
VOID
LibICMP6FillAddress(PIPV6_ADDRESS_EX Address, const ip_addr_t *IpAddr)
{
    RtlZeroMemory(Address, sizeof(*Address));
    RtlCopyMemory(Address->sin6_addr, ip_2_ip6(IpAddr)->addr, sizeof(Address->sin6_addr));
}

static
VOID
NTAPI
LibICMP6FinishWorker(PDEVICE_OBJECT DeviceObject, PVOID Context);

static
VOID
LibICMP6Complete(PICMP6_ECHO_CONTEXT Context, NTSTATUS Status, ULONG ReplyStatus)
{
    RtlZeroMemory(Context->Reply, sizeof(*Context->Reply));
    Context->Reply->Status = ReplyStatus;
    Context->Irp->IoStatus.Status = Status;
    Context->Irp->IoStatus.Information = sizeof(*Context->Reply);
    IoQueueWorkItem(Context->WorkItem, LibICMP6FinishWorker, DelayedWorkQueue, Context);
}

static
u8_t
LibICMP6Recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    PICMP6_ECHO_CONTEXT Context = arg;
    struct icmp6_echo_hdr Header;
    u16_t Offset = ip_current_header_tot_len();
    PICMPV6_ECHO_REPLY Reply = Context->Reply;
    ULONG Copy;

    if (p->tot_len < Offset + sizeof(Header) ||
        pbuf_copy_partial(p, &Header, sizeof(Header), Offset) != sizeof(Header) ||
        Header.type != ICMP6_TYPE_EREP || Header.code != 0 ||
        Header.id != Context->Identifier || Header.seqno != Context->Sequence)
    {
        return 0;
    }

    if (InterlockedCompareExchange(&Context->Finished, 1, 0) != 0)
    {
        pbuf_free(p);
        return 1;
    }

    RtlZeroMemory(Reply, sizeof(*Reply));
    LibICMP6FillAddress(&Reply->Address, addr);
    Reply->Status = IP_SUCCESS;
    Reply->RoundTripTime = (ULONG)((KeQueryPerformanceCounter(NULL).QuadPart - Context->StartTicks) * 1000 /
                                   Context->Frequency.QuadPart);

    Copy = p->tot_len - Offset - sizeof(Header);
    Copy = min(Copy, Context->OutputLength - sizeof(*Reply));
    if (Copy)
        pbuf_copy_partial(p, Reply + 1, (u16_t)Copy, Offset + sizeof(Header));
    Context->Irp->IoStatus.Status = STATUS_SUCCESS;
    Context->Irp->IoStatus.Information = sizeof(*Reply) + Copy;

    pbuf_free(p);
    KeCancelTimer(&Context->Timer);
    IoQueueWorkItem(Context->WorkItem, LibICMP6FinishWorker, DelayedWorkQueue, Context);
    return 1;
}

static
void
LibICMP6StartCallback(void *arg)
{
    PICMP6_ECHO_CONTEXT Context = arg;
    struct icmp6_echo_hdr *Header;
    struct pbuf *p;

    Context->Pcb = raw_new_ip6(IP6_NEXTH_ICMP6);
    if (!Context->Pcb)
    {
        Context->SendError = ERR_MEM;
        return;
    }

    Context->Pcb->chksum_reqd = 1;
    Context->Pcb->chksum_offset = 2;
    if (!Context->DefaultHopLimit)
        Context->Pcb->ttl = Context->HopLimit;
    if (Context->HasSource)
        raw_bind(Context->Pcb, &Context->Source);
    raw_recv(Context->Pcb, LibICMP6Recv, Context);

    p = pbuf_alloc(PBUF_IP, sizeof(*Header) + Context->DataSize, PBUF_RAM);
    if (!p)
    {
        Context->SendError = ERR_MEM;
        return;
    }

    Header = p->payload;
    Header->type = ICMP6_TYPE_EREQ;
    Header->code = 0;
    Header->chksum = 0;
    Header->id = Context->Identifier;
    Header->seqno = Context->Sequence;
    if (Context->DataSize)
        RtlCopyMemory(Header + 1, Context->Data, Context->DataSize);

    Context->StartTicks = KeQueryPerformanceCounter(&Context->Frequency).QuadPart;
    Context->SendError = raw_sendto(Context->Pcb, p, &Context->Destination);
    pbuf_free(p);
}

static
void
LibICMP6StopCallback(void *arg)
{
    PICMP6_ECHO_CONTEXT Context = arg;

    if (Context->Pcb)
    {
        raw_remove(Context->Pcb);
        Context->Pcb = NULL;
    }
}

static
VOID
LibICMP6FreeContext(PICMP6_ECHO_CONTEXT Context)
{
    (void)tcpip_callback_wait(LibICMP6StopCallback, Context);
    KeCancelTimer(&Context->Timer);
    KeFlushQueuedDpcs();
    if (Context->WorkItem)
        IoFreeWorkItem(Context->WorkItem);
    if (Context->Data)
        ExFreePoolWithTag(Context->Data, LWIP_TAG);
    ExFreePoolWithTag(Context, LWIP_TAG);
}

static
VOID
LibICMP6Release(PICMP6_ECHO_CONTEXT Context)
{
    if (InterlockedDecrement(&Context->RefCount) == 0)
        LibICMP6FreeContext(Context);
}

static
VOID
NTAPI
LibICMP6FinishWorker(PDEVICE_OBJECT DeviceObject, PVOID ContextArg)
{
    PICMP6_ECHO_CONTEXT Context = ContextArg;
    PIRP Irp = Context->Irp;
    KIRQL OldIrql;

    IoAcquireCancelSpinLock(&OldIrql);
    IoSetCancelRoutine(Irp, NULL);
    IoReleaseCancelSpinLock(OldIrql);
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);

    LibICMP6Release(Context);
}

static
VOID
NTAPI
LibICMP6TimeoutDpc(PKDPC Dpc, PVOID ContextArg, PVOID SystemArgument1, PVOID SystemArgument2)
{
    PICMP6_ECHO_CONTEXT Context = ContextArg;

    if (InterlockedCompareExchange(&Context->Finished, 2, 0) == 0)
        LibICMP6Complete(Context, STATUS_TIMEOUT, IP_REQ_TIMED_OUT);
}

static
BOOLEAN
LibICMP6ResolveScope(ip6_addr_t *Address, ULONG ScopeId)
{
    struct netif *netif;

    ip6_addr_clear_zone(Address);
    if (!ip6_addr_has_scope(Address, IP6_UNKNOWN) || !ScopeId)
        return TRUE;

    NETIF_FOREACH(netif)
    {
        PIP_INTERFACE IF = netif->state;

        if (IF && IF->Index == ScopeId)
        {
            ip6_addr_assign_zone(Address, IP6_UNKNOWN, netif);
            return TRUE;
        }
    }

    return FALSE;
}

NTSTATUS
LibICMP6EchoRequest(PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
    PICMPV6_ECHO_REQUEST Request = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    PICMP6_ECHO_CONTEXT Context;
    PICMPV6_ECHO_REPLY Reply;
    IP_ADDRESS SourceAddress, DestinationAddress;
    LARGE_INTEGER Timeout;
    ULONG DataSize;

    if (InputLength < sizeof(*Request) || OutputLength < sizeof(ICMPV6_ECHO_REPLY) || !Irp->MdlAddress)
        return STATUS_INVALID_PARAMETER;

    DataSize = InputLength - sizeof(*Request);
    if (DataSize > 0xFFFF - IP6_HLEN - sizeof(struct icmp6_echo_hdr))
        return STATUS_INVALID_PARAMETER;

    Reply = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (!Reply)
        return STATUS_INSUFFICIENT_RESOURCES;

    SourceAddress.Type = IP_ADDRESS_V6;
    RtlCopyMemory(SourceAddress.Address.IPv6Address, Request->SourceAddress.sin6_addr,
                  sizeof(SourceAddress.Address.IPv6Address));
    DestinationAddress.Type = IP_ADDRESS_V6;
    RtlCopyMemory(DestinationAddress.Address.IPv6Address, Request->DestinationAddress.sin6_addr,
                  sizeof(DestinationAddress.Address.IPv6Address));

    if (AddrIsUnspecified(&DestinationAddress))
        return STATUS_INVALID_ADDRESS_COMPONENT;

    if (!AddrIsUnspecified(&SourceAddress) && !LibIPIsLocalAddress(&SourceAddress))
        return STATUS_INVALID_ADDRESS_COMPONENT;

    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), LWIP_TAG);
    if (!Context)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Context, sizeof(*Context));

    Context->Irp = Irp;
    Context->Reply = Reply;
    Context->OutputLength = OutputLength;
    Context->WorkItem = IoAllocateWorkItem(DeviceObject);
    if (DataSize)
        Context->Data = ExAllocatePoolWithTag(NonPagedPool, DataSize, LWIP_TAG);
    if (!Context->WorkItem || (DataSize && !Context->Data))
    {
        LibICMP6FreeContext(Context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (DataSize)
        RtlCopyMemory(Context->Data, Request + 1, DataSize);
    Context->DataSize = (u16_t)DataSize;
    Context->Identifier = lwip_htons((u16_t)((ULONG_PTR)PsGetCurrentProcessId() & 0xFFFF));
    Context->Sequence = lwip_htons((u16_t)InterlockedIncrement(&Icmp6Sequence));
    Context->DefaultHopLimit = (Request->Ttl > 0xFF);
    Context->HopLimit = (u8_t)Request->Ttl;

    IP_SET_TYPE(&Context->Destination, IPADDR_TYPE_V6);
    RtlCopyMemory(ip_2_ip6(&Context->Destination)->addr, Request->DestinationAddress.sin6_addr,
                  sizeof(ip_2_ip6(&Context->Destination)->addr));

    IP_SET_TYPE(&Context->Source, IPADDR_TYPE_V6);
    RtlCopyMemory(ip_2_ip6(&Context->Source)->addr, Request->SourceAddress.sin6_addr,
                  sizeof(ip_2_ip6(&Context->Source)->addr));
    ip6_addr_clear_zone(ip_2_ip6(&Context->Source));
    Context->HasSource = !ip6_addr_isany(ip_2_ip6(&Context->Source));

    KeInitializeTimer(&Context->Timer);
    KeInitializeDpc(&Context->Dpc, LibICMP6TimeoutDpc, Context);
    Context->RefCount = 2;

    IoMarkIrpPending(Irp);

    if (!Context->DefaultHopLimit && !Context->HopLimit)
    {
        Context->Finished = 3;
        LibICMP6Complete(Context, STATUS_INVALID_PARAMETER, IP_GENERAL_FAILURE);
        LibICMP6Release(Context);
        return STATUS_PENDING;
    }

    if (!LibICMP6ResolveScope(ip_2_ip6(&Context->Destination), Request->DestinationAddress.sin6_scope_id))
    {
        Context->Finished = 3;
        LibICMP6Complete(Context, STATUS_SUCCESS, IP_DEST_NET_UNREACHABLE);
        LibICMP6Release(Context);
        return STATUS_PENDING;
    }

    Timeout.QuadPart = -10000LL * (Request->Timeout ? Request->Timeout : 1);
    KeSetTimer(&Context->Timer, Timeout, &Context->Dpc);

    if (tcpip_callback_wait(LibICMP6StartCallback, Context) != ERR_OK || Context->SendError != ERR_OK)
    {
        if (InterlockedCompareExchange(&Context->Finished, 3, 0) == 0)
        {
            KeCancelTimer(&Context->Timer);
            LibICMP6Complete(Context, STATUS_SUCCESS,
                             Context->SendError == ERR_RTE ? IP_DEST_NET_UNREACHABLE : IP_GENERAL_FAILURE);
        }
    }

    LibICMP6Release(Context);
    return STATUS_PENDING;
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
