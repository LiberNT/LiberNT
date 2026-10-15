/*
 * PROJECT:     LiberNT Realtek Bluetooth UART transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Realtek three-wire UART (H5) Bluetooth transport definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>
#define RESHUB_USE_HELPER_ROUTINES
#include <reshub.h>
#include <ntddser.h>
#include <gpio.h>
#include <bthxddi.h>

#define RTKH5_TAG                   'h5tR'
#define RTKH5_UART_BAUD             115200
#define RTKH5_READ_TIMEOUT_MS       20
#define RTKH5_RETRANSMIT_MS         250
#define RTKH5_LINK_RETRY_MS         250
#define RTKH5_LINK_TIMEOUT_MS       3000
#define RTKH5_COMMAND_TIMEOUT_MS    3000
#define RTKH5_TX_WINDOW             4
#define RTKH5_MAX_PAYLOAD           1024
#define RTKH5_MAX_FRAME             (4 + RTKH5_MAX_PAYLOAD)
#define RTKH5_MAX_QUEUED_PACKETS    64
#define RTKH5_MAX_ACL_IN            1021

#define RTKH5_PKT_ACK               0
#define RTKH5_PKT_COMMAND           1
#define RTKH5_PKT_ACL               2
#define RTKH5_PKT_SCO               3
#define RTKH5_PKT_EVENT             4
#define RTKH5_PKT_LINK              15

#define RTKH5_FIRMWARE_PATH         L"\\SystemRoot\\System32\\drivers\\realtek\\rtl8852bs_fw.bin"
#define RTKH5_CONFIG_PATH           L"\\SystemRoot\\System32\\drivers\\realtek\\rtl8852bs_config.bin"
#define RTKH5_FIRMWARE_MAX_SIZE     (512 * 1024)

#define RTKH5_PDO_HARDWARE_ID       L"RTKH5BT\\RTL8852BS"
#define RTKH5_PDO_COMPATIBLE_ID     L"MS_BTHX_BTHMINI"

typedef enum _RTKH5_LINK_STATE
{
    RtkH5LinkUninitialized,
    RtkH5LinkInitialized,
    RtkH5LinkActive
} RTKH5_LINK_STATE;

typedef struct _RTKH5_PACKET
{
    LIST_ENTRY Link;
    UCHAR Type;
    UCHAR Sequence;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} RTKH5_PACKET, *PRTKH5_PACKET;

typedef struct _RTKH5_DEVICE
{
    WDFDEVICE Device;
    LARGE_INTEGER UartConnection;
    LARGE_INTEGER GpioConnection;
    BOOLEAN HasUart;
    BOOLEAN HasGpio;
    WDFIOTARGET Uart;
    WDFIOTARGET Gpio;
    PKTHREAD Thread;
    volatile BOOLEAN StopThread;

    KSPIN_LOCK Lock;
    KMUTEX WriteLock;
    RTKH5_LINK_STATE LinkState;
    UCHAR TxSequence;
    UCHAR RxSequence;
    BOOLEAN AckPending;
    UCHAR PeerWindow;
    LIST_ENTRY TxQueue;
    LIST_ENTRY Unacked;
    ULONG UnackedCount;
    ULONGLONG LastTransmit;
    ULONGLONG NextLinkMessage;

    UCHAR RxFrame[RTKH5_MAX_FRAME];
    ULONG RxLength;
    BOOLEAN RxEscape;
    BOOLEAN RxInFrame;

    LIST_ENTRY RxPackets;
    ULONG RxPacketCount;
    KEVENT RxEvent;
    BOOLEAN Ready;
    WDFQUEUE EventQueue;
    WDFQUEUE AclQueue;
    WDFDEVICE Child;

    USHORT LmpSubversion;
    USHORT HciRevision;
    UCHAR RomVersion;
} RTKH5_DEVICE, *PRTKH5_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RTKH5_DEVICE, RtkH5GetDevice)

typedef struct _RTKH5_PDO
{
    PRTKH5_DEVICE Parent;
} RTKH5_PDO, *PRTKH5_PDO;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RTKH5_PDO, RtkH5GetPdo)

ULONGLONG
RtkH5NowMs(VOID);

VOID
RtkH5LinkReset(
    _In_ PRTKH5_DEVICE Dev);

NTSTATUS
RtkH5Pump(
    _In_ PRTKH5_DEVICE Dev);

NTSTATUS
RtkH5WaitLink(
    _In_ PRTKH5_DEVICE Dev);

NTSTATUS
RtkH5SendReliable(
    _In_ PRTKH5_DEVICE Dev,
    _In_ UCHAR Type,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);

PRTKH5_PACKET
RtkH5TakePacket(
    _In_ PRTKH5_DEVICE Dev);

NTSTATUS
RtkH5HciCommand(
    _In_ PRTKH5_DEVICE Dev,
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(ParameterLength) const UCHAR *Parameters,
    _In_ UCHAR ParameterLength,
    _Out_writes_bytes_to_opt_(ResultSize, *ResultLength) PUCHAR Result,
    _In_ ULONG ResultSize,
    _Out_opt_ PULONG ResultLength);

NTSTATUS
RtkH5InitializeController(
    _In_ PRTKH5_DEVICE Dev);

NTSTATUS
RtkH5CreatePdo(
    _In_ PRTKH5_DEVICE Dev);

VOID
RtkH5DeliverPackets(
    _In_ PRTKH5_DEVICE Dev);

VOID
RtkH5FlushPackets(
    _In_ PRTKH5_DEVICE Dev);
