/*
 * PROJECT:     LiberNT Bluetooth USB transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth HCI USB transport definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>
#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>
#include <bthxddi.h>
#include <rtkbtfw.h>

#define BTHUSB_TAG                  'bUtB'
#define BTHUSB_MAX_EVENT            (2 + 255)
#define BTHUSB_MAX_ACL_IN           1021
#define BTHUSB_ACL_READ_SIZE        1024
#define BTHUSB_READS_PER_PIPE       2
#define BTHUSB_MAX_QUEUED_PACKETS   64
#define BTHUSB_COMMAND_TIMEOUT_MS   3000
#define BTHUSB_MAX_READ_FAILURES    8

#define BTHUSB_PDO_COMPATIBLE_ID    L"MS_BTHX_BTHMINI"

#define HCI_EVENT_COMMAND_COMPLETE  0x0E
#define HCI_EVENT_COMMAND_STATUS    0x0F
#define HCI_COMMAND_RESET           0x0C03

typedef enum _BTHUSB_RX_KIND
{
    BthUsbRxEvent,
    BthUsbRxAcl,
    BthUsbRxKinds
} BTHUSB_RX_KIND;

typedef struct _BTHUSB_PACKET
{
    LIST_ENTRY Link;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} BTHUSB_PACKET, *PBTHUSB_PACKET;

typedef struct _BTHUSB_DEVICE *PBTHUSB_DEVICE;

typedef struct _BTHUSB_READ
{
    PBTHUSB_DEVICE Dev;
    BTHUSB_RX_KIND Kind;
    PIRP Irp;
    PUCHAR Buffer;
    ULONG Size;
    volatile BOOLEAN Failed;
    URB Urb;
} BTHUSB_READ, *PBTHUSB_READ;

typedef struct _BTHUSB_RX_STREAM
{
    UCHAR Data[4 + BTHUSB_MAX_ACL_IN];
    ULONG Length;
    BOOLEAN Discard;
    LIST_ENTRY Packets;
    ULONG Count;
    WDFQUEUE Queue;
    BOOLEAN Delivering;
} BTHUSB_RX_STREAM, *PBTHUSB_RX_STREAM;

typedef struct _BTHUSB_DEVICE
{
    WDFDEVICE Device;
    PDEVICE_OBJECT LowerDeviceObject;
    USHORT VendorId;
    USHORT ProductId;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;
    USBD_PIPE_HANDLE EventPipe;
    USBD_PIPE_HANDLE AclInPipe;
    USBD_PIPE_HANDLE AclOutPipe;
    ULONG EventPacketSize;

    BTHUSB_READ Reads[BthUsbRxKinds][BTHUSB_READS_PER_PIPE];
    volatile LONG ReadsOutstanding;
    volatile LONG ReadFailures;
    WDFWORKITEM RecoverItem;
    volatile LONG WritesOutstanding;
    KEVENT IoDrained;
    volatile BOOLEAN Stopping;

    KSPIN_LOCK Lock;
    BTHUSB_RX_STREAM Rx[BthUsbRxKinds];
    KEVENT EventArrived;
    BOOLEAN Ready;

    PKTHREAD Thread;
    WDFDEVICE Child;
    RTKBT_VERSION Version;
} BTHUSB_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BTHUSB_DEVICE, BthUsbGetDevice)

typedef struct _BTHUSB_PDO
{
    PBTHUSB_DEVICE Parent;
} BTHUSB_PDO, *PBTHUSB_PDO;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BTHUSB_PDO, BthUsbGetPdo)

NTSTATUS
BthUsbConfigure(
    _In_ PBTHUSB_DEVICE Dev);

VOID
BthUsbUnconfigure(
    _In_ PBTHUSB_DEVICE Dev);

NTSTATUS
BthUsbStartReads(
    _In_ PBTHUSB_DEVICE Dev);

EVT_WDF_WORKITEM BthUsbRecoverReads;

VOID
BthUsbStopIo(
    _In_ PBTHUSB_DEVICE Dev);

NTSTATUS
BthUsbWrite(
    _In_ PBTHUSB_DEVICE Dev,
    _In_ BTHX_HCI_PACKET_TYPE Type,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_opt_ WDFREQUEST Request);

VOID
BthUsbDeliverPackets(
    _In_ PBTHUSB_DEVICE Dev,
    _In_ BTHUSB_RX_KIND Kind);

VOID
BthUsbFlushPackets(
    _In_ PBTHUSB_DEVICE Dev);

PBTHUSB_PACKET
BthUsbTakePacket(
    _In_ PBTHUSB_DEVICE Dev,
    _In_ BTHUSB_RX_KIND Kind);
