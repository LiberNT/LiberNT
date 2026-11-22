#ifndef _NSPAPI_H
#define _NSPAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#define NS_ALL		0

#define NS_SAP		1
#define NS_NDS		2
#define NS_PEER_BROWSE	3

#define NS_TCPIP_LOCAL	10
#define NS_TCPIP_HOSTS	11
#define NS_DNS		12
#define NS_NETBT	13
#define NS_WINS		14

#define NS_NBP		20

#define NS_MS		30
#define NS_STDA		31
#define NS_NTDS		32

#define NS_X500		40
#define NS_NIS		41
#define NS_NISPLUS	42

#define NS_WRQ		50

#define SERVICE_REGISTER	1
#define SERVICE_DEREGISTER	2
#define SERVICE_FLUSH		3
#define SERVICE_FLAG_HARD	0x00000002

#ifndef RC_INVOKED

#if defined (_WINSOCKAPI_) || defined (_WINSOCK2API_) /* needed for LPSOCKADDR */
#ifndef __CSADDR_DEFINED__ /* also in winsock2.h, but not in winsock.h */
#define __CSADDR_DEFINED__
typedef struct _SOCKET_ADDRESS {
	LPSOCKADDR lpSockaddr;
	INT iSockaddrLength;
} SOCKET_ADDRESS,*PSOCKET_ADDRESS,*LPSOCKET_ADDRESS;
typedef struct _CSADDR_INFO {
	SOCKET_ADDRESS LocalAddr;
	SOCKET_ADDRESS RemoteAddr;
	INT iSocketType;
	INT iProtocol;
} CSADDR_INFO,*PCSADDR_INFO,*LPCSADDR_INFO;
#endif
#endif

#ifndef __BLOB_T_DEFINED /* also in wtypes.h and winsock2.h */
#define __BLOB_T_DEFINED
typedef struct _BLOB {
	ULONG	cbSize;
	BYTE	*pBlobData;
} BLOB,*PBLOB,*LPBLOB;
#endif

typedef struct _SERVICE_ADDRESS {
	DWORD dwAddressType;
	DWORD dwAddressFlags;
	DWORD dwAddressLength;
	DWORD dwPrincipalLength;
	BYTE *lpAddress;
	BYTE *lpPrincipal;
} SERVICE_ADDRESS;
typedef struct _SERVICE_ADDRESSES {
	DWORD dwAddressCount;
	SERVICE_ADDRESS Addresses[1];
} SERVICE_ADDRESSES, *PSERVICE_ADDRESSES, *LPSERVICE_ADDRESSES;
typedef struct _SERVICE_INFOA {
	LPGUID lpServiceType;
	LPSTR lpServiceName;
	LPSTR lpComment;
	LPSTR lpLocale;
	DWORD dwDisplayHint;
	DWORD dwVersion;
	DWORD dwTime;
	LPSTR lpMachineName;
	LPSERVICE_ADDRESSES lpServiceAddress;
	BLOB ServiceSpecificInfo;
} SERVICE_INFOA, *LPSERVICE_INFOA;
typedef struct _SERVICE_INFOW {
	LPGUID lpServiceType;
	LPWSTR lpServiceName;
	LPWSTR lpComment;
	LPWSTR lpLocale;
	DWORD dwDisplayHint;
	DWORD dwVersion;
	DWORD dwTime;
	LPWSTR lpMachineName;
	LPSERVICE_ADDRESSES lpServiceAddress;
	BLOB ServiceSpecificInfo;
} SERVICE_INFOW, *LPSERVICE_INFOW;

typedef void *LPSERVICE_ASYNC_INFO;
INT WINAPI SetServiceA(DWORD,DWORD,DWORD,LPSERVICE_INFOA,LPSERVICE_ASYNC_INFO,LPDWORD);
INT WINAPI SetServiceW(DWORD,DWORD,DWORD,LPSERVICE_INFOW,LPSERVICE_ASYNC_INFO,LPDWORD);
INT WINAPI GetAddressByNameA(DWORD,LPGUID,LPSTR,LPINT,DWORD,LPSERVICE_ASYNC_INFO,LPVOID,LPDWORD,LPSTR,LPDWORD);
INT WINAPI GetAddressByNameW(DWORD,LPGUID,LPWSTR,LPINT,DWORD,LPSERVICE_ASYNC_INFO,LPVOID,LPDWORD,LPWSTR,LPDWORD);

#ifdef UNICODE
typedef SERVICE_INFOW SERVICE_INFO, *LPSERVICE_INFO;
#define _SERVICE_INFO SERVICE_INFOW
#define SetService SetServiceW
#define GetAddressByName GetAddressByNameW
#else
typedef SERVICE_INFOA SERVICE_INFO, *LPSERVICE_INFO;
#define _SERVICE_INFO SERVICE_INFOA
#define SetService SetServiceA
#define GetAddressByName GetAddressByNameA
#endif

#endif /* RC_INVOKED */

#ifdef __cplusplus
}
#endif
#ifndef NSTYPE_DYNAMIC
#define NSTYPE_DYNAMIC 0x00000002
#endif
#ifndef NSTYPE_ENUMERABLE
#define NSTYPE_ENUMERABLE 0x00000004
#endif
#ifndef NSTYPE_HIERARCHICAL
#define NSTYPE_HIERARCHICAL 0x00000001
#endif
#ifndef NSTYPE_WORKGROUP
#define NSTYPE_WORKGROUP 0x00000008
#endif
#ifndef NS_DEFAULT
#define NS_DEFAULT 0
#endif
#ifndef NS_VNS
#define NS_VNS 50
#endif
#ifndef PROP_ADDRESSES
#define PROP_ADDRESSES 0x00000100
#endif
#ifndef PROP_ALL
#define PROP_ALL 0x80000000
#endif
#ifndef PROP_COMMENT
#define PROP_COMMENT 0x00000001
#endif
#ifndef PROP_DISPLAY_HINT
#define PROP_DISPLAY_HINT 0x00000004
#endif
#ifndef PROP_LOCALE
#define PROP_LOCALE 0x00000002
#endif
#ifndef PROP_MACHINE
#define PROP_MACHINE 0x00000020
#endif
#ifndef PROP_SD
#define PROP_SD 0x00000200
#endif
#ifndef PROP_START_TIME
#define PROP_START_TIME 0x00000010
#endif
#ifndef PROP_VERSION
#define PROP_VERSION 0x00000008
#endif
#ifndef RES_FIND_MULTIPLE
#define RES_FIND_MULTIPLE 0x00000002
#endif
#ifndef RES_SOFT_SEARCH
#define RES_SOFT_SEARCH 0x00000001
#endif
#ifndef SERVICE_ADDRESS_FLAG_RPC_CN
#define SERVICE_ADDRESS_FLAG_RPC_CN 0x00000001
#endif
#ifndef SERVICE_ADDRESS_FLAG_RPC_DG
#define SERVICE_ADDRESS_FLAG_RPC_DG 0x00000002
#endif
#ifndef SERVICE_ADDRESS_FLAG_RPC_NB
#define SERVICE_ADDRESS_FLAG_RPC_NB 0x00000004
#endif
#ifndef SERVICE_ADD_TYPE
#define SERVICE_ADD_TYPE 0x00000004
#endif
#ifndef SERVICE_DELETE_TYPE
#define SERVICE_DELETE_TYPE 0x00000005
#endif
#ifndef SERVICE_FLAG_DEFER
#define SERVICE_FLAG_DEFER 0x00000001
#endif
#ifndef SERVICE_LOCAL
#define SERVICE_LOCAL 0x00000004
#endif
#ifndef SERVICE_RESOURCE
#define SERVICE_RESOURCE 0x00000001
#endif
#ifndef SERVICE_SERVICE
#define SERVICE_SERVICE 0x00000002
#endif
#ifndef XP_BANDWIDTH_ALLOCATION
#define XP_BANDWIDTH_ALLOCATION 0x00000800
#endif
#ifndef XP_CONNECTIONLESS
#define XP_CONNECTIONLESS 0x00000001
#endif
#ifndef XP_CONNECT_DATA
#define XP_CONNECT_DATA 0x00000080
#endif
#ifndef XP_DISCONNECT_DATA
#define XP_DISCONNECT_DATA 0x00000100
#endif
#ifndef XP_ENCRYPTS
#define XP_ENCRYPTS 0x00002000
#endif
#ifndef XP_EXPEDITED_DATA
#define XP_EXPEDITED_DATA 0x00000040
#endif
#ifndef XP_FRAGMENTATION
#define XP_FRAGMENTATION 0x00001000
#endif
#ifndef XP_GRACEFUL_CLOSE
#define XP_GRACEFUL_CLOSE 0x00000020
#endif
#ifndef XP_GUARANTEED_DELIVERY
#define XP_GUARANTEED_DELIVERY 0x00000002
#endif
#ifndef XP_GUARANTEED_ORDER
#define XP_GUARANTEED_ORDER 0x00000004
#endif
#ifndef XP_MESSAGE_ORIENTED
#define XP_MESSAGE_ORIENTED 0x00000008
#endif
#ifndef XP_PSEUDO_STREAM
#define XP_PSEUDO_STREAM 0x00000010
#endif
#ifndef XP_SUPPORTS_BROADCAST
#define XP_SUPPORTS_BROADCAST 0x00000200
#endif
#ifndef XP_SUPPORTS_MULTICAST
#define XP_SUPPORTS_MULTICAST 0x00000400
#endif

typedef struct _SERVICE_ADDRESS * LPSERVICE_ADDRESS;
typedef struct _SERVICE_ADDRESS * PSERVICE_ADDRESS;
typedef struct _SERVICE_INFOA * PSERVICE_INFOA;
#if (!(defined(UNICODE)))
typedef PSERVICE_INFOA PSERVICE_INFO;
#endif
typedef struct _SERVICE_INFOW * PSERVICE_INFOW;

#ifdef __cplusplus
extern "C" {
#endif
INT WINAPI EnumProtocolsA(LPINT lpiProtocols, LPVOID lpProtocolBuffer, LPDWORD lpdwBufferLength);
INT WINAPI EnumProtocolsW(LPINT lpiProtocols, LPVOID lpProtocolBuffer, LPDWORD lpdwBufferLength);
INT WINAPI GetTypeByNameA(LPSTR lpServiceName, LPGUID lpServiceType);
INT WINAPI GetTypeByNameW(LPWSTR lpServiceName, LPGUID lpServiceType);
#ifdef __cplusplus
}
#endif

typedef struct _NS_INFOA {
  DWORD dwNameSpace;
  DWORD dwNameSpaceFlags;
  LPSTR lpNameSpace;
} NS_INFOA, *PNS_INFOA, *LPNS_INFOA;

typedef struct _NS_INFOW {
  DWORD dwNameSpace;
  DWORD dwNameSpaceFlags;
  LPWSTR lpNameSpace;
} NS_INFOW, *PNS_INFOW, *LPNS_INFOW;

typedef struct _NS_SERVICE_INFOA {
  DWORD dwNameSpace;
  SERVICE_INFOA ServiceInfo;
} NS_SERVICE_INFOA, *PNS_SERVICE_INFOA, *LPNS_SERVICE_INFOA;

typedef struct _NS_SERVICE_INFOW {
  DWORD dwNameSpace;
  SERVICE_INFOW ServiceInfo;
} NS_SERVICE_INFOW, *PNS_SERVICE_INFOW, *LPNS_SERVICE_INFOW;

typedef struct _PROTOCOL_INFOA {
  DWORD dwServiceFlags;
  INT iAddressFamily;
  INT iMaxSockAddr;
  INT iMinSockAddr;
  INT iSocketType;
  INT iProtocol;
  DWORD dwMessageSize;
  LPSTR lpProtocol;
} PROTOCOL_INFOA, *PPROTOCOL_INFOA, *LPPROTOCOL_INFOA;

typedef struct _PROTOCOL_INFOW {
  DWORD dwServiceFlags;
  INT iAddressFamily;
  INT iMaxSockAddr;
  INT iMinSockAddr;
  INT iSocketType;
  INT iProtocol;
  DWORD dwMessageSize;
  LPWSTR lpProtocol;
} PROTOCOL_INFOW, *PPROTOCOL_INFOW, *LPPROTOCOL_INFOW;

typedef struct _SERVICE_TYPE_VALUE {
  DWORD dwNameSpace;
  DWORD dwValueType;
  DWORD dwValueSize;
  DWORD dwValueNameOffset;
  DWORD dwValueOffset;
} SERVICE_TYPE_VALUE, *PSERVICE_TYPE_VALUE, *LPSERVICE_TYPE_VALUE;

typedef struct _SERVICE_TYPE_INFO {
  DWORD dwTypeNameOffset;
  DWORD dwValueCount;
  SERVICE_TYPE_VALUE Values[1];
} SERVICE_TYPE_INFO, *PSERVICE_TYPE_INFO, *LPSERVICE_TYPE_INFO;

typedef struct _SERVICE_TYPE_VALUE_ABSA {
  DWORD dwNameSpace;
  DWORD dwValueType;
  DWORD dwValueSize;
  LPSTR lpValueName;
  PVOID lpValue;
} SERVICE_TYPE_VALUE_ABSA, *PSERVICE_TYPE_VALUE_ABSA, *LPSERVICE_TYPE_VALUE_ABSA;

typedef struct _SERVICE_TYPE_INFO_ABSA {
  LPSTR lpTypeName;
  DWORD dwValueCount;
  SERVICE_TYPE_VALUE_ABSA Values[1];
} SERVICE_TYPE_INFO_ABSA, *PSERVICE_TYPE_INFO_ABSA, *LPSERVICE_TYPE_INFO_ABSA;

typedef struct _SERVICE_TYPE_VALUE_ABSW {
  DWORD dwNameSpace;
  DWORD dwValueType;
  DWORD dwValueSize;
  LPWSTR lpValueName;
  PVOID lpValue;
} SERVICE_TYPE_VALUE_ABSW, *PSERVICE_TYPE_VALUE_ABSW, *LPSERVICE_TYPE_VALUE_ABSW;

typedef struct _SERVICE_TYPE_INFO_ABSW {
  LPWSTR lpTypeName;
  DWORD dwValueCount;
  SERVICE_TYPE_VALUE_ABSW Values[1];
} SERVICE_TYPE_INFO_ABSW, *PSERVICE_TYPE_INFO_ABSW, *LPSERVICE_TYPE_INFO_ABSW;

#if (!(defined(UNICODE)))
#ifdef UNICODE
#define EnumProtocols EnumProtocolsW
#else
#define EnumProtocols EnumProtocolsA
#endif
#endif
#ifdef UNICODE
#define GetNameByType GetNameByTypeW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define GetNameByType GetNameByTypeW
#else
#define GetNameByType GetNameByTypeA
#endif
#endif
#endif
#ifdef UNICODE
#define GetTypeByName GetTypeByNameW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define GetTypeByName GetTypeByNameW
#else
#define GetTypeByName GetTypeByNameA
#endif
#endif
#endif
#ifdef UNICODE
#define SERVICE_TYPE_VALUE_CONN SERVICE_TYPE_VALUE_CONNW
#else
#if (!(defined(UNICODE)))
#ifdef UNICODE
#define SERVICE_TYPE_VALUE_CONN SERVICE_TYPE_VALUE_CONNW
#else
#define SERVICE_TYPE_VALUE_CONN SERVICE_TYPE_VALUE_CONNA
#endif
#endif
#endif
#define SERVICE_TYPE_VALUE_CONNA "ConnectionOriented"
#define SERVICE_TYPE_VALUE_CONNW L"ConnectionOriented"
#define SET_SERVICE_PARTIAL_SUCCESS (0x00000001)

#if (!(defined(UNICODE)))
typedef LPNS_INFOA LPNS_INFO;

typedef LPNS_SERVICE_INFOA LPNS_SERVICE_INFO;

typedef LPPROTOCOL_INFOA LPPROTOCOL_INFO;
#endif

typedef void (*LPSERVICE_CALLBACK_PROC)(LPARAM, HANDLE);

struct _SERVICE_ASYNC_INFO
{
    LPSERVICE_CALLBACK_PROC lpServiceCallbackProc;
    LPARAM lParam;
    HANDLE hAsyncTaskHandle;
};

#if (!(defined(UNICODE)))
typedef LPSERVICE_TYPE_INFO_ABSA LPSERVICE_TYPE_INFO_ABS;

typedef LPSERVICE_TYPE_VALUE_ABSA LPSERVICE_TYPE_VALUE_ABS;

typedef NS_INFOA NS_INFO;
#endif

struct _NETRESOURCE2A
{
    DWORD dwScope;
    DWORD dwType;
    DWORD dwUsage;
    DWORD dwDisplayType;
    LPSTR lpLocalName;
    LPSTR lpRemoteName;
    LPSTR lpComment;
    NS_INFO ns_info;
    GUID ServiceType;
    DWORD dwProtocols;
    LPINT lpiProtocols;
};

struct _NETRESOURCE2W
{
    DWORD dwScope;
    DWORD dwType;
    DWORD dwUsage;
    DWORD dwDisplayType;
    LPWSTR lpLocalName;
    LPWSTR lpRemoteName;
    LPWSTR lpComment;
    NS_INFO ns_info;
    GUID ServiceType;
    DWORD dwProtocols;
    LPINT lpiProtocols;
};

typedef struct _NETRESOURCE2A *LPNETRESOURCE2A;

#if (!(defined(UNICODE)))
typedef LPNETRESOURCE2A LPNETRESOURCE2;
#endif

typedef struct _NETRESOURCE2W *LPNETRESOURCE2W;

typedef struct _NETRESOURCE2A NETRESOURCE2A;

#if (!(defined(UNICODE)))
typedef NETRESOURCE2A NETRESOURCE2;
#endif

typedef struct _NETRESOURCE2W NETRESOURCE2W;

#if (!(defined(UNICODE)))
typedef NS_SERVICE_INFOA NS_SERVICE_INFO;
#endif

typedef struct _NETRESOURCE2A *PNETRESOURCE2A;

#if (!(defined(UNICODE)))
typedef PNETRESOURCE2A PNETRESOURCE2;
#endif

typedef struct _NETRESOURCE2W *PNETRESOURCE2W;

#if (!(defined(UNICODE)))
typedef PNS_INFOA PNS_INFO;

typedef PNS_SERVICE_INFOA PNS_SERVICE_INFO;

typedef PPROTOCOL_INFOA PPROTOCOL_INFO;

typedef PROTOCOL_INFOA PROTOCOL_INFO;
#endif

typedef struct _SERVICE_ASYNC_INFO *PSERVICE_ASYNC_INFO;

#if (!(defined(UNICODE)))
typedef PSERVICE_TYPE_INFO_ABSA PSERVICE_TYPE_INFO_ABS;

typedef PSERVICE_TYPE_VALUE_ABSA PSERVICE_TYPE_VALUE_ABS;
#endif

typedef struct _SERVICE_ASYNC_INFO SERVICE_ASYNC_INFO;

#if (!(defined(UNICODE)))
typedef SERVICE_TYPE_INFO_ABSA SERVICE_TYPE_INFO_ABS;

typedef SERVICE_TYPE_VALUE_ABSA SERVICE_TYPE_VALUE_ABS;
#endif

#ifdef __cplusplus
extern "C" {
#endif

INT WINAPI GetNameByTypeA(LPGUID lpServiceType, LPSTR lpServiceName, DWORD dwNameLength);
INT WINAPI GetNameByTypeW(LPGUID lpServiceType, LPWSTR lpServiceName, DWORD dwNameLength);
INT WINAPI GetServiceA(DWORD dwNameSpace, LPGUID lpGuid, LPSTR lpServiceName, DWORD dwProperties, LPVOID lpBuffer, LPDWORD lpdwBufferSize, LPSERVICE_ASYNC_INFO lpServiceAsyncInfo);
INT WINAPI GetServiceW(DWORD dwNameSpace, LPGUID lpGuid, LPWSTR lpServiceName, DWORD dwProperties, LPVOID lpBuffer, LPDWORD lpdwBufferSize, LPSERVICE_ASYNC_INFO lpServiceAsyncInfo);

#ifdef __cplusplus
}
#endif

#endif /* _NSPAPI_H */
