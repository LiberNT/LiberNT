
#pragma once

/* Ioctl called by GetInterfaceInfo. Returns IP_INTERFACE_INFO structure. */
#define IOCTL_IP_INTERFACE_INFO _TCP_CTL_CODE(0x10, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP6_QUERY_TABLE _TCP_CTL_CODE(0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_ICMPV6_ECHO_REQUEST _TCP_CTL_CODE(0x801, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)

#define IOCTL_IP_RESOLVE_NEIGHBOR _TCP_CTL_CODE(0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP_GET_BEST_SOURCE _TCP_CTL_CODE(0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP6_SET_ADDRESS _TCP_CTL_CODE(0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP6_SET_ROUTE _TCP_CTL_CODE(0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP6_SET_INTERFACE _TCP_CTL_CODE(0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP6_SET_NEIGHBOR _TCP_CTL_CODE(0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP6_GET_BEST_ROUTE _TCP_CTL_CODE(0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP_QUERY_INTERFACE _TCP_CTL_CODE(0x809, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP_SET_INTERFACE _TCP_CTL_CODE(0x80A, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP_QUERY_ADDRESS _TCP_CTL_CODE(0x80B, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _IP4_ADDRESS_INFO
{
    ULONG Index;
    ULONG Address;
    ULONG DadState;
    ULONG Origin;
    LARGE_INTEGER CreationTime;
} IP4_ADDRESS_INFO, *PIP4_ADDRESS_INFO;

#define IOCTL_IP_QUERY_ROUTE _TCP_CTL_CODE(0x80C, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _IP4_ROUTE_INFO
{
    ULONG Index;
    ULONG Destination;
    ULONG Mask;
    ULONG NextHop;
    ULONG Origin;
    ULONG ValidLifetime;
    ULONG Age;
    ULONG Reserved;
} IP4_ROUTE_INFO, *PIP4_ROUTE_INFO;

#define IOCTL_IP_QUERY_PATHS _TCP_CTL_CODE(0x80D, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP_FLUSH_PATHS _TCP_CTL_CODE(0x80E, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP_WAIT_CHANGE _TCP_CTL_CODE(0x80F, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP_CHANGE_MEMBERSHIP _TCP_CTL_CODE(0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP_SET_ROUTE _TCP_CTL_CODE(0x811, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IP_SET_ADDRESS _TCP_CTL_CODE(0x812, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _IP_MEMBERSHIP_REQUEST
{
    ULONG Family;
    ULONG Join;
    ULONG Interface;
    UCHAR Group[16];
} IP_MEMBERSHIP_REQUEST, *PIP_MEMBERSHIP_REQUEST;

typedef struct _IP_PATH_ROW
{
    ULONG Index;
    UCHAR Source[16];
    UCHAR Destination[16];
    UCHAR NextHop[16];
    ULONG PathMtu;
    ULONG LastReachable;
    BOOLEAN IsReachable;
    BOOLEAN Reserved[3];
    ULONGLONG LinkSpeed;
} IP_PATH_ROW, *PIP_PATH_ROW;

typedef struct _IP_PATH_TABLE
{
    ULONG NumEntries;
    ULONG Reserved;
    IP_PATH_ROW Rows[ANYSIZE_ARRAY];
} IP_PATH_TABLE, *PIP_PATH_TABLE;

typedef struct _IP4_INTERFACE_INFO
{
    ULONG Index;
    ULONG Metric;
    ULONG Mtu;
    ULONG RetransmitTime;
    ULONG BaseReachableTime;
    ULONG ReachableTime;
    ULONG DadTransmits;
    ULONG RouterDiscoveryBehavior;
    ULONG PathMtuDiscoveryTimeout;
    ULONG LinkLocalAddressTimeout;
    ULONG SitePrefixLength;
    ULONG ZoneIndices[16];
    BOOLEAN UseAutomaticMetric;
    BOOLEAN Forwarding;
    BOOLEAN WeakHostSend;
    BOOLEAN WeakHostReceive;
    BOOLEAN DisableDefaultRoutes;
    BOOLEAN AdvertiseDefaultRoute;
    BOOLEAN DhcpRouterDiscovery;
    BOOLEAN Reserved;
    ULONG PhysicalMedium;
} IP4_INTERFACE_INFO, *PIP4_INTERFACE_INFO;

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
    Ip6TableEndpoints,
    Ip6TableDnsServers,
    Ip6TableDnsSuffixes
} IP6_TABLE_ID;

typedef enum _IP6_SET_OPERATION
{
    Ip6SetAdd,
    Ip6SetDelete,
    Ip6SetUpdate,
    Ip6SetFlush
} IP6_SET_OPERATION;

typedef struct _IP6_ADDRESS_SET
{
    ULONG Operation;
    ULONG Index;
    UCHAR Address[16];
    ULONG PrefixLength;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    ULONG PrefixOrigin;
    ULONG SuffixOrigin;
    BOOLEAN SkipAsSource;
    BOOLEAN Reserved[3];
} IP6_ADDRESS_SET, *PIP6_ADDRESS_SET;

typedef struct _IP6_ROUTE_SET
{
    ULONG Operation;
    ULONG Index;
    UCHAR Prefix[16];
    ULONG PrefixLength;
    UCHAR NextHop[16];
    ULONG Metric;
    ULONG Protocol;
    ULONG ValidLifetime;
    ULONG PreferredLifetime;
    BOOLEAN Publish;
    BOOLEAN Reserved[3];
} IP6_ROUTE_SET, *PIP6_ROUTE_SET;

typedef struct _IP6_INTERFACE_SET
{
    ULONG Index;
    ULONG Metric;
    ULONG Mtu;
    ULONG DadTransmits;
    ULONG BaseReachableTime;
    ULONG RetransmitTime;
    ULONG SitePrefixLength;
    BOOLEAN UseAutomaticMetric;
    BOOLEAN RouterDiscovery;
    BOOLEAN Forwarding;
    BOOLEAN WeakHostSend;
    BOOLEAN WeakHostReceive;
    BOOLEAN Advertising;
    BOOLEAN AdvertiseDefaultRoute;
    BOOLEAN DisableDefaultRoutes;
    BOOLEAN ManagedAddressConfiguration;
    BOOLEAN OtherStatefulConfiguration;
    BOOLEAN Reserved[2];
    ULONG RouterDiscoveryBehavior;
    ULONG PathMtuDiscoveryTimeout;
    ULONG LinkLocalAddressTimeout;
    ULONG ZoneIndices[16];
} IP6_INTERFACE_SET, *PIP6_INTERFACE_SET;

typedef struct _IP6_NEIGHBOR_SET
{
    ULONG Operation;
    ULONG Index;
    UCHAR Address[16];
    UCHAR PhysicalAddress[32];
    ULONG PhysicalAddressLength;
    BOOLEAN Permanent;
    BOOLEAN IsRouter;
    BOOLEAN Reserved[2];
} IP6_NEIGHBOR_SET, *PIP6_NEIGHBOR_SET;

typedef struct _IP6_BEST_ROUTE
{
    UCHAR Destination[16];
    ULONG ScopeId;
    ULONG InterfaceIndex;
    UCHAR Source[16];
    UCHAR Prefix[16];
    ULONG PrefixLength;
    UCHAR NextHop[16];
    ULONG Metric;
    ULONG Protocol;
    ULONG Origin;
} IP6_BEST_ROUTE, *PIP6_BEST_ROUTE;

typedef struct _IP6_DNS_SERVER_ROW
{
    ULONG Index;
    UCHAR Address[16];
    ULONG Lifetime;
} IP6_DNS_SERVER_ROW, *PIP6_DNS_SERVER_ROW;

typedef struct _IP6_DNS_SUFFIX_ROW
{
    ULONG Index;
    ULONG Lifetime;
    WCHAR Suffix[256];
} IP6_DNS_SUFFIX_ROW, *PIP6_DNS_SUFFIX_ROW;

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
    BOOLEAN ManagedAddressConfiguration;
    BOOLEAN OtherStatefulConfiguration;
    BOOLEAN RouterAdvertised;
    BOOLEAN Reserved2;
    ULONG Metric;
    BOOLEAN UseAutomaticMetric;
    BOOLEAN RouterDiscovery;
    BOOLEAN Reserved3[2];
    ULONG SitePrefixLength;
    BOOLEAN Forwarding;
    BOOLEAN WeakHostSend;
    BOOLEAN WeakHostReceive;
    BOOLEAN Advertising;
    BOOLEAN AdvertiseDefaultRoute;
    BOOLEAN DisableDefaultRoutes;
    BOOLEAN Reserved4[2];
    ULONG RouterDiscoveryBehavior;
    ULONG PathMtuDiscoveryTimeout;
    ULONG LinkLocalAddressTimeout;
    ULONG ZoneIndices[16];
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
    BOOLEAN SkipAsSource;
    BOOLEAN Reserved[3];
    LARGE_INTEGER CreationTime;
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
