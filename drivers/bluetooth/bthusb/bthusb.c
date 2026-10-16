/*
 * PROJECT:     LiberNT Bluetooth USB transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth HCI USB transport bus driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bthusb.h"

#define NDEBUG
#include <debug.h>

DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DRIVER_DEVICE_ADD BthUsbEvtDeviceAdd;
static EVT_WDF_DEVICE_PREPARE_HARDWARE BthUsbEvtPrepareHardware;
static EVT_WDF_DEVICE_RELEASE_HARDWARE BthUsbEvtReleaseHardware;
static EVT_WDF_DEVICE_D0_ENTRY BthUsbEvtD0Entry;
static EVT_WDF_DEVICE_D0_EXIT BthUsbEvtD0Exit;
static EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL BthUsbEvtPdoIoctl;
static RTKBT_HCI_COMMAND BthUsbHciCommand;

static
ULONGLONG
BthUsbNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
NTSTATUS
NTAPI
BthUsbHciCommand(
    _In_ PVOID Context,
    _In_ USHORT Opcode,
    _In_reads_bytes_opt_(ParameterLength) const UCHAR *Parameters,
    _In_ UCHAR ParameterLength,
    _Out_writes_bytes_to_opt_(ResultSize, *ResultLength) PUCHAR Result,
    _In_ ULONG ResultSize,
    _Out_opt_ PULONG ResultLength)
{
    PBTHUSB_DEVICE Dev = Context;
    UCHAR Command[3 + 255];
    ULONGLONG Deadline;
    NTSTATUS Status;

    if (ResultLength)
        *ResultLength = 0;
    Command[0] = (UCHAR)Opcode;
    Command[1] = (UCHAR)(Opcode >> 8);
    Command[2] = ParameterLength;
    if (ParameterLength)
        RtlCopyMemory(Command + 3, Parameters, ParameterLength);

    KeClearEvent(&Dev->EventArrived);
    Status = BthUsbWrite(Dev, HciPacketCommand, Command, 3 + ParameterLength, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Deadline = BthUsbNowMs() + BTHUSB_COMMAND_TIMEOUT_MS;
    for (;;)
    {
        PBTHUSB_PACKET Packet;
        LARGE_INTEGER Timeout;
        ULONGLONG Now;

        while ((Packet = BthUsbTakePacket(Dev, BthUsbRxEvent)) != NULL)
        {
            const UCHAR *Event = Packet->Data;

            if (Packet->Length >= 6 && Event[0] == HCI_EVENT_COMMAND_COMPLETE &&
                (Event[3] | (Event[4] << 8)) == Opcode)
            {
                ULONG Length = min(ResultSize, Packet->Length - 5);

                if (Result && Length)
                    RtlCopyMemory(Result, Event + 5, Length);
                if (ResultLength)
                    *ResultLength = Length;
                Status = Event[5] == 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                ExFreePoolWithTag(Packet, BTHUSB_TAG);
                return Status;
            }
            if (Packet->Length >= 6 && Event[0] == HCI_EVENT_COMMAND_STATUS &&
                (Event[4] | (Event[5] << 8)) == Opcode && Event[2] != 0)
            {
                ExFreePoolWithTag(Packet, BTHUSB_TAG);
                return STATUS_UNSUCCESSFUL;
            }
            ExFreePoolWithTag(Packet, BTHUSB_TAG);
        }

        Now = BthUsbNowMs();
        if (Now >= Deadline || Dev->Stopping)
            return Dev->Stopping ? STATUS_CANCELLED : STATUS_IO_TIMEOUT;
        Timeout.QuadPart = -(LONGLONG)(Deadline - Now) * 10000;
        KeWaitForSingleObject(&Dev->EventArrived, Executive, KernelMode, FALSE, &Timeout);
    }
}

static
NTSTATUS
BthUsbCreatePdo(
    _In_ PBTHUSB_DEVICE Dev)
{
    DECLARE_CONST_UNICODE_STRING(CompatibleId, BTHUSB_PDO_COMPATIBLE_ID);
    DECLARE_CONST_UNICODE_STRING(InstanceId, L"00");
    DECLARE_CONST_UNICODE_STRING(Description, L"Bluetooth Radio");
    DECLARE_UNICODE_STRING_SIZE(HardwareId, 64);
    WDF_IO_QUEUE_CONFIG QueueConfig;
    WDF_OBJECT_ATTRIBUTES Attributes;
    PWDFDEVICE_INIT Init;
    WDFDEVICE Child;
    WDFQUEUE Queue;
    NTSTATUS Status;

    Status = RtlUnicodeStringPrintf(&HardwareId, L"BTHUSB\\VID_%04X&PID_%04X", Dev->VendorId, Dev->ProductId);
    if (!NT_SUCCESS(Status))
        return Status;
    Init = WdfPdoInitAllocate(Dev->Device);
    if (!Init)
        return STATUS_INSUFFICIENT_RESOURCES;
    WdfDeviceInitSetDeviceType(Init, FILE_DEVICE_BLUETOOTH);
    Status = WdfPdoInitAssignDeviceID(Init, &HardwareId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddHardwareID(Init, &HardwareId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddCompatibleID(Init, &CompatibleId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAssignInstanceID(Init, &InstanceId);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddDeviceText(Init, &Description, &Description, 0x409);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    WdfPdoInitSetDefaultLocale(Init, 0x409);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BTHUSB_PDO);
    Status = WdfDeviceCreate(&Init, &Attributes, &Child);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    BthUsbGetPdo(Child)->Parent = Dev;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
    QueueConfig.EvtIoDeviceControl = BthUsbEvtPdoIoctl;
    QueueConfig.EvtIoInternalDeviceControl = BthUsbEvtPdoIoctl;
    Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Queue);
    if (NT_SUCCESS(Status))
    {
        WDF_IO_QUEUE_CONFIG_INIT(&QueueConfig, WdfIoQueueDispatchManual);
        Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Dev->Rx[BthUsbRxEvent].Queue);
    }
    if (NT_SUCCESS(Status))
        Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Dev->Rx[BthUsbRxAcl].Queue);
    if (NT_SUCCESS(Status))
        Status = WdfFdoAddStaticChild(Dev->Device, Child);
    if (!NT_SUCCESS(Status))
    {
        Dev->Rx[BthUsbRxEvent].Queue = Dev->Rx[BthUsbRxAcl].Queue = NULL;
        WdfObjectDelete(Child);
        return Status;
    }
    Dev->Child = Child;
    IoInvalidateDeviceRelations(WdfDeviceWdmGetPhysicalDevice(Dev->Device), BusRelations);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
BthUsbThread(
    _In_ PVOID Context)
{
    PBTHUSB_DEVICE Dev = Context;
    NTSTATUS Status;

    Status = RtkBtLoadPatch(BthUsbHciCommand, Dev, RtkBtBusUsb, &Dev->Version);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BTHUSB: %04x:%04x controller bring-up failed 0x%08lx\n", Dev->VendorId, Dev->ProductId, Status);
        PsTerminateSystemThread(Status);
    }
    DPRINT1("BTHUSB: %04x:%04x HCI version %u revision 0x%04x, LMP version %u subversion 0x%04x, manufacturer %u\n",
            Dev->VendorId, Dev->ProductId, Dev->Version.HciVersion, Dev->Version.HciRevision,
            Dev->Version.LmpVersion, Dev->Version.LmpSubversion, Dev->Version.Manufacturer);

    BthUsbFlushPackets(Dev);
    Dev->Ready = TRUE;
    if (!Dev->Child && !NT_SUCCESS(BthUsbCreatePdo(Dev)))
        DPRINT1("BTHUSB: radio PDO not created\n");
    BthUsbDeliverPackets(Dev, BthUsbRxEvent);
    BthUsbDeliverPackets(Dev, BthUsbRxAcl);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NTSTATUS
NTAPI
BthUsbEvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PBTHUSB_DEVICE Dev = BthUsbGetDevice(Device);

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    Dev->LowerDeviceObject = WdfDeviceWdmGetAttachedDevice(Device);
    return BthUsbConfigure(Dev);
}

static
NTSTATUS
NTAPI
BthUsbEvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    BthUsbUnconfigure(BthUsbGetDevice(Device));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BthUsbEvtD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PBTHUSB_DEVICE Dev = BthUsbGetDevice(Device);
    HANDLE Thread;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(PreviousState);
    Status = BthUsbStartReads(Dev);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, BthUsbThread, Dev);
    if (!NT_SUCCESS(Status))
    {
        BthUsbStopIo(Dev);
        return Status;
    }
    Status = ObReferenceObjectByHandle(Thread, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Dev->Thread, NULL);
    ZwClose(Thread);
    return Status;
}

static
NTSTATUS
NTAPI
BthUsbEvtD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PBTHUSB_DEVICE Dev = BthUsbGetDevice(Device);

    UNREFERENCED_PARAMETER(TargetState);
    Dev->Ready = FALSE;
    BthUsbStopIo(Dev);
    KeSetEvent(&Dev->EventArrived, IO_NO_INCREMENT, FALSE);
    if (Dev->Thread)
    {
        KeWaitForSingleObject(Dev->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Dev->Thread);
        Dev->Thread = NULL;
    }
    BthUsbFlushPackets(Dev);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
BthUsbEvtPdoIoctl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    PBTHUSB_DEVICE Dev = BthUsbGetPdo(WdfIoQueueGetDevice(Queue))->Parent;
    WDFMEMORY Input, Output;
    PVOID InBuffer, OutBuffer;
    size_t InSize, OutSize;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode)
    {
        case IOCTL_BTHX_GET_VERSION:
            Status = WdfRequestRetrieveOutputMemory(Request, &Output);
            if (!NT_SUCCESS(Status))
                break;
            OutBuffer = WdfMemoryGetBuffer(Output, &OutSize);
            if (OutSize < sizeof(BTHX_VERSION))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            ((PBTHX_VERSION)OutBuffer)->Version = BTHX_DDI_VERSION_1;
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(BTHX_VERSION));
            return;

        case IOCTL_BTHX_SET_VERSION:
            Status = WdfRequestRetrieveInputMemory(Request, &Input);
            if (!NT_SUCCESS(Status))
                break;
            InBuffer = WdfMemoryGetBuffer(Input, &InSize);
            Status = InSize >= sizeof(BTHX_VERSION) && ((PBTHX_VERSION)InBuffer)->Version == BTHX_DDI_VERSION_1 ?
                     STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
            break;

        case IOCTL_BTHX_QUERY_CAPABILITIES:
            Status = WdfRequestRetrieveOutputMemory(Request, &Output);
            if (!NT_SUCCESS(Status))
                break;
            OutBuffer = WdfMemoryGetBuffer(Output, &OutSize);
            if (OutSize < sizeof(BTHX_CAPABILITIES))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            RtlZeroMemory(OutBuffer, sizeof(BTHX_CAPABILITIES));
            ((PBTHX_CAPABILITIES)OutBuffer)->MaxAclTransferInSize = BTHUSB_MAX_ACL_IN;
            ((PBTHX_CAPABILITIES)OutBuffer)->ScoSupport = ScoSupportNone;
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(BTHX_CAPABILITIES));
            return;

        case IOCTL_BTHX_WRITE_HCI:
        {
            PBTHX_HCI_READ_WRITE_CONTEXT Context;

            Status = WdfRequestRetrieveInputMemory(Request, &Input);
            if (!NT_SUCCESS(Status))
                break;
            Context = WdfMemoryGetBuffer(Input, &InSize);
            if (InSize < FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) ||
                Context->DataLen > InSize - FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) ||
                (Context->Type != HciPacketCommand && Context->Type != HciPacketAclData))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (!Dev->Ready)
            {
                Status = STATUS_DEVICE_NOT_READY;
                break;
            }
            Status = BthUsbWrite(Dev, (BTHX_HCI_PACKET_TYPE)Context->Type, Context->Data, Context->DataLen, Request);
            if (Status == STATUS_PENDING)
                return;
            break;
        }

        case IOCTL_BTHX_READ_HCI:
        {
            BTHUSB_RX_KIND Kind;

            Status = WdfRequestRetrieveInputMemory(Request, &Input);
            if (!NT_SUCCESS(Status))
                break;
            InBuffer = WdfMemoryGetBuffer(Input, &InSize);
            if (InSize < sizeof(UCHAR))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (*(PUCHAR)InBuffer == HciPacketEvent)
                Kind = BthUsbRxEvent;
            else if (*(PUCHAR)InBuffer == HciPacketAclData)
                Kind = BthUsbRxAcl;
            else
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Status = WdfRequestForwardToIoQueue(Request, Dev->Rx[Kind].Queue);
            if (NT_SUCCESS(Status))
            {
                BthUsbDeliverPackets(Dev, Kind);
                return;
            }
            break;
        }

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    WdfRequestComplete(Request, Status);
}

static
NTSTATUS
NTAPI
BthUsbEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS Callbacks;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_WORKITEM_CONFIG WorkItemConfig;
    PBTHUSB_DEVICE Dev;
    WDFDEVICE Device;
    NTSTATUS Status;
    ULONG Kind;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&Callbacks);
    Callbacks.EvtDevicePrepareHardware = BthUsbEvtPrepareHardware;
    Callbacks.EvtDeviceReleaseHardware = BthUsbEvtReleaseHardware;
    Callbacks.EvtDeviceD0Entry = BthUsbEvtD0Entry;
    Callbacks.EvtDeviceD0Exit = BthUsbEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &Callbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BTHUSB_DEVICE);
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Dev = BthUsbGetDevice(Device);
    Dev->Device = Device;
    KeInitializeSpinLock(&Dev->Lock);
    KeInitializeEvent(&Dev->IoDrained, NotificationEvent, FALSE);
    KeInitializeEvent(&Dev->EventArrived, SynchronizationEvent, FALSE);
    for (Kind = 0; Kind < BthUsbRxKinds; Kind++)
        InitializeListHead(&Dev->Rx[Kind].Packets);

    WDF_WORKITEM_CONFIG_INIT(&WorkItemConfig, BthUsbRecoverReads);
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Device;
    return WdfWorkItemCreate(&WorkItemConfig, &Attributes, &Dev->RecoverItem);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;

    WDF_DRIVER_CONFIG_INIT(&Config, BthUsbEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, WDF_NO_HANDLE);
}
