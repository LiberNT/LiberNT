/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth extensible transport miniport and radio definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>
#include <bthdef.h>
#include <bthioctl.h>
#include <bthxddi.h>

#define BTHMINI_TAG                     'iMhB'
#define BTHMINI_EVENT_READS             2
#define BTHMINI_ACL_READS               2
#define BTHMINI_MAX_EVENT               (2 + 255)
#define BTHMINI_MAX_RX_PACKETS          128
#define BTHMINI_COMMAND_TIMEOUT_MS      5000
#define BTHMINI_INQUIRY_LENGTH          0x04
#define BTHMINI_MAX_DEVICES             64
#define BTHMINI_DEFAULT_COD             0x000104

#define LOCAL_RADIO_DISCOVERABLE        0x0001
#define LOCAL_RADIO_CONNECTABLE         0x0002

#define IOCTL_WINEBTH_RADIO_SET_FLAG            CTL_CODE(FILE_DEVICE_BLUETOOTH, 0xa3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WINEBTH_RADIO_START_DISCOVERY     CTL_CODE(FILE_DEVICE_BLUETOOTH, 0xa6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_WINEBTH_RADIO_STOP_DISCOVERY      CTL_CODE(FILE_DEVICE_BLUETOOTH, 0xa7, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define HCI_OP(Ogf, Ocf)                ((USHORT)(((Ogf) << 10) | (Ocf)))

#define HCI_INQUIRY                     HCI_OP(0x01, 0x0001)
#define HCI_INQUIRY_CANCEL              HCI_OP(0x01, 0x0002)
#define HCI_DISCONNECT                  HCI_OP(0x01, 0x0006)
#define HCI_ACCEPT_CONNECTION_REQUEST   HCI_OP(0x01, 0x0009)
#define HCI_REJECT_CONNECTION_REQUEST   HCI_OP(0x01, 0x000A)
#define HCI_LINK_KEY_REQUEST_NEG_REPLY  HCI_OP(0x01, 0x000C)
#define HCI_REMOTE_NAME_REQUEST         HCI_OP(0x01, 0x0019)
#define HCI_WRITE_DEFAULT_LINK_POLICY   HCI_OP(0x02, 0x000F)
#define HCI_SET_EVENT_MASK              HCI_OP(0x03, 0x0001)
#define HCI_RESET                       HCI_OP(0x03, 0x0003)
#define HCI_WRITE_LOCAL_NAME            HCI_OP(0x03, 0x0013)
#define HCI_WRITE_SCAN_ENABLE           HCI_OP(0x03, 0x001A)
#define HCI_WRITE_CLASS_OF_DEVICE       HCI_OP(0x03, 0x0024)
#define HCI_WRITE_INQUIRY_MODE          HCI_OP(0x03, 0x0045)
#define HCI_WRITE_EIR                   HCI_OP(0x03, 0x0052)
#define HCI_WRITE_SIMPLE_PAIRING_MODE   HCI_OP(0x03, 0x0056)
#define HCI_READ_LOCAL_VERSION          HCI_OP(0x04, 0x0001)
#define HCI_READ_LOCAL_COMMANDS         HCI_OP(0x04, 0x0002)
#define HCI_READ_LOCAL_FEATURES         HCI_OP(0x04, 0x0003)
#define HCI_READ_BUFFER_SIZE            HCI_OP(0x04, 0x0005)
#define HCI_READ_BD_ADDR                HCI_OP(0x04, 0x0009)

#define HCI_EV_INQUIRY_COMPLETE         0x01
#define HCI_EV_INQUIRY_RESULT           0x02
#define HCI_EV_CONNECTION_COMPLETE      0x03
#define HCI_EV_CONNECTION_REQUEST       0x04
#define HCI_EV_DISCONNECTION_COMPLETE   0x05
#define HCI_EV_REMOTE_NAME_COMPLETE     0x07
#define HCI_EV_COMMAND_COMPLETE         0x0E
#define HCI_EV_COMMAND_STATUS           0x0F
#define HCI_EV_HARDWARE_ERROR           0x10
#define HCI_EV_NUMBER_OF_COMPLETED      0x13
#define HCI_EV_LINK_KEY_REQUEST         0x17
#define HCI_EV_INQUIRY_RESULT_RSSI      0x22
#define HCI_EV_EXTENDED_INQUIRY_RESULT  0x2F

#define HCI_SCAN_INQUIRY                0x01
#define HCI_SCAN_PAGE                   0x02

#define LMP_FEATURE(Features, Byte, Bit)    (((Features) >> ((Byte) * 8 + (Bit))) & 1)

typedef struct _BTHMINI_RX_PACKET
{
    LIST_ENTRY Link;
    UCHAR Type;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} BTHMINI_RX_PACKET, *PBTHMINI_RX_PACKET;

typedef struct _BTHMINI_COMMAND
{
    LIST_ENTRY Link;
    USHORT Opcode;
    UCHAR Length;
    BOOLEAN Async;
    BOOLEAN Done;
    NTSTATUS Status;
    ULONG ResultLength;
    UCHAR Parameters[255];
    UCHAR Result[255];
} BTHMINI_COMMAND, *PBTHMINI_COMMAND;

typedef struct _BTHMINI_REMOTE
{
    LIST_ENTRY Link;
    BTH_ADDR Address;
    ULONG Flags;
    BTH_COD ClassOfDevice;
    UCHAR PageScanRepetitionMode;
    USHORT ClockOffset;
    USHORT Handle;
    BOOLEAN NameRequested;
    CHAR Name[BTH_MAX_NAME_SIZE];
} BTHMINI_REMOTE, *PBTHMINI_REMOTE;

typedef struct _BTHMINI_DEVICE
{
    WDFDEVICE Device;
    WDFIOTARGET Target;
    WDFQUEUE Queue;
    BTHX_CAPABILITIES Capabilities;
    WDFREQUEST Reads[BTHMINI_EVENT_READS + BTHMINI_ACL_READS];
    volatile LONG ReadsOutstanding;
    volatile BOOLEAN Stopping;
    KEVENT ReadsDrained;

    PKTHREAD Thread;
    KEVENT WorkEvent;
    KSPIN_LOCK RxLock;
    LIST_ENTRY RxPackets;
    ULONG RxCount;

    LIST_ENTRY CommandsPending;
    LIST_ENTRY CommandsSent;
    ULONG CommandCredits;
    BOOLEAN Ready;

    BTH_ADDR Address;
    UCHAR HciVersion;
    USHORT HciRevision;
    UCHAR LmpVersion;
    USHORT LmpSubversion;
    USHORT Manufacturer;
    ULONGLONG LmpFeatures;
    UCHAR SupportedCommands[64];
    USHORT AclMtu;
    USHORT AclPackets;
    USHORT AclCredits;
    BTH_COD ClassOfDevice;
    UCHAR ScanEnable;
    CHAR Name[BTH_MAX_NAME_SIZE];

    BOOLEAN Discovering;
    BOOLEAN Inquiring;
    BOOLEAN NameRequestActive;
    LIST_ENTRY Remotes;
    ULONG RemoteCount;
} BTHMINI_DEVICE, *PBTHMINI_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BTHMINI_DEVICE, BthMiniGetDevice)

NTSTATUS
BthMiniSendPacket(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ UCHAR Type,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);

BOOLEAN
BthMiniWaitWork(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ ULONG Milliseconds);

VOID
BthHciProcessRx(
    _In_ PBTHMINI_DEVICE Dev);

NTSTATUS
BthHciQueueCommand(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(Length) const UCHAR *Parameters,
    _In_ UCHAR Length);

NTSTATUS
BthHciCommand(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(Length) const UCHAR *Parameters,
    _In_ UCHAR Length,
    _Out_writes_bytes_opt_(ResultSize) PUCHAR Result,
    _In_ ULONG ResultSize);

VOID
BthHciFlushCommands(
    _In_ PBTHMINI_DEVICE Dev);

NTSTATUS
BthHciInitialize(
    _In_ PBTHMINI_DEVICE Dev);

NTSTATUS
BthHciWriteScanEnable(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ UCHAR ScanEnable);

VOID
BthRadioEvent(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Event,
    _In_ ULONG Length);

VOID
BthRadioCommandFailed(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ USHORT Opcode);

VOID
BthRadioDeviceControl(
    _In_ PBTHMINI_DEVICE Dev,
    _In_ WDFREQUEST Request);

VOID
BthRadioFreeRemotes(
    _In_ PBTHMINI_DEVICE Dev);

VOID
BthAclReceive(
    _In_ PBTHMINI_DEVICE Dev,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);
