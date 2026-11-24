/*
 * PROJECT:     LiberNT qWAVE
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     qwave.dll <-> qwavedrv.sys control interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _QWAVEDRV_H_
#define _QWAVEDRV_H_

#ifndef CTL_CODE
#include <winioctl.h>
#endif

#define QWAVE_DEVICE_NAME               L"\\Device\\QWAVEdrv"

#define _QWAVE_CTL(_fn) \
    CTL_CODE(FILE_DEVICE_NETWORK, (0x800 + (_fn)), METHOD_BUFFERED, FILE_ANY_ACCESS)

/* IN: QWAVE_ADD_SOCKET_INPUT, OUT: ULONG flow id */
#define IOCTL_QWAVE_ADD_SOCKET          _QWAVE_CTL(0x00)
/* IN: QWAVE_REMOVE_SOCKET_INPUT */
#define IOCTL_QWAVE_REMOVE_SOCKET       _QWAVE_CTL(0x01)
/* IN: QWAVE_SET_FLOW_INPUT followed by Size bytes of the operation's buffer */
#define IOCTL_QWAVE_SET_FLOW            _QWAVE_CTL(0x02)
/* IN: QWAVE_QUERY_FLOW_INPUT, OUT: QWAVE_PACKET_PRIORITY, ULONG64 rate or QWAVE_FUNDAMENTALS */
#define IOCTL_QWAVE_QUERY_FLOW          _QWAVE_CTL(0x03)
/* OUT: QWAVE_FLOW_LIST with room for the caller's flow ids */
#define IOCTL_QWAVE_ENUMERATE_FLOWS     _QWAVE_CTL(0x04)

/* An NTSTATUS that DeviceIoControl reports as the Win32 error itself */
#define QWAVE_STATUS_FROM_WIN32(Error)  ((NTSTATUS)(0x80070000 | ((Error) & 0xFFFF)))

/* The layouts do not depend on the pointer width, so 32-bit callers share them. */
typedef struct _QWAVE_ADD_SOCKET_INPUT
{
    ULONG64 Socket;
    ULONG FlowId;
    ULONG TrafficType;
    ULONG Flags;
    ULONG Reserved;
} QWAVE_ADD_SOCKET_INPUT, *PQWAVE_ADD_SOCKET_INPUT;

typedef struct _QWAVE_REMOVE_SOCKET_INPUT
{
    ULONG64 Socket;
    ULONG FlowId;
    ULONG Flags;
} QWAVE_REMOVE_SOCKET_INPUT, *PQWAVE_REMOVE_SOCKET_INPUT;

typedef struct _QWAVE_SET_FLOW_INPUT
{
    ULONG FlowId;
    ULONG Operation;
    ULONG Flags;
    ULONG Size;
    UCHAR Buffer[ANYSIZE_ARRAY];
} QWAVE_SET_FLOW_INPUT, *PQWAVE_SET_FLOW_INPUT;

typedef struct _QWAVE_QUERY_FLOW_INPUT
{
    ULONG FlowId;
    ULONG Operation;
    ULONG Flags;
    ULONG Reserved;
} QWAVE_QUERY_FLOW_INPUT, *PQWAVE_QUERY_FLOW_INPUT;

typedef struct _QWAVE_PACKET_PRIORITY
{
    ULONG ConformantDscp;
    ULONG NonConformantDscp;
    ULONG ConformantL2;
    ULONG NonConformantL2;
} QWAVE_PACKET_PRIORITY, *PQWAVE_PACKET_PRIORITY;

/* The layout of QOS_FLOW_FUNDAMENTALS, which is the same for 32-bit and 64-bit callers */
typedef struct _QWAVE_FUNDAMENTALS
{
    ULONG BottleneckBandwidthSet;
    ULONG Reserved1;
    ULONG64 BottleneckBandwidth;
    ULONG AvailableBandwidthSet;
    ULONG Reserved2;
    ULONG64 AvailableBandwidth;
    ULONG RttSet;
    ULONG Rtt;
} QWAVE_FUNDAMENTALS, *PQWAVE_FUNDAMENTALS;

typedef struct _QWAVE_FLOW_LIST
{
    ULONG Total;
    ULONG Returned;
    ULONG FlowIds[ANYSIZE_ARRAY];
} QWAVE_FLOW_LIST, *PQWAVE_FLOW_LIST;

/* qos2.h values the driver needs; qwave.dll asserts that they match */
#define QWAVE_NON_ADAPTIVE_FLOW         0x00000002
#define QWAVE_TRAFFIC_TYPE_LAST         5
#define QWAVE_SET_TRAFFIC_TYPE          0
#define QWAVE_SET_OUTGOING_RATE         1
#define QWAVE_SET_OUTGOING_DSCP         2
#define QWAVE_QUERY_FUNDAMENTALS        0
#define QWAVE_QUERY_PACKET_PRIORITY     1
#define QWAVE_QUERY_OUTGOING_RATE       2
#define QWAVE_QUERYFLOW_FRESH           0x00000001
#define QWAVE_SHAPING_LAST              2
#define QWAVE_FLOWRATE_REASON_LAST      4
#define QWAVE_DSCP_LAST                 63

typedef struct _QWAVE_FLOWRATE
{
    ULONG64 Bandwidth;
    ULONG ShapingBehavior;
    ULONG Reason;
} QWAVE_FLOWRATE, *PQWAVE_FLOWRATE;

/* The largest buffer a QOS_SET_FLOW operation takes (QOS_FLOWRATE_OUTGOING) */
#define QWAVE_SET_FLOW_MAX_BUFFER       sizeof(QWAVE_FLOWRATE)

C_ASSERT(sizeof(QWAVE_FLOWRATE) == 16);
C_ASSERT(sizeof(QWAVE_QUERY_FLOW_INPUT) == 16);
C_ASSERT(sizeof(QWAVE_PACKET_PRIORITY) == 16);
C_ASSERT(sizeof(QWAVE_FUNDAMENTALS) == 40);
C_ASSERT(FIELD_OFFSET(QWAVE_FUNDAMENTALS, BottleneckBandwidth) == 8);
C_ASSERT(FIELD_OFFSET(QWAVE_FUNDAMENTALS, AvailableBandwidth) == 24);
C_ASSERT(FIELD_OFFSET(QWAVE_FUNDAMENTALS, Rtt) == 36);
C_ASSERT(FIELD_OFFSET(QWAVE_FLOW_LIST, FlowIds) == 8);
C_ASSERT(sizeof(QWAVE_ADD_SOCKET_INPUT) == 24);
C_ASSERT(sizeof(QWAVE_REMOVE_SOCKET_INPUT) == 16);
C_ASSERT(FIELD_OFFSET(QWAVE_SET_FLOW_INPUT, Buffer) == 16);

#endif /* _QWAVEDRV_H_ */
