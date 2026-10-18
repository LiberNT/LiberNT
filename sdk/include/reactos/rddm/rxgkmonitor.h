/*
 * PROJECT:     LiberNT Display Driver Model
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private monitor.sys <-> dxgkrnl monitor child request
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define IOCTL_RXGK_QUERY_MONITOR_DESCRIPTOR \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define RXGK_MONITOR_EDID_MAX_SIZE (128 * 4)

typedef struct _RXGK_MONITOR_DESCRIPTOR
{
    ULONG VideoOutputTechnology;
    ULONG EdidLength;
    UCHAR Edid[RXGK_MONITOR_EDID_MAX_SIZE];
} RXGK_MONITOR_DESCRIPTOR, *PRXGK_MONITOR_DESCRIPTOR;
