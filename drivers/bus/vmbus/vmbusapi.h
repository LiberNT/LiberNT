/*
 * PROJECT:     LiberNT Hyper-V VMBus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Channel interface exported to VMBus client drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define VMBUS_PACKET_TYPE_INBAND          6
#define VMBUS_PACKET_TYPE_TRANSFER_PAGES  7
#define VMBUS_PACKET_TYPE_GPA_DIRECT      9
#define VMBUS_PACKET_TYPE_COMPLETION      11

#define VMBUS_PACKET_FLAG_COMPLETION_REQUESTED 1

typedef struct _VMBUS_PACKET_DESCRIPTOR
{
    USHORT Type;
    USHORT DataOffset8;
    USHORT Length8;
    USHORT Flags;
    ULONG64 TransactionId;
} VMBUS_PACKET_DESCRIPTOR, *PVMBUS_PACKET_DESCRIPTOR;

typedef struct _VMBUS_TRANSFER_PAGE_HEADER
{
    USHORT TransferPageSetId;
    USHORT Reserved;
    ULONG RangeCount;
} VMBUS_TRANSFER_PAGE_HEADER, *PVMBUS_TRANSFER_PAGE_HEADER;

typedef struct _VMBUS_TRANSFER_PAGE_RANGE
{
    ULONG ByteCount;
    ULONG ByteOffset;
} VMBUS_TRANSFER_PAGE_RANGE, *PVMBUS_TRANSFER_PAGE_RANGE;

C_ASSERT(sizeof(VMBUS_PACKET_DESCRIPTOR) == 16);
C_ASSERT(sizeof(VMBUS_TRANSFER_PAGE_HEADER) == 8);

typedef struct _VMBUS_CHANNEL *PVMBUS_CHANNEL;

typedef VOID (NTAPI *PVMBUS_CHANNEL_CALLBACK)(_In_opt_ PVOID Context);

PVMBUS_CHANNEL
NTAPI
VmbChannelFromDevice(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject);

NTSTATUS
NTAPI
VmbChannelOpen(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ ULONG RingBufferSize,
    _In_ PVMBUS_CHANNEL_CALLBACK Callback,
    _In_opt_ PVOID Context);

VOID
NTAPI
VmbChannelClose(
    _In_ PVMBUS_CHANNEL Channel);

NTSTATUS
NTAPI
VmbChannelCreateGpadl(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Gpadl);

VOID
NTAPI
VmbChannelDestroyGpadl(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ ULONG Gpadl);

NTSTATUS
NTAPI
VmbChannelSendPacket(
    _In_ PVMBUS_CHANNEL Channel,
    _In_ USHORT Type,
    _In_ USHORT Flags,
    _In_ ULONG64 TransactionId,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length);

NTSTATUS
NTAPI
VmbChannelReceivePacket(
    _In_ PVMBUS_CHANNEL Channel,
    _Out_writes_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG PacketLength);
