/* WINE iprtrmib.h
 * Copyright (C) 2003 Juan Lang
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
#ifndef WINE_IPRTRMIB_H__
#define WINE_IPRTRMIB_H__

#include <mprapi.h>
#include <ipifcons.h>

#include <ipmib.h>
#include <tcpmib.h>
#include <udpmib.h>

typedef enum _TCP_TABLE_CLASS
{
    TCP_TABLE_BASIC_LISTENER,
    TCP_TABLE_BASIC_CONNECTIONS,
    TCP_TABLE_BASIC_ALL,
    TCP_TABLE_OWNER_PID_LISTENER,
    TCP_TABLE_OWNER_PID_CONNECTIONS,
    TCP_TABLE_OWNER_PID_ALL,
    TCP_TABLE_OWNER_MODULE_LISTENER,
    TCP_TABLE_OWNER_MODULE_CONNECTIONS,
    TCP_TABLE_OWNER_MODULE_ALL
} TCP_TABLE_CLASS, *PTCP_TABLE_CLASS;

typedef enum _UDP_TABLE_CLASS
{
    UDP_TABLE_BASIC,
    UDP_TABLE_OWNER_PID,
    UDP_TABLE_OWNER_MODULE
} UDP_TABLE_CLASS, *PUDP_TABLE_CLASS;

/* ReactOS */
typedef enum _TCPIP_OWNER_MODULE_INFO_CLASS
{
    TCPIP_OWNER_MODULE_INFO_BASIC
} TCPIP_OWNER_MODULE_INFO_CLASS, *PTCPIP_OWNER_MODULE_INFO_CLASS;

typedef struct _TCPIP_OWNER_MODULE_BASIC_INFO {
    PWCHAR pModuleName;
    PWCHAR pModulePath;
} TCPIP_OWNER_MODULE_BASIC_INFO, *PTCPIP_OWNER_MODULE_BASIC_INFO;

#ifndef IF_NUMBER
#define IF_NUMBER 0
#endif
#ifndef IPRTRMGR_PID
#define IPRTRMGR_PID 10000
#endif
#ifndef MAX_MIB_OFFSET
#define MAX_MIB_OFFSET 8
#endif
#ifndef MAX_SCOPE_NAME_LEN
#define MAX_SCOPE_NAME_LEN 255
#endif

typedef WCHAR SN_CHAR;
typedef SN_CHAR * SCOPE_NAME;

#define BEST_IF 0x14
#define BEST_ROUTE 0x15
#define CAST_MIB_INFOX, Y, Z) Z = (Y) (X -> rgbyData)
#define DEST_LONGER 0x1D
#define DEST_MATCHING 0x1C
#define DEST_SHORTER 0x1E
#define ICMP_STATS 0xB
#define IF_ROW 2
#define IF_STATUS 0x19
#define IF_TABLE 1
#define IP6_STATS 0x24
#define IP_ADDRROW 5
#define IP_ADDRTABLE 4
#define IP_FORWARDNUMBER 6
#define IP_FORWARDROW 8
#define IP_FORWARDTABLE 7
#define IP_NETROW 0xA
#define IP_NETTABLE 9
#define IP_STATS 3
#define MCAST_BOUNDARY 0x1A
#define MCAST_GLOBAL 0x18
#define MCAST_IF_ENTRY 0x17
#define MCAST_MFE 0x12
#define MCAST_MFE_STATS 0x13
#define MCAST_MFE_STATS_EX 0x23
#define MCAST_SCOPE 0x1B
#define MIB_INFO_SIZES) (MAX_MIB_OFFSET + sizeof (S))
#define MIB_INFO_SIZE_IN_DWORDSS) ((MIB_INFO_SIZE(S)) / sizeof (DWORD) + 1)
#define NUMBER_OF_EXPORTED_VARIABLES 0x27
#define PROXY_ARP 0x16
#define ROUTE_LONGER 0x20
#define ROUTE_MATCHING 0x1F
#define ROUTE_SHORTER 0x21
#define ROUTE_STATE 0x22
#define SN_UNICODE 0
#define TCP6_STATS 0x26
#define TCP_ROW 0xE
#define TCP_STATS 0xC
#define TCP_TABLE 0xD
#define UDP6_STATS 0x25
#define UDP_ROW 0x11
#define UDP_STATS 0xF
#define UDP_TABLE 0x10

struct _MIB_IPDESTROW
{
    DWORD dwForwardPreference;
    DWORD dwForwardViewSet;
};

struct _MIB_OPAQUE_INFO
{
    DWORD dwId;
    union
    {
        ULONGLONG ullAlign;
        BYTE rgbyData[1];
    };
};

typedef struct _MIB_IPDESTROW MIB_IPDESTROW;

struct _MIB_IPDESTTABLE
{
    DWORD dwNumEntries;
    MIB_IPDESTROW table[1];
};

typedef struct _MIB_IPDESTTABLE MIB_IPDESTTABLE;

typedef struct _MIB_OPAQUE_INFO MIB_OPAQUE_INFO;

typedef struct _MIB_IPDESTROW *PMIB_IPDESTROW;

typedef struct _MIB_IPDESTTABLE *PMIB_IPDESTTABLE;

typedef struct _MIB_OPAQUE_INFO *PMIB_OPAQUE_INFO;

typedef SN_CHAR SCOPE_NAME_BUFFER[256];

struct _MIB_IPMCAST_SCOPE
{
    DWORD dwGroupAddress;
    DWORD dwGroupMask;
    SCOPE_NAME_BUFFER snNameBuffer;
    DWORD dwStatus;
};

typedef struct _MIB_IPMCAST_SCOPE MIB_IPMCAST_SCOPE;

typedef struct _MIB_IPMCAST_SCOPE *PMIB_IPMCAST_SCOPE;

#define CAST_MIB_INFOX, Y, Z) Z = (Y) (X -> rgbyData)
#define MIB_INFO_SIZE_IN_DWORDSS) ((MIB_INFO_SIZE(S)) / sizeof (DWORD) + 1)

#define CAST_MIB_INFOX, Y, Z) Z = (Y) (X -> rgbyData)
#define MIB_INFO_SIZE_IN_DWORDSS) ((MIB_INFO_SIZE(S)) / sizeof (DWORD) + 1)
#define SIZEOF_BOUNDARY_TABLEX) (FIELD_OFFSET(MIB_IPMCAST_BOUNDARY_TABLE, table [0]) + ((X) * sizeof (MIB_IPMCAST_BOUNDARY)) + ALIGN_SIZE)

struct _MIB_BEST_IF
{
    DWORD dwDestAddr;
    DWORD dwIfIndex;
};

struct _MIB_IFSTATUS
{
    DWORD dwIfIndex;
    DWORD dwAdminStatus;
    DWORD dwOperationalStatus;
    BOOL bMHbeatActive;
    BOOL bMHbeatAlive;
};

struct _MIB_IPMCAST_BOUNDARY
{
    DWORD dwIfIndex;
    DWORD dwGroupAddress;
    DWORD dwGroupMask;
    DWORD dwStatus;
};

struct _MIB_OPAQUE_QUERY
{
    DWORD dwVarId;
    DWORD rgdwVarIndex[1];
};

struct _MIB_PROXYARP
{
    DWORD dwAddress;
    DWORD dwMask;
    DWORD dwIfIndex;
};

struct _MIB_ROUTESTATE
{
    BOOL bRoutesSetToStack;
};

typedef struct _MIB_BEST_IF MIB_BEST_IF;

struct MIB_BOUNDARYROW
{
    DWORD dwGroupAddress;
    DWORD dwGroupMask;
};

typedef struct _MIB_IFSTATUS MIB_IFSTATUS;

typedef struct _MIB_IPMCAST_BOUNDARY MIB_IPMCAST_BOUNDARY;

struct _MIB_IPMCAST_BOUNDARY_TABLE
{
    DWORD dwNumEntries;
    MIB_IPMCAST_BOUNDARY table[1];
};

typedef struct _MIB_IPMCAST_BOUNDARY_TABLE MIB_IPMCAST_BOUNDARY_TABLE;

struct MIB_MCAST_LIMIT_ROW
{
    DWORD dwTtl;
    DWORD dwRateLimit;
};

typedef struct _MIB_OPAQUE_QUERY MIB_OPAQUE_QUERY;

typedef struct _MIB_PROXYARP MIB_PROXYARP;

typedef struct _MIB_ROUTESTATE MIB_ROUTESTATE;

typedef struct _MIB_BEST_IF *PMIB_BEST_IF;

typedef struct MIB_BOUNDARYROW *PMIB_BOUNDARYROW;

typedef struct _MIB_IFSTATUS *PMIB_IFSTATUS;

typedef struct _MIB_IPMCAST_BOUNDARY *PMIB_IPMCAST_BOUNDARY;

typedef struct _MIB_IPMCAST_BOUNDARY_TABLE *PMIB_IPMCAST_BOUNDARY_TABLE;

typedef struct MIB_MCAST_LIMIT_ROW *PMIB_MCAST_LIMIT_ROW;

typedef struct _MIB_OPAQUE_QUERY *PMIB_OPAQUE_QUERY;

typedef struct _MIB_PROXYARP *PMIB_PROXYARP;

typedef struct _MIB_ROUTESTATE *PMIB_ROUTESTATE;

#endif /* WINE_IPRTRMIB_H__ */
