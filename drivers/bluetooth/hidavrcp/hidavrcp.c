/*
 * PROJECT:     LiberNT Bluetooth AVRCP HID miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AVRCP target for remote control PASS THROUGH commands, reported as media keys
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define _HIDPI_
#define _HIDPI_NO_FUNCTION_MACROS_
#include <ntddk.h>
#include <hidport.h>
#include <initguid.h>
#include <bthdef.h>
#include <bthioctl.h>
#include <bthddi.h>
#include <bthguid.h>
#include <bthminiddi.h>
#include <bthvolume.h>

#define NDEBUG
#include <debug.h>

#define HIDAVRCP_TAG                    'cRvA'
#define HIDAVRCP_READS                  2
#define HIDAVRCP_BUFFER_SIZE            512
#define HIDAVRCP_QUEUED_REPORTS         16
#define HIDAVRCP_CLOSE_TIMEOUT_MS       5000

#define AVCTP_COMMAND                   0x00
#define AVCTP_RESPONSE                  0x02
#define AVCTP_INVALID_PID               0x01
#define AVCTP_PACKET_SINGLE             0x00

#define AVC_CTYPE_CONTROL               0x0
#define AVC_CTYPE_STATUS                0x1
#define AVC_CTYPE_NOTIFY                0x3
#define AVC_RESPONSE_NOT_IMPLEMENTED    0x8
#define AVC_RESPONSE_ACCEPTED           0x9
#define AVC_RESPONSE_REJECTED           0xA
#define AVC_RESPONSE_STABLE             0xC
#define AVC_RESPONSE_CHANGED            0xD
#define AVC_RESPONSE_INTERIM            0xF
#define AVC_SUBUNIT_PANEL               0x09
#define AVC_SUBUNIT_UNIT                0x1F
#define AVC_OPCODE_VENDOR_DEPENDENT     0x00
#define AVC_OPCODE_UNIT_INFO            0x30
#define AVC_OPCODE_SUBUNIT_INFO         0x31
#define AVC_OPCODE_PASS_THROUGH         0x7C

#define AVRCP_VENDOR_HEADER             13
#define AVRCP_PDU_GET_CAPABILITIES      0x10
#define AVRCP_PDU_REGISTER_NOTIFICATION 0x31
#define AVRCP_PDU_SET_ABSOLUTE_VOLUME   0x50
#define AVRCP_CAPABILITY_EVENTS         0x03
#define AVRCP_EVENT_VOLUME_CHANGED      0x0D

#define AVRCP_OP_VOLUME_UP              0x41
#define AVRCP_OP_VOLUME_DOWN            0x42
#define AVRCP_OP_MUTE                   0x43
#define AVRCP_OP_PLAY                   0x44
#define AVRCP_OP_STOP                   0x45
#define AVRCP_OP_PAUSE                  0x46
#define AVRCP_OP_FORWARD                0x4B
#define AVRCP_OP_BACKWARD               0x4C

#define HIDAVRCP_KEY_PLAY_PAUSE         0x01
#define HIDAVRCP_KEY_STOP               0x02
#define HIDAVRCP_KEY_NEXT               0x04
#define HIDAVRCP_KEY_PREVIOUS           0x08
#define HIDAVRCP_KEY_VOLUME_UP          0x10
#define HIDAVRCP_KEY_VOLUME_DOWN        0x20
#define HIDAVRCP_KEY_MUTE               0x40

typedef struct _HIDAVRCP_DEVICE_EXTENSION HIDAVRCP_DEVICE_EXTENSION, *PHIDAVRCP_DEVICE_EXTENSION;

typedef struct _HIDAVRCP_TRANSFER
{
    PHIDAVRCP_DEVICE_EXTENSION Extension;
    volatile LONG Active;
    PIRP Irp;
    BRB Brb;
    UCHAR Buffer[HIDAVRCP_BUFFER_SIZE];
} HIDAVRCP_TRANSFER, *PHIDAVRCP_TRANSFER;

struct _HIDAVRCP_DEVICE_EXTENSION
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDevice;
    BTH_PROFILE_DRIVER_INTERFACE Profile;
    BOOLEAN HaveProfile;
    BTH_ADDR Address;
    BOOLEAN PsmRegistered;
    L2CAP_SERVER_HANDLE Server;
    HANDLE_SDP SdpRecord;
    HANDLE_SDP ControllerSdpRecord;
    KSPIN_LOCK Lock;
    BOOLEAN Started;
    ULONG Opening;
    L2CAP_CHANNEL_HANDLE Channel;
    HIDAVRCP_TRANSFER Reads[HIDAVRCP_READS];
    volatile LONG IoOutstanding;
    KEVENT IoDrained;
    UCHAR Keys;
    LIST_ENTRY PendingReads;
    UCHAR ReportQueue[HIDAVRCP_QUEUED_REPORTS];
    ULONG ReportQueueHead;
    ULONG ReportQueueCount;
    volatile LONG NextLabel;
    volatile LONG AbsoluteVolume;
    PCALLBACK_OBJECT VolumeSetCallback;
    PCALLBACK_OBJECT VolumeStateCallback;
    PVOID VolumeSetRegistration;
};

typedef struct _HIDAVRCP_OPERATION
{
    PHIDAVRCP_DEVICE_EXTENSION Extension;
    PIRP Irp;
    BRB Brb;
    UCHAR Buffer[HIDAVRCP_BUFFER_SIZE];
} HIDAVRCP_OPERATION, *PHIDAVRCP_OPERATION;

static const UCHAR HidAvrcpReportDescriptor[] =
{
    0x05, 0x01,
    0x09, 0x06,
    0xA1, 0x01,
    0x05, 0x07,
    0x19, 0xE0,
    0x29, 0xE7,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,
    0x05, 0x0C,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x07,
    0x09, 0xCD,
    0x09, 0xB7,
    0x09, 0xB5,
    0x09, 0xB6,
    0x09, 0xE9,
    0x09, 0xEA,
    0x09, 0xE2,
    0x81, 0x02,
    0x95, 0x01,
    0x81, 0x01,
    0xC0
};

static const UCHAR HidAvrcpTargetRecord[] =
{
    0x35, 0x38,
    0x09, 0x00, 0x01, 0x35, 0x03, 0x19, 0x11, 0x0C,
    0x09, 0x00, 0x04, 0x35, 0x10,
    0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x17,
    0x35, 0x06, 0x19, 0x00, 0x17, 0x09, 0x01, 0x03,
    0x09, 0x00, 0x05, 0x35, 0x03, 0x19, 0x10, 0x02,
    0x09, 0x00, 0x09, 0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x0E, 0x09, 0x01, 0x03,
    0x09, 0x03, 0x11, 0x09, 0x00, 0x01
};

static const UCHAR HidAvrcpControllerRecord[] =
{
    0x35, 0x3B,
    0x09, 0x00, 0x01, 0x35, 0x06, 0x19, 0x11, 0x0E, 0x19, 0x11, 0x0F,
    0x09, 0x00, 0x04, 0x35, 0x10,
    0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x17,
    0x35, 0x06, 0x19, 0x00, 0x17, 0x09, 0x01, 0x04,
    0x09, 0x00, 0x05, 0x35, 0x03, 0x19, 0x10, 0x02,
    0x09, 0x00, 0x09, 0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x0E, 0x09, 0x01, 0x06,
    0x09, 0x03, 0x11, 0x09, 0x00, 0x03
};

static CALLBACK_FUNCTION HidAvrcpVolumeSet;
static IO_COMPLETION_ROUTINE HidAvrcpReadComplete;
static IO_COMPLETION_ROUTINE HidAvrcpOperationComplete;
static IO_COMPLETION_ROUTINE HidAvrcpOpenComplete;

static
PHIDAVRCP_DEVICE_EXTENSION
HidAvrcpGetExtension(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return ((PHID_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->MiniDeviceExtension;
}

static
VOID
HidAvrcpIoDone(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension)
{
    if (InterlockedDecrement(&Extension->IoOutstanding) == 0)
        KeSetEvent(&Extension->IoDrained, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
HidAvrcpCallLower(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, Extension->LowerDevice, NULL, 0, NULL, 0, TRUE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->Parameters.Others.Argument1 = Argument1;
    IoStack->Parameters.Others.Argument2 = Argument2;
    Status = IoCallDriver(Extension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
HidAvrcpCallBuffered(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputLength) PVOID Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, Extension->LowerDevice, Input, InputLength, Output, OutputLength,
                                        FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(Extension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
VOID
HidAvrcpSubmitAsync(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _In_ PIRP Irp,
    _Inout_ PBRB Brb,
    _In_ PIO_COMPLETION_ROUTINE Completion,
    _In_ PVOID Context)
{
    PIO_STACK_LOCATION IoStack;

    IoReuseIrp(Irp, STATUS_SUCCESS);
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_BTH_SUBMIT_BRB;
    IoStack->Parameters.Others.Argument1 = Brb;
    IoSetCompletionRoutine(Irp, Completion, Context, TRUE, TRUE, TRUE);
    InterlockedIncrement(&Extension->IoOutstanding);
    IoCallDriver(Extension->LowerDevice, Irp);
}

static
PHIDAVRCP_OPERATION
HidAvrcpAllocateOperation(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _In_ BRB_TYPE Type)
{
    PHIDAVRCP_OPERATION Operation = ExAllocatePoolZero(NonPagedPool, sizeof(*Operation), HIDAVRCP_TAG);

    if (!Operation)
        return NULL;
    Operation->Irp = IoAllocateIrp(Extension->LowerDevice->StackSize, FALSE);
    if (!Operation->Irp)
    {
        ExFreePoolWithTag(Operation, HIDAVRCP_TAG);
        return NULL;
    }
    Operation->Extension = Extension;
    Extension->Profile.BthInitializeBrb(&Operation->Brb, Type);
    return Operation;
}

static
NTSTATUS
NTAPI
HidAvrcpOperationComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PHIDAVRCP_OPERATION Operation = Context;
    PHIDAVRCP_DEVICE_EXTENSION Extension = Operation->Extension;

    UNREFERENCED_PARAMETER(DeviceObject);
    IoFreeIrp(Irp);
    ExFreePoolWithTag(Operation, HIDAVRCP_TAG);
    HidAvrcpIoDone(Extension);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
HidAvrcpSend(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    struct _BRB_L2CA_ACL_TRANSFER *Transfer;
    PHIDAVRCP_OPERATION Operation;
    L2CAP_CHANNEL_HANDLE Channel;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Channel = Extension->Started ? Extension->Channel : NULL;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    if (!Channel || Length > HIDAVRCP_BUFFER_SIZE)
        return;
    Operation = HidAvrcpAllocateOperation(Extension, BRB_L2CA_ACL_TRANSFER);
    if (!Operation)
        return;
    RtlCopyMemory(Operation->Buffer, Data, Length);
    Transfer = &Operation->Brb.BrbL2caAclTransfer;
    Transfer->BtAddress = Extension->Address;
    Transfer->ChannelHandle = Channel;
    Transfer->TransferFlags = ACL_TRANSFER_DIRECTION_OUT;
    Transfer->BufferSize = Length;
    Transfer->Buffer = Operation->Buffer;
    HidAvrcpSubmitAsync(Extension, Operation->Irp, &Operation->Brb, HidAvrcpOperationComplete, Operation);
}

static
VOID
NTAPI
HidAvrcpCancelRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDAVRCP_DEVICE_EXTENSION Extension = HidAvrcpGetExtension(DeviceObject);
    KIRQL OldIrql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
PIRP
HidAvrcpRemovePendingRead(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension)
{
    while (!IsListEmpty(&Extension->PendingReads))
    {
        PIRP Irp = CONTAINING_RECORD(RemoveHeadList(&Extension->PendingReads), IRP, Tail.Overlay.ListEntry);

        if (IoSetCancelRoutine(Irp, NULL))
            return Irp;
        InitializeListHead(&Irp->Tail.Overlay.ListEntry);
    }
    return NULL;
}

static
VOID
HidAvrcpCompleteRead(
    _Inout_ PIRP Irp,
    _In_ UCHAR Report)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);

    if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < 2)
    {
        Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
        Irp->IoStatus.Information = 0;
    }
    else
    {
        ((PUCHAR)Irp->UserBuffer)[0] = 0;
        ((PUCHAR)Irp->UserBuffer)[1] = Report;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 2;
    }
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
VOID
HidAvrcpSetKey(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _In_ UCHAR Key,
    _In_ BOOLEAN Pressed)
{
    KIRQL OldIrql;
    UCHAR Report;
    PIRP Irp;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (Pressed)
        Extension->Keys |= Key;
    else
        Extension->Keys &= (UCHAR)~Key;
    Report = Extension->Keys;
    Irp = HidAvrcpRemovePendingRead(Extension);
    if (!Irp)
    {
        if (Extension->ReportQueueCount == HIDAVRCP_QUEUED_REPORTS)
        {
            Extension->ReportQueueHead = (Extension->ReportQueueHead + 1) % HIDAVRCP_QUEUED_REPORTS;
            Extension->ReportQueueCount--;
        }
        Extension->ReportQueue[(Extension->ReportQueueHead + Extension->ReportQueueCount) % HIDAVRCP_QUEUED_REPORTS] =
            Report;
        Extension->ReportQueueCount++;
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    if (Irp)
        HidAvrcpCompleteRead(Irp, Report);
}

static
UCHAR
HidAvrcpKey(
    _In_ UCHAR Operation)
{
    switch (Operation)
    {
        case AVRCP_OP_PLAY:
        case AVRCP_OP_PAUSE:
            return HIDAVRCP_KEY_PLAY_PAUSE;
        case AVRCP_OP_STOP:
            return HIDAVRCP_KEY_STOP;
        case AVRCP_OP_FORWARD:
            return HIDAVRCP_KEY_NEXT;
        case AVRCP_OP_BACKWARD:
            return HIDAVRCP_KEY_PREVIOUS;
        case AVRCP_OP_VOLUME_UP:
            return HIDAVRCP_KEY_VOLUME_UP;
        case AVRCP_OP_VOLUME_DOWN:
            return HIDAVRCP_KEY_VOLUME_DOWN;
        case AVRCP_OP_MUTE:
            return HIDAVRCP_KEY_MUTE;
        default:
            return 0;
    }
}

static
VOID
HidAvrcpVendorCommand(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _In_ UCHAR Ctype,
    _In_ UCHAR Pdu,
    _In_reads_bytes_(Length) const UCHAR *Parameters,
    _In_ USHORT Length)
{
    UCHAR Frame[AVRCP_VENDOR_HEADER + 8];
    UCHAR Label = (UCHAR)(InterlockedIncrement(&Extension->NextLabel) & 0x0F);

    if (Length > sizeof(Frame) - AVRCP_VENDOR_HEADER)
        return;
    Frame[0] = (UCHAR)(Label << 4);
    Frame[1] = (UCHAR)(AVRemoteControlServiceClassID_UUID16 >> 8);
    Frame[2] = (UCHAR)AVRemoteControlServiceClassID_UUID16;
    Frame[3] = Ctype;
    Frame[4] = AVC_SUBUNIT_PANEL << 3;
    Frame[5] = AVC_OPCODE_VENDOR_DEPENDENT;
    Frame[6] = 0x00;
    Frame[7] = 0x19;
    Frame[8] = 0x58;
    Frame[9] = Pdu;
    Frame[10] = 0x00;
    Frame[11] = (UCHAR)(Length >> 8);
    Frame[12] = (UCHAR)Length;
    RtlCopyMemory(Frame + AVRCP_VENDOR_HEADER, Parameters, Length);
    HidAvrcpSend(Extension, Frame, AVRCP_VENDOR_HEADER + Length);
}

static
VOID
HidAvrcpReportVolume(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _In_ BOOLEAN Supported,
    _In_ UCHAR Level)
{
    BTH_ABSOLUTE_VOLUME Volume;

    InterlockedExchange(&Extension->AbsoluteVolume, Supported);
    if (!Extension->VolumeStateCallback)
        return;
    Volume.Address = Extension->Address;
    Volume.Supported = Supported;
    Volume.Level = Level & BTH_VOLUME_MAXIMUM_LEVEL;
    ExNotifyCallback(Extension->VolumeStateCallback, &Volume, NULL);
}

static
VOID
HidAvrcpRegisterVolumeNotification(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension)
{
    static const UCHAR Parameters[] = { AVRCP_EVENT_VOLUME_CHANGED, 0, 0, 0, 0 };

    HidAvrcpVendorCommand(Extension, AVC_CTYPE_NOTIFY, AVRCP_PDU_REGISTER_NOTIFICATION, Parameters, sizeof(Parameters));
}

static
VOID
HidAvrcpHandleResponse(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    const UCHAR *Parameters = Data + AVRCP_VENDOR_HEADER;
    UCHAR Response, Pdu;
    ULONG ParameterLength, Index;

    if (Length < AVRCP_VENDOR_HEADER || Data[5] != AVC_OPCODE_VENDOR_DEPENDENT || Data[6] != 0x00 ||
        Data[7] != 0x19 || Data[8] != 0x58 || Data[10] != 0x00)
        return;
    Response = Data[3] & 0x0F;
    Pdu = Data[9];
    ParameterLength = (Data[11] << 8) | Data[12];
    if (AVRCP_VENDOR_HEADER + ParameterLength > Length)
        return;

    switch (Pdu)
    {
        case AVRCP_PDU_GET_CAPABILITIES:
            if (Response != AVC_RESPONSE_STABLE || ParameterLength < 2 || Parameters[0] != AVRCP_CAPABILITY_EVENTS)
                return;
            for (Index = 0; Index < Parameters[1] && 2 + Index < ParameterLength; Index++)
            {
                if (Parameters[2 + Index] == AVRCP_EVENT_VOLUME_CHANGED)
                {
                    HidAvrcpRegisterVolumeNotification(Extension);
                    return;
                }
            }
            return;

        case AVRCP_PDU_REGISTER_NOTIFICATION:
            if (ParameterLength < 1 || Parameters[0] != AVRCP_EVENT_VOLUME_CHANGED)
                return;
            if ((Response == AVC_RESPONSE_INTERIM || Response == AVC_RESPONSE_CHANGED) && ParameterLength >= 2)
            {
                if (!Extension->AbsoluteVolume)
                    DPRINT1("HIDAVRCP: %012I64x supports absolute volume, level %u\n", Extension->Address,
                            Parameters[1] & BTH_VOLUME_MAXIMUM_LEVEL);
                HidAvrcpReportVolume(Extension, TRUE, Parameters[1]);
                if (Response == AVC_RESPONSE_CHANGED)
                    HidAvrcpRegisterVolumeNotification(Extension);
            }
            else if (Response == AVC_RESPONSE_REJECTED || Response == AVC_RESPONSE_NOT_IMPLEMENTED)
            {
                HidAvrcpReportVolume(Extension, FALSE, 0);
            }
            return;

        case AVRCP_PDU_SET_ABSOLUTE_VOLUME:
            if (Response == AVC_RESPONSE_ACCEPTED && ParameterLength >= 1)
                HidAvrcpReportVolume(Extension, TRUE, Parameters[0]);
            return;

        default:
            return;
    }
}

static
VOID
NTAPI
HidAvrcpVolumeSet(
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PHIDAVRCP_DEVICE_EXTENSION Extension = CallbackContext;
    PBTH_ABSOLUTE_VOLUME Volume = Argument1;
    UCHAR Level;

    UNREFERENCED_PARAMETER(Argument2);
    if (!Extension || !Volume || Volume->Address != Extension->Address || !Extension->AbsoluteVolume)
        return;
    Level = Volume->Level & BTH_VOLUME_MAXIMUM_LEVEL;
    HidAvrcpVendorCommand(Extension, AVC_CTYPE_CONTROL, AVRCP_PDU_SET_ABSOLUTE_VOLUME, &Level, 1);
}

static
VOID
HidAvrcpHandleMessage(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    UCHAR Response[HIDAVRCP_BUFFER_SIZE];
    ULONG ResponseLength = Length;
    UCHAR Label, Ctype, Subunit, Opcode;

    if (Length < 3 || ((Data[0] >> 2) & 3) != AVCTP_PACKET_SINGLE)
        return;
    if (Data[0] & AVCTP_RESPONSE)
    {
        if (((Data[1] << 8) | Data[2]) == AVRemoteControlServiceClassID_UUID16)
            HidAvrcpHandleResponse(Extension, Data, Length);
        return;
    }
    Label = Data[0] >> 4;
    Response[0] = (UCHAR)((Label << 4) | AVCTP_RESPONSE);
    Response[1] = Data[1];
    Response[2] = Data[2];
    if (((Data[1] << 8) | Data[2]) != AVRemoteControlServiceClassID_UUID16)
    {
        Response[0] |= AVCTP_INVALID_PID;
        HidAvrcpSend(Extension, Response, 3);
        return;
    }
    if (Length < 6 || Length > sizeof(Response))
        return;
    RtlCopyMemory(Response + 3, Data + 3, Length - 3);
    Ctype = Data[3] & 0x0F;
    Subunit = Data[4] >> 3;
    Opcode = Data[5];

    if (Opcode == AVC_OPCODE_UNIT_INFO && Ctype == AVC_CTYPE_STATUS && Length >= 11)
    {
        Response[3] = AVC_RESPONSE_STABLE;
        Response[6] = 0x07;
        Response[7] = AVC_SUBUNIT_PANEL << 3;
        Response[8] = 0xFF;
        Response[9] = 0xFF;
        Response[10] = 0xFF;
        ResponseLength = 11;
    }
    else if (Opcode == AVC_OPCODE_SUBUNIT_INFO && Ctype == AVC_CTYPE_STATUS && Length >= 11)
    {
        Response[3] = AVC_RESPONSE_STABLE;
        Response[7] = AVC_SUBUNIT_PANEL << 3;
        Response[8] = 0xFF;
        Response[9] = 0xFF;
        Response[10] = 0xFF;
        ResponseLength = 11;
    }
    else if (Opcode == AVC_OPCODE_PASS_THROUGH && Ctype == AVC_CTYPE_CONTROL && Subunit == AVC_SUBUNIT_PANEL &&
             Length >= 8)
    {
        UCHAR Key = HidAvrcpKey(Data[6] & 0x7F);

        Response[3] = Key ? AVC_RESPONSE_ACCEPTED : AVC_RESPONSE_NOT_IMPLEMENTED;
        if (Key)
            HidAvrcpSetKey(Extension, Key, !(Data[6] & 0x80));
    }
    else
    {
        Response[3] = AVC_RESPONSE_NOT_IMPLEMENTED;
    }
    HidAvrcpSend(Extension, Response, ResponseLength);
}

static
BOOLEAN
HidAvrcpPostRead(
    _In_ PHIDAVRCP_TRANSFER Transfer)
{
    PHIDAVRCP_DEVICE_EXTENSION Extension = Transfer->Extension;
    struct _BRB_L2CA_ACL_TRANSFER *Brb = &Transfer->Brb.BrbL2caAclTransfer;
    L2CAP_CHANNEL_HANDLE Channel;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Channel = Extension->Started ? Extension->Channel : NULL;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    if (!Channel)
        return FALSE;
    Extension->Profile.BthReuseBrb(&Transfer->Brb, BRB_L2CA_ACL_TRANSFER);
    Brb->BtAddress = Extension->Address;
    Brb->ChannelHandle = Channel;
    Brb->TransferFlags = ACL_TRANSFER_DIRECTION_IN | ACL_SHORT_TRANSFER_OK;
    Brb->BufferSize = sizeof(Transfer->Buffer);
    Brb->Buffer = Transfer->Buffer;
    HidAvrcpSubmitAsync(Extension, Transfer->Irp, &Transfer->Brb, HidAvrcpReadComplete, Transfer);
    return TRUE;
}

static
NTSTATUS
NTAPI
HidAvrcpReadComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PHIDAVRCP_TRANSFER Transfer = Context;
    PHIDAVRCP_DEVICE_EXTENSION Extension = Transfer->Extension;
    L2CAP_CHANNEL_HANDLE Channel = Transfer->Brb.BrbL2caAclTransfer.ChannelHandle;
    BOOLEAN Repost;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (NT_SUCCESS(Irp->IoStatus.Status) && Transfer->Brb.BrbL2caAclTransfer.BufferSize)
        HidAvrcpHandleMessage(Extension, Transfer->Buffer, Transfer->Brb.BrbL2caAclTransfer.BufferSize);
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Repost = Extension->Started && Extension->Channel &&
             (NT_SUCCESS(Irp->IoStatus.Status) || Extension->Channel != Channel);
    if (!Repost)
        InterlockedExchange(&Transfer->Active, 0);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    if (Repost && !HidAvrcpPostRead(Transfer))
        InterlockedExchange(&Transfer->Active, 0);
    HidAvrcpIoDone(Extension);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
HidAvrcpStartReads(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension)
{
    ULONG Index;

    static const UCHAR Capability = AVRCP_CAPABILITY_EVENTS;

    DPRINT1("HIDAVRCP: %012I64x AVCTP connected\n", Extension->Address);
    for (Index = 0; Index < HIDAVRCP_READS; Index++)
    {
        PHIDAVRCP_TRANSFER Transfer = &Extension->Reads[Index];

        if (InterlockedCompareExchange(&Transfer->Active, 1, 0) == 0 && !HidAvrcpPostRead(Transfer))
            InterlockedExchange(&Transfer->Active, 0);
    }
    HidAvrcpVendorCommand(Extension, AVC_CTYPE_STATUS, AVRCP_PDU_GET_CAPABILITIES, &Capability, 1);
}

static
VOID
HidAvrcpChannelIndication(
    _In_ PVOID Context,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters)
{
    PHIDAVRCP_DEVICE_EXTENSION Extension = Context;
    PHIDAVRCP_OPERATION Operation;
    L2CAP_CHANNEL_HANDLE Handle = NULL;
    KIRQL OldIrql;

    if (Indication != IndicationRemoteDisconnect)
        return;
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (Extension->Channel == Parameters->ConnectionHandle)
    {
        Handle = Extension->Channel;
        Extension->Channel = NULL;
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    if (!Handle)
        return;
    HidAvrcpReportVolume(Extension, FALSE, 0);
    HidAvrcpSetKey(Extension, 0xFF, FALSE);
    Operation = HidAvrcpAllocateOperation(Extension, BRB_L2CA_CLOSE_CHANNEL);
    if (!Operation)
        return;
    Operation->Brb.BrbL2caCloseChannel.BtAddress = Extension->Address;
    Operation->Brb.BrbL2caCloseChannel.ChannelHandle = Handle;
    HidAvrcpSubmitAsync(Extension, Operation->Irp, &Operation->Brb, HidAvrcpOperationComplete, Operation);
}

static
VOID
HidAvrcpPrepareOpen(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _Inout_ struct _BRB_L2CA_OPEN_CHANNEL *Open)
{
    Open->BtAddress = Extension->Address;
    Open->ChannelFlags = CF_ROLE_EITHER | CF_LINK_AUTHENTICATED | CF_LINK_ENCRYPTED;
    Open->CallbackFlags = CALLBACK_DISCONNECT;
    Open->Callback = HidAvrcpChannelIndication;
    Open->CallbackContext = Extension;
    Open->ReferenceObject = Extension->DeviceObject;
    Open->IncomingQueueDepth = HIDAVRCP_QUEUED_REPORTS;
}

static
NTSTATUS
NTAPI
HidAvrcpOpenComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PHIDAVRCP_OPERATION Operation = Context;
    PHIDAVRCP_DEVICE_EXTENSION Extension = Operation->Extension;
    struct _BRB_L2CA_OPEN_CHANNEL *Open = &Operation->Brb.BrbL2caOpenChannel;
    L2CAP_CHANNEL_HANDLE Stale = NULL;
    BOOLEAN Ready = FALSE;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Extension->Opening--;
    if (NT_SUCCESS(Irp->IoStatus.Status) && Open->ChannelHandle &&
        (Open->Hdr.Type == BRB_L2CA_OPEN_CHANNEL || Open->Response == CONNECT_RSP_RESULT_SUCCESS))
    {
        if (!Extension->Started || Extension->Channel)
        {
            Stale = Open->ChannelHandle;
        }
        else
        {
            Extension->Channel = Open->ChannelHandle;
            Ready = TRUE;
        }
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    IoFreeIrp(Irp);
    if (Stale)
    {
        Extension->Profile.BthReuseBrb(&Operation->Brb, BRB_L2CA_CLOSE_CHANNEL);
        Operation->Brb.BrbL2caCloseChannel.BtAddress = Extension->Address;
        Operation->Brb.BrbL2caCloseChannel.ChannelHandle = Stale;
        Operation->Irp = IoAllocateIrp(Extension->LowerDevice->StackSize, FALSE);
        if (Operation->Irp)
        {
            HidAvrcpSubmitAsync(Extension, Operation->Irp, &Operation->Brb, HidAvrcpOperationComplete, Operation);
            HidAvrcpIoDone(Extension);
            return STATUS_MORE_PROCESSING_REQUIRED;
        }
    }
    ExFreePoolWithTag(Operation, HIDAVRCP_TAG);
    if (Ready)
        HidAvrcpStartReads(Extension);
    HidAvrcpIoDone(Extension);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
HidAvrcpServerIndication(
    _In_ PVOID Context,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters)
{
    PHIDAVRCP_DEVICE_EXTENSION Extension = Context;
    struct _BRB_L2CA_OPEN_CHANNEL *Open;
    PHIDAVRCP_OPERATION Operation;
    BOOLEAN Accept;
    KIRQL OldIrql;

    if (Indication != IndicationRemoteConnect || Parameters->BtAddress != Extension->Address ||
        Parameters->Parameters.Connect.Request.PSM != PSM_AVCTP)
        return;
    Operation = HidAvrcpAllocateOperation(Extension, BRB_L2CA_OPEN_CHANNEL_RESPONSE);
    if (!Operation)
        return;
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Accept = Extension->Started && !Extension->Channel;
    if (Accept)
        Extension->Opening++;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    Open = &Operation->Brb.BrbL2caOpenChannel;
    HidAvrcpPrepareOpen(Extension, Open);
    Open->ChannelHandle = Parameters->ConnectionHandle;
    Open->Response = Accept ? CONNECT_RSP_RESULT_SUCCESS : CONNECT_RSP_RESULT_NO_RESOURCES;
    Open->ResponseStatus = CONNECT_RSP_STATUS_NO_INFORMATION;
    HidAvrcpSubmitAsync(Extension, Operation->Irp, &Operation->Brb,
                        Accept ? HidAvrcpOpenComplete : HidAvrcpOperationComplete, Operation);
}

static
VOID
HidAvrcpConnect(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension)
{
    PHIDAVRCP_OPERATION Operation;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (!Extension->Started || Extension->Channel || Extension->Opening)
    {
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        return;
    }
    Extension->Opening++;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    Operation = HidAvrcpAllocateOperation(Extension, BRB_L2CA_OPEN_CHANNEL);
    if (!Operation)
    {
        KeAcquireSpinLock(&Extension->Lock, &OldIrql);
        Extension->Opening--;
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        return;
    }
    HidAvrcpPrepareOpen(Extension, &Operation->Brb.BrbL2caOpenChannel);
    Operation->Brb.BrbL2caOpenChannel.Psm = PSM_AVCTP;
    HidAvrcpSubmitAsync(Extension, Operation->Irp, &Operation->Brb, HidAvrcpOpenComplete, Operation);
}

static
NTSTATUS
HidAvrcpQueryProfileInterface(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension)
{
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Extension->LowerDevice, NULL, 0, NULL, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    IoStack->Parameters.QueryInterface.InterfaceType = &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE;
    IoStack->Parameters.QueryInterface.Size = sizeof(Extension->Profile);
    IoStack->Parameters.QueryInterface.Version = BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI;
    IoStack->Parameters.QueryInterface.Interface = (PINTERFACE)&Extension->Profile;
    IoStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    Status = IoCallDriver(Extension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
VOID
HidAvrcpStop(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension)
{
    L2CAP_CHANNEL_HANDLE Channel;
    LARGE_INTEGER Timeout;
    KIRQL OldIrql;
    ULONG Index;
    BRB Brb;
    PIRP Irp;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Channel = Extension->Channel;
    Extension->Channel = NULL;
    Extension->Started = FALSE;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    for (;;)
    {
        KeAcquireSpinLock(&Extension->Lock, &OldIrql);
        Irp = HidAvrcpRemovePendingRead(Extension);
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        if (!Irp)
            break;
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    if (!Extension->HaveProfile)
        return;
    if (Channel)
    {
        Extension->Profile.BthInitializeBrb(&Brb, BRB_L2CA_CLOSE_CHANNEL);
        Brb.BrbL2caCloseChannel.BtAddress = Extension->Address;
        Brb.BrbL2caCloseChannel.ChannelHandle = Channel;
        HidAvrcpCallLower(Extension, IOCTL_INTERNAL_BTH_SUBMIT_BRB, &Brb, NULL);
    }
    HidAvrcpReportVolume(Extension, FALSE, 0);
    if (Extension->VolumeSetRegistration)
        ExUnregisterCallback(Extension->VolumeSetRegistration);
    Extension->VolumeSetRegistration = NULL;
    if (Extension->SdpRecord)
        HidAvrcpCallBuffered(Extension, IOCTL_BTH_SDP_REMOVE_RECORD, &Extension->SdpRecord, sizeof(Extension->SdpRecord),
                             NULL, 0);
    Extension->SdpRecord = 0;
    if (Extension->ControllerSdpRecord)
        HidAvrcpCallBuffered(Extension, IOCTL_BTH_SDP_REMOVE_RECORD, &Extension->ControllerSdpRecord,
                             sizeof(Extension->ControllerSdpRecord), NULL, 0);
    Extension->ControllerSdpRecord = 0;
    if (Extension->Server)
    {
        Extension->Profile.BthInitializeBrb(&Brb, BRB_L2CA_UNREGISTER_SERVER);
        Brb.BrbL2caUnregisterServer.BtAddress = Extension->Address;
        Brb.BrbL2caUnregisterServer.ServerHandle = Extension->Server;
        HidAvrcpCallLower(Extension, IOCTL_INTERNAL_BTH_SUBMIT_BRB, &Brb, NULL);
        Extension->Server = NULL;
    }
    if (Extension->PsmRegistered)
    {
        Extension->Profile.BthInitializeBrb(&Brb, BRB_UNREGISTER_PSM);
        Brb.BrbPsm.Psm = PSM_AVCTP;
        HidAvrcpCallLower(Extension, IOCTL_INTERNAL_BTH_SUBMIT_BRB, &Brb, NULL);
        Extension->PsmRegistered = FALSE;
    }
    Timeout.QuadPart = -(LONGLONG)HIDAVRCP_CLOSE_TIMEOUT_MS * 10000;
    KeClearEvent(&Extension->IoDrained);
    if (Extension->IoOutstanding)
    {
        for (Index = 0; Index < HIDAVRCP_READS; Index++)
        {
            if (Extension->Reads[Index].Irp)
                IoCancelIrp(Extension->Reads[Index].Irp);
        }
        if (KeWaitForSingleObject(&Extension->IoDrained, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
            DPRINT1("HIDAVRCP: %ld requests still outstanding\n", Extension->IoOutstanding);
    }
}

static
VOID
HidAvrcpFreeResources(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension)
{
    ULONG Index;

    for (Index = 0; Index < HIDAVRCP_READS && !Extension->IoOutstanding; Index++)
    {
        if (Extension->Reads[Index].Irp)
            IoFreeIrp(Extension->Reads[Index].Irp);
        Extension->Reads[Index].Irp = NULL;
    }
    if (Extension->VolumeSetCallback)
        ObDereferenceObject(Extension->VolumeSetCallback);
    Extension->VolumeSetCallback = NULL;
    if (Extension->VolumeStateCallback)
        ObDereferenceObject(Extension->VolumeStateCallback);
    Extension->VolumeStateCallback = NULL;
    if (Extension->HaveProfile && Extension->Profile.Interface.InterfaceDereference)
        Extension->Profile.Interface.InterfaceDereference(Extension->Profile.Interface.Context);
    Extension->HaveProfile = FALSE;
}

static
NTSTATUS
HidAvrcpStart(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension)
{
    struct _BRB_L2CA_REGISTER_SERVER *Server;
    BTH_DEVICE_INFO DeviceInfo;
    ULONG Index;
    NTSTATUS Status;
    BRB Brb;

    Status = HidAvrcpQueryProfileInterface(Extension);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension->HaveProfile = TRUE;
    RtlZeroMemory(&DeviceInfo, sizeof(DeviceInfo));
    Status = HidAvrcpCallLower(Extension, IOCTL_INTERNAL_BTHENUM_GET_DEVINFO, &DeviceInfo, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension->Address = DeviceInfo.address;
    for (Index = 0; Index < HIDAVRCP_READS; Index++)
    {
        Extension->Reads[Index].Extension = Extension;
        Extension->Reads[Index].Irp = IoAllocateIrp(Extension->LowerDevice->StackSize, FALSE);
        if (!Extension->Reads[Index].Irp)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    Extension->Profile.BthInitializeBrb(&Brb, BRB_REGISTER_PSM);
    Brb.BrbPsm.Psm = PSM_AVCTP;
    Status = HidAvrcpCallLower(Extension, IOCTL_INTERNAL_BTH_SUBMIT_BRB, &Brb, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension->PsmRegistered = TRUE;
    Extension->Profile.BthInitializeBrb(&Brb, BRB_L2CA_REGISTER_SERVER);
    Server = &Brb.BrbL2caRegisterServer;
    Server->BtAddress = Extension->Address;
    Server->IndicationCallback = HidAvrcpServerIndication;
    Server->IndicationCallbackContext = Extension;
    Server->ReferenceObject = Extension->DeviceObject;
    Status = HidAvrcpCallLower(Extension, IOCTL_INTERNAL_BTH_SUBMIT_BRB, &Brb, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension->Server = Server->ServerHandle;
    HidAvrcpCallBuffered(Extension, IOCTL_BTH_SDP_SUBMIT_RECORD, (PVOID)HidAvrcpTargetRecord,
                         sizeof(HidAvrcpTargetRecord), &Extension->SdpRecord, sizeof(Extension->SdpRecord));
    HidAvrcpCallBuffered(Extension, IOCTL_BTH_SDP_SUBMIT_RECORD, (PVOID)HidAvrcpControllerRecord,
                         sizeof(HidAvrcpControllerRecord), &Extension->ControllerSdpRecord,
                         sizeof(Extension->ControllerSdpRecord));
    if ((Extension->VolumeStateCallback || NT_SUCCESS(BthOpenVolumeCallback(BTH_VOLUME_STATE_CALLBACK,
                                                                           &Extension->VolumeStateCallback))) &&
        (Extension->VolumeSetCallback || NT_SUCCESS(BthOpenVolumeCallback(BTH_VOLUME_SET_CALLBACK,
                                                                         &Extension->VolumeSetCallback))))
    {
        Extension->VolumeSetRegistration = ExRegisterCallback(Extension->VolumeSetCallback, HidAvrcpVolumeSet, Extension);
    }

    Extension->Started = TRUE;
    DPRINT1("HIDAVRCP: %012I64x started\n", Extension->Address);
    HidAvrcpConnect(Extension);
    return STATUS_SUCCESS;
}

static
NTSTATUS
HidAvrcpCompleteWithBuffer(
    _Inout_ PIRP Irp,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Irp->UserBuffer || IoStack->Parameters.DeviceIoControl.OutputBufferLength < Length)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        Irp->IoStatus.Information = 0;
    }
    else
    {
        RtlCopyMemory(Irp->UserBuffer, Data, Length);
        Irp->IoStatus.Information = Length;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
HidAvrcpReadReport(
    _In_ PHIDAVRCP_DEVICE_EXTENSION Extension,
    _Inout_ PIRP Irp)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (!Extension->Started)
    {
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }
    if (Extension->ReportQueueCount)
    {
        UCHAR Report = Extension->ReportQueue[Extension->ReportQueueHead];

        Extension->ReportQueueHead = (Extension->ReportQueueHead + 1) % HIDAVRCP_QUEUED_REPORTS;
        Extension->ReportQueueCount--;
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        HidAvrcpCompleteRead(Irp, Report);
        return STATUS_SUCCESS;
    }
    IoMarkIrpPending(Irp);
    InsertTailList(&Extension->PendingReads, &Irp->Tail.Overlay.ListEntry);
    IoSetCancelRoutine(Irp, HidAvrcpCancelRead);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_PENDING;
    }
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    return STATUS_PENDING;
}

static
NTSTATUS
NTAPI
HidAvrcpInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDAVRCP_DEVICE_EXTENSION Extension = HidAvrcpGetExtension(DeviceObject);
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_HID_READ_REPORT:
            return HidAvrcpReadReport(Extension, Irp);

        case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        {
            HID_DESCRIPTOR HidDescriptor;

            RtlZeroMemory(&HidDescriptor, sizeof(HidDescriptor));
            HidDescriptor.bLength = sizeof(HidDescriptor);
            HidDescriptor.bDescriptorType = HID_HID_DESCRIPTOR_TYPE;
            HidDescriptor.bcdHID = 0x0111;
            HidDescriptor.bNumDescriptors = 1;
            HidDescriptor.DescriptorList[0].bReportType = HID_REPORT_DESCRIPTOR_TYPE;
            HidDescriptor.DescriptorList[0].wReportLength = sizeof(HidAvrcpReportDescriptor);
            return HidAvrcpCompleteWithBuffer(Irp, &HidDescriptor, sizeof(HidDescriptor));
        }

        case IOCTL_HID_GET_REPORT_DESCRIPTOR:
            return HidAvrcpCompleteWithBuffer(Irp, HidAvrcpReportDescriptor, sizeof(HidAvrcpReportDescriptor));

        case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
        {
            HID_DEVICE_ATTRIBUTES Attributes;

            RtlZeroMemory(&Attributes, sizeof(Attributes));
            Attributes.Size = sizeof(Attributes);
            return HidAvrcpCompleteWithBuffer(Irp, &Attributes, sizeof(Attributes));
        }

        case IOCTL_HID_ACTIVATE_DEVICE:
        case IOCTL_HID_DEACTIVATE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
HidAvrcpPnpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
HidAvrcpPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDAVRCP_DEVICE_EXTENSION Extension = HidAvrcpGetExtension(DeviceObject);
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    KEVENT Event;
    NTSTATUS Status;

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, HidAvrcpPnpCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status))
                Status = HidAvrcpStart(Extension);
            if (!NT_SUCCESS(Status))
            {
                HidAvrcpStop(Extension);
                HidAvrcpFreeResources(Extension);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            HidAvrcpStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            HidAvrcpStop(Extension);
            HidAvrcpFreeResources(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidAvrcpPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(HidAvrcpGetExtension(DeviceObject)->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidAvrcpPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(HidAvrcpGetExtension(DeviceObject)->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidAvrcpCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HidAvrcpAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PHIDAVRCP_DEVICE_EXTENSION Extension = HidExtension->MiniDeviceExtension;

    UNREFERENCED_PARAMETER(DriverObject);
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->DeviceObject = DeviceObject;
    Extension->LowerDevice = HidExtension->NextDeviceObject;
    KeInitializeSpinLock(&Extension->Lock);
    KeInitializeEvent(&Extension->IoDrained, NotificationEvent, FALSE);
    InitializeListHead(&Extension->PendingReads);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
HidAvrcpUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    HID_MINIDRIVER_REGISTRATION Registration;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = HidAvrcpCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = HidAvrcpCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = HidAvrcpInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = HidAvrcpPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = HidAvrcpPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = HidAvrcpPassThrough;
    DriverObject->DriverExtension->AddDevice = HidAvrcpAddDevice;
    DriverObject->DriverUnload = HidAvrcpUnload;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Revision = HID_REVISION;
    Registration.DriverObject = DriverObject;
    Registration.RegistryPath = RegistryPath;
    Registration.DeviceExtensionSize = sizeof(HIDAVRCP_DEVICE_EXTENSION);
    Registration.DevicesArePolled = FALSE;
    return HidRegisterMinidriver(&Registration);
}
