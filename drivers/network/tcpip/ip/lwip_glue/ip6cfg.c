/*
 * PROJECT:     LiberNT TCP/IP protocol driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     IPv6 configuration: manual addresses and routes, router advertisement options
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include <lwip/raw.h>
#include <lwip/ip.h>
#include <lwip/ip6.h>
#include <lwip/nd6.h>
#include <lwip/mld6.h>
#include <lwip/inet_chksum.h>
#include <lwip/prot/icmp6.h>
#include <lwip/prot/nd6.h>
#include <lwip/priv/nd6_priv.h>

#include "lwip_glue.h"
#include <lwiphooks.h>
#include <bcrypt.h>
#include <nldef.h>
#include <tcpip_undoc.h>
#include <ntstrsafe.h>

#define LIB_IP6_ROUTES 64
#define LIB_IP6_ADDRESS_INFOS 32
#define LIB_IP6_RA_NETIFS 16
#define LIB_IP6_RDNSS 4
#define LIB_IP6_DNSSL 4
#define LIB_IP6_INFINITE MAXULONGLONG
#define ND6_OPTION_TYPE_DNSSL 31
#define LIB_IP6_TEMP_VALID_LIFETIME (7 * 24 * 3600)
#define LIB_IP6_TEMP_PREFERRED_LIFETIME (24 * 3600)
#define LIB_IP6_TEMP_MAX_DESYNC (LIB_IP6_TEMP_PREFERRED_LIFETIME / 10 * 4)
#define LIB_IP6_TEMP_REGEN_ADVANCE 5
#define LIB_IP6_TIMER_INTERVAL 1000
#define LIB_IP6_PMTU_TIMEOUT 600000
#define LIB_IP6_MIN_RETRANSMIT 500
#define LIB_IP6_RA_MIN_INTERVAL 200
#define LIB_IP6_RA_MAX_INTERVAL 600
#define LIB_IP6_RA_INITIAL_INTERVAL 16
#define LIB_IP6_RA_INITIAL_COUNT 3
#define LIB_IP6_RA_MIN_DELAY 3
#define LIB_IP6_RA_ROUTER_LIFETIME 1800
#define LIB_IP6_PATHS 64
#define LIB_IP6_PATH_IDLE 90000

typedef struct _LIB_IP6_ROUTE
{
    struct netif *Netif;
    ip6_addr_t Prefix;
    ip6_addr_t NextHop;
    ULONG PrefixLength;
    ULONG Metric;
    ULONG Protocol;
    ULONG Origin;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    ULONGLONG Expiry;
    BOOLEAN Publish;
    BOOLEAN Implicit;
} LIB_IP6_ROUTE, *PLIB_IP6_ROUTE;

typedef struct _LIB_IP6_ADDRESS_INFO
{
    struct netif *Netif;
    ip6_addr_t Address;
    ULONG PrefixLength;
    ULONG PrefixOrigin;
    ULONG SuffixOrigin;
    BOOLEAN SkipAsSource;
    BOOLEAN Temporary;
    BOOLEAN Regenerated;
    ULONG Desync;
    ULONGLONG Created;
} LIB_IP6_ADDRESS_INFO, *PLIB_IP6_ADDRESS_INFO;

typedef struct _LIB_IP6_RA_INFO
{
    struct netif *Netif;
    BOOLEAN Seen;
    BOOLEAN Managed;
    BOOLEAN Other;
    BOOLEAN RouterDiscoveryDisabled;
    BOOLEAN ManualMetric;
    BOOLEAN ManualDad;
    BOOLEAN ManualSitePrefix;
    BOOLEAN Forwarding;
    BOOLEAN WeakHostSend;
    BOOLEAN WeakHostReceive;
    BOOLEAN Advertising;
    BOOLEAN AdvertiseDefaultRoute;
    BOOLEAN DisableDefaultRoutes;
    BOOLEAN AdvManaged;
    BOOLEAN AdvOther;
    BOOLEAN ManualRouterDiscovery;
    ULONG Metric;
    ULONG DadTransmits;
    ULONG BaseReachableTime;
    ULONG ReachableTime;
    ULONG SitePrefixLength;
    ULONG RetransmitTime;
    ULONG PathMtuDiscoveryTimeout;
    ULONG LinkLocalAddressTimeout;
    ULONG RouterDiscoveryBehavior;
    ULONG ZoneIndices[16];
    ULONGLONG NextAdvertisement;
    ULONGLONG LastAdvertisement;
    ULONG InitialAdvertisements;
    struct
    {
        ip6_addr_t Address;
        ULONGLONG Expiry;
    } Rdnss[LIB_IP6_RDNSS];
    struct
    {
        WCHAR Name[256];
        ULONGLONG Expiry;
    } Dnssl[LIB_IP6_DNSSL];
} LIB_IP6_RA_INFO, *PLIB_IP6_RA_INFO;

typedef struct _LIB_IP6_PATH
{
    struct netif *Netif;
    ip6_addr_t Destination;
    ip6_addr_t Source;
    ULONGLONG LastUsed;
    ULONGLONG Confirmed;
    BOOLEAN Local;
} LIB_IP6_PATH, *PLIB_IP6_PATH;

static LIB_IP6_ROUTE LibIP6Routes[LIB_IP6_ROUTES];
static LIB_IP6_ADDRESS_INFO LibIP6AddressInfo[LIB_IP6_ADDRESS_INFOS];
static LIB_IP6_RA_INFO LibIP6RaInfo[LIB_IP6_RA_NETIFS];
static struct raw_pcb *LibIP6RaPcb;
static LIB_IP6_PATH LibIP6Paths[LIB_IP6_PATHS];
static struct raw_pcb *LibIP6RaSendPcb;
static BCRYPT_ALG_HANDLE LibIP6HmacAlgorithm;
static UCHAR LibIP6Secret[32];

static
ULONGLONG
LibIP6Now(VOID)
{
    return KeQueryInterruptTime() / 10000000ULL;
}

static
ULONGLONG
LibIP6Expiry(ULONG Lifetime)
{
    return Lifetime == 0xFFFFFFFF ? LIB_IP6_INFINITE : LibIP6Now() + Lifetime;
}

static
ULONG
LibIP6Remaining(ULONGLONG Expiry)
{
    ULONGLONG Now = LibIP6Now();

    if (Expiry == LIB_IP6_INFINITE)
        return 0xFFFFFFFF;
    return Expiry > Now ? (ULONG)min(Expiry - Now, 0xFFFFFFFEULL) : 0;
}

static
BOOLEAN
LibIP6PrefixMatch(const ip6_addr_t *Address, const ip6_addr_t *Prefix, ULONG PrefixLength)
{
    const UCHAR *A = (const UCHAR *)Address->addr;
    const UCHAR *P = (const UCHAR *)Prefix->addr;
    ULONG Bytes = PrefixLength / 8, Bits = PrefixLength % 8;

    if (Bytes && RtlCompareMemory(A, P, Bytes) != Bytes)
        return FALSE;
    if (Bits && ((A[Bytes] ^ P[Bytes]) & (UCHAR)(0xFF << (8 - Bits))))
        return FALSE;
    return TRUE;
}

static
VOID
LibIP6MaskPrefix(ip6_addr_t *Prefix, ULONG PrefixLength)
{
    UCHAR *P = (UCHAR *)Prefix->addr;
    ULONG i;

    for (i = 0; i < 16; i++)
    {
        if (PrefixLength >= 8)
            PrefixLength -= 8;
        else
        {
            P[i] &= (UCHAR)(0xFF << (8 - PrefixLength));
            PrefixLength = 0;
        }
    }
}

static PLIB_IP6_RA_INFO LibIP6FindRaInfo(struct netif *Netif, BOOLEAN Create);
static VOID LibIP6SendAdvertisement(struct netif *Netif, PLIB_IP6_RA_INFO Info, BOOLEAN Final);
static struct netif *LibIP6SourceNetif(const ip6_addr_t *Source);
static BOOLEAN LibIP6AddressExists(const ip6_addr_t *Address);

static
struct netif *
LibIP6FindNetif(ULONG Index)
{
    struct netif *netif;

    NETIF_FOREACH(netif)
    {
        PIP_INTERFACE IF = netif->state;

        if (IF && IF->Index == Index)
            return netif;
    }
    return NULL;
}

static
BOOLEAN
LibIP6RouteUsable(PLIB_IP6_ROUTE Route)
{
    if (!Route->Netif)
        return FALSE;
    if (Route->Expiry != LIB_IP6_INFINITE && Route->Expiry <= LibIP6Now())
    {
        RtlZeroMemory(Route, sizeof(*Route));
        return FALSE;
    }
    return netif_is_up(Route->Netif) && netif_is_link_up(Route->Netif);
}

static
ULONG
LibIP6InterfaceMetric(struct netif *Netif)
{
    PLIB_IP6_RA_INFO Info = LibIP6FindRaInfo(Netif, FALSE);
    PIP_INTERFACE IF = Netif->state;

    if (Info && Info->ManualMetric)
        return Info->Metric;
    if (LibIP6NetifIsLoopback(Netif) || !IF)
        return 75;
    return IPAutomaticMetric(IF);
}

static
ULONG
LibIP6RouteCost(PLIB_IP6_ROUTE Route)
{
    return Route->Metric + LibIP6InterfaceMetric(Route->Netif);
}

static
PLIB_IP6_ROUTE
LibIP6LookupRoute(const ip6_addr_t *Destination, struct netif *Netif)
{
    PLIB_IP6_ROUTE Best = NULL;
    ULONG i;

    for (i = 0; i < LIB_IP6_ROUTES; i++)
    {
        PLIB_IP6_ROUTE Route = &LibIP6Routes[i];

        if (!LibIP6RouteUsable(Route) || (Netif && Route->Netif != Netif))
            continue;
        if (!LibIP6PrefixMatch(Destination, &Route->Prefix, Route->PrefixLength))
            continue;
        if (!Route->PrefixLength && !LibIP6RouterAllowed(Route->Netif))
            continue;
        if (!Best || Route->PrefixLength > Best->PrefixLength ||
            (Route->PrefixLength == Best->PrefixLength && LibIP6RouteCost(Route) < LibIP6RouteCost(Best)))
            Best = Route;
    }
    return Best;
}

static
struct netif *
LibIP6BestRouterNetif(struct netif *Netif, PULONG Cost)
{
    struct netif *Best = NULL;
    BOOLEAN BestReachable = FALSE;
    s8_t i;

    *Cost = MAXULONG;
    for (i = 0; i < LWIP_ND6_NUM_ROUTERS; i++)
    {
        struct nd6_neighbor_cache_entry *Entry = default_router_list[i].neighbor_entry;
        struct netif *RouterNetif;
        BOOLEAN Reachable;
        ULONG RouterCost;

        if (!Entry || !(RouterNetif = Entry->netif) || (Netif && RouterNetif != Netif) || !LibIP6RouterAllowed(RouterNetif))
            continue;
        if (!netif_is_up(RouterNetif) || !netif_is_link_up(RouterNetif))
            continue;
        Reachable = Entry->state != ND6_INCOMPLETE;
        RouterCost = 256 + LibIP6InterfaceMetric(RouterNetif);
        if (!Best || (Reachable && !BestReachable) || (Reachable == BestReachable && RouterCost < *Cost))
        {
            Best = RouterNetif;
            BestReachable = Reachable;
            *Cost = RouterCost;
        }
    }
    return Best;
}

static
BOOLEAN
LibIP6DestinationOnLink(const ip6_addr_t *Destination)
{
    struct netif *netif;
    s8_t i;

    NETIF_FOREACH(netif)
    {
        if (!netif->state || !netif_is_up(netif) || !netif_is_link_up(netif))
            continue;
        for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
        {
            if (ip6_addr_isvalid(netif_ip6_addr_state(netif, i)) &&
                ip6_addr_net_eq(Destination, netif_ip6_addr(netif, i)) &&
                (netif_ip6_addr_isstatic(netif, i) || ip6_addr_nethost_eq(Destination, netif_ip6_addr(netif, i))))
                return TRUE;
        }
    }
    for (i = 0; i < LWIP_ND6_NUM_PREFIXES; i++)
    {
        if (prefix_list[i].netif && prefix_list[i].invalidation_timer &&
            ip6_addr_net_eq(Destination, &prefix_list[i].prefix))
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
LibIP6RouteYields(PLIB_IP6_ROUTE Route, const ip6_addr_t *Destination, struct netif *Netif)
{
    ULONG RouterCost;

    if (Route->PrefixLength <= 64 && LibIP6DestinationOnLink(Destination))
        return TRUE;
    if (Route->PrefixLength)
        return FALSE;
    return LibIP6BestRouterNetif(Netif, &RouterCost) && RouterCost <= LibIP6RouteCost(Route);
}

u32_t
LibIP6ReachableTime(struct netif *Netif, u32_t Default)
{
    PLIB_IP6_RA_INFO Info = Netif ? LibIP6FindRaInfo(Netif, FALSE) : NULL;

    return (Info && Info->BaseReachableTime) ? Info->ReachableTime : Default;
}

u16_t
LibIP6DadTransmits(struct netif *Netif, u16_t Default)
{
    PLIB_IP6_RA_INFO Info = Netif ? LibIP6FindRaInfo(Netif, FALSE) : NULL;

    return (Info && Info->ManualDad) ? (u16_t)min(Info->DadTransmits, 0xFFFF) : Default;
}

u32_t
LibIP6RetransTime(struct netif *Netif, u32_t Default)
{
    PLIB_IP6_RA_INFO Info = Netif ? LibIP6FindRaInfo(Netif, FALSE) : NULL;

    return (Info && Info->RetransmitTime) ? Info->RetransmitTime : Default;
}

u8_t
LibIP6RouterAllowed(struct netif *Netif)
{
    PLIB_IP6_RA_INFO Info = Netif ? LibIP6FindRaInfo(Netif, FALSE) : NULL;

    return !Info || !Info->DisableDefaultRoutes;
}

u32_t
LibIP6PmtuTimeout(const ip6_addr_t *NextHop)
{
    ULONG i;

    for (i = 0; i < LWIP_ND6_NUM_NEIGHBORS; i++)
    {
        PLIB_IP6_RA_INFO Info;

        if (neighbor_cache[i].state == ND6_NO_ENTRY || !neighbor_cache[i].netif ||
            !ip6_addr_zoneless_eq(&neighbor_cache[i].next_hop_address, NextHop))
            continue;
        Info = LibIP6FindRaInfo(neighbor_cache[i].netif, FALSE);
        if (Info && Info->PathMtuDiscoveryTimeout)
            return Info->PathMtuDiscoveryTimeout;
        break;
    }
    return LIB_IP6_PMTU_TIMEOUT;
}

u8_t
LibIP6ForwardAllowed(struct netif *Netif)
{
    PLIB_IP6_RA_INFO Info = Netif ? LibIP6FindRaInfo(Netif, FALSE) : NULL;

    return Info && Info->Forwarding;
}

u8_t
LibIP6WeakSend(const ip6_addr_t *Source)
{
    struct netif *Owner = LibIP6SourceNetif(Source);
    PLIB_IP6_RA_INFO Info = Owner ? LibIP6FindRaInfo(Owner, FALSE) : NULL;

    return Info && Info->WeakHostSend;
}

u8_t
LibIP6WeakReceive(struct netif *Input, struct netif *Owner)
{
    PLIB_IP6_RA_INFO Info = Input ? LibIP6FindRaInfo(Input, FALSE) : NULL;

    LWIP_UNUSED_ARG(Owner);
    return Info && Info->WeakHostReceive;
}

struct netif *
LibIP6RouteHook(const ip6_addr_t *Source, const ip6_addr_t *Destination)
{
    PLIB_IP6_ROUTE Route;
    ULONG RouterCost;

    if (Source && !ip6_addr_isany(Source) && !LibIP6AddressExists(Destination))
    {
        struct netif *Owner = LibIP6SourceNetif(Source);
        PLIB_IP6_RA_INFO Info = Owner ? LibIP6FindRaInfo(Owner, FALSE) : NULL;

        if (Owner && (!Info || !Info->WeakHostSend))
            return Owner;
    }

    Route = LibIP6LookupRoute(Destination, NULL);
    if (Route && !LibIP6RouteYields(Route, Destination, NULL))
        return Route->Netif;
    if (LibIP6DestinationOnLink(Destination))
        return NULL;
    return LibIP6BestRouterNetif(NULL, &RouterCost);
}

static
BOOLEAN
LibIP6NetifReaches(struct netif *Netif, const ip6_addr_t *Destination)
{
    ULONG RouterCost;
    s8_t i;

    if (ip6_addr_islinklocal(Destination))
        return TRUE;
    for (i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++)
    {
        if (ip6_addr_isvalid(netif_ip6_addr_state(Netif, i)) &&
            ip6_addr_net_eq(Destination, netif_ip6_addr(Netif, i)))
            return TRUE;
    }
    for (i = 0; i < LWIP_ND6_NUM_PREFIXES; i++)
    {
        if (prefix_list[i].netif == Netif && prefix_list[i].invalidation_timer &&
            ip6_addr_net_eq(Destination, &prefix_list[i].prefix))
            return TRUE;
    }
    return LibIP6LookupRoute(Destination, Netif) != NULL || LibIP6BestRouterNetif(Netif, &RouterCost) != NULL;
}

static
BOOLEAN
LibIP6IsHeldMulticast(const ip6_addr_t *Address)
{
    struct netif *netif;

    if (!ip6_addr_ismulticast(Address))
        return FALSE;
    NETIF_FOREACH(netif)
    {
        if (!netif->state || LibIP6NetifIsLoopback(netif) || !netif_is_up(netif))
            continue;
        if (ip6_addr_isallnodes_linklocal(Address) || mld6_lookfor_group(netif, Address))
            return TRUE;
    }
    return FALSE;
}

NTSTATUS
LibIP6RoutePktInfo(const ip6_addr_t *Source, ULONG Index, const ip6_addr_t *Destination, struct netif **Netif)
{
    BOOLEAN Unspecified = ip6_addr_isany(Source);
    struct netif *Selected, *Route, *Owner;
    BOOLEAN Loopback;

    LWIP_ASSERT_CORE_LOCKED();
    *Netif = NULL;

    if (Index)
    {
        Selected = LibIP6FindNetif(Index);
        if (!Selected)
            return STATUS_INVALID_PARAMETER;
        if (!Unspecified && netif_get_ip6_addr_match(Selected, Source) < 0)
            return STATUS_INVALID_ADDRESS;
    }
    else if (!Unspecified)
    {
        Selected = LibIP6SourceNetif(Source);
        if (!Selected)
            return LibIP6IsHeldMulticast(Source) ? STATUS_INVALID_ADDRESS : STATUS_INVALID_PARAMETER;
    }
    else
    {
        return STATUS_SUCCESS;
    }

    ASSERT(Selected->state != NULL);
    if (ip6_addr_has_zone(Destination) && !ip6_addr_test_zone(Destination, Selected))
        return STATUS_INVALID_ADDRESS;

    if (!ip6_addr_ismulticast(Destination))
    {
        Route = NULL;
        if (!ip6_addr_islinklocal(Destination))
        {
            Route = ip6_route(IP6_ADDR_ANY6, Destination);
            if (!Route)
                return STATUS_INVALID_ADDRESS;
        }

        Loopback = LibIP6NetifIsLoopback(Selected);
        if (ip6_addr_isloopback(Destination) != Loopback)
            return Loopback ? STATUS_NETWORK_UNREACHABLE : STATUS_INVALID_ADDRESS;

        if (!Loopback && Route && Route != Selected)
        {
            Owner = LibIP6SourceNetif(Destination);
            if (Owner != Selected && !LibIP6NetifReaches(Selected, Destination))
                return STATUS_NETWORK_UNREACHABLE;
        }
    }

    *Netif = Selected;
    return STATUS_SUCCESS;
}

const ip6_addr_t *
LibIP6GatewayHook(struct netif *Netif, const ip6_addr_t *Destination)
{
    PLIB_IP6_ROUTE Route = LibIP6LookupRoute(Destination, Netif);

    if (!Route || LibIP6RouteYields(Route, Destination, Netif))
        return NULL;
    if (ip6_addr_isany(&Route->NextHop))
        return Destination;
    return &Route->NextHop;
}

static
PLIB_IP6_ROUTE
LibIP6FindRouteEntry(struct netif *Netif, const ip6_addr_t *Prefix, ULONG PrefixLength, const ip6_addr_t *NextHop)
{
    ULONG i;

    for (i = 0; i < LIB_IP6_ROUTES; i++)
    {
        PLIB_IP6_ROUTE Route = &LibIP6Routes[i];

        if (Route->Netif == Netif && Route->PrefixLength == PrefixLength &&
            ip6_addr_zoneless_eq(&Route->Prefix, Prefix) && ip6_addr_zoneless_eq(&Route->NextHop, NextHop))
            return Route;
    }
    return NULL;
}

static
PLIB_IP6_ROUTE
LibIP6AllocateRoute(VOID)
{
    ULONG i;

    for (i = 0; i < LIB_IP6_ROUTES; i++)
    {
        PLIB_IP6_ROUTE Route = &LibIP6Routes[i];

        if (Route->Netif && Route->Expiry != LIB_IP6_INFINITE && Route->Expiry <= LibIP6Now())
            RtlZeroMemory(Route, sizeof(*Route));
        if (!Route->Netif)
            return Route;
    }
    return NULL;
}

static
PLIB_IP6_ROUTE
LibIP6StoreRoute(struct netif *Netif, const ip6_addr_t *Prefix, ULONG PrefixLength, const ip6_addr_t *NextHop,
                 ULONG Metric, ULONG Protocol, ULONG Origin, ULONG ValidLifetime, ULONG PreferredLifetime)
{
    PLIB_IP6_ROUTE Route = LibIP6FindRouteEntry(Netif, Prefix, PrefixLength, NextHop);

    if (!Route)
        Route = LibIP6AllocateRoute();
    if (!Route)
        return NULL;

    Route->Netif = Netif;
    ip6_addr_copy(Route->Prefix, *Prefix);
    ip6_addr_clear_zone(&Route->Prefix);
    ip6_addr_copy(Route->NextHop, *NextHop);
    ip6_addr_clear_zone(&Route->NextHop);
    if (ip6_addr_has_scope(&Route->NextHop, IP6_UNICAST))
        ip6_addr_assign_zone(&Route->NextHop, IP6_UNICAST, Netif);
    Route->PrefixLength = PrefixLength;
    Route->Metric = Metric;
    Route->Protocol = Protocol;
    Route->Origin = Origin;
    Route->ValidLifetime = ValidLifetime;
    Route->PreferredLifetime = PreferredLifetime;
    Route->Expiry = LibIP6Expiry(ValidLifetime);
    nd6_clear_destination_cache();
    return Route;
}

static
PLIB_IP6_ADDRESS_INFO
LibIP6FindAddressInfo(struct netif *Netif, const ip6_addr_t *Address)
{
    ULONG i;

    for (i = 0; i < LIB_IP6_ADDRESS_INFOS; i++)
    {
        PLIB_IP6_ADDRESS_INFO Info = &LibIP6AddressInfo[i];

        if (Info->Netif == Netif && ip6_addr_zoneless_eq(&Info->Address, Address))
            return Info;
    }
    return NULL;
}

static
VOID
LibIP6DropImplicitRoute(struct netif *Netif, PLIB_IP6_ADDRESS_INFO Info)
{
    ip6_addr_t Prefix, Any;
    PLIB_IP6_ROUTE Route;

    if (Info->PrefixLength >= 128)
        return;
    ip6_addr_copy(Prefix, Info->Address);
    ip6_addr_clear_zone(&Prefix);
    LibIP6MaskPrefix(&Prefix, Info->PrefixLength);
    ip6_addr_set_zero(&Any);
    Route = LibIP6FindRouteEntry(Netif, &Prefix, Info->PrefixLength, &Any);
    if (Route && Route->Implicit)
    {
        RtlZeroMemory(Route, sizeof(*Route));
        nd6_clear_destination_cache();
    }
}

static
VOID
LibIP6AddImplicitRoute(struct netif *Netif, PLIB_IP6_ADDRESS_INFO Info)
{
    ip6_addr_t Prefix, Any;
    PLIB_IP6_ROUTE Route;

    if (Info->PrefixLength >= 128 || ip6_addr_islinklocal(&Info->Address))
        return;
    ip6_addr_copy(Prefix, Info->Address);
    ip6_addr_clear_zone(&Prefix);
    LibIP6MaskPrefix(&Prefix, Info->PrefixLength);
    ip6_addr_set_zero(&Any);
    if (LibIP6FindRouteEntry(Netif, &Prefix, Info->PrefixLength, &Any))
        return;
    Route = LibIP6StoreRoute(Netif, &Prefix, Info->PrefixLength, &Any, 256, MIB_IPPROTO_LOCAL, NlroManual,
                             0xFFFFFFFF, 0xFFFFFFFF);
    if (Route)
        Route->Implicit = TRUE;
}

static
VOID
LibIP6ForgetAddressInfo(struct netif *Netif, PLIB_IP6_ADDRESS_INFO Info)
{
    LibIP6DropImplicitRoute(Netif, Info);
    RtlZeroMemory(Info, sizeof(*Info));
}

VOID
LibIP6DescribeAddress(struct netif *Netif, const ip6_addr_t *Address, PULONG PrefixLength,
                      PULONG PrefixOrigin, PULONG SuffixOrigin, PBOOLEAN SkipAsSource)
{
    PLIB_IP6_ADDRESS_INFO Info = LibIP6FindAddressInfo(Netif, Address);

    if (!Info)
        return;
    *PrefixLength = Info->PrefixLength;
    *PrefixOrigin = Info->PrefixOrigin;
    *SuffixOrigin = Info->SuffixOrigin;
    *SkipAsSource = Info->SkipAsSource;
}

static
BOOLEAN
LibIP6PrefixAdvertised(struct netif *Netif, const ip6_addr_t *Prefix, ULONG PrefixLength)
{
    s8_t i;

    if (PrefixLength != 64)
        return FALSE;
    for (i = 0; i < LWIP_ND6_NUM_PREFIXES; i++)
    {
        if (prefix_list[i].netif == Netif && prefix_list[i].invalidation_timer &&
            ip6_addr_net_eq(Prefix, &prefix_list[i].prefix))
            return TRUE;
    }
    return FALSE;
}

VOID
LibIP6QueryConfigRoutes(struct lib_ip6_query *Query, struct netif *Netif)
{
    ULONG i;

    for (i = 0; i < LIB_IP6_ROUTES; i++)
    {
        PLIB_IP6_ROUTE Route = &LibIP6Routes[i];
        IP6_ROUTE_ROW Row;
        PIP_INTERFACE IF;

        if (Route->Netif != Netif || !(IF = Netif->state))
            continue;
        if (Route->Expiry != LIB_IP6_INFINITE && Route->Expiry <= LibIP6Now())
        {
            RtlZeroMemory(Route, sizeof(*Route));
            continue;
        }
        if (Route->Implicit && LibIP6PrefixAdvertised(Netif, &Route->Prefix, Route->PrefixLength))
            continue;
        if (Query->Statistics)
        {
            Query->Statistics->NumRoutes++;
            continue;
        }

        RtlZeroMemory(&Row, sizeof(Row));
        Row.Index = IF->Index;
        RtlCopyMemory(Row.Prefix, Route->Prefix.addr, sizeof(Row.Prefix));
        Row.PrefixLength = Route->PrefixLength;
        RtlCopyMemory(Row.NextHop, Route->NextHop.addr, sizeof(Row.NextHop));
        Row.Metric = Route->Metric;
        Row.Protocol = Route->Protocol;
        Row.Origin = Route->Origin;
        Row.ValidLifetime = LibIP6Remaining(Route->Expiry);
        Row.PreferredLifetime = Route->PreferredLifetime == 0xFFFFFFFF ? 0xFFFFFFFF :
                                min(Route->PreferredLifetime, Row.ValidLifetime);
        Row.Loopback = FALSE;
        Row.Autoconfigure = TRUE;
        Row.Publish = Route->Publish;
        Row.Immortal = FALSE;
        LibIP6Emit(Query, &Row, sizeof(Row));
    }
}

static
PLIB_IP6_RA_INFO
LibIP6FindRaInfo(struct netif *Netif, BOOLEAN Create)
{
    ULONG i;
    PLIB_IP6_RA_INFO Free = NULL;

    for (i = 0; i < LIB_IP6_RA_NETIFS; i++)
    {
        if (LibIP6RaInfo[i].Netif == Netif)
            return &LibIP6RaInfo[i];
        if (!LibIP6RaInfo[i].Netif && !Free)
            Free = &LibIP6RaInfo[i];
    }
    if (!Create || !Free)
        return NULL;
    RtlZeroMemory(Free, sizeof(*Free));
    Free->Netif = Netif;
    return Free;
}

VOID
LibIP6DescribeInterface(struct netif *Netif, PIP6_INTERFACE_ROW Row)
{
    PLIB_IP6_RA_INFO Info = LibIP6FindRaInfo(Netif, FALSE);

    Row->Metric = LibIP6InterfaceMetric(Netif);
    Row->UseAutomaticMetric = !Info || !Info->ManualMetric;
    Row->RouterDiscovery = !LibIP6NetifIsLoopback(Netif) && (!Info || !Info->RouterDiscoveryDisabled);
    Row->SitePrefixLength = (Info && Info->ManualSitePrefix) ? Info->SitePrefixLength : 64;
    Row->DadTransmits = LibIP6DadTransmits(Netif, (u16_t)Row->DadTransmits);
    Row->RetransmitTime = LibIP6RetransTime(Netif, Row->RetransmitTime);
    Row->RouterDiscoveryBehavior = (Info && Info->ManualRouterDiscovery) ? Info->RouterDiscoveryBehavior : Row->RouterDiscovery;
    Row->PathMtuDiscoveryTimeout = (Info && Info->PathMtuDiscoveryTimeout) ? Info->PathMtuDiscoveryTimeout : LIB_IP6_PMTU_TIMEOUT;
    if (Info && Info->BaseReachableTime)
    {
        Row->BaseReachableTime = Info->BaseReachableTime;
        Row->ReachableTime = Info->ReachableTime;
    }
    if (!Info)
        return;
    Row->RouterAdvertised = Info->Seen;
    Row->ManagedAddressConfiguration = Info->Advertising ? Info->AdvManaged : Info->Managed;
    Row->OtherStatefulConfiguration = Info->Advertising ? Info->AdvOther : Info->Other;
    Row->Forwarding = Info->Forwarding;
    Row->WeakHostSend = Info->WeakHostSend;
    Row->WeakHostReceive = Info->WeakHostReceive;
    Row->Advertising = Info->Advertising;
    Row->AdvertiseDefaultRoute = Info->AdvertiseDefaultRoute;
    Row->DisableDefaultRoutes = Info->DisableDefaultRoutes;
    Row->LinkLocalAddressTimeout = Info->LinkLocalAddressTimeout;
    RtlCopyMemory(Row->ZoneIndices, Info->ZoneIndices, sizeof(Row->ZoneIndices));
}

VOID
LibIP6QueryDns(struct lib_ip6_query *Query)
{
    ULONG i, j;

    for (i = 0; i < LIB_IP6_RA_NETIFS; i++)
    {
        PLIB_IP6_RA_INFO Info = &LibIP6RaInfo[i];
        PIP_INTERFACE IF;

        if (!Info->Netif || !(IF = Info->Netif->state))
            continue;

        if (Query->TableId == Ip6TableDnsServers)
        {
            for (j = 0; j < LIB_IP6_RDNSS; j++)
            {
                IP6_DNS_SERVER_ROW Row;
                ULONG Lifetime = LibIP6Remaining(Info->Rdnss[j].Expiry);

                if (!Lifetime)
                    continue;
                RtlZeroMemory(&Row, sizeof(Row));
                Row.Index = IF->Index;
                RtlCopyMemory(Row.Address, Info->Rdnss[j].Address.addr, sizeof(Row.Address));
                Row.Lifetime = Lifetime;
                LibIP6Emit(Query, &Row, sizeof(Row));
            }
        }
        else
        {
            for (j = 0; j < LIB_IP6_DNSSL; j++)
            {
                IP6_DNS_SUFFIX_ROW Row;
                ULONG Lifetime = LibIP6Remaining(Info->Dnssl[j].Expiry);

                if (!Lifetime || !Info->Dnssl[j].Name[0])
                    continue;
                RtlZeroMemory(&Row, sizeof(Row));
                Row.Index = IF->Index;
                Row.Lifetime = Lifetime;
                RtlCopyMemory(Row.Suffix, Info->Dnssl[j].Name, sizeof(Row.Suffix));
                LibIP6Emit(Query, &Row, sizeof(Row));
            }
        }
    }
}

static
VOID
LibIP6NoteRdnss(PLIB_IP6_RA_INFO Info, const ip6_addr_t *Address, ULONG Lifetime)
{
    ULONG i, Slot = LIB_IP6_RDNSS;

    for (i = 0; i < LIB_IP6_RDNSS; i++)
    {
        if (LibIP6Remaining(Info->Rdnss[i].Expiry) && ip6_addr_zoneless_eq(&Info->Rdnss[i].Address, Address))
        {
            Slot = i;
            break;
        }
        if (Slot == LIB_IP6_RDNSS && !LibIP6Remaining(Info->Rdnss[i].Expiry))
            Slot = i;
    }
    if (Slot == LIB_IP6_RDNSS)
        return;
    ip6_addr_copy(Info->Rdnss[Slot].Address, *Address);
    Info->Rdnss[Slot].Expiry = Lifetime ? LibIP6Expiry(Lifetime) : 0;
}

static
VOID
LibIP6NoteDnssl(PLIB_IP6_RA_INFO Info, const WCHAR *Name, ULONG Lifetime)
{
    ULONG i, Slot = LIB_IP6_DNSSL;

    for (i = 0; i < LIB_IP6_DNSSL; i++)
    {
        if (LibIP6Remaining(Info->Dnssl[i].Expiry) && !_wcsicmp(Info->Dnssl[i].Name, Name))
        {
            Slot = i;
            break;
        }
        if (Slot == LIB_IP6_DNSSL && !LibIP6Remaining(Info->Dnssl[i].Expiry))
            Slot = i;
    }
    if (Slot == LIB_IP6_DNSSL)
        return;
    RtlStringCbCopyW(Info->Dnssl[Slot].Name, sizeof(Info->Dnssl[Slot].Name), Name);
    Info->Dnssl[Slot].Expiry = Lifetime ? LibIP6Expiry(Lifetime) : 0;
}

static
VOID
LibIP6ParseDnssl(PLIB_IP6_RA_INFO Info, const UCHAR *Data, ULONG Length, ULONG Lifetime)
{
    WCHAR Name[256];
    ULONG Offset = 0, Out = 0;

    while (Offset < Length)
    {
        UCHAR Label = Data[Offset++];

        if (!Label)
        {
            if (!Out)
                break;
            Name[Out] = UNICODE_NULL;
            LibIP6NoteDnssl(Info, Name, Lifetime);
            Out = 0;
            continue;
        }
        if (Label > 63 || Offset + Label > Length || Out + Label + 2 > RTL_NUMBER_OF(Name))
            break;
        if (Out)
            Name[Out++] = L'.';
        while (Label--)
            Name[Out++] = (WCHAR)Data[Offset++];
    }
}

static
VOID
LibIP6LoadSecret(VOID)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\Tcpip6\\Parameters");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"StableInterfaceIdentifierKey");
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(LibIP6Secret)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES Attributes;
    BOOLEAN Loaded = FALSE;
    HANDLE Key = NULL;
    ULONG Length;

    if (LibIP6HmacAlgorithm)
        return;
    InitializeObjectAttributes(&Attributes, &KeyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (NT_SUCCESS(ZwCreateKey(&Key, KEY_QUERY_VALUE | KEY_SET_VALUE, &Attributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL)))
    {
        if (NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Value, sizeof(Buffer), &Length)) &&
            Value->Type == REG_BINARY && Value->DataLength == sizeof(LibIP6Secret))
        {
            RtlCopyMemory(LibIP6Secret, Value->Data, sizeof(LibIP6Secret));
            Loaded = TRUE;
        }
    }
    if (!Loaded)
    {
        if (!NT_SUCCESS(BCryptGenRandom(NULL, LibIP6Secret, sizeof(LibIP6Secret), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        {
            if (Key)
                ZwClose(Key);
            return;
        }
        if (Key)
            ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, LibIP6Secret, sizeof(LibIP6Secret));
    }
    if (Key)
        ZwClose(Key);
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&LibIP6HmacAlgorithm, BCRYPT_SHA256_ALGORITHM, NULL,
                                                BCRYPT_ALG_HANDLE_HMAC_FLAG)))
        LibIP6HmacAlgorithm = NULL;
}

static
BOOLEAN
LibIP6InterfaceIdReserved(const UCHAR Iid[8])
{
    static const UCHAR Zero[8];
    static const UCHAR Proxy[6] = {0x02, 0x00, 0x5E, 0xFF, 0xFE, 0x00};
    static const UCHAR Anycast[7] = {0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    if (RtlCompareMemory(Iid, Zero, sizeof(Zero)) == sizeof(Zero))
        return TRUE;
    if (RtlCompareMemory(Iid, Proxy, sizeof(Proxy)) == sizeof(Proxy) && ((Iid[6] << 8) | Iid[7]) <= 0x5213)
        return TRUE;
    return RtlCompareMemory(Iid, Anycast, sizeof(Anycast)) == sizeof(Anycast) && Iid[7] >= 0x80;
}

static
BOOLEAN
LibIP6StableInterfaceId(struct netif *Netif, const ip6_addr_t *Prefix, ip6_addr_t *Address)
{
    UCHAR Digest[32], Counter;

    LibIP6LoadSecret();
    if (!LibIP6HmacAlgorithm)
        return FALSE;
    for (Counter = 0; Counter < 4; Counter++)
    {
        BCRYPT_HASH_HANDLE Hash;
        NTSTATUS Status;

        if (!NT_SUCCESS(BCryptCreateHash(LibIP6HmacAlgorithm, &Hash, NULL, 0, LibIP6Secret, sizeof(LibIP6Secret), 0)))
            return FALSE;
        Status = BCryptHashData(Hash, (PUCHAR)Prefix->addr, 8, 0);
        if (NT_SUCCESS(Status))
            Status = BCryptHashData(Hash, Netif->hwaddr, Netif->hwaddr_len, 0);
        if (NT_SUCCESS(Status))
            Status = BCryptHashData(Hash, &Counter, sizeof(Counter), 0);
        if (NT_SUCCESS(Status))
            Status = BCryptFinishHash(Hash, Digest, sizeof(Digest), 0);
        BCryptDestroyHash(Hash);
        if (!NT_SUCCESS(Status))
            return FALSE;
        if (LibIP6InterfaceIdReserved(Digest))
            continue;
        RtlCopyMemory(&Address->addr[2], Digest, 8);
        return TRUE;
    }
    return FALSE;
}

VOID
LibIP6CreateLinkLocal(struct netif *Netif)
{
    ip6_addr_t Prefix, Address;

    IP6_ADDR(&Prefix, PP_HTONL(0xFE800000UL), 0, 0, 0);
    ip6_addr_copy(Address, Prefix);
    if (!LibIP6StableInterfaceId(Netif, &Prefix, &Address))
    {
        netif_create_ip6_linklocal_address(Netif, 1);
        return;
    }
    ip6_addr_assign_zone(&Address, IP6_UNICAST, Netif);
    ip_addr_copy_from_ip6(Netif->ip6_addr[0], Address);
    netif_ip6_addr_set_state(Netif, 0, IP6_ADDR_TENTATIVE);
}

void
LibIP6StableAddress(struct netif *Netif, const ip6_addr_t *Prefix, ip6_addr_t *Address)
{
    LibIP6StableInterfaceId(Netif, Prefix, Address);
}

u8_t
LibIP6AddressIsManaged(struct netif *Netif, s8_t Slot)
{
    return LibIP6FindAddressInfo(Netif, netif_ip6_addr(Netif, Slot)) != NULL;
}

s8_t
LibIP6SourceRank(struct netif *Netif, s8_t Slot)
{
    PLIB_IP6_ADDRESS_INFO Info = LibIP6FindAddressInfo(Netif, netif_ip6_addr(Netif, Slot));

    if (!Info)
        return 0;
    if (Info->SkipAsSource)
        return -1;
    return Info->Temporary ? 1 : 0;
}

static
ULONG
LibIP6Random(VOID)
{
    ULONG Value = 0;

    BCryptGenRandom(NULL, (PUCHAR)&Value, sizeof(Value), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return Value;
}

static
PLIB_IP6_ADDRESS_INFO
LibIP6OldestTemporary(struct netif *Netif)
{
    PLIB_IP6_ADDRESS_INFO Oldest = NULL;
    ULONG i;

    for (i = 0; i < LIB_IP6_ADDRESS_INFOS; i++)
    {
        PLIB_IP6_ADDRESS_INFO Info = &LibIP6AddressInfo[i];
        s8_t Slot;

        if (Info->Netif != Netif || !Info->Temporary)
            continue;
        Slot = netif_get_ip6_addr_match(Netif, &Info->Address);
        if (Slot >= 0 && !ip6_addr_isdeprecated(netif_ip6_addr_state(Netif, Slot)))
            continue;
        if (!Oldest || Info->Created < Oldest->Created)
            Oldest = Info;
    }
    return Oldest;
}

static
VOID
LibIP6DropAddress(struct netif *Netif, PLIB_IP6_ADDRESS_INFO Info)
{
    s8_t Slot = netif_get_ip6_addr_match(Netif, &Info->Address);

    if (Slot >= 0)
        netif_ip6_addr_set_state(Netif, Slot, IP6_ADDR_INVALID);
    LibIP6ForgetAddressInfo(Netif, Info);
}

static
VOID
LibIP6NewTemporary(struct netif *Netif, const ip6_addr_t *Prefix, ULONG ValidLifetime, ULONG PreferredLifetime)
{
    PLIB_IP6_ADDRESS_INFO Info = NULL;
    ip6_addr_t Address;
    ULONG Desync, i, Attempt;
    s8_t Slot;

    Desync = LibIP6Random() % (LIB_IP6_TEMP_MAX_DESYNC + 1);
    ValidLifetime = min(ValidLifetime, LIB_IP6_TEMP_VALID_LIFETIME);
    PreferredLifetime = min(PreferredLifetime, LIB_IP6_TEMP_PREFERRED_LIFETIME - Desync);
    if (PreferredLifetime <= LIB_IP6_TEMP_REGEN_ADVANCE || !ValidLifetime)
        return;

    for (Attempt = 0; Attempt < 3; Attempt++)
    {
        ip6_addr_copy(Address, *Prefix);
        BCryptGenRandom(NULL, (PUCHAR)&Address.addr[2], 8, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!LibIP6InterfaceIdReserved((const UCHAR *)&Address.addr[2]) && netif_get_ip6_addr_match(Netif, &Address) < 0)
            break;
    }
    if (Attempt == 3)
        return;

    for (i = 0; i < LIB_IP6_ADDRESS_INFOS && LibIP6AddressInfo[i].Netif; i++);
    if (i == LIB_IP6_ADDRESS_INFOS || netif_add_ip6_address(Netif, &Address, &Slot) != ERR_OK)
    {
        PLIB_IP6_ADDRESS_INFO Oldest = LibIP6OldestTemporary(Netif);

        if (!Oldest)
            return;
        LibIP6DropAddress(Netif, Oldest);
        for (i = 0; i < LIB_IP6_ADDRESS_INFOS && LibIP6AddressInfo[i].Netif; i++);
        if (i == LIB_IP6_ADDRESS_INFOS || netif_add_ip6_address(Netif, &Address, &Slot) != ERR_OK)
            return;
    }
    netif_ip6_addr_set_valid_life(Netif, Slot, ValidLifetime == 0xFFFFFFFF ? IP6_ADDR_LIFE_INFINITE : ValidLifetime);
    netif_ip6_addr_set_pref_life(Netif, Slot, PreferredLifetime);

    Info = &LibIP6AddressInfo[i];
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Netif = Netif;
    ip6_addr_copy(Info->Address, Address);
    Info->PrefixLength = 64;
    Info->PrefixOrigin = IpPrefixOriginRouterAdvertisement;
    Info->SuffixOrigin = IpSuffixOriginRandom;
    Info->Temporary = TRUE;
    Info->Desync = Desync;
    Info->Created = LibIP6Now();
}

static
VOID
LibIP6TemporaryPrefix(struct netif *Netif, const ip6_addr_t *Prefix, ULONG ValidLifetime, ULONG PreferredLifetime)
{
    BOOLEAN Current = FALSE;
    ULONGLONG Now = LibIP6Now();
    ULONG i;

    if (!Netif->ip6_autoconfig_enabled)
        return;

    for (i = 0; i < LIB_IP6_ADDRESS_INFOS; i++)
    {
        PLIB_IP6_ADDRESS_INFO Info = &LibIP6AddressInfo[i];
        ULONG Age, Valid, Preferred;
        s8_t Slot;

        if (Info->Netif != Netif || !Info->Temporary || !ip6_addr_net_eq(&Info->Address, Prefix))
            continue;
        Slot = netif_get_ip6_addr_match(Netif, &Info->Address);
        if (Slot < 0)
            continue;
        Age = (ULONG)min(Now - Info->Created, 0xFFFFFFFFULL);
        Valid = Age >= LIB_IP6_TEMP_VALID_LIFETIME ? 0 : min(ValidLifetime, LIB_IP6_TEMP_VALID_LIFETIME - Age);
        Preferred = Age + Info->Desync >= LIB_IP6_TEMP_PREFERRED_LIFETIME ? 0 :
                    min(PreferredLifetime, LIB_IP6_TEMP_PREFERRED_LIFETIME - Info->Desync - Age);
        if (!Valid)
        {
            LibIP6DropAddress(Netif, Info);
            continue;
        }
        netif_ip6_addr_set_valid_life(Netif, Slot, Valid);
        netif_ip6_addr_set_pref_life(Netif, Slot, Preferred);
        if (!Preferred && ip6_addr_ispreferred(netif_ip6_addr_state(Netif, Slot)))
            netif_ip6_addr_set_state(Netif, Slot, IP6_ADDR_DEPRECATED);
        if (Preferred > LIB_IP6_TEMP_REGEN_ADVANCE && !Info->Regenerated)
            Current = TRUE;
    }
    if (!Current && PreferredLifetime > LIB_IP6_TEMP_REGEN_ADVANCE)
        LibIP6NewTemporary(Netif, Prefix, ValidLifetime, PreferredLifetime);
}

static
BOOLEAN
LibIP6PublicLifetimes(struct netif *Netif, const ip6_addr_t *Prefix, PULONG ValidLifetime, PULONG PreferredLifetime)
{
    s8_t i;

    for (i = 1; i < LWIP_IPV6_NUM_ADDRESSES; i++)
    {
        const ip6_addr_t *Address = netif_ip6_addr(Netif, i);

        if (!ip6_addr_isvalid(netif_ip6_addr_state(Netif, i)) || netif_ip6_addr_isstatic(Netif, i) ||
            !ip6_addr_net_eq(Address, Prefix) || LibIP6FindAddressInfo(Netif, Address))
            continue;
        *ValidLifetime = netif_ip6_addr_valid_life(Netif, i);
        *PreferredLifetime = ip6_addr_isdeprecated(netif_ip6_addr_state(Netif, i)) ? 0 : netif_ip6_addr_pref_life(Netif, i);
        return TRUE;
    }
    return FALSE;
}

static
ULONG
LibIP6PutOption(PUCHAR Packet, ULONG Length, ULONG Capacity, const VOID *Option, ULONG OptionLength)
{
    if (Length + OptionLength > Capacity)
        return Length;
    RtlCopyMemory(Packet + Length, Option, OptionLength);
    return Length + OptionLength;
}

static
VOID
LibIP6SendAdvertisement(struct netif *Netif, PLIB_IP6_RA_INFO Info, BOOLEAN Final)
{
    UCHAR Packet[1024], Option[32];
    struct ra_header *Ra = (struct ra_header *)Packet;
    ULONG Length = sizeof(*Ra), OptionLength, i;
    ip_addr_t AllNodes, Source;
    struct pbuf *p;

    if (!LibIP6RaSendPcb || !netif_is_up(Netif) || !netif_is_link_up(Netif) ||
        !ip6_addr_isvalid(netif_ip6_addr_state(Netif, 0)))
        return;

    RtlZeroMemory(Packet, sizeof(Packet));
    Ra->type = ICMP6_TYPE_RA;
    Ra->flags = (Info->AdvManaged ? ND6_RA_FLAG_MANAGED_ADDR_CONFIG : 0) | (Info->AdvOther ? ND6_RA_FLAG_OTHER_CONFIG : 0);
    Ra->router_lifetime = lwip_htons((Final || !Info->AdvertiseDefaultRoute) ? 0 : LIB_IP6_RA_ROUTER_LIFETIME);

    OptionLength = (2 + Netif->hwaddr_len + 7) & ~7;
    RtlZeroMemory(Option, sizeof(Option));
    Option[0] = ND6_OPTION_TYPE_SOURCE_LLADDR;
    Option[1] = (UCHAR)(OptionLength / 8);
    RtlCopyMemory(&Option[2], Netif->hwaddr, Netif->hwaddr_len);
    Length = LibIP6PutOption(Packet, Length, sizeof(Packet), Option, OptionLength);

    for (i = 0; i < LIB_IP6_ROUTES; i++)
    {
        PLIB_IP6_ROUTE Route = &LibIP6Routes[i];
        ULONG Valid, Preferred;

        if (Route->Netif != Netif || !Route->Publish || !LibIP6RouteUsable(Route))
            continue;
        Valid = LibIP6Remaining(Route->Expiry);
        Preferred = Route->PreferredLifetime == 0xFFFFFFFF ? 0xFFFFFFFF : min(Route->PreferredLifetime, Valid);
        RtlZeroMemory(Option, sizeof(Option));
        if (ip6_addr_isany(&Route->NextHop) && Route->PrefixLength)
        {
            struct prefix_option *Prefix = (struct prefix_option *)Option;

            Prefix->type = ND6_OPTION_TYPE_PREFIX_INFO;
            Prefix->length = 4;
            Prefix->prefix_length = (u8_t)Route->PrefixLength;
            Prefix->flags = ND6_PREFIX_FLAG_ON_LINK | (Route->PrefixLength == 64 ? ND6_PREFIX_FLAG_AUTONOMOUS : 0);
            Prefix->valid_lifetime = lwip_htonl(Valid);
            Prefix->preferred_lifetime = lwip_htonl(Preferred);
            RtlCopyMemory(&Prefix->prefix, Route->Prefix.addr, 16);
            Length = LibIP6PutOption(Packet, Length, sizeof(Packet), Option, sizeof(*Prefix));
        }
        else if (Route->PrefixLength)
        {
            Option[0] = ND6_OPTION_TYPE_ROUTE_INFO;
            Option[1] = 3;
            Option[2] = (UCHAR)Route->PrefixLength;
            *(u32_t *)&Option[4] = lwip_htonl(Valid);
            RtlCopyMemory(&Option[8], Route->Prefix.addr, 16);
            Length = LibIP6PutOption(Packet, Length, sizeof(Packet), Option, 24);
        }
    }

    p = pbuf_alloc(PBUF_IP, (u16_t)Length, PBUF_RAM);
    if (!p)
        return;
    pbuf_take(p, Packet, (u16_t)Length);
    ip_addr_copy_from_ip6(Source, *netif_ip6_addr(Netif, 0));
    ip6_addr_set_allnodes_linklocal(ip_2_ip6(&AllNodes));
    IP_SET_TYPE_VAL(AllNodes, IPADDR_TYPE_V6);
    ip6_addr_assign_zone(ip_2_ip6(&AllNodes), IP6_MULTICAST, Netif);
    raw_sendto_if_src(LibIP6RaSendPcb, p, &AllNodes, Netif, &Source);
    pbuf_free(p);
    Info->LastAdvertisement = LibIP6Now();
}

static
VOID
LibIP6ScheduleAdvertisements(VOID)
{
    ULONGLONG Now = LibIP6Now();
    ULONG i;

    for (i = 0; i < LIB_IP6_RA_NETIFS; i++)
    {
        PLIB_IP6_RA_INFO Info = &LibIP6RaInfo[i];
        ULONG Interval;

        if (!Info->Netif || !Info->Advertising || Now < Info->NextAdvertisement)
            continue;
        LibIP6SendAdvertisement(Info->Netif, Info, FALSE);
        Interval = LIB_IP6_RA_MIN_INTERVAL + LibIP6Random() % (LIB_IP6_RA_MAX_INTERVAL - LIB_IP6_RA_MIN_INTERVAL + 1);
        if (Info->InitialAdvertisements < LIB_IP6_RA_INITIAL_COUNT)
        {
            Info->InitialAdvertisements++;
            Interval = min(Interval, LIB_IP6_RA_INITIAL_INTERVAL);
        }
        Info->NextAdvertisement = Now + Interval;
    }
}

static
VOID
LibIP6RouterSolicited(struct netif *Netif)
{
    PLIB_IP6_RA_INFO Info = LibIP6FindRaInfo(Netif, FALSE);
    ULONGLONG Now = LibIP6Now();

    if (!Info || !Info->Advertising)
        return;
    if (Now >= Info->LastAdvertisement + LIB_IP6_RA_MIN_DELAY)
        LibIP6SendAdvertisement(Netif, Info, FALSE);
    else
        Info->NextAdvertisement = min(Info->NextAdvertisement, Info->LastAdvertisement + LIB_IP6_RA_MIN_DELAY);
}

static
ULONGLONG
LibIP6UptimeMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

void
LibIP6PathOutput(struct netif *Netif, const ip6_addr_t *Source, const ip6_addr_t *Destination, u8_t NextHeader,
                 struct pbuf *p)
{
    PLIB_IP6_PATH Path = NULL, Victim = NULL;
    const ip_addr_t *Selected;
    ULONG i;

    if (!Netif || !Destination || !p)
        return;
    if (NextHeader == IP6_NEXTH_ICMP6 && p->len >= 1 &&
        *(u8_t *)p->payload >= ICMP6_TYPE_RS && *(u8_t *)p->payload <= ICMP6_TYPE_RD)
        return;

    for (i = 0; i < LIB_IP6_PATHS; i++)
    {
        PLIB_IP6_PATH Current = &LibIP6Paths[i];

        if (Current->Netif == Netif && ip6_addr_zoneless_eq(&Current->Destination, Destination))
        {
            Path = Current;
            break;
        }
        if (!Victim || (Victim->Netif && (!Current->Netif || Current->LastUsed < Victim->LastUsed)))
            Victim = Current;
    }
    if (!Path)
    {
        Path = Victim;
        RtlZeroMemory(Path, sizeof(*Path));
        Path->Netif = Netif;
        ip6_addr_copy(Path->Destination, *Destination);
        ip6_addr_clear_zone(&Path->Destination);
        Path->Local = LibIP6AddressExists(&Path->Destination) || ip6_addr_isallnodes_linklocal(&Path->Destination) ||
                      (ip6_addr_ismulticast(&Path->Destination) && mld6_lookfor_group(Netif, Destination));
    }
    if (Source && !ip6_addr_isany(Source))
        ip6_addr_copy(Path->Source, *Source);
    else if ((Selected = ip6_select_source_address(Netif, Destination)))
        ip6_addr_copy(Path->Source, *ip_2_ip6(Selected));
    ip6_addr_clear_zone(&Path->Source);
    Path->LastUsed = LibIP6UptimeMs();
}

VOID
LibIP6ConfirmPath(const ip6_addr_t *Destination)
{
    ULONGLONG Now = LibIP6UptimeMs();
    ULONG i;

    for (i = 0; i < LIB_IP6_PATHS; i++)
    {
        if (LibIP6Paths[i].Netif && ip6_addr_zoneless_eq(&LibIP6Paths[i].Destination, Destination))
            LibIP6Paths[i].Confirmed = Now;
    }
}

static
VOID
LibIP6ExpirePaths(VOID)
{
    ULONGLONG Now = LibIP6UptimeMs();
    ULONG i;

    for (i = 0; i < LIB_IP6_PATHS; i++)
    {
        if (LibIP6Paths[i].Netif && Now - LibIP6Paths[i].LastUsed >= LIB_IP6_PATH_IDLE)
            RtlZeroMemory(&LibIP6Paths[i], sizeof(LibIP6Paths[i]));
    }
}

struct lib_ip6_path_msg
{
    PIP_PATH_TABLE Table;
    ULONG Capacity;
    ULONG Count;
};

static
void
LibIP6QueryPathsCallback(void *arg)
{
    struct lib_ip6_path_msg *Msg = arg;
    ULONGLONG Now = LibIP6UptimeMs();
    ULONG i, j;

    for (i = 0; i < LIB_IP6_PATHS; i++)
    {
        PLIB_IP6_PATH Path = &LibIP6Paths[i];
        PIP_INTERFACE IF;
        PIP_PATH_ROW Row;
        ULONGLONG Elapsed;

        if (!Path->Netif || !(IF = Path->Netif->state))
            continue;
        if (Msg->Count < Msg->Capacity)
        {
            Row = &Msg->Table->Rows[Msg->Count];
            RtlZeroMemory(Row, sizeof(*Row));
            Row->Index = IF->Index;
            RtlCopyMemory(Row->Source, Path->Source.addr, sizeof(Row->Source));
            RtlCopyMemory(Row->Destination, Path->Destination.addr, sizeof(Row->Destination));
            RtlCopyMemory(Row->NextHop, Path->Destination.addr, sizeof(Row->NextHop));
            Row->PathMtu = netif_mtu6(Path->Netif);
            for (j = 0; j < LWIP_ND6_NUM_DESTINATIONS; j++)
            {
                if (ip6_addr_zoneless_eq(&destination_cache[j].destination_addr, &Path->Destination))
                {
                    RtlCopyMemory(Row->NextHop, destination_cache[j].next_hop_addr.addr, sizeof(Row->NextHop));
                    if (destination_cache[j].pmtu)
                        Row->PathMtu = destination_cache[j].pmtu;
                    break;
                }
            }
            Elapsed = Now - Path->Confirmed;
            Row->LastReachable = (ULONG)min(Elapsed - Elapsed % 500, MAXULONG);
            Row->IsReachable = TRUE;
            Row->LinkSpeed = Path->Local ? 0 : (ULONGLONG)IF->Speed * 100;
        }
        Msg->Count++;
    }
}

NTSTATUS
LibIP6QueryPaths(PIP_PATH_TABLE Table, ULONG Length, PULONG Information)
{
    struct lib_ip6_path_msg Msg;

    if (Length < FIELD_OFFSET(IP_PATH_TABLE, Rows))
        return STATUS_BUFFER_TOO_SMALL;
    Msg.Table = Table;
    Msg.Capacity = (Length - FIELD_OFFSET(IP_PATH_TABLE, Rows)) / sizeof(IP_PATH_ROW);
    Msg.Count = 0;
    if (tcpip_callback_wait(LibIP6QueryPathsCallback, &Msg) != ERR_OK)
        return STATUS_INSUFFICIENT_RESOURCES;
    Table->NumEntries = Msg.Count;
    *Information = FIELD_OFFSET(IP_PATH_TABLE, Rows) + min(Msg.Count, Msg.Capacity) * sizeof(IP_PATH_ROW);
    return Msg.Count > Msg.Capacity ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
}

static
void
LibIP6FlushPathsCallback(void *arg)
{
    LWIP_UNUSED_ARG(arg);
    RtlZeroMemory(LibIP6Paths, sizeof(LibIP6Paths));
    nd6_clear_destination_cache();
}

NTSTATUS
LibIP6FlushPaths(VOID)
{
    if (tcpip_callback_wait(LibIP6FlushPathsCallback, NULL) != ERR_OK)
        return STATUS_INSUFFICIENT_RESOURCES;
    return STATUS_SUCCESS;
}

static
VOID
LibIP6Timer(void *arg)
{
    ULONG i;

    LWIP_UNUSED_ARG(arg);

    for (i = 0; i < LIB_IP6_ADDRESS_INFOS; i++)
    {
        PLIB_IP6_ADDRESS_INFO Info = &LibIP6AddressInfo[i];
        ULONG Valid, Preferred;
        ip6_addr_t Prefix;
        s8_t Slot;

        if (!Info->Netif)
            continue;
        Slot = netif_get_ip6_addr_match(Info->Netif, &Info->Address);
        if (Slot < 0)
        {
            LibIP6ForgetAddressInfo(Info->Netif, Info);
            continue;
        }
        if (!Info->Temporary || Info->Regenerated || !ip6_addr_ispreferred(netif_ip6_addr_state(Info->Netif, Slot)) ||
            netif_ip6_addr_pref_life(Info->Netif, Slot) > LIB_IP6_TEMP_REGEN_ADVANCE)
            continue;
        Info->Regenerated = TRUE;
        ip6_addr_copy(Prefix, Info->Address);
        Prefix.addr[2] = Prefix.addr[3] = 0;
        if (LibIP6PublicLifetimes(Info->Netif, &Prefix, &Valid, &Preferred) && Preferred > LIB_IP6_TEMP_REGEN_ADVANCE)
            LibIP6NewTemporary(Info->Netif, &Prefix, Valid, Preferred);
    }
    LibIP6ScheduleAdvertisements();
    LibIP6ExpirePaths();
    sys_timeout(LIB_IP6_TIMER_INTERVAL, LibIP6Timer, NULL);
}

static
u8_t
LibIP6RaRecv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    struct netif *Netif = ip_current_input_netif();
    const struct ip6_hdr *Header = ip6_current_header();
    u16_t HeaderLength = ip_current_header_tot_len();
    struct ra_header Ra;
    PLIB_IP6_RA_INFO Info;
    u16_t Offset, Checksum;

    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(pcb);

    if (Netif && Netif->state && Header && IP_IS_V6(addr) && IP6H_HOPLIM(Header) == 255 &&
        p->tot_len >= HeaderLength + 8 && pbuf_get_at(p, HeaderLength) == ICMP6_TYPE_RS &&
        pbuf_get_at(p, HeaderLength + 1) == 0)
    {
        LibIP6RouterSolicited(Netif);
        return 0;
    }
    if (!Netif || !Netif->state || !Header || !IP_IS_V6(addr) || IP6H_HOPLIM(Header) != 255 ||
        !ip6_addr_islinklocal(ip_2_ip6(addr)) || p->tot_len < HeaderLength + sizeof(Ra))
        return 0;
    if (netif_get_ip6_addr_match(Netif, ip_2_ip6(addr)) >= 0)
    {
        pbuf_free(p);
        return 1;
    }
    if (pbuf_copy_partial(p, &Ra, sizeof(Ra), HeaderLength) != sizeof(Ra) || Ra.type != ICMP6_TYPE_RA || Ra.code)
        return 0;
    Info = LibIP6FindRaInfo(Netif, FALSE);
    if (Info && Info->RouterDiscoveryDisabled)
    {
        pbuf_free(p);
        return 1;
    }

    if (pbuf_remove_header(p, HeaderLength))
        return 0;
    Checksum = ip6_chksum_pseudo(p, IP6_NEXTH_ICMP6, p->tot_len, ip6_current_src_addr(), ip6_current_dest_addr());
    pbuf_add_header_force(p, HeaderLength);
    if (Checksum)
        return 0;

    Info = LibIP6FindRaInfo(Netif, TRUE);
    if (!Info)
        return 0;
    Info->Seen = TRUE;
    Info->Managed = (Ra.flags & ND6_RA_FLAG_MANAGED_ADDR_CONFIG) != 0;
    Info->Other = (Ra.flags & ND6_RA_FLAG_OTHER_CONFIG) != 0;

    Offset = HeaderLength + sizeof(Ra);
    while (Offset + 2 <= p->tot_len)
    {
        UCHAR Option[2], Data[256 + 8];
        ULONG Length, Lifetime;

        pbuf_copy_partial(p, Option, sizeof(Option), Offset);
        Length = Option[1] * 8;
        if (!Length || Offset + Length > p->tot_len)
            break;

        if (Option[0] == ND6_OPTION_TYPE_PREFIX_INFO && Length == sizeof(struct prefix_option))
        {
            struct prefix_option Pio;
            ip6_addr_t Prefix;

            pbuf_copy_partial(p, &Pio, sizeof(Pio), Offset);
            ip6_addr_copy_from_packed(Prefix, Pio.prefix);
            ip6_addr_clear_zone(&Prefix);
            if ((Pio.flags & ND6_PREFIX_FLAG_AUTONOMOUS) && Pio.prefix_length == 64 &&
                !ip6_addr_islinklocal(&Prefix) && lwip_ntohl(Pio.preferred_lifetime) <= lwip_ntohl(Pio.valid_lifetime))
            {
                LibIP6TemporaryPrefix(Netif, &Prefix, lwip_ntohl(Pio.valid_lifetime), lwip_ntohl(Pio.preferred_lifetime));
            }
        }
        if ((Option[0] == ND6_OPTION_TYPE_RDNSS || Option[0] == ND6_OPTION_TYPE_DNSSL ||
             Option[0] == ND6_OPTION_TYPE_ROUTE_INFO) && Length >= 8 && Length <= sizeof(Data))
        {
            pbuf_copy_partial(p, Data, (u16_t)Length, Offset);
            Lifetime = lwip_ntohl(*(u32_t *)&Data[4]);

            if (Option[0] == ND6_OPTION_TYPE_RDNSS)
            {
                ULONG i;

                for (i = 8; i + 16 <= Length; i += 16)
                {
                    ip6_addr_t Server;

                    RtlCopyMemory(Server.addr, &Data[i], sizeof(Server.addr));
                    ip6_addr_clear_zone(&Server);
                    LibIP6NoteRdnss(Info, &Server, Lifetime);
                }
            }
            else if (Option[0] == ND6_OPTION_TYPE_DNSSL)
            {
                LibIP6ParseDnssl(Info, &Data[8], Length - 8, Lifetime);
            }
            else if (Data[2] <= 128 && (Data[2] <= 64 || Length >= 24) && (Data[2] == 0 || Length >= 16))
            {
                ip6_addr_t Prefix;
                ULONG Bytes = (Length - 8) < 16 ? (Length - 8) : 16;

                ip6_addr_set_zero(&Prefix);
                RtlCopyMemory(Prefix.addr, &Data[8], Bytes);
                LibIP6MaskPrefix(&Prefix, Data[2]);
                if (Lifetime)
                {
                    LibIP6StoreRoute(Netif, &Prefix, Data[2], ip_2_ip6(addr), 256, MIB_IPPROTO_NETMGMT,
                                     NlroRouterAdvertisement, Lifetime, Lifetime);
                }
                else
                {
                    PLIB_IP6_ROUTE Route = LibIP6FindRouteEntry(Netif, &Prefix, Data[2], ip_2_ip6(addr));

                    if (Route)
                    {
                        RtlZeroMemory(Route, sizeof(*Route));
                        nd6_clear_destination_cache();
                    }
                }
            }
        }
        Offset += (u16_t)Length;
    }

    return 0;
}

VOID
LibIP6ConfigInit(VOID)
{
    LibIP6LoadSecret();
    LibIP6RaPcb = raw_new_ip_type(IPADDR_TYPE_V6, IP6_NEXTH_ICMP6);
    if (LibIP6RaPcb)
        raw_recv(LibIP6RaPcb, LibIP6RaRecv, NULL);
    LibIP6RaSendPcb = raw_new_ip_type(IPADDR_TYPE_V6, IP6_NEXTH_ICMP6);
    if (LibIP6RaSendPcb)
    {
        LibIP6RaSendPcb->ttl = 255;
        LibIP6RaSendPcb->chksum_reqd = 1;
        LibIP6RaSendPcb->chksum_offset = 2;
    }
    sys_timeout(LIB_IP6_TIMER_INTERVAL, LibIP6Timer, NULL);
}

VOID
LibIP6ConfigForgetNetif(struct netif *Netif)
{
    ULONG i;

    for (i = 0; i < LIB_IP6_ROUTES; i++)
    {
        if (LibIP6Routes[i].Netif == Netif)
            RtlZeroMemory(&LibIP6Routes[i], sizeof(LibIP6Routes[i]));
    }
    for (i = 0; i < LIB_IP6_ADDRESS_INFOS; i++)
    {
        if (LibIP6AddressInfo[i].Netif == Netif)
            RtlZeroMemory(&LibIP6AddressInfo[i], sizeof(LibIP6AddressInfo[i]));
    }
    for (i = 0; i < LIB_IP6_RA_NETIFS; i++)
    {
        if (LibIP6RaInfo[i].Netif == Netif)
            RtlZeroMemory(&LibIP6RaInfo[i], sizeof(LibIP6RaInfo[i]));
    }
    for (i = 0; i < LIB_IP6_PATHS; i++)
    {
        if (LibIP6Paths[i].Netif == Netif)
            RtlZeroMemory(&LibIP6Paths[i], sizeof(LibIP6Paths[i]));
    }
}

struct lib_ip6_set_msg
{
    PIP6_ADDRESS_SET Address;
    PIP6_ROUTE_SET Route;
    PIP6_INTERFACE_SET Interface;
    PIP6_NEIGHBOR_SET Neighbor;
    PIP6_BEST_ROUTE BestRoute;
    NTSTATUS Status;
};

static
BOOLEAN
LibIP6AddressExists(const ip6_addr_t *Address)
{
    struct netif *netif;

    NETIF_FOREACH(netif)
    {
        if (netif->state && netif_get_ip6_addr_match(netif, Address) >= 0)
            return TRUE;
    }
    return FALSE;
}

static
VOID
LibIP6ApplyLifetimes(struct netif *Netif, s8_t Slot, ULONG ValidLifetime, ULONG PreferredLifetime, BOOLEAN Manual)
{
    if (Manual && ValidLifetime == 0xFFFFFFFF && PreferredLifetime == 0xFFFFFFFF)
    {
        netif_ip6_addr_set_valid_life(Netif, Slot, IP6_ADDR_LIFE_STATIC);
        netif_ip6_addr_set_pref_life(Netif, Slot, IP6_ADDR_LIFE_STATIC);
    }
    else
    {
        netif_ip6_addr_set_valid_life(Netif, Slot, ValidLifetime == 0xFFFFFFFF ? IP6_ADDR_LIFE_INFINITE : ValidLifetime);
        netif_ip6_addr_set_pref_life(Netif, Slot, PreferredLifetime == 0xFFFFFFFF ? IP6_ADDR_LIFE_INFINITE : PreferredLifetime);
    }
}

static
VOID
LibIP6SetAddressCallback(void *arg)
{
    struct lib_ip6_set_msg *Msg = arg;
    PIP6_ADDRESS_SET Request = Msg->Address;
    struct netif *Netif = LibIP6FindNetif(Request->Index);
    PLIB_IP6_ADDRESS_INFO Info;
    ip6_addr_t Address;
    s8_t Slot;
    ULONG i;

    if (!Netif)
    {
        Msg->Status = STATUS_NOT_FOUND;
        return;
    }

    RtlCopyMemory(Address.addr, Request->Address, sizeof(Address.addr));
    ip6_addr_clear_zone(&Address);
    if (ip6_addr_has_scope(&Address, IP6_UNICAST))
        ip6_addr_assign_zone(&Address, IP6_UNICAST, Netif);
    Slot = netif_get_ip6_addr_match(Netif, &Address);

    switch (Request->Operation)
    {
    case Ip6SetAdd:
        if (Slot >= 0 || LibIP6AddressExists(&Address))
        {
            Msg->Status = STATUS_OBJECT_NAME_COLLISION;
            return;
        }
        for (i = 0; i < LIB_IP6_ADDRESS_INFOS && LibIP6AddressInfo[i].Netif; i++);
        if (i == LIB_IP6_ADDRESS_INFOS || netif_add_ip6_address(Netif, &Address, &Slot) != ERR_OK)
        {
            Msg->Status = STATUS_INSUFFICIENT_RESOURCES;
            return;
        }
        Info = &LibIP6AddressInfo[i];
        Info->Netif = Netif;
        ip6_addr_copy(Info->Address, Address);
        Info->PrefixLength = Request->PrefixLength;
        Info->PrefixOrigin = Request->PrefixOrigin;
        Info->SuffixOrigin = Request->SuffixOrigin;
        Info->SkipAsSource = Request->SkipAsSource;
        LibIP6ApplyLifetimes(Netif, Slot, Request->ValidLifetime, Request->PreferredLifetime,
                             Request->PrefixOrigin == IpPrefixOriginManual);
        if (!Request->PreferredLifetime && Request->ValidLifetime)
            netif_ip6_addr_set_state(Netif, Slot, IP6_ADDR_DEPRECATED);
        else if (LibIP6NetifIsLoopback(Netif))
            netif_ip6_addr_set_state(Netif, Slot, IP6_ADDR_PREFERRED);
        LibIP6AddImplicitRoute(Netif, Info);
        nd6_clear_destination_cache();
        Msg->Status = STATUS_SUCCESS;
        return;

    case Ip6SetDelete:
        if (Slot < 0)
        {
            Msg->Status = STATUS_NOT_FOUND;
            return;
        }
        Info = LibIP6FindAddressInfo(Netif, &Address);
        if (Info)
            LibIP6ForgetAddressInfo(Netif, Info);
        netif_ip6_addr_set_state(Netif, Slot, IP6_ADDR_INVALID);
        nd6_clear_destination_cache();
        Msg->Status = STATUS_SUCCESS;
        return;

    case Ip6SetUpdate:
        if (Slot < 0)
        {
            Msg->Status = STATUS_NOT_FOUND;
            return;
        }
        Info = LibIP6FindAddressInfo(Netif, &Address);
        if (Info)
        {
            if (Info->PrefixLength != Request->PrefixLength)
            {
                LibIP6DropImplicitRoute(Netif, Info);
                Info->PrefixLength = Request->PrefixLength;
                LibIP6AddImplicitRoute(Netif, Info);
            }
            Info->PrefixOrigin = Request->PrefixOrigin;
            Info->SuffixOrigin = Request->SuffixOrigin;
            Info->SkipAsSource = Request->SkipAsSource;
        }
        LibIP6ApplyLifetimes(Netif, Slot, Request->ValidLifetime, Request->PreferredLifetime,
                             Info && Info->PrefixOrigin == IpPrefixOriginManual);
        if (ip6_addr_isvalid(netif_ip6_addr_state(Netif, Slot)) && !ip6_addr_istentative(netif_ip6_addr_state(Netif, Slot)))
        {
            netif_ip6_addr_set_state(Netif, Slot, (!Request->PreferredLifetime && Request->ValidLifetime) ?
                                     IP6_ADDR_DEPRECATED : IP6_ADDR_PREFERRED);
        }
        Msg->Status = STATUS_SUCCESS;
        return;

    default:
        Msg->Status = STATUS_INVALID_PARAMETER;
        return;
    }
}

static
VOID
LibIP6SetRouteCallback(void *arg)
{
    struct lib_ip6_set_msg *Msg = arg;
    PIP6_ROUTE_SET Request = Msg->Route;
    struct netif *Netif = LibIP6FindNetif(Request->Index);
    ip6_addr_t Prefix, NextHop;
    PLIB_IP6_ROUTE Route;

    if (!Netif)
    {
        Msg->Status = STATUS_NOT_FOUND;
        return;
    }

    RtlCopyMemory(Prefix.addr, Request->Prefix, sizeof(Prefix.addr));
    ip6_addr_clear_zone(&Prefix);
    RtlCopyMemory(NextHop.addr, Request->NextHop, sizeof(NextHop.addr));
    ip6_addr_clear_zone(&NextHop);
    Route = LibIP6FindRouteEntry(Netif, &Prefix, Request->PrefixLength, &NextHop);

    switch (Request->Operation)
    {
    case Ip6SetAdd:
        if (Route)
        {
            Msg->Status = STATUS_OBJECT_NAME_COLLISION;
            return;
        }
        Route = LibIP6StoreRoute(Netif, &Prefix, Request->PrefixLength, &NextHop, Request->Metric,
                                 Request->Protocol, NlroManual, Request->ValidLifetime, Request->PreferredLifetime);
        if (!Route)
        {
            Msg->Status = STATUS_INSUFFICIENT_RESOURCES;
            return;
        }
        Route->Publish = Request->Publish;
        Msg->Status = STATUS_SUCCESS;
        return;

    case Ip6SetUpdate:
        if (!Route)
        {
            Msg->Status = STATUS_NOT_FOUND;
            return;
        }
        Route->Metric = Request->Metric;
        Route->Publish = Request->Publish;
        Route->ValidLifetime = Request->ValidLifetime;
        Route->PreferredLifetime = Request->PreferredLifetime;
        Route->Expiry = LibIP6Expiry(Request->ValidLifetime);
        nd6_clear_destination_cache();
        Msg->Status = STATUS_SUCCESS;
        return;

    case Ip6SetDelete:
        if (!Route)
        {
            Msg->Status = STATUS_NOT_FOUND;
            return;
        }
        RtlZeroMemory(Route, sizeof(*Route));
        nd6_clear_destination_cache();
        Msg->Status = STATUS_SUCCESS;
        return;

    default:
        Msg->Status = STATUS_INVALID_PARAMETER;
        return;
    }
}

NTSTATUS
LibIP6SetAddress(PIP6_ADDRESS_SET Request)
{
    struct lib_ip6_set_msg Msg;

    if (Request->PrefixLength > 128)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Msg, sizeof(Msg));
    Msg.Address = Request;
    Msg.Status = STATUS_UNSUCCESSFUL;
    if (tcpip_callback_wait(LibIP6SetAddressCallback, &Msg) != ERR_OK)
        return STATUS_INSUFFICIENT_RESOURCES;
    return Msg.Status;
}

NTSTATUS
LibIP6SetRoute(PIP6_ROUTE_SET Request)
{
    struct lib_ip6_set_msg Msg;

    if (Request->PrefixLength > 128)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Msg, sizeof(Msg));
    Msg.Route = Request;
    Msg.Status = STATUS_UNSUCCESSFUL;
    if (tcpip_callback_wait(LibIP6SetRouteCallback, &Msg) != ERR_OK)
        return STATUS_INSUFFICIENT_RESOURCES;
    return Msg.Status;
}

static
VOID
LibIP6ForgetRouterState(struct netif *Netif, PLIB_IP6_RA_INFO Info)
{
    ULONG i;

    for (i = 0; i < LWIP_ND6_NUM_ROUTERS; i++)
    {
        struct nd6_neighbor_cache_entry *Entry = default_router_list[i].neighbor_entry;

        if (Entry && Entry->netif == Netif)
        {
            default_router_list[i].neighbor_entry = NULL;
            default_router_list[i].invalidation_timer = 0;
            default_router_list[i].flags = 0;
            Entry->isrouter = 0;
        }
    }
    for (i = 0; i < LWIP_ND6_NUM_PREFIXES; i++)
    {
        if (prefix_list[i].netif == Netif)
        {
            prefix_list[i].netif = NULL;
            prefix_list[i].invalidation_timer = 0;
        }
    }
    for (i = 0; i < LIB_IP6_ROUTES; i++)
    {
        if (LibIP6Routes[i].Netif == Netif && LibIP6Routes[i].Origin == NlroRouterAdvertisement)
            RtlZeroMemory(&LibIP6Routes[i], sizeof(LibIP6Routes[i]));
    }
    Info->Seen = FALSE;
    Info->Managed = FALSE;
    Info->Other = FALSE;
    RtlZeroMemory(Info->Rdnss, sizeof(Info->Rdnss));
    RtlZeroMemory(Info->Dnssl, sizeof(Info->Dnssl));
    nd6_clear_destination_cache();
}

static
ULONG
LibIP6ReachableTimeBase(struct netif *Netif, PLIB_IP6_RA_INFO Info)
{
    LWIP_UNUSED_ARG(Netif);
    return Info->BaseReachableTime ? Info->BaseReachableTime : LWIP_ND6_REACHABLE_TIME;
}

static
VOID
LibIP6SetInterfaceCallback(void *arg)
{
    struct lib_ip6_set_msg *Msg = arg;
    PIP6_INTERFACE_SET Request = Msg->Interface;
    struct netif *Netif = LibIP6FindNetif(Request->Index);
    PLIB_IP6_RA_INFO Info;
    BOOLEAN Loopback, WasAdvertising;

    if (!Netif)
    {
        Msg->Status = STATUS_NOT_FOUND;
        return;
    }
    Loopback = LibIP6NetifIsLoopback(Netif);
    if ((Request->Mtu && Request->Mtu < 1280) || !Request->BaseReachableTime ||
        Request->RouterDiscoveryBehavior > RouterDiscoveryDhcp)
    {
        Msg->Status = STATUS_INVALID_PARAMETER;
        return;
    }
    Info = LibIP6FindRaInfo(Netif, TRUE);
    if (!Info)
    {
        Msg->Status = STATUS_INSUFFICIENT_RESOURCES;
        return;
    }

    if (Request->Mtu && Request->Mtu != netif_mtu6(Netif))
    {
        Netif->mtu6 = (u16_t)min(Request->Mtu, Netif->mtu);
        nd6_clear_destination_cache();
    }
    if (Request->DadTransmits != LibIP6DadTransmits(Netif, Loopback ? 0 : LWIP_IPV6_DUP_DETECT_ATTEMPTS))
    {
        Info->ManualDad = TRUE;
        Info->DadTransmits = Request->DadTransmits;
    }
    if (Request->BaseReachableTime != LibIP6ReachableTimeBase(Netif, Info))
    {
        ULONG Random = 0;

        BCryptGenRandom(NULL, (PUCHAR)&Random, sizeof(Random), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        Info->BaseReachableTime = Request->BaseReachableTime;
        Info->ReachableTime = (ULONG)(((ULONG64)Request->BaseReachableTime * (512 + (Random % 1024))) / 1024);
    }
    if (Request->RetransmitTime != LibIP6RetransTime(Netif, retrans_timer))
        Info->RetransmitTime = max(Request->RetransmitTime, LIB_IP6_MIN_RETRANSMIT);
    Info->ManualSitePrefix = TRUE;
    Info->SitePrefixLength = Request->SitePrefixLength;
    Info->ManualMetric = !Request->UseAutomaticMetric;
    Info->Metric = Request->UseAutomaticMetric ? 0 : Request->Metric;
    Info->Forwarding = Request->Forwarding;
    Info->WeakHostSend = Request->WeakHostSend;
    Info->WeakHostReceive = Request->WeakHostReceive;
    Info->PathMtuDiscoveryTimeout = Request->PathMtuDiscoveryTimeout;
    Info->LinkLocalAddressTimeout = Request->LinkLocalAddressTimeout;
    RtlCopyMemory(Info->ZoneIndices, Request->ZoneIndices, sizeof(Info->ZoneIndices));
    if (Request->DisableDefaultRoutes != Info->DisableDefaultRoutes)
    {
        Info->DisableDefaultRoutes = Request->DisableDefaultRoutes;
        nd6_clear_destination_cache();
    }
    WasAdvertising = Info->Advertising;
    Info->Advertising = !Loopback && Request->Advertising;
    Info->AdvertiseDefaultRoute = Request->AdvertiseDefaultRoute;
    Info->AdvManaged = Request->ManagedAddressConfiguration;
    Info->AdvOther = Request->OtherStatefulConfiguration;
    if (Info->Advertising && !WasAdvertising)
    {
        Info->InitialAdvertisements = 0;
        Info->NextAdvertisement = 0;
    }
    else if (!Info->Advertising && WasAdvertising)
    {
        LibIP6SendAdvertisement(Netif, Info, TRUE);
    }
    Info->ManualRouterDiscovery = TRUE;
    Info->RouterDiscoveryBehavior = Loopback ? RouterDiscoveryDisabled : Request->RouterDiscoveryBehavior;
    if (!Loopback && Request->RouterDiscovery == Info->RouterDiscoveryDisabled)
    {
        Info->RouterDiscoveryDisabled = !Request->RouterDiscovery;
        if (Info->RouterDiscoveryDisabled)
        {
            Netif->rs_count = 0;
            LibIP6ForgetRouterState(Netif, Info);
        }
        else
        {
            Netif->rs_count = LWIP_ND6_MAX_MULTICAST_SOLICIT;
        }
    }
    nd6_clear_destination_cache();
    Msg->Status = STATUS_SUCCESS;
}

static
struct nd6_neighbor_cache_entry *
LibIP6FindNeighbor(struct netif *Netif, const ip6_addr_t *Address)
{
    ULONG i;

    for (i = 0; i < LWIP_ND6_NUM_NEIGHBORS; i++)
    {
        if (neighbor_cache[i].state != ND6_NO_ENTRY && neighbor_cache[i].netif == Netif &&
            ip6_addr_eq(&neighbor_cache[i].next_hop_address, Address))
            return &neighbor_cache[i];
    }
    return NULL;
}

static
VOID
LibIP6SetNeighborCallback(void *arg)
{
    struct lib_ip6_set_msg *Msg = arg;
    PIP6_NEIGHBOR_SET Request = Msg->Neighbor;
    struct netif *Netif = NULL;
    ip6_addr_t Address;

    if (Request->Index)
    {
        Netif = LibIP6FindNetif(Request->Index);
        if (!Netif)
        {
            Msg->Status = STATUS_NOT_FOUND;
            return;
        }
    }

    if (Request->Operation == Ip6SetFlush)
    {
        nd6_flush_neighbors(Netif, 1);
        Msg->Status = STATUS_SUCCESS;
        return;
    }
    if (!Netif)
    {
        Msg->Status = STATUS_INVALID_PARAMETER;
        return;
    }

    RtlCopyMemory(Address.addr, Request->Address, sizeof(Address.addr));
    ip6_addr_clear_zone(&Address);
    if (ip6_addr_has_scope(&Address, IP6_UNICAST))
        ip6_addr_assign_zone(&Address, IP6_UNICAST, Netif);

    switch (Request->Operation)
    {
    case Ip6SetAdd:
    case Ip6SetUpdate:
        if ((Request->Operation == Ip6SetAdd) == (LibIP6FindNeighbor(Netif, &Address) != NULL))
        {
            Msg->Status = Request->Operation == Ip6SetAdd ? STATUS_OBJECT_NAME_COLLISION : STATUS_NOT_FOUND;
            return;
        }
        if (Netif->hwaddr_len > sizeof(Request->PhysicalAddress))
        {
            Msg->Status = STATUS_INVALID_PARAMETER;
            return;
        }
        Msg->Status = nd6_add_neighbor(Netif, &Address, Request->PhysicalAddress, Request->Permanent,
                                       Request->IsRouter) == ERR_OK ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
        return;

    case Ip6SetDelete:
        Msg->Status = nd6_remove_neighbor(Netif, &Address) == ERR_OK ? STATUS_SUCCESS : STATUS_NOT_FOUND;
        return;

    default:
        Msg->Status = STATUS_INVALID_PARAMETER;
        return;
    }
}

NTSTATUS
LibIP6SetInterface(PIP6_INTERFACE_SET Request)
{
    struct lib_ip6_set_msg Msg;

    RtlZeroMemory(&Msg, sizeof(Msg));
    Msg.Interface = Request;
    Msg.Status = STATUS_UNSUCCESSFUL;
    if (tcpip_callback_wait(LibIP6SetInterfaceCallback, &Msg) != ERR_OK)
        return STATUS_INSUFFICIENT_RESOURCES;
    return Msg.Status;
}

NTSTATUS
LibIP6SetNeighbor(PIP6_NEIGHBOR_SET Request)
{
    struct lib_ip6_set_msg Msg;

    if (Request->PhysicalAddressLength > sizeof(Request->PhysicalAddress))
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Msg, sizeof(Msg));
    Msg.Neighbor = Request;
    Msg.Status = STATUS_UNSUCCESSFUL;
    if (tcpip_callback_wait(LibIP6SetNeighborCallback, &Msg) != ERR_OK)
        return STATUS_INSUFFICIENT_RESOURCES;
    return Msg.Status;
}

static
struct netif *
LibIP6SourceNetif(const ip6_addr_t *Source)
{
    struct netif *netif;

    NETIF_FOREACH(netif)
    {
        if (netif->state && netif_get_ip6_addr_match(netif, Source) >= 0)
            return netif;
    }
    return NULL;
}

static
VOID
LibIP6GetBestRouteCallback(void *arg)
{
    struct lib_ip6_set_msg *Msg = arg;
    PIP6_BEST_ROUTE Request = Msg->BestRoute;
    struct netif *Netif = NULL;
    ip6_addr_t Destination, Source, Any;
    const ip_addr_t *Selected;
    PLIB_IP6_ROUTE Route;
    PIP_INTERFACE IF;
    ULONG i;

    RtlCopyMemory(Destination.addr, Request->Destination, sizeof(Destination.addr));
    ip6_addr_clear_zone(&Destination);
    RtlCopyMemory(Source.addr, Request->Source, sizeof(Source.addr));
    ip6_addr_clear_zone(&Source);
    ip6_addr_set_zero(&Any);

    if (Request->InterfaceIndex)
        Netif = LibIP6FindNetif(Request->InterfaceIndex);
    else if (ip6_addr_has_scope(&Destination, IP6_UNKNOWN) && Request->ScopeId)
        Netif = LibIP6FindNetif(Request->ScopeId);
    if (!ip6_addr_isany(&Source))
    {
        struct netif *Owner;

        if (ip6_addr_has_scope(&Source, IP6_UNICAST) && (Netif || Request->ScopeId))
            ip6_addr_assign_zone(&Source, IP6_UNICAST, Netif ? Netif : LibIP6FindNetif(Request->ScopeId));
        Owner = LibIP6SourceNetif(&Source);
        if (!Owner || (Netif && Owner != Netif))
        {
            Msg->Status = STATUS_NOT_FOUND;
            return;
        }
        Netif = Owner;
    }
    if (Netif && ip6_addr_has_scope(&Destination, IP6_UNKNOWN))
        ip6_addr_assign_zone(&Destination, IP6_UNKNOWN, Netif);
    if (!Netif)
        Netif = ip6_route(&Any, &Destination);
    if (!Netif || !(IF = Netif->state))
    {
        Msg->Status = STATUS_NETWORK_UNREACHABLE;
        return;
    }
    if (ip6_addr_has_scope(&Destination, IP6_UNKNOWN))
        ip6_addr_assign_zone(&Destination, IP6_UNKNOWN, Netif);

    if (ip6_addr_isany(&Source))
    {
        Selected = ip6_select_source_address(Netif, &Destination);
        if (!Selected)
        {
            Msg->Status = STATUS_NETWORK_UNREACHABLE;
            return;
        }
        ip6_addr_copy(Source, *ip_2_ip6(Selected));
    }

    Request->InterfaceIndex = IF->Index;
    RtlCopyMemory(Request->Source, Source.addr, sizeof(Request->Source));
    RtlZeroMemory(Request->Prefix, sizeof(Request->Prefix));
    RtlZeroMemory(Request->NextHop, sizeof(Request->NextHop));
    Request->PrefixLength = 0;
    Request->Metric = 256;
    Request->Protocol = MIB_IPPROTO_NETMGMT;
    Request->Origin = NlroRouterAdvertisement;

    Route = LibIP6LookupRoute(&Destination, Netif);
    if (Route && !LibIP6RouteYields(Route, &Destination, Netif))
    {
        RtlCopyMemory(Request->Prefix, Route->Prefix.addr, sizeof(Request->Prefix));
        Request->PrefixLength = Route->PrefixLength;
        RtlCopyMemory(Request->NextHop, Route->NextHop.addr, sizeof(Request->NextHop));
        Request->Metric = Route->Metric;
        Request->Protocol = Route->Protocol;
        Request->Origin = Route->Origin;
        Msg->Status = STATUS_SUCCESS;
        return;
    }
    if (ip6_addr_islinklocal(&Destination) || ip6_addr_ismulticast(&Destination) || LibIP6DestinationOnLink(&Destination))
    {
        ip6_addr_t Prefix;

        ip6_addr_copy(Prefix, Destination);
        ip6_addr_clear_zone(&Prefix);
        Request->PrefixLength = ip6_addr_ismulticast(&Destination) ? 8 : 64;
        LibIP6MaskPrefix(&Prefix, Request->PrefixLength);
        RtlCopyMemory(Request->Prefix, Prefix.addr, sizeof(Request->Prefix));
        Request->Protocol = MIB_IPPROTO_LOCAL;
        Request->Origin = NlroManual;
        Msg->Status = STATUS_SUCCESS;
        return;
    }
    for (i = 0; i < LWIP_ND6_NUM_ROUTERS; i++)
    {
        struct nd6_neighbor_cache_entry *Entry = default_router_list[i].neighbor_entry;

        if (Entry && Entry->netif == Netif && LibIP6RouterAllowed(Netif))
        {
            RtlCopyMemory(Request->NextHop, Entry->next_hop_address.addr, sizeof(Request->NextHop));
            Msg->Status = STATUS_SUCCESS;
            return;
        }
    }
    Msg->Status = STATUS_NETWORK_UNREACHABLE;
}

NTSTATUS
LibIP6GetBestRoute(PIP6_BEST_ROUTE Request)
{
    struct lib_ip6_set_msg Msg;

    RtlZeroMemory(&Msg, sizeof(Msg));
    Msg.BestRoute = Request;
    Msg.Status = STATUS_UNSUCCESSFUL;
    if (tcpip_callback_wait(LibIP6GetBestRouteCallback, &Msg) != ERR_OK)
        return STATUS_INSUFFICIENT_RESOURCES;
    return Msg.Status;
}
