/*
 * PROJECT:     LiberNT Realtek Bluetooth UART transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Realtek three-wire UART Bluetooth transport bus driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rtkh5bt.h"

#define NDEBUG
#include <debug.h>

DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DRIVER_DEVICE_ADD RtkH5EvtDeviceAdd;
static EVT_WDF_DEVICE_PREPARE_HARDWARE RtkH5EvtPrepareHardware;
static EVT_WDF_DEVICE_D0_ENTRY RtkH5EvtD0Entry;
static EVT_WDF_DEVICE_D0_EXIT RtkH5EvtD0Exit;
static EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL RtkH5EvtPdoIoctl;

static
NTSTATUS
RtkH5Ioctl(
    _In_ WDFIOTARGET Target,
    _In_ ULONG Code,
    _In_reads_bytes_opt_(Length) PVOID Input,
    _In_ ULONG Length)
{
    WDF_MEMORY_DESCRIPTOR Descriptor;
    WDF_REQUEST_SEND_OPTIONS Options;

    WDF_REQUEST_SEND_OPTIONS_INIT(&Options, 0);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&Options, WDF_REL_TIMEOUT_IN_MS(1000));
    if (Input && Length)
    {
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&Descriptor, Input, Length);
        return WdfIoTargetSendIoctlSynchronously(Target, NULL, Code, &Descriptor, NULL, &Options, NULL);
    }
    return WdfIoTargetSendIoctlSynchronously(Target, NULL, Code, NULL, NULL, &Options, NULL);
}

static
NTSTATUS
RtkH5OpenConnection(
    _In_ PRTKH5_DEVICE Dev,
    _In_ LARGE_INTEGER Connection,
    _Out_ WDFIOTARGET *Target)
{
    DECLARE_UNICODE_STRING_SIZE(Path, RESOURCE_HUB_PATH_SIZE);
    WDF_IO_TARGET_OPEN_PARAMS Params;
    NTSTATUS Status;

    *Target = NULL;
    Status = RESOURCE_HUB_CREATE_PATH_FROM_ID(&Path, Connection.LowPart, Connection.HighPart);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WdfIoTargetCreate(Dev->Device, WDF_NO_OBJECT_ATTRIBUTES, Target);
    if (!NT_SUCCESS(Status))
        return Status;
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&Params, &Path, GENERIC_READ | GENERIC_WRITE);
    Status = WdfIoTargetOpen(*Target, &Params);
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(*Target);
        *Target = NULL;
    }
    return Status;
}

static
NTSTATUS
RtkH5SetEnable(
    _In_ PRTKH5_DEVICE Dev,
    _In_ BOOLEAN On)
{
    UCHAR Value = On ? 1 : 0;

    if (!Dev->Gpio)
        return STATUS_SUCCESS;
    return RtkH5Ioctl(Dev->Gpio, IOCTL_GPIO_WRITE_PINS, &Value, sizeof(Value));
}

static
NTSTATUS
RtkH5ConfigureUart(
    _In_ PRTKH5_DEVICE Dev)
{
    SERIAL_BAUD_RATE Baud = { RTKH5_UART_BAUD };
    SERIAL_LINE_CONTROL Line = { STOP_BIT_1, EVEN_PARITY, 8 };
    SERIAL_HANDFLOW Flow;
    SERIAL_TIMEOUTS Timeouts;
    ULONG Purge = SERIAL_PURGE_RXCLEAR | SERIAL_PURGE_TXCLEAR;
    NTSTATUS Status;

    RtlZeroMemory(&Flow, sizeof(Flow));
    Flow.FlowReplace = SERIAL_RTS_CONTROL;
    RtlZeroMemory(&Timeouts, sizeof(Timeouts));
    Timeouts.ReadIntervalTimeout = MAXULONG;
    Timeouts.ReadTotalTimeoutMultiplier = MAXULONG;
    Timeouts.ReadTotalTimeoutConstant = RTKH5_READ_TIMEOUT_MS;
    Timeouts.WriteTotalTimeoutConstant = 1000;

    Status = RtkH5Ioctl(Dev->Uart, IOCTL_SERIAL_SET_BAUD_RATE, &Baud, sizeof(Baud));
    if (NT_SUCCESS(Status))
        Status = RtkH5Ioctl(Dev->Uart, IOCTL_SERIAL_SET_LINE_CONTROL, &Line, sizeof(Line));
    if (NT_SUCCESS(Status))
        Status = RtkH5Ioctl(Dev->Uart, IOCTL_SERIAL_SET_HANDFLOW, &Flow, sizeof(Flow));
    if (NT_SUCCESS(Status))
        Status = RtkH5Ioctl(Dev->Uart, IOCTL_SERIAL_SET_TIMEOUTS, &Timeouts, sizeof(Timeouts));
    if (NT_SUCCESS(Status))
        Status = RtkH5Ioctl(Dev->Uart, IOCTL_SERIAL_PURGE, &Purge, sizeof(Purge));
    return Status;
}

static
VOID
RtkH5Sleep(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
VOID
NTAPI
RtkH5Thread(
    _In_ PVOID Context)
{
    PRTKH5_DEVICE Dev = Context;
    NTSTATUS Status;

    RtkH5SetEnable(Dev, FALSE);
    RtkH5Sleep(50);
    RtkH5SetEnable(Dev, TRUE);
    RtkH5Sleep(300);

    Status = RtkH5ConfigureUart(Dev);
    if (NT_SUCCESS(Status))
    {
        RtkH5LinkReset(Dev);
        Status = RtkH5WaitLink(Dev);
    }
    if (NT_SUCCESS(Status))
        Status = RtkH5InitializeController(Dev);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTKH5BT: controller bring-up failed 0x%08lx (link state %u)\n", Status, Dev->LinkState);
        PsTerminateSystemThread(Status);
    }

    RtkH5FlushPackets(Dev);
    Dev->Ready = TRUE;
    if (!Dev->Child && !NT_SUCCESS(RtkH5CreatePdo(Dev)))
        DPRINT1("RTKH5BT: radio PDO not created\n");

    while (!Dev->StopThread)
        RtkH5Pump(Dev);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NTSTATUS
RtkH5EvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PRTKH5_DEVICE Dev = RtkH5GetDevice(Device);
    ULONG Index;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    for (Index = 0; Index < WdfCmResourceListGetCount(ResourcesTranslated); Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);

        if (!Resource || Resource->Type != CmResourceTypeConnection)
            continue;
        if (Resource->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
            Resource->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_UART && !Dev->HasUart)
        {
            Dev->UartConnection.LowPart = Resource->u.Connection.IdLowPart;
            Dev->UartConnection.HighPart = Resource->u.Connection.IdHighPart;
            Dev->HasUart = TRUE;
        }
        else if (Resource->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_GPIO &&
                 Resource->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_GPIO_IO && !Dev->HasGpio)
        {
            Dev->GpioConnection.LowPart = Resource->u.Connection.IdLowPart;
            Dev->GpioConnection.HighPart = Resource->u.Connection.IdHighPart;
            Dev->HasGpio = TRUE;
        }
    }
    return Dev->HasUart ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static
NTSTATUS
RtkH5EvtD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PRTKH5_DEVICE Dev = RtkH5GetDevice(Device);
    HANDLE Thread;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(PreviousState);
    Status = RtkH5OpenConnection(Dev, Dev->UartConnection, &Dev->Uart);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTKH5BT: UART connection unavailable 0x%08lx\n", Status);
        return Status;
    }
    if (Dev->HasGpio && !NT_SUCCESS(RtkH5OpenConnection(Dev, Dev->GpioConnection, &Dev->Gpio)))
        DPRINT1("RTKH5BT: enable GPIO unavailable\n");

    Dev->StopThread = FALSE;
    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, RtkH5Thread, Dev);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ObReferenceObjectByHandle(Thread, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Dev->Thread, NULL);
    ZwClose(Thread);
    return Status;
}

static
NTSTATUS
RtkH5EvtD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PRTKH5_DEVICE Dev = RtkH5GetDevice(Device);

    UNREFERENCED_PARAMETER(TargetState);
    Dev->Ready = FALSE;
    Dev->StopThread = TRUE;
    if (Dev->Thread)
    {
        KeWaitForSingleObject(Dev->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Dev->Thread);
        Dev->Thread = NULL;
    }
    RtkH5FlushPackets(Dev);
    RtkH5SetEnable(Dev, FALSE);
    if (Dev->Gpio)
    {
        WdfObjectDelete(Dev->Gpio);
        Dev->Gpio = NULL;
    }
    if (Dev->Uart)
    {
        WdfObjectDelete(Dev->Uart);
        Dev->Uart = NULL;
    }
    return STATUS_SUCCESS;
}

static
WDFQUEUE
RtkH5ReadQueueForType(
    _In_ PRTKH5_DEVICE Dev,
    _In_ UCHAR Type)
{
    return Type == RTKH5_PKT_EVENT ? Dev->EventQueue : Dev->AclQueue;
}

VOID
RtkH5DeliverPackets(
    _In_ PRTKH5_DEVICE Dev)
{
    for (;;)
    {
        PRTKH5_PACKET Packet;
        WDFREQUEST Request;
        WDFMEMORY Memory;
        PBTHX_HCI_READ_WRITE_CONTEXT Context;
        size_t Size;
        KIRQL OldIrql;

        KeAcquireSpinLock(&Dev->Lock, &OldIrql);
        if (IsListEmpty(&Dev->RxPackets))
        {
            KeReleaseSpinLock(&Dev->Lock, OldIrql);
            return;
        }
        Packet = CONTAINING_RECORD(Dev->RxPackets.Flink, RTKH5_PACKET, Link);
        KeReleaseSpinLock(&Dev->Lock, OldIrql);

        if (Packet->Type == RTKH5_PKT_SCO)
        {
            ExFreePoolWithTag(RtkH5TakePacket(Dev), RTKH5_TAG);
            continue;
        }
        if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(RtkH5ReadQueueForType(Dev, Packet->Type), &Request)))
            return;

        Packet = RtkH5TakePacket(Dev);
        if (NT_SUCCESS(WdfRequestRetrieveOutputMemory(Request, &Memory)))
        {
            Context = WdfMemoryGetBuffer(Memory, &Size);
            if (Size >= FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) + Packet->Length)
            {
                Context->DataLen = Packet->Length;
                Context->Type = Packet->Type == RTKH5_PKT_EVENT ? HciPacketEvent : HciPacketAclData;
                RtlCopyMemory(Context->Data, Packet->Data, Packet->Length);
                WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                                  FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) + Packet->Length);
            }
            else
            {
                WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
            }
        }
        else
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        }
        ExFreePoolWithTag(Packet, RTKH5_TAG);
    }
}

VOID
RtkH5FlushPackets(
    _In_ PRTKH5_DEVICE Dev)
{
    PRTKH5_PACKET Packet;

    while ((Packet = RtkH5TakePacket(Dev)) != NULL)
        ExFreePoolWithTag(Packet, RTKH5_TAG);
}

static
VOID
RtkH5EvtPdoIoctl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    PRTKH5_DEVICE Dev = RtkH5GetPdo(WdfIoQueueGetDevice(Queue))->Parent;
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
            ((PBTHX_CAPABILITIES)OutBuffer)->MaxAclTransferInSize = RTKH5_MAX_ACL_IN;
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
            Status = RtkH5SendReliable(Dev,
                                       Context->Type == HciPacketCommand ? RTKH5_PKT_COMMAND : RTKH5_PKT_ACL,
                                       Context->Data, Context->DataLen);
            if (NT_SUCCESS(Status) && NT_SUCCESS(WdfRequestRetrieveOutputMemory(Request, &Output)))
            {
                OutBuffer = WdfMemoryGetBuffer(Output, &OutSize);
                if (OutSize >= sizeof(ULONG))
                {
                    *(PULONG)OutBuffer = Context->DataLen;
                    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(ULONG));
                    return;
                }
            }
            break;
        }

        case IOCTL_BTHX_READ_HCI:
        {
            UCHAR Type;

            Status = WdfRequestRetrieveInputMemory(Request, &Input);
            if (!NT_SUCCESS(Status))
                break;
            InBuffer = WdfMemoryGetBuffer(Input, &InSize);
            if (InSize < sizeof(UCHAR))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Type = *(PUCHAR)InBuffer;
            if (Type != HciPacketEvent && Type != HciPacketAclData)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Status = WdfRequestForwardToIoQueue(Request,
                                                RtkH5ReadQueueForType(Dev, Type == HciPacketEvent ? RTKH5_PKT_EVENT : RTKH5_PKT_ACL));
            if (NT_SUCCESS(Status))
                return;
            break;
        }

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    WdfRequestComplete(Request, Status);
}

NTSTATUS
RtkH5CreatePdo(
    _In_ PRTKH5_DEVICE Dev)
{
    DECLARE_CONST_UNICODE_STRING(HardwareId, RTKH5_PDO_HARDWARE_ID);
    DECLARE_CONST_UNICODE_STRING(CompatibleId, RTKH5_PDO_COMPATIBLE_ID);
    DECLARE_CONST_UNICODE_STRING(InstanceId, L"00");
    DECLARE_CONST_UNICODE_STRING(Description, L"Realtek RTL8852BS Bluetooth Radio");
    WDF_IO_QUEUE_CONFIG QueueConfig;
    WDF_OBJECT_ATTRIBUTES Attributes;
    PWDFDEVICE_INIT Init;
    WDFDEVICE Child;
    WDFQUEUE Queue;
    NTSTATUS Status;

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

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, RTKH5_PDO);
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    Status = WdfDeviceCreate(&Init, &Attributes, &Child);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    RtkH5GetPdo(Child)->Parent = Dev;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
    QueueConfig.EvtIoDeviceControl = RtkH5EvtPdoIoctl;
    QueueConfig.EvtIoInternalDeviceControl = RtkH5EvtPdoIoctl;
    Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Queue);
    if (NT_SUCCESS(Status))
    {
        WDF_IO_QUEUE_CONFIG_INIT(&QueueConfig, WdfIoQueueDispatchManual);
        Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Dev->EventQueue);
    }
    if (NT_SUCCESS(Status))
        Status = WdfIoQueueCreate(Child, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Dev->AclQueue);
    if (NT_SUCCESS(Status))
        Status = WdfFdoAddStaticChild(Dev->Device, Child);
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Child);
        return Status;
    }
    Dev->Child = Child;
    IoInvalidateDeviceRelations(WdfDeviceWdmGetPhysicalDevice(Dev->Device), BusRelations);
    DPRINT1("RTKH5BT: radio ready\n");
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtkH5EvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS Callbacks;
    WDF_OBJECT_ATTRIBUTES Attributes;
    PRTKH5_DEVICE Dev;
    WDFDEVICE Device;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&Callbacks);
    Callbacks.EvtDevicePrepareHardware = RtkH5EvtPrepareHardware;
    Callbacks.EvtDeviceD0Entry = RtkH5EvtD0Entry;
    Callbacks.EvtDeviceD0Exit = RtkH5EvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &Callbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, RTKH5_DEVICE);
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Dev = RtkH5GetDevice(Device);
    Dev->Device = Device;
    KeInitializeSpinLock(&Dev->Lock);
    KeInitializeMutex(&Dev->WriteLock, 0);
    InitializeListHead(&Dev->TxQueue);
    InitializeListHead(&Dev->Unacked);
    InitializeListHead(&Dev->RxPackets);
    KeInitializeEvent(&Dev->RxEvent, SynchronizationEvent, FALSE);
    Dev->PeerWindow = 1;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;

    WDF_DRIVER_CONFIG_INIT(&Config, RtkH5EvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, WDF_NO_HANDLE);
}
