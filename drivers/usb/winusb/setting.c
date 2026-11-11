/*
 * PROJECT:     LiberNT WinUSB driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Descriptors, alternate settings and the bus frame number
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winusb.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
WinUsbGetDescriptor(
    _In_ PWINUSB_DEVICE Device,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PLNT_WINUSB_DESCRIPTOR_REQUEST Input = Irp->AssociatedIrp.SystemBuffer;
    ULONG Length = IoStack->Parameters.DeviceIoControl.OutputBufferLength;
    struct _URB_CONTROL_DESCRIPTOR_REQUEST Urb;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    Irp->IoStatus.Information = 0;
    if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Input) || !Length || !Irp->MdlAddress)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Urb, sizeof(Urb));
    UsbBuildGetDescriptorRequest((PURB)&Urb, sizeof(Urb), Input->DescriptorType, Input->Index, Input->LanguageId,
                                 NULL, Irp->MdlAddress, min(Length, MAXUSHORT), NULL);
    Status = WinUsbSubmitUrbSync(Device, (PURB)&Urb);
    if (!NT_SUCCESS(Status))
        return Urb.Hdr.Status == USBD_STATUS_CANCELED ? STATUS_CANCELLED : STATUS_UNSUCCESSFUL;
    Irp->IoStatus.Information = Urb.TransferBufferLength;
    return STATUS_SUCCESS;
}

NTSTATUS
WinUsbSelectSetting(
    _In_ PWINUSB_DEVICE Device,
    _In_ UCHAR InterfaceIndex,
    _In_ UCHAR Setting)
{
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PUSBD_INTERFACE_INFORMATION Info;
    PWINUSB_INTERFACE Interface;
    ULONG Size, Pipe;
    NTSTATUS Status;
    KIRQL OldIrql;
    PURB Urb;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (InterfaceIndex >= Device->InterfaceCount)
        return STATUS_INVALID_PARAMETER;
    Interface = &Device->Interfaces[InterfaceIndex];

    KeWaitForSingleObject(&Device->ConfigurationMutex, Executive, KernelMode, FALSE, NULL);
    Descriptor = USBD_ParseConfigurationDescriptorEx(Device->Configuration, Device->Configuration,
                                                     Interface->Number, Setting, -1, -1, -1);
    if (!Descriptor || Descriptor->bNumEndpoints > WINUSB_MAX_PIPES)
    {
        KeReleaseMutex(&Device->ConfigurationMutex, FALSE);
        return STATUS_NOT_FOUND;
    }

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (WinUsbInterfaceBusy(Device, Interface))
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        KeReleaseMutex(&Device->ConfigurationMutex, FALSE);
        return STATUS_DEVICE_BUSY;
    }
    Interface->Selecting = TRUE;
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    Size = GET_SELECT_INTERFACE_REQUEST_SIZE(Descriptor->bNumEndpoints);
    Urb = ExAllocatePoolZero(NonPagedPool, Size, WINUSB_TAG);
    if (!Urb)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Urb->UrbSelectInterface.Hdr.Length = (USHORT)Size;
    Urb->UrbSelectInterface.Hdr.Function = URB_FUNCTION_SELECT_INTERFACE;
    Urb->UrbSelectInterface.ConfigurationHandle = Device->ConfigurationHandle;
    Info = &Urb->UrbSelectInterface.Interface;
    Info->Length = (USHORT)GET_USBD_INTERFACE_SIZE(Descriptor->bNumEndpoints);
    Info->InterfaceNumber = Interface->Number;
    Info->AlternateSetting = Setting;
    for (Pipe = 0; Pipe < Descriptor->bNumEndpoints; Pipe++)
        Info->Pipes[Pipe].MaximumTransferSize = LNT_WINUSB_MAXIMUM_TRANSFER_SIZE;

    Status = WinUsbSubmitUrbSync(Device, Urb);
    if (NT_SUCCESS(Status))
    {
        for (Pipe = 0; Pipe < Interface->PipeCount; Pipe++)
            WinUsbFlushPipe(Device, &Interface->Pipes[Pipe]);
        KeAcquireSpinLock(&Device->Lock, &OldIrql);
        ASSERT(!WinUsbInterfaceBusy(Device, Interface));
        WinUsbLoadPipes(Interface, Info);
        KeReleaseSpinLock(&Device->Lock, OldIrql);
    }
    else
    {
        DPRINT1("WinUSB: select of setting %u on interface %u failed: %lx\n", Setting, Interface->Number, Status);
        Status = STATUS_UNSUCCESSFUL;
    }
    ExFreePoolWithTag(Urb, WINUSB_TAG);

Exit:
    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Interface->Selecting = FALSE;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    KeReleaseMutex(&Device->ConfigurationMutex, FALSE);
    return Status;
}

NTSTATUS
WinUsbGetFrameNumber(
    _In_ PWINUSB_DEVICE Device,
    _Out_ PLNT_WINUSB_FRAME_NUMBER FrameNumber)
{
    struct _URB_GET_CURRENT_FRAME_NUMBER Urb;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    RtlZeroMemory(&Urb, sizeof(Urb));
    Urb.Hdr.Length = sizeof(Urb);
    Urb.Hdr.Function = URB_FUNCTION_GET_CURRENT_FRAME_NUMBER;
    Status = WinUsbSubmitUrbSync(Device, (PURB)&Urb);
    FrameNumber->TimeStamp = KeQueryPerformanceCounter(NULL);
    FrameNumber->FrameNumber = Urb.FrameNumber;
    FrameNumber->Reserved = 0;
    return Status;
}
