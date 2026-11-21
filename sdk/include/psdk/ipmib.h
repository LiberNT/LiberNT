/*
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
#ifndef __WINE_IPMIB_H
#define __WINE_IPMIB_H

#include <ifmib.h>
#include <nldef.h>

/* Flags used in the wType field from MIB_IPADDRROW */

#define MIB_IPADDR_PRIMARY 0x0001
#define MIB_IPADDR_DYNAMIC 0x0004
#define MIB_IPADDR_DISCONNECTED 0x0008
#define MIB_IPADDR_DELETED 0x0040
#define MIB_IPADDR_TRANSIENT 0x0080

/* IPADDR table */

typedef struct _MIB_IPADDRROW
{
    DWORD          dwAddr;
    IF_INDEX       dwIndex;
    DWORD          dwMask;
    DWORD          dwBCastAddr;
    DWORD          dwReasmSize;
    unsigned short unused1;
    unsigned short wType;
} MIB_IPADDRROW, *PMIB_IPADDRROW;

typedef struct _MIB_IPADDRTABLE
{
    DWORD         dwNumEntries;
    MIB_IPADDRROW table[1];
} MIB_IPADDRTABLE, *PMIB_IPADDRTABLE;


/* IPFORWARD table */

typedef struct _MIB_IPFORWARDNUMBER
{
    DWORD dwValue;
} MIB_IPFORWARDNUMBER, *PMIB_IPFORWARDNUMBER;

typedef enum
{
    MIB_IPROUTE_TYPE_OTHER = 1,
    MIB_IPROUTE_TYPE_INVALID = 2,
    MIB_IPROUTE_TYPE_DIRECT = 3,
    MIB_IPROUTE_TYPE_INDIRECT = 4,
} MIB_IPFORWARD_TYPE;

typedef NL_ROUTE_PROTOCOL MIB_IPFORWARD_PROTO;

typedef struct _MIB_IPFORWARDROW
{
    DWORD    dwForwardDest;
    DWORD    dwForwardMask;
    DWORD    dwForwardPolicy;
    DWORD    dwForwardNextHop;
    IF_INDEX dwForwardIfIndex;
    union
    {
        DWORD              dwForwardType;
        MIB_IPFORWARD_TYPE ForwardType;
    } DUMMYUNIONNAME1;
    union
    {
        DWORD               dwForwardProto;
        MIB_IPFORWARD_PROTO ForwardProto;
    } DUMMYUNIONNAME2;
    DWORD    dwForwardAge;
    DWORD    dwForwardNextHopAS;
    DWORD    dwForwardMetric1;
    DWORD    dwForwardMetric2;
    DWORD    dwForwardMetric3;
    DWORD    dwForwardMetric4;
    DWORD    dwForwardMetric5;
} MIB_IPFORWARDROW, *PMIB_IPFORWARDROW;

typedef struct _MIB_IPFORWARDTABLE
{
    DWORD            dwNumEntries;
    MIB_IPFORWARDROW table[1];
} MIB_IPFORWARDTABLE, *PMIB_IPFORWARDTABLE;


/* IPNET table */

typedef enum
{
    MIB_IPNET_TYPE_OTHER = 1,
    MIB_IPNET_TYPE_INVALID = 2,
    MIB_IPNET_TYPE_DYNAMIC = 3,
    MIB_IPNET_TYPE_STATIC = 4,
} MIB_IPNET_TYPE;

typedef struct _MIB_IPNETROW
{
    DWORD dwIndex;
    DWORD dwPhysAddrLen;
    BYTE  bPhysAddr[MAXLEN_PHYSADDR];
    DWORD dwAddr;
    union
    {
        DWORD          dwType;
        MIB_IPNET_TYPE Type;
    } DUMMYUNIONNAME;
} MIB_IPNETROW, *PMIB_IPNETROW;

typedef struct _MIB_IPNETTABLE
{
    DWORD        dwNumEntries;
    MIB_IPNETROW table[1];
} MIB_IPNETTABLE, *PMIB_IPNETTABLE;


/* IP statistics */

typedef enum
{
    MIB_IP_FORWARDING = 1,
    MIB_IP_NOT_FORWARDING = 2,
} MIB_IPSTATS_FORWARDING, *PMIB_IPSTATS_FORWARDING;

typedef struct _MIB_IPSTATS
{
    union
    {
        DWORD                  dwForwarding;
        MIB_IPSTATS_FORWARDING Forwarding;
    } DUMMYUNIONNAME;
    DWORD dwDefaultTTL;
    DWORD dwInReceives;
    DWORD dwInHdrErrors;
    DWORD dwInAddrErrors;
    DWORD dwForwDatagrams;
    DWORD dwInUnknownProtos;
    DWORD dwInDiscards;
    DWORD dwInDelivers;
    DWORD dwOutRequests;
    DWORD dwRoutingDiscards;
    DWORD dwOutDiscards;
    DWORD dwOutNoRoutes;
    DWORD dwReasmTimeout;
    DWORD dwReasmReqds;
    DWORD dwReasmOks;
    DWORD dwReasmFails;
    DWORD dwFragOks;
    DWORD dwFragFails;
    DWORD dwFragCreates;
    DWORD dwNumIf;
    DWORD dwNumAddr;
    DWORD dwNumRoutes;
} MIB_IPSTATS, *PMIB_IPSTATS;


/* ICMP statistics */

typedef struct _MIBICMPSTATS
{
    DWORD dwMsgs;
    DWORD dwErrors;
    DWORD dwDestUnreachs;
    DWORD dwTimeExcds;
    DWORD dwParmProbs;
    DWORD dwSrcQuenchs;
    DWORD dwRedirects;
    DWORD dwEchos;
    DWORD dwEchoReps;
    DWORD dwTimestamps;
    DWORD dwTimestampReps;
    DWORD dwAddrMasks;
    DWORD dwAddrMaskReps;
} MIBICMPSTATS, *PMIBICMPSTATS;

typedef struct _MIBICMPINFO
{
    MIBICMPSTATS icmpInStats;
    MIBICMPSTATS icmpOutStats;
} MIBICMPINFO;

typedef struct _MIB_ICMP
{
    MIBICMPINFO stats;
} MIB_ICMP, *PMIB_ICMP;

typedef enum
{
    ICMP4_ECHO_REPLY        =  0,
    ICMP4_DST_UNREACH       =  3,
    ICMP4_SOURCE_QUENCH     =  4,
    ICMP4_REDIRECT          =  5,
    ICMP4_ECHO_REQUEST      =  8,
    ICMP4_ROUTER_ADVERT     =  9,
    ICMP4_ROUTER_SOLICIT    = 10,
    ICMP4_TIME_EXCEEDED     = 11,
    ICMP4_PARAM_PROB        = 12,
    ICMP4_TIMESTAMP_REQUEST = 13,
    ICMP4_TIMESTAMP_REPLY   = 14,
    ICMP4_MASK_REQUEST      = 17,
    ICMP4_MASK_REPLY        = 18,
} ICMP4_TYPE, *PICMP4_TYPE;

typedef enum
{
    ICMP6_DST_UNREACH          =   1,
    ICMP6_PACKET_TOO_BIG       =   2,
    ICMP6_TIME_EXCEEDED        =   3,
    ICMP6_PARAM_PROB           =   4,
    ICMP6_ECHO_REQUEST         = 128,
    ICMP6_ECHO_REPLY           = 129,
    ICMP6_MEMBERSHIP_QUERY     = 130,
    ICMP6_MEMBERSHIP_REPORT    = 131,
    ICMP6_MEMBERSHIP_REDUCTION = 132,
    ND_ROUTER_SOLICIT          = 133,
    ND_ROUTER_ADVERT           = 134,
    ND_NEIGHBOR_SOLICIT        = 135,
    ND_NEIGHBOR_ADVERT         = 136,
    ND_REDIRECT                = 137,
    ICMP6_V2_MEMBERSHIP_REPORT = 143,
} ICMP6_TYPE, *PICMP6_TYPE;

typedef struct _MIBICMPSTATS_EX
{
    DWORD dwMsgs;
    DWORD dwErrors;
    DWORD rgdwTypeCount[256];
} MIBICMPSTATS_EX, *PMIBICMPSTATS_EX;

typedef struct _MIB_ICMP_EX
{
  MIBICMPSTATS_EX icmpInStats;
  MIBICMPSTATS_EX icmpOutStats;
} MIB_ICMP_EX, *PMIB_ICMP_EX;

#ifndef MIB_IPADDR_DNS_ELIGIBLE
#define MIB_IPADDR_DNS_ELIGIBLE 0X0100
#endif

#define _IPMIB_ 0
#define ICMP4_ISTYPEERRORType) (((Type) == ICMP4_DST_UNREACH) || ((Type) == ICMP4_SOURCE_QUENCH) || ((Type) == ICMP4_REDIRECT) || ((Type) == ICMP4_PARAM_PROB) || ((Type) == ICMP4_TIME_EXCEEDED))
#define ICMP6_ISTYPEERRORType) (!ICMP6_ISTYPEINFORMATIONAL(Type))
#define ICMP6_ISTYPEINFORMATIONALType) (((Type) & ICMP6_INFOMSG_MASK) != 0)
#define MIB_IPROUTE_METRIC_UNUSED ((DWORD)0xFFFFFFFF)
#define MIB_USE_CURRENT_FORWARDING ((DWORD)0xFFFFFFFF)
#define MIB_USE_CURRENT_TTL ((DWORD)0xFFFFFFFF)
#define SIZEOF_BASIC_MIB_MFE ((ULONG)0x3C)
#define SIZEOF_BASIC_MIB_MFE_STATS ((ULONG)0x40)
#define SIZEOF_BASIC_MIB_MFE_STATS_EX ((ULONG)0x54)
#define SIZEOF_IPADDRTABLEX) (FIELD_OFFSET(MIB_IPADDRTABLE, table [0]) + ((X) * sizeof (MIB_IPADDRROW)) + ALIGN_SIZE)
#define SIZEOF_IPFORWARDTABLEX) (FIELD_OFFSET(MIB_IPFORWARDTABLE, table [0]) + ((X) * sizeof (MIB_IPFORWARDROW)) + ALIGN_SIZE)
#define SIZEOF_IPNETTABLEX) (FIELD_OFFSET(MIB_IPNETTABLE, table [0]) + ((X) * sizeof (MIB_IPNETROW)) + ALIGN_SIZE)

struct _MIB_IPNETROW_LH
{
    IF_INDEX dwIndex;
    DWORD dwPhysAddrLen;
    UCHAR bPhysAddr[8];
    DWORD dwAddr;
    union
    {
        DWORD dwType;
        MIB_IPNET_TYPE Type;
    };
};

struct _MIB_IPSTATS_LH
{
    union
    {
        DWORD dwForwarding;
        MIB_IPSTATS_FORWARDING Forwarding;
    };
    DWORD dwDefaultTTL;
    DWORD dwInReceives;
    DWORD dwInHdrErrors;
    DWORD dwInAddrErrors;
    DWORD dwForwDatagrams;
    DWORD dwInUnknownProtos;
    DWORD dwInDiscards;
    DWORD dwInDelivers;
    DWORD dwOutRequests;
    DWORD dwRoutingDiscards;
    DWORD dwOutDiscards;
    DWORD dwOutNoRoutes;
    DWORD dwReasmTimeout;
    DWORD dwReasmReqds;
    DWORD dwReasmOks;
    DWORD dwReasmFails;
    DWORD dwFragOks;
    DWORD dwFragFails;
    DWORD dwFragCreates;
    DWORD dwNumIf;
    DWORD dwNumAddr;
    DWORD dwNumRoutes;
};

typedef struct _MIB_IPNETROW_LH MIB_IPNETROW_LH;

typedef struct _MIB_IPSTATS_LH MIB_IPSTATS_LH;

typedef struct _MIB_IPNETROW_LH *PMIB_IPNETROW_LH;

typedef struct _MIB_IPSTATS_LH *PMIB_IPSTATS_LH;

#define ICMP4_ISTYPEERRORType) (((Type) == ICMP4_DST_UNREACH) || ((Type) == ICMP4_SOURCE_QUENCH) || ((Type) == ICMP4_REDIRECT) || ((Type) == ICMP4_PARAM_PROB) || ((Type) == ICMP4_TIME_EXCEEDED))
#define ICMP6_ISTYPEERRORType) (!ICMP6_ISTYPEINFORMATIONAL(Type))
#define SIZEOF_IPADDRTABLEX) (FIELD_OFFSET(MIB_IPADDRTABLE, table [0]) + ((X) * sizeof (MIB_IPADDRROW)) + ALIGN_SIZE)
#define SIZEOF_IPFORWARDTABLEX) (FIELD_OFFSET(MIB_IPFORWARDTABLE, table [0]) + ((X) * sizeof (MIB_IPFORWARDROW)) + ALIGN_SIZE)
#define SIZEOF_IPNETTABLEX) (FIELD_OFFSET(MIB_IPNETTABLE, table [0]) + ((X) * sizeof (MIB_IPNETROW)) + ALIGN_SIZE)

#define ICMP4_ISTYPEERRORType) (((Type) == ICMP4_DST_UNREACH) || ((Type) == ICMP4_SOURCE_QUENCH) || ((Type) == ICMP4_REDIRECT) || ((Type) == ICMP4_PARAM_PROB) || ((Type) == ICMP4_TIME_EXCEEDED))
#define ICMP6_ISTYPEERRORType) (!ICMP6_ISTYPEINFORMATIONAL(Type))
#define SIZEOF_IPADDRTABLEX) (FIELD_OFFSET(MIB_IPADDRTABLE, table [0]) + ((X) * sizeof (MIB_IPADDRROW)) + ALIGN_SIZE)
#define SIZEOF_IPFORWARDTABLEX) (FIELD_OFFSET(MIB_IPFORWARDTABLE, table [0]) + ((X) * sizeof (MIB_IPFORWARDROW)) + ALIGN_SIZE)
#define SIZEOF_IPNETTABLEX) (FIELD_OFFSET(MIB_IPNETTABLE, table [0]) + ((X) * sizeof (MIB_IPNETROW)) + ALIGN_SIZE)
#define SIZEOF_MCAST_IF_TABLEX) (FIELD_OFFSET(MIB_IPMCAST_IF_TABLE, table [0]) + ((X) * sizeof (MIB_IPMCAST_IF_ENTRY)) + ALIGN_SIZE)
#define SIZEOF_MIB_MFEX) (SIZEOF_BASIC_MIB_MFE + ((X) * sizeof (MIB_IPMCAST_OIF)))
#define SIZEOF_MIB_MFE_STATSX) (SIZEOF_BASIC_MIB_MFE_STATS + ((X) * sizeof (MIB_IPMCAST_OIF_STATS)))
#define SIZEOF_MIB_MFE_STATS_EXX) (SIZEOF_BASIC_MIB_MFE_STATS_EX + ((X) * sizeof (MIB_IPMCAST_OIF_STATS)))

struct _MIB_ICMP_EX_XPSP1
{
    MIBICMPSTATS_EX icmpInStats;
    MIBICMPSTATS_EX icmpOutStats;
};

struct _MIB_IPADDRROW_W2K
{
    DWORD dwAddr;
    DWORD dwIndex;
    DWORD dwMask;
    DWORD dwBCastAddr;
    DWORD dwReasmSize;
    unsigned short unused1;
    unsigned short unused2;
};

struct _MIB_IPADDRROW_XP
{
    DWORD dwAddr;
    IF_INDEX dwIndex;
    DWORD dwMask;
    DWORD dwBCastAddr;
    DWORD dwReasmSize;
    unsigned short unused1;
    unsigned short wType;
};

struct _MIB_IPMCAST_GLOBAL
{
    DWORD dwEnable;
};

struct _MIB_IPMCAST_IF_ENTRY
{
    DWORD dwIfIndex;
    DWORD dwTtl;
    DWORD dwProtocol;
    DWORD dwRateLimit;
    ULONG ulInMcastOctets;
    ULONG ulOutMcastOctets;
};

struct _MIB_IPMCAST_OIF_STATS_LH
{
    DWORD dwOutIfIndex;
    DWORD dwNextHopAddr;
    DWORD dwDialContext;
    ULONG ulTtlTooLow;
    ULONG ulFragNeeded;
    ULONG ulOutPackets;
    ULONG ulOutDiscards;
};

struct _MIB_IPMCAST_OIF_STATS_W2K
{
    DWORD dwOutIfIndex;
    DWORD dwNextHopAddr;
    PVOID pvDialContext;
    ULONG ulTtlTooLow;
    ULONG ulFragNeeded;
    ULONG ulOutPackets;
    ULONG ulOutDiscards;
};

struct _MIB_IPMCAST_OIF_W2K
{
    DWORD dwOutIfIndex;
    DWORD dwNextHopAddr;
    PVOID pvReserved;
    DWORD dwReserved;
};

struct _MIB_IPMCAST_OIF_XP
{
    DWORD dwOutIfIndex;
    DWORD dwNextHopAddr;
    DWORD dwReserved;
    DWORD dwReserved1;
};

struct _MIB_IPNETROW_W2K
{
    IF_INDEX dwIndex;
    DWORD dwPhysAddrLen;
    UCHAR bPhysAddr[8];
    DWORD dwAddr;
    DWORD dwType;
};

struct _MIB_IPSTATS_W2K
{
    DWORD dwForwarding;
    DWORD dwDefaultTTL;
    DWORD dwInReceives;
    DWORD dwInHdrErrors;
    DWORD dwInAddrErrors;
    DWORD dwForwDatagrams;
    DWORD dwInUnknownProtos;
    DWORD dwInDiscards;
    DWORD dwInDelivers;
    DWORD dwOutRequests;
    DWORD dwRoutingDiscards;
    DWORD dwOutDiscards;
    DWORD dwOutNoRoutes;
    DWORD dwReasmTimeout;
    DWORD dwReasmReqds;
    DWORD dwReasmOks;
    DWORD dwReasmFails;
    DWORD dwFragOks;
    DWORD dwFragFails;
    DWORD dwFragCreates;
    DWORD dwNumIf;
    DWORD dwNumAddr;
    DWORD dwNumRoutes;
};

struct _MIBICMPSTATS_EX_XPSP1
{
    DWORD dwMsgs;
    DWORD dwErrors;
    DWORD rgdwTypeCount[256];
};

typedef struct _MIB_ICMP_EX_XPSP1 MIB_ICMP_EX_XPSP1;

typedef struct _MIB_IPADDRROW_W2K MIB_IPADDRROW_W2K;

typedef struct _MIB_IPADDRROW_XP MIB_IPADDRROW_XP;

typedef struct _MIB_IPMCAST_GLOBAL MIB_IPMCAST_GLOBAL;

typedef struct _MIB_IPMCAST_IF_ENTRY MIB_IPMCAST_IF_ENTRY;

struct _MIB_IPMCAST_IF_TABLE
{
    DWORD dwNumEntries;
    MIB_IPMCAST_IF_ENTRY table[1];
};

typedef struct _MIB_IPMCAST_IF_TABLE MIB_IPMCAST_IF_TABLE;

typedef struct _MIB_IPMCAST_MFE MIB_IPMCAST_MFE;

struct _MIB_MFE_TABLE
{
    DWORD dwNumEntries;
    MIB_IPMCAST_MFE table[1];
};

typedef struct _MIB_IPMCAST_MFE_STATS MIB_IPMCAST_MFE_STATS;

struct _MIB_MFE_STATS_TABLE
{
    DWORD dwNumEntries;
    MIB_IPMCAST_MFE_STATS table[1];
};

typedef struct _MIB_IPMCAST_MFE_STATS_EX_XP MIB_IPMCAST_MFE_STATS_EX_XP;

typedef MIB_IPMCAST_MFE_STATS_EX_XP MIB_IPMCAST_MFE_STATS_EX;

typedef struct _MIB_IPMCAST_OIF_STATS_LH MIB_IPMCAST_OIF_STATS_LH;

typedef MIB_IPMCAST_OIF_STATS_LH MIB_IPMCAST_OIF_STATS;

struct _MIB_IPMCAST_MFE_STATS
{
    DWORD dwGroup;
    DWORD dwSource;
    DWORD dwSrcMask;
    DWORD dwUpStrmNgbr;
    DWORD dwInIfIndex;
    DWORD dwInIfProtocol;
    DWORD dwRouteProtocol;
    DWORD dwRouteNetwork;
    DWORD dwRouteMask;
    ULONG ulUpTime;
    ULONG ulExpiryTime;
    ULONG ulNumOutIf;
    ULONG ulInPkts;
    ULONG ulInOctets;
    ULONG ulPktsDifferentIf;
    ULONG ulQueueOverflow;
    MIB_IPMCAST_OIF_STATS rgmiosOutStats[1];
};

struct _MIB_IPMCAST_MFE_STATS_EX_XP
{
    DWORD dwGroup;
    DWORD dwSource;
    DWORD dwSrcMask;
    DWORD dwUpStrmNgbr;
    DWORD dwInIfIndex;
    DWORD dwInIfProtocol;
    DWORD dwRouteProtocol;
    DWORD dwRouteNetwork;
    DWORD dwRouteMask;
    ULONG ulUpTime;
    ULONG ulExpiryTime;
    ULONG ulNumOutIf;
    ULONG ulInPkts;
    ULONG ulInOctets;
    ULONG ulPktsDifferentIf;
    ULONG ulQueueOverflow;
    ULONG ulUninitMfe;
    ULONG ulNegativeMfe;
    ULONG ulInDiscards;
    ULONG ulInHdrErrors;
    ULONG ulTotalOutPackets;
    MIB_IPMCAST_OIF_STATS rgmiosOutStats[1];
};

typedef struct _MIB_IPMCAST_OIF_STATS_W2K MIB_IPMCAST_OIF_STATS_W2K;

typedef struct _MIB_IPMCAST_OIF_W2K MIB_IPMCAST_OIF_W2K;

typedef struct _MIB_IPMCAST_OIF_XP MIB_IPMCAST_OIF_XP;

typedef MIB_IPMCAST_OIF_XP MIB_IPMCAST_OIF;

struct _MIB_IPMCAST_MFE
{
    DWORD dwGroup;
    DWORD dwSource;
    DWORD dwSrcMask;
    DWORD dwUpStrmNgbr;
    DWORD dwInIfIndex;
    DWORD dwInIfProtocol;
    DWORD dwRouteProtocol;
    DWORD dwRouteNetwork;
    DWORD dwRouteMask;
    ULONG ulUpTime;
    ULONG ulExpiryTime;
    ULONG ulTimeOut;
    ULONG ulNumOutIf;
    DWORD fFlags;
    DWORD dwReserved;
    MIB_IPMCAST_OIF rgmioOutInfo[1];
};

typedef struct _MIB_IPNETROW_W2K MIB_IPNETROW_W2K;

typedef struct _MIB_IPSTATS_W2K MIB_IPSTATS_W2K;

typedef struct _MIB_MFE_STATS_TABLE MIB_MFE_STATS_TABLE;

typedef struct _MIB_MFE_STATS_TABLE_EX_XP MIB_MFE_STATS_TABLE_EX_XP;

typedef MIB_MFE_STATS_TABLE_EX_XP MIB_MFE_STATS_TABLE_EX;

typedef struct _MIB_MFE_TABLE MIB_MFE_TABLE;

typedef struct _MIBICMPSTATS_EX_XPSP1 MIBICMPSTATS_EX_XPSP1;

typedef struct _MIB_ICMP_EX_XPSP1 *PMIB_ICMP_EX_XPSP1;

typedef struct _MIB_IPADDRROW_W2K *PMIB_IPADDRROW_W2K;

typedef struct _MIB_IPADDRROW_XP *PMIB_IPADDRROW_XP;

typedef struct _MIB_IPMCAST_GLOBAL *PMIB_IPMCAST_GLOBAL;

typedef struct _MIB_IPMCAST_IF_ENTRY *PMIB_IPMCAST_IF_ENTRY;

typedef struct _MIB_IPMCAST_IF_TABLE *PMIB_IPMCAST_IF_TABLE;

typedef struct _MIB_IPMCAST_MFE *PMIB_IPMCAST_MFE;

typedef struct _MIB_IPMCAST_MFE_STATS *PMIB_IPMCAST_MFE_STATS;

typedef MIB_IPMCAST_MFE_STATS_EX_XP *PMIB_IPMCAST_MFE_STATS_EX;

typedef struct _MIB_IPMCAST_MFE_STATS_EX_XP *PMIB_IPMCAST_MFE_STATS_EX_XP;

struct _MIB_MFE_STATS_TABLE_EX_XP
{
    DWORD dwNumEntries;
    PMIB_IPMCAST_MFE_STATS_EX_XP table[1];
};

typedef MIB_IPMCAST_OIF_XP *PMIB_IPMCAST_OIF;

typedef MIB_IPMCAST_OIF_STATS_LH *PMIB_IPMCAST_OIF_STATS;

typedef struct _MIB_IPMCAST_OIF_STATS_LH *PMIB_IPMCAST_OIF_STATS_LH;

typedef struct _MIB_IPMCAST_OIF_STATS_W2K *PMIB_IPMCAST_OIF_STATS_W2K;

typedef struct _MIB_IPMCAST_OIF_W2K *PMIB_IPMCAST_OIF_W2K;

typedef struct _MIB_IPMCAST_OIF_XP *PMIB_IPMCAST_OIF_XP;

typedef struct _MIB_IPNETROW_W2K *PMIB_IPNETROW_W2K;

typedef struct _MIB_IPSTATS_W2K *PMIB_IPSTATS_W2K;

typedef struct _MIB_MFE_STATS_TABLE *PMIB_MFE_STATS_TABLE;

typedef MIB_MFE_STATS_TABLE_EX_XP *PMIB_MFE_STATS_TABLE_EX;

typedef struct _MIB_MFE_STATS_TABLE_EX_XP *PMIB_MFE_STATS_TABLE_EX_XP;

typedef struct _MIB_MFE_TABLE *PMIB_MFE_TABLE;

typedef struct _MIBICMPSTATS_EX_XPSP1 *PMIBICMPSTATS_EX_XPSP1;

#endif /* __WINE_IPMIB_H */
