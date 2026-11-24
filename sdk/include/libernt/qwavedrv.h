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

/* qos2.h values the driver needs; qwave.dll asserts that they match */
#define QWAVE_NON_ADAPTIVE_FLOW         0x00000002
#define QWAVE_TRAFFIC_TYPE_LAST         5
#define QWAVE_SET_TRAFFIC_TYPE          0
#define QWAVE_SET_OUTGOING_RATE         1
#define QWAVE_SET_OUTGOING_DSCP         2
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
C_ASSERT(sizeof(QWAVE_ADD_SOCKET_INPUT) == 24);
C_ASSERT(sizeof(QWAVE_REMOVE_SOCKET_INPUT) == 16);
C_ASSERT(FIELD_OFFSET(QWAVE_SET_FLOW_INPUT, Buffer) == 16);

#endif /* _QWAVEDRV_H_ */
