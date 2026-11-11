/*
 * PROJECT:     LiberNT USB driver support library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USBD client handles, capability queries and URB allocation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <usb.h>
#include <usbdlib.h>
#include <reactos/drivers/usbcapability.h>

#define USBDEX_HANDLE_SIGNATURE 'hdBU'

typedef struct _USBDEX_HANDLE
{
    ULONG Signature;
    ULONG PoolTag;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT TargetDeviceObject;
} USBDEX_HANDLE, *PUSBDEX_HANDLE;

static
PUSBDEX_HANDLE
UsbdexHandle(
    _In_ USBD_HANDLE USBDHandle)
{
    PUSBDEX_HANDLE Handle = (PUSBDEX_HANDLE)USBDHandle;

    ASSERT(Handle == NULL || Handle->Signature == USBDEX_HANDLE_SIGNATURE);
    return Handle;
}

NTSTATUS
USBD_QueryUsbCapability(
    _In_ USBD_HANDLE USBDHandle,
    _In_ const GUID* CapabilityType,
    _In_ ULONG       OutputBufferLength,
    _When_(OutputBufferLength == 0, _Pre_null_)
    _When_(OutputBufferLength != 0 && ResultLength == NULL, _Out_writes_bytes_(OutputBufferLength))
    _When_(OutputBufferLength != 0 && ResultLength != NULL, _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultLength))
        PUCHAR                        OutputBuffer,
    _Out_opt_
    _When_(ResultLength != NULL, _Deref_out_range_(<=,OutputBufferLength))
        PULONG                        ResultLength
)
{
    PUSBDEX_HANDLE Handle = UsbdexHandle(USBDHandle);
    ROS_USB_CAPABILITY_QUERY Query;
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Next;
    NTSTATUS Status;
    KEVENT Event;
    PIRP Irp;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (ResultLength)
        *ResultLength = 0;
    if (!Handle || !CapabilityType || (OutputBuffer == NULL) != (OutputBufferLength == 0))
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Query, sizeof(Query));
    Query.Version = ROS_USB_CAPABILITY_QUERY_VERSION;
    Query.Size = sizeof(Query);
    Query.CapabilityType = *CapabilityType;
    Query.OutputBufferLength = OutputBufferLength;
    Query.OutputBuffer = OutputBuffer;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_REACTOS_USB_QUERY_CAPABILITY, Handle->TargetDeviceObject,
                                        NULL, 0, NULL, 0, TRUE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Next = IoGetNextIrpStackLocation(Irp);
    Next->Parameters.Others.Argument1 = &Query;
    Status = IoCallDriver(Handle->TargetDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (NT_SUCCESS(Status) && ResultLength)
    {
        ASSERT(Query.ResultLength <= OutputBufferLength);
        *ResultLength = Query.ResultLength;
    }
    return Status;
}

VOID
USBD_AssignUrbToIoStackLocation(
    _In_ USBD_HANDLE USBDHandle,
    _In_ PIO_STACK_LOCATION IoStackLocation,
    _In_ PURB Urb
)
{
    ASSERT(UsbdexHandle(USBDHandle) != NULL);
    ASSERT(IoStackLocation != NULL && Urb != NULL);
    IoStackLocation->Parameters.Others.Argument1 = Urb;
}

NTSTATUS
USBD_CreateHandle(
    _In_      PDEVICE_OBJECT DeviceObject,
    _In_      PDEVICE_OBJECT TargetDeviceObject,
    _In_      ULONG          USBDClientContractVersion,
    _In_      ULONG          PoolTag,
    _Out_     USBD_HANDLE   *USBDHandle
)
{
    PUSBDEX_HANDLE Handle;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_LEVEL;
    if (!DeviceObject || !TargetDeviceObject || !USBDHandle ||
        USBDClientContractVersion != USBD_CLIENT_CONTRACT_VERSION_602 || PoolTag == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Handle = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Handle), PoolTag);
    if (!Handle)
        return STATUS_INSUFFICIENT_RESOURCES;
    Handle->Signature = USBDEX_HANDLE_SIGNATURE;
    Handle->PoolTag = PoolTag;
    Handle->DeviceObject = DeviceObject;
    Handle->TargetDeviceObject = TargetDeviceObject;
    *USBDHandle = (USBD_HANDLE)Handle;
    return STATUS_SUCCESS;
}

VOID
USBD_CloseHandle(
    _In_ USBD_HANDLE USBDHandle
)
{
    PUSBDEX_HANDLE Handle = UsbdexHandle(USBDHandle);

    if (!Handle)
        return;
    Handle->Signature = 0;
    ExFreePoolWithTag(Handle, Handle->PoolTag);
}

NTSTATUS
USBD_UrbAllocate(
    _In_ USBD_HANDLE USBDHandle,
    _Outptr_result_bytebuffer_(sizeof(URB)) PURB *Urb
)
{
    PUSBDEX_HANDLE Handle = UsbdexHandle(USBDHandle);

    if (!Handle || !Urb)
        return STATUS_INVALID_PARAMETER;
    *Urb = ExAllocatePoolWithTag(NonPagedPool, sizeof(URB), Handle->PoolTag);
    if (!*Urb)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(*Urb, sizeof(URB));
    return STATUS_SUCCESS;
}

NTSTATUS
USBD_IsochUrbAllocate(
    _In_ USBD_HANDLE USBDHandle,
    _In_ ULONG NumberOfIsochPacket,
    _Outptr_result_bytebuffer_(sizeof(struct _URB_ISOCH_TRANSFER)
                               + (NumberOfIsochPackets * sizeof(USBD_ISO_PACKET_DESCRIPTOR))
                               - sizeof(USBD_ISO_PACKET_DESCRIPTOR))
    PURB *Urb
)
{
    PUSBDEX_HANDLE Handle = UsbdexHandle(USBDHandle);
    SIZE_T Size;

    if (!Handle || !Urb || NumberOfIsochPacket == 0)
        return STATUS_INVALID_PARAMETER;
    Size = GET_ISO_URB_SIZE(NumberOfIsochPacket);
    *Urb = ExAllocatePoolWithTag(NonPagedPool, Size, Handle->PoolTag);
    if (!*Urb)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(*Urb, Size);
    return STATUS_SUCCESS;
}

VOID
USBD_UrbFree(
    _In_ USBD_HANDLE USBDHandle,
    _In_ PURB Urb
)
{
    PUSBDEX_HANDLE Handle = UsbdexHandle(USBDHandle);

    ASSERT(Handle != NULL);
    if (Handle && Urb)
        ExFreePoolWithTag(Urb, Handle->PoolTag);
}
