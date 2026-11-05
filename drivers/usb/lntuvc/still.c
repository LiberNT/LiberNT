/*
 * PROJECT:     LiberNT USB Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Still image pin, its trigger and the status interrupt endpoint
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc.h"
#include "uvcguid.h"

#define NDEBUG
#include <debug.h>

#define UVC_STILL_REQUEST_SIZE 11
#define UVC_STILL_TRIGGER_TRANSMIT 1
#define UVC_STATUS_STREAMING 2
#define UVC_STATUS_BUTTON 0
#define UVC_STATUS_PRESSED 1

typedef struct _UVC_STILL
{
    PKSPIN Pin;
    PUVC_DEVICE Device;
    PUVC_FORMAT Format;
    PUVC_STILL_SIZE Size;
    ULONG ImageSize;
    PUCHAR Buffers[2];
    ULONG FillIndex;
    ULONG FillUsed;
    LONG ReadyIndex;
    ULONG ReadyUsed;
    ULONGLONG ReadyTime;
    BOOLEAN Busy;
    BOOLEAN Error;
} UVC_STILL, *PUVC_STILL;

VOID
UvcStillFrameBegin(
    PUVC_DEVICE Device)
{
    PUVC_STILL Still;

    KeAcquireSpinLockAtDpcLevel(&Device->StillLock);
    Still = Device->Still;
    if (Still)
    {
        Still->FillUsed = 0;
        Still->Error = FALSE;
    }
    KeReleaseSpinLockFromDpcLevel(&Device->StillLock);
}

VOID
UvcStillFrameData(
    PUVC_DEVICE Device,
    PUCHAR Data,
    ULONG Length)
{
    PUVC_STILL Still;

    KeAcquireSpinLockAtDpcLevel(&Device->StillLock);
    Still = Device->Still;
    if (Still)
    {
        if (Length > Still->ImageSize - Still->FillUsed)
        {
            Length = Still->ImageSize - Still->FillUsed;
            Still->Error = TRUE;
        }
        RtlCopyMemory(Still->Buffers[Still->FillIndex] + Still->FillUsed, Data, Length);
        Still->FillUsed += Length;
    }
    KeReleaseSpinLockFromDpcLevel(&Device->StillLock);
}

VOID
UvcStillFrameEnd(
    PUVC_DEVICE Device,
    BOOLEAN Error)
{
    PUVC_STILL Still;
    PKSPIN Pin = NULL;

    KeAcquireSpinLockAtDpcLevel(&Device->StillLock);
    Still = Device->Still;
    if (Still && !Error && !Still->Error && Still->FillUsed &&
        (!Still->Format->FixedSize || Still->FillUsed == Still->ImageSize) &&
        Still->ReadyIndex < 0 && !Still->Busy)
    {
        Still->ReadyIndex = Still->FillIndex;
        Still->ReadyUsed = Still->FillUsed;
        Still->ReadyTime = KeQueryInterruptTime();
        Still->FillIndex ^= 1;
        Pin = Still->Pin;
    }
    if (Pin)
        KsPinAttemptProcessing(Pin, TRUE);
    KeReleaseSpinLockFromDpcLevel(&Device->StillLock);
}

NTSTATUS
UvcStillTrigger(
    PUVC_DEVICE Device)
{
    UCHAR Request[UVC_STILL_REQUEST_SIZE] = { 0 };
    UCHAR Trigger = UVC_STILL_TRIGGER_TRANSMIT;
    PUVC_STILL Still;
    KIRQL OldIrql;
    NTSTATUS Status;

    KeAcquireSpinLock(&Device->StillLock, &OldIrql);
    Still = Device->Still;
    if (Still)
    {
        Request[0] = Still->Format->Index;
        Request[1] = (UCHAR)(Still->Size - Still->Format->StillSizes) + 1;
        Request[2] = Still->Format->StillCompressions ? 1 : 0;
    }
    KeReleaseSpinLock(&Device->StillLock, OldIrql);

    if (!Still)
        return STATUS_SUCCESS;

    Status = UvcClassRequest(Device, FALSE, UVC_SET_CUR, UVC_VS_STILL_PROBE_CONTROL << 8, Device->StreamInterface, Request, sizeof(Request), NULL);
    if (NT_SUCCESS(Status))
        Status = UvcClassRequest(Device, TRUE, UVC_GET_CUR, UVC_VS_STILL_PROBE_CONTROL << 8, Device->StreamInterface, Request, sizeof(Request), NULL);
    if (NT_SUCCESS(Status))
        Status = UvcClassRequest(Device, FALSE, UVC_SET_CUR, UVC_VS_STILL_COMMIT_CONTROL << 8, Device->StreamInterface, Request, sizeof(Request), NULL);
    if (NT_SUCCESS(Status))
        Status = UvcClassRequest(Device, FALSE, UVC_SET_CUR, UVC_VS_STILL_TRIGGER_CONTROL << 8, Device->StreamInterface, &Trigger, sizeof(Trigger), NULL);

    if (!NT_SUCCESS(Status))
        DPRINT1("USB video function: the still trigger failed %lx\n", Status);
    return Status;
}

static
NTSTATUS
NTAPI
UvcStillProcess(
    PKSPIN Pin)
{
    PUVC_STILL Still = Pin->Context;
    PUVC_DEVICE Device = Still->Device;
    PKSSTREAM_POINTER Leading;
    PKSSTREAM_HEADER Header;
    KIRQL OldIrql;
    LONG Index;
    ULONG Used;
    ULONGLONG Time;

    Leading = KsPinGetLeadingEdgeStreamPointer(Pin, KSSTREAM_POINTER_STATE_LOCKED);
    if (!Leading)
        return STATUS_PENDING;

    KeAcquireSpinLock(&Device->StillLock, &OldIrql);
    Index = Still->ReadyIndex;
    Used = Still->ReadyUsed;
    Time = Still->ReadyTime;
    if (Index >= 0)
    {
        Still->ReadyIndex = -1;
        Still->Busy = TRUE;
    }
    KeReleaseSpinLock(&Device->StillLock, OldIrql);

    if (Index < 0)
    {
        KsStreamPointerUnlock(Leading, FALSE);
        return STATUS_SUCCESS;
    }

    if (Used > Leading->OffsetOut.Remaining)
    {
        KeAcquireSpinLock(&Device->StillLock, &OldIrql);
        Still->Busy = FALSE;
        KeReleaseSpinLock(&Device->StillLock, OldIrql);
        KsStreamPointerUnlock(Leading, FALSE);
        return STATUS_SUCCESS;
    }

    RtlCopyMemory(Leading->OffsetOut.Data, Still->Buffers[Index], Used);
    Header = Leading->StreamHeader;
    Header->OptionsFlags = KSSTREAM_HEADER_OPTIONSF_SPLICEPOINT | KSSTREAM_HEADER_OPTIONSF_TIMEVALID;
    Header->DataUsed = Used;
    Header->Duration = 0;
    Header->PresentationTime.Time = Time;
    Header->PresentationTime.Numerator = 0;
    Header->PresentationTime.Denominator = 0;
    if (Header->Size >= sizeof(KSSTREAM_HEADER) + sizeof(KS_FRAME_INFO))
    {
        PKS_FRAME_INFO Info = (PKS_FRAME_INFO)(Header + 1);

        RtlZeroMemory(Info, sizeof(*Info));
        Info->ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
    }

    KeAcquireSpinLock(&Device->StillLock, &OldIrql);
    Still->Busy = FALSE;
    KeReleaseSpinLock(&Device->StillLock, OldIrql);

    KsStreamPointerAdvanceOffsetsAndUnlock(Leading, 0, Used, TRUE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcStillFindFormat(
    PUVC_DEVICE Device,
    PKSDATAFORMAT DataFormat,
    PUVC_FORMAT *OutFormat,
    PUVC_STILL_SIZE *OutSize)
{
    KS_DATAFORMAT_VIDEOINFOHEADER *Video = (KS_DATAFORMAT_VIDEOINFOHEADER *)DataFormat;
    ULONG F, I;

    if (DataFormat->FormatSize < sizeof(*Video) ||
        !IsEqualGUIDAligned(&DataFormat->Specifier, &UVC_KSDATAFORMAT_SPECIFIER_VIDEOINFO))
    {
        return STATUS_NO_MATCH;
    }

    for (F = 0; F < Device->FormatCount; F++)
    {
        PUVC_FORMAT Format = &Device->Formats[F];

        if (!IsEqualGUIDAligned(&DataFormat->SubFormat, &Format->SubFormat))
            continue;

        for (I = 0; I < Format->StillCount; I++)
        {
            if (Video->VideoInfoHeader.bmiHeader.biWidth == Format->StillSizes[I].Width &&
                Video->VideoInfoHeader.bmiHeader.biHeight == Format->StillSizes[I].Height)
            {
                *OutFormat = Format;
                *OutSize = &Format->StillSizes[I];
                return STATUS_SUCCESS;
            }
        }
    }

    return STATUS_NO_MATCH;
}

static
VOID
UvcStillFreeBuffers(
    PUVC_STILL Still)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Still->Buffers); Index++)
    {
        if (Still->Buffers[Index])
        {
            ExFreePoolWithTag(Still->Buffers[Index], LNTUVC_TAG);
            Still->Buffers[Index] = NULL;
        }
    }
}

static
VOID
UvcStillDetach(
    PUVC_STILL Still)
{
    PUVC_DEVICE Device = Still->Device;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Device->StillLock, &OldIrql);
    if (Device->Still == Still)
        Device->Still = NULL;
    KeReleaseSpinLock(&Device->StillLock, OldIrql);
}

static
NTSTATUS
NTAPI
UvcStillSetDeviceState(
    PKSPIN Pin,
    KSSTATE To,
    KSSTATE From)
{
    PUVC_STILL Still = Pin->Context;
    PUVC_DEVICE Device = Still->Device;
    KIRQL OldIrql;
    ULONG Index;

    if (From == To)
        return STATUS_SUCCESS;

    if (From == KSSTATE_STOP)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Still->Buffers); Index++)
        {
            Still->Buffers[Index] = ExAllocatePoolWithTag(NonPagedPool, Still->ImageSize, LNTUVC_TAG);
            if (!Still->Buffers[Index])
            {
                UvcStillFreeBuffers(Still);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
        }
        Still->FillIndex = 0;
        Still->FillUsed = 0;
        Still->ReadyIndex = -1;
        Still->Busy = FALSE;
    }

    if (To == KSSTATE_RUN)
    {
        KeAcquireSpinLock(&Device->StillLock, &OldIrql);
        Device->Still = Still;
        KeReleaseSpinLock(&Device->StillLock, OldIrql);
    }
    else if (From == KSSTATE_RUN)
    {
        UvcStillDetach(Still);
    }

    if (To == KSSTATE_STOP)
    {
        KsPinAcquireProcessingMutex(Pin);
        UvcStillFreeBuffers(Still);
        KsPinReleaseProcessingMutex(Pin);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcStillCreate(
    PKSPIN Pin,
    PIRP Irp)
{
    PUVC_DEVICE Device = KsFilterGetDevice(KsPinGetParentFilter(Pin))->Context;
    PUVC_FORMAT Format;
    PUVC_STILL_SIZE Size;
    PUVC_STILL Still;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);

    if (Device->CapturePins == 0)
        return STATUS_INVALID_PARAMETER;

    Status = UvcStillFindFormat(Device, Pin->ConnectionFormat, &Format, &Size);
    if (!NT_SUCCESS(Status))
        return Status;

    Still = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Still), LNTUVC_TAG);
    if (!Still)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Still, sizeof(*Still));
    Status = KsAddItemToObjectBag(Pin->Bag, Still, ExFreePool);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Still, LNTUVC_TAG);
        return Status;
    }

    Still->Pin = Pin;
    Still->Device = Device;
    Still->Format = Format;
    Still->Size = Size;
    Still->ImageSize = UvcStillImageSize(Format, Size);
    Still->ReadyIndex = -1;
    Pin->StreamHeaderSize = 0;
    Pin->Context = Still;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcStillClose(
    PKSPIN Pin,
    PIRP Irp)
{
    PUVC_STILL Still = Pin->Context;

    UNREFERENCED_PARAMETER(Irp);

    if (Still)
    {
        UvcStillDetach(Still);
        UvcStillFreeBuffers(Still);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcStillSetDataFormat(
    PKSPIN Pin,
    PKSDATAFORMAT OldFormat,
    PKSMULTIPLE_ITEM OldAttributes,
    const KSDATARANGE *DataRange,
    const KSATTRIBUTE_LIST *AttributeRange)
{
    PUVC_DEVICE Device = KsFilterGetDevice(KsPinGetParentFilter(Pin))->Context;
    PUVC_FORMAT Format;
    PUVC_STILL_SIZE Size;

    UNREFERENCED_PARAMETER(OldFormat);
    UNREFERENCED_PARAMETER(OldAttributes);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(AttributeRange);

    return UvcStillFindFormat(Device, Pin->ConnectionFormat, &Format, &Size);
}

ULONG
UvcStillImageSize(
    PUVC_FORMAT Format,
    PUVC_STILL_SIZE Size)
{
    return (ULONG)Size->Width * Size->Height * Format->BitCount / 8;
}

const KSPIN_DISPATCH UvcStillDispatch =
{
    UvcStillCreate,
    UvcStillClose,
    UvcStillProcess,
    NULL,
    UvcStillSetDataFormat,
    UvcStillSetDeviceState,
    NULL,
    NULL,
    NULL,
    NULL
};

static IO_COMPLETION_ROUTINE UvcStatusComplete;

static
VOID
UvcStatusSubmit(
    PUVC_DEVICE Device)
{
    PIRP Irp = Device->StatusIrp;
    PIO_STACK_LOCATION Stack;

    IoReuseIrp(Irp, STATUS_NOT_SUPPORTED);
    RtlZeroMemory(&Device->StatusUrb, sizeof(Device->StatusUrb));
    UsbBuildInterruptOrBulkTransferRequest((PURB)&Device->StatusUrb,
                                           sizeof(Device->StatusUrb),
                                           Device->StatusPipe,
                                           Device->StatusBuffer,
                                           NULL,
                                           sizeof(Device->StatusBuffer),
                                           USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
                                           NULL);
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    Stack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    Stack->Parameters.Others.Argument1 = &Device->StatusUrb;
    IoSetCompletionRoutine(Irp, UvcStatusComplete, Device, TRUE, TRUE, TRUE);
    IoCallDriver(Device->LowerDevice, Irp);
}

static
VOID
NTAPI
UvcStatusWork(
    PDEVICE_OBJECT DeviceObject,
    PVOID Context)
{
    PUVC_DEVICE Device = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    InterlockedExchange(&Device->StatusWorkQueued, 0);
    UvcStillTrigger(Device);
    if (InterlockedDecrement(&Device->StatusPending) == 0)
        KeSetEvent(&Device->StatusIdle, IO_NO_INCREMENT, FALSE);
}

static
VOID
UvcStatusPacket(
    PUVC_DEVICE Device)
{
    PUCHAR Data = Device->StatusBuffer;
    ULONG Length = Device->StatusUrb.TransferBufferLength;

    if (Length < 4 || (Data[0] & 0x0F) != UVC_STATUS_STREAMING)
        return;
    if (Data[2] != UVC_STATUS_BUTTON || Data[3] != UVC_STATUS_PRESSED || !Device->StatusWorkItem)
        return;
    if (InterlockedCompareExchange(&Device->StatusWorkQueued, 1, 0) != 0)
        return;

    InterlockedIncrement(&Device->StatusPending);
    IoQueueWorkItem(Device->StatusWorkItem, UvcStatusWork, DelayedWorkQueue, Device);
}

static
NTSTATUS
NTAPI
UvcStatusComplete(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context)
{
    PUVC_DEVICE Device = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (NT_SUCCESS(Irp->IoStatus.Status) && Device->StatusRunning && !Device->Gone)
    {
        UvcStatusPacket(Device);
        UvcStatusSubmit(Device);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    if (InterlockedDecrement(&Device->StatusPending) == 0)
        KeSetEvent(&Device->StatusIdle, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

VOID
UvcStatusStart(
    PUVC_DEVICE Device)
{
    if (!Device->StatusPipe || Device->StatusRunning)
        return;

    if (!Device->StatusIrp)
    {
        Device->StatusIrp = IoAllocateIrp(Device->LowerDevice->StackSize, FALSE);
        if (!Device->StatusIrp)
            return;
    }
    if (!Device->StatusWorkItem)
        Device->StatusWorkItem = IoAllocateWorkItem(Device->KsDevice->FunctionalDeviceObject);

    KeInitializeEvent(&Device->StatusIdle, NotificationEvent, FALSE);
    Device->StatusPending = 1;
    Device->StatusWorkQueued = 0;
    Device->StatusRunning = TRUE;
    UvcStatusSubmit(Device);
}

VOID
UvcStatusStop(
    PUVC_DEVICE Device)
{
    if (!Device->StatusRunning)
        return;

    Device->StatusRunning = FALSE;
    IoCancelIrp(Device->StatusIrp);
    KeWaitForSingleObject(&Device->StatusIdle, Executive, KernelMode, FALSE, NULL);
}

VOID
UvcStatusFree(
    PUVC_DEVICE Device)
{
    UvcStatusStop(Device);
    if (Device->StatusIrp)
    {
        IoFreeIrp(Device->StatusIrp);
        Device->StatusIrp = NULL;
    }
    if (Device->StatusWorkItem)
    {
        IoFreeWorkItem(Device->StatusWorkItem);
        Device->StatusWorkItem = NULL;
    }
}
