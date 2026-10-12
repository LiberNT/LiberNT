/*
 * PROJECT:     LiberNT Storage Drivers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LSI Fusion-MPT SAS StorPort miniport private declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include <ntddk.h>
#include <storport.h>
#include <ntddstor.h>

#define NDEBUG
#include <debug.h>

#include "lsimpi.h"

#define LSI_ACCESS_RANGES           3
#define LSI_MAX_SLOTS               128
#define LSI_CONTEXT_SLOT_BITS       7
#define LSI_MAX_SGE                 65
#define LSI_SENSE_STRIDE            256
#define LSI_SENSE_LENGTH            255
#define LSI_REPLY_FRAME_SIZE        128
#define LSI_MAX_TARGETS             255
#define LSI_EMPTY_REPLY             0xFFFFFFFF

#define LSI_HANDSHAKE_TIMEOUT_MS    10000
#define LSI_READY_TIMEOUT_MS        60000
#define LSI_DIAG_RESET_TIMEOUT_MS   5000
#define LSI_PORT_ENABLE_TIMEOUT_MS  300000
#define LSI_TASK_TIMEOUT_MS         30000
#define LSI_WATCHDOG_INTERVAL_US    1000000

#define LSI_SLOT_FREE               0
#define LSI_SLOT_SCSI               1
#define LSI_SLOT_TASK               2
#define LSI_SLOT_INTERNAL           3

C_ASSERT(LSI_MAX_SLOTS == (1 << LSI_CONTEXT_SLOT_BITS));

typedef struct _LSI_SLOT
{
    PSCSI_REQUEST_BLOCK Srb;
    ULONG Context;
    PUCHAR Frame;
    ULONGLONG FramePhysical;
    PUCHAR Sense;
    ULONG SensePhysical;
    UCHAR Type;
    BOOLEAN Aborted;
} LSI_SLOT, *PLSI_SLOT;

typedef struct _LSI_DEVICE_EXTENSION
{
    PUCHAR Registers;
    PUCHAR Uncached;
    ULONGLONG UncachedPhysical;
    ULONG UncachedSize;
    PUCHAR ReplyFrames;
    ULONG ReplyFramesPhysical;
    ULONG ReplyFrameCount;
    ULONG FrameHighAddress;
    ULONG SenseHighAddress;
    ULONG FrameSize;
    ULONG ChainSegments;
    ULONG MaxSge;
    ULONG SlotCount;
    ULONG NextSlot;
    ULONG Sequence;
    ULONG BusCount;
    ULONG TargetCount;
    USHORT RequestFrameSize;
    UCHAR MaxDevices;
    UCHAR MaxBuses;
    USHORT PortSCSIID;
    USHORT PortMaxDevices;
    UCHAR PortType;
    BOOLEAN Operational;
    BOOLEAN InterruptsEnabled;
    BOOLEAN InternalTaskDone;
    BOOLEAN InternalTaskSucceeded;
    volatile LONG WatchdogActive;
    volatile LONG WatchdogBusy;
    LSI_SLOT Slots[LSI_MAX_SLOTS];
} LSI_DEVICE_EXTENSION, *PLSI_DEVICE_EXTENSION;

ULONG
LsiReadRegister(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG Offset);

VOID
LsiWriteRegister(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG Offset,
    _In_ ULONG Value);

BOOLEAN
LsiMakeReady(
    _In_ PLSI_DEVICE_EXTENSION Device);

BOOLEAN
LsiGetIocFacts(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _Out_ PMPI_IOC_FACTS_REPLY Facts);

BOOLEAN
LsiStartIoc(
    _In_ PLSI_DEVICE_EXTENSION Device);

BOOLEAN
LsiReinitializeIoc(
    _In_ PLSI_DEVICE_EXTENSION Device);

VOID
LsiStopIoc(
    _In_ PLSI_DEVICE_EXTENSION Device);

VOID
LsiStartScsi(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PSCSI_REQUEST_BLOCK Srb);

VOID
LsiStartTask(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PSCSI_REQUEST_BLOCK Srb);

VOID
LsiCompleteRequest(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR SrbStatus);

VOID
LsiProcessReplies(
    _In_ PLSI_DEVICE_EXTENSION Device);

VOID
LsiFailOutstanding(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ UCHAR SrbStatus);

BOOLEAN
LsiResetBusLocked(
    _In_ PLSI_DEVICE_EXTENSION Device,
    _In_ ULONG PathId);
