/*
   ------------------------------------
   ---------- Memory options ----------
   ------------------------------------
*/

/* This combo allows us to implement malloc, free, and realloc ourselves */
#define MEM_LIBC_MALLOC                 1
#define MEMP_MEM_MALLOC                 1

/* Define LWIP_COMPAT_MUTEX if the port has no mutexes and binary semaphores
 should be used instead */
#define LWIP_COMPAT_MUTEX               1
#define LWIP_COMPAT_MUTEX_ALLOWED       1

#define MEM_ALIGNMENT                   4

#define LWIP_ARP                        0

#define LWIP_ETHERNET                   1

#define LWIP_IPV6                       1

#define LWIP_NETIF_LOOPBACK             1

#define LWIP_HAVE_LOOPIF                0

#define IPV6_FRAG_COPYHEADER            1

#define LWIP_IPV6_NUM_ADDRESSES         10

#define LWIP_ND6_NUM_NEIGHBORS          64

#define LWIP_ND6_NUM_DESTINATIONS       64

#define LWIP_ND6_NUM_PREFIXES           10

#define LWIP_ND6_NUM_ROUTERS            8

#define LWIP_HOOK_FILENAME              "lwiphooks.h"

#define LWIP_HOOK_IP6_ROUTE(src, dest)  LibIP6RouteHook(src, dest)

#define LWIP_HOOK_ND6_GET_GW(netif, dest) LibIP6GatewayHook(netif, dest)

#define LWIP_HOOK_ND6_REACHABLE_TIME(netif, def) LibIP6ReachableTime(netif, def)

#define LWIP_HOOK_ND6_DAD_TRANSMITS(netif, def) LibIP6DadTransmits(netif, def)

#define LWIP_HOOK_ND6_AUTOCONF_SKIP(netif, i) LibIP6AddressIsManaged(netif, i)

#define LWIP_HOOK_ND6_AUTOCONF_ADDRESS(netif, prefix, addr) LibIP6StableAddress(netif, prefix, addr)

#define LWIP_HOOK_IP6_SOURCE_RANK(netif, i) LibIP6SourceRank(netif, i)

#define LWIP_HOOK_ND6_RETRANS_TIME(netif, def) LibIP6RetransTime(netif, def)

#define LWIP_HOOK_ND6_ROUTER_ALLOWED(netif) LibIP6RouterAllowed(netif)

#define LWIP_HOOK_ND6_PMTU_TIMEOUT(nexthop) LibIP6PmtuTimeout(nexthop)

#define LWIP_HOOK_IP6_FORWARD_ALLOWED(netif) LibIP6ForwardAllowed(netif)

#define LWIP_HOOK_IP6_WEAK_RECEIVE(inp, netif) LibIP6WeakReceive(inp, netif)

#define LWIP_HOOK_IP6_WEAK_SEND(src) LibIP6WeakSend(src)

#define LWIP_HOOK_TCP_INPACKET_PCB(pcb, hdr, optlen, opt1len, opt2, p) LibTCPWfpInPacket(pcb, hdr)

#define LWIP_IPV6_FORWARD               1

#define LWIP_HOOK_IP6_NO_DEFAULT_NETIF  1

#define LWIP_HOOK_IP4_DONT_FRAGMENT(proto) ((proto) == IP_PROTO_TCP)
#define LWIP_HOOK_IP6_PATH_OUTPUT(netif, src, dest, nexth, p) LibIP6PathOutput(netif, src, dest, nexth, p)
#define LWIP_HOOK_IP6_ADDR_STATE_CHANGED(netif, idx) TiSignalChange()

unsigned int Random(void);
#define LWIP_RAND() ((u32_t)Random())
#define LWIP_HOOK_IP4_PATH_MTU(dest, netif) LibIP4PathMtu(dest, netif)
#define LWIP_TCP_PCB_BASE_MSS           1

#define ETH_PAD_SIZE                    2

#define IP_REASS_MAX_PBUFS              0xFFFFFFFF

#define IP_DEFAULT_TTL                  128

#define IP_SOF_BROADCAST                1

#define IP_SOF_BROADCAST_RECV           1

#define LWIP_ICMP                       0

#define LWIP_RAW                        1

#define LWIP_UDP                        1

#define SO_REUSE                        1

#define SO_REUSE_RXTOALL                1

/* FIXME: These MSS and TCP Window definitions assume an MTU
 * of 1500. We need to add some code to lwIP which would allow us
 * to change these values based upon the interface we are
 * using. Currently ReactOS only supports Ethernet so we're
 * fine for now but it does need to be fixed later when we
 * add support for other transport mediums */
#define TCP_MSS                         1460

#define LWIP_WND_SCALE                  1
#define TCP_RCV_SCALE                   4
#define TCP_WND                         (512 * 1024)

#define TCP_SND_BUF                     (512 * 1024)

#define TCP_SNDLOWAT                    (32 * 1024)
#define TCP_SNDQUEUELOWAT               64

#define TCP_MAXRTX                      8

#define TCP_SYNMAXRTX                   4

#define TCP_LISTEN_BACKLOG              1

#define LWIP_TCP_TIMESTAMPS             1

#define LWIP_TCP_SACK_OUT               1

#define TCP_TMR_INTERVAL                25

#define LWIP_SOCKET                     0

#define LWIP_NETCONN                    0

#define LWIP_STATS                      1

#define LINK_STATS                      0

#define IP_STATS                        0

#define IPFRAG_STATS                    0

#define TCP_STATS                       0

#define SYS_STATS                       0

#define ICMP_STATS                      0

void LibTCPLockCore(void);
void LibTCPUnlockCore(void);
#define LOCK_TCPIP_CORE()               LibTCPLockCore()
#define UNLOCK_TCPIP_CORE()             LibTCPUnlockCore()

/*
   ---------------------------------------
   ---------- Debugging options ----------
   ---------------------------------------
*/

/**
 * TCP_DEBUG: Enable debugging for TCP.
 */
#define TCP_DEBUG                       LWIP_DBG_ON
