/*
 * PROJECT:     LiberNT Apple Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the Apple silicon graphics processor offers the display driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define APPLE_AGX_DEVICE_NAME L"\\Device\\AppleAgx"

/* Asked by the display driver before it sends anything to the graphics processor. */
#define IOCTL_APPLE_AGX_QUERY_STATE CTL_CODE(FILE_DEVICE_VIDEO, 0x920, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_APPLE_AGX_GET_PARAMS  CTL_CODE(FILE_DEVICE_VIDEO, 0x921, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* How far the graphics processor has come. Rendering is offered only when it is ready. */
#define APPLE_AGX_STATE_ABSENT      0
#define APPLE_AGX_STATE_IDENTIFIED  1
#define APPLE_AGX_STATE_ADOPTED     2
#define APPLE_AGX_STATE_STARTED     3
#define APPLE_AGX_STATE_READY       4

typedef struct _APPLE_AGX_REPORT
{
    ULONG State;
    ULONG Generation;
    ULONG Variant;
    ULONG Revision;
    ULONG Clusters;
    ULONG CoresPerCluster;
    ULONG ActiveCores;
    ULONG TopKilohertz;
    ULONG64 Records;
} APPLE_AGX_REPORT, *PAPPLE_AGX_REPORT;
