/*
 * PROJECT:     LiberNT LLTD
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     lltdio.sys control interface for LLTD QoS controllers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _LIBERNT_LLTDIO_H_
#define _LIBERNT_LLTDIO_H_

#ifndef CTL_CODE
#include <winioctl.h>
#endif

#define LLTDIO_DEVICE_NAME              L"\\Device\\LLTDIO"

#define _LLTDIO_CTL(_fn, _method) \
    CTL_CODE(FILE_DEVICE_NETWORK, (0x800 + (_fn)), _method, FILE_ANY_ACCESS)

/* IN: the 6-byte MAC address of the adapter, OUT: LLTDIO_ADAPTER_INFO */
#define IOCTL_LLTDIO_BIND               _LLTDIO_CTL(0x00, METHOD_BUFFERED)
/* IN: LLTDIO_SEND_INPUT followed by Count frames of FrameLength bytes, from the demultiplex header on */
#define IOCTL_LLTDIO_SEND               _LLTDIO_CTL(0x01, METHOD_BUFFERED)
/* OUT: LLTDIO_RECEIVED followed by the frame from its Ethernet header on; pends until a frame arrives */
#define IOCTL_LLTDIO_RECEIVE            _LLTDIO_CTL(0x02, METHOD_BUFFERED)

#define LLTDIO_NO_TIMESTAMP             0xFFFFFFFF
#define LLTDIO_MAX_TRAIN                82

typedef struct _LLTDIO_ADAPTER_INFO
{
    UCHAR MacAddress[6];
    USHORT Reserved;
    ULONG64 LinkSpeed;
    ULONG64 PerformanceCounterFrequency;
} LLTDIO_ADAPTER_INFO, *PLLTDIO_ADAPTER_INFO;

typedef struct _LLTDIO_SEND_INPUT
{
    UCHAR Destination[6];
    USHORT Reserved;
    ULONG Count;
    ULONG FrameLength;
    /* Offset in each frame of the 8-byte transmit time stamp the driver writes, or LLTDIO_NO_TIMESTAMP */
    ULONG TimestampOffset;
    ULONG Reserved2;
} LLTDIO_SEND_INPUT, *PLLTDIO_SEND_INPUT;

typedef struct _LLTDIO_RECEIVED
{
    ULONG64 ReceiveTimestamp;
    ULONG Length;
    ULONG Reserved;
} LLTDIO_RECEIVED, *PLLTDIO_RECEIVED;

C_ASSERT(sizeof(LLTDIO_ADAPTER_INFO) == 24);
C_ASSERT(sizeof(LLTDIO_SEND_INPUT) == 24);
C_ASSERT(sizeof(LLTDIO_RECEIVED) == 16);

#endif /* _LIBERNT_LLTDIO_H_ */
