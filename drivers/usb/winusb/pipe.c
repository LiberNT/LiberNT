/*
 * PROJECT:     LiberNT WinUSB driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bulk and interrupt pipe queues, transfers, policies, cancellation and timeouts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winusb.h"

#define NDEBUG
#include <debug.h>

static IO_COMPLETION_ROUTINE WinUsbTransferComplete;
static KDEFERRED_ROUTINE WinUsbTimerDpc;
static IO_WORKITEM_ROUTINE WinUsbAutoClearWorker;
static DRIVER_CANCEL WinUsbCancelQueued;

VOID
WinUsbResetPipePolicy(
    _Inout_ PWINUSB_PIPE Pipe)
{
    Pipe->ShortPacketTerminate = FALSE;
    Pipe->AutoClearStall = FALSE;
    Pipe->IgnoreShortPackets = FALSE;
    Pipe->AllowPartialReads = TRUE;
    Pipe->AutoFlush = FALSE;
    Pipe->RawIo = FALSE;
    Pipe->ResetPipeOnResume = FALSE;
    Pipe->Timeout = 0;
}

VOID
WinUsbResetPolicies(
    _In_ PWINUSB_DEVICE Device)
{
    ULONG Interface, Pipe;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    for (Interface = 0; Interface < Device->InterfaceCount; Interface++)
    {
        for (Pipe = 0; Pipe < Device->Interfaces[Interface].PipeCount; Pipe++)
            WinUsbResetPipePolicy(&Device->Interfaces[Interface].Pipes[Pipe]);
    }
    Device->ControlTimeout = LNT_WINUSB_DEFAULT_CONTROL_TIMEOUT;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
}

PWINUSB_PIPE
WinUsbFindPipe(
    _In_ PWINUSB_DEVICE Device,
    _In_ UCHAR InterfaceIndex,
    _In_ UCHAR PipeId)
{
    PWINUSB_INTERFACE Interface;
    ULONG Index;

    if (InterfaceIndex >= Device->InterfaceCount)
        return NULL;
    Interface = &Device->Interfaces[InterfaceIndex];
    for (Index = 0; Index < Interface->PipeCount; Index++)
    {
        if (Interface->Pipes[Index].EndpointAddress == PipeId)
            return &Interface->Pipes[Index];
    }
    return NULL;
}

static
NTSTATUS
WinUsbMapStatus(
    _In_ PWINUSB_REQUEST Request,
    _In_ NTSTATUS Status)
{
    USBD_STATUS UsbdStatus = Request->Urb.Bulk.Hdr.Status;

    if (NT_SUCCESS(Status))
        return Status;
    if (Request->TimedOut)
        return STATUS_IO_TIMEOUT;
    if (Status == STATUS_CANCELLED || Status == STATUS_DEVICE_NOT_CONNECTED || Status == STATUS_NO_SUCH_DEVICE)
        return Status;
    if (UsbdStatus == USBD_STATUS_CANCELED)
        return STATUS_CANCELLED;
    if (UsbdStatus == USBD_STATUS_BABBLE_DETECTED || UsbdStatus == USBD_STATUS_DATA_OVERRUN ||
        UsbdStatus == USBD_STATUS_BUFFER_OVERRUN)
    {
        return STATUS_DEVICE_DATA_ERROR;
    }
    return STATUS_UNSUCCESSFUL;
}

static
VOID
WinUsbFreeRequest(
    _In_ PWINUSB_REQUEST Request)
{
    ASSERT(Request->PartialMdl == NULL);
    if (Request->Bounce)
        ExFreePoolWithTag(Request->Bounce, WINUSB_TAG);
    if (Request->WorkItem)
        IoFreeWorkItem(Request->WorkItem);
    ExFreePoolWithTag(Request, WINUSB_TAG);
}

static
VOID
WinUsbStartPipe(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe);

static
VOID
WinUsbCompleteRequest(
    _In_ PWINUSB_REQUEST Request)
{
    PWINUSB_DEVICE Device = Request->Device;
    PWINUSB_PIPE Pipe = Request->Pipe;
    PIRP Irp = Request->Irp;
    KIRQL OldIrql;

    ASSERT(Request->References == 0);
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Request->Stage == WinUsbStageActive)
    {
        if (!IsListEmpty(&Request->Link))
        {
            RemoveEntryList(&Request->Link);
            InitializeListHead(&Request->Link);
        }
        if (Pipe)
        {
            ASSERT(Pipe->Active > 0);
            Pipe->Active--;
        }
        else
        {
            ASSERT(Device->ControlActive > 0);
            Device->ControlActive--;
        }
    }
    Request->Stage = WinUsbStageDone;
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    ASSERT(IoGetCurrentIrpStackLocation(Irp)->MajorFunction == IRP_MJ_DEVICE_CONTROL);
    Irp->IoStatus.Status = Request->Status;
    Irp->IoStatus.Information = Request->Done;
    WinUsbFreeRequest(Request);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    IoReleaseRemoveLock(&Device->RemoveLock, Irp);

    if (Pipe)
        WinUsbStartPipe(Device, Pipe);
}

static
VOID
WinUsbReleaseRequest(
    _In_ PWINUSB_REQUEST Request)
{
    LONG References = InterlockedDecrement(&Request->References);

    ASSERT(References >= 0);
    if (References == 0)
        WinUsbCompleteRequest(Request);
}

static
VOID
WinUsbFinishRequest(
    _In_ PWINUSB_REQUEST Request,
    _In_ NTSTATUS Status)
{
    Request->Status = Status;
    if (Request->TimerArmed && KeCancelTimer(&Request->Timer))
        WinUsbReleaseRequest(Request);
    WinUsbReleaseRequest(Request);
}

static
VOID
NTAPI
WinUsbTimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PWINUSB_REQUEST Request = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ASSERT(Request != NULL);
    ASSERT(Request->References > 0);

    Request->TimedOut = TRUE;
    IoCancelIrp(Request->Irp);
    WinUsbReleaseRequest(Request);
}

static
VOID
WinUsbArmTimer(
    _In_ PWINUSB_REQUEST Request)
{
    LARGE_INTEGER DueTime;

    if (!Request->Timeout || Request->TimerArmed)
        return;
    InterlockedIncrement(&Request->References);
    Request->TimerArmed = TRUE;
    DueTime.QuadPart = -(LONGLONG)Request->Timeout * 10000;
    KeSetTimer(&Request->Timer, DueTime, &Request->TimerDpc);
}

static
PVOID
WinUsbUserAddress(
    _In_ PIRP Irp)
{
    if (!Irp->MdlAddress)
        return NULL;
    return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority | MdlMappingNoExecute);
}

static
VOID
WinUsbSaveExcess(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PUCHAR Saved;
    KIRQL OldIrql;

    Saved = ExAllocatePoolWithTag(NonPagedPool, Length, WINUSB_TAG);
    if (!Saved)
        return;
    RtlCopyMemory(Saved, Data, Length);

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    ASSERT(Pipe->SavedOffset >= Pipe->SavedLength);
    if (Pipe->Saved)
        ExFreePoolWithTag(Pipe->Saved, WINUSB_TAG);
    Pipe->Saved = Saved;
    Pipe->SavedLength = Length;
    Pipe->SavedOffset = 0;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
}

static
ULONG
WinUsbTakeSaved(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    PUCHAR Free = NULL;
    ULONG Copy = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Pipe->Saved && Pipe->SavedOffset < Pipe->SavedLength)
    {
        Copy = min(Length, Pipe->SavedLength - Pipe->SavedOffset);
        RtlCopyMemory(Buffer, Pipe->Saved + Pipe->SavedOffset, Copy);
        Pipe->SavedOffset += Copy;
    }
    if (Pipe->Saved && Pipe->SavedOffset >= Pipe->SavedLength)
    {
        Free = Pipe->Saved;
        Pipe->Saved = NULL;
        Pipe->SavedLength = Pipe->SavedOffset = 0;
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    if (Free)
        ExFreePoolWithTag(Free, WINUSB_TAG);
    return Copy;
}

static
BOOLEAN
WinUsbPrepareChunk(
    _Inout_ PWINUSB_REQUEST Request)
{
    PWINUSB_PIPE Pipe = Request->Pipe;
    struct _URB_BULK_OR_INTERRUPT_TRANSFER *Urb = &Request->Urb.Bulk;
    ULONG MaxPacket = Pipe->MaximumPacketSize;
    ULONG Remaining = Request->Length - Request->Done;
    ULONG Chunk;
    BOOLEAN Bounce = FALSE;

    ASSERT(MaxPacket != 0);
    ASSERT(Request->PartialMdl == NULL);

    if (Request->In)
    {
        if (Request->Length == 0)
        {
            if (Request->ZeroLengthSent)
                return FALSE;
            Request->ZeroLengthSent = TRUE;
            Chunk = 0;
        }
        else if (Request->RawIo)
        {
            if (Request->Done)
                return FALSE;
            Chunk = Request->Length;
        }
        else if (Remaining == 0)
        {
            return FALSE;
        }
        else if (Remaining >= MaxPacket)
        {
            Chunk = min(Remaining - Remaining % MaxPacket, LNT_WINUSB_MAXIMUM_TRANSFER_SIZE);
        }
        else if (Request->TailAllowed || Request->Done % LNT_WINUSB_MAXIMUM_TRANSFER_SIZE == 0 ||
                 Pipe->IgnoreShortPackets)
        {
            Chunk = MaxPacket;
            Bounce = TRUE;
        }
        else
        {
            return FALSE;
        }
    }
    else
    {
        if (Remaining)
        {
            Chunk = min(Remaining, LNT_WINUSB_MAXIMUM_TRANSFER_SIZE);
        }
        else if (!Request->ZeroLengthSent &&
                 (Request->Length == 0 ||
                  (Pipe->ShortPacketTerminate && Request->Length % MaxPacket == 0)))
        {
            Request->ZeroLengthSent = TRUE;
            Chunk = 0;
        }
        else
        {
            return FALSE;
        }
    }

    RtlZeroMemory(Urb, sizeof(*Urb));
    Urb->Hdr.Length = sizeof(*Urb);
    Urb->Hdr.Function = URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER;
    Urb->PipeHandle = Pipe->Handle;
    Urb->TransferFlags = Request->In ? (USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK) : 0;
    Urb->TransferBufferLength = Chunk;
    Request->Chunk = Chunk;
    Request->UsedBounce = Bounce;

    if (Bounce)
    {
        if (!Request->Bounce || Request->BounceLength < Chunk)
        {
            if (Request->Bounce)
                ExFreePoolWithTag(Request->Bounce, WINUSB_TAG);
            Request->Bounce = ExAllocatePoolWithTag(NonPagedPool, Chunk, WINUSB_TAG);
            Request->BounceLength = Request->Bounce ? Chunk : 0;
            if (!Request->Bounce)
                return FALSE;
        }
        Urb->TransferBuffer = Request->Bounce;
    }
    else if (Chunk)
    {
        ASSERT(Request->Irp->MdlAddress != NULL);
        Request->PartialMdl = IoAllocateMdl((PUCHAR)MmGetMdlVirtualAddress(Request->Irp->MdlAddress) + Request->Done,
                                            Chunk, FALSE, FALSE, NULL);
        if (!Request->PartialMdl)
            return FALSE;
        IoBuildPartialMdl(Request->Irp->MdlAddress, Request->PartialMdl,
                          (PUCHAR)MmGetMdlVirtualAddress(Request->Irp->MdlAddress) + Request->Done, Chunk);
        Urb->TransferBufferMDL = Request->PartialMdl;
    }
    return TRUE;
}

static
VOID
WinUsbSubmitChunk(
    _In_ PWINUSB_REQUEST Request)
{
    PIO_STACK_LOCATION IoStack;
    PIRP Irp = Request->Irp;

    ASSERT(Request->Stage == WinUsbStageActive);

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->MinorFunction = 0;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoStack->Parameters.Others.Argument1 = &Request->Urb;
    IoSetCompletionRoutine(Irp, WinUsbTransferComplete, Request, TRUE, TRUE, TRUE);
    IoCallDriver(Request->Device->Lower, Irp);
}

static
VOID
WinUsbContinue(
    _In_ PWINUSB_REQUEST Request)
{
    if (Request->Irp->Cancel)
    {
        WinUsbFinishRequest(Request, Request->TimedOut ? STATUS_IO_TIMEOUT : STATUS_CANCELLED);
        return;
    }
    if (!WinUsbPrepareChunk(Request))
    {
        WinUsbFinishRequest(Request, Request->Overflow ? STATUS_DEVICE_DATA_ERROR : Request->Status);
        return;
    }
    WinUsbSubmitChunk(Request);
}

static
VOID
WinUsbStartRequest(
    _In_ PWINUSB_REQUEST Request)
{
    PUCHAR Buffer;
    ULONG Copied;

    ASSERT(Request->Stage == WinUsbStageActive);

    if (Request->In && !Request->RawIo && Request->Length)
    {
        Buffer = WinUsbUserAddress(Request->Irp);
        if (!Buffer)
        {
            WinUsbFinishRequest(Request, STATUS_INSUFFICIENT_RESOURCES);
            return;
        }
        Copied = WinUsbTakeSaved(Request->Device, Request->Pipe, Buffer, Request->Length);
        if (Copied)
        {
            Request->Done = Copied;
            WinUsbFinishRequest(Request, STATUS_SUCCESS);
            return;
        }
    }

    WinUsbArmTimer(Request);
    WinUsbContinue(Request);
}

static
VOID
NTAPI
WinUsbAutoClearWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PWINUSB_REQUEST Request = Context;
    PWINUSB_DEVICE Device = DeviceObject->DeviceExtension;
    struct _URB_PIPE_REQUEST Urb;
    NTSTATUS Status;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Request != NULL && Request->Pipe != NULL);

    RtlZeroMemory(&Urb, sizeof(Urb));
    Urb.Hdr.Length = sizeof(Urb);
    Urb.Hdr.Function = URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL;
    Urb.PipeHandle = Request->Pipe->Handle;
    Status = WinUsbSubmitUrbSync(Device, (PURB)&Urb);
    if (!NT_SUCCESS(Status))
        DPRINT1("WinUSB: automatic reset of pipe %02x failed: %lx\n", Request->Pipe->EndpointAddress, Status);

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Request->Pipe->Halted = !NT_SUCCESS(Status);
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    WinUsbFinishRequest(Request, Request->Status);
}

static
NTSTATUS
NTAPI
WinUsbTransferComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PWINUSB_REQUEST Request = Context;
    PWINUSB_DEVICE Device = Request->Device;
    PWINUSB_PIPE Pipe = Request->Pipe;
    ULONG Actual = Request->Urb.Bulk.TransferBufferLength;
    ULONG Copy, Excess;
    PUCHAR Buffer;
    NTSTATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(Request->Irp == Irp);
    ASSERT(Request->Stage == WinUsbStageActive);
    ASSERT(Actual <= Request->Chunk);

    Status = Irp->IoStatus.Status;
    if (Request->PartialMdl)
    {
        IoFreeMdl(Request->PartialMdl);
        Request->PartialMdl = NULL;
    }

    if (Request->UsedBounce && Actual)
    {
        Buffer = WinUsbUserAddress(Irp);
        Copy = min(Actual, Request->Length - Request->Done);
        if (Buffer)
            RtlCopyMemory(Buffer + Request->Done, Request->Bounce, Copy);
        Request->Done += Copy;
        Excess = Actual - Copy;
        if (Excess && NT_SUCCESS(Status))
        {
            if (!Pipe->AllowPartialReads)
                Request->Overflow = TRUE;
            else if (!Pipe->AutoFlush)
                WinUsbSaveExcess(Device, Pipe, Request->Bounce + Copy, Excess);
        }
    }
    else
    {
        Request->Done += Actual;
    }

    if (!NT_SUCCESS(Status))
    {
        Status = WinUsbMapStatus(Request, Status);
        DPRINT("WinUSB: pipe %02x transfer failed: %lx, USBD %lx\n", Pipe->EndpointAddress, Status,
               Request->Urb.Bulk.Hdr.Status);
        if ((Status == STATUS_UNSUCCESSFUL || Status == STATUS_DEVICE_DATA_ERROR) &&
            USBD_ERROR(Request->Urb.Bulk.Hdr.Status) &&
            ((ULONG)Request->Urb.Bulk.Hdr.Status & 0x0FFFFFFF) - 1 < 0xFF)
        {
            KeAcquireSpinLock(&Device->Lock, &OldIrql);
            Pipe->Halted = TRUE;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            if (Request->In && Pipe->AutoClearStall)
            {
                Request->Status = Status;
                IoQueueWorkItem(Request->WorkItem, WinUsbAutoClearWorker, DelayedWorkQueue, Request);
                return STATUS_MORE_PROCESSING_REQUIRED;
            }
        }
        WinUsbFinishRequest(Request, Status);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    Request->Status = STATUS_SUCCESS;
    if (Request->Overflow)
    {
        WinUsbFinishRequest(Request, STATUS_DEVICE_DATA_ERROR);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }
    if (Request->In && (Request->RawIo || Request->Chunk == 0 ||
                        (Actual < Request->Chunk && !Pipe->IgnoreShortPackets)))
    {
        WinUsbFinishRequest(Request, STATUS_SUCCESS);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    WinUsbContinue(Request);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
NTAPI
WinUsbCancelQueued(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ _IRQL_uses_cancel_ PIRP Irp)
{
    PWINUSB_DEVICE Device = DeviceObject->DeviceExtension;
    PWINUSB_REQUEST Request = Irp->Tail.Overlay.DriverContext[0];
    KIRQL OldIrql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    ASSERT(Request != NULL && Request->Irp == Irp);

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    ASSERT(Request->Stage == WinUsbStageQueued);
    if (!IsListEmpty(&Request->Link))
    {
        RemoveEntryList(&Request->Link);
        InitializeListHead(&Request->Link);
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    WinUsbFinishRequest(Request, STATUS_CANCELLED);
}

static
VOID
WinUsbStartPipe(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe)
{
    PWINUSB_REQUEST Request;
    KIRQL OldIrql;

    for (;;)
    {
        Request = NULL;
        KeAcquireSpinLock(&Device->Lock, &OldIrql);
        while (!IsListEmpty(&Pipe->Queue) && !Pipe->Halted && !Device->Gone)
        {
            Request = CONTAINING_RECORD(Pipe->Queue.Flink, WINUSB_REQUEST, Link);
            if (!Request->RawIo && Pipe->Active)
            {
                Request = NULL;
                break;
            }
            RemoveEntryList(&Request->Link);
            if (IoSetCancelRoutine(Request->Irp, NULL))
            {
                Request->Stage = WinUsbStageActive;
                Pipe->Active++;
                InsertTailList(&Pipe->ActiveList, &Request->Link);
                break;
            }
            InitializeListHead(&Request->Link);
            Request = NULL;
        }
        KeReleaseSpinLock(&Device->Lock, OldIrql);

        if (!Request)
            return;
        WinUsbStartRequest(Request);
    }
}

static
PWINUSB_REQUEST
WinUsbAllocateRequest(
    _In_ PWINUSB_DEVICE Device,
    _In_ PIRP Irp)
{
    PWINUSB_REQUEST Request;

    Request = ExAllocatePoolZero(NonPagedPool, sizeof(*Request), WINUSB_TAG);
    if (!Request)
        return NULL;
    Request->WorkItem = IoAllocateWorkItem(Device->Self);
    if (!Request->WorkItem)
    {
        ExFreePoolWithTag(Request, WINUSB_TAG);
        return NULL;
    }
    InitializeListHead(&Request->Link);
    Request->Irp = Irp;
    Request->Device = Device;
    Request->FileObject = IoGetCurrentIrpStackLocation(Irp)->FileObject;
    Request->References = 1;
    Request->Status = STATUS_SUCCESS;
    KeInitializeTimer(&Request->Timer);
    KeInitializeDpc(&Request->TimerDpc, WinUsbTimerDpc, Request);
    Irp->Tail.Overlay.DriverContext[0] = Request;
    return Request;
}

static
NTSTATUS
WinUsbFailTransfer(
    _In_ PWINUSB_DEVICE Device,
    _In_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    IoReleaseRemoveLock(&Device->RemoveLock, Irp);
    return Status;
}

NTSTATUS
WinUsbQueueTransfer(
    _In_ PWINUSB_DEVICE Device,
    _In_ PIRP Irp,
    _In_ BOOLEAN In)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PLNT_WINUSB_PIPE_REQUEST Input = Irp->AssociatedIrp.SystemBuffer;
    ULONG Length = IoStack->Parameters.DeviceIoControl.OutputBufferLength;
    PWINUSB_INTERFACE Interface;
    PWINUSB_REQUEST Request;
    PWINUSB_PIPE Pipe;
    BOOLEAN RawIo, Queue;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Input))
        return WinUsbFailTransfer(Device, Irp, STATUS_INVALID_PARAMETER, 0);
    Pipe = WinUsbFindPipe(Device, Input->InterfaceIndex, Input->PipeId);
    if (!Pipe || !!In != !!USB_ENDPOINT_DIRECTION_IN(Pipe->EndpointAddress) ||
        (Length && !Irp->MdlAddress))
    {
        return WinUsbFailTransfer(Device, Irp, STATUS_INVALID_PARAMETER, 0);
    }
    if (Pipe->Type != UsbdPipeTypeBulk && Pipe->Type != UsbdPipeTypeInterrupt)
        return WinUsbFailTransfer(Device, Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    Interface = &Device->Interfaces[Input->InterfaceIndex];

    RawIo = In && Pipe->RawIo;
    if (RawIo && (!Length || Length % Pipe->MaximumPacketSize || Length > LNT_WINUSB_MAXIMUM_TRANSFER_SIZE))
        return WinUsbFailTransfer(Device, Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (In && !RawIo && !Length && Pipe->AllowPartialReads)
        return WinUsbFailTransfer(Device, Irp, STATUS_SUCCESS, 0);

    Request = WinUsbAllocateRequest(Device, Irp);
    if (!Request)
        return WinUsbFailTransfer(Device, Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    Request->Pipe = Pipe;
    Request->Interface = Interface;
    Request->In = In;
    Request->RawIo = RawIo;
    Request->Length = Length;
    Request->Timeout = Pipe->Timeout;
    Request->TailAllowed = Length <= LNT_WINUSB_MAXIMUM_TRANSFER_SIZE;

    IoMarkIrpPending(Irp);
    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Interface->Selecting || Device->Gone)
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        Irp->Tail.Overlay.DriverContext[0] = NULL;
        WinUsbFreeRequest(Request);
        WinUsbFailTransfer(Device, Irp, STATUS_DEVICE_BUSY, 0);
        return STATUS_PENDING;
    }
    Queue = !RawIo || Pipe->Halted || !IsListEmpty(&Pipe->Queue);
    if (Queue)
    {
        Request->Stage = WinUsbStageQueued;
        IoSetCancelRoutine(Irp, WinUsbCancelQueued);
        if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
        {
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            WinUsbFinishRequest(Request, STATUS_CANCELLED);
            return STATUS_PENDING;
        }
        InsertTailList(&Pipe->Queue, &Request->Link);
    }
    else
    {
        Request->Stage = WinUsbStageActive;
        Pipe->Active++;
        InsertTailList(&Pipe->ActiveList, &Request->Link);
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    if (Queue)
        WinUsbStartPipe(Device, Pipe);
    else
        WinUsbStartRequest(Request);
    return STATUS_PENDING;
}

static
VOID
WinUsbTakeQueued(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe,
    _In_opt_ PFILE_OBJECT FileObject,
    _Inout_ PLIST_ENTRY Taken)
{
    PLIST_ENTRY Entry, Next;
    PWINUSB_REQUEST Request;

    UNREFERENCED_PARAMETER(Device);

    for (Entry = Pipe->Queue.Flink; Entry != &Pipe->Queue; Entry = Next)
    {
        Next = Entry->Flink;
        Request = CONTAINING_RECORD(Entry, WINUSB_REQUEST, Link);
        if (FileObject && Request->FileObject != FileObject)
            continue;
        if (!IoSetCancelRoutine(Request->Irp, NULL))
            continue;
        RemoveEntryList(&Request->Link);
        InsertTailList(Taken, &Request->Link);
    }
}

static
VOID
WinUsbCancelActive(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe,
    _In_opt_ PFILE_OBJECT FileObject)
{
    PWINUSB_REQUEST Targets[16];
    PLIST_ENTRY Entry;
    PWINUSB_REQUEST Request;
    ULONG Count, Index;
    KIRQL OldIrql;

    do
    {
        Count = 0;
        KeAcquireSpinLock(&Device->Lock, &OldIrql);
        for (Entry = Pipe->ActiveList.Flink; Entry != &Pipe->ActiveList && Count < RTL_NUMBER_OF(Targets);
             Entry = Entry->Flink)
        {
            Request = CONTAINING_RECORD(Entry, WINUSB_REQUEST, Link);
            if ((FileObject && Request->FileObject != FileObject) || Request->Irp->Cancel)
                continue;
            InterlockedIncrement(&Request->References);
            Targets[Count++] = Request;
        }
        KeReleaseSpinLock(&Device->Lock, OldIrql);

        for (Index = 0; Index < Count; Index++)
        {
            IoCancelIrp(Targets[Index]->Irp);
            WinUsbReleaseRequest(Targets[Index]);
        }
    } while (Count == RTL_NUMBER_OF(Targets));
}

static
VOID
WinUsbCompleteTaken(
    _Inout_ PLIST_ENTRY Taken,
    _In_ NTSTATUS Status)
{
    PWINUSB_REQUEST Request;
    PLIST_ENTRY Entry;

    while (!IsListEmpty(Taken))
    {
        Entry = RemoveHeadList(Taken);
        Request = CONTAINING_RECORD(Entry, WINUSB_REQUEST, Link);
        InitializeListHead(&Request->Link);
        WinUsbFinishRequest(Request, Status);
    }
}

VOID
WinUsbCancelFileRequests(
    _In_ PWINUSB_DEVICE Device,
    _In_opt_ PFILE_OBJECT FileObject)
{
    LIST_ENTRY Taken;
    ULONG Interface, Pipe;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    InitializeListHead(&Taken);
    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    for (Interface = 0; Interface < Device->InterfaceCount; Interface++)
    {
        for (Pipe = 0; Pipe < Device->Interfaces[Interface].PipeCount; Pipe++)
            WinUsbTakeQueued(Device, &Device->Interfaces[Interface].Pipes[Pipe], FileObject, &Taken);
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    WinUsbCompleteTaken(&Taken, STATUS_CANCELLED);

    for (Interface = 0; Interface < Device->InterfaceCount; Interface++)
    {
        for (Pipe = 0; Pipe < Device->Interfaces[Interface].PipeCount; Pipe++)
            WinUsbCancelActive(Device, &Device->Interfaces[Interface].Pipes[Pipe], FileObject);
    }
}

NTSTATUS
WinUsbAbortPipe(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe)
{
    struct _URB_PIPE_REQUEST Urb;
    LIST_ENTRY Taken;
    KIRQL OldIrql;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    InitializeListHead(&Taken);
    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    WinUsbTakeQueued(Device, Pipe, NULL, &Taken);
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    WinUsbCompleteTaken(&Taken, STATUS_CANCELLED);

    RtlZeroMemory(&Urb, sizeof(Urb));
    Urb.Hdr.Length = sizeof(Urb);
    Urb.Hdr.Function = URB_FUNCTION_ABORT_PIPE;
    Urb.PipeHandle = Pipe->Handle;
    Status = WinUsbSubmitUrbSync(Device, (PURB)&Urb);
    if (!NT_SUCCESS(Status))
        DPRINT1("WinUSB: abort of pipe %02x failed: %lx\n", Pipe->EndpointAddress, Status);
    WinUsbCancelActive(Device, Pipe, NULL);
    return Device->Gone ? STATUS_SUCCESS : Status;
}

NTSTATUS
WinUsbResetPipe(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe)
{
    struct _URB_PIPE_REQUEST Urb;
    NTSTATUS Status;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    RtlZeroMemory(&Urb, sizeof(Urb));
    Urb.Hdr.Length = sizeof(Urb);
    Urb.Hdr.Function = URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL;
    Urb.PipeHandle = Pipe->Handle;
    Status = WinUsbSubmitUrbSync(Device, (PURB)&Urb);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WinUSB: reset of pipe %02x failed: %lx\n", Pipe->EndpointAddress, Status);
        return Status;
    }

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Pipe->Halted = FALSE;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    WinUsbStartPipe(Device, Pipe);
    return STATUS_SUCCESS;
}

VOID
WinUsbFlushPipe(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_PIPE Pipe)
{
    PUCHAR Saved;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Saved = Pipe->Saved;
    Pipe->Saved = NULL;
    Pipe->SavedLength = Pipe->SavedOffset = 0;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    if (Saved)
        ExFreePoolWithTag(Saved, WINUSB_TAG);
}

BOOLEAN
WinUsbInterfaceBusy(
    _In_ PWINUSB_DEVICE Device,
    _In_ PWINUSB_INTERFACE Interface)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(Device);

    for (Index = 0; Index < Interface->PipeCount; Index++)
    {
        if (Interface->Pipes[Index].Active || !IsListEmpty(&Interface->Pipes[Index].Queue))
            return TRUE;
    }
    return FALSE;
}

static
NTSTATUS
NTAPI
WinUsbControlComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PWINUSB_REQUEST Request = Context;
    NTSTATUS Status = Irp->IoStatus.Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(Request->Irp == Irp);
    ASSERT(Request->Control);

    Request->Done = Request->Urb.Control.TransferBufferLength;
    WinUsbFinishRequest(Request, WinUsbMapStatus(Request, Status));
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
WinUsbControlTransfer(
    _In_ PWINUSB_DEVICE Device,
    _In_ PIRP Irp,
    _In_ BOOLEAN In)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PLNT_WINUSB_CONTROL_REQUEST Input = Irp->AssociatedIrp.SystemBuffer;
    ULONG Length = IoStack->Parameters.DeviceIoControl.OutputBufferLength;
    struct _URB_CONTROL_TRANSFER *Urb;
    PWINUSB_REQUEST Request;
    USHORT Index, SetupLength;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Input) ||
        Input->InterfaceIndex >= Device->InterfaceCount || Length > LNT_WINUSB_MAX_CONTROL_LENGTH ||
        (Length && !Irp->MdlAddress) || !!In != !!(Input->SetupPacket[0] & 0x80))
    {
        return WinUsbFailTransfer(Device, Irp, STATUS_INVALID_PARAMETER, 0);
    }

    Request = WinUsbAllocateRequest(Device, Irp);
    if (!Request)
        return WinUsbFailTransfer(Device, Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    Request->Control = TRUE;
    Request->In = In;
    Request->Interface = &Device->Interfaces[Input->InterfaceIndex];
    Request->Timeout = Device->ControlTimeout;

    RtlCopyMemory(&SetupLength, &Input->SetupPacket[6], sizeof(SetupLength));
    if (SetupLength > Length)
    {
        SetupLength = (USHORT)Length;
        RtlCopyMemory(&Input->SetupPacket[6], &SetupLength, sizeof(SetupLength));
    }
    if ((Input->SetupPacket[0] & 0x1F) == BMREQUEST_TO_INTERFACE)
    {
        RtlCopyMemory(&Index, &Input->SetupPacket[4], sizeof(Index));
        Index = (Index & 0xFF00) | Request->Interface->Number;
        RtlCopyMemory(&Input->SetupPacket[4], &Index, sizeof(Index));
    }

    Urb = &Request->Urb.Control;
    Urb->Hdr.Length = sizeof(*Urb);
    Urb->Hdr.Function = URB_FUNCTION_CONTROL_TRANSFER;
    Urb->TransferFlags = USBD_DEFAULT_PIPE_TRANSFER | (In ? (USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK) : 0);
    Urb->TransferBufferLength = min(Length, SetupLength);
    Urb->TransferBufferMDL = Urb->TransferBufferLength ? Irp->MdlAddress : NULL;
    RtlCopyMemory(Urb->SetupPacket, Input->SetupPacket, sizeof(Urb->SetupPacket));
    Request->Length = Urb->TransferBufferLength;
    Request->Chunk = Urb->TransferBufferLength;

    IoMarkIrpPending(Irp);
    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Request->Stage = WinUsbStageActive;
    Device->ControlActive++;
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    WinUsbArmTimer(Request);
    if (Irp->Cancel)
    {
        WinUsbFinishRequest(Request, STATUS_CANCELLED);
        return STATUS_PENDING;
    }
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->MinorFunction = 0;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoStack->Parameters.Others.Argument1 = Urb;
    IoSetCompletionRoutine(Irp, WinUsbControlComplete, Request, TRUE, TRUE, TRUE);
    IoCallDriver(Device->Lower, Irp);
    return STATUS_PENDING;
}
