
#pragma once

/* Ioctl called by GetInterfaceInfo. Returns IP_INTERFACE_INFO structure. */
#define IOCTL_IP_INTERFACE_INFO _TCP_CTL_CODE(0x10, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP6_QUERY_TABLE _TCP_CTL_CODE(0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_ICMPV6_ECHO_REQUEST _TCP_CTL_CODE(0x801, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)

#define IOCTL_IP_RESOLVE_NEIGHBOR _TCP_CTL_CODE(0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP_GET_BEST_SOURCE _TCP_CTL_CODE(0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _IP_BEST_SOURCE
{
    ULONG Destination;
    ULONG InterfaceIndex;
    ULONG Source;
    ULONG NextHop;
} IP_BEST_SOURCE, *PIP_BEST_SOURCE;

typedef enum _IP6_TABLE_ID
{
    Ip6TableInterfaces,
    Ip6TableAddresses,
    Ip6TableRoutes,
    Ip6TableNeighbors,
    Ip6TableStatistics,
    Ip6TableEndpoints
} IP6_TABLE_ID;

typedef struct _IP6_TABLE_HEADER
{
    ULONG Count;
    ULONG Needed;
} IP6_TABLE_HEADER, *PIP6_TABLE_HEADER;

typedef struct _IP6_INTERFACE_ROW
{
    ULONG Index;
    ULONG Mtu;
    ULONG HopLimit;
    ULONG BaseReachableTime;
    ULONG ReachableTime;
    ULONG RetransmitTime;
    ULONG DadTransmits;
    BOOLEAN Connected;
    BOOLEAN Loopback;
    BOOLEAN Autoconfiguration;
    BOOLEAN Reserved;
    ULONG64 LinkSpeed;
} IP6_INTERFACE_ROW, *PIP6_INTERFACE_ROW;

typedef struct _IP6_ADDRESS_ROW
{
    ULONG Index;
    UCHAR Address[16];
    ULONG PrefixLength;
    ULONG DadState;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    ULONG PrefixOrigin;
    ULONG SuffixOrigin;
} IP6_ADDRESS_ROW, *PIP6_ADDRESS_ROW;

typedef struct _IP6_ROUTE_ROW
{
    ULONG Index;
    UCHAR Prefix[16];
    ULONG PrefixLength;
    UCHAR NextHop[16];
    ULONG Metric;
    ULONG Protocol;
    ULONG Origin;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    BOOLEAN Loopback;
    BOOLEAN Autoconfigure;
    BOOLEAN Publish;
    BOOLEAN Immortal;
} IP6_ROUTE_ROW, *PIP6_ROUTE_ROW;

typedef struct _IP6_NEIGHBOR_ROW
{
    ULONG Index;
    UCHAR Address[16];
    UCHAR PhysicalAddress[32];
    ULONG PhysicalAddressLength;
    ULONG State;
    BOOLEAN IsRouter;
    BOOLEAN IsUnreachable;
    BOOLEAN Reserved[2];
} IP6_NEIGHBOR_ROW, *PIP6_NEIGHBOR_ROW;

typedef struct _IP6_STATISTICS
{
    ULONG DefaultHopLimit;
    ULONG NumInterfaces;
    ULONG NumAddresses;
    ULONG NumRoutes;
    ULONGLONG InReceives;
    ULONGLONG InDelivers;
    ULONGLONG OutRequests;
    ULONGLONG ForwardedDatagrams;
    ULONG InHeaderErrors;
    ULONG InAddressErrors;
    ULONG InUnknownProtocols;
    ULONG InDiscards;
    ULONG OutNoRoutes;
    ULONG OutDiscards;
    ULONG ReassemblyRequired;
    ULONG ReassemblyOk;
    ULONG ReassemblyFailures;
    ULONG FragmentOk;
    ULONG FragmentFailures;
    ULONG FragmentsCreated;
    ULONG ReassemblyTimeout;
    ULONG IcmpInMessages;
    ULONG IcmpInErrors;
    ULONG IcmpOutMessages;
    ULONG IcmpOutErrors;
    ULONGLONG UdpInDatagrams;
    ULONGLONG UdpOutDatagrams;
    ULONG UdpNoPorts;
    ULONG UdpInErrors;
    ULONG UdpNumAddresses;
    ULONG TcpActiveOpens;
    ULONG TcpPassiveOpens;
    ULONG TcpAttemptFailures;
    ULONG TcpEstablishedResets;
    ULONG TcpCurrentEstablished;
    ULONG TcpNumConnections;
    ULONGLONG TcpInSegments;
    ULONGLONG TcpOutSegments;
    ULONG TcpRetransmittedSegments;
    ULONG TcpOutResets;
    ULONG TcpInErrors;
} IP6_STATISTICS, *PIP6_STATISTICS;

typedef struct _IP6_ENDPOINT_ROW
{
    ULONG Protocol;
    UCHAR LocalAddress[16];
    ULONG LocalScopeId;
    USHORT LocalPort;
    USHORT RemotePort;
    UCHAR RemoteAddress[16];
    ULONG RemoteScopeId;
    ULONG State;
    ULONG ProcessId;
    LARGE_INTEGER CreateTime;
    ULONGLONG ModuleInfo;
} IP6_ENDPOINT_ROW, *PIP6_ENDPOINT_ROW;
