/*
 * PROJECT:     LiberNT Hyper-V Storage Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     StorPort miniport for the Hyper-V synthetic SCSI controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <storport.h>
#include <ntddstor.h>
#include <vmbusapi.h>

#define NDEBUG
#include <debug.h>

#define STORVSC_RING_SIZE          (128 * 1024)
#define STORVSC_PACKET_SIZE        64
#define STORVSC_RECEIVE_SIZE       256
#define STORVSC_MAX_TRANSFER       (1024 * 1024)
#define STORVSC_MAX_PFNS           ((STORVSC_MAX_TRANSFER / PAGE_SIZE) + 1)
#define STORVSC_MAX_TARGETS        2
#define STORVSC_MAX_LUNS           64
#define STORVSC_WAIT_MS            10000
#define STORVSC_CONTROL_TID        0x53564343ULL

#define VSTOR_OP_COMPLETE_IO            1
#define VSTOR_OP_REMOVE_DEVICE          2
#define VSTOR_OP_EXECUTE_SRB            3
#define VSTOR_OP_BEGIN_INITIALIZATION   7
#define VSTOR_OP_END_INITIALIZATION     8
#define VSTOR_OP_QUERY_PROTOCOL_VERSION 9
#define VSTOR_OP_QUERY_PROPERTIES       10
#define VSTOR_OP_ENUMERATE_BUS          11

#define VSTOR_FLAG_REQUEST_COMPLETION 1

#define VSTOR_VERSION_WIN7       0x0402
#define VSTOR_VERSION_WIN8       0x0501

#define VSTOR_REQUEST_LENGTH_V1  0x24
#define VSTOR_REQUEST_LENGTH_V2  0x34
#define VSTOR_SENSE_SIZE_V1      0x12
#define VSTOR_SENSE_SIZE_V2      0x14

#define VSTOR_DATA_OUT      0
#define VSTOR_DATA_IN       1
#define VSTOR_DATA_UNKNOWN  2

#define VSTOR_OFFSET_OPERATION       0
#define VSTOR_OFFSET_FLAGS           4
#define VSTOR_OFFSET_STATUS          8
#define VSTOR_OFFSET_LENGTH          12
#define VSTOR_OFFSET_SRB_STATUS      14
#define VSTOR_OFFSET_SCSI_STATUS     15
#define VSTOR_OFFSET_PATH            17
#define VSTOR_OFFSET_TARGET          18
#define VSTOR_OFFSET_LUN             19
#define VSTOR_OFFSET_CDB_LENGTH      20
#define VSTOR_OFFSET_SENSE_LENGTH    21
#define VSTOR_OFFSET_DATA_IN         22
#define VSTOR_OFFSET_TRANSFER_LENGTH 24
#define VSTOR_OFFSET_PAYLOAD         28
#define VSTOR_OFFSET_SRB_FLAGS       52
#define VSTOR_OFFSET_TIMEOUT         56
#define VSTOR_OFFSET_VERSION         12
#define VSTOR_OFFSET_MAX_TRANSFER    24

#define VSTOR_PAYLOAD_SIZE 20

typedef struct _STORVSC_SRB_EXTENSION
{
    PSCSI_REQUEST_BLOCK Srb;
    UCHAR Packet[STORVSC_PACKET_SIZE];
    ULONG64 Pfns[STORVSC_MAX_PFNS];
} STORVSC_SRB_EXTENSION, *PSTORVSC_SRB_EXTENSION;

typedef struct _STORVSC_EXTENSION
{
    PVMBUS_CHANNEL Channel;
    BOOLEAN Open;
    BOOLEAN Ready;
    USHORT Version;
    ULONG MaxTransfer;
    KEVENT ControlEvent;
    UCHAR ControlResponse[STORVSC_PACKET_SIZE];
    UCHAR Receive[STORVSC_RECEIVE_SIZE];
} STORVSC_EXTENSION, *PSTORVSC_EXTENSION;

static const USHORT StorvscVersions[] = { 0x0602, 0x0600, 0x0501, 0x0402, 0x0200 };

static
VOID
StorvscPut32(
    _Out_ PUCHAR Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    RtlCopyMemory(Buffer + Offset, &Value, sizeof(Value));
}

static
ULONG
StorvscGet32(
    _In_ const UCHAR *Buffer,
    _In_ ULONG Offset)
{
    ULONG Value;

    RtlCopyMemory(&Value, Buffer + Offset, sizeof(Value));
    return Value;
}

static
NTSTATUS
StorvscControl(
    _Inout_ PSTORVSC_EXTENSION Extension,
    _In_ ULONG Operation,
    _In_reads_bytes_opt_(PayloadLength) const VOID *Payload,
    _In_ ULONG PayloadLength)
{
    UCHAR Packet[STORVSC_PACKET_SIZE];
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    RtlZeroMemory(Packet, sizeof(Packet));
    StorvscPut32(Packet, VSTOR_OFFSET_OPERATION, Operation);
    StorvscPut32(Packet, VSTOR_OFFSET_FLAGS, VSTOR_FLAG_REQUEST_COMPLETION);
    if (Payload != NULL)
        RtlCopyMemory(Packet + VSTOR_OFFSET_LENGTH, Payload, min(PayloadLength, sizeof(Packet) - VSTOR_OFFSET_LENGTH));

    KeClearEvent(&Extension->ControlEvent);
    Status = VmbChannelSendPacket(Extension->Channel,
                                  VMBUS_PACKET_TYPE_INBAND,
                                  VMBUS_PACKET_FLAG_COMPLETION_REQUESTED,
                                  STORVSC_CONTROL_TID,
                                  Packet,
                                  sizeof(Packet));
    if (!NT_SUCCESS(Status))
        return Status;

    Timeout.QuadPart = -10000LL * STORVSC_WAIT_MS;
    if (KeWaitForSingleObject(&Extension->ControlEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
        return STATUS_IO_TIMEOUT;

    if (StorvscGet32(Extension->ControlResponse, VSTOR_OFFSET_OPERATION) != VSTOR_OP_COMPLETE_IO)
        return STATUS_UNSUCCESSFUL;

    return (NTSTATUS)StorvscGet32(Extension->ControlResponse, VSTOR_OFFSET_STATUS);
}

static
VOID
StorvscCompleteSrb(
    _In_ PSTORVSC_EXTENSION Extension,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_reads_bytes_(Length) const UCHAR *Packet,
    _In_ ULONG Length)
{
    UCHAR SrbStatus;
    UCHAR ScsiStatus;
    ULONG Transferred;
    ULONG SenseLength;
    BOOLEAN Underrun;

    if (Length < VSTOR_OFFSET_PAYLOAD + VSTOR_PAYLOAD_SIZE ||
        StorvscGet32(Packet, VSTOR_OFFSET_OPERATION) != VSTOR_OP_COMPLETE_IO)
    {
        Srb->SrbStatus = SRB_STATUS_ERROR;
        StorPortNotification(RequestComplete, Extension, Srb);
        return;
    }

    SrbStatus = Packet[VSTOR_OFFSET_SRB_STATUS];
    ScsiStatus = Packet[VSTOR_OFFSET_SCSI_STATUS];
    Transferred = StorvscGet32(Packet, VSTOR_OFFSET_TRANSFER_LENGTH);
    Underrun = Transferred < Srb->DataTransferLength;

    Srb->ScsiStatus = ScsiStatus;
    if (Underrun)
        Srb->DataTransferLength = Transferred;

    if (ScsiStatus == SCSISTAT_CHECK_CONDITION && (SrbStatus & SRB_STATUS_AUTOSENSE_VALID) &&
        Srb->SenseInfoBuffer != NULL && Srb->SenseInfoBufferLength != 0)
    {
        SenseLength = min((ULONG)Srb->SenseInfoBufferLength, (ULONG)VSTOR_PAYLOAD_SIZE);
        RtlCopyMemory(Srb->SenseInfoBuffer, Packet + VSTOR_OFFSET_PAYLOAD, SenseLength);
        Srb->SenseInfoBufferLength = (UCHAR)SenseLength;
    }
    else
    {
        SrbStatus &= ~SRB_STATUS_AUTOSENSE_VALID;
    }

    if (SRB_STATUS(SrbStatus) == SRB_STATUS_SUCCESS && Underrun)
        SrbStatus = SRB_STATUS_DATA_OVERRUN | (SrbStatus & (SRB_STATUS_QUEUE_FROZEN | SRB_STATUS_AUTOSENSE_VALID));

    Srb->SrbStatus = SrbStatus;
    StorPortNotification(RequestComplete, Extension, Srb);
}

static
VOID
NTAPI
StorvscChannelCallback(
    _In_opt_ PVOID Context)
{
    PSTORVSC_EXTENSION Extension = Context;
    PVMBUS_PACKET_DESCRIPTOR Descriptor;
    PSTORVSC_SRB_EXTENSION SrbExtension;
    PUCHAR Payload;
    ULONG Length;
    ULONG Offset;
    ULONG Operation;
    NTSTATUS Status;

    if (Extension == NULL)
        return;

    for (;;)
    {
        Status = VmbChannelReceivePacket(Extension->Channel, Extension->Receive, sizeof(Extension->Receive), &Length);
        if (!NT_SUCCESS(Status))
        {
            if (Status != STATUS_NO_MORE_ENTRIES)
                DPRINT1("STORVSC: receive failed with 0x%lx (length %lu)\n", Status, Length);
            break;
        }

        Descriptor = (PVMBUS_PACKET_DESCRIPTOR)Extension->Receive;
        Offset = (ULONG)Descriptor->DataOffset8 * 8;
        if (Offset < sizeof(VMBUS_PACKET_DESCRIPTOR) || Offset + VSTOR_OFFSET_LENGTH > Length)
            continue;

        Payload = Extension->Receive + Offset;
        Length -= Offset;

        if (Descriptor->Type == VMBUS_PACKET_TYPE_COMPLETION)
        {
            if (Descriptor->TransactionId == STORVSC_CONTROL_TID)
            {
                RtlZeroMemory(Extension->ControlResponse, sizeof(Extension->ControlResponse));
                RtlCopyMemory(Extension->ControlResponse, Payload, min(Length, (ULONG)sizeof(Extension->ControlResponse)));
                KeSetEvent(&Extension->ControlEvent, IO_NO_INCREMENT, FALSE);
                continue;
            }

            SrbExtension = (PSTORVSC_SRB_EXTENSION)(ULONG_PTR)Descriptor->TransactionId;
            if (SrbExtension != NULL && SrbExtension->Srb != NULL)
                StorvscCompleteSrb(Extension, SrbExtension->Srb, Payload, Length);
            continue;
        }

        if (Descriptor->Type == VMBUS_PACKET_TYPE_INBAND)
        {
            Operation = StorvscGet32(Payload, VSTOR_OFFSET_OPERATION);
            if (Operation == VSTOR_OP_ENUMERATE_BUS || Operation == VSTOR_OP_REMOVE_DEVICE)
                StorPortNotification(BusChangeDetected, Extension, 0);
        }
    }
}

static
NTSTATUS
StorvscConnect(
    _Inout_ PSTORVSC_EXTENSION Extension)
{
    USHORT Version[2];
    ULONG Index;
    ULONG MaxTransfer;
    NTSTATUS Status;

    Status = VmbChannelOpen(Extension->Channel, STORVSC_RING_SIZE, StorvscChannelCallback, Extension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("STORVSC: VmbChannelOpen failed with 0x%lx\n", Status);
        return Status;
    }
    Extension->Open = TRUE;

    Status = StorvscControl(Extension, VSTOR_OP_BEGIN_INITIALIZATION, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("STORVSC: begin initialization failed with 0x%lx\n", Status);
        return Status;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(StorvscVersions); Index++)
    {
        Version[0] = StorvscVersions[Index];
        Version[1] = 0;
        Status = StorvscControl(Extension, VSTOR_OP_QUERY_PROTOCOL_VERSION, Version, sizeof(Version));
        if (NT_SUCCESS(Status))
        {
            Extension->Version = StorvscVersions[Index];
            break;
        }
    }

    if (Extension->Version == 0)
    {
        DPRINT1("STORVSC: host accepted no protocol version\n");
        return STATUS_REVISION_MISMATCH;
    }

    Extension->MaxTransfer = STORVSC_MAX_TRANSFER;
    Status = StorvscControl(Extension, VSTOR_OP_QUERY_PROPERTIES, NULL, 0);
    if (NT_SUCCESS(Status))
    {
        MaxTransfer = StorvscGet32(Extension->ControlResponse, VSTOR_OFFSET_MAX_TRANSFER);
        if (MaxTransfer >= PAGE_SIZE)
            Extension->MaxTransfer = min(MaxTransfer, (ULONG)STORVSC_MAX_TRANSFER) & ~(PAGE_SIZE - 1);
    }

    Status = StorvscControl(Extension, VSTOR_OP_END_INITIALIZATION, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("STORVSC: end initialization failed with 0x%lx\n", Status);
        return Status;
    }

    Extension->Ready = TRUE;
    return STATUS_SUCCESS;
}

static
ULONG
NTAPI
StorvscFindAdapter(
    _In_ PVOID DeviceExtension,
    _In_ PVOID HwContext,
    _In_ PVOID BusInformation,
    _In_z_ PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _Out_ PBOOLEAN Reserved3)
{
    PSTORVSC_EXTENSION Extension = DeviceExtension;
    PVOID AdapterDevice = NULL;
    PVOID PhysicalDevice = NULL;
    PVOID LowerDevice = NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);

    *Reserved3 = FALSE;
    RtlZeroMemory(Extension, sizeof(*Extension));
    KeInitializeEvent(&Extension->ControlEvent, NotificationEvent, FALSE);

    if (StorPortGetDeviceObjects(Extension, &AdapterDevice, &PhysicalDevice, &LowerDevice) != STOR_STATUS_SUCCESS ||
        PhysicalDevice == NULL)
    {
        return SP_RETURN_NOT_FOUND;
    }

    Extension->Channel = VmbChannelFromDevice(PhysicalDevice);
    if (Extension->Channel == NULL)
        return SP_RETURN_NOT_FOUND;

    Status = StorvscConnect(Extension);
    if (!NT_SUCCESS(Status))
    {
        if (Extension->Open)
        {
            Extension->Open = FALSE;
            VmbChannelClose(Extension->Channel);
        }
        return SP_RETURN_ERROR;
    }

    StorPortSetAdapterBusType(Extension, BusTypeScsi);
    ConfigInfo->MaximumTransferLength = Extension->MaxTransfer;
    ConfigInfo->NumberOfPhysicalBreaks = Extension->MaxTransfer / PAGE_SIZE + 1;
    ConfigInfo->AlignmentMask = 0;
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->MaximumNumberOfTargets = STORVSC_MAX_TARGETS;
    ConfigInfo->MaximumNumberOfLogicalUnits = STORVSC_MAX_LUNS;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Master = TRUE;
    ConfigInfo->CachesData = FALSE;
    ConfigInfo->Dma64BitAddresses = SCSI_DMA64_MINIPORT_FULL64BIT_NO_BOUNDARY_REQ_SUPPORTED;
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    ConfigInfo->InitiatorBusId[0] = (UCHAR)STORVSC_MAX_TARGETS;
    return SP_RETURN_FOUND;
}

static
BOOLEAN
NTAPI
StorvscHwInitialize(
    _In_ PVOID DeviceExtension)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    return TRUE;
}

static
BOOLEAN
NTAPI
StorvscHwInterrupt(
    _In_ PVOID DeviceExtension)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    return FALSE;
}

static
BOOLEAN
NTAPI
StorvscHwResetBus(
    _In_ PVOID DeviceExtension,
    _In_ ULONG PathId)
{
    UNREFERENCED_PARAMETER(DeviceExtension);
    UNREFERENCED_PARAMETER(PathId);
    return TRUE;
}

static
SCSI_ADAPTER_CONTROL_STATUS
NTAPI
StorvscHwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    PSCSI_SUPPORTED_CONTROL_TYPE_LIST List;

    UNREFERENCED_PARAMETER(DeviceExtension);

    switch (ControlType)
    {
        case ScsiQuerySupportedControlTypes:
            List = Parameters;
            if (List->MaxControlType > ScsiStopAdapter)
                List->SupportedTypeList[ScsiStopAdapter] = TRUE;
            if (List->MaxControlType > ScsiQuerySupportedControlTypes)
                List->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
            return ScsiAdapterControlSuccess;

        case ScsiStopAdapter:
            return ScsiAdapterControlSuccess;

        default:
            return ScsiAdapterControlUnsuccessful;
    }
}

static
BOOLEAN
StorvscBuildPfns(
    _In_ PSTOR_SCATTER_GATHER_LIST List,
    _Out_writes_(STORVSC_MAX_PFNS) PULONG64 Pfns,
    _Out_ PULONG PfnCount,
    _Out_ PULONG ByteOffset)
{
    ULONG Index;
    ULONG Count = 0;
    ULONG64 Address;
    ULONG64 End;
    ULONG64 Page;

    *PfnCount = 0;
    *ByteOffset = 0;
    if (List == NULL || List->NumberOfElements == 0)
        return FALSE;

    *ByteOffset = List->List[0].PhysicalAddress.LowPart & (PAGE_SIZE - 1);

    for (Index = 0; Index < List->NumberOfElements; Index++)
    {
        Address = (ULONG64)List->List[Index].PhysicalAddress.QuadPart;
        End = Address + List->List[Index].Length;

        if ((Index != 0 && (Address & (PAGE_SIZE - 1)) != 0) ||
            (Index + 1 != List->NumberOfElements && (End & (PAGE_SIZE - 1)) != 0))
        {
            return FALSE;
        }

        for (Page = Address >> PAGE_SHIFT; Page <= (End - 1) >> PAGE_SHIFT; Page++)
        {
            if (Count == STORVSC_MAX_PFNS)
                return FALSE;
            Pfns[Count++] = Page;
        }
    }

    *PfnCount = Count;
    return TRUE;
}

static
UCHAR
StorvscExecuteScsi(
    _In_ PSTORVSC_EXTENSION Extension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PSTORVSC_SRB_EXTENSION SrbExtension = Srb->SrbExtension;
    PUCHAR Packet = SrbExtension->Packet;
    PSTOR_SCATTER_GATHER_LIST List;
    ULONG PfnCount;
    ULONG ByteOffset;
    ULONG SrbFlags;
    NTSTATUS Status;

    if (!Extension->Ready)
        return SRB_STATUS_NO_HBA;

    SrbExtension->Srb = Srb;
    RtlZeroMemory(Packet, STORVSC_PACKET_SIZE);
    StorvscPut32(Packet, VSTOR_OFFSET_OPERATION, VSTOR_OP_EXECUTE_SRB);
    StorvscPut32(Packet, VSTOR_OFFSET_FLAGS, VSTOR_FLAG_REQUEST_COMPLETION);

    if (Extension->Version >= VSTOR_VERSION_WIN8)
    {
        Packet[VSTOR_OFFSET_LENGTH] = VSTOR_REQUEST_LENGTH_V2;
        Packet[VSTOR_OFFSET_SENSE_LENGTH] = VSTOR_SENSE_SIZE_V2;
    }
    else
    {
        Packet[VSTOR_OFFSET_LENGTH] = VSTOR_REQUEST_LENGTH_V1;
        Packet[VSTOR_OFFSET_SENSE_LENGTH] = VSTOR_SENSE_SIZE_V1;
    }

    Packet[VSTOR_OFFSET_PATH] = Srb->PathId;
    Packet[VSTOR_OFFSET_TARGET] = Srb->TargetId;
    Packet[VSTOR_OFFSET_LUN] = Srb->Lun;
    Packet[VSTOR_OFFSET_CDB_LENGTH] = min(Srb->CdbLength, (UCHAR)16);
    RtlCopyMemory(Packet + VSTOR_OFFSET_PAYLOAD, Srb->Cdb, Packet[VSTOR_OFFSET_CDB_LENGTH]);
    StorvscPut32(Packet, VSTOR_OFFSET_TRANSFER_LENGTH, Srb->DataTransferLength);
    StorvscPut32(Packet, VSTOR_OFFSET_TIMEOUT, Srb->TimeOutValue);

    SrbFlags = Srb->SrbFlags & (SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT | SRB_FLAGS_NO_DATA_TRANSFER);
    StorvscPut32(Packet, VSTOR_OFFSET_SRB_FLAGS, SrbFlags | SRB_FLAGS_DISABLE_SYNCH_TRANSFER);

    if (Srb->SrbFlags & SRB_FLAGS_DATA_IN)
        Packet[VSTOR_OFFSET_DATA_IN] = VSTOR_DATA_IN;
    else if (Srb->SrbFlags & SRB_FLAGS_DATA_OUT)
        Packet[VSTOR_OFFSET_DATA_IN] = VSTOR_DATA_OUT;
    else
        Packet[VSTOR_OFFSET_DATA_IN] = VSTOR_DATA_UNKNOWN;

    if (Srb->DataTransferLength != 0 && (Srb->SrbFlags & (SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT)))
    {
        List = StorPortGetScatterGatherList(Extension, Srb);
        if (!StorvscBuildPfns(List, SrbExtension->Pfns, &PfnCount, &ByteOffset))
        {
            DPRINT1("STORVSC: unusable scatter/gather list for %lu bytes\n", Srb->DataTransferLength);
            return SRB_STATUS_INVALID_REQUEST;
        }

        Status = VmbChannelSendGpaDirect(Extension->Channel,
                                         (ULONG64)(ULONG_PTR)SrbExtension,
                                         Packet,
                                         STORVSC_PACKET_SIZE,
                                         ByteOffset,
                                         Srb->DataTransferLength,
                                         SrbExtension->Pfns,
                                         PfnCount);
    }
    else
    {
        Status = VmbChannelSendPacket(Extension->Channel,
                                      VMBUS_PACKET_TYPE_INBAND,
                                      VMBUS_PACKET_FLAG_COMPLETION_REQUESTED,
                                      (ULONG64)(ULONG_PTR)SrbExtension,
                                      Packet,
                                      STORVSC_PACKET_SIZE);
    }

    if (Status == STATUS_INSUFFICIENT_RESOURCES)
        return SRB_STATUS_BUSY;
    if (!NT_SUCCESS(Status))
        return SRB_STATUS_ERROR;

    return SRB_STATUS_PENDING;
}

static
BOOLEAN
NTAPI
StorvscHwStartIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PSTORVSC_EXTENSION Extension = DeviceExtension;
    UCHAR SrbStatus;

    switch (Srb->Function)
    {
        case SRB_FUNCTION_EXECUTE_SCSI:
            SrbStatus = StorvscExecuteScsi(Extension, Srb);
            break;

        case SRB_FUNCTION_RESET_LOGICAL_UNIT:
        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_RESET_BUS:
        case SRB_FUNCTION_ABORT_COMMAND:
        case SRB_FUNCTION_FLUSH:
        case SRB_FUNCTION_SHUTDOWN:
        case SRB_FUNCTION_PNP:
        case SRB_FUNCTION_POWER:
            SrbStatus = SRB_STATUS_SUCCESS;
            break;

        default:
            SrbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }

    if (SrbStatus != SRB_STATUS_PENDING)
    {
        Srb->SrbStatus = SrbStatus;
        StorPortNotification(RequestComplete, Extension, Srb);
    }

    return TRUE;
}

ULONG
NTAPI
DriverEntry(
    _In_ PVOID DriverObject,
    _In_ PVOID RegistryPath)
{
    HW_INITIALIZATION_DATA InitData;

    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.HwInitializationDataSize = sizeof(InitData);
    InitData.AdapterInterfaceType = Internal;
    InitData.HwInitialize = StorvscHwInitialize;
    InitData.HwStartIo = StorvscHwStartIo;
    InitData.HwInterrupt = StorvscHwInterrupt;
    InitData.HwFindAdapter = StorvscFindAdapter;
    InitData.HwResetBus = StorvscHwResetBus;
    InitData.HwAdapterControl = StorvscHwAdapterControl;
    InitData.DeviceExtensionSize = sizeof(STORVSC_EXTENSION);
    InitData.SrbExtensionSize = sizeof(STORVSC_SRB_EXTENSION);
    InitData.NumberOfAccessRanges = 0;
    InitData.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    InitData.NeedPhysicalAddresses = TRUE;
    InitData.TaggedQueuing = TRUE;
    InitData.AutoRequestSense = TRUE;
    InitData.MultipleRequestPerLu = TRUE;

    return StorPortInitialize(DriverObject, RegistryPath, &InitData, NULL);
}
