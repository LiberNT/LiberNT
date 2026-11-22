/*
 * Copyright (C) 2006 Dmitry Timoshkov
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

#ifndef __WINE_MPRAPI_H
#define __WINE_MPRAPI_H

#include <lmcons.h>
#include <ras.h>
#include <in6addr.h>
#include <wincrypt.h>

#define MAX_DEVICE_NAME                 128
#define MAX_DEVICETYPE_NAME              16
#define MAX_INTERFACE_NAME_LEN          256
#define MAX_MEDIA_NAME                   16
#define MAX_PHONE_NUMBER_LEN            128
#define MAX_PORT_NAME                    16
#define MAX_TRANSPORT_NAME_LEN           40


#ifdef __cplusplus
extern "C" {
#endif

BOOL APIENTRY MprAdminIsServiceRunning(LPWSTR);

#ifdef __cplusplus
}
#endif

#ifndef ATADDRESSLEN
#define ATADDRESSLEN 32
#endif
#ifndef DO_NOT_ALLOW_NO_AUTH
#define DO_NOT_ALLOW_NO_AUTH 0
#endif
#ifndef IPADDRESSLEN
#define IPADDRESSLEN 15
#endif
#ifndef IPXADDRESSLEN
#define IPXADDRESSLEN 22
#endif
#ifndef MAXIPADRESSLEN
#define MAXIPADRESSLEN 64
#endif
#ifndef MPRAPI_ADMIN_DLL_VERSION_1
#define MPRAPI_ADMIN_DLL_VERSION_1 0x1
#endif
#ifndef MPRAPI_ADMIN_DLL_VERSION_2
#define MPRAPI_ADMIN_DLL_VERSION_2 0x2
#endif
#ifndef MPRAPI_IF_CUSTOM_CONFIG_FOR_IKEV2
#define MPRAPI_IF_CUSTOM_CONFIG_FOR_IKEV2 0x1
#endif
#ifndef MPRAPI_IKEV2_AUTH_USING_CERT
#define MPRAPI_IKEV2_AUTH_USING_CERT 1
#endif
#ifndef MPRAPI_IKEV2_AUTH_USING_EAP
#define MPRAPI_IKEV2_AUTH_USING_EAP 2
#endif
#ifndef MPRAPI_IKEV2_PROJECTION_INFO_TYPE
#define MPRAPI_IKEV2_PROJECTION_INFO_TYPE 2
#endif
#ifndef MPRAPI_L2TP_SET_TUNNEL_CONFIG_PARAMS
#define MPRAPI_L2TP_SET_TUNNEL_CONFIG_PARAMS 0x1
#endif
#ifndef MPRAPI_MPR_IF_CUSTOM_CONFIG_OBJECT_REVISION_1
#define MPRAPI_MPR_IF_CUSTOM_CONFIG_OBJECT_REVISION_1 0x1
#endif
#ifndef MPRAPI_MPR_IF_CUSTOM_CONFIG_OBJECT_REVISION_2
#define MPRAPI_MPR_IF_CUSTOM_CONFIG_OBJECT_REVISION_2 0x2
#endif
#ifndef MPRAPI_MPR_IF_CUSTOM_CONFIG_OBJECT_REVISION_3
#define MPRAPI_MPR_IF_CUSTOM_CONFIG_OBJECT_REVISION_3 0x3
#endif
#ifndef MPRAPI_MPR_SERVER_OBJECT_REVISION_2
#define MPRAPI_MPR_SERVER_OBJECT_REVISION_2 0x2
#endif
#ifndef MPRAPI_MPR_SERVER_OBJECT_REVISION_3
#define MPRAPI_MPR_SERVER_OBJECT_REVISION_3 0x3
#endif
#ifndef MPRAPI_MPR_SERVER_OBJECT_REVISION_4
#define MPRAPI_MPR_SERVER_OBJECT_REVISION_4 0x4
#endif
#ifndef MPRAPI_MPR_SERVER_OBJECT_REVISION_5
#define MPRAPI_MPR_SERVER_OBJECT_REVISION_5 0x5
#endif
#ifndef MPRAPI_MPR_SERVER_SET_CONFIG_OBJECT_REVISION_2
#define MPRAPI_MPR_SERVER_SET_CONFIG_OBJECT_REVISION_2 0x2
#endif
#ifndef MPRAPI_MPR_SERVER_SET_CONFIG_OBJECT_REVISION_3
#define MPRAPI_MPR_SERVER_SET_CONFIG_OBJECT_REVISION_3 0x3
#endif
#ifndef MPRAPI_MPR_SERVER_SET_CONFIG_OBJECT_REVISION_4
#define MPRAPI_MPR_SERVER_SET_CONFIG_OBJECT_REVISION_4 0x4
#endif
#ifndef MPRAPI_MPR_SERVER_SET_CONFIG_OBJECT_REVISION_5
#define MPRAPI_MPR_SERVER_SET_CONFIG_OBJECT_REVISION_5 0x5
#endif
#ifndef MPRAPI_PPP_PROJECTION_INFO_TYPE
#define MPRAPI_PPP_PROJECTION_INFO_TYPE 1
#endif
#ifndef MPRAPI_RAS_CONNECTION_OBJECT_REVISION_1
#define MPRAPI_RAS_CONNECTION_OBJECT_REVISION_1 0x1
#endif
#ifndef MPRAPI_RAS_UPDATE_CONNECTION_OBJECT_REVISION_1
#define MPRAPI_RAS_UPDATE_CONNECTION_OBJECT_REVISION_1 0x1
#endif
#ifndef MPRAPI_SET_CONFIG_PROTOCOL_FOR_GRE
#define MPRAPI_SET_CONFIG_PROTOCOL_FOR_GRE 0x10
#endif
#ifndef MPRAPI_SET_CONFIG_PROTOCOL_FOR_IKEV2
#define MPRAPI_SET_CONFIG_PROTOCOL_FOR_IKEV2 0x8
#endif
#ifndef MPRAPI_SET_CONFIG_PROTOCOL_FOR_L2TP
#define MPRAPI_SET_CONFIG_PROTOCOL_FOR_L2TP 0x2
#endif
#ifndef MPRAPI_SET_CONFIG_PROTOCOL_FOR_PPTP
#define MPRAPI_SET_CONFIG_PROTOCOL_FOR_PPTP 0x1
#endif
#ifndef MPRAPI_SET_CONFIG_PROTOCOL_FOR_SSTP
#define MPRAPI_SET_CONFIG_PROTOCOL_FOR_SSTP 0x4
#endif
#ifndef MPRIO_IpSecPreSharedKey
#define MPRIO_IpSecPreSharedKey 0x80000000
#endif
#ifndef MPRIO_RequireMachineCertificates
#define MPRIO_RequireMachineCertificates 0x01000000
#endif
#ifndef MPRIO_UsePreSharedKeyForIkev2Initiator
#define MPRIO_UsePreSharedKeyForIkev2Initiator 0x02000000
#endif
#ifndef MPRIO_UsePreSharedKeyForIkev2Responder
#define MPRIO_UsePreSharedKeyForIkev2Responder 0x04000000
#endif
#ifndef MPR_ENABLE_RAS_ON_DEVICE
#define MPR_ENABLE_RAS_ON_DEVICE 0x00000001
#endif
#ifndef MPR_ENABLE_ROUTING_ON_DEVICE
#define MPR_ENABLE_ROUTING_ON_DEVICE 0x00000002
#endif
#ifndef MPR_INTERFACE_ADMIN_DISABLED
#define MPR_INTERFACE_ADMIN_DISABLED 0x00000002
#endif
#ifndef MPR_INTERFACE_CONNECTION_FAILURE
#define MPR_INTERFACE_CONNECTION_FAILURE 0x00000004
#endif
#ifndef MPR_INTERFACE_DIALOUT_HOURS_RESTRICTION
#define MPR_INTERFACE_DIALOUT_HOURS_RESTRICTION 0x00000010
#endif
#ifndef MPR_INTERFACE_NO_DEVICE
#define MPR_INTERFACE_NO_DEVICE 0x00000040
#endif
#ifndef MPR_INTERFACE_NO_MEDIA_SENSE
#define MPR_INTERFACE_NO_MEDIA_SENSE 0x00000020
#endif
#ifndef MPR_INTERFACE_OUT_OF_RESOURCES
#define MPR_INTERFACE_OUT_OF_RESOURCES 0x00000001
#endif
#ifndef MPR_INTERFACE_SERVICE_PAUSED
#define MPR_INTERFACE_SERVICE_PAUSED 0x00000008
#endif
#ifndef PID_ATALK
#define PID_ATALK 0x00000029
#endif
#ifndef PID_IP
#define PID_IP 0x00000021
#endif
#ifndef PID_IPV6
#define PID_IPV6 0x00000057
#endif
#ifndef PID_IPX
#define PID_IPX 0x0000002B
#endif
#ifndef PID_NBF
#define PID_NBF 0x0000003F
#endif
#ifndef PPP_CCP_COMPRESSION
#define PPP_CCP_COMPRESSION 0x00000001
#endif
#ifndef PPP_CCP_ENCRYPTION128BIT
#define PPP_CCP_ENCRYPTION128BIT 0x00000040
#endif
#ifndef PPP_CCP_ENCRYPTION40BIT
#define PPP_CCP_ENCRYPTION40BIT 0x00000020
#endif
#ifndef PPP_CCP_ENCRYPTION40BITOLD
#define PPP_CCP_ENCRYPTION40BITOLD 0x00000010
#endif
#ifndef PPP_CCP_ENCRYPTION56BIT
#define PPP_CCP_ENCRYPTION56BIT 0x00000080
#endif
#ifndef PPP_CCP_HISTORYLESS
#define PPP_CCP_HISTORYLESS 0x01000000
#endif
#ifndef PPP_IPCP_VJ
#define PPP_IPCP_VJ 0x00000001
#endif
#ifndef PPP_LCP_3_DES
#define PPP_LCP_3_DES 0x00000020
#endif
#ifndef PPP_LCP_ACFC
#define PPP_LCP_ACFC 0x00000004
#endif
#ifndef PPP_LCP_AES_128
#define PPP_LCP_AES_128 0x00000040
#endif
#ifndef PPP_LCP_AES_192
#define PPP_LCP_AES_192 0x00000100
#endif
#ifndef PPP_LCP_AES_256
#define PPP_LCP_AES_256 0x00000080
#endif
#ifndef PPP_LCP_CHAP
#define PPP_LCP_CHAP 0xC223
#endif
#ifndef PPP_LCP_CHAP_MD5
#define PPP_LCP_CHAP_MD5 0x05
#endif
#ifndef PPP_LCP_CHAP_MS
#define PPP_LCP_CHAP_MS 0x80
#endif
#ifndef PPP_LCP_CHAP_MSV2
#define PPP_LCP_CHAP_MSV2 0x81
#endif
#ifndef PPP_LCP_DES_56
#define PPP_LCP_DES_56 0x00000010
#endif
#ifndef PPP_LCP_EAP
#define PPP_LCP_EAP 0xC227
#endif
#ifndef PPP_LCP_GCM_AES_128
#define PPP_LCP_GCM_AES_128 0x00000200
#endif
#ifndef PPP_LCP_GCM_AES_192
#define PPP_LCP_GCM_AES_192 0x00000400
#endif
#ifndef PPP_LCP_GCM_AES_256
#define PPP_LCP_GCM_AES_256 0x00000800
#endif
#ifndef PPP_LCP_MULTILINK_FRAMING
#define PPP_LCP_MULTILINK_FRAMING 0x00000001
#endif
#ifndef PPP_LCP_PAP
#define PPP_LCP_PAP 0xC023
#endif
#ifndef PPP_LCP_PFC
#define PPP_LCP_PFC 0x00000002
#endif
#ifndef PPP_LCP_SPAP
#define PPP_LCP_SPAP 0xC027
#endif
#ifndef PPP_LCP_SSHF
#define PPP_LCP_SSHF 0x00000008
#endif
#ifndef RASPRIV2_DialinPolicy
#define RASPRIV2_DialinPolicy 0x1
#endif
#ifndef RAS_FLAGS_DORMANT
#define RAS_FLAGS_DORMANT 0x00000020
#endif
#ifndef RAS_FLAGS_MESSENGER_PRESENT
#define RAS_FLAGS_MESSENGER_PRESENT 0x00000002
#endif
#ifndef RAS_FLAGS_PPP_CONNECTION
#define RAS_FLAGS_PPP_CONNECTION 0x00000001
#endif
#ifndef RAS_FLAGS_QUARANTINE_PRESENT
#define RAS_FLAGS_QUARANTINE_PRESENT 0x00000008
#endif

typedef enum _MPR_VPN_TS_TYPE {
    MPR_VPN_TS_IPv4_ADDR_RANGE = 7,
    MPR_VPN_TS_IPv6_ADDR_RANGE = 8
} MPR_VPN_TS_TYPE;

typedef enum _MPRAPI_OBJECT_TYPE {
    MPRAPI_OBJECT_TYPE_RAS_CONNECTION_OBJECT = 1,
    MPRAPI_OBJECT_TYPE_MPR_SERVER_OBJECT = 2,
    MPRAPI_OBJECT_TYPE_MPR_SERVER_SET_CONFIG_OBJECT = 3,
    MPRAPI_OBJECT_TYPE_AUTH_VALIDATION_OBJECT = 4,
    MPRAPI_OBJECT_TYPE_UPDATE_CONNECTION_OBJECT = 5,
    MPRAPI_OBJECT_TYPE_IF_CUSTOM_CONFIG_OBJECT = 6
} MPRAPI_OBJECT_TYPE, *PMPRAPI_OBJECT_TYPE;

typedef enum _RAS_HARDWARE_CONDITION {
    RAS_HARDWARE_OPERATIONAL = 0,
    RAS_HARDWARE_FAILURE = 1
} RAS_HARDWARE_CONDITION;

typedef enum _RAS_QUARANTINE_STATE {
    RAS_QUAR_STATE_NORMAL = 0,
    RAS_QUAR_STATE_QUARANTINE = 1,
    RAS_QUAR_STATE_PROBATION = 2,
    RAS_QUAR_STATE_NOT_CAPABLE = 3
} RAS_QUARANTINE_STATE;

typedef enum _ROUTER_CONNECTION_STATE {
    ROUTER_IF_STATE_UNREACHABLE = 0,
    ROUTER_IF_STATE_DISCONNECTED = 1,
    ROUTER_IF_STATE_CONNECTING = 2,
    ROUTER_IF_STATE_CONNECTED = 3
} ROUTER_CONNECTION_STATE;

typedef enum _ROUTER_INTERFACE_TYPE {
    ROUTER_IF_TYPE_CLIENT = 0,
    ROUTER_IF_TYPE_HOME_ROUTER = 1,
    ROUTER_IF_TYPE_FULL_ROUTER = 2,
    ROUTER_IF_TYPE_DEDICATED = 3,
    ROUTER_IF_TYPE_INTERNAL = 4,
    ROUTER_IF_TYPE_LOOPBACK = 5,
    ROUTER_IF_TYPE_TUNNEL1 = 6,
    ROUTER_IF_TYPE_DIALOUT = 7,
    ROUTER_IF_TYPE_MAX = 8
} ROUTER_INTERFACE_TYPE;

typedef HANDLE MIB_SERVER_HANDLE;
typedef HANDLE MPR_SERVER_HANDLE;
typedef HANDLE RAS_SERVER_HANDLE;

typedef struct _GRE_CONFIG_PARAMS0 {
  DWORD dwNumPorts;
  DWORD dwPortFlags;
} GRE_CONFIG_PARAMS0, *PGRE_CONFIG_PARAMS0;

typedef struct _IKEV2_PROJECTION_INFO {
  DWORD dwIPv4NegotiationError;
  WCHAR wszAddress[16];
  WCHAR wszRemoteAddress[16];
  ULONG64 IPv4SubInterfaceIndex;
  DWORD dwIPv6NegotiationError;
  BYTE bInterfaceIdentifier[8];
  BYTE bRemoteInterfaceIdentifier[8];
  BYTE bPrefix[8];
  DWORD dwPrefixLength;
  ULONG64 IPv6SubInterfaceIndex;
  DWORD dwOptions;
  DWORD dwAuthenticationProtocol;
  DWORD dwEapTypeId;
  DWORD dwCompressionAlgorithm;
  DWORD dwEncryptionMethod;
} IKEV2_PROJECTION_INFO, *PIKEV2_PROJECTION_INFO;

typedef struct _IKEV2_PROJECTION_INFO2 {
  DWORD dwIPv4NegotiationError;
  WCHAR wszAddress[16];
  WCHAR wszRemoteAddress[16];
  ULONG64 IPv4SubInterfaceIndex;
  DWORD dwIPv6NegotiationError;
  BYTE bInterfaceIdentifier[8];
  BYTE bRemoteInterfaceIdentifier[8];
  BYTE bPrefix[8];
  DWORD dwPrefixLength;
  ULONG64 IPv6SubInterfaceIndex;
  DWORD dwOptions;
  DWORD dwAuthenticationProtocol;
  DWORD dwEapTypeId;
  DWORD dwEmbeddedEAPTypeId;
  DWORD dwCompressionAlgorithm;
  DWORD dwEncryptionMethod;
} IKEV2_PROJECTION_INFO2, *PIKEV2_PROJECTION_INFO2;

typedef struct _L2TP_CONFIG_PARAMS0 {
  DWORD dwNumPorts;
  DWORD dwPortFlags;
} L2TP_CONFIG_PARAMS0, *PL2TP_CONFIG_PARAMS0;

typedef struct _MPR_CERT_EKU {
  DWORD dwSize;
  BOOL IsEKUOID;
  WCHAR * pwszEKU;
} MPR_CERT_EKU, *PMPR_CERT_EKU;

typedef struct _MPR_CREDENTIALSEX_0 {
  DWORD dwSize;
  LPBYTE lpbCredentialsInfo;
} MPR_CREDENTIALSEX_0, *PMPR_CREDENTIALSEX_0;

typedef struct _MPR_CREDENTIALSEX_1 {
  DWORD dwSize;
  LPBYTE lpbCredentialsInfo;
} MPR_CREDENTIALSEX_1, *PMPR_CREDENTIALSEX_1;

typedef struct _MPR_DEVICE_0 {
  WCHAR szDeviceType[17];
  WCHAR szDeviceName[129];
} MPR_DEVICE_0, *PMPR_DEVICE_0;

typedef struct _MPR_DEVICE_1 {
  WCHAR szDeviceType[17];
  WCHAR szDeviceName[129];
  WCHAR szLocalPhoneNumber[129];
  PWCHAR szAlternates;
} MPR_DEVICE_1, *PMPR_DEVICE_1;

typedef struct _MPR_FILTER_0 {
  BOOL fEnable;
} MPR_FILTER_0, *PMPR_FILTER_0;

typedef struct _MPR_IFTRANSPORT_0 {
  DWORD dwTransportId;
  HANDLE hIfTransport;
  WCHAR wszIfTransportName[41];
} MPR_IFTRANSPORT_0, *PMPR_IFTRANSPORT_0;

typedef struct _MPR_INTERFACE_0 {
  WCHAR wszInterfaceName[257];
  HANDLE hInterface;
  BOOL fEnabled;
  ROUTER_INTERFACE_TYPE dwIfType;
  ROUTER_CONNECTION_STATE dwConnectionState;
  DWORD fUnReachabilityReasons;
  DWORD dwLastError;
} MPR_INTERFACE_0, *PMPR_INTERFACE_0;

typedef struct _MPR_INTERFACE_1 {
  WCHAR wszInterfaceName[257];
  HANDLE hInterface;
  BOOL fEnabled;
  ROUTER_INTERFACE_TYPE dwIfType;
  ROUTER_CONNECTION_STATE dwConnectionState;
  DWORD fUnReachabilityReasons;
  DWORD dwLastError;
  LPWSTR lpwsDialoutHoursRestriction;
} MPR_INTERFACE_1, *PMPR_INTERFACE_1;

typedef struct _MPR_INTERFACE_2 {
  WCHAR wszInterfaceName[257];
  HANDLE hInterface;
  BOOL fEnabled;
  ROUTER_INTERFACE_TYPE dwIfType;
  ROUTER_CONNECTION_STATE dwConnectionState;
  DWORD fUnReachabilityReasons;
  DWORD dwLastError;
  DWORD dwfOptions;
  WCHAR szLocalPhoneNumber[129];
  PWCHAR szAlternates;
  DWORD ipaddr;
  DWORD ipaddrDns;
  DWORD ipaddrDnsAlt;
  DWORD ipaddrWins;
  DWORD ipaddrWinsAlt;
  DWORD dwfNetProtocols;
  WCHAR szDeviceType[17];
  WCHAR szDeviceName[129];
  WCHAR szX25PadType[33];
  WCHAR szX25Address[201];
  WCHAR szX25Facilities[201];
  WCHAR szX25UserData[201];
  DWORD dwChannels;
  DWORD dwSubEntries;
  DWORD dwDialMode;
  DWORD dwDialExtraPercent;
  DWORD dwDialExtraSampleSeconds;
  DWORD dwHangUpExtraPercent;
  DWORD dwHangUpExtraSampleSeconds;
  DWORD dwIdleDisconnectSeconds;
  DWORD dwType;
  DWORD dwEncryptionType;
  DWORD dwCustomAuthKey;
  DWORD dwCustomAuthDataSize;
  LPBYTE lpbCustomAuthData;
  GUID guidId;
  DWORD dwVpnStrategy;
} MPR_INTERFACE_2, *PMPR_INTERFACE_2;

typedef struct _MPR_INTERFACE_3 {
  WCHAR wszInterfaceName[257];
  HANDLE hInterface;
  BOOL fEnabled;
  ROUTER_INTERFACE_TYPE dwIfType;
  ROUTER_CONNECTION_STATE dwConnectionState;
  DWORD fUnReachabilityReasons;
  DWORD dwLastError;
  DWORD dwfOptions;
  WCHAR szLocalPhoneNumber[129];
  PWCHAR szAlternates;
  DWORD ipaddr;
  DWORD ipaddrDns;
  DWORD ipaddrDnsAlt;
  DWORD ipaddrWins;
  DWORD ipaddrWinsAlt;
  DWORD dwfNetProtocols;
  WCHAR szDeviceType[17];
  WCHAR szDeviceName[129];
  WCHAR szX25PadType[33];
  WCHAR szX25Address[201];
  WCHAR szX25Facilities[201];
  WCHAR szX25UserData[201];
  DWORD dwChannels;
  DWORD dwSubEntries;
  DWORD dwDialMode;
  DWORD dwDialExtraPercent;
  DWORD dwDialExtraSampleSeconds;
  DWORD dwHangUpExtraPercent;
  DWORD dwHangUpExtraSampleSeconds;
  DWORD dwIdleDisconnectSeconds;
  DWORD dwType;
  DWORD dwEncryptionType;
  DWORD dwCustomAuthKey;
  DWORD dwCustomAuthDataSize;
  LPBYTE lpbCustomAuthData;
  GUID guidId;
  DWORD dwVpnStrategy;
  ULONG AddressCount;
  IN6_ADDR ipv6addrDns;
  IN6_ADDR ipv6addrDnsAlt;
  IN6_ADDR * ipv6addr;
} MPR_INTERFACE_3, *PMPR_INTERFACE_3;

typedef struct _MPR_IPINIP_INTERFACE_0 {
  WCHAR wszFriendlyName[257];
  GUID Guid;
} MPR_IPINIP_INTERFACE_0, *PMPR_IPINIP_INTERFACE_0;

typedef struct _MPR_SERVER_0 {
  BOOL fLanOnlyMode;
  DWORD dwUpTime;
  DWORD dwTotalPorts;
  DWORD dwPortsInUse;
} MPR_SERVER_0, *PMPR_SERVER_0;

typedef struct _MPR_SERVER_1 {
  DWORD dwNumPptpPorts;
  DWORD dwPptpPortFlags;
  DWORD dwNumL2tpPorts;
  DWORD dwL2tpPortFlags;
} MPR_SERVER_1, *PMPR_SERVER_1;

typedef struct _MPR_SERVER_2 {
  DWORD dwNumPptpPorts;
  DWORD dwPptpPortFlags;
  DWORD dwNumL2tpPorts;
  DWORD dwL2tpPortFlags;
  DWORD dwNumSstpPorts;
  DWORD dwSstpPortFlags;
} MPR_SERVER_2, *PMPR_SERVER_2;

typedef struct _MPR_TRANSPORT_0 {
  DWORD dwTransportId;
  HANDLE hTransport;
  WCHAR wszTransportName[41];
} MPR_TRANSPORT_0, *PMPR_TRANSPORT_0;

typedef struct _MPRAPI_OBJECT_HEADER {
  UCHAR revision;
  UCHAR type;
  USHORT size;
} MPRAPI_OBJECT_HEADER, *PMPRAPI_OBJECT_HEADER;

typedef struct _AUTH_VALIDATION_EX {
  MPRAPI_OBJECT_HEADER Header;
  HANDLE hRasConnection;
  WCHAR wszUserName[257];
  WCHAR wszLogonDomain[16];
  DWORD AuthInfoSize;
  BYTE AuthInfo[1];
} AUTH_VALIDATION_EX;

typedef struct _PPP_ATCP_INFO {
  DWORD dwError;
  WCHAR wszAddress[33];
} PPP_ATCP_INFO;

typedef struct _PPP_CCP_INFO {
  DWORD dwError;
  DWORD dwCompressionAlgorithm;
  DWORD dwOptions;
  DWORD dwRemoteCompressionAlgorithm;
  DWORD dwRemoteOptions;
} PPP_CCP_INFO;

typedef struct _PPP_IPCP_INFO {
  DWORD dwError;
  WCHAR wszAddress[16];
  WCHAR wszRemoteAddress[16];
} PPP_IPCP_INFO;

typedef struct _PPP_IPCP_INFO2 {
  DWORD dwError;
  WCHAR wszAddress[16];
  WCHAR wszRemoteAddress[16];
  DWORD dwOptions;
  DWORD dwRemoteOptions;
} PPP_IPCP_INFO2;

typedef struct _PPP_IPV6_CP_INFO {
  DWORD dwVersion;
  DWORD dwSize;
  DWORD dwError;
  BYTE bInterfaceIdentifier[8];
  BYTE bRemoteInterfaceIdentifier[8];
  DWORD dwOptions;
  DWORD dwRemoteOptions;
  BYTE bPrefix[8];
  DWORD dwPrefixLength;
} PPP_IPV6_CP_INFO;

typedef struct _PPP_IPXCP_INFO {
  DWORD dwError;
  WCHAR wszAddress[23];
} PPP_IPXCP_INFO;

typedef struct _PPP_LCP_INFO {
  DWORD dwError;
  DWORD dwAuthenticationProtocol;
  DWORD dwAuthenticationData;
  DWORD dwRemoteAuthenticationProtocol;
  DWORD dwRemoteAuthenticationData;
  DWORD dwTerminateReason;
  DWORD dwRemoteTerminateReason;
  DWORD dwOptions;
  DWORD dwRemoteOptions;
  DWORD dwEapTypeId;
  DWORD dwRemoteEapTypeId;
} PPP_LCP_INFO;

typedef struct _PPP_NBFCP_INFO {
  DWORD dwError;
  WCHAR wszWksta[17];
} PPP_NBFCP_INFO;

typedef struct _PPP_INFO {
  PPP_NBFCP_INFO nbf;
  PPP_IPCP_INFO ip;
  PPP_IPXCP_INFO ipx;
  PPP_ATCP_INFO at;
} PPP_INFO;

typedef struct _PPP_INFO_2 {
  PPP_NBFCP_INFO nbf;
  PPP_IPCP_INFO2 ip;
  PPP_IPXCP_INFO ipx;
  PPP_ATCP_INFO at;
  PPP_CCP_INFO ccp;
  PPP_LCP_INFO lcp;
} PPP_INFO_2;

typedef struct _PPP_INFO_3 {
  PPP_NBFCP_INFO nbf;
  PPP_IPCP_INFO2 ip;
  PPP_IPV6_CP_INFO ipv6;
  PPP_CCP_INFO ccp;
  PPP_LCP_INFO lcp;
} PPP_INFO_3;

typedef struct _PPP_PROJECTION_INFO {
  DWORD dwIPv4NegotiationError;
  WCHAR wszAddress[16];
  WCHAR wszRemoteAddress[16];
  DWORD dwIPv4Options;
  DWORD dwIPv4RemoteOptions;
  ULONG64 IPv4SubInterfaceIndex;
  DWORD dwIPv6NegotiationError;
  BYTE bInterfaceIdentifier[8];
  BYTE bRemoteInterfaceIdentifier[8];
  BYTE bPrefix[8];
  DWORD dwPrefixLength;
  ULONG64 IPv6SubInterfaceIndex;
  DWORD dwLcpError;
  DWORD dwAuthenticationProtocol;
  DWORD dwAuthenticationData;
  DWORD dwRemoteAuthenticationProtocol;
  DWORD dwRemoteAuthenticationData;
  DWORD dwLcpTerminateReason;
  DWORD dwLcpRemoteTerminateReason;
  DWORD dwLcpOptions;
  DWORD dwLcpRemoteOptions;
  DWORD dwEapTypeId;
  DWORD dwRemoteEapTypeId;
  DWORD dwCcpError;
  DWORD dwCompressionAlgorithm;
  DWORD dwCcpOptions;
  DWORD dwRemoteCompressionAlgorithm;
  DWORD dwCcpRemoteOptions;
} PPP_PROJECTION_INFO, *PPPP_PROJECTION_INFO;

typedef struct _PPP_PROJECTION_INFO2 {
  DWORD dwIPv4NegotiationError;
  WCHAR wszAddress[16];
  WCHAR wszRemoteAddress[16];
  DWORD dwIPv4Options;
  DWORD dwIPv4RemoteOptions;
  ULONG64 IPv4SubInterfaceIndex;
  DWORD dwIPv6NegotiationError;
  BYTE bInterfaceIdentifier[8];
  BYTE bRemoteInterfaceIdentifier[8];
  BYTE bPrefix[8];
  DWORD dwPrefixLength;
  ULONG64 IPv6SubInterfaceIndex;
  DWORD dwLcpError;
  DWORD dwAuthenticationProtocol;
  DWORD dwAuthenticationData;
  DWORD dwRemoteAuthenticationProtocol;
  DWORD dwRemoteAuthenticationData;
  DWORD dwLcpTerminateReason;
  DWORD dwLcpRemoteTerminateReason;
  DWORD dwLcpOptions;
  DWORD dwLcpRemoteOptions;
  DWORD dwEapTypeId;
  DWORD dwEmbeddedEAPTypeId;
  DWORD dwRemoteEapTypeId;
  DWORD dwCcpError;
  DWORD dwCompressionAlgorithm;
  DWORD dwCcpOptions;
  DWORD dwRemoteCompressionAlgorithm;
  DWORD dwCcpRemoteOptions;
} PPP_PROJECTION_INFO2, *PPPP_PROJECTION_INFO2;

typedef struct _PPTP_CONFIG_PARAMS {
  DWORD dwNumPorts;
  DWORD dwPortFlags;
} PPTP_CONFIG_PARAMS, *PPPTP_CONFIG_PARAMS;

typedef struct _RAS_CONNECTION_0 {
  HANDLE hConnection;
  HANDLE hInterface;
  DWORD dwConnectDuration;
  ROUTER_INTERFACE_TYPE dwInterfaceType;
  DWORD dwConnectionFlags;
  WCHAR wszInterfaceName[257];
  WCHAR wszUserName[257];
  WCHAR wszLogonDomain[16];
  WCHAR wszRemoteComputer[17];
} RAS_CONNECTION_0, *PRAS_CONNECTION_0;

typedef struct _RAS_CONNECTION_1 {
  HANDLE hConnection;
  HANDLE hInterface;
  PPP_INFO PppInfo;
  DWORD dwBytesXmited;
  DWORD dwBytesRcved;
  DWORD dwFramesXmited;
  DWORD dwFramesRcved;
  DWORD dwCrcErr;
  DWORD dwTimeoutErr;
  DWORD dwAlignmentErr;
  DWORD dwHardwareOverrunErr;
  DWORD dwFramingErr;
  DWORD dwBufferOverrunErr;
  DWORD dwCompressionRatioIn;
  DWORD dwCompressionRatioOut;
} RAS_CONNECTION_1, *PRAS_CONNECTION_1;

typedef struct _RAS_CONNECTION_2 {
  HANDLE hConnection;
  WCHAR wszUserName[257];
  ROUTER_INTERFACE_TYPE dwInterfaceType;
  GUID guid;
  PPP_INFO_2 PppInfo2;
} RAS_CONNECTION_2, *PRAS_CONNECTION_2;

typedef struct _RAS_CONNECTION_3 {
  DWORD dwVersion;
  DWORD dwSize;
  HANDLE hConnection;
  WCHAR wszUserName[257];
  ROUTER_INTERFACE_TYPE dwInterfaceType;
  GUID guid;
  PPP_INFO_3 PppInfo3;
  RAS_QUARANTINE_STATE rasQuarState;
  FILETIME timer;
} RAS_CONNECTION_3, *PRAS_CONNECTION_3;

typedef struct _RAS_PORT_2 {
  HANDLE hPort;
  HANDLE hConnection;
  DWORD dwConn_State;
  WCHAR wszPortName[17];
  WCHAR wszMediaName[17];
  WCHAR wszDeviceName[129];
  WCHAR wszDeviceType[17];
  RAS_HARDWARE_CONDITION dwHardwareCondition;
  DWORD dwLineSpeed;
  DWORD dwCrcErr;
  DWORD dwSerialOverRunErrs;
  DWORD dwTimeoutErr;
  DWORD dwAlignmentErr;
  DWORD dwHardwareOverrunErr;
  DWORD dwFramingErr;
  DWORD dwBufferOverrunErr;
  DWORD dwCompressionRatioIn;
  DWORD dwCompressionRatioOut;
  DWORD dwTotalErrors;
  ULONGLONG ullBytesXmited;
  ULONGLONG ullBytesRcved;
  ULONGLONG ullFramesXmited;
  ULONGLONG ullFramesRcved;
  ULONGLONG ullBytesTxUncompressed;
  ULONGLONG ullBytesTxCompressed;
  ULONGLONG ullBytesRcvUncompressed;
  ULONGLONG ullBytesRcvCompressed;
} RAS_PORT_2, *PRAS_PORT_2;

typedef struct _RAS_USER_1 {
  BYTE bfPrivilege;
  WCHAR wszPhoneNumber[129];
  BYTE bfPrivilege2;
} RAS_USER_1, *PRAS_USER_1;

typedef struct _ROUTER_CUSTOM_IKEv2_POLICY0 {
  DWORD dwIntegrityMethod;
  DWORD dwEncryptionMethod;
  DWORD dwCipherTransformConstant;
  DWORD dwAuthTransformConstant;
  DWORD dwPfsGroup;
  DWORD dwDhGroup;
} ROUTER_CUSTOM_IKEv2_POLICY0, *PROUTER_CUSTOM_IKEv2_POLICY0, ROUTER_CUSTOM_L2TP_POLICY0, *PROUTER_CUSTOM_L2TP_POLICY0;

typedef struct _IKEV2_TUNNEL_CONFIG_PARAMS2 {
  DWORD dwIdleTimeout;
  DWORD dwNetworkBlackoutTime;
  DWORD dwSaLifeTime;
  DWORD dwSaDataSizeForRenegotiation;
  DWORD dwConfigOptions;
  DWORD dwTotalCertificates;
  CERT_NAME_BLOB * certificateNames;
  CERT_NAME_BLOB machineCertificateName;
  DWORD dwEncryptionType;
  PROUTER_CUSTOM_IKEv2_POLICY0 customPolicy;
} IKEV2_TUNNEL_CONFIG_PARAMS2, *PIKEV2_TUNNEL_CONFIG_PARAMS2;

typedef struct _IKEV2_TUNNEL_CONFIG_PARAMS3 {
  DWORD dwIdleTimeout;
  DWORD dwNetworkBlackoutTime;
  DWORD dwSaLifeTime;
  DWORD dwSaDataSizeForRenegotiation;
  DWORD dwConfigOptions;
  DWORD dwTotalCertificates;
  CERT_NAME_BLOB * certificateNames;
  CERT_NAME_BLOB machineCertificateName;
  DWORD dwEncryptionType;
  PROUTER_CUSTOM_IKEv2_POLICY0 customPolicy;
  DWORD dwTotalEkus;
  PMPR_CERT_EKU certificateEKUs;
  CRYPT_HASH_BLOB machineCertificateHash;
} IKEV2_TUNNEL_CONFIG_PARAMS3, *PIKEV2_TUNNEL_CONFIG_PARAMS3;

typedef struct _IKEV2_TUNNEL_CONFIG_PARAMS4 {
  DWORD dwIdleTimeout;
  DWORD dwNetworkBlackoutTime;
  DWORD dwSaLifeTime;
  DWORD dwSaDataSizeForRenegotiation;
  DWORD dwConfigOptions;
  DWORD dwTotalCertificates;
  CERT_NAME_BLOB * certificateNames;
  CERT_NAME_BLOB machineCertificateName;
  DWORD dwEncryptionType;
  PROUTER_CUSTOM_IKEv2_POLICY0 customPolicy;
  DWORD dwTotalEkus;
  PMPR_CERT_EKU certificateEKUs;
  CRYPT_HASH_BLOB machineCertificateHash;
  DWORD dwMmSaLifeTime;
} IKEV2_TUNNEL_CONFIG_PARAMS4, *PIKEV2_TUNNEL_CONFIG_PARAMS4;

typedef struct _IKEV2_CONFIG_PARAMS {
  DWORD dwNumPorts;
  DWORD dwPortFlags;
  DWORD dwTunnelConfigParamFlags;
  IKEV2_TUNNEL_CONFIG_PARAMS4 TunnelConfigParams;
} IKEV2_CONFIG_PARAMS, *PIKEV2_CONFIG_PARAMS;

typedef struct _L2TP_TUNNEL_CONFIG_PARAMS1 {
  DWORD dwIdleTimeout;
  DWORD dwEncryptionType;
  DWORD dwSaLifeTime;
  DWORD dwSaDataSizeForRenegotiation;
  PROUTER_CUSTOM_L2TP_POLICY0 customPolicy;
} L2TP_TUNNEL_CONFIG_PARAMS1, *PL2TP_TUNNEL_CONFIG_PARAMS1;

typedef struct _L2TP_TUNNEL_CONFIG_PARAMS2 {
  DWORD dwIdleTimeout;
  DWORD dwEncryptionType;
  DWORD dwSaLifeTime;
  DWORD dwSaDataSizeForRenegotiation;
  PROUTER_CUSTOM_L2TP_POLICY0 customPolicy;
  DWORD dwMmSaLifeTime;
} L2TP_TUNNEL_CONFIG_PARAMS2, *PL2TP_TUNNEL_CONFIG_PARAMS2;

typedef struct _L2TP_CONFIG_PARAMS1 {
  DWORD dwNumPorts;
  DWORD dwPortFlags;
  DWORD dwTunnelConfigParamFlags;
  L2TP_TUNNEL_CONFIG_PARAMS2 TunnelConfigParams;
} L2TP_CONFIG_PARAMS1, *PL2TP_CONFIG_PARAMS1;

typedef struct _ROUTER_IKEv2_IF_CUSTOM_CONFIG0 {
  DWORD dwSaLifeTime;
  DWORD dwSaDataSize;
  CERT_NAME_BLOB certificateName;
  ROUTER_CUSTOM_IKEv2_POLICY0 * customPolicy;
} ROUTER_IKEv2_IF_CUSTOM_CONFIG0, *PROUTER_IKEv2_IF_CUSTOM_CONFIG0;

typedef struct _MPR_IF_CUSTOMINFOEX0 {
  MPRAPI_OBJECT_HEADER Header;
  DWORD dwFlags;
  ROUTER_IKEv2_IF_CUSTOM_CONFIG0 customIkev2Config;
} MPR_IF_CUSTOMINFOEX0, *PMPR_IF_CUSTOMINFOEX0;

typedef struct _ROUTER_IKEv2_IF_CUSTOM_CONFIG1 {
  DWORD dwSaLifeTime;
  DWORD dwSaDataSize;
  CERT_NAME_BLOB certificateName;
  ROUTER_CUSTOM_IKEv2_POLICY0 * customPolicy;
  CRYPT_HASH_BLOB certificateHash;
} ROUTER_IKEv2_IF_CUSTOM_CONFIG1, *PROUTER_IKEv2_IF_CUSTOM_CONFIG1;

typedef struct _MPR_IF_CUSTOMINFOEX1 {
  MPRAPI_OBJECT_HEADER Header;
  DWORD dwFlags;
  ROUTER_IKEv2_IF_CUSTOM_CONFIG1 customIkev2Config;
} MPR_IF_CUSTOMINFOEX1, *PMPR_IF_CUSTOMINFOEX1;

typedef struct _SSTP_CERT_INFO {
  BOOL isDefault;
  CRYPT_HASH_BLOB certBlob;
} SSTP_CERT_INFO, *PSSTP_CERT_INFO;

typedef struct _SSTP_CONFIG_PARAMS {
  DWORD dwNumPorts;
  DWORD dwPortFlags;
  BOOL isUseHttps;
  DWORD certAlgorithm;
  SSTP_CERT_INFO sstpCertDetails;
} SSTP_CONFIG_PARAMS, *PSSTP_CONFIG_PARAMS;

typedef struct _MPRAPI_TUNNEL_CONFIG_PARAMS0 {
  IKEV2_CONFIG_PARAMS IkeConfigParams;
  PPTP_CONFIG_PARAMS PptpConfigParams;
  L2TP_CONFIG_PARAMS1 L2tpConfigParams;
  SSTP_CONFIG_PARAMS SstpConfigParams;
} MPRAPI_TUNNEL_CONFIG_PARAMS0, *PMPRAPI_TUNNEL_CONFIG_PARAMS0;

typedef struct _MPR_SERVER_EX0 {
  MPRAPI_OBJECT_HEADER Header;
  DWORD fLanOnlyMode;
  DWORD dwUpTime;
  DWORD dwTotalPorts;
  DWORD dwPortsInUse;
  DWORD Reserved;
  MPRAPI_TUNNEL_CONFIG_PARAMS0 ConfigParams;
} MPR_SERVER_EX0, *PMPR_SERVER_EX0;

typedef struct _MPR_SERVER_SET_CONFIG_EX0 {
  MPRAPI_OBJECT_HEADER Header;
  DWORD setConfigForProtocols;
  MPRAPI_TUNNEL_CONFIG_PARAMS0 ConfigParams;
} MPR_SERVER_SET_CONFIG_EX0, *PMPR_SERVER_SET_CONFIG_EX0;

typedef struct _MPRAPI_TUNNEL_CONFIG_PARAMS1 {
  IKEV2_CONFIG_PARAMS IkeConfigParams;
  PPTP_CONFIG_PARAMS PptpConfigParams;
  L2TP_CONFIG_PARAMS1 L2tpConfigParams;
  SSTP_CONFIG_PARAMS SstpConfigParams;
  GRE_CONFIG_PARAMS0 GREConfigParams;
} MPRAPI_TUNNEL_CONFIG_PARAMS1, *PMPRAPI_TUNNEL_CONFIG_PARAMS1;

typedef struct _MPR_SERVER_EX1 {
  MPRAPI_OBJECT_HEADER Header;
  DWORD fLanOnlyMode;
  DWORD dwUpTime;
  DWORD dwTotalPorts;
  DWORD dwPortsInUse;
  DWORD Reserved;
  MPRAPI_TUNNEL_CONFIG_PARAMS1 ConfigParams;
} MPR_SERVER_EX1, *PMPR_SERVER_EX1;

typedef struct _MPR_SERVER_SET_CONFIG_EX1 {
  MPRAPI_OBJECT_HEADER Header;
  DWORD setConfigForProtocols;
  MPRAPI_TUNNEL_CONFIG_PARAMS1 ConfigParams;
} MPR_SERVER_SET_CONFIG_EX1, *PMPR_SERVER_SET_CONFIG_EX1;

typedef struct RAS_UPDATE_CONNECTION_ {
  MPRAPI_OBJECT_HEADER Header;
  DWORD dwIfIndex;
  WCHAR wszLocalEndpointAddress[65];
  WCHAR wszRemoteEndpointAddress[65];
} RAS_UPDATE_CONNECTION, *PRAS_UPDATE_CONNECTION;

#define ALLOW_NO_AUTH 1
#define GRE_CONFIG_PARAMS GRE_CONFIG_PARAMS0
#define IKEV2_TUNNEL_CONFIG_PARAMS IKEV2_TUNNEL_CONFIG_PARAMS4
#define L2TP_CONFIG_PARAMS L2TP_CONFIG_PARAMS1
#define L2TP_TUNNEL_CONFIG_PARAMS L2TP_TUNNEL_CONFIG_PARAMS2
#define MAX_SSTP_HASH_SIZE 32
#define MPR_ET_None 0
#define MPR_ET_Optional 3
#define MPR_ET_Require 1
#define MPR_ET_RequireMax 2
#define MPR_IF_CUSTOMINFOEX MPR_IF_CUSTOMINFOEX2
#define MPR_MaxAreaCode 0xA
#define MPR_MaxCallbackNumber 0x80
#define MPR_MaxDeviceName 0x80
#define MPR_MaxDeviceType 0x10
#define MPR_MaxEntryName 0x100
#define MPR_MaxFacilities 0xC8
#define MPR_MaxIpAddress 0xF
#define MPR_MaxIpxAddress 0x15
#define MPR_MaxPadType 0x20
#define MPR_MaxPhoneNumber 0x80
#define MPR_MaxUserData 0xC8
#define MPR_MaxX25Address 0xC8
#define MPR_SERVER_EX MPR_SERVER_EX1
#define MPR_SERVER_SET_CONFIG_EX MPR_SERVER_SET_CONFIG_EX1
#define MPR_VS_Default 0
#define MPR_VS_Ikev2First 8
#define MPR_VS_Ikev2Only 7
#define MPR_VS_L2tpFirst 4
#define MPR_VS_L2tpOnly 3
#define MPR_VS_PptpFirst 2
#define MPR_VS_PptpOnly 1
#define MPRAPI_IKEV2_SET_TUNNEL_CONFIG_PARAMS 0x1
#define MPRAPI_MPR_SERVER_OBJECT_REVISION_1 0x1
#define MPRAPI_MPR_SERVER_SET_CONFIG_OBJECT_REVISION_1 0x1
#define MPRAPI_TUNNEL_CONFIG_PARAMS MPRAPI_TUNNEL_CONFIG_PARAMS1
#define MPRDM_DialAll 1
#define MPRDM_DialAsNeeded 2
#define MPRDM_DialFirst 0
#define MPRDT_Atm RASDT_Atm
#define MPRDT_FrameRelay RASDT_FrameRelay
#define MPRDT_Generic RASDT_Generic
#define MPRDT_Irda RASDT_Irda
#define MPRDT_Isdn RASDT_Isdn
#define MPRDT_Modem RASDT_Modem
#define MPRDT_Pad RASDT_Pad
#define MPRDT_Parallel RASDT_Parallel
#define MPRDT_Serial RASDT_Serial
#define MPRDT_Sonet RASDT_Sonet
#define MPRDT_SW56 RASDT_SW56
#define MPRDT_Vpn RASDT_Vpn
#define MPRDT_X25 RASDT_X25
#define MPRET_Direct RASET_Direct
#define MPRET_Phone RASET_Phone
#define MPRET_Vpn 2
#define MPRIDS_Disabled 0xFFFFFFFF
#define MPRIDS_UseGlobalValue 0
#define MPRIO_DisableLcpExtensions 0x20
#define MPRIO_IpHeaderCompression 8
#define MPRIO_NetworkLogon 0x2000
#define MPRIO_PromoteAlternates 0x8000
#define MPRIO_RemoteDefaultGateway 0x10
#define MPRIO_RequireCHAP 0x8000000
#define MPRIO_RequireDataEncryption 0x1000
#define MPRIO_RequireEAP 0x20000
#define MPRIO_RequireEncryptedPw 0x400
#define MPRIO_RequireMsCHAP 0x10000000
#define MPRIO_RequireMsCHAP2 0x20000000
#define MPRIO_RequireMsEncryptedPw 0x800
#define MPRIO_RequirePAP 0x40000
#define MPRIO_RequireSPAP 0x80000
#define MPRIO_SecureLocalFiles 0x10000
#define MPRIO_SharedPhoneNumbers 0x800000
#define MPRIO_SpecificIpAddr 2
#define MPRIO_SpecificNameServers 4
#define MPRIO_SwCompression 0x200
#define MPRNP_Ip 4
#define MPRNP_Ipv6 8
#define MPRNP_Ipx 2
#define PGRE_CONFIG_PARAMS PGRE_CONFIG_PARAMS0
#define PIKEV2_TUNNEL_CONFIG_PARAMS PIKEV2_TUNNEL_CONFIG_PARAMS4
#define PL2TP_CONFIG_PARAMS PL2TP_CONFIG_PARAMS1
#define PL2TP_TUNNEL_CONFIG_PARAMS PL2TP_TUNNEL_CONFIG_PARAMS2
#define PMPR_IF_CUSTOMINFOEX PMPR_IF_CUSTOMINFOEX2
#define PMPR_SERVER_EX PMPR_SERVER_EX1
#define PMPR_SERVER_SET_CONFIG_EX PMPR_SERVER_SET_CONFIG_EX1
#define PMPRAPI_TUNNEL_CONFIG_PARAMS PMPRAPI_TUNNEL_CONFIG_PARAMS1
#define PROUTER_CUSTOM_IKEv2_POLICY PROUTER_CUSTOM_IKEv2_POLICY0
#define PROUTER_CUSTOM_L2TP_POLICY PROUTER_CUSTOM_L2TP_POLICY0
#define PROUTER_IKEv2_IF_CUSTOM_CONFIG PROUTER_IKEv2_IF_CUSTOM_CONFIG2
#define ROUTER_CUSTOM_IKEv2_POLICY ROUTER_CUSTOM_IKEv2_POLICY0
#define ROUTER_CUSTOM_L2TP_POLICY ROUTER_CUSTOM_L2TP_POLICY0
#define ROUTER_IKEv2_IF_CUSTOM_CONFIG ROUTER_IKEv2_IF_CUSTOM_CONFIG2
#define RRAS_SERVICE_NAME TEXT("RemoteAccess")

struct _PROJECTION_INFO
{
    UCHAR projectionInfoType;
    union
    {
        PPP_PROJECTION_INFO PppProjectionInfo;
        IKEV2_PROJECTION_INFO Ikev2ProjectionInfo;
    };
};

struct _PROJECTION_INFO2
{
    UCHAR projectionInfoType;
    union
    {
        PPP_PROJECTION_INFO2 PppProjectionInfo;
        IKEV2_PROJECTION_INFO2 Ikev2ProjectionInfo;
    };
};

struct _VPN_TS_IP_ADDRESS
{
    USHORT Type;
    union
    {
        IN_ADDR v4;
        IN6_ADDR v6;
    };
};

typedef BOOL (WINAPI *PMPRADMINACCEPTNEWCONNECTION)(RAS_CONNECTION_0 *, RAS_CONNECTION_1 *);

typedef BOOL (WINAPI *PMPRADMINACCEPTNEWCONNECTION2)(RAS_CONNECTION_0 *, RAS_CONNECTION_1 *, RAS_CONNECTION_2 *);

typedef BOOL (WINAPI *PMPRADMINACCEPTNEWCONNECTION3)(RAS_CONNECTION_0 *, RAS_CONNECTION_1 *, RAS_CONNECTION_2 *, RAS_CONNECTION_3 *);

typedef struct _RAS_PORT_0 RAS_PORT_0;

typedef struct _RAS_PORT_1 RAS_PORT_1;

typedef BOOL (WINAPI *PMPRADMINACCEPTNEWLINK)(RAS_PORT_0 *, RAS_PORT_1 *);

typedef BOOL (WINAPI *PMPRADMINACCEPTREAUTHENTICATION)(RAS_CONNECTION_0 *, RAS_CONNECTION_1 *, RAS_CONNECTION_2 *, RAS_CONNECTION_3 *);

typedef void (WINAPI *PMPRADMINCONNECTIONHANGUPNOTIFICATION)(RAS_CONNECTION_0 *, RAS_CONNECTION_1 *);

typedef void (WINAPI *PMPRADMINCONNECTIONHANGUPNOTIFICATION2)(RAS_CONNECTION_0 *, RAS_CONNECTION_1 *, RAS_CONNECTION_2 *);

typedef void (WINAPI *PMPRADMINCONNECTIONHANGUPNOTIFICATION3)(RAS_CONNECTION_0 *, RAS_CONNECTION_1 *, RAS_CONNECTION_2 *, RAS_CONNECTION_3);

typedef DWORD (WINAPI *PMPRADMINGETIPADDRESSFORUSER)(WCHAR *, WCHAR *, DWORD *, BOOL *);

typedef DWORD (WINAPI *PMPRADMINGETIPV6ADDRESSFORUSER)(WCHAR *, WCHAR *, IN6_ADDR *, BOOL *);

typedef void (WINAPI *PMPRADMINLINKHANGUPNOTIFICATION)(RAS_PORT_0 *, RAS_PORT_1 *);

typedef DWORD (WINAPI *PMPRADMINRASVALIDATEPREAUTHENTICATEDCONNECTIONEX)(AUTH_VALIDATION_EX *);

typedef void (WINAPI *PMPRADMINRELEASEIPADRESS)(WCHAR *, WCHAR *, DWORD *);

typedef void (WINAPI *PMPRADMINRELEASEIPV6ADDRESSFORUSER)(WCHAR *, WCHAR *, IN6_ADDR *);

typedef DWORD (WINAPI *PMPRADMINTERMINATEDLL)();

typedef struct _PROJECTION_INFO *PPROJECTION_INFO;

typedef struct _PROJECTION_INFO2 *PPROJECTION_INFO2;

typedef struct _PROJECTION_INFO PROJECTION_INFO;

struct _RAS_CONNECTION_EX
{
    MPRAPI_OBJECT_HEADER Header;
    DWORD dwConnectDuration;
    ROUTER_INTERFACE_TYPE dwInterfaceType;
    DWORD dwConnectionFlags;
    WCHAR wszInterfaceName[257];
    WCHAR wszUserName[257];
    WCHAR wszLogonDomain[16];
    WCHAR wszRemoteComputer[17];
    GUID guid;
    RAS_QUARANTINE_STATE rasQuarState;
    FILETIME probationTime;
    DWORD dwBytesXmited;
    DWORD dwBytesRcved;
    DWORD dwFramesXmited;
    DWORD dwFramesRcved;
    DWORD dwCrcErr;
    DWORD dwTimeoutErr;
    DWORD dwAlignmentErr;
    DWORD dwHardwareOverrunErr;
    DWORD dwFramingErr;
    DWORD dwBufferOverrunErr;
    DWORD dwCompressionRatioIn;
    DWORD dwCompressionRatioOut;
    DWORD dwNumSwitchOvers;
    WCHAR wszRemoteEndpointAddress[65];
    WCHAR wszLocalEndpointAddress[65];
    PROJECTION_INFO ProjectionInfo;
    HANDLE hConnection;
    HANDLE hInterface;
};

typedef struct _RAS_CONNECTION_EX *PRAS_CONNECTION_EX;

typedef struct _PROJECTION_INFO2 PROJECTION_INFO2;

struct _RAS_CONNECTION_4
{
    DWORD dwConnectDuration;
    ROUTER_INTERFACE_TYPE dwInterfaceType;
    DWORD dwConnectionFlags;
    WCHAR wszInterfaceName[257];
    WCHAR wszUserName[257];
    WCHAR wszLogonDomain[16];
    WCHAR wszRemoteComputer[17];
    GUID guid;
    RAS_QUARANTINE_STATE rasQuarState;
    FILETIME probationTime;
    FILETIME connectionStartTime;
    ULONGLONG ullBytesXmited;
    ULONGLONG ullBytesRcved;
    DWORD dwFramesXmited;
    DWORD dwFramesRcved;
    DWORD dwCrcErr;
    DWORD dwTimeoutErr;
    DWORD dwAlignmentErr;
    DWORD dwHardwareOverrunErr;
    DWORD dwFramingErr;
    DWORD dwBufferOverrunErr;
    DWORD dwCompressionRatioIn;
    DWORD dwCompressionRatioOut;
    DWORD dwNumSwitchOvers;
    WCHAR wszRemoteEndpointAddress[65];
    WCHAR wszLocalEndpointAddress[65];
    PROJECTION_INFO2 ProjectionInfo;
    HANDLE hConnection;
    HANDLE hInterface;
    DWORD dwDeviceType;
};

typedef struct _RAS_CONNECTION_4 *PRAS_CONNECTION_4;

typedef struct _VPN_TS_IP_ADDRESS *PVPN_TS_IP_ADDRESS;

typedef struct _RAS_CONNECTION_4 RAS_CONNECTION_4;

typedef struct _RAS_CONNECTION_EX RAS_CONNECTION_EX;

typedef BOOL (WINAPI *PMPRADMINACCEPTNEWCONNECTIONEX)(RAS_CONNECTION_EX *);

typedef BOOL (WINAPI *PMPRADMINACCEPTREAUTHENTICATIONEX)(RAS_CONNECTION_EX *);

typedef BOOL (WINAPI *PMPRADMINACCEPTTUNNELENDPOINTCHANGEEX)(RAS_CONNECTION_EX *);

typedef void (WINAPI *PMPRADMINCONNECTIONHANGUPNOTIFICATIONEX)(RAS_CONNECTION_EX *);

struct _MPRAPI_ADMIN_DLL_CALLBACKS
{
    UCHAR revision;
    PMPRADMINGETIPADDRESSFORUSER lpfnMprAdminGetIpAddressForUser;
    PMPRADMINRELEASEIPADRESS lpfnMprAdminReleaseIpAddress;
    PMPRADMINGETIPV6ADDRESSFORUSER lpfnMprAdminGetIpv6AddressForUser;
    PMPRADMINRELEASEIPV6ADDRESSFORUSER lpfnMprAdminReleaseIpV6AddressForUser;
    PMPRADMINACCEPTNEWLINK lpfnRasAdminAcceptNewLink;
    PMPRADMINLINKHANGUPNOTIFICATION lpfnRasAdminLinkHangupNotification;
    PMPRADMINTERMINATEDLL lpfnRasAdminTerminateDll;
    PMPRADMINACCEPTNEWCONNECTIONEX lpfnRasAdminAcceptNewConnectionEx;
    PMPRADMINACCEPTTUNNELENDPOINTCHANGEEX lpfnRasAdminAcceptEndpointChangeEx;
    PMPRADMINACCEPTREAUTHENTICATIONEX lpfnRasAdminAcceptReauthenticationEx;
    PMPRADMINCONNECTIONHANGUPNOTIFICATIONEX lpfnRasAdminConnectionHangupNotificationEx;
    PMPRADMINRASVALIDATEPREAUTHENTICATEDCONNECTIONEX lpfnRASValidatePreAuthenticatedConnectionEx;
};

typedef struct _MPRAPI_ADMIN_DLL_CALLBACKS MPRAPI_ADMIN_DLL_CALLBACKS;

typedef struct _MPRAPI_ADMIN_DLL_CALLBACKS *PMPRAPI_ADMIN_DLL_CALLBACKS;

typedef struct _VPN_TS_IP_ADDRESS VPN_TS_IP_ADDRESS;

struct _MPR_VPN_SELECTOR
{
    MPR_VPN_TS_TYPE type;
    UINT8 protocolId;
    UINT16 portStart;
    UINT16 portEnd;
    UINT16 tsPayloadId;
    VPN_TS_IP_ADDRESS addrStart;
    VPN_TS_IP_ADDRESS addrEnd;
};

typedef struct _MPR_VPN_SELECTOR MPR_VPN_TRAFFIC_SELECTOR;

typedef struct _MPR_VPN_SELECTOR *PMPR_VPN_TRAFFIC_SELECTOR;

struct _MPR_VPN_TRAFFIC_SELECTORS
{
    DWORD numTsi;
    DWORD numTsr;
    PMPR_VPN_TRAFFIC_SELECTOR tsI;
    PMPR_VPN_TRAFFIC_SELECTOR tsR;
};

typedef struct _MPR_VPN_TRAFFIC_SELECTORS MPR_VPN_TRAFFIC_SELECTORS;

struct _ROUTER_IKEv2_IF_CUSTOM_CONFIG2
{
    DWORD dwSaLifeTime;
    DWORD dwSaDataSize;
    CERT_NAME_BLOB certificateName;
    ROUTER_CUSTOM_IKEv2_POLICY0 *customPolicy;
    CRYPT_HASH_BLOB certificateHash;
    DWORD dwMmSaLifeTime;
    MPR_VPN_TRAFFIC_SELECTORS vpnTrafficSelectors;
};

typedef struct _MPR_VPN_TRAFFIC_SELECTORS *PMPR_VPN_TRAFFIC_SELECTORS;

typedef struct _ROUTER_IKEv2_IF_CUSTOM_CONFIG2 *PROUTER_IKEv2_IF_CUSTOM_CONFIG2;

typedef struct _ROUTER_IKEv2_IF_CUSTOM_CONFIG2 ROUTER_IKEv2_IF_CUSTOM_CONFIG2;

struct _MPR_IF_CUSTOMINFOEX2
{
    MPRAPI_OBJECT_HEADER Header;
    DWORD dwFlags;
    ROUTER_IKEv2_IF_CUSTOM_CONFIG2 customIkev2Config;
};

typedef struct _MPR_IF_CUSTOMINFOEX2 MPR_IF_CUSTOMINFOEX2;

typedef struct _MPR_IF_CUSTOMINFOEX2 *PMPR_IF_CUSTOMINFOEX2;

#ifdef __cplusplus
extern "C" {
#endif

DWORD WINAPI MprAdminGetErrorString(DWORD dwError, LPWSTR *lplpwsErrorString);
DWORD WINAPI MprConfigServerConnect(LPWSTR lpwsServerName, HANDLE *phMprConfig);

/* TODO: LiberNT does not implement the functions below yet */
BOOL WINAPI MprAdminAcceptNewConnection(RAS_CONNECTION_0 *pRasConnection0, RAS_CONNECTION_1 *pRasConnection1);
BOOL WINAPI MprAdminAcceptNewConnection2(RAS_CONNECTION_0 *pRasConnection0, RAS_CONNECTION_1 *pRasConnection1, RAS_CONNECTION_2 *pRasConnection2);
BOOL WINAPI MprAdminAcceptNewConnection3(RAS_CONNECTION_0 *pRasConnection0, RAS_CONNECTION_1 *pRasConnection1, RAS_CONNECTION_2 *pRasConnection2, RAS_CONNECTION_3 *pRasConnection3);
BOOL WINAPI MprAdminAcceptNewConnectionEx(RAS_CONNECTION_EX *pRasConn);
BOOL WINAPI MprAdminAcceptNewLink(RAS_PORT_0 *pRasPort0, RAS_PORT_1 *pRasPort1);
BOOL WINAPI MprAdminAcceptReauthentication(RAS_CONNECTION_0 *pRasConnection0, RAS_CONNECTION_1 *pRasConnection1, RAS_CONNECTION_2 *pRasConnection2, RAS_CONNECTION_3 *pRasConnection3);
BOOL WINAPI MprAdminAcceptReauthenticationEx(RAS_CONNECTION_EX *pRasConn);
DWORD WINAPI MprAdminBufferFree(LPVOID pBuffer);
DWORD WINAPI MprAdminConnectionClearStats(RAS_SERVER_HANDLE hRasServer, HANDLE hRasConnection);
DWORD WINAPI MprAdminConnectionEnum(RAS_SERVER_HANDLE hRasServer, DWORD dwLevel, LPBYTE *lplpbBuffer, DWORD dwPrefMaxLen, LPDWORD lpdwEntriesRead, LPDWORD lpdwTotalEntries, LPDWORD lpdwResumeHandle);
DWORD WINAPI MprAdminConnectionEnumEx(RAS_SERVER_HANDLE hRasServer, PMPRAPI_OBJECT_HEADER pObjectHeader, DWORD dwPreferedMaxLen, LPDWORD lpdwEntriesRead, LPDWORD lpdwTotalEntries, PRAS_CONNECTION_EX *ppRasConn, LPDWORD lpdwResumeHandle);
DWORD WINAPI MprAdminConnectionGetInfo(RAS_SERVER_HANDLE hRasServer, DWORD dwLevel, HANDLE hRasConnection, LPBYTE *lplpbBuffer);
DWORD WINAPI MprAdminConnectionGetInfoEx(RAS_SERVER_HANDLE hRasServer, HANDLE hRasConnection, PRAS_CONNECTION_EX pRasConnection);
void MprAdminConnectionHangupNotification(RAS_CONNECTION_0 *pRasConnection0, RAS_CONNECTION_1 *pRasConnection1);
void MprAdminConnectionHangupNotification2(RAS_CONNECTION_0 *pRasConnection0, RAS_CONNECTION_1 *pRasConnection1, RAS_CONNECTION_2 *pRasConnection2);
void MprAdminConnectionHangupNotification3(RAS_CONNECTION_0 *pRasConnection0, RAS_CONNECTION_1 *pRasConnection1, RAS_CONNECTION_2 *pRasConnection2, RAS_CONNECTION_3 *pRasConnection3);
void WINAPI MprAdminConnectionHangupNotificationEx(RAS_CONNECTION_EX *pRasConn);
DWORD WINAPI MprAdminConnectionRemoveQuarantine(HANDLE hRasServer, HANDLE hRasConnection, BOOL fIsIpAddress);
DWORD WINAPI MprAdminDeregisterConnectionNotification(MPR_SERVER_HANDLE hMprServer, HANDLE hEventNotification);
DWORD WINAPI MprAdminDeviceEnum(MPR_SERVER_HANDLE hMprServer, DWORD dwLevel, LPBYTE *lplpbBuffer, LPDWORD lpdwTotalEntries);
DWORD WINAPI MprAdminEstablishDomainRasServer(LPWSTR pszDomain, LPWSTR pszMachine, BOOL bEnable);
DWORD WINAPI MprAdminGetIpAddressForUser(WCHAR *lpwszUserName, WCHAR *lpwszPortName, DWORD *lpdwIpAddress, BOOL *bNotifyRelease);
DWORD WINAPI MprAdminGetIpv6AddressForUser(WCHAR *lpwszUserName, WCHAR *lpwszPortName, IN6_ADDR *lpdwIpv6Address, BOOL *bNotifyRelease);
DWORD WINAPI MprAdminGetPDCServer(LPCWSTR lpszDomain, LPCWSTR lpszServer, LPWSTR lpszPDCServer);
DWORD WINAPI MprAdminInitializeDll(void);
DWORD WINAPI MprAdminInitializeDllEx(PMPRAPI_ADMIN_DLL_CALLBACKS pAdminCallbacks);
DWORD WINAPI MprAdminInterfaceConnect(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, HANDLE hEvent, BOOL fSynchronous);
DWORD WINAPI MprAdminInterfaceCreate(MPR_SERVER_HANDLE hMprServer, DWORD dwLevel, LPBYTE lpbBuffer, HANDLE *phInterface);
DWORD WINAPI MprAdminInterfaceDelete(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface);
DWORD WINAPI MprAdminInterfaceDeviceGetInfo(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwIndex, DWORD dwLevel, LPBYTE *lplpBuffer);
DWORD WINAPI MprAdminInterfaceDeviceSetInfo(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwIndex, DWORD dwLevel, LPBYTE lpbBuffer);
DWORD WINAPI MprAdminInterfaceDisconnect(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface);
DWORD WINAPI MprAdminInterfaceEnum(MPR_SERVER_HANDLE hMprServer, DWORD dwLevel, LPBYTE *lplpbBuffer, DWORD dwPrefMaxLen, LPDWORD lpdwEntriesRead, LPDWORD lpdwTotalEntries, LPDWORD lpdwResumeHandle);
DWORD WINAPI MprAdminInterfaceGetCredentials(LPWSTR lpwsServer, LPWSTR lpwsInterfaceName, LPWSTR lpwsUserName, LPWSTR lpwsPassword, LPWSTR lpwsDomainName);
DWORD WINAPI MprAdminInterfaceGetCredentialsEx(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwLevel, LPBYTE *lplpbBuffer);
DWORD WINAPI MprAdminInterfaceGetCustomInfoEx(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, PMPR_IF_CUSTOMINFOEX2 pCustomInfo);
DWORD WINAPI MprAdminInterfaceGetHandle(MPR_SERVER_HANDLE hMprServer, LPWSTR lpwsInterfaceName, HANDLE *phInterface, BOOL fIncludeClientInterfaces);
DWORD WINAPI MprAdminInterfaceGetInfo(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwLevel, LPBYTE *lplpbBuffer);
DWORD WINAPI MprAdminInterfaceQueryUpdateResult(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwProtocolId, LPDWORD lpdwUpdateResult);
DWORD WINAPI MprAdminInterfaceSetCredentials(LPWSTR lpwsServer, LPWSTR lpwsInterfaceName, LPWSTR lpwsUserName, LPWSTR lpwsDomainName, LPWSTR lpwsPassword);
DWORD WINAPI MprAdminInterfaceSetCredentialsEx(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwLevel, LPBYTE lpbBuffer);
DWORD WINAPI MprAdminInterfaceSetCustomInfoEx(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, PMPR_IF_CUSTOMINFOEX2 pCustomInfo);
DWORD WINAPI MprAdminInterfaceSetInfo(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwLevel, LPBYTE lpbBuffer);
DWORD WINAPI MprAdminInterfaceTransportAdd(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwTransportId, LPBYTE pInterfaceInfo, DWORD dwInterfaceInfoSize);
DWORD WINAPI MprAdminInterfaceTransportGetInfo(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwTransportId, LPBYTE *ppInterfaceInfo, LPDWORD lpdwInterfaceInfoSize);
DWORD WINAPI MprAdminInterfaceTransportRemove(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwTransportId);
DWORD WINAPI MprAdminInterfaceTransportSetInfo(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwTransportId, LPBYTE pInterfaceInfo, DWORD dwInterfaceInfoSize);
DWORD WINAPI MprAdminInterfaceUpdatePhonebookInfo(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface);
DWORD WINAPI MprAdminInterfaceUpdateRoutes(MPR_SERVER_HANDLE hMprServer, HANDLE hInterface, DWORD dwProtocolId, HANDLE hEvent);
DWORD WINAPI MprAdminIsDomainRasServer(LPWSTR pszDomain, LPWSTR pszMachine, PBOOL pbIsRasServer);
DWORD WINAPI MprAdminIsServiceInitialized(LPWSTR lpwsServerName, BOOL *fIsServiceInitialized);
void MprAdminLinkHangupNotification(RAS_PORT_0 *pRasPort0, RAS_PORT_1 *pRasPort1);
DWORD WINAPI MprAdminMIBBufferFree(LPVOID pBuffer);
DWORD WINAPI MprAdminMIBEntryCreate(MIB_SERVER_HANDLE hMibServer, DWORD dwPid, DWORD dwRoutingPid, LPVOID lpEntry, DWORD dwEntrySize);
DWORD WINAPI MprAdminMIBEntryDelete(MIB_SERVER_HANDLE hMibServer, DWORD dwProtocolId, DWORD dwRoutingPid, LPVOID lpEntry, DWORD dwEntrySize);
DWORD WINAPI MprAdminMIBEntryGet(MIB_SERVER_HANDLE hMibServer, DWORD dwProtocolId, DWORD dwRoutingPid, LPVOID lpInEntry, DWORD dwInEntrySize, LPVOID *lplpOutEntry, LPDWORD lpOutEntrySize);
DWORD WINAPI MprAdminMIBEntryGetFirst(MIB_SERVER_HANDLE hMibServer, DWORD dwProtocolId, DWORD dwRoutingPid, LPVOID lpInEntry, DWORD dwInEntrySize, LPVOID *lplpOutEntry, LPDWORD lpOutEntrySize);
DWORD WINAPI MprAdminMIBEntryGetNext(MIB_SERVER_HANDLE hMibServer, DWORD dwProtocolId, DWORD dwRoutingPid, LPVOID lpInEntry, DWORD dwInEntrySize, LPVOID *lplpOutEntry, LPDWORD lpOutEntrySize);
DWORD WINAPI MprAdminMIBEntrySet(MIB_SERVER_HANDLE hMibServer, DWORD dwProtocolId, DWORD dwRoutingPid, LPVOID lpEntry, DWORD dwEntrySize);
DWORD WINAPI MprAdminMIBGetTrapInfo(MIB_SERVER_HANDLE hMibServer, DWORD dwProtocolId, DWORD dwRoutingPid, LPVOID lpInData, DWORD dwInDataSize, LPVOID *lplpOutData, LPDWORD lpOutDataSize);
DWORD WINAPI MprAdminMIBServerConnect(LPWSTR lpwsServerName, MIB_SERVER_HANDLE *phMibServer);
void MprAdminMIBServerDisconnect(MIB_SERVER_HANDLE hMibServer);
DWORD WINAPI MprAdminMIBSetTrapInfo(DWORD dwProtocolId, DWORD dwRoutingPid, HANDLE hEvent, LPVOID lpInData, DWORD dwInDataSize, LPVOID *lplpOutData, LPDWORD lpOutDataSize);
DWORD WINAPI MprAdminPortClearStats(RAS_SERVER_HANDLE hRasServer, HANDLE hPort);
DWORD WINAPI MprAdminPortDisconnect(RAS_SERVER_HANDLE hRasServer, HANDLE hPort);
DWORD WINAPI MprAdminPortEnum(RAS_SERVER_HANDLE hRasServer, DWORD dwLevel, HANDLE hRasConnection, LPBYTE *lplpbBuffer, DWORD dwPrefMaxLen, LPDWORD lpdwEntriesRead, LPDWORD lpdwTotalEntries, LPDWORD lpdwResumeHandle);
DWORD WINAPI MprAdminPortGetInfo(RAS_SERVER_HANDLE hRasServer, DWORD dwLevel, HANDLE hPort, LPBYTE *lplpbBuffer);
DWORD WINAPI MprAdminPortReset(RAS_SERVER_HANDLE hRasServer, HANDLE hPort);
DWORD WINAPI MprAdminRegisterConnectionNotification(MPR_SERVER_HANDLE hMprServer, HANDLE hEventNotification);
void MprAdminReleaseIpAddress(WCHAR *lpszUserName, WCHAR *lpszPortName, DWORD *lpdwIpAddress);
void MprAdminReleaseIpv6AddressForUser(WCHAR *lpszUserName, WCHAR *lpszPortName, IN6_ADDR *lpdwIpv6Address);
DWORD WINAPI MprAdminSendUserMessage(MPR_SERVER_HANDLE hMprServer, HANDLE hConnection, LPWSTR lpwszMessage);
DWORD WINAPI MprAdminServerConnect(LPWSTR lpwsServerName, MPR_SERVER_HANDLE *phMprServer);
void MprAdminServerDisconnect(MPR_SERVER_HANDLE hMprServer);
DWORD WINAPI MprAdminServerGetCredentials(MPR_SERVER_HANDLE hMprServer, DWORD dwLevel, LPBYTE *lplpbBuffer);
DWORD WINAPI MprAdminServerGetInfo(MPR_SERVER_HANDLE hMprServer, DWORD dwLevel, LPBYTE *lplpbBuffer);
DWORD WINAPI MprAdminServerGetInfoEx(MPR_SERVER_HANDLE hMprServer, MPR_SERVER_EX1 *pServerInfo);
DWORD WINAPI MprAdminServerSetCredentials(MPR_SERVER_HANDLE hMprServer, DWORD dwLevel, LPBYTE lpbBuffer);
DWORD WINAPI MprAdminServerSetInfo(MPR_SERVER_HANDLE hMprServer, DWORD dwLevel, LPBYTE lpbBuffer);
DWORD WINAPI MprAdminServerSetInfoEx(MPR_SERVER_HANDLE hMprServer, MPR_SERVER_SET_CONFIG_EX1 *pServerInfo);
DWORD WINAPI MprAdminTerminateDll(void);
DWORD WINAPI MprAdminTransportCreate(MPR_SERVER_HANDLE hMprServer, DWORD dwTransportId, LPWSTR lpwsTransportName, LPBYTE pGlobalInfo, DWORD dwGlobalInfoSize, LPBYTE pClientInterfaceInfo, DWORD dwClientInterfaceInfoSize, LPWSTR lpwsDLLPath);
DWORD WINAPI MprAdminTransportGetInfo(MPR_SERVER_HANDLE hMprServer, DWORD dwTransportId, LPBYTE *ppGlobalInfo, LPDWORD lpdwGlobalInfoSize, LPBYTE *ppClientInterfaceInfo, LPDWORD lpdwClientInterfaceInfoSize);
DWORD WINAPI MprAdminTransportSetInfo(MPR_SERVER_HANDLE hMprServer, DWORD dwTransportId, LPBYTE pGlobalInfo, DWORD dwGlobalInfoSize, LPBYTE pClientInterfaceInfo, DWORD dwClientInterfaceInfoSize);
DWORD WINAPI MprAdminUpdateConnection(RAS_SERVER_HANDLE hRasServer, HANDLE hRasConnection, PRAS_UPDATE_CONNECTION pRasUpdateConnection);
DWORD WINAPI MprAdminUserGetInfo(LPCWSTR lpszServer, LPCWSTR lpszUser, DWORD dwLevel, LPBYTE lpbBuffer);
DWORD WINAPI MprAdminUserSetInfo(LPCWSTR lpszServer, LPCWSTR lpszUser, DWORD dwLevel, const LPBYTE lpbBuffer);
DWORD WINAPI MprConfigBufferFree(LPVOID pBuffer);
DWORD WINAPI MprConfigFilterGetInfo(HANDLE hMprConfig, DWORD dwLevel, DWORD dwTransportId, LPBYTE lpBuffer);
DWORD WINAPI MprConfigFilterSetInfo(HANDLE hMprConfig, DWORD dwLevel, DWORD dwTransportId, LPBYTE lpBuffer);
DWORD WINAPI MprConfigGetFriendlyName(HANDLE hMprConfig, PWSTR pszGuidName, PWCHAR pszBuffer, DWORD dwBufferSize);
DWORD WINAPI MprConfigGetGuidName(HANDLE hMprConfig, PWSTR pszFriendlyName, PWCHAR pszBuffer, DWORD dwBufferSize);
DWORD WINAPI MprConfigInterfaceCreate(HANDLE hMprConfig, DWORD dwLevel, LPBYTE lpbBuffer, HANDLE *phRouterInterface);
DWORD WINAPI MprConfigInterfaceDelete(HANDLE hMprConfig, HANDLE hRouterInterface);
DWORD WINAPI MprConfigInterfaceEnum(HANDLE hMprConfig, DWORD dwLevel, LPBYTE *lplpBuffer, DWORD dwPrefMaxLen, LPDWORD lpdwEntriesRead, LPDWORD lpdwTotalEntries, LPDWORD lpdwResumeHandle);
DWORD WINAPI MprConfigInterfaceGetCustomInfoEx(HANDLE hMprConfig, HANDLE hRouterInterface, PMPR_IF_CUSTOMINFOEX2 pCustomInfo);
DWORD WINAPI MprConfigInterfaceGetHandle(HANDLE hMprConfig, LPWSTR lpwsInterfaceName, HANDLE *phRouterInterface);
DWORD WINAPI MprConfigInterfaceGetInfo(HANDLE hMprConfig, HANDLE hRouterInterface, DWORD dwLevel, LPBYTE *lplpBuffer, LPDWORD lpdwBufferSize);
DWORD WINAPI MprConfigInterfaceSetCustomInfoEx(HANDLE hMprConfig, HANDLE hRouterInterface, PMPR_IF_CUSTOMINFOEX2 pCustomInfo);
DWORD WINAPI MprConfigInterfaceSetInfo(HANDLE hMprConfig, HANDLE hRouterInterface, DWORD dwLevel, LPBYTE lpbBuffer);
DWORD WINAPI MprConfigInterfaceTransportAdd(HANDLE hMprConfig, HANDLE hRouterInterface, DWORD dwTransportId, LPWSTR lpwsTransportName, LPBYTE pInterfaceInfo, DWORD dwInterfaceInfoSize, HANDLE *phRouterIfTransport);
DWORD WINAPI MprConfigInterfaceTransportEnum(HANDLE hMprConfig, HANDLE hRouterInterface, DWORD dwLevel, LPBYTE *lplpBuffer, DWORD dwPrefMaxLen, LPDWORD lpdwEntriesRead, LPDWORD lpdwTotalEntries, LPDWORD lpdwResumeHandle);
DWORD WINAPI MprConfigInterfaceTransportGetHandle(HANDLE hMprConfig, HANDLE hRouterInterface, DWORD dwTransportId, HANDLE *phRouterIfTransport);
DWORD WINAPI MprConfigInterfaceTransportGetInfo(HANDLE hMprConfig, HANDLE hRouterInterface, HANDLE hRouterIfTransport, LPBYTE *ppInterfaceInfo, LPDWORD lpdwInterfaceInfoSize);
DWORD WINAPI MprConfigInterfaceTransportRemove(HANDLE hMprConfig, HANDLE hRouterInterface, HANDLE hRouterIfTransport);
DWORD WINAPI MprConfigInterfaceTransportSetInfo(HANDLE hMprConfig, HANDLE hRouterInterface, HANDLE hRouterIfTransport, LPBYTE pInterfaceInfo, DWORD dwInterfaceInfoSize);
DWORD WINAPI MprConfigServerBackup(HANDLE hMprConfig, LPWSTR lpwsPath);
void MprConfigServerDisconnect(HANDLE hMprConfig);
DWORD WINAPI MprConfigServerGetInfo(HANDLE hMprConfig, DWORD dwLevel, LPBYTE *lplpbBuffer);
DWORD WINAPI MprConfigServerGetInfoEx(HANDLE hMprConfig, MPR_SERVER_EX1 *pServerInfo);
DWORD WINAPI MprConfigServerInstall(DWORD dwLevel, PVOID pBuffer);
DWORD WINAPI MprConfigServerRefresh(HANDLE hMprConfig);
DWORD WINAPI MprConfigServerRestore(HANDLE hMprConfig, LPWSTR lpwsPath);
DWORD WINAPI MprConfigServerSetInfo(MPR_SERVER_HANDLE hMprServer, DWORD dwLevel, LPBYTE lpbBuffer);
DWORD WINAPI MprConfigServerSetInfoEx(HANDLE hMprConfig, MPR_SERVER_SET_CONFIG_EX1 *pSetServerConfig);
DWORD WINAPI MprConfigTransportCreate(HANDLE hMprConfig, DWORD dwTransportId, LPWSTR lpwsTransportName, LPBYTE pGlobalInfo, DWORD dwGlobalInfoSize, LPBYTE pClientInterfaceInfo, DWORD dwClientInterfaceInfoSize, LPWSTR lpwsDLLPath, HANDLE *phRouterTransport);
DWORD WINAPI MprConfigTransportDelete(HANDLE hMprConfig, HANDLE hRouterTransport);
DWORD WINAPI MprConfigTransportEnum(HANDLE hMprConfig, DWORD dwLevel, LPBYTE *lplpBuffer, DWORD dwPrefMaxLen, LPDWORD lpdwEntriesRead, LPDWORD lpdwTotalEntries, LPDWORD lpdwResumeHandle);
DWORD WINAPI MprConfigTransportGetHandle(HANDLE hMprConfig, DWORD dwTransportId, HANDLE *phRouterTransport);
DWORD WINAPI MprConfigTransportGetInfo(HANDLE hMprConfig, HANDLE hRouterTransport, LPBYTE *ppGlobalInfo, LPDWORD lpdwGlobalInfoSize, LPBYTE *ppClientInterfaceInfo, LPDWORD lpdwClientInterfaceInfoSize, LPWSTR *lplpwsDLLPath);
DWORD WINAPI MprConfigTransportSetInfo(HANDLE hMprConfig, HANDLE hRouterTransport, LPBYTE pGlobalInfo, DWORD dwGlobalInfoSize, LPBYTE pClientInterfaceInfo, DWORD dwClientInterfaceInfoSize, LPWSTR lpwsDLLPath);
DWORD WINAPI MprInfoBlockAdd(LPVOID lpHeader, DWORD dwInfoType, DWORD dwItemSize, DWORD dwItemCount, LPBYTE lpItemData, LPVOID *lplpNewHeader);
DWORD WINAPI MprInfoBlockFind(LPVOID lpHeader, DWORD dwInfoType, LPDWORD lpdwItemSize, LPDWORD lpdwItemCount, LPBYTE *lplpItemData);
DWORD WINAPI MprInfoBlockQuerySize(LPVOID lpHeader);
DWORD WINAPI MprInfoBlockRemove(LPVOID lpHeader, DWORD dwInfoType, LPVOID *lplpNewHeader);
DWORD WINAPI MprInfoBlockSet(LPVOID lpHeader, DWORD dwInfoType, DWORD dwItemSize, DWORD dwItemCount, LPBYTE lpItemData, LPVOID *lplpNewHeader);
DWORD WINAPI MprInfoCreate(DWORD dwVersion, LPVOID *lplpNewHeader);
DWORD WINAPI MprInfoDelete(LPVOID lpHeader);
DWORD WINAPI MprInfoDuplicate(LPVOID lpHeader, LPVOID *lplpNewHeader);
DWORD WINAPI MprInfoRemoveAll(LPVOID lpHeader, LPVOID *lplpNewHeader);
DWORD WINAPI MprSetupIpInIpInterfaceFriendlyNameCreate(PWCHAR pwszMachineName, PMPR_IPINIP_INTERFACE_0 pNameInformation);
DWORD WINAPI MprSetupIpInIpInterfaceFriendlyNameDelete(PWCHAR pwszMachineName, GUID *pGuid);
DWORD WINAPI MprSetupIpInIpInterfaceFriendlyNameEnum(PWCHAR pwszMachineName, LPBYTE *lplpBuffer, LPDWORD lpdwEntriesRead);
DWORD WINAPI MprSetupIpInIpInterfaceFriendlyNameFree(LPVOID lpBuffer);

#ifdef __cplusplus
}
#endif


enum _RAS_PORT_CONDITION
{

};

typedef enum _RAS_PORT_CONDITION RAS_PORT_CONDITION;

#define MprInfoBlockExists(h, t) (MprInfoBlockFind((h), (t), NULL, NULL, NULL) == NO_ERROR)

#endif /* __WINE_MPRAPI_H */
