#ifndef _LWIP_GLUE_H_
#define _LWIP_GLUE_H_

#include <lwip/tcp.h>
#include <lwip/pbuf.h>
#include <lwip/ip_addr.h>
#include <tcpip.h>

#ifndef LWIP_TAG
    #define LWIP_TAG         'PIwl'
    #define LWIP_MESSAGE_TAG 'sMwl'
    #define LWIP_QUEUE_TAG   'uQwl'
#endif

typedef struct tcp_pcb* PTCP_PCB;

typedef struct _QUEUE_ENTRY
{
    struct pbuf *p;
    ULONG Offset;
    ULONG UrgentOffset;
    BOOLEAN Urgent;
    LIST_ENTRY ListEntry;
} QUEUE_ENTRY, *PQUEUE_ENTRY;

struct lwip_callback_msg
{
    /* Synchronization */
    KEVENT Event;

    /* Input */
    union {
        struct {
            PVOID Arg;
        } Socket;
        struct {
            struct tcp_pcb* pcb;
        } FreeSocket;
        struct {
            PCONNECTION_ENDPOINT Connection;
            ip_addr_t *IpAddress;
            u16_t Port;
        } Bind;
        struct {
            PCONNECTION_ENDPOINT Connection;
            UINT Backlog;
        } Listen;
        struct {
            PCONNECTION_ENDPOINT Connection;
            void *Data;
            u32_t DataLength;
            ULONG Flags;
        } Send;
        struct {
            PCONNECTION_ENDPOINT Connection;
            ip_addr_t *IpAddress;
            u16_t Port;
        } Connect;
        struct {
            PCONNECTION_ENDPOINT Connection;
            int shut_rx;
            int shut_tx;
        } Shutdown;
        struct {
            PCONNECTION_ENDPOINT Connection;
            int Callback;
        } Close;
        struct {
            PCONNECTION_ENDPOINT Connection;
            u32_t Length;
        } Recved;
    } Input;

    /* Output */
    union {
        struct {
            struct tcp_pcb *NewPcb;
        } Socket;
        struct {
            err_t Error;
        } Bind;
        struct {
            struct tcp_pcb *NewPcb;
        } Listen;
        struct {
            err_t Error;
            u32_t Information;
        } Send;
        struct {
            err_t Error;
        } Connect;
        struct {
            err_t Error;
        } Shutdown;
        struct {
            err_t Error;
        } Close;
    } Output;
};

NTSTATUS    LibTCPGetDataFromConnectionQueue(PCONNECTION_ENDPOINT Connection, PUCHAR RecvBuffer, UINT RecvLen, UINT *Received, const int safe);

/* External TCP event handlers */
extern void TCPConnectEventHandler(void *arg, const err_t err);
extern NTSTATUS TCPAcceptEventHandler(void *arg, PTCP_PCB newpcb);
extern void TCPSendEventHandler(void *arg, const u16_t space);
extern void TCPFinEventHandler(void *arg, const err_t err);
extern void TCPRecvEventHandler(void *arg);

/* TCP functions */
PTCP_PCB    LibTCPSocket(void *arg);
VOID        LibTCPFreeSocket(PTCP_PCB pcb);
err_t       LibTCPBind(PCONNECTION_ENDPOINT Connection, ip_addr_t *const ipaddr, const u16_t port);
PTCP_PCB    LibTCPListen(PCONNECTION_ENDPOINT Connection, const UINT backlog);
err_t       LibTCPSend(PCONNECTION_ENDPOINT Connection, void *const dataptr, const u32_t len, ULONG *sent, ULONG Flags, const int safe);
err_t       LibTCPConnect(PCONNECTION_ENDPOINT Connection, ip_addr_t *const ipaddr, const u16_t port);
err_t       LibTCPShutdown(PCONNECTION_ENDPOINT Connection, const int shut_rx, const int shut_tx);
err_t       LibTCPAbort(PCONNECTION_ENDPOINT Connection);
err_t       LibTCPClose(PCONNECTION_ENDPOINT Connection, const int safe, const int callback);
BOOLEAN     LibTCPCoreHeld(void);

err_t       LibTCPGetPeerName(PTCP_PCB pcb, ip_addr_t *const ipaddr, u16_t *const port);
err_t       LibTCPGetHostName(PTCP_PCB pcb, ip_addr_t *const ipaddr, u16_t *const port);
void        LibTCPAccept(PTCP_PCB pcb, struct tcp_pcb *listen_pcb, void *arg);
void        LibTCPWfpEstablish(PCONNECTION_ENDPOINT Connection, PTCP_PCB pcb, BOOLEAN Outbound);
NTSTATUS    LibTCPDeferAcceptLocked(PCONNECTION_ENDPOINT Connection, PTCP_PCB pcb);
void        LibTCPDrainPendingAccept(PCONNECTION_ENDPOINT Connection);
void        LibTCPSetNoDelay(PTCP_PCB pcb, BOOLEAN Set);
void        LibTCPSetKeepAlive(PTCP_PCB pcb, BOOLEAN Set);
void        LibTcpSetKeepAliveValues(PTCP_PCB pcb, u32_t KeepAliveTime, u32_t KeepAliveInterval);
void        LibTCPGetSocketStatus(PTCP_PCB pcb, PULONG State);
VOID        LibTCPPathMtuExceeded(PIP_INTERFACE Interface, ULONG Source, ULONG Destination, USHORT SourcePort,
                                  USHORT DestinationPort, ULONG Sequence, ULONG Mtu);
BOOLEAN     PmtuLower(PIP_INTERFACE Interface, IPv4_RAW_ADDRESS Destination, ULONG Mtu);
ULONG       PmtuQuery(PIP_INTERFACE Interface, IPv4_RAW_ADDRESS Destination, PBOOLEAN Raised);
ULONG       PmtuForDestination(IPv4_RAW_ADDRESS Destination, ULONG Default);
VOID        PathConfirmV4(IPv4_RAW_ADDRESS Destination);
VOID        LibIP6ConfirmPath(const ip6_addr_t *Destination);

struct lib_ip6_query
{
    ULONG TableId;
    PUCHAR Buffer;
    ULONG Length;
    ULONG Count;
    ULONG Needed;
    struct _IP6_STATISTICS *Statistics;
};

struct _IP6_INTERFACE_ROW;
struct _IP6_ADDRESS_SET;
struct _IP6_ROUTE_SET;
struct _IP6_INTERFACE_SET;
struct _IP6_NEIGHBOR_SET;
struct _IP6_BEST_ROUTE;

/* IP functions */
void LibIPInsertPacket(void *ifarg, const void *const data, const u32_t size);
void LibIP6NoteMulticastDestination(struct netif *netif, const ip6_addr_t *Address);
void LibIP6ForgetNetif(struct netif *netif);
void LibIPAddressToLwip(const IP_ADDRESS *Address, ip_addr_t *IpAddr);
void LibIPAddressFromLwip(const ip_addr_t *IpAddr, PIP_ADDRESS Address);
void LibIPInitialize(void);
VOID LibIP6Emit(struct lib_ip6_query *Query, const VOID *Row, ULONG Size);
BOOLEAN LibIP6NetifIsLoopback(struct netif *netif);
VOID LibIP6ConfigInit(VOID);
VOID LibIP6ConfigForgetNetif(struct netif *Netif);
VOID LibIP6QueryConfigRoutes(struct lib_ip6_query *Query, struct netif *Netif);
VOID LibIP6QueryDns(struct lib_ip6_query *Query);
VOID LibIP6DescribeInterface(struct netif *Netif, struct _IP6_INTERFACE_ROW *Row);
VOID LibIP6DescribeAddress(struct netif *Netif, const ip6_addr_t *Address, PULONG PrefixLength,
                           PULONG PrefixOrigin, PULONG SuffixOrigin, PBOOLEAN SkipAsSource);
NTSTATUS LibIP6SetAddress(struct _IP6_ADDRESS_SET *Request);
NTSTATUS LibIP6SetRoute(struct _IP6_ROUTE_SET *Request);
NTSTATUS LibIP6SetInterface(struct _IP6_INTERFACE_SET *Request);
VOID LibIP6CreateLinkLocal(struct netif *Netif);
NTSTATUS LibIP6SetNeighbor(struct _IP6_NEIGHBOR_SET *Request);
NTSTATUS LibIP6GetBestRoute(struct _IP6_BEST_ROUTE *Request);
void LibIPShutdown(void);
LONGLONG LibSysNowToSystemTime(u32_t Tick);

#endif
