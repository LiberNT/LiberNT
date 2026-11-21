/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private USB capability query between usbdex, the hub, usbport and its miniports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define ROS_USB_CAPABILITY_QUERY_VERSION 1
#define IOCTL_INTERNAL_REACTOS_USB_QUERY_CAPABILITY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef struct _ROS_USB_CAPABILITY_QUERY
{
    ULONG Version;
    ULONG Size;
    GUID CapabilityType;
    ULONG OutputBufferLength;
    PVOID OutputBuffer;
    ULONG ResultLength;
} ROS_USB_CAPABILITY_QUERY, *PROS_USB_CAPABILITY_QUERY;

DEFINE_GUID(GUID_ROS_USB_MINIPORT_MAX_STREAMS,
    0xfee65691, 0x79fb, 0x438b, 0xa0, 0x59, 0xb7, 0x9d, 0x6a, 0x8a, 0x26, 0xce);
