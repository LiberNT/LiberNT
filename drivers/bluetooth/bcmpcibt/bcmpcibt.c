/*
 * PROJECT:     LiberNT Broadcom PCIe Bluetooth transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Broadcom BCM4378/4387/4388 PCIe Bluetooth transport bus driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "bcmpcibt.h"

#define NDEBUG
#include <debug.h>

DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DRIVER_DEVICE_ADD BcmBtEvtDeviceAdd;
static EVT_WDF_OBJECT_CONTEXT_CLEANUP BcmBtEvtDeviceCleanup;
static EVT_WDF_DEVICE_PREPARE_HARDWARE BcmBtEvtPrepareHardware;
static EVT_WDF_DEVICE_RELEASE_HARDWARE BcmBtEvtReleaseHardware;
static EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED BcmBtEvtD0EntryPostInterruptsEnabled;
static EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED BcmBtEvtD0ExitPreInterruptsDisabled;
static EVT_WDF_INTERRUPT_ISR BcmBtEvtIsr;
static EVT_WDF_INTERRUPT_DPC BcmBtEvtDpc;
static EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL BcmBtEvtPdoIoctl;

VOID
BcmBtSleep(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000LL * Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

ULONG
BcmBtRead0(
    _In_ PBCMBT_DEVICE Dev,
    _In_ ULONG Offset)
{
    if (!Dev->Bar0 || Offset + sizeof(ULONG) > Dev->Bar0Length)
        return MAXULONG;
    return READ_REGISTER_ULONG((volatile ULONG *)(Dev->Bar0 + Offset));
}

VOID
BcmBtWrite0(
    _In_ PBCMBT_DEVICE Dev,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    if (Dev->Bar0 && Offset + sizeof(ULONG) <= Dev->Bar0Length)
        WRITE_REGISTER_ULONG((volatile ULONG *)(Dev->Bar0 + Offset), Value);
}

ULONG
BcmBtRead2(
    _In_ PBCMBT_DEVICE Dev,
    _In_ ULONG Offset)
{
    Offset += Dev->Chip->Bar2Offset;
    if (!Dev->Bar2 || Offset + sizeof(ULONG) > Dev->Bar2Length)
        return MAXULONG;
    return READ_REGISTER_ULONG((volatile ULONG *)(Dev->Bar2 + Offset));
}

VOID
BcmBtWrite2(
    _In_ PBCMBT_DEVICE Dev,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    Offset += Dev->Chip->Bar2Offset;
    if (Dev->Bar2 && Offset + sizeof(ULONG) <= Dev->Bar2Length)
        WRITE_REGISTER_ULONG((volatile ULONG *)(Dev->Bar2 + Offset), Value);
}

static
NTSTATUS
BcmBtWriteConfig(
    _In_ PBCMBT_DEVICE Dev,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    return Dev->Bus.SetBusData(Dev->Bus.Context, PCI_WHICHSPACE_CONFIG, &Value, Offset, sizeof(Value)) == sizeof(Value) ?
           STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
}

static
NTSTATUS
BcmBtInitConfig(
    _In_ PBCMBT_DEVICE Dev)
{
    ULONG Control = 0;
    NTSTATUS Status;

    Status = BcmBtWriteConfig(Dev, BCMBT_PCI_BAR0_WINDOW1, Dev->Chip->Bar0Window1);
    if (NT_SUCCESS(Status))
        Status = BcmBtWriteConfig(Dev, BCMBT_PCI_BAR0_WINDOW2, Dev->Chip->Bar0Window2);
    if (NT_SUCCESS(Status))
        Status = BcmBtWriteConfig(Dev, BCMBT_PCI_BAR0_CORE2_WINDOW1, BCMBT_CORE2_WINDOW1_DEFAULT);
    if (NT_SUCCESS(Status))
        Status = BcmBtWriteConfig(Dev, BCMBT_PCI_BAR0_CORE2_WINDOW2, Dev->Chip->Bar0Core2Window2);
    if (NT_SUCCESS(Status))
        Status = BcmBtWriteConfig(Dev, BCMBT_PCI_BAR2_WINDOW, BCMBT_BAR2_WINDOW_DEFAULT);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Dev->Bus.GetBusData(Dev->Bus.Context, PCI_WHICHSPACE_CONFIG, &Control, BCMBT_PCI_SUBSYSTEM_CONTROL,
                            sizeof(Control)) != sizeof(Control))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }
    if (Dev->Chip->ClearSubsystemBit19)
        Control &= ~(1UL << 19);
    Control |= 1UL << 16;
    return BcmBtWriteConfig(Dev, BCMBT_PCI_SUBSYSTEM_CONTROL, Control);
}

NTSTATUS
BcmBtResetFunction(
    _In_ PBCMBT_DEVICE Dev)
{
    NTSTATUS Status;

    if (!Dev->HaveReset)
    {
        DPRINT1("BCMBT: the bus offers no function-level reset\n");
        return STATUS_DEVICE_NOT_READY;
    }
    Status = Dev->Reset.DeviceReset(Dev->Reset.Context, FunctionLevelDeviceReset, 0, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BCMBT: function-level reset failed: %lx\n", Status);
        return Status;
    }
    BcmBtSleep(BCMBT_RESET_SETTLE_MS);
    Dev->Booted = FALSE;
    Dev->SetupDone = FALSE;
    Status = BcmBtInitConfig(Dev);
    BcmBtUpdateState(Dev);
    DPRINT1("BCMBT: function reset: bootstage %ld, RTI %ld: %lx\n", Dev->Bootstage, Dev->RtiStatus, Status);
    if (NT_SUCCESS(Status) && (Dev->Bootstage != 0 || Dev->RtiStatus != 0))
        Status = STATUS_DEVICE_NOT_READY;
    return Status;
}

static
ULONG64
BcmBtBarAddress(
    _In_ const PCI_COMMON_HEADER *Header,
    _In_ ULONG Index)
{
    ULONG Low = Header->u.type0.BaseAddresses[Index];
    ULONG64 Address = Low & ~0xFUL;

    if ((Low & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT && Index + 1 < PCI_TYPE0_ADDRESSES)
        Address |= (ULONG64)Header->u.type0.BaseAddresses[Index + 1] << 32;
    return Address;
}

static
ULONG64
BcmBtResourceLength(
    _In_ const CM_PARTIAL_RESOURCE_DESCRIPTOR *Resource)
{
    if (Resource->Type == CmResourceTypeMemory)
        return Resource->u.Memory.Length;
    if (Resource->Flags & CM_RESOURCE_MEMORY_LARGE_40)
        return (ULONG64)Resource->u.Memory40.Length40 << 8;
    if (Resource->Flags & CM_RESOURCE_MEMORY_LARGE_48)
        return (ULONG64)Resource->u.Memory48.Length48 << 16;
    if (Resource->Flags & CM_RESOURCE_MEMORY_LARGE_64)
        return (ULONG64)Resource->u.Memory64.Length64 << 32;
    return 0;
}

static
NTSTATUS
NTAPI
BcmBtEvtPrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PBCMBT_DEVICE Dev = BcmBtGetDevice(Device);
    PCI_COMMON_HEADER Header;
    ULONG64 Bar0Bus, Bar2Bus;
    BOOLEAN HaveMessage = FALSE;
    ULONG Index;
    NTSTATUS Status;

    if (!Dev->HaveBus)
    {
        Status = WdfFdoQueryForInterface(Device, &GUID_BUS_INTERFACE_STANDARD, (PINTERFACE)&Dev->Bus,
                                         sizeof(Dev->Bus), 1, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        Dev->HaveBus = TRUE;
    }
    if (!Dev->HaveReset &&
        NT_SUCCESS(WdfFdoQueryForInterface(Device, &GUID_DEVICE_RESET_INTERFACE_STANDARD, (PINTERFACE)&Dev->Reset,
                                           sizeof(Dev->Reset), DEVICE_RESET_INTERFACE_VERSION, NULL)))
    {
        Dev->HaveReset = TRUE;
        if (!(Dev->Reset.SupportedResetTypes & (1UL << FunctionLevelDeviceReset)))
            DPRINT1("BCMBT: the function does not support a function-level reset\n");
    }
    RtlZeroMemory(&Header, sizeof(Header));
    if (Dev->Bus.GetBusData(Dev->Bus.Context, PCI_WHICHSPACE_CONFIG, &Header, 0, sizeof(Header)) != sizeof(Header))
        return STATUS_DEVICE_DATA_ERROR;
    Dev->Chip = Header.VendorID == 0x14e4 ? BcmBtFindChip(Header.DeviceID) : NULL;
    if (!Dev->Chip)
    {
        DPRINT1("BCMBT: %04x:%04x is not a supported controller\n", Header.VendorID, Header.DeviceID);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Status = BcmBtInitConfig(Dev);
    if (!NT_SUCCESS(Status))
        return Status;

    Bar0Bus = BcmBtBarAddress(&Header, 0);
    Bar2Bus = BcmBtBarAddress(&Header, 2);
    for (Index = 0; Index < WdfCmResourceListGetCount(ResourcesTranslated); Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Raw = WdfCmResourceListGetDescriptor(ResourcesRaw, Index);
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Translated = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);
        ULONG64 Length;

        if (!Raw || !Translated)
            continue;
        if (Raw->Type == CmResourceTypeInterrupt && (Raw->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            HaveMessage = TRUE;
        if (Translated->Type != CmResourceTypeMemory && Translated->Type != CmResourceTypeMemoryLarge)
            continue;
        Length = BcmBtResourceLength(Translated);
        if (!Length || Length > MAXULONG)
            continue;
        if ((ULONG64)Raw->u.Memory.Start.QuadPart == Bar0Bus && !Dev->Bar0)
        {
            Dev->Bar0 = MmMapIoSpace(Translated->u.Memory.Start, (SIZE_T)Length, MmNonCached);
            Dev->Bar0Length = (ULONG)Length;
        }
        else if ((ULONG64)Raw->u.Memory.Start.QuadPart == Bar2Bus && !Dev->Bar2)
        {
            Dev->Bar2 = MmMapIoSpace(Translated->u.Memory.Start, (SIZE_T)Length, MmNonCached);
            Dev->Bar2Length = (ULONG)Length;
        }
    }
    DPRINT1("BCMBT: BCM%04x BAR0 %I64x+%lx BAR2 %I64x+%lx, %s interrupt\n", Dev->Chip->ChipId, Bar0Bus,
            Dev->Bar0Length, Bar2Bus, Dev->Bar2Length, HaveMessage ? "message-signaled" : "line-based");
    if (!Dev->Bar0 || !Dev->Bar2 || !HaveMessage)
    {
        DPRINT1("BCMBT: the function lacks %s\n", HaveMessage ? "a BAR mapping" : "a message-signaled interrupt");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Status = BcmBtReadOtp(Dev);
    if (NT_SUCCESS(Status))
        Status = BcmBtReadPlatform(Dev);
    if (NT_SUCCESS(Status))
        Status = BcmBtAllocateShared(Dev);
    if (!NT_SUCCESS(Status))
        DPRINT1("BCMBT: controller start failed: %lx\n", Status);
    return Status;
}

static
NTSTATUS
NTAPI
BcmBtEvtReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PBCMBT_DEVICE Dev = BcmBtGetDevice(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    if (Dev->Bar0)
        MmUnmapIoSpace(Dev->Bar0, Dev->Bar0Length);
    if (Dev->Bar2)
        MmUnmapIoSpace(Dev->Bar2, Dev->Bar2Length);
    Dev->Bar0 = NULL;
    Dev->Bar2 = NULL;
    Dev->Bar0Length = 0;
    Dev->Bar2Length = 0;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
BcmBtThread(
    _In_ PVOID Context)
{
    PBCMBT_DEVICE Dev = Context;
    NTSTATUS Status;

    Status = BcmBtBringUp(Dev);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("BCMBT: controller bring-up failed: %lx\n", Status);
        PsTerminateSystemThread(Status);
    }
    BcmBtFlushPackets(Dev);
    Dev->Ready = TRUE;
    if (!Dev->Child && !NT_SUCCESS(BcmBtCreatePdo(Dev)))
        DPRINT1("BCMBT: radio PDO not created\n");
    BcmBtDeliverPackets(Dev);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NTSTATUS
NTAPI
BcmBtEvtD0EntryPostInterruptsEnabled(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PBCMBT_DEVICE Dev = BcmBtGetDevice(Device);
    HANDLE Thread;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(PreviousState);
    Dev->StopThread = FALSE;
    {
        LARGE_INTEGER Due;

        /* FIXME: the rings are polled because the controller's interrupts never arrive on all-core boots */
        Due.QuadPart = -10000LL * BCMBT_POLL_INTERVAL_MS;
        KeSetTimerEx(&Dev->PollTimer, Due, BCMBT_POLL_INTERVAL_MS, &Dev->PollDpc);
    }
    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, BcmBtThread, Dev);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ObReferenceObjectByHandle(Thread, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Dev->Thread, NULL);
    ZwClose(Thread);
    return Status;
}

static
NTSTATUS
NTAPI
BcmBtEvtD0ExitPreInterruptsDisabled(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PBCMBT_DEVICE Dev = BcmBtGetDevice(Device);

    Dev->Ready = FALSE;
    Dev->StopThread = TRUE;
    KeSetEvent(&Dev->StateEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Dev->RxEvent, IO_NO_INCREMENT, FALSE);
    if (Dev->Thread)
    {
        KeWaitForSingleObject(Dev->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Dev->Thread);
        Dev->Thread = NULL;
    }
    KeCancelTimer(&Dev->PollTimer);
    KeFlushQueuedDpcs();
    if (Dev->Booted && TargetState == WdfPowerDeviceD3Final)
    {
        BcmBtDestroyRings(Dev);
        Dev->Booted = FALSE;
        Dev->SetupDone = FALSE;
    }
    else if (Dev->Booted)
    {
        BcmBtWrite0(Dev, BCMBT_BAR0_SLEEP_CONTROL, BCMBT_SLEEP_QUIESCE);
    }
    BcmBtFlushPackets(Dev);
    return STATUS_SUCCESS;
}

static
BOOLEAN
NTAPI
BcmBtEvtIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG MessageID)
{
    UNREFERENCED_PARAMETER(MessageID);
    WdfInterruptQueueDpcForIsr(Interrupt);
    return TRUE;
}

static
VOID
BcmBtService(
    _In_ PBCMBT_DEVICE Dev)
{
    if (BcmBtUpdateState(Dev))
    {
        if (Dev->RtiStatus > 2)
            DPRINT1("BCMBT: RTI status %ld\n", Dev->RtiStatus);
        KeSetEvent(&Dev->StateEvent, IO_NO_INCREMENT, FALSE);
    }
    BcmBtPollCompletions(Dev);
    if (Dev->Ready)
        BcmBtDeliverPackets(Dev);
}

static
VOID
NTAPI
BcmBtEvtDpc(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFOBJECT AssociatedObject)
{
    UNREFERENCED_PARAMETER(AssociatedObject);
    BcmBtService(BcmBtGetDevice(WdfInterruptGetDevice(Interrupt)));
}

static
VOID
NTAPI
BcmBtPollDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    BcmBtService((PBCMBT_DEVICE)Context);
}

VOID
BcmBtQueuePacket(
    _In_ PBCMBT_DEVICE Dev,
    _In_ UCHAR Type,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PBCMBT_PACKET Packet;
    KIRQL OldIrql;

    Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(BCMBT_PACKET, Data) + max(Length, 1), BCMBT_TAG);
    if (!Packet)
    {
        DPRINT1("BCMBT: %lu-byte packet dropped, out of memory\n", Length);
        return;
    }
    Packet->Type = Type;
    Packet->Length = Length;
    RtlCopyMemory(Packet->Data, Data, Length);
    if (Type == HciPacketEvent)
        BcmBtFixupEvent(Packet->Data, Length);

    KeAcquireSpinLock(&Dev->PacketLock, &OldIrql);
    if (Dev->RxPacketCount >= BCMBT_MAX_QUEUED_PACKETS)
    {
        KeReleaseSpinLock(&Dev->PacketLock, OldIrql);
        ExFreePoolWithTag(Packet, BCMBT_TAG);
        DPRINT1("BCMBT: receive queue full, packet type %u dropped\n", Type);
        return;
    }
    InsertTailList(&Dev->RxPackets, &Packet->Link);
    Dev->RxPacketCount++;
    KeReleaseSpinLock(&Dev->PacketLock, OldIrql);
    KeSetEvent(&Dev->RxEvent, IO_NO_INCREMENT, FALSE);
}

PBCMBT_PACKET
BcmBtTakePacket(
    _In_ PBCMBT_DEVICE Dev)
{
    PBCMBT_PACKET Packet = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Dev->PacketLock, &OldIrql);
    if (!IsListEmpty(&Dev->RxPackets))
    {
        Packet = CONTAINING_RECORD(RemoveHeadList(&Dev->RxPackets), BCMBT_PACKET, Link);
        Dev->RxPacketCount--;
    }
    KeReleaseSpinLock(&Dev->PacketLock, OldIrql);
    return Packet;
}

static
VOID
BcmBtDeliverQueued(
    _In_ PBCMBT_DEVICE Dev)
{
    for (;;)
    {
        PBTHX_HCI_READ_WRITE_CONTEXT Context;
        PBCMBT_PACKET Packet;
        WDFREQUEST Request;
        WDFMEMORY Memory;
        WDFQUEUE Queue;
        KIRQL OldIrql;
        size_t Size;

        KeAcquireSpinLock(&Dev->PacketLock, &OldIrql);
        Packet = IsListEmpty(&Dev->RxPackets) ? NULL : CONTAINING_RECORD(Dev->RxPackets.Flink, BCMBT_PACKET, Link);
        KeReleaseSpinLock(&Dev->PacketLock, OldIrql);
        if (!Packet)
            return;
        Queue = Packet->Type == HciPacketEvent ? Dev->EventQueue : Dev->AclQueue;
        if (!Queue || !NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Queue, &Request)))
            return;

        Packet = BcmBtTakePacket(Dev);
        if (!Packet)
        {
            if (!NT_SUCCESS(WdfRequestRequeue(Request)))
                WdfRequestComplete(Request, STATUS_CANCELLED);
            return;
        }
        if (!NT_SUCCESS(WdfRequestRetrieveOutputMemory(Request, &Memory)))
        {
            WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        }
        else
        {
            Context = WdfMemoryGetBuffer(Memory, &Size);
            if (Size < FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) + Packet->Length)
            {
                WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
            }
            else
            {
                Context->DataLen = Packet->Length;
                Context->Type = Packet->Type;
                RtlCopyMemory(Context->Data, Packet->Data, Packet->Length);
                WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS,
                                                  FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) + Packet->Length);
            }
        }
        ExFreePoolWithTag(Packet, BCMBT_TAG);
    }
}

VOID
BcmBtDeliverPackets(
    _In_ PBCMBT_DEVICE Dev)
{
    if (InterlockedIncrement(&Dev->DeliverPending) != 1)
        return;
    do
    {
        InterlockedExchange(&Dev->DeliverPending, 1);
        BcmBtDeliverQueued(Dev);
    } while (InterlockedDecrement(&Dev->DeliverPending) != 0);
}

VOID
BcmBtFlushPackets(
    _In_ PBCMBT_DEVICE Dev)
{
    PBCMBT_PACKET Packet;

    while ((Packet = BcmBtTakePacket(Dev)) != NULL)
        ExFreePoolWithTag(Packet, BCMBT_TAG);
}

static
NTSTATUS
BcmBtWriteHci(
    _In_ PBCMBT_DEVICE Dev,
    _In_ WDFREQUEST Request)
{
    PBTHX_HCI_READ_WRITE_CONTEXT Context;
    WDFMEMORY Input, Output;
    size_t InSize, OutSize;
    PVOID OutBuffer;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputMemory(Request, &Input);
    if (!NT_SUCCESS(Status))
        return Status;
    Context = WdfMemoryGetBuffer(Input, &InSize);
    if (InSize < FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) ||
        Context->DataLen > InSize - FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) ||
        (Context->Type != HciPacketCommand && Context->Type != HciPacketAclData))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Dev->Ready)
        return STATUS_DEVICE_NOT_READY;
    Status = BcmBtEnqueue(Dev, Context->Type == HciPacketCommand ? BCMBT_XFER_HCI_H2D : BCMBT_XFER_ACL_H2D,
                          Context->Data, Context->DataLen, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!NT_SUCCESS(WdfRequestRetrieveOutputMemory(Request, &Output)))
        return STATUS_SUCCESS;
    OutBuffer = WdfMemoryGetBuffer(Output, &OutSize);
    if (OutSize < sizeof(ULONG))
        return STATUS_SUCCESS;
    *(PULONG)OutBuffer = Context->DataLen;
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(ULONG));
    return STATUS_PENDING;
}

static
VOID
NTAPI
BcmBtEvtPdoIoctl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    PBCMBT_DEVICE Dev = BcmBtGetPdo(WdfIoQueueGetDevice(Queue))->Parent;
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
            ((PBTHX_CAPABILITIES)OutBuffer)->MaxAclTransferInSize = BCMBT_ACL_PAYLOAD_SIZE;
            ((PBTHX_CAPABILITIES)OutBuffer)->ScoSupport = ScoSupportNone;
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(BTHX_CAPABILITIES));
            return;

        case IOCTL_BTHX_WRITE_HCI:
            Status = BcmBtWriteHci(Dev, Request);
            if (Status == STATUS_PENDING)
                return;
            break;

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
            Status = WdfRequestForwardToIoQueue(Request, Type == HciPacketEvent ? Dev->EventQueue : Dev->AclQueue);
            if (!NT_SUCCESS(Status))
                break;
            if (Dev->Ready)
                BcmBtDeliverPackets(Dev);
            return;
        }

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    WdfRequestComplete(Request, Status);
}

NTSTATUS
BcmBtCreatePdo(
    _In_ PBCMBT_DEVICE Dev)
{
    DECLARE_CONST_UNICODE_STRING(CompatibleId, BCMBT_PDO_COMPATIBLE_ID);
    DECLARE_CONST_UNICODE_STRING(InstanceId, L"00");
    DECLARE_UNICODE_STRING_SIZE(HardwareId, 32);
    DECLARE_UNICODE_STRING_SIZE(Description, 64);
    WDF_IO_QUEUE_CONFIG QueueConfig;
    WDF_OBJECT_ATTRIBUTES Attributes;
    PWDFDEVICE_INIT Init;
    WDFDEVICE Child;
    WDFQUEUE Queue;
    NTSTATUS Status;

    Status = RtlUnicodeStringPrintf(&HardwareId, L"BCMPCIBT\\BCM%04X", Dev->Chip->ChipId);
    if (NT_SUCCESS(Status))
        Status = RtlUnicodeStringPrintf(&Description, L"Broadcom BCM%04X Bluetooth Radio", Dev->Chip->ChipId);
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

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BCMBT_PDO);
    Status = WdfDeviceCreate(&Init, &Attributes, &Child);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }
    BcmBtGetPdo(Child)->Parent = Dev;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
    QueueConfig.EvtIoDeviceControl = BcmBtEvtPdoIoctl;
    QueueConfig.EvtIoInternalDeviceControl = BcmBtEvtPdoIoctl;
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
        Dev->EventQueue = NULL;
        Dev->AclQueue = NULL;
        WdfObjectDelete(Child);
        return Status;
    }
    Dev->Child = Child;
    IoInvalidateDeviceRelations(WdfDeviceWdmGetPhysicalDevice(Dev->Device), BusRelations);
    DPRINT1("BCMBT: radio ready\n");
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
BcmBtEvtDeviceCleanup(
    _In_ WDFOBJECT Object)
{
    PBCMBT_DEVICE Dev = BcmBtGetDevice(Object);

    BcmBtFlushPackets(Dev);
    BcmBtFreeShared(Dev);
    if (Dev->HaveTree)
        FdtDriverClose(&Dev->Tree);
    Dev->HaveTree = FALSE;
    if (Dev->HaveReset && Dev->Reset.InterfaceDereference)
        Dev->Reset.InterfaceDereference(Dev->Reset.Context);
    Dev->HaveReset = FALSE;
    if (Dev->HaveBus && Dev->Bus.InterfaceDereference)
        Dev->Bus.InterfaceDereference(Dev->Bus.Context);
    Dev->HaveBus = FALSE;
}

static
NTSTATUS
NTAPI
BcmBtEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS Callbacks;
    WDF_INTERRUPT_CONFIG InterruptConfig;
    WDF_OBJECT_ATTRIBUTES Attributes;
    PBCMBT_DEVICE Dev;
    WDFDEVICE Device;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&Callbacks);
    Callbacks.EvtDevicePrepareHardware = BcmBtEvtPrepareHardware;
    Callbacks.EvtDeviceReleaseHardware = BcmBtEvtReleaseHardware;
    Callbacks.EvtDeviceD0EntryPostInterruptsEnabled = BcmBtEvtD0EntryPostInterruptsEnabled;
    Callbacks.EvtDeviceD0ExitPreInterruptsDisabled = BcmBtEvtD0ExitPreInterruptsDisabled;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &Callbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, BCMBT_DEVICE);
    Attributes.EvtCleanupCallback = BcmBtEvtDeviceCleanup;
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Dev = BcmBtGetDevice(Device);
    Dev->Device = Device;
    Dev->DartNode = FDT_NO_NODE;
    Dev->Tree.Node = FDT_NO_NODE;
    KeInitializeSpinLock(&Dev->RingLock);
    KeInitializeSpinLock(&Dev->PacketLock);
    InitializeListHead(&Dev->RxPackets);
    KeInitializeEvent(&Dev->StateEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Dev->RxEvent, SynchronizationEvent, FALSE);
    KeInitializeTimer(&Dev->PollTimer);
    KeInitializeDpc(&Dev->PollDpc, BcmBtPollDpc, Dev);

    WDF_INTERRUPT_CONFIG_INIT(&InterruptConfig, BcmBtEvtIsr, BcmBtEvtDpc);
    return WdfInterruptCreate(Device, &InterruptConfig, WDF_NO_OBJECT_ATTRIBUTES, &Dev->Interrupt);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;

    WDF_DRIVER_CONFIG_INIT(&Config, BcmBtEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, WDF_NO_HANDLE);
}
