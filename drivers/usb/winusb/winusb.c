/*
 * PROJECT:     LiberNT WinUSB driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, file handles, power and the private WinUSB requests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winusb.h"

#define NDEBUG
#include <debug.h>

DRIVER_INITIALIZE DriverEntry;
static DRIVER_UNLOAD WinUsbUnload;
_Dispatch_type_(IRP_MJ_CREATE) static DRIVER_DISPATCH WinUsbDispatchCreate;
_Dispatch_type_(IRP_MJ_CLOSE) static DRIVER_DISPATCH WinUsbDispatchClose;
_Dispatch_type_(IRP_MJ_CLEANUP) static DRIVER_DISPATCH WinUsbDispatchCleanup;
_Dispatch_type_(IRP_MJ_POWER) static DRIVER_DISPATCH WinUsbDispatchPower;
_Dispatch_type_(IRP_MJ_SYSTEM_CONTROL) static DRIVER_DISPATCH WinUsbDispatchSystemControl;

static
NTSTATUS
WinUsbCompleteIrp(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
WinUsbDispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PWINUSB_DEVICE Device = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Status = IoAcquireRemoveLock(&Device->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return WinUsbCompleteIrp(Irp, Status, 0);

    if (!Device->Started || Device->Gone)
        Status = STATUS_DEVICE_NOT_CONNECTED;
    else if (InterlockedCompareExchange(&Device->Opened, 1, 0) != 0)
        Status = STATUS_ACCESS_DENIED;
    else
        Status = STATUS_SUCCESS;

    IoReleaseRemoveLock(&Device->RemoveLock, Irp);
    return WinUsbCompleteIrp(Irp, Status, 0);
}

static
NTSTATUS
NTAPI
WinUsbDispatchCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PWINUSB_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    WinUsbCancelFileRequests(Device, IoStack->FileObject);
    return WinUsbCompleteIrp(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NTAPI
WinUsbDispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PWINUSB_DEVICE Device = DeviceObject->DeviceExtension;
    LONG Previous;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Previous = InterlockedExchange(&Device->Opened, 0);
    ASSERT(Previous == 1);
    return WinUsbCompleteIrp(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NTAPI
WinUsbDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PWINUSB_DEVICE Device = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);
    Status = IoAcquireRemoveLock(&Device->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return WinUsbCompleteIrp(Irp, Status, 0);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = PoCallDriver(Device->Lower, Irp);
    IoReleaseRemoveLock(&Device->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
WinUsbDispatchSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PWINUSB_DEVICE Device = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&Device->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return WinUsbCompleteIrp(Irp, Status, 0);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(Device->Lower, Irp);
    IoReleaseRemoveLock(&Device->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
WinUsbInitialize(
    _In_ PWINUSB_DEVICE Device,
    _In_ PIRP Irp,
    _Out_ PULONG_PTR Information)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PLNT_WINUSB_INFORMATION Info = Irp->AssociatedIrp.SystemBuffer;
    ULONG Size, Index;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Device->Configuration != NULL);

    Size = FIELD_OFFSET(LNT_WINUSB_INFORMATION, Configuration) + Device->Configuration->wTotalLength;
    if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < Size)
        return STATUS_BUFFER_TOO_SMALL;

    WinUsbResetPolicies(Device);

    RtlZeroMemory(Info, Size);
    Info->Version = LNT_WINUSB_VERSION;
    Info->Size = Size;
    Info->Speed = Device->Speed;
    Info->InterfaceCount = Device->InterfaceCount;
    for (Index = 0; Index < Device->InterfaceCount; Index++)
        Info->InterfaceNumbers[Index] = Device->Interfaces[Index].Number;
    RtlCopyMemory(Info->DeviceDescriptor, &Device->DeviceDescriptor, sizeof(Info->DeviceDescriptor));
    Info->ConfigurationLength = Device->Configuration->wTotalLength;
    RtlCopyMemory(Info->Configuration, Device->Configuration, Device->Configuration->wTotalLength);
    *Information = Size;
    return STATUS_SUCCESS;
}

static
NTSTATUS
WinUsbGetPipePolicy(
    _In_ PWINUSB_DEVICE Device,
    _Inout_ PLNT_WINUSB_POLICY_REQUEST Request)
{
    PWINUSB_PIPE Pipe;
    KIRQL OldIrql;
    BOOLEAN Valid;

    Request->ValueLength = sizeof(UCHAR);
    if (Request->PipeId == 0)
    {
        if (Request->PolicyType == PIPE_TRANSFER_TIMEOUT)
            Request->Value = Device->ControlTimeout;
        else if (Request->PolicyType == MAXIMUM_TRANSFER_SIZE)
            Request->Value = 0;
        else
            return STATUS_INVALID_PARAMETER;
        Request->ValueLength = sizeof(ULONG);
        return STATUS_SUCCESS;
    }

    Pipe = WinUsbFindPipe(Device, Request->InterfaceIndex, Request->PipeId);
    if (!Pipe)
        return STATUS_INVALID_PARAMETER;

    switch (Request->PolicyType)
    {
        case SHORT_PACKET_TERMINATE:
            Valid = !USB_ENDPOINT_DIRECTION_IN(Pipe->EndpointAddress);
            break;
        case IGNORE_SHORT_PACKETS:
        case ALLOW_PARTIAL_READS:
        case AUTO_FLUSH:
            Valid = USB_ENDPOINT_DIRECTION_IN(Pipe->EndpointAddress);
            break;
        case AUTO_CLEAR_STALL:
        case PIPE_TRANSFER_TIMEOUT:
        case RAW_IO:
        case MAXIMUM_TRANSFER_SIZE:
        case RESET_PIPE_ON_RESUME:
            Valid = TRUE;
            break;
        default:
            Valid = FALSE;
            break;
    }
    if (!Valid)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    switch (Request->PolicyType)
    {
        case SHORT_PACKET_TERMINATE: Request->Value = Pipe->ShortPacketTerminate; break;
        case AUTO_CLEAR_STALL: Request->Value = Pipe->AutoClearStall; break;
        case IGNORE_SHORT_PACKETS: Request->Value = Pipe->IgnoreShortPackets; break;
        case ALLOW_PARTIAL_READS: Request->Value = Pipe->AllowPartialReads; break;
        case AUTO_FLUSH: Request->Value = Pipe->AutoFlush; break;
        case RAW_IO: Request->Value = Pipe->RawIo; break;
        case RESET_PIPE_ON_RESUME: Request->Value = Pipe->ResetPipeOnResume; break;
        case PIPE_TRANSFER_TIMEOUT:
            Request->Value = Pipe->Timeout;
            Request->ValueLength = sizeof(ULONG);
            break;
        case MAXIMUM_TRANSFER_SIZE:
            Request->Value = LNT_WINUSB_MAXIMUM_TRANSFER_SIZE;
            Request->ValueLength = sizeof(ULONG);
            break;
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
WinUsbSetPipePolicy(
    _In_ PWINUSB_DEVICE Device,
    _In_ PLNT_WINUSB_POLICY_REQUEST Request)
{
    PWINUSB_PIPE Pipe;
    KIRQL OldIrql;
    BOOLEAN Flag = (Request->Value & 0xFF) != 0;

    if (Request->PolicyType == PIPE_TRANSFER_TIMEOUT)
    {
        if (Request->ValueLength < sizeof(ULONG))
            return STATUS_INVALID_PARAMETER;
    }
    else if (Request->ValueLength < sizeof(UCHAR))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->PipeId == 0)
    {
        if (Request->PolicyType != PIPE_TRANSFER_TIMEOUT)
            return STATUS_INVALID_PARAMETER;
        Device->ControlTimeout = Request->Value;
        return STATUS_SUCCESS;
    }

    Pipe = WinUsbFindPipe(Device, Request->InterfaceIndex, Request->PipeId);
    if (!Pipe)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    switch (Request->PolicyType)
    {
        case SHORT_PACKET_TERMINATE: Pipe->ShortPacketTerminate = Flag; break;
        case AUTO_CLEAR_STALL: Pipe->AutoClearStall = Flag; break;
        case PIPE_TRANSFER_TIMEOUT: Pipe->Timeout = Request->Value; break;
        case IGNORE_SHORT_PACKETS: Pipe->IgnoreShortPackets = Flag; break;
        case ALLOW_PARTIAL_READS: Pipe->AllowPartialReads = Flag; break;
        case AUTO_FLUSH: Pipe->AutoFlush = Flag; break;
        case RAW_IO: Pipe->RawIo = Flag; break;
        case RESET_PIPE_ON_RESUME: Pipe->ResetPipeOnResume = Flag; break;
        default:
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            return STATUS_INVALID_PARAMETER;
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
WinUsbPowerPolicy(
    _In_ PWINUSB_DEVICE Device,
    _Inout_ PLNT_WINUSB_POLICY_REQUEST Request,
    _In_ BOOLEAN Set)
{
    /* TODO: selective suspend of an idle device when AUTO_SUSPEND and DeviceIdleEnabled allow it; the device stays in D0 */
    switch (Request->PolicyType)
    {
        case AUTO_SUSPEND:
            if (Request->ValueLength < sizeof(UCHAR))
                return STATUS_INVALID_PARAMETER;
            if (Set)
                Device->AutoSuspend = (Request->Value & 0xFF) != 0;
            Request->Value = Device->AutoSuspend;
            Request->ValueLength = sizeof(UCHAR);
            return STATUS_SUCCESS;

        case SUSPEND_DELAY:
            if (Request->ValueLength < sizeof(ULONG))
                return STATUS_INVALID_PARAMETER;
            if (Set)
                Device->SuspendDelay = Request->Value;
            Request->Value = Device->SuspendDelay;
            Request->ValueLength = sizeof(ULONG);
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static
PWINUSB_PIPE
WinUsbPipeFromRequest(
    _In_ PWINUSB_DEVICE Device,
    _In_ PIO_STACK_LOCATION IoStack,
    _In_ PVOID Buffer)
{
    PLNT_WINUSB_PIPE_REQUEST Request = Buffer;

    if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Request))
        return NULL;
    return WinUsbFindPipe(Device, Request->InterfaceIndex, Request->PipeId);
}

NTSTATUS
NTAPI
WinUsbDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PWINUSB_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Code = IoStack->Parameters.DeviceIoControl.IoControlCode;
    ULONG InputLength = IoStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    PLNT_WINUSB_POLICY_REQUEST Policy;
    PLNT_WINUSB_SETTING_REQUEST Setting;
    ULONG_PTR Information = 0;
    PWINUSB_PIPE Pipe;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Status = IoAcquireRemoveLock(&Device->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return WinUsbCompleteIrp(Irp, Status, 0);

    if (!Device->Started || Device->Gone)
    {
        IoReleaseRemoveLock(&Device->RemoveLock, Irp);
        return WinUsbCompleteIrp(Irp, STATUS_DEVICE_NOT_CONNECTED, 0);
    }

    switch (Code)
    {
        case IOCTL_LNT_WINUSB_READ_PIPE:
        case IOCTL_LNT_WINUSB_WRITE_PIPE:
            return WinUsbQueueTransfer(Device, Irp, Code == IOCTL_LNT_WINUSB_READ_PIPE);

        case IOCTL_LNT_WINUSB_CONTROL_IN:
        case IOCTL_LNT_WINUSB_CONTROL_OUT:
            return WinUsbControlTransfer(Device, Irp, Code == IOCTL_LNT_WINUSB_CONTROL_IN);

        case IOCTL_LNT_WINUSB_INITIALIZE:
            Status = WinUsbInitialize(Device, Irp, &Information);
            break;

        case IOCTL_LNT_WINUSB_GET_DESCRIPTOR:
            Status = WinUsbGetDescriptor(Device, Irp);
            Information = Irp->IoStatus.Information;
            break;

        case IOCTL_LNT_WINUSB_SET_SETTING:
            Setting = Buffer;
            if (InputLength < sizeof(*Setting))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Status = WinUsbSelectSetting(Device, Setting->InterfaceIndex, Setting->AlternateSetting);
            break;

        case IOCTL_LNT_WINUSB_GET_SETTING:
            Setting = Buffer;
            if (InputLength < sizeof(*Setting) || OutputLength < sizeof(*Setting) ||
                Setting->InterfaceIndex >= Device->InterfaceCount)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Setting->AlternateSetting = Device->Interfaces[Setting->InterfaceIndex].Setting;
            Information = sizeof(*Setting);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_LNT_WINUSB_SET_PIPE_POLICY:
        case IOCTL_LNT_WINUSB_GET_PIPE_POLICY:
        case IOCTL_LNT_WINUSB_SET_POWER_POLICY:
        case IOCTL_LNT_WINUSB_GET_POWER_POLICY:
            Policy = Buffer;
            if (InputLength < sizeof(*Policy) || OutputLength < sizeof(*Policy))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (Code == IOCTL_LNT_WINUSB_SET_PIPE_POLICY)
                Status = WinUsbSetPipePolicy(Device, Policy);
            else if (Code == IOCTL_LNT_WINUSB_GET_PIPE_POLICY)
                Status = WinUsbGetPipePolicy(Device, Policy);
            else
                Status = WinUsbPowerPolicy(Device, Policy, Code == IOCTL_LNT_WINUSB_SET_POWER_POLICY);
            if (NT_SUCCESS(Status))
                Information = sizeof(*Policy);
            break;

        case IOCTL_LNT_WINUSB_RESET_PIPE:
        case IOCTL_LNT_WINUSB_ABORT_PIPE:
        case IOCTL_LNT_WINUSB_FLUSH_PIPE:
            Pipe = WinUsbPipeFromRequest(Device, IoStack, Buffer);
            if (!Pipe)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (Code == IOCTL_LNT_WINUSB_RESET_PIPE)
            {
                Status = WinUsbResetPipe(Device, Pipe);
            }
            else if (Code == IOCTL_LNT_WINUSB_ABORT_PIPE)
            {
                Status = WinUsbAbortPipe(Device, Pipe);
            }
            else
            {
                WinUsbFlushPipe(Device, Pipe);
                Status = STATUS_SUCCESS;
            }
            break;

        case IOCTL_LNT_WINUSB_GET_FRAME_NUMBER:
            if (OutputLength < sizeof(LNT_WINUSB_FRAME_NUMBER))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            Status = WinUsbGetFrameNumber(Device, Buffer);
            if (NT_SUCCESS(Status))
                Information = sizeof(LNT_WINUSB_FRAME_NUMBER);
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    IoReleaseRemoveLock(&Device->RemoveLock, Irp);
    return WinUsbCompleteIrp(Irp, Status, Information);
}

NTSTATUS
NTAPI
WinUsbAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_OBJECT DeviceObject;
    PWINUSB_DEVICE Device;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Status = IoCreateDevice(DriverObject, sizeof(WINUSB_DEVICE), NULL, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Device = DeviceObject->DeviceExtension;
    RtlZeroMemory(Device, sizeof(*Device));
    Device->Self = DeviceObject;
    Device->Pdo = PhysicalDeviceObject;
    Device->ControlTimeout = LNT_WINUSB_DEFAULT_CONTROL_TIMEOUT;
    KeInitializeSpinLock(&Device->Lock);
    KeInitializeMutex(&Device->ConfigurationMutex, 0);
    IoInitializeRemoveLock(&Device->RemoveLock, WINUSB_TAG, 0, 0);

    Device->Lower = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (!Device->Lower)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
WinUsbUnload(
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
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->DriverExtension->AddDevice = WinUsbAddDevice;
    DriverObject->DriverUnload = WinUsbUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = WinUsbDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = WinUsbDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = WinUsbDispatchCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = WinUsbDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = WinUsbDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = WinUsbDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = WinUsbDispatchSystemControl;
    return STATUS_SUCCESS;
}
