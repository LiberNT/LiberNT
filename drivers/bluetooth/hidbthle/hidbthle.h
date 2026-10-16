/*
 * PROJECT:     LiberNT Bluetooth LE HID miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HID over GATT Profile (HOGP) report host definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define _HIDPI_
#define _HIDPI_NO_FUNCTION_MACROS_
#include <ntddk.h>
#include <hidport.h>
#include <bthdef.h>
#include <bthminiddi.h>

#define HIDBTHLE_TAG                    'eLdH'
#define HIDBTHLE_MAX_REPORTS            32
#define HIDBTHLE_MAX_VALUE              512
#define HIDBTHLE_QUEUED_REPORTS         32
#define HIDBTHLE_SLOT_LENGTH            (sizeof(USHORT) + 1 + HIDBTHLE_MAX_VALUE)
#define HIDBTHLE_CACHE_VERSION          1

#define HOGP_UUID_HID_INFORMATION       0x2A4A
#define HOGP_UUID_REPORT_MAP            0x2A4B
#define HOGP_UUID_REPORT                0x2A4D
#define HOGP_UUID_REPORT_REFERENCE      0x2908
#define HOGP_REPORT_INPUT               1
#define HOGP_REPORT_OUTPUT              2
#define HOGP_REPORT_FEATURE             3

typedef struct _HIDBTHLE_DEVICE_EXTENSION HIDBTHLE_DEVICE_EXTENSION, *PHIDBTHLE_DEVICE_EXTENSION;

typedef struct _HIDBTHLE_REPORT
{
    USHORT CharacteristicHandle;
    USHORT ValueHandle;
    USHORT ReferenceHandle;
    UCHAR Id;
    UCHAR Type;
    BOOLEAN Writable;
    BOOLEAN WritableWithoutResponse;
    BOOLEAN Notifiable;
    BOOLEAN Readable;
} HIDBTHLE_REPORT, *PHIDBTHLE_REPORT;

typedef struct _HIDBTHLE_SUBSCRIPTION
{
    PHIDBTHLE_DEVICE_EXTENSION Extension;
    PHIDBTHLE_REPORT Report;
    BTHMINI_GATT_REQUEST Request;
    PIRP Irp;
    BOOLEAN Registered;
} HIDBTHLE_SUBSCRIPTION, *PHIDBTHLE_SUBSCRIPTION;

typedef struct _HIDBTHLE_CACHE_HEADER
{
    ULONG Version;
    USHORT HidVersion;
    UCHAR CountryCode;
    UCHAR ReportCount;
    USHORT ReportMapLength;
    USHORT Reserved;
} HIDBTHLE_CACHE_HEADER, *PHIDBTHLE_CACHE_HEADER;

struct _HIDBTHLE_DEVICE_EXTENSION
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    BTHMINI_GATT_DEVICE_INFO Info;
    USHORT HidVersion;
    UCHAR CountryCode;
    PUCHAR ReportDescriptor;
    USHORT ReportDescriptorLength;
    HIDBTHLE_REPORT Reports[HIDBTHLE_MAX_REPORTS];
    ULONG ReportCount;
    BOOLEAN UsesReportIds;
    HIDBTHLE_SUBSCRIPTION Subscriptions[HIDBTHLE_MAX_REPORTS];

    KSPIN_LOCK Lock;
    BOOLEAN Started;
    LIST_ENTRY PendingReads;
    PUCHAR ReportQueue;
    ULONG ReportQueueHead;
    ULONG ReportQueueCount;
    volatile LONG IoOutstanding;
    KEVENT IoDrained;
};
