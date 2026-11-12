/*
 * PROJECT:     LiberNT Broadcom PCIe Bluetooth transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Broadcom BCM4378/4387/4388 PCIe Bluetooth transport definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <wdmguid.h>
#include <ntstrsafe.h>
#include <bthxddi.h>
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/appledart.h>
#include <reactos/drivers/applepcie.h>
#include "proto.h"

#define BCMBT_TAG                       'tBcB'
#define BCMBT_CONTROL_TIMEOUT_MS        1000
#define BCMBT_POLL_INTERVAL_MS          10
#define BCMBT_BOOT_TIMEOUT_MS           5000
#define BCMBT_COMMAND_TIMEOUT_MS        2000
#define BCMBT_RESET_SETTLE_MS           100
#define BCMBT_MAX_QUEUED_PACKETS        256
#define BCMBT_FIRMWARE_MAX_SIZE         (4 * 1024 * 1024)
#define BCMBT_FIRMWARE_DIR              L"\\SystemRoot\\System32\\drivers\\brcm\\"
#define BCMBT_PDO_COMPATIBLE_ID         L"MS_BTHX_BTHMINI"

typedef struct _BCMBT_PACKET
{
    LIST_ENTRY Link;
    UCHAR Type;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} BCMBT_PACKET, *PBCMBT_PACKET;

#define BCMBT_DMA_BLOCKS (1 + BCMBT_TRANSFER_RINGS + 2 * BCMBT_PAYLOAD_CHUNKS + BCMBT_COMPLETION_RINGS)

typedef struct _BCMBT_DMA_BLOCK
{
    PUCHAR Va;
    SIZE_T Size;
    ULONG64 Iova;
} BCMBT_DMA_BLOCK, *PBCMBT_DMA_BLOCK;

typedef struct _BCMBT_DEVICE
{
    WDFDEVICE Device;
    WDFINTERRUPT Interrupt;
    const BCMBT_CHIP *Chip;
    BUS_INTERFACE_STANDARD Bus;
    BOOLEAN HaveBus;
    DEVICE_RESET_INTERFACE_STANDARD Reset;
    BOOLEAN HaveReset;

    PUCHAR Bar0;
    ULONG Bar0Length;
    PUCHAR Bar2;
    ULONG Bar2Length;

    FDT_DRIVER_TREE Tree;
    BOOLEAN HaveTree;
    ULONG DartNode;
    ULONG Sid;
    CHAR BoardType[64];
    UCHAR PlatformAddress[6];
    BOOLEAN HavePlatformAddress;
    CHAR Stepping[BCMBT_OTP_PARAM_LENGTH];
    CHAR Vendor[BCMBT_OTP_PARAM_LENGTH];

    BCMBT_DMA_BLOCK Blocks[BCMBT_DMA_BLOCKS];
    ULONG BlockCount;
    PUCHAR Firmware;
    SIZE_T FirmwareSize;
    ULONG64 FirmwareIova;
    PBCMBT_RING_STATE RingState;
    PUCHAR Context;
    ULONG64 ContextIova;

    KSPIN_LOCK RingLock;
    BCMBT_COMPLETION_RING CompletionRings[BCMBT_COMPLETION_RINGS - 1];
    BCMBT_TRANSFER_RING TransferRings[BCMBT_XFER_ACL_D2H + 1];

    volatile LONG Bootstage;
    volatile LONG RtiStatus;
    KEVENT StateEvent;
    BOOLEAN Booted;
    BOOLEAN RingsCreated;
    BOOLEAN SetupDone;

    PKTHREAD Thread;
    volatile BOOLEAN StopThread;
    volatile BOOLEAN Ready;

    KSPIN_LOCK PacketLock;
    LIST_ENTRY RxPackets;
    ULONG RxPacketCount;
    KEVENT RxEvent;
    volatile LONG DeliverPending;
    KTIMER PollTimer;
    KDPC PollDpc;
    WDFQUEUE EventQueue;
    WDFQUEUE AclQueue;
    WDFDEVICE Child;
} BCMBT_DEVICE, *PBCMBT_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BCMBT_DEVICE, BcmBtGetDevice)

typedef struct _BCMBT_PDO
{
    PBCMBT_DEVICE Parent;
} BCMBT_PDO, *PBCMBT_PDO;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(BCMBT_PDO, BcmBtGetPdo)

VOID
BcmBtSleep(
    _In_ ULONG Milliseconds);

ULONG
BcmBtRead0(
    _In_ PBCMBT_DEVICE Dev,
    _In_ ULONG Offset);

VOID
BcmBtWrite0(
    _In_ PBCMBT_DEVICE Dev,
    _In_ ULONG Offset,
    _In_ ULONG Value);

ULONG
BcmBtRead2(
    _In_ PBCMBT_DEVICE Dev,
    _In_ ULONG Offset);

VOID
BcmBtWrite2(
    _In_ PBCMBT_DEVICE Dev,
    _In_ ULONG Offset,
    _In_ ULONG Value);

NTSTATUS
BcmBtResetFunction(
    _In_ PBCMBT_DEVICE Dev);

NTSTATUS
BcmBtMapDma(
    _In_ PBCMBT_DEVICE Dev,
    _In_ PVOID Buffer,
    _In_ SIZE_T Length,
    _Out_ PULONG64 Iova);

VOID
BcmBtUnmapDma(
    _In_ PBCMBT_DEVICE Dev,
    _In_ ULONG64 Iova,
    _In_ SIZE_T Length);

NTSTATUS
BcmBtAllocateShared(
    _In_ PBCMBT_DEVICE Dev);

NTSTATUS
BcmBtResetShared(
    _In_ PBCMBT_DEVICE Dev);

VOID
BcmBtFreeShared(
    _In_ PBCMBT_DEVICE Dev);

NTSTATUS
BcmBtEnqueue(
    _In_ PBCMBT_DEVICE Dev,
    _In_ UCHAR RingId,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait);

NTSTATUS
BcmBtCreateRings(
    _In_ PBCMBT_DEVICE Dev);

VOID
BcmBtDestroyRings(
    _In_ PBCMBT_DEVICE Dev);

VOID
BcmBtPollCompletions(
    _In_ PBCMBT_DEVICE Dev);

BOOLEAN
BcmBtUpdateState(
    _In_ PBCMBT_DEVICE Dev);

NTSTATUS
BcmBtBringUp(
    _In_ PBCMBT_DEVICE Dev);

NTSTATUS
BcmBtReadPlatform(
    _In_ PBCMBT_DEVICE Dev);

NTSTATUS
BcmBtReadOtp(
    _In_ PBCMBT_DEVICE Dev);

VOID
BcmBtQueuePacket(
    _In_ PBCMBT_DEVICE Dev,
    _In_ UCHAR Type,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);

PBCMBT_PACKET
BcmBtTakePacket(
    _In_ PBCMBT_DEVICE Dev);

VOID
BcmBtDeliverPackets(
    _In_ PBCMBT_DEVICE Dev);

VOID
BcmBtFlushPackets(
    _In_ PBCMBT_DEVICE Dev);

NTSTATUS
BcmBtCreatePdo(
    _In_ PBCMBT_DEVICE Dev);
