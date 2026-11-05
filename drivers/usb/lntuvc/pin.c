/*
 * PROJECT:     LiberNT USB Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Capture pin: format negotiation, USB streaming and frame assembly
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lntuvc.h"
#include "uvcguid.h"

#define NDEBUG
#include <debug.h>

#define WRITE16(P, V) do { (P)[0] = (UCHAR)(V); (P)[1] = (UCHAR)((V) >> 8); } while (0)
#define WRITE32(P, V) do { (P)[0] = (UCHAR)(V); (P)[1] = (UCHAR)((V) >> 8); (P)[2] = (UCHAR)((V) >> 16); (P)[3] = (UCHAR)((V) >> 24); } while (0)
#define READ32(P) ((ULONG)((P)[0] | ((P)[1] << 8) | ((P)[2] << 16) | ((ULONG)(P)[3] << 24)))

#define UVC_PROBE_HINT              0
#define UVC_PROBE_FORMAT            2
#define UVC_PROBE_FRAME             3
#define UVC_PROBE_INTERVAL          4
#define UVC_PROBE_MAX_FRAME         18
#define UVC_PROBE_MAX_PAYLOAD       22
#define UVC_PROBE_HINT_INTERVAL     0x0001

#define UVC_FRAMING_FLAGS           0x80004002

static
USHORT
UvcStreamIndex(
    PUVC_DEVICE Device)
{
    return Device->StreamInterface;
}

static
NTSTATUS
UvcProbe(
    PUVC_PIN Context,
    UCHAR Selector)
{
    PUVC_DEVICE Device = Context->Device;
    NTSTATUS Status;

    Status = UvcClassRequest(Device,
                             FALSE,
                             UVC_SET_CUR,
                             (USHORT)(Selector << 8),
                             UvcStreamIndex(Device),
                             Context->Probe,
                             Context->ProbeLength,
                             NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USB video stream: setting control %u failed %lx\n", Selector, Status);
    }
    return Status;
}

static
NTSTATUS
UvcNegotiate(
    PUVC_PIN Context)
{
    PUVC_DEVICE Device = Context->Device;
    ULONG Transferred = 0;
    NTSTATUS Status;

    Context->ProbeLength = Device->UvcVersion >= 0x0150 ? 48 : Device->UvcVersion >= 0x0110 ? 34 : 26;

    RtlZeroMemory(Context->Probe, sizeof(Context->Probe));
    WRITE16(Context->Probe + UVC_PROBE_HINT, UVC_PROBE_HINT_INTERVAL);
    Context->Probe[UVC_PROBE_FORMAT] = Context->Format->Index;
    Context->Probe[UVC_PROBE_FRAME] = Context->Frame->Index;
    WRITE32(Context->Probe + UVC_PROBE_INTERVAL, Context->Interval);

    Status = UvcProbe(Context, UVC_VS_PROBE_CONTROL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UvcClassRequest(Device,
                             TRUE,
                             UVC_GET_CUR,
                             UVC_VS_PROBE_CONTROL << 8,
                             UvcStreamIndex(Device),
                             Context->Probe,
                             Context->ProbeLength,
                             &Transferred);
    if (!NT_SUCCESS(Status) || Transferred < 26)
    {
        DPRINT1("USB video stream: reading the probe failed %lx (%lu bytes)\n", Status, Transferred);
        return NT_SUCCESS(Status) ? STATUS_DEVICE_DATA_ERROR : Status;
    }

    Context->MaxFrameSize = READ32(Context->Probe + UVC_PROBE_MAX_FRAME);
    Context->MaxPayloadSize = READ32(Context->Probe + UVC_PROBE_MAX_PAYLOAD);
    if (Context->MaxFrameSize == 0)
        Context->MaxFrameSize = Context->ImageSize;

    return STATUS_SUCCESS;
}

static
PUVC_ALT
UvcChooseAlt(
    PUVC_PIN Context)
{
    PUVC_DEVICE Device = Context->Device;
    PUVC_ALT Best = NULL, Largest = NULL;
    ULONG Index;

    for (Index = 0; Index < Device->StreamAltCount; Index++)
    {
        PUVC_ALT Alt = &Device->Alts[Index];

        if (!Largest || Alt->Bytes > Largest->Bytes)
            Largest = Alt;
        if (Alt->Bytes >= Context->MaxPayloadSize && (!Best || Alt->Bytes < Best->Bytes))
            Best = Alt;
    }

    return Best ? Best : Largest;
}

static
NTSTATUS
UvcSelectAlternate(
    PUVC_PIN Context,
    UCHAR Alternate,
    USBD_PIPE_HANDLE *Pipe)
{
    PUVC_DEVICE Device = Context->Device;
    ULONG Endpoints = Alternate ? 1 : (Device->Bulk ? 1 : 0);
    ULONG Size = GET_SELECT_INTERFACE_REQUEST_SIZE(max(Endpoints, 1));
    NTSTATUS Status;
    PURB Urb;

    Urb = ExAllocatePoolWithTag(NonPagedPool, Size, LNTUVC_TAG);
    if (!Urb)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Urb, Size);
    UsbBuildSelectInterfaceRequest(Urb, (USHORT)Size, Device->ConfigHandle, Device->StreamInterface, Alternate);
    Urb->UrbSelectInterface.Interface.Length = GET_USBD_INTERFACE_SIZE(Endpoints);

    Status = UvcSubmitUrbSync(Device, Urb);
    if (NT_SUCCESS(Status) && Pipe && Urb->UrbSelectInterface.Interface.NumberOfPipes)
        *Pipe = Urb->UrbSelectInterface.Interface.Pipes[0].PipeHandle;

    ExFreePoolWithTag(Urb, LNTUVC_TAG);
    return Status;
}

static
VOID
UvcPipeRequest(
    PUVC_PIN Context,
    USHORT Function)
{
    struct _URB_PIPE_REQUEST Urb;

    RtlZeroMemory(&Urb, sizeof(Urb));
    Urb.Hdr.Length = sizeof(Urb);
    Urb.Hdr.Function = Function;
    Urb.PipeHandle = Context->Pipe;
    UvcSubmitUrbSync(Context->Device, (PURB)&Urb);
}

static
VOID
UvcFinishFrame(
    PUVC_PIN Context)
{
    PUVC_FRAME_SLOT Slot = Context->FillSlot;
    BOOLEAN Valid;

    Context->InFrame = FALSE;
    Context->LastFid = Context->FrameFid;
    Context->HaveLastFid = TRUE;

    if (Context->FrameIsStill)
    {
        UvcStillFrameEnd(Context->Device, Context->FrameError);
        return;
    }

    Valid = !Context->FrameError && Slot->Used != 0;
    if (Context->Format->FixedSize && Slot->Used != Context->ImageSize)
        Valid = FALSE;

    if (!Valid)
    {
        Context->FramesDropped++;
        Context->Discontinuity = TRUE;
        return;
    }

    Slot->Time = KeQueryInterruptTime();

    if (Context->ReadySlot)
    {
        PUVC_FRAME_SLOT Old = Context->ReadySlot;

        Context->FramesDropped++;
        Context->Discontinuity = TRUE;
        Context->ReadySlot = Slot;
        Context->FillSlot = Old;
    }
    else
    {
        ULONG Index;

        Context->ReadySlot = Slot;
        for (Index = 0; Index < UVC_FRAME_SLOTS; Index++)
        {
            PUVC_FRAME_SLOT Free = &Context->Slots[Index];

            if (Free != Context->ReadySlot && Free != Context->BusySlot)
            {
                Context->FillSlot = Free;
                break;
            }
        }
    }

    KsPinAttemptProcessing(Context->Pin, TRUE);
}

static
VOID
UvcFramePayload(
    PUVC_PIN Context,
    UCHAR Flags,
    PUCHAR Data,
    ULONG Length,
    BOOLEAN EndOfPayload)
{
    UCHAR Fid = Flags & UVC_HDR_FID;

    if (Context->InFrame && Fid != Context->FrameFid)
        UvcFinishFrame(Context);

    if (!Context->InFrame)
    {
        if (Length == 0)
            return;
        if (Context->HaveLastFid && Fid == Context->LastFid)
            return;

        Context->InFrame = TRUE;
        Context->FrameFid = Fid;
        Context->FrameError = FALSE;
        Context->FrameIsStill = (Flags & UVC_HDR_STI) != 0;
        if (Context->FrameIsStill)
            UvcStillFrameBegin(Context->Device);
        else
            Context->FillSlot->Used = 0;
    }

    if (Length && Context->FrameIsStill)
    {
        UvcStillFrameData(Context->Device, Data, Length);
    }
    else if (Length)
    {
        PUVC_FRAME_SLOT Slot = Context->FillSlot;

        if (Length > Context->SlotSize - Slot->Used)
        {
            Length = Context->SlotSize - Slot->Used;
            Context->FrameError = TRUE;
        }
        RtlCopyMemory(Slot->Data + Slot->Used, Data, Length);
        Slot->Used += Length;
    }

    if (Flags & UVC_HDR_ERR)
        Context->FrameError = TRUE;

    if (EndOfPayload && (Flags & UVC_HDR_EOF))
        UvcFinishFrame(Context);
}

static
VOID
UvcHandleIso(
    PUVC_PIN Context,
    PURB Urb)
{
    struct _URB_ISOCH_TRANSFER *Iso = &Urb->UrbIsochronousTransfer;
    ULONG Index;

    for (Index = 0; Index < Iso->NumberOfPackets; Index++)
    {
        PUCHAR Data = (PUCHAR)Iso->TransferBuffer + Iso->IsoPacket[Index].Offset;
        ULONG Length = Iso->IsoPacket[Index].Length;
        ULONG Header;

        if (!USBD_SUCCESS(Iso->IsoPacket[Index].Status))
        {
            if (Context->InFrame)
                Context->FrameError = TRUE;
            continue;
        }

        if (Length < 2)
            continue;

        Header = Data[0];
        if (Header < 2 || Header > Length)
        {
            if (Context->InFrame)
                Context->FrameError = TRUE;
            continue;
        }

        UvcFramePayload(Context, Data[1], Data + Header, Length - Header, TRUE);
    }
}

static
VOID
UvcHandleBulk(
    PUVC_PIN Context,
    PURB Urb,
    ULONG TransferSize)
{
    struct _URB_BULK_OR_INTERRUPT_TRANSFER *Bulk = &Urb->UrbBulkOrInterruptTransfer;
    PUCHAR Data = Bulk->TransferBuffer;
    ULONG Length = Bulk->TransferBufferLength;
    BOOLEAN Complete;

    if (!Context->PayloadActive)
    {
        ULONG Header;

        if (Length < 2)
            return;

        Header = Data[0];
        if (Header < 2 || Header > Length)
        {
            if (Context->InFrame)
                Context->FrameError = TRUE;
            return;
        }

        Context->PayloadActive = TRUE;
        Context->PayloadFlags = Data[1];
        Context->PayloadBytes = Header;
        Data += Header;
        Length -= Header;
        Context->PayloadBytes += Length;
    }
    else
    {
        Context->PayloadBytes += Length;
    }

    Complete = Bulk->TransferBufferLength < TransferSize || Context->PayloadBytes >= Context->MaxPayloadSize;
    UvcFramePayload(Context, Context->PayloadFlags, Data, Length, Complete);
    if (Complete)
        Context->PayloadActive = FALSE;
}

static
VOID
UvcPrepareTransfer(
    PUVC_TRANSFER Transfer)
{
    PUVC_PIN Context = Transfer->Owner;
    PUVC_DEVICE Device = Context->Device;
    PIO_STACK_LOCATION Stack;
    PURB Urb = Transfer->Urb;
    PIRP Irp = Transfer->Irp;
    ULONG Index;

    IoReuseIrp(Irp, STATUS_NOT_SUPPORTED);
    Irp->Tail.Overlay.DriverContext[0] = Transfer;

    if (Device->Bulk)
    {
        RtlZeroMemory(Urb, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER));
        UsbBuildInterruptOrBulkTransferRequest(Urb,
                                               sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                               Context->Pipe,
                                               Transfer->Buffer,
                                               NULL,
                                               Transfer->Size,
                                               USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
                                               NULL);
    }
    else
    {
        ULONG Packet = Context->Alt->Bytes;

        RtlZeroMemory(Urb, GET_ISO_URB_SIZE(UVC_ISO_PACKETS));
        Urb->UrbIsochronousTransfer.Hdr.Function = URB_FUNCTION_ISOCH_TRANSFER;
        Urb->UrbIsochronousTransfer.Hdr.Length = (USHORT)GET_ISO_URB_SIZE(Context->IsoPackets);
        Urb->UrbIsochronousTransfer.PipeHandle = Context->Pipe;
        Urb->UrbIsochronousTransfer.TransferFlags = USBD_TRANSFER_DIRECTION_IN | USBD_START_ISO_TRANSFER_ASAP;
        Urb->UrbIsochronousTransfer.TransferBufferLength = Transfer->Size;
        Urb->UrbIsochronousTransfer.TransferBuffer = Transfer->Buffer;
        Urb->UrbIsochronousTransfer.NumberOfPackets = Context->IsoPackets;
        for (Index = 0; Index < Context->IsoPackets; Index++)
            Urb->UrbIsochronousTransfer.IsoPacket[Index].Offset = Index * Packet;
    }

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    Stack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    Stack->Parameters.Others.Argument1 = Urb;
}

static IO_COMPLETION_ROUTINE UvcTransferComplete;

static
VOID
UvcSubmitTransfer(
    PUVC_TRANSFER Transfer)
{
    UvcPrepareTransfer(Transfer);
    IoSetCompletionRoutine(Transfer->Irp, UvcTransferComplete, Transfer, TRUE, TRUE, TRUE);
    IoCallDriver(Transfer->Owner->Device->LowerDevice, Transfer->Irp);
}

static
VOID
UvcTransferDone(
    PUVC_PIN Context)
{
    if (InterlockedDecrement(&Context->Pending) == 0)
        KeSetEvent(&Context->Idle, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
NTAPI
UvcTransferComplete(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Param)
{
    PUVC_TRANSFER Transfer = Param;
    PUVC_PIN Context = Transfer->Owner;
    NTSTATUS Status = Irp->IoStatus.Status;
    BOOLEAN Streaming;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Streaming = Context->Streaming;
    if (NT_SUCCESS(Status) && Streaming)
    {
        if (Context->Device->Bulk)
            UvcHandleBulk(Context, Transfer->Urb, Transfer->Size);
        else
            UvcHandleIso(Context, Transfer->Urb);
    }
    KeReleaseSpinLock(&Context->Lock, OldIrql);

    if (!Streaming || Status == STATUS_CANCELLED)
    {
        UvcTransferDone(Context);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    if (NT_SUCCESS(Status))
    {
        InterlockedExchange(&Context->TransferErrors, 0);
        UvcSubmitTransfer(Transfer);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    if (InterlockedIncrement(&Context->TransferErrors) > UVC_MAX_TRANSFER_ERRORS || Context->Device->Gone)
    {
        DPRINT1("USB video stream: giving up after transfer error %lx\n", Status);
        UvcTransferDone(Context);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    ExInterlockedInsertTailList(&Context->RetryList, &Transfer->RetryEntry, &Context->Lock);
    KsIncrementCountedWorker(Context->RetryWorker);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
NTAPI
UvcRetryRoutine(
    PVOID Param)
{
    PUVC_PIN Context = Param;

    do
    {
        PLIST_ENTRY Entry;

        while ((Entry = ExInterlockedRemoveHeadList(&Context->RetryList, &Context->Lock)) != NULL)
        {
            PUVC_TRANSFER Transfer = CONTAINING_RECORD(Entry, UVC_TRANSFER, RetryEntry);

            if (Context->Streaming)
                UvcSubmitTransfer(Transfer);
            else
                UvcTransferDone(Context);
        }
    } while (KsDecrementCountedWorker(Context->RetryWorker));
}

static
VOID
UvcFreeTransfers(
    PUVC_PIN Context)
{
    ULONG Index;

    for (Index = 0; Index < Context->TransferCount; Index++)
    {
        PUVC_TRANSFER Transfer = &Context->Transfers[Index];

        if (Transfer->Irp)
            IoFreeIrp(Transfer->Irp);
        if (Transfer->Urb)
            ExFreePoolWithTag(Transfer->Urb, LNTUVC_TAG);
        if (Transfer->Buffer)
            ExFreePoolWithTag(Transfer->Buffer, LNTUVC_TAG);
        RtlZeroMemory(Transfer, sizeof(*Transfer));
    }
    Context->TransferCount = 0;
}

static
NTSTATUS
UvcAllocateTransfers(
    PUVC_PIN Context)
{
    PUVC_DEVICE Device = Context->Device;
    ULONG Size, UrbSize, Index;

    if (Device->Bulk)
    {
        ULONG Packet = Context->Alt->Bytes ? Context->Alt->Bytes : 512;

        Size = max(Context->MaxPayloadSize, UVC_MIN_BULK_TRANSFER);
        Size = min(Size, UVC_MAX_BULK_TRANSFER);
        Size = ((Size + Packet - 1) / Packet) * Packet;
        UrbSize = sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER);
    }
    else
    {
        Context->IsoPackets = UVC_ISO_PACKETS * Context->Alt->Bytes <= UVC_ISO_LARGE_TRANSFER ? UVC_ISO_PACKETS : UVC_ISO_PACKETS_LARGE;
        Size = Context->IsoPackets * Context->Alt->Bytes;
        UrbSize = GET_ISO_URB_SIZE(UVC_ISO_PACKETS);
    }

    for (Index = 0; Index < UVC_TRANSFER_COUNT; Index++)
    {
        PUVC_TRANSFER Transfer = &Context->Transfers[Index];

        Transfer->Owner = Context;
        Transfer->Size = Size;
        Transfer->Irp = IoAllocateIrp(Device->LowerDevice->StackSize, FALSE);
        Transfer->Urb = ExAllocatePoolWithTag(NonPagedPool, UrbSize, LNTUVC_TAG);
        Transfer->Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, LNTUVC_TAG);
        Context->TransferCount = Index + 1;

        if (!Transfer->Irp || !Transfer->Urb || !Transfer->Buffer)
        {
            UvcFreeTransfers(Context);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    return STATUS_SUCCESS;
}

static
VOID
UvcFreeSlots(
    PUVC_PIN Context)
{
    ULONG Index;

    for (Index = 0; Index < UVC_FRAME_SLOTS; Index++)
    {
        if (Context->Slots[Index].Data)
            ExFreePoolWithTag(Context->Slots[Index].Data, LNTUVC_TAG);
        Context->Slots[Index].Data = NULL;
    }
    Context->FillSlot = NULL;
    Context->ReadySlot = NULL;
    Context->BusySlot = NULL;
}

static
NTSTATUS
UvcAllocateSlots(
    PUVC_PIN Context)
{
    ULONG Index;

    Context->SlotSize = max(Context->MaxFrameSize, Context->ImageSize);

    for (Index = 0; Index < UVC_FRAME_SLOTS; Index++)
    {
        Context->Slots[Index].Data = ExAllocatePoolWithTag(NonPagedPool, Context->SlotSize, LNTUVC_TAG);
        if (!Context->Slots[Index].Data)
        {
            UvcFreeSlots(Context);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    return STATUS_SUCCESS;
}

static
VOID
UvcStopStreaming(
    PUVC_PIN Context)
{
    PUVC_DEVICE Device = Context->Device;
    KIRQL OldIrql;

    if (!Context->PipeOpen)
        return;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Context->Streaming = FALSE;
    KeReleaseSpinLock(&Context->Lock, OldIrql);

    UvcPipeRequest(Context, URB_FUNCTION_ABORT_PIPE);
    if (InterlockedDecrement(&Context->Pending) == 0)
        KeSetEvent(&Context->Idle, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&Context->Idle, Executive, KernelMode, FALSE, NULL);

    if (Device->Bulk)
        UvcPipeRequest(Context, URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL);
    else
        UvcSelectAlternate(Context, 0, NULL);

    UvcFreeTransfers(Context);
    Context->PipeOpen = FALSE;
    Context->Pipe = NULL;
}

static
NTSTATUS
UvcStartStreaming(
    PUVC_PIN Context)
{
    PUVC_DEVICE Device = Context->Device;
    USBD_PIPE_HANDLE Pipe = NULL;
    NTSTATUS Status;
    KIRQL OldIrql;
    ULONG Index;

    Status = UvcNegotiate(Context);
    if (!NT_SUCCESS(Status))
        return Status;

    Context->Alt = UvcChooseAlt(Context);
    if (!Context->Alt)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    if (!Device->Bulk)
    {
        Status = UvcProbe(Context, UVC_VS_COMMIT_CONTROL);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = UvcSelectAlternate(Context, Context->Alt->AlternateSetting, &Pipe);
    if (!NT_SUCCESS(Status) || !Pipe)
    {
        DPRINT1("USB video stream: selecting alternate setting %u failed %lx\n", Context->Alt->AlternateSetting, Status);
        return NT_SUCCESS(Status) ? STATUS_DEVICE_CONFIGURATION_ERROR : Status;
    }
    Context->Pipe = Pipe;

    Status = UvcAllocateTransfers(Context);
    if (!NT_SUCCESS(Status))
    {
        if (!Device->Bulk)
            UvcSelectAlternate(Context, 0, NULL);
        return Status;
    }

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Context->ReadySlot = NULL;
    Context->Delivered = FALSE;
    Context->Discontinuity = FALSE;
    KeReleaseSpinLock(&Context->Lock, OldIrql);

    Context->InFrame = FALSE;
    Context->FrameError = FALSE;
    Context->HaveLastFid = FALSE;
    Context->PayloadActive = FALSE;
    Context->TransferErrors = 0;
    Context->Streaming = TRUE;
    Context->PipeOpen = TRUE;
    Context->Pending = Context->TransferCount + 1;
    KeClearEvent(&Context->Idle);

    for (Index = 0; Index < Context->TransferCount; Index++)
        UvcSubmitTransfer(&Context->Transfers[Index]);

    if (Device->Bulk)
    {
        Status = UvcProbe(Context, UVC_VS_COMMIT_CONTROL);
        if (!NT_SUCCESS(Status))
        {
            UvcStopStreaming(Context);
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
UvcPrepare(
    PUVC_PIN Context)
{
    NTSTATUS Status;

    if (Context->Prepared)
        return STATUS_SUCCESS;

    Status = UvcAllocateSlots(Context);
    if (!NT_SUCCESS(Status))
        return Status;

    Context->FillSlot = &Context->Slots[0];
    Context->ReadySlot = NULL;
    Context->BusySlot = NULL;
    Context->Delivered = FALSE;
    Context->Discontinuity = FALSE;
    Context->FramesDropped = 0;
    Context->Prepared = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
UvcUnprepare(
    PUVC_PIN Context)
{
    if (!Context->Prepared)
        return;

    KsPinAcquireProcessingMutex(Context->Pin);
    UvcFreeSlots(Context);
    Context->Prepared = FALSE;
    KsPinReleaseProcessingMutex(Context->Pin);
}

static
NTSTATUS
NTAPI
UvcPinSetDeviceState(
    PKSPIN Pin,
    KSSTATE To,
    KSSTATE From)
{
    PUVC_PIN Context = Pin->Context;
    NTSTATUS Status = STATUS_SUCCESS;

    if (From == To)
        return STATUS_SUCCESS;

    if (From == KSSTATE_RUN)
        UvcStopStreaming(Context);

    if (From == KSSTATE_STOP)
    {
        Status = UvcPrepare(Context);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (To == KSSTATE_RUN)
    {
        Status = UvcStartStreaming(Context);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("USB video stream: starting failed %lx\n", Status);
            return Status;
        }
    }

    if (To == KSSTATE_STOP)
        UvcUnprepare(Context);

    return STATUS_SUCCESS;
}

static
VOID
UvcFillHeaders(
    PUVC_PIN Context,
    PKSSTREAM_POINTER Leading,
    PUVC_FRAME_SLOT Slot,
    BOOLEAN Discontinuity)
{
    PKSSTREAM_HEADER Header = Leading->StreamHeader;
    ULONG Options = KSSTREAM_HEADER_OPTIONSF_SPLICEPOINT |
                    KSSTREAM_HEADER_OPTIONSF_TIMEVALID |
                    KSSTREAM_HEADER_OPTIONSF_DURATIONVALID;

    if (Discontinuity)
        Options |= KSSTREAM_HEADER_OPTIONSF_DATADISCONTINUITY;

    Header->OptionsFlags = Options;
    Header->DataUsed = Slot->Used;
    Header->Duration = Context->Interval;
    Header->PresentationTime.Time = Slot->Time;
    Header->PresentationTime.Numerator = 0;
    Header->PresentationTime.Denominator = 0;

    if (Header->Size >= sizeof(KSSTREAM_HEADER) + sizeof(KS_FRAME_INFO))
    {
        PKS_FRAME_INFO Info = (PKS_FRAME_INFO)(Header + 1);

        RtlZeroMemory(Info, sizeof(*Info));
        Info->ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
        /* TODO: PictureNumber and DropCount when a clock is attached; Windows 11 leaves them 0 without one */
    }
}

static
NTSTATUS
NTAPI
UvcPinProcess(
    PKSPIN Pin)
{
    PUVC_PIN Context = Pin->Context;

    for (;;)
    {
        PKSSTREAM_POINTER Leading;
        PUVC_FRAME_SLOT Slot;
        BOOLEAN Discontinuity;
        KIRQL OldIrql;

        if (!Context->Prepared || !Context->ReadySlot)
            return STATUS_SUCCESS;

        Leading = KsPinGetLeadingEdgeStreamPointer(Pin, KSSTREAM_POINTER_STATE_LOCKED);
        if (!Leading)
            return STATUS_PENDING;

        KeAcquireSpinLock(&Context->Lock, &OldIrql);
        Slot = Context->ReadySlot;
        Context->ReadySlot = NULL;
        Context->BusySlot = Slot;
        Discontinuity = Context->Delivered && Context->Discontinuity;
        KeReleaseSpinLock(&Context->Lock, OldIrql);

        if (!Slot)
        {
            KsStreamPointerUnlock(Leading, FALSE);
            return STATUS_SUCCESS;
        }

        if (Slot->Used <= Leading->OffsetOut.Remaining)
        {
            RtlCopyMemory(Leading->OffsetOut.Data, Slot->Data, Slot->Used);
            UvcFillHeaders(Context, Leading, Slot, Discontinuity);

            KeAcquireSpinLock(&Context->Lock, &OldIrql);
            Context->BusySlot = NULL;
            Context->Delivered = TRUE;
            Context->Discontinuity = FALSE;
            KeReleaseSpinLock(&Context->Lock, OldIrql);

            KsStreamPointerAdvanceOffsetsAndUnlock(Leading, 0, Slot->Used, TRUE);
        }
        else
        {
            KeAcquireSpinLock(&Context->Lock, &OldIrql);
            Context->BusySlot = NULL;
            Context->FramesDropped++;
            Context->Discontinuity = TRUE;
            KeReleaseSpinLock(&Context->Lock, OldIrql);

            KsStreamPointerUnlock(Leading, FALSE);
            return STATUS_SUCCESS;
        }
    }
}

static
NTSTATUS
UvcApplyFraming(
    PKSPIN Pin,
    PUVC_PIN Context)
{
    PKSALLOCATOR_FRAMING_EX Framing;
    NTSTATUS Status;

    Status = KsEdit(Pin, &Pin->Descriptor, LNTUVC_TAG);
    if (!NT_SUCCESS(Status))
        return Status;

    Framing = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Framing), LNTUVC_TAG);
    if (!Framing)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Framing, sizeof(*Framing));
    Status = KsAddItemToObjectBag(Pin->Bag, Framing, ExFreePool);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Framing, LNTUVC_TAG);
        return Status;
    }

    Framing->CountItems = 1;
    Framing->OutputCompression.RatioNumerator = 1;
    Framing->OutputCompression.RatioDenominator = 1;
    Framing->FramingItem[0].MemoryType = UVC_KSMEMORY_TYPE_KERNEL_NONPAGED;
    Framing->FramingItem[0].Flags = UVC_FRAMING_FLAGS;
    Framing->FramingItem[0].Frames = UVC_FRAMING_FRAMES;
    Framing->FramingItem[0].PhysicalRange.MinFrameSize = Context->MaxFrameSize;
    Framing->FramingItem[0].PhysicalRange.MaxFrameSize = Context->MaxFrameSize;
    Framing->FramingItem[0].FramingRange.Range.MinFrameSize = Context->MaxFrameSize;
    Framing->FramingItem[0].FramingRange.Range.MaxFrameSize = Context->MaxFrameSize;
    ((PKSPIN_DESCRIPTOR_EX)Pin->Descriptor)->AllocatorFraming = Framing;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcPinCreate(
    PKSPIN Pin,
    PIRP Irp)
{
    PKSFILTER Filter = KsPinGetParentFilter(Pin);
    PKSDEVICE KsDevice = KsFilterGetDevice(Filter);
    PUVC_DEVICE Device = KsDevice->Context;
    PUVC_PIN Context;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);

    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), LNTUVC_TAG);
    if (!Context)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Context, sizeof(*Context));
    Status = KsAddItemToObjectBag(Pin->Bag, Context, ExFreePool);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Context, LNTUVC_TAG);
        return Status;
    }

    Context->Pin = Pin;
    Context->Device = Device;
    KeInitializeSpinLock(&Context->Lock);
    KeInitializeEvent(&Context->Idle, NotificationEvent, TRUE);
    InitializeListHead(&Context->RetryList);

    Status = UvcFindFormat(Device, Pin->ConnectionFormat, &Context->Format, &Context->Frame, &Context->Interval);
    if (!NT_SUCCESS(Status))
        return Status;

    Context->ImageSize = UvcImageSize(Context->Format, Context->Frame);

    Status = UvcNegotiate(Context);
    if (!NT_SUCCESS(Status))
        return Status;

    ExInitializeWorkItem(&Context->RetryItem, UvcRetryRoutine, Context);
    Status = KsRegisterCountedWorker(CriticalWorkQueue, &Context->RetryItem, &Context->RetryWorker);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UvcApplyFraming(Pin, Context);
    if (!NT_SUCCESS(Status))
    {
        KsUnregisterWorker(Context->RetryWorker);
        Context->RetryWorker = NULL;
        return Status;
    }

    Pin->StreamHeaderSize = 0;
    Pin->Context = Context;
    InterlockedIncrement(&Device->CapturePins);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcPinClose(
    PKSPIN Pin,
    PIRP Irp)
{
    PUVC_PIN Context = Pin->Context;

    UNREFERENCED_PARAMETER(Irp);

    if (!Context)
        return STATUS_SUCCESS;

    UvcStopStreaming(Context);
    UvcUnprepare(Context);

    if (Context->RetryWorker)
    {
        KsUnregisterWorker(Context->RetryWorker);
        Context->RetryWorker = NULL;
    }

    InterlockedDecrement(&Context->Device->CapturePins);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
UvcPinSetDataFormat(
    PKSPIN Pin,
    PKSDATAFORMAT OldFormat,
    PKSMULTIPLE_ITEM OldAttributes,
    const KSDATARANGE *DataRange,
    const KSATTRIBUTE_LIST *AttributeRange)
{
    PKSDEVICE KsDevice = KsFilterGetDevice(KsPinGetParentFilter(Pin));
    PUVC_FORMAT Format;
    PUVC_FRAME Frame;
    ULONG Interval;

    UNREFERENCED_PARAMETER(OldFormat);
    UNREFERENCED_PARAMETER(OldAttributes);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(AttributeRange);

    return UvcFindFormat(KsDevice->Context, Pin->ConnectionFormat, &Format, &Frame, &Interval);
}

const KSPIN_DISPATCH UvcPinDispatch =
{
    UvcPinCreate,
    UvcPinClose,
    UvcPinProcess,
    NULL,
    UvcPinSetDataFormat,
    UvcPinSetDeviceState,
    NULL,
    NULL,
    NULL,
    NULL
};
