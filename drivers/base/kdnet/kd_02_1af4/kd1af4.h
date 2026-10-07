/*
 * PROJECT:     LiberNT VirtIO Network Kernel Debugger Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VirtIO 1.0 PCI transport and network device layout, module interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define NOEXTAPI
#include <ntifs.h>
#include <kdnetextensibility.h>

#define VIO_PCI_CAPABILITY_VENDOR   0x09
#define VIO_PCI_CAP_COMMON          1
#define VIO_PCI_CAP_NOTIFY          2
#define VIO_PCI_CAP_DEVICE          4

#define VIO_COMMON_DEVICE_FEATURE_SELECT    0x00
#define VIO_COMMON_DEVICE_FEATURE           0x04
#define VIO_COMMON_DRIVER_FEATURE_SELECT    0x08
#define VIO_COMMON_DRIVER_FEATURE           0x0C
#define VIO_COMMON_DEVICE_STATUS            0x14
#define VIO_COMMON_QUEUE_SELECT             0x16
#define VIO_COMMON_QUEUE_SIZE               0x18
#define VIO_COMMON_QUEUE_VECTOR             0x1A
#define VIO_COMMON_QUEUE_ENABLE             0x1C
#define VIO_COMMON_QUEUE_NOTIFY_OFFSET      0x1E
#define VIO_COMMON_QUEUE_DESCRIPTORS        0x20
#define VIO_COMMON_QUEUE_DRIVER             0x28
#define VIO_COMMON_QUEUE_DEVICE             0x30

#define VIO_STATUS_ACKNOWLEDGE      0x01
#define VIO_STATUS_DRIVER           0x02
#define VIO_STATUS_DRIVER_OK        0x04
#define VIO_STATUS_FEATURES_OK      0x08

#define VIO_NET_FEATURE_MAC         0x00000020
#define VIO_NET_FEATURE_STATUS      0x00010000
#define VIO_FEATURE_VERSION_1       0x00000001

#define VIO_NET_CONFIG_MAC          0
#define VIO_NET_CONFIG_STATUS       6
#define VIO_NET_STATUS_LINK_UP      0x0001

#define VIO_NO_VECTOR               0xFFFF
#define VIO_DESCRIPTOR_WRITE        0x0002
#define VIO_AVAILABLE_NO_INTERRUPT  0x0001

#define VIO_NET_HEADER_SIZE         12
#define VIO_RX_COUNT                64
#define VIO_TX_COUNT                16
#define VIO_BUFFER_SIZE             2048
#define VIO_MAX_FRAME               1514

typedef struct _VIO_DESCRIPTOR
{
    ULONG64 Address;
    ULONG Length;
    USHORT Flags;
    USHORT Next;
} VIO_DESCRIPTOR, *PVIO_DESCRIPTOR;

typedef struct _VIO_AVAILABLE
{
    USHORT Flags;
    USHORT Index;
    USHORT Ring[ANYSIZE_ARRAY];
} VIO_AVAILABLE, *PVIO_AVAILABLE;

typedef struct _VIO_USED_ELEMENT
{
    ULONG Id;
    ULONG Length;
} VIO_USED_ELEMENT, *PVIO_USED_ELEMENT;

typedef struct _VIO_USED
{
    USHORT Flags;
    USHORT Index;
    VIO_USED_ELEMENT Ring[ANYSIZE_ARRAY];
} VIO_USED, *PVIO_USED;

C_ASSERT(sizeof(VIO_DESCRIPTOR) == 16);
C_ASSERT(sizeof(VIO_USED_ELEMENT) == 8);
C_ASSERT(FIELD_OFFSET(VIO_AVAILABLE, Ring) == 4);
C_ASSERT(FIELD_OFFSET(VIO_USED, Ring) == 4);

typedef struct _VIO_QUEUE
{
    USHORT Size;
    USHORT Available;
    USHORT LastUsed;
    volatile VIO_DESCRIPTOR *Descriptors;
    volatile VIO_AVAILABLE *Driver;
    volatile VIO_USED *Device;
    PUCHAR Notify;
    USHORT Number;
} VIO_QUEUE, *PVIO_QUEUE;

typedef struct _VIO_ADAPTER
{
    PKDNET_SHARED_DATA KdNet;
    PUCHAR Common;
    PUCHAR Notify;
    PUCHAR Config;
    ULONG NotifyMultiplier;
    BOOLEAN HasStatus;
    VIO_QUEUE Rx;
    VIO_QUEUE Tx;
    PUCHAR RxBuffers;
    PUCHAR TxBuffers;
    ULONG TxNext;
    ULONG LinkPoll;
    USHORT RxLength[VIO_RX_COUNT];
    BOOLEAN TxSubmitted[VIO_TX_COUNT];
} VIO_ADAPTER, *PVIO_ADAPTER;

extern PKDNET_EXTENSIBILITY_IMPORTS KdNetExtensibilityImports;

BOOLEAN
VioIsSupportedDevice(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

ULONG
NTAPI
VioGetHardwareContextSize(
    _In_ PDEBUG_DEVICE_DESCRIPTOR Device);

NTSTATUS
NTAPI
VioInitializeController(
    _In_ PKDNET_SHARED_DATA KdNet);

VOID
NTAPI
VioShutdownController(
    _In_ PVOID Context);

NTSTATUS
NTAPI
VioGetRxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle,
    _Out_ PVOID *Packet,
    _Out_ PULONG Length);

VOID
NTAPI
VioReleaseRxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle);

NTSTATUS
NTAPI
VioGetTxPacket(
    _In_ PVOID Context,
    _Out_ PULONG Handle);

NTSTATUS
NTAPI
VioSendTxPacket(
    _In_ PVOID Context,
    _In_ ULONG Handle,
    _In_ ULONG Length);

PVOID
NTAPI
VioGetPacketAddress(
    _In_ PVOID Context,
    _In_ ULONG Handle);

ULONG
NTAPI
VioGetPacketLength(
    _In_ PVOID Context,
    _In_ ULONG Handle);
