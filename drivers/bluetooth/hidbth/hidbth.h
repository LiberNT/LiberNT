/*
 * PROJECT:     LiberNT Bluetooth HID miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HID over Bluetooth BR/EDR (HIDP) miniport definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define _HIDPI_
#define _HIDPI_NO_FUNCTION_MACROS_
#include <ntddk.h>
#include <hidport.h>
#include <bthdef.h>
#include <bthioctl.h>
#include <bthddi.h>
#include <bthguid.h>
#include <bthminiddi.h>

#define HIDBTH_TAG                  'tBdH'
#define HIDBTH_PSM_CONTROL          0x0011
#define HIDBTH_PSM_INTERRUPT        0x0013
#define HIDBTH_BUFFER_SIZE          1024
#define HIDBTH_INTERRUPT_READS      2
#define HIDBTH_QUEUED_REPORTS       16
#define HIDBTH_CONTROL_TIMEOUT_MS   3000
#define HIDBTH_CLOSE_TIMEOUT_MS     5000

#define HIDP_HANDSHAKE              0x00
#define HIDP_CONTROL                0x10
#define HIDP_GET_REPORT             0x40
#define HIDP_SET_REPORT             0x50
#define HIDP_DATA                   0xA0
#define HIDP_TYPE_MASK              0xF0
#define HIDP_REPORT_INPUT           0x01
#define HIDP_REPORT_OUTPUT          0x02
#define HIDP_REPORT_FEATURE         0x03
#define HIDP_CONTROL_UNPLUG         0x05

#define SDP_ATTRIB_HID_RECONNECT_INITIATE   0x0205
#define SDP_ATTRIB_HID_DESCRIPTOR_LIST      0x0206

typedef enum _HIDBTH_CHANNEL
{
    HidBthControl,
    HidBthInterrupt,
    HidBthChannels
} HIDBTH_CHANNEL;

typedef struct _HIDBTH_DEVICE_EXTENSION HIDBTH_DEVICE_EXTENSION, *PHIDBTH_DEVICE_EXTENSION;

typedef struct _HIDBTH_TRANSFER
{
    PHIDBTH_DEVICE_EXTENSION Extension;
    HIDBTH_CHANNEL Channel;
    volatile LONG Active;
    PIRP Irp;
    BRB Brb;
    UCHAR Buffer[HIDBTH_BUFFER_SIZE];
} HIDBTH_TRANSFER, *PHIDBTH_TRANSFER;

struct _HIDBTH_DEVICE_EXTENSION
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDevice;
    BTH_PROFILE_DRIVER_INTERFACE Profile;
    BOOLEAN HaveProfile;
    BTH_ADDR Address;
    USHORT VendorId;
    USHORT ProductId;
    USHORT Version;
    PUCHAR ReportDescriptor;
    USHORT ReportDescriptorLength;

    L2CAP_SERVER_HANDLE Server;
    BOOLEAN PsmRegistered[HidBthChannels];
    KSPIN_LOCK Lock;
    BOOLEAN Started;
    L2CAP_CHANNEL_HANDLE Channels[HidBthChannels];
    BOOLEAN Opening[HidBthChannels];
    BOOLEAN ReadsRunning;
    HIDBTH_TRANSFER ControlRead;
    HIDBTH_TRANSFER InterruptReads[HIDBTH_INTERRUPT_READS];
    volatile LONG IoOutstanding;
    KEVENT IoDrained;

    LIST_ENTRY PendingReads;
    ULONG ReportSlotLength;
    PUCHAR ReportQueue;
    ULONG ReportQueueHead;
    ULONG ReportQueueCount;

    KMUTEX ControlLock;
    KEVENT ControlDone;
    BOOLEAN ControlWaiting;
    UCHAR ControlResult;
    PUCHAR ControlData;
    ULONG ControlLength;
};
