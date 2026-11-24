/*
 * PROJECT:     LiberNT LLTD
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Link Layer Topology Discovery QoS diagnostics frames ([MS-LLTD] 2.2.3, 2.2.5, 2.2.6)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _LIBERNT_LLTD_H_
#define _LIBERNT_LLTD_H_

#define LLTD_ETHERTYPE                  0x88D9
#define LLTD_VERSION                    0x01
#define LLTD_TOS_QOS_DIAGNOSTICS        0x02

#define LLTD_QOS_INITIALIZE_SINK        0x00
#define LLTD_QOS_READY                  0x01
#define LLTD_QOS_PROBE                  0x02
#define LLTD_QOS_QUERY                  0x03
#define LLTD_QOS_QUERY_RESP             0x04
#define LLTD_QOS_RESET                  0x05
#define LLTD_QOS_ERROR                  0x06
#define LLTD_QOS_ACK                    0x07
#define LLTD_QOS_COUNTER_SNAPSHOT       0x08
#define LLTD_QOS_COUNTER_RESULT         0x09
#define LLTD_QOS_COUNTER_LEASE          0x0A

#define LLTD_INTERRUPT_MOD_DISABLE      0x00
#define LLTD_INTERRUPT_MOD_EXISTING     0xFF

#define LLTD_PROBE_TIMED                0x00
#define LLTD_PROBE_PROBEGAP_CONTROLLER  0x01
#define LLTD_PROBE_PROBEGAP_SINK        0x02
#define LLTD_PROBE_TAG                  0x80

#define LLTD_ERROR_RESOURCES            0x0000
#define LLTD_ERROR_BUSY                 0x0001
#define LLTD_ERROR_INTERRUPT_MOD        0x0002

#define LLTD_QUERY_RESP_ERROR           0x4000
#define LLTD_QUERY_RESP_COUNT_MASK      0x3FFF

#define LLTD_MAX_TIMED_PROBES           82
#define LLTD_MAX_FRAME                  1514
#define LLTD_MIN_FRAME                  60

/* Every multi-byte field is in network byte order */
#pragma pack(push, 1)
typedef struct _LLTD_QOS_HEADER
{
    UCHAR Destination[6];
    UCHAR Source[6];
    USHORT EtherType;
    UCHAR Version;
    UCHAR TypeOfService;
    UCHAR Reserved;
    UCHAR Function;
    UCHAR RealDestination[6];
    UCHAR RealSource[6];
    USHORT SequenceNumber;
} LLTD_QOS_HEADER, *PLLTD_QOS_HEADER;

typedef struct _LLTD_QOS_INITIALIZE
{
    UCHAR InterruptMod;
} LLTD_QOS_INITIALIZE, *PLLTD_QOS_INITIALIZE;

typedef struct _LLTD_QOS_READY_BODY
{
    ULONG SinkLinkSpeed;
    ULONG64 PerformanceCounterFrequency;
} LLTD_QOS_READY_BODY, *PLLTD_QOS_READY_BODY;

typedef struct _LLTD_QOS_PROBE_BODY
{
    ULONG64 ControllerTransmitTimestamp;
    ULONG64 SinkReceiveTimestamp;
    ULONG64 SinkTransmitTimestamp;
    UCHAR TestType;
    UCHAR PacketId;
    UCHAR TagPriority;
} LLTD_QOS_PROBE_BODY, *PLLTD_QOS_PROBE_BODY;

typedef struct _LLTD_QOS_EVENT
{
    ULONG64 ControllerTransmitTimestamp;
    ULONG64 SinkReceiveTimestamp;
    UCHAR PacketId;
    UCHAR Reserved;
} LLTD_QOS_EVENT, *PLLTD_QOS_EVENT;

typedef struct _LLTD_QOS_ERROR_BODY
{
    USHORT ErrorCode;
} LLTD_QOS_ERROR_BODY, *PLLTD_QOS_ERROR_BODY;

typedef struct _LLTD_QOS_SNAPSHOT_REQUEST
{
    UCHAR HistorySize;
} LLTD_QOS_SNAPSHOT_REQUEST, *PLLTD_QOS_SNAPSHOT_REQUEST;

typedef struct _LLTD_QOS_COUNTER_RESULT_BODY
{
    UCHAR SubsecondSpan;
    UCHAR ByteScale;
    UCHAR PacketScale;
    UCHAR HistorySize;
} LLTD_QOS_COUNTER_RESULT_BODY, *PLLTD_QOS_COUNTER_RESULT_BODY;

typedef struct _LLTD_QOS_SNAPSHOT
{
    USHORT BytesReceived;
    USHORT PacketsReceived;
    USHORT BytesSent;
    USHORT PacketsSent;
} LLTD_QOS_SNAPSHOT, *PLLTD_QOS_SNAPSHOT;
#pragma pack(pop)

C_ASSERT(sizeof(LLTD_QOS_HEADER) == 32);
C_ASSERT(sizeof(LLTD_QOS_READY_BODY) == 12);
C_ASSERT(sizeof(LLTD_QOS_PROBE_BODY) == 27);
C_ASSERT(sizeof(LLTD_QOS_EVENT) == 18);
C_ASSERT(sizeof(LLTD_QOS_SNAPSHOT) == 8);
C_ASSERT(sizeof(LLTD_QOS_HEADER) + sizeof(USHORT) + LLTD_MAX_TIMED_PROBES * sizeof(LLTD_QOS_EVENT) <= LLTD_MAX_FRAME);

#endif /* _LIBERNT_LLTD_H_ */
