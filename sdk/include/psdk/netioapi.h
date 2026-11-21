/*
 * Copyright 2015 Hans Leidekker for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_NETIOAPI_H
#define __WINE_NETIOAPI_H

#include <ws2def.h>
#include <ws2ipdef.h>
#include <ifdef.h>
#include <nldef.h>
#include <ntddndis.h>

#ifndef IPHLPAPI_DLL_LINKAGE
#if defined(__REACTOS__) && defined(__GNUC__)
/* FIXME: CORE-6504 */
#define IPHLPAPI_DLL_LINKAGE
#else
#define IPHLPAPI_DLL_LINKAGE DECLSPEC_IMPORT
#endif
#endif

#ifndef ANY_SIZE
#define ANY_SIZE 1
#endif

#if defined(_NTDDK_) || defined(_NTIFS_)
#define NETIO_STATUS NTSTATUS
#define NETIOAPI_API_ NTAPI
#else
#define NETIO_STATUS DWORD
#define NETIOAPI_API_ WINAPI
#endif
#define NETIOAPI_API NETIO_STATUS NETIOAPI_API_

typedef enum _MIB_IF_TABLE_LEVEL
{
    MibIfTableNormal,
    MibIfTableRaw,
    MibIfTableNormalWithoutStatistics,
} MIB_IF_TABLE_LEVEL, *PMIB_IF_TABLE_LEVEL;

typedef enum _MIB_NOTIFICATION_TYPE
{
    MibParameterNotification,
    MibAddInstance,
    MibDeleteInstance,
    MibInitialNotification,
} MIB_NOTIFICATION_TYPE, *PMIB_NOTIFICATION_TYPE;

typedef struct _MIB_IF_ROW2
{
    NET_LUID InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    GUID InterfaceGuid;
    WCHAR Alias[IF_MAX_STRING_SIZE + 1];
    WCHAR Description[IF_MAX_STRING_SIZE + 1];
    ULONG PhysicalAddressLength;
    UCHAR PhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
    UCHAR PermanentPhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
    ULONG Mtu;
    IFTYPE Type;
    TUNNEL_TYPE TunnelType;
    NDIS_MEDIUM MediaType;
    NDIS_PHYSICAL_MEDIUM PhysicalMediumType;
    NET_IF_ACCESS_TYPE AccessType;
    NET_IF_DIRECTION_TYPE DirectionType;
    struct
    {
        BOOLEAN HardwareInterface : 1;
        BOOLEAN FilterInterface : 1;
        BOOLEAN ConnectorPresent : 1;
        BOOLEAN NotAuthenticated : 1;
        BOOLEAN NotMediaConnected : 1;
        BOOLEAN Paused : 1;
        BOOLEAN LowPower : 1;
        BOOLEAN EndPointInterface : 1;
    } InterfaceAndOperStatusFlags;
    IF_OPER_STATUS OperStatus;
    NET_IF_ADMIN_STATUS AdminStatus;
    NET_IF_MEDIA_CONNECT_STATE MediaConnectState;
    NET_IF_NETWORK_GUID NetworkGuid;
    NET_IF_CONNECTION_TYPE ConnectionType;
    ULONG64 TransmitLinkSpeed;
    ULONG64 ReceiveLinkSpeed;
    ULONG64 InOctets;
    ULONG64 InUcastPkts;
    ULONG64 InNUcastPkts;
    ULONG64 InDiscards;
    ULONG64 InErrors;
    ULONG64 InUnknownProtos;
    ULONG64 InUcastOctets;
    ULONG64 InMulticastOctets;
    ULONG64 InBroadcastOctets;
    ULONG64 OutOctets;
    ULONG64 OutUcastPkts;
    ULONG64 OutNUcastPkts;
    ULONG64 OutDiscards;
    ULONG64 OutErrors;
    ULONG64 OutUcastOctets;
    ULONG64 OutMulticastOctets;
    ULONG64 OutBroadcastOctets;
    ULONG64 OutQLen;
} MIB_IF_ROW2, *PMIB_IF_ROW2;

typedef struct _MIB_IF_TABLE2
{
    ULONG NumEntries;
    MIB_IF_ROW2 Table[1];
} MIB_IF_TABLE2, *PMIB_IF_TABLE2;

typedef struct _MIB_IPINTERFACE_ROW
{
    ADDRESS_FAMILY Family;
    NET_LUID InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    ULONG MaxReassemblySize;
    ULONG64 InterfaceIdentifier;
    ULONG MinRouterAdvertisementInterval;
    ULONG MaxRouterAdvertisementInterval;
    BOOLEAN AdvertisingEnabled;
    BOOLEAN ForwardingEnabled;
    BOOLEAN WeakHostSend;
    BOOLEAN WeakHostReceive;
    BOOLEAN UseAutomaticMetric;
    BOOLEAN UseNeighborUnreachabilityDetection;
    BOOLEAN ManagedAddressConfigurationSupported;
    BOOLEAN OtherStatefulConfigurationSupported;
    BOOLEAN AdvertiseDefaultRoute;
    NL_ROUTER_DISCOVERY_BEHAVIOR RouterDiscoveryBehavior;
    ULONG DadTransmits;
    ULONG BaseReachableTime;
    ULONG RetransmitTime;
    ULONG PathMtuDiscoveryTimeout;
    NL_LINK_LOCAL_ADDRESS_BEHAVIOR LinkLocalAddressBehavior;
    ULONG LinkLocalAddressTimeout;
    ULONG ZoneIndices[ScopeLevelCount];
    ULONG SitePrefixLength;
    ULONG Metric;
    ULONG NlMtu;
    BOOLEAN Connected;
    BOOLEAN SupportsWakeUpPatterns;
    BOOLEAN SupportsNeighborDiscovery;
    BOOLEAN SupportsRouterDiscovery;
    ULONG ReachableTime;
    NL_INTERFACE_OFFLOAD_ROD TransmitOffload;
    NL_INTERFACE_OFFLOAD_ROD ReceiveOffload;
    BOOLEAN DisableDefaultRoutes;
} MIB_IPINTERFACE_ROW, *PMIB_IPINTERFACE_ROW;

typedef struct _MIB_IPINTERFACE_TABLE
{
    ULONG NumEntries;
    MIB_IPINTERFACE_ROW Table[ANY_SIZE];
} MIB_IPINTERFACE_TABLE, *PMIB_IPINTERFACE_TABLE;

typedef struct _MIB_UNICASTIPADDRESS_ROW
{
    SOCKADDR_INET       Address;
    NET_LUID            InterfaceLuid;
    NET_IFINDEX         InterfaceIndex;
    NL_PREFIX_ORIGIN    PrefixOrigin;
    NL_SUFFIX_ORIGIN    SuffixOrigin;
    ULONG               ValidLifetime;
    ULONG               PreferredLifetime;
    UINT8               OnLinkPrefixLength;
    BOOLEAN             SkipAsSource;
    NL_DAD_STATE        DadState;
    SCOPE_ID            ScopeId;
    LARGE_INTEGER       CreationTimeStamp;
} MIB_UNICASTIPADDRESS_ROW, *PMIB_UNICASTIPADDRESS_ROW;

typedef struct _MIB_UNICASTIPADDRESS_TABLE
{
    ULONG NumEntries;
    MIB_UNICASTIPADDRESS_ROW Table[1];
} MIB_UNICASTIPADDRESS_TABLE, *PMIB_UNICASTIPADDRESS_TABLE;

typedef struct _MIB_ANYCASTIPADDRESS_ROW
{
    SOCKADDR_INET Address;
    NET_LUID      InterfaceLuid;
    NET_IFINDEX   InterfaceIndex;
    SCOPE_ID      ScopeId;
} MIB_ANYCASTIPADDRESS_ROW, *PMIB_ANYCASTIPADDRESS_ROW;

typedef struct _MIB_ANYCASTIPADDRESS_TABLE
{
    ULONG                    NumEntries;
    MIB_ANYCASTIPADDRESS_ROW Table[ANY_SIZE];
} MIB_ANYCASTIPADDRESS_TABLE, *PMIB_ANYCASTIPADDRESS_TABLE;

typedef struct _IP_ADDRESS_PREFIX
{
    SOCKADDR_INET Prefix;
    UINT8         PrefixLength;
} IP_ADDRESS_PREFIX, *PIP_ADDRESS_PREFIX;

typedef struct _MIB_IPFORWARD_ROW2
{
    NET_LUID          InterfaceLuid;
    NET_IFINDEX       InterfaceIndex;
    IP_ADDRESS_PREFIX DestinationPrefix;
    SOCKADDR_INET     NextHop;
    UCHAR             SitePrefixLength;
    ULONG             ValidLifetime;
    ULONG             PreferredLifetime;
    ULONG             Metric;
    NL_ROUTE_PROTOCOL Protocol;
    BOOLEAN           Loopback;
    BOOLEAN           AutoconfigureAddress;
    BOOLEAN           Publish;
    BOOLEAN           Immortal;
    ULONG             Age;
    NL_ROUTE_ORIGIN   Origin;
} MIB_IPFORWARD_ROW2, *PMIB_IPFORWARD_ROW2;

typedef struct _MIB_IPFORWARD_TABLE2
{
    ULONG              NumEntries;
    MIB_IPFORWARD_ROW2 Table[ANY_SIZE];
} MIB_IPFORWARD_TABLE2, *PMIB_IPFORWARD_TABLE2;

typedef struct _MIB_IPNET_ROW2
{
    SOCKADDR_INET Address;
    NET_IFINDEX InterfaceIndex;
    NET_LUID InterfaceLuid;
    UCHAR PhysicalAddress[IF_MAX_PHYS_ADDRESS_LENGTH];
    ULONG PhysicalAddressLength;
    NL_NEIGHBOR_STATE State;

    union
    {
        struct
        {
            BOOLEAN IsRouter : 1;
            BOOLEAN IsUnreachable : 1;
        } DUMMYSTRUCTNAME;
        UCHAR Flags;
    } DUMMYUNIONNAME;

    union
    {
        ULONG LastReachable;
        ULONG LastUnreachable;
    } ReachabilityTime;
} MIB_IPNET_ROW2, *PMIB_IPNET_ROW2;

typedef struct _MIB_IPNET_TABLE2
{
    ULONG NumEntries;
    MIB_IPNET_ROW2 Table[ANY_SIZE];
} MIB_IPNET_TABLE2, *PMIB_IPNET_TABLE2;

typedef struct _MIB_IPPATH_ROW
{
    SOCKADDR_INET Source;
    SOCKADDR_INET Destination;
    NET_LUID InterfaceLuid;
    NET_IFINDEX InterfaceIndex;
    SOCKADDR_INET CurrentNextHop;
    ULONG PathMtu;
    ULONG RttMean;
    ULONG RttDeviation;
    union
    {
        ULONG LastReachable;
        ULONG LastUnreachable;
    } DUMMYUNIONNAME;
    BOOLEAN IsReachable;
    ULONG64 LinkTransmitSpeed;
    ULONG64 LinkReceiveSpeed;
} MIB_IPPATH_ROW, *PMIB_IPPATH_ROW;

typedef struct _MIB_IPPATH_TABLE
{
    ULONG NumEntries;
    MIB_IPPATH_ROW Table[ANY_SIZE];
} MIB_IPPATH_TABLE, *PMIB_IPPATH_TABLE;

typedef VOID (NETIOAPI_API_ *PIPINTERFACE_CHANGE_CALLBACK)(PVOID, PMIB_IPINTERFACE_ROW,
                                                    MIB_NOTIFICATION_TYPE);
typedef VOID (NETIOAPI_API_ *PUNICAST_IPADDRESS_CHANGE_CALLBACK)(PVOID, PMIB_UNICASTIPADDRESS_ROW,
                                                          MIB_NOTIFICATION_TYPE);

typedef VOID (NETIOAPI_API_ *PIPFORWARD_CHANGE_CALLBACK)(VOID*,MIB_IPFORWARD_ROW2*,MIB_NOTIFICATION_TYPE);

IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ CancelMibChangeNotify2(HANDLE);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ ConvertInterfaceAliasToLuid(const WCHAR*,NET_LUID*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ ConvertInterfaceGuidToLuid(const GUID*,NET_LUID*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ ConvertInterfaceIndexToLuid(NET_IFINDEX,NET_LUID*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ ConvertInterfaceLuidToAlias(const NET_LUID*,WCHAR*,SIZE_T);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ ConvertInterfaceLuidToGuid(const NET_LUID*,GUID*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ ConvertInterfaceLuidToIndex(const NET_LUID*,NET_IFINDEX*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ ConvertInterfaceLuidToNameA(const NET_LUID*,char*,SIZE_T);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ ConvertInterfaceLuidToNameW(const NET_LUID*,WCHAR*,SIZE_T);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ ConvertInterfaceNameToLuidA(const char*,NET_LUID*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ ConvertInterfaceNameToLuidW(const WCHAR*,NET_LUID*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ ConvertLengthToIpv4Mask(ULONG,ULONG*);
IPHLPAPI_DLL_LINKAGE void NETIOAPI_API_ FreeMibTable(void*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetAnycastIpAddressTable(ADDRESS_FAMILY,MIB_ANYCASTIPADDRESS_TABLE**);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetBestRoute2(NET_LUID *luid, NET_IFINDEX index, const SOCKADDR_INET *source,
                                                const SOCKADDR_INET *destination, ULONG options,
                                                PMIB_IPFORWARD_ROW2 bestroute, SOCKADDR_INET *bestaddress);
IPHLPAPI_DLL_LINKAGE NET_IF_COMPARTMENT_ID NETIOAPI_API_ GetCurrentThreadCompartmentId(void);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIfEntry2(MIB_IF_ROW2*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIfEntry2Ex(MIB_IF_TABLE_LEVEL,MIB_IF_ROW2*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIfTable2(MIB_IF_TABLE2**);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIfTable2Ex(MIB_IF_TABLE_LEVEL,MIB_IF_TABLE2**);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIpForwardEntry2(MIB_IPFORWARD_ROW2*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIpForwardTable2(ADDRESS_FAMILY,MIB_IPFORWARD_TABLE2**);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIpInterfaceEntry(MIB_IPINTERFACE_ROW*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIpInterfaceTable(ADDRESS_FAMILY,MIB_IPINTERFACE_TABLE**);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIpNetEntry2(MIB_IPNET_ROW2*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIpNetTable2(ADDRESS_FAMILY,MIB_IPNET_TABLE2**);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetUnicastIpAddressEntry(MIB_UNICASTIPADDRESS_ROW*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetUnicastIpAddressTable(ADDRESS_FAMILY,MIB_UNICASTIPADDRESS_TABLE**);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ NotifyIpInterfaceChange(ADDRESS_FAMILY,PIPINTERFACE_CHANGE_CALLBACK,PVOID,BOOLEAN,PHANDLE);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ NotifyRouteChange2(ADDRESS_FAMILY,PIPFORWARD_CHANGE_CALLBACK,PVOID,BOOLEAN,PHANDLE);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ NotifyUnicastIpAddressChange(ADDRESS_FAMILY,PUNICAST_IPADDRESS_CHANGE_CALLBACK,PVOID,BOOLEAN,PHANDLE);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ SetCurrentThreadCompartmentId(NET_IF_COMPARTMENT_ID);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ CreateIpForwardEntry2(const MIB_IPFORWARD_ROW2*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ CreateIpNetEntry2(const MIB_IPNET_ROW2*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ CreateUnicastIpAddressEntry(const MIB_UNICASTIPADDRESS_ROW*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ DeleteIpForwardEntry2(const MIB_IPFORWARD_ROW2*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ DeleteIpNetEntry2(const MIB_IPNET_ROW2*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ FlushIpNetTable2(ADDRESS_FAMILY,NET_IFINDEX);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ FlushIpPathTable(ADDRESS_FAMILY);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIpPathEntry(PMIB_IPPATH_ROW);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ GetIpPathTable(ADDRESS_FAMILY,PMIB_IPPATH_TABLE*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ DeleteUnicastIpAddressEntry(const MIB_UNICASTIPADDRESS_ROW*);
IPHLPAPI_DLL_LINKAGE void NETIOAPI_API_ InitializeIpForwardEntry(MIB_IPFORWARD_ROW2*);
IPHLPAPI_DLL_LINKAGE void NETIOAPI_API_ InitializeIpInterfaceEntry(MIB_IPINTERFACE_ROW*);
IPHLPAPI_DLL_LINKAGE void NETIOAPI_API_ InitializeUnicastIpAddressEntry(MIB_UNICASTIPADDRESS_ROW*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ SetIpInterfaceEntry(MIB_IPINTERFACE_ROW*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ SetIpForwardEntry2(const MIB_IPFORWARD_ROW2*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ SetIpNetEntry2(MIB_IPNET_ROW2*);
IPHLPAPI_DLL_LINKAGE NETIO_STATUS NETIOAPI_API_ SetUnicastIpAddressEntry(const MIB_UNICASTIPADDRESS_ROW*);
IPHLPAPI_DLL_LINKAGE PCHAR NETIOAPI_API_ if_indextoname(NET_IFINDEX,PCHAR);
IPHLPAPI_DLL_LINKAGE NET_IFINDEX NETIOAPI_API_ if_nametoindex(PCSTR);

#ifndef DNS_DDR_ADAPTER_ENABLE_DOH
#define DNS_DDR_ADAPTER_ENABLE_DOH 0x0001
#endif
#ifndef DNS_DDR_ADAPTER_ENABLE_UDP_FALLBACK
#define DNS_DDR_ADAPTER_ENABLE_UDP_FALLBACK 0x0002
#endif
#ifndef DNS_DDR_ADAPTER_MAKE_DDR_NON_BLOCKING
#define DNS_DDR_ADAPTER_MAKE_DDR_NON_BLOCKING 0x0004
#endif
#ifndef DNS_DOH_AUTO_UPGRADE_SERVER
#define DNS_DOH_AUTO_UPGRADE_SERVER 0x0008
#endif
#ifndef DNS_DOH_POLICY_AUTO
#define DNS_DOH_POLICY_AUTO 0x0010
#endif
#ifndef DNS_DOH_POLICY_BLOCK
#define DNS_DOH_POLICY_BLOCK 0x0200
#endif
#ifndef DNS_DOH_POLICY_DISABLE
#define DNS_DOH_POLICY_DISABLE 0x0008
#endif
#ifndef DNS_DOH_POLICY_NOT_CONFIGURED
#define DNS_DOH_POLICY_NOT_CONFIGURED 0x0004
#endif
#ifndef DNS_DOH_POLICY_REQUIRED
#define DNS_DOH_POLICY_REQUIRED 0x0020
#endif
#ifndef DNS_DOH_SERVER_SETTINGS_ENABLE
#define DNS_DOH_SERVER_SETTINGS_ENABLE 0x0002
#endif
#ifndef DNS_DOH_SERVER_SETTINGS_ENABLE_AUTO
#define DNS_DOH_SERVER_SETTINGS_ENABLE_AUTO 0x0001
#endif
#ifndef DNS_DOH_SERVER_SETTINGS_ENABLE_DDR
#define DNS_DOH_SERVER_SETTINGS_ENABLE_DDR 0x0010
#endif
#ifndef DNS_DOH_SERVER_SETTINGS_FALLBACK_TO_UDP
#define DNS_DOH_SERVER_SETTINGS_FALLBACK_TO_UDP 0x0004
#endif
#ifndef DNS_DOH_SERVER_SETTINGS_MAKE_DDR_NON_BLOCKING
#define DNS_DOH_SERVER_SETTINGS_MAKE_DDR_NON_BLOCKING 0x0020
#endif
#ifndef DNS_DOT_AUTO_UPGRADE_SERVER
#define DNS_DOT_AUTO_UPGRADE_SERVER 0x0004
#endif
#ifndef DNS_DOT_POLICY_BLOCK
#define DNS_DOT_POLICY_BLOCK 0x0100
#endif
#ifndef DNS_DOT_SERVER_SETTINGS_ENABLE
#define DNS_DOT_SERVER_SETTINGS_ENABLE 0x0001
#endif
#ifndef DNS_DOT_SERVER_SETTINGS_ENABLE_AUTO
#define DNS_DOT_SERVER_SETTINGS_ENABLE_AUTO 0x0008
#endif
#ifndef DNS_DOT_SERVER_SETTINGS_ENABLE_DDR
#define DNS_DOT_SERVER_SETTINGS_ENABLE_DDR 0x0010
#endif
#ifndef DNS_DOT_SERVER_SETTINGS_FALLBACK_TO_UDP
#define DNS_DOT_SERVER_SETTINGS_FALLBACK_TO_UDP 0x0002
#endif
#ifndef DNS_DOT_SERVER_SETTINGS_MAKE_DDR_NON_BLOCKING
#define DNS_DOT_SERVER_SETTINGS_MAKE_DDR_NON_BLOCKING 0x0020
#endif
#ifndef DNS_ENABLE_DDR
#define DNS_ENABLE_DDR 0x0040
#endif
#ifndef DNS_ENABLE_DNR
#define DNS_ENABLE_DNR 0x0400
#endif
#ifndef DNS_ENABLE_DOH
#define DNS_ENABLE_DOH 0x0001
#endif
#ifndef DNS_ENABLE_DOT
#define DNS_ENABLE_DOT 0x0080
#endif
#ifndef DNS_INTERFACE_SETTINGS_VERSION1
#define DNS_INTERFACE_SETTINGS_VERSION1 0x0001
#endif
#ifndef DNS_INTERFACE_SETTINGS_VERSION2
#define DNS_INTERFACE_SETTINGS_VERSION2 0x0002
#endif
#ifndef DNS_INTERFACE_SETTINGS_VERSION3
#define DNS_INTERFACE_SETTINGS_VERSION3 0x0003
#endif
#ifndef DNS_INTERFACE_SETTINGS_VERSION4
#define DNS_INTERFACE_SETTINGS_VERSION4 0x0004
#endif
#ifndef DNS_SERVER_PROPERTY_VERSION1
#define DNS_SERVER_PROPERTY_VERSION1 0x0001
#endif
#ifndef DNS_SETTINGS_ENABLE_LLMNR
#define DNS_SETTINGS_ENABLE_LLMNR 0x0080
#endif
#ifndef DNS_SETTINGS_QUERY_ADAPTER_NAME
#define DNS_SETTINGS_QUERY_ADAPTER_NAME 0x0100
#endif
#ifndef DNS_SETTINGS_VERSION1
#define DNS_SETTINGS_VERSION1 0x0001
#endif
#ifndef DNS_SETTINGS_VERSION2
#define DNS_SETTINGS_VERSION2 0x0002
#endif
#ifndef DNS_SETTING_DDR
#define DNS_SETTING_DDR 0x8000
#endif
#ifndef DNS_SETTING_DISABLE_UNCONSTRAINED_QUERIES
#define DNS_SETTING_DISABLE_UNCONSTRAINED_QUERIES 0x0400
#endif
#ifndef DNS_SETTING_DOH
#define DNS_SETTING_DOH 0x1000
#endif
#ifndef DNS_SETTING_DOH_PROFILE
#define DNS_SETTING_DOH_PROFILE 0x2000
#endif
#ifndef DNS_SETTING_DOMAIN
#define DNS_SETTING_DOMAIN 0x0020
#endif
#ifndef DNS_SETTING_DOT
#define DNS_SETTING_DOT 0x10000
#endif
#ifndef DNS_SETTING_DOT_PROFILE
#define DNS_SETTING_DOT_PROFILE 0x20000
#endif
#ifndef DNS_SETTING_ENCRYPTED_DNS_ADAPTER_FLAGS
#define DNS_SETTING_ENCRYPTED_DNS_ADAPTER_FLAGS 0x4000
#endif
#ifndef DNS_SETTING_HOSTNAME
#define DNS_SETTING_HOSTNAME 0x0040
#endif
#ifndef DNS_SETTING_IPV6
#define DNS_SETTING_IPV6 0x0001
#endif
#ifndef DNS_SETTING_NAMESERVER
#define DNS_SETTING_NAMESERVER 0x0002
#endif
#ifndef DNS_SETTING_PROFILE_NAMESERVER
#define DNS_SETTING_PROFILE_NAMESERVER 0x0200
#endif
#ifndef DNS_SETTING_REGISTER_ADAPTER_NAME
#define DNS_SETTING_REGISTER_ADAPTER_NAME 0x0010
#endif
#ifndef DNS_SETTING_REGISTRATION_ENABLED
#define DNS_SETTING_REGISTRATION_ENABLED 0x0008
#endif
#ifndef DNS_SETTING_SEARCHLIST
#define DNS_SETTING_SEARCHLIST 0x0004
#endif
#ifndef DNS_SETTING_SUPPLEMENTAL_SEARCH_LIST
#define DNS_SETTING_SUPPLEMENTAL_SEARCH_LIST 0x0800
#endif

typedef enum _DNS_SERVER_PROPERTY_TYPE {
    DnsServerInvalidProperty = 0,
    DnsServerDohProperty = 1,
    DnsServerDotProperty = 2
} DNS_SERVER_PROPERTY_TYPE;

typedef enum _MIB_IF_ENTRY_LEVEL {
    MibIfEntryNormal = 0,
    MibIfEntryNormalWithoutStatistics = 2
} MIB_IF_ENTRY_LEVEL, *PMIB_IF_ENTRY_LEVEL;

typedef enum _NET_FL_ISOLATION_MODE {
    NetFlIsolationModeNone = 0,
    NetFlIsolationModeVlan = 1,
    NetFlIsolationModeVsid = 2
} NET_FL_ISOLATION_MODE;

typedef enum _NET_FL_VIRTUAL_INTERFACE_ORIGIN {
    NetFlVirtualInterfaceOriginOid = 0,
    NetFlVirtualInterfaceOriginApi = 1,
    NetFlVirtualInterfaceOriginDefault = 2
} NET_FL_VIRTUAL_INTERFACE_ORIGIN;

typedef struct _DNS_DOH_SERVER_SETTINGS {
  PWSTR Template;
  ULONG64 Flags;
} DNS_DOH_SERVER_SETTINGS;

typedef struct _DNS_DOT_SERVER_SETTINGS {
  PWSTR Hostname;
  ULONG64 Flags;
  USHORT Port;
} DNS_DOT_SERVER_SETTINGS;

typedef struct _DNS_INTERFACE_SETTINGS {
  ULONG Version;
  ULONG64 Flags;
  PWSTR Domain;
  PWSTR NameServer;
  PWSTR SearchList;
  ULONG RegistrationEnabled;
  ULONG RegisterAdapterName;
  ULONG EnableLLMNR;
  ULONG QueryAdapterName;
  PWSTR ProfileNameServer;
} DNS_INTERFACE_SETTINGS;

typedef struct _DNS_INTERFACE_SETTINGS_EX {
  DNS_INTERFACE_SETTINGS SettingsV1;
  ULONG DisableUnconstrainedQueries;
  PWSTR SupplementalSearchList;
} DNS_INTERFACE_SETTINGS_EX;

typedef union _DNS_SERVER_PROPERTY_TYPES {
  DNS_DOH_SERVER_SETTINGS * DohSettings;
  DNS_DOT_SERVER_SETTINGS * DotSettings;
} DNS_SERVER_PROPERTY_TYPES;

typedef struct _DNS_SERVER_PROPERTY {
  ULONG Version;
  ULONG ServerIndex;
  DNS_SERVER_PROPERTY_TYPE Type;
  DNS_SERVER_PROPERTY_TYPES Property;
} DNS_SERVER_PROPERTY;

typedef struct _DNS_INTERFACE_SETTINGS3 {
  ULONG Version;
  ULONG64 Flags;
  PWSTR Domain;
  PWSTR NameServer;
  PWSTR SearchList;
  ULONG RegistrationEnabled;
  ULONG RegisterAdapterName;
  ULONG EnableLLMNR;
  ULONG QueryAdapterName;
  PWSTR ProfileNameServer;
  ULONG DisableUnconstrainedQueries;
  PWSTR SupplementalSearchList;
  ULONG cServerProperties;
  DNS_SERVER_PROPERTY * ServerProperties;
  ULONG cProfileServerProperties;
  DNS_SERVER_PROPERTY * ProfileServerProperties;
} DNS_INTERFACE_SETTINGS3;

typedef struct _DNS_INTERFACE_SETTINGS4 {
  ULONG Version;
  ULONG64 Flags;
  PWSTR Domain;
  PWSTR NameServer;
  PWSTR SearchList;
  ULONG RegistrationEnabled;
  ULONG RegisterAdapterName;
  ULONG EnableLLMNR;
  ULONG QueryAdapterName;
  PWSTR ProfileNameServer;
  ULONG DisableUnconstrainedQueries;
  PWSTR SupplementalSearchList;
  ULONG cServerProperties;
  DNS_SERVER_PROPERTY * ServerProperties;
  ULONG cProfileServerProperties;
  DNS_SERVER_PROPERTY * ProfileServerProperties;
  ULONG EncryptedDnsAdapterFlags;
} DNS_INTERFACE_SETTINGS4;

typedef struct _DNS_SETTINGS {
  ULONG Version;
  ULONG64 Flags;
  PWSTR Hostname;
  PWSTR Domain;
  PWSTR SearchList;
} DNS_SETTINGS;

typedef struct _DNS_SETTINGS2 {
  ULONG Version;
  ULONG64 Flags;
  PWSTR Hostname;
  PWSTR Domain;
  PWSTR SearchList;
  ULONG64 SettingFlags;
} DNS_SETTINGS2;

typedef struct _MIB_FL_VIRTUAL_INTERFACE_ROW {
  ADDRESS_FAMILY Family;
  IF_LUID IfLuid;
  ULONG VirtualIfId;
  GUID CompartmentGuid;
  NET_FL_ISOLATION_MODE IsolationMode;
  NET_FL_VIRTUAL_INTERFACE_ORIGIN Origin;
  IF_LUID VirtualIfLuid;
  IF_INDEX VirtualIfIndex;
  BOOLEAN AllowLocalNd;
  ULONG AttachedFlsnpiClients;
  ULONG FlsnpiClientConfigErrors;
  ULONG64 FlsnpiClientInjectErrors;
  ULONG64 FlsnpiClientCloneErrors;
  ULONG64 InFlsnpiIndicatedPackets;
  ULONG64 InFlsnpiClientReturnedPackets;
  ULONG64 InFlsnpiClientSilentlyDroppedPackets;
  ULONG64 InFlsnpiClientDroppedPackets;
  ULONG64 InFlsnpiClientInjectedPackets;
  ULONG64 InFlsnpiClientClonedPackets;
  ULONG64 OutFlsnpiIndicatedPackets;
  ULONG64 OutFlsnpiClientReturnedPackets;
  ULONG64 OutFlsnpiClientDroppedPackets;
  ULONG64 OutFlsnpiClientSilentlyDroppedPackets;
  ULONG64 OutFlsnpiClientInjectedPackets;
  ULONG64 OutFlsnpiClientClonedPackets;
  ULONG64 OutFlsnpiClientClonedPacketsForNbSplit;
} MIB_FL_VIRTUAL_INTERFACE_ROW, *PMIB_FL_VIRTUAL_INTERFACE_ROW;

typedef struct _MIB_FL_VIRTUAL_INTERFACE_TABLE {
  ULONG NumEntries;
  MIB_FL_VIRTUAL_INTERFACE_ROW Table[1];
} MIB_FL_VIRTUAL_INTERFACE_TABLE, *PMIB_FL_VIRTUAL_INTERFACE_TABLE;

typedef struct _MIB_IFSTACK_ROW {
  NET_IFINDEX HigherLayerInterfaceIndex;
  NET_IFINDEX LowerLayerInterfaceIndex;
} MIB_IFSTACK_ROW, *PMIB_IFSTACK_ROW;

typedef struct _MIB_IFSTACK_TABLE {
  ULONG NumEntries;
  MIB_IFSTACK_ROW Table[1];
} MIB_IFSTACK_TABLE, *PMIB_IFSTACK_TABLE;

typedef struct _MIB_INVERTEDIFSTACK_ROW {
  NET_IFINDEX LowerLayerInterfaceIndex;
  NET_IFINDEX HigherLayerInterfaceIndex;
} MIB_INVERTEDIFSTACK_ROW, *PMIB_INVERTEDIFSTACK_ROW;

typedef struct _MIB_INVERTEDIFSTACK_TABLE {
  ULONG NumEntries;
  MIB_INVERTEDIFSTACK_ROW Table[1];
} MIB_INVERTEDIFSTACK_TABLE, *PMIB_INVERTEDIFSTACK_TABLE;

typedef struct _MIB_MULTICASTIPADDRESS_ROW {
  SOCKADDR_INET Address;
  NET_IFINDEX InterfaceIndex;
  NET_LUID InterfaceLuid;
  SCOPE_ID ScopeId;
} MIB_MULTICASTIPADDRESS_ROW, *PMIB_MULTICASTIPADDRESS_ROW;

typedef struct _MIB_MULTICASTIPADDRESS_TABLE {
  ULONG NumEntries;
  MIB_MULTICASTIPADDRESS_ROW Table[1];
} MIB_MULTICASTIPADDRESS_TABLE, *PMIB_MULTICASTIPADDRESS_TABLE;

#define _NETIOAPI_SUCCESS_
#define DNS_DDR_ADAPTER_ENABLE 1
#define DNS_ENCRYPTION_POLICY_AUTO 0x10
#define DNS_ENCRYPTION_POLICY_DISABLE 8
#define DNS_ENCRYPTION_POLICY_NOT_CONFIGURED 4
#define DNS_ENCRYPTION_POLICY_REQUIRED 0x20
#define IF_NAMESIZE 0x100
#define NETIO_PULONG 0
#define NETIO_SUCCESSx) NT_SUCCESS(x)

struct _MIB_IP_NETWORK_CONNECTION_BANDWIDTH_ESTIMATES
{
    NL_BANDWIDTH_INFORMATION InboundBandwidthInformation;
    NL_BANDWIDTH_INFORMATION OutboundBandwidthInformation;
};

typedef struct _MIB_IP_NETWORK_CONNECTION_BANDWIDTH_ESTIMATES MIB_IP_NETWORK_CONNECTION_BANDWIDTH_ESTIMATES;

typedef struct _MIB_IP_NETWORK_CONNECTION_BANDWIDTH_ESTIMATES *PMIB_IP_NETWORK_CONNECTION_BANDWIDTH_ESTIMATES;

typedef void (*PNETWORK_CONNECTIVITY_HINT_CHANGE_CALLBACK)(PVOID, NL_NETWORK_CONNECTIVITY_HINT);

typedef void (*PSTABLE_UNICAST_IPADDRESS_TABLE_CALLBACK)(PVOID, PMIB_UNICASTIPADDRESS_TABLE);

typedef void (*PTEREDO_PORT_CHANGE_CALLBACK)(PVOID, USHORT, MIB_NOTIFICATION_TYPE);

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS WINAPI CreateSortedAddressPairs(const PSOCKADDR_IN6 SourceAddressList, ULONG SourceAddressCount, const PSOCKADDR_IN6 DestinationAddressList, ULONG DestinationAddressCount, ULONG AddressSortOptions, PSOCKADDR_IN6_PAIR *SortedAddressPairList, ULONG *SortedAddressPairCount);
NTSTATUS WINAPI SetInterfaceDnsSettings(GUID Interface, const DNS_INTERFACE_SETTINGS *Settings);

/* TODO: LiberNT does not implement the functions below yet */
NTSTATUS WINAPI ConvertCompartmentGuidToId(const GUID *CompartmentGuid, PNET_IF_COMPARTMENT_ID CompartmentId);
NTSTATUS WINAPI ConvertCompartmentIdToGuid(NET_IF_COMPARTMENT_ID CompartmentId, GUID *CompartmentGuid);
NTSTATUS WINAPI ConvertIpv4MaskToLength(ULONG Mask, PUINT8 MaskLength);
NTSTATUS WINAPI CreateAnycastIpAddressEntry(const MIB_ANYCASTIPADDRESS_ROW *Row);
NTSTATUS WINAPI CreateFlVirtualInterface(const MIB_FL_VIRTUAL_INTERFACE_ROW *Row);
NTSTATUS WINAPI DeleteAnycastIpAddressEntry(const MIB_ANYCASTIPADDRESS_ROW *Row);
NTSTATUS WINAPI DeleteFlVirtualInterface(const MIB_FL_VIRTUAL_INTERFACE_ROW *Row);
void FreeDnsSettings(DNS_SETTINGS *Settings);
void FreeInterfaceDnsSettings(DNS_INTERFACE_SETTINGS *Settings);
NTSTATUS WINAPI GetAnycastIpAddressEntry(PMIB_ANYCASTIPADDRESS_ROW Row);
void GetCurrentThreadCompartmentScope(PNET_IF_COMPARTMENT_SCOPE CompartmentScope, PNET_IF_COMPARTMENT_ID CompartmentId);
NET_IF_COMPARTMENT_ID GetDefaultCompartmentId(void);
NTSTATUS WINAPI GetDnsSettings(DNS_SETTINGS *Settings);
NTSTATUS WINAPI GetFlVirtualInterface(PMIB_FL_VIRTUAL_INTERFACE_ROW Row);
NTSTATUS WINAPI GetFlVirtualInterfaceTable(ADDRESS_FAMILY Family, PMIB_FL_VIRTUAL_INTERFACE_TABLE *Table);
NTSTATUS WINAPI GetIfStackTable(PMIB_IFSTACK_TABLE *Table);
NTSTATUS WINAPI GetInterfaceDnsSettings(GUID Interface, DNS_INTERFACE_SETTINGS *Settings);
NTSTATUS WINAPI GetInvertedIfStackTable(PMIB_INVERTEDIFSTACK_TABLE *Table);
NTSTATUS WINAPI GetIpNetworkConnectionBandwidthEstimates(NET_IFINDEX InterfaceIndex, ADDRESS_FAMILY AddressFamily, PMIB_IP_NETWORK_CONNECTION_BANDWIDTH_ESTIMATES BandwidthEstimates);
NET_IF_COMPARTMENT_ID GetJobCompartmentId(HANDLE JobHandle);
NTSTATUS WINAPI GetMulticastIpAddressEntry(PMIB_MULTICASTIPADDRESS_ROW Row);
NTSTATUS WINAPI GetMulticastIpAddressTable(ADDRESS_FAMILY Family, PMIB_MULTICASTIPADDRESS_TABLE *Table);
NTSTATUS WINAPI GetNetworkConnectivityHint(NL_NETWORK_CONNECTIVITY_HINT *ConnectivityHint);
NTSTATUS WINAPI GetNetworkConnectivityHintForInterface(NET_IFINDEX InterfaceIndex, NL_NETWORK_CONNECTIVITY_HINT *ConnectivityHint);
NTSTATUS WINAPI GetNetworkInformation(const NET_IF_NETWORK_GUID *NetworkGuid, PNET_IF_COMPARTMENT_ID CompartmentId, PULONG SiteId, PWCHAR NetworkName, ULONG Length);
NET_IF_COMPARTMENT_ID GetSessionCompartmentId(ULONG SessionId);
NTSTATUS WINAPI GetTeredoPort(USHORT *Port);
void InitializeFlVirtualInterfaceEntry(PMIB_FL_VIRTUAL_INTERFACE_ROW Row);
NTSTATUS WINAPI NotifyNetworkConnectivityHintChange(PNETWORK_CONNECTIVITY_HINT_CHANGE_CALLBACK Callback, PVOID CallerContext, BOOLEAN InitialNotification, PHANDLE NotificationHandle);
NTSTATUS WINAPI NotifyStableUnicastIpAddressTable(ADDRESS_FAMILY Family, PMIB_UNICASTIPADDRESS_TABLE *Table, PSTABLE_UNICAST_IPADDRESS_TABLE_CALLBACK CallerCallback, PVOID CallerContext, HANDLE *NotificationHandle);
NTSTATUS WINAPI NotifyTeredoPortChange(PTEREDO_PORT_CHANGE_CALLBACK Callback, PVOID CallerContext, BOOLEAN InitialNotification, HANDLE *NotificationHandle);
NTSTATUS WINAPI SetCurrentThreadCompartmentScope(NET_IF_COMPARTMENT_SCOPE CompartmentScope);
NTSTATUS WINAPI SetDnsSettings(const DNS_SETTINGS *Settings);
NTSTATUS WINAPI SetFlVirtualInterface(const MIB_FL_VIRTUAL_INTERFACE_ROW *Row);
NTSTATUS WINAPI SetJobCompartmentId(HANDLE JobHandle, NET_IF_COMPARTMENT_ID CompartmentId);
NTSTATUS WINAPI SetNetworkInformation(const NET_IF_NETWORK_GUID *NetworkGuid, NET_IF_COMPARTMENT_ID CompartmentId, const WCHAR *NetworkName);
NTSTATUS WINAPI SetSessionCompartmentId(ULONG SessionId, NET_IF_COMPARTMENT_ID CompartmentId);

#ifdef __cplusplus
}
#endif

#define NETIO_SUCCESSx) NT_SUCCESS(x)

#define NETIO_SUCCESSx) NT_SUCCESS(x)

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS WINAPI ResolveIpNetEntry2(PMIB_IPNET_ROW2 Row, const SOCKADDR_INET *SourceAddress);

#ifdef __cplusplus
}
#endif

#define NETIO_SUCCESSx) NT_SUCCESS(x)

#endif /* __WINE_NETIOAPI_H */
