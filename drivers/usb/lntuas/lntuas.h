/*
 * PROJECT:     LiberNT USB Attached SCSI Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Definitions shared by the USB Attached SCSI StorPort miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <storport.h>
#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>

#define UAS_POOL_TAG                'saUL'

#define UAS_INTERFACE_CLASS         0x08
#define UAS_INTERFACE_SUBCLASS      0x06
#define UAS_INTERFACE_PROTOCOL      0x62
#define UAS_PIPE_USAGE_DESCRIPTOR   0x24
#define UAS_COMPANION_DESCRIPTOR    0x30
#define UAS_COMPANION_STREAMS       0x1F

#define UAS_PIPE_COMMAND            0
#define UAS_PIPE_STATUS             1
#define UAS_PIPE_DATA_IN            2
#define UAS_PIPE_DATA_OUT           3
#define UAS_PIPES                   4

#define UAS_IU_COMMAND              0x01
#define UAS_IU_SENSE                0x03
#define UAS_IU_RESPONSE             0x04
#define UAS_IU_TASK_MANAGEMENT      0x05
#define UAS_IU_READ_READY           0x06
#define UAS_IU_WRITE_READY          0x07

#define UAS_COMMAND_IU_SIZE         32
#define UAS_TASK_IU_SIZE            16
#define UAS_STATUS_IU_SIZE          (16 + 252)
#define UAS_SENSE_IU_HEADER         16
#define UAS_RESPONSE_IU_SIZE        8
#define UAS_READY_IU_SIZE           4

#define UAS_STATUS_GOOD             0x00
#define UAS_STATUS_CHECK_CONDITION  0x02
#define UAS_TMF_ABORT_TASK          0x01
#define UAS_TMF_LOGICAL_UNIT_RESET  0x08
#define UAS_TMF_I_T_NEXUS_RESET     0x10
#define UAS_TMF_TIMEOUT_MS          30000
#define UAS_RC_TMF_COMPLETE         0x00
#define UAS_RC_TMF_SUCCEEDED        0x08

#define UAS_MAX_STREAMS             16
#define UAS_MAX_TASKS               (UAS_MAX_STREAMS - 1)
#define UAS_MAX_TRANSFER_LENGTH     (512 * 1024)
#define UAS_ALIGNMENT_MASK          3

#define UAS_PHASE_COMMAND           0
#define UAS_PHASE_STATUS            1
#define UAS_PHASE_DATA              2
#define UAS_PHASES                  3

#define UAS_TASK_DONE(Phase)        (1L << (Phase))
#define UAS_TASK_ALL_DONE           (UAS_TASK_DONE(UAS_PHASE_COMMAND) | UAS_TASK_DONE(UAS_PHASE_STATUS) | \
                                     UAS_TASK_DONE(UAS_PHASE_DATA))
#define UAS_TASK_DATA               0x10
#define UAS_TASK_SENSE_ERROR        0x20
#define UAS_TASK_FAILED             0x40
#define UAS_TASK_DRAIN              0x100

typedef enum _UAS_TASK_STATE
{
    UasTaskFree,
    UasTaskParked,
    UasTaskActive,
    UasTaskDraining
} UAS_TASK_STATE;

typedef struct _UAS_TASK
{
    struct _UAS_ADAPTER *Adapter;
    PSCSI_REQUEST_BLOCK Srb;
    UAS_TASK_STATE State;
    USHORT Tag;
    volatile LONG Pending;
    volatile LONG Flags;
    PIRP Irp[UAS_PHASES];
    struct _URB_BULK_OR_INTERRUPT_TRANSFER Urb[UAS_PHASES];
    PMDL DataMdl;
    BOOLEAN OwnsDataMdl;
    PVOID IuAllocation;
    PUCHAR CommandIu;
    PUCHAR StatusIu;
    UCHAR DrainLun;
    BOOLEAN DrainAborted;
    BOOLEAN DrainDone;
    BOOLEAN DrainHeld;
} UAS_TASK, *PUAS_TASK;

typedef struct _UAS_ADAPTER
{
    PDEVICE_OBJECT AdapterDevice;
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    KSPIN_LOCK Lock;
    PUSB_CONFIGURATION_DESCRIPTOR Configuration;
    PUSBD_INTERFACE_INFORMATION Interface;
    UCHAR Endpoint[UAS_PIPES];
    USBD_PIPE_HANDLE Pipe[UAS_PIPES];
    BOOLEAN Streams;
    ULONG StreamCount;
    USBD_STREAM_INFORMATION StreamInfo[UAS_PIPES][UAS_MAX_STREAMS];
    ULONG TaskCount;
    UAS_TASK Task[UAS_MAX_TASKS];
    UAS_TASK ManagementTask;
    ULONG ActiveTasks;
    PSCSI_REQUEST_BLOCK ResetSrb;
    BOOLEAN ManagementStarted;
    ULONG DrainingTasks;
    PUAS_TASK AbortTask;
    BOOLEAN NexusPending;
    BOOLEAN NexusAfterReset;
    BOOLEAN TmfTimedOut;
    KTIMER TmfTimer;
    KDPC TmfDpc;
    ULONGLONG TmfDeadline;
    BOOLEAN Resetting;
    BOOLEAN PortResetQueued;
    BOOLEAN Stopped;
    KEVENT Quiesced;
    KEVENT PortResetIdle;
    PIO_WORKITEM PortResetWorkItem;
} UAS_ADAPTER, *PUAS_ADAPTER;

KDEFERRED_ROUTINE UasTmfTimeoutDpc;

BOOLEAN
UasScanConfiguration(
    _In_ PUSB_CONFIGURATION_DESCRIPTOR Configuration,
    _Out_ PUSB_INTERFACE_DESCRIPTOR *Interface,
    _Out_writes_(UAS_PIPES) PUCHAR Endpoint,
    _Out_ PULONG StreamCount);

NTSTATUS
UasSyncUrb(
    _In_ PUAS_ADAPTER Adapter,
    _Inout_ PURB Urb);

NTSTATUS
UasResetPort(
    _In_ PUAS_ADAPTER Adapter);

VOID
UasStartSrb(
    _In_ PUAS_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb);

VOID
UasResetLogicalUnit(
    _In_ PUAS_ADAPTER Adapter,
    _In_ PSCSI_REQUEST_BLOCK Srb);

VOID
UasQueuePortReset(
    _In_ PUAS_ADAPTER Adapter);

VOID
UasStopAdapter(
    _In_ PUAS_ADAPTER Adapter);

VOID
UasWaitQuiesced(
    _In_ PUAS_ADAPTER Adapter);
