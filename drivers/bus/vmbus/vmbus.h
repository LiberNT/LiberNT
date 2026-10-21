/*
 * PROJECT:     LiberNT Hyper-V VMBus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Internal definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ntstrsafe.h>
#include <wdmguid.h>
#include "vmbusapi.h"

#define VMBUS_TAG 'bmVL'

#define HV_CPUID_VENDOR_AND_MAX       0x40000000
#define HV_CPUID_INTERFACE            0x40000001
#define HV_CPUID_FEATURES             0x40000003
#define HV_CPUID_INTERFACE_SIGNATURE  0x31237648
#define HV_FEATURE_ACCESS_SYNIC_REGS  0x00000004
#define HV_FEATURE_ACCESS_HYPERCALL   0x00000020

#define HV_X64_MSR_GUEST_OS_ID  0x40000000
#define HV_X64_MSR_HYPERCALL    0x40000001
#define HV_X64_MSR_VP_INDEX     0x40000002
#define HV_X64_MSR_SCONTROL     0x40000080
#define HV_X64_MSR_SIEFP        0x40000082
#define HV_X64_MSR_SIMP         0x40000083
#define HV_X64_MSR_EOM          0x40000084
#define HV_X64_MSR_SINT0        0x40000090

#define HV_SINT_MASKED   (1ULL << 16)
#define HV_SINT_POLLING  (1ULL << 18)

#define HV_GUEST_OS_ID_LIBERNT 0x8000000A000065F4ULL

#define HVCALL_POST_MESSAGE   0x005C
#define HVCALL_SIGNAL_EVENT   0x005D
#define HV_HYPERCALL_FAST     (1ULL << 16)

#define HV_STATUS_SUCCESS                0x0000
#define HV_STATUS_INSUFFICIENT_MEMORY    0x000B
#define HV_STATUS_INVALID_CONNECTION_ID  0x0012
#define HV_STATUS_INSUFFICIENT_BUFFERS   0x0013

#define HV_MESSAGE_PAYLOAD_SIZE 240
#define HV_MESSAGE_PENDING      0x01

#define VMBUS_MESSAGE_SINT                 2
#define VMBUS_MESSAGE_CONNECTION_ID        1
#define VMBUS_MESSAGE_CONNECTION_ID_4      4
#define VMBUS_POST_MESSAGE_TYPE            1

#define VMBUS_VERSION_WIN8       0x00020004
#define VMBUS_VERSION_WIN8_1     0x00030000
#define VMBUS_VERSION_WIN10      0x00040000
#define VMBUS_VERSION_WIN10_RS3  0x00040001
#define VMBUS_VERSION_WIN10_V5   0x00050000
#define VMBUS_VERSION_WIN10_V5_1 0x00050001
#define VMBUS_VERSION_WIN10_V5_2 0x00050002

#define VMBUS_MSGTYPE_OFFER_CHANNEL         1
#define VMBUS_MSGTYPE_RESCIND_CHANNEL_OFFER 2
#define VMBUS_MSGTYPE_REQUEST_OFFERS        3
#define VMBUS_MSGTYPE_ALL_OFFERS_DELIVERED  4
#define VMBUS_MSGTYPE_OPEN_CHANNEL          5
#define VMBUS_MSGTYPE_OPEN_CHANNEL_RESULT   6
#define VMBUS_MSGTYPE_CLOSE_CHANNEL         7
#define VMBUS_MSGTYPE_GPADL_HEADER          8
#define VMBUS_MSGTYPE_GPADL_BODY            9
#define VMBUS_MSGTYPE_GPADL_CREATED         10
#define VMBUS_MSGTYPE_GPADL_TEARDOWN        11
#define VMBUS_MSGTYPE_GPADL_TORNDOWN        12
#define VMBUS_MSGTYPE_REL_ID_RELEASED       13
#define VMBUS_MSGTYPE_INITIATE_CONTACT      14
#define VMBUS_MSGTYPE_VERSION_RESPONSE      15

typedef struct _HV_MESSAGE
{
    ULONG MessageType;
    UCHAR PayloadSize;
    UCHAR MessageFlags;
    USHORT Reserved;
    ULONG64 Sender;
    UCHAR Payload[HV_MESSAGE_PAYLOAD_SIZE];
} HV_MESSAGE, *PHV_MESSAGE;

typedef struct _HV_POST_MESSAGE_INPUT
{
    ULONG ConnectionId;
    ULONG Reserved;
    ULONG MessageType;
    ULONG PayloadSize;
    UCHAR Payload[HV_MESSAGE_PAYLOAD_SIZE];
} HV_POST_MESSAGE_INPUT, *PHV_POST_MESSAGE_INPUT;

typedef struct _VMBUS_MSG_HEADER
{
    ULONG MessageType;
    ULONG Padding;
} VMBUS_MSG_HEADER, *PVMBUS_MSG_HEADER;

typedef struct _VMBUS_MSG_INITIATE_CONTACT
{
    VMBUS_MSG_HEADER Header;
    ULONG VersionRequested;
    ULONG TargetVcpu;
    ULONG64 InterruptPageOrTarget;
    ULONG64 MonitorPage1;
    ULONG64 MonitorPage2;
} VMBUS_MSG_INITIATE_CONTACT;

typedef struct _VMBUS_MSG_VERSION_RESPONSE
{
    VMBUS_MSG_HEADER Header;
    UCHAR VersionSupported;
    UCHAR ConnectionState;
    USHORT Padding;
    ULONG SelectedVersionOrConnectionId;
} VMBUS_MSG_VERSION_RESPONSE;

typedef struct _VMBUS_MSG_OFFER_CHANNEL
{
    VMBUS_MSG_HEADER Header;
    GUID InterfaceType;
    GUID InterfaceInstance;
    ULONG Reserved[4];
    USHORT Flags;
    USHORT MmioMegabytes;
    UCHAR UserDefined[120];
    USHORT SubchannelIndex;
    USHORT MmioMegabytesOptional;
    ULONG ChildRelId;
    UCHAR MonitorId;
    UCHAR MonitorAllocated;
    USHORT IsDedicatedInterrupt;
    ULONG ConnectionId;
} VMBUS_MSG_OFFER_CHANNEL, *PVMBUS_MSG_OFFER_CHANNEL;

typedef struct _VMBUS_MSG_CHILD_RELID
{
    VMBUS_MSG_HEADER Header;
    ULONG ChildRelId;
} VMBUS_MSG_CHILD_RELID;

typedef struct _VMBUS_MSG_GPADL_CREATED
{
    VMBUS_MSG_HEADER Header;
    ULONG ChildRelId;
    ULONG Gpadl;
    ULONG CreationStatus;
} VMBUS_MSG_GPADL_CREATED;

typedef struct _VMBUS_MSG_GPADL_TEARDOWN
{
    VMBUS_MSG_HEADER Header;
    ULONG ChildRelId;
    ULONG Gpadl;
} VMBUS_MSG_GPADL_TEARDOWN;

typedef struct _VMBUS_MSG_GPADL_TORNDOWN
{
    VMBUS_MSG_HEADER Header;
    ULONG Gpadl;
} VMBUS_MSG_GPADL_TORNDOWN;

typedef struct _VMBUS_MSG_OPEN_CHANNEL
{
    VMBUS_MSG_HEADER Header;
    ULONG ChildRelId;
    ULONG OpenId;
    ULONG RingBufferGpadl;
    ULONG TargetVp;
    ULONG DownstreamRingBufferPageOffset;
    UCHAR UserData[120];
} VMBUS_MSG_OPEN_CHANNEL;

typedef struct _VMBUS_MSG_OPEN_RESULT
{
    VMBUS_MSG_HEADER Header;
    ULONG ChildRelId;
    ULONG OpenId;
    ULONG Status;
} VMBUS_MSG_OPEN_RESULT;

#define VMBUS_GPADL_HEADER_SIZE   20
#define VMBUS_GPADL_BODY_SIZE     16
#define VMBUS_GPADL_HEADER_PFNS   ((HV_MESSAGE_PAYLOAD_SIZE - VMBUS_GPADL_HEADER_SIZE - 8) / sizeof(ULONG64))
#define VMBUS_GPADL_BODY_PFNS     ((HV_MESSAGE_PAYLOAD_SIZE - VMBUS_GPADL_BODY_SIZE) / sizeof(ULONG64))

C_ASSERT(sizeof(HV_MESSAGE) == 256);
C_ASSERT(sizeof(VMBUS_MSG_INITIATE_CONTACT) == 40);
C_ASSERT(sizeof(VMBUS_MSG_VERSION_RESPONSE) == 16);
C_ASSERT(sizeof(VMBUS_MSG_OFFER_CHANNEL) == 196);
C_ASSERT(sizeof(VMBUS_MSG_GPADL_CREATED) == 20);
C_ASSERT(sizeof(VMBUS_MSG_OPEN_CHANNEL) == 148);
C_ASSERT(sizeof(VMBUS_MSG_OPEN_RESULT) == 20);

typedef struct _VMBUS_RING_CONTROL
{
    volatile ULONG WriteIndex;
    volatile ULONG ReadIndex;
    volatile ULONG InterruptMask;
    volatile ULONG PendingSendSize;
    ULONG Reserved[12];
    volatile ULONG FeatureBits;
} VMBUS_RING_CONTROL, *PVMBUS_RING_CONTROL;

#define VMBUS_RING_FEATURE_PENDING_SEND_SIZE 1

typedef struct _VMBUS_RING
{
    PVMBUS_RING_CONTROL Control;
    PUCHAR Data;
    ULONG Size;
    KSPIN_LOCK Lock;
} VMBUS_RING, *PVMBUS_RING;

struct _VMBUS_FDO_EXTENSION;

typedef struct _VMBUS_CHANNEL
{
    LIST_ENTRY ListEntry;
    struct _VMBUS_FDO_EXTENSION *Bus;
    PDEVICE_OBJECT Pdo;
    GUID InterfaceType;
    GUID InterfaceInstance;
    ULONG ChildRelId;
    ULONG ConnectionId;
    ULONG MmioMegabytes;
    BOOLEAN Rescinded;
    BOOLEAN Open;
    PVOID RingBuffer;
    ULONG RingBufferSize;
    ULONG RingGpadl;
    VMBUS_RING Outbound;
    VMBUS_RING Inbound;
    PVMBUS_CHANNEL_CALLBACK Callback;
    PVOID CallbackContext;
    volatile LONG KickPending;
} VMBUS_CHANNEL;

typedef struct _VMBUS_COMMON_EXTENSION
{
    BOOLEAN IsFdo;
} VMBUS_COMMON_EXTENSION, *PVMBUS_COMMON_EXTENSION;

typedef struct _VMBUS_PDO_EXTENSION
{
    VMBUS_COMMON_EXTENSION Common;
    PVMBUS_CHANNEL Channel;
    BOOLEAN Reported;
} VMBUS_PDO_EXTENSION, *PVMBUS_PDO_EXTENSION;

typedef struct _VMBUS_FDO_EXTENSION
{
    VMBUS_COMMON_EXTENSION Common;
    PDEVICE_OBJECT Fdo;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT LowerDevice;

    PKINTERRUPT Interrupt;
    KDPC Dpc;
    KTIMER PollTimer;
    KDPC PollDpc;
    BOOLEAN Polling;
    ULONG Vector;

    PHV_MESSAGE MessagePage;
    PULONG EventPage;
    PVOID MonitorPages;
    PVOID InterruptPage;
    PHV_POST_MESSAGE_INPUT PostInput;
    PHYSICAL_ADDRESS PostInputPa;
    ULONG VpIndex;
    BOOLEAN HypervisorReady;

    ULONG Version;
    ULONG MessageConnectionId;
    BOOLEAN Connected;

    FAST_MUTEX ControlLock;
    KEVENT ResponseEvent;
    ULONG ExpectedResponse;
    ULONG ExpectedKey;
    UCHAR Response[HV_MESSAGE_PAYLOAD_SIZE];

    KEVENT OffersEvent;
    KSPIN_LOCK ChannelLock;
    LIST_ENTRY Channels;
    LONG NextGpadl;
} VMBUS_FDO_EXTENSION, *PVMBUS_FDO_EXTENSION;

extern PDRIVER_OBJECT VmbusDriverObject;

BOOLEAN
HvIsPresent(VOID);

NTSTATUS
HvInitialize(
    _Inout_ PVMBUS_FDO_EXTENSION Bus);

VOID
HvShutdown(
    _Inout_ PVMBUS_FDO_EXTENSION Bus);

NTSTATUS
HvPostMessage(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _In_reads_bytes_(Size) const VOID *Message,
    _In_ ULONG Size);

VOID
HvSignalEvent(
    _In_ ULONG ConnectionId);

VOID
HvAcknowledgeMessage(VOID);

VOID
VmbusControlBegin(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _In_ ULONG ResponseType,
    _In_ ULONG ResponseKey);

NTSTATUS
VmbusControlWait(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _Out_writes_bytes_opt_(ResponseSize) PVOID Response,
    _In_ ULONG ResponseSize);

VOID
VmbusControlEnd(
    _Inout_ PVMBUS_FDO_EXTENSION Bus);

NTSTATUS
VmbusSendControl(
    _Inout_ PVMBUS_FDO_EXTENSION Bus,
    _In_reads_bytes_(Size) const VOID *Message,
    _In_ ULONG Size,
    _In_ ULONG ResponseType,
    _In_ ULONG ResponseKey,
    _Out_writes_bytes_opt_(ResponseSize) PVOID Response,
    _In_ ULONG ResponseSize);

VOID
VmbusChannelInterrupt(
    _In_ PVMBUS_CHANNEL Channel);
