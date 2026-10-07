/*
 * PROJECT:     LiberNT HID over I2C Transport Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HID minidriver for devices that follow the HID over I2C protocol
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <hidport.h>
#include <acpiioct.h>
#include <initguid.h>
#include <reshub.h>
#include <spb.h>
#include <ntstrsafe.h>

#define NDEBUG
#include <debug.h>

#define HIDI2C_TAG 'C2iH'
#define HIDI2C_BCD_VERSION 0x0100
#define HIDI2C_QUEUED_REPORTS 32
#define HIDI2C_RESET_TIMEOUT_MS 5000
#define HIDI2C_POWER_ON_DELAY_MS 60
#define HIDI2C_WAKE_RETRY_DELAY_US 500
#define HIDI2C_MAXIMUM_REPORT_LENGTH 0xfff0
#define HIDI2C_COMMAND_HEADER_LENGTH 10

#define HIDI2C_OPCODE_RESET 0x01
#define HIDI2C_OPCODE_GET_REPORT 0x02
#define HIDI2C_OPCODE_SET_REPORT 0x03
#define HIDI2C_OPCODE_SET_POWER 0x08

#define HIDI2C_REPORT_TYPE_INPUT 0x01
#define HIDI2C_REPORT_TYPE_OUTPUT 0x02
#define HIDI2C_REPORT_TYPE_FEATURE 0x03

#define HIDI2C_POWER_ON 0x00
#define HIDI2C_POWER_SLEEP 0x01

DEFINE_GUID(HIDI2C_DSM_GUID, 0x3cdff6f7, 0x4267, 0x4555, 0xad, 0x05, 0xb3, 0x0a, 0x3d, 0x89, 0x38, 0xde);

#define HIDI2C_DSM_REVISION 1
#define HIDI2C_DSM_FUNCTION_DESCRIPTOR_ADDRESS 1

#include <pshpack1.h>
typedef struct _HIDI2C_DESCRIPTOR
{
    USHORT wHIDDescLength;
    USHORT bcdVersion;
    USHORT wReportDescLength;
    USHORT wReportDescRegister;
    USHORT wInputRegister;
    USHORT wMaxInputLength;
    USHORT wOutputRegister;
    USHORT wMaxOutputLength;
    USHORT wCommandRegister;
    USHORT wDataRegister;
    USHORT wVendorID;
    USHORT wProductID;
    USHORT wVersionID;
    ULONG Reserved;
} HIDI2C_DESCRIPTOR, *PHIDI2C_DESCRIPTOR;
#include <poppack.h>

C_ASSERT(sizeof(HIDI2C_DESCRIPTOR) == 30);

typedef struct _HIDI2C_DEVICE_EXTENSION
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDevice;
    LARGE_INTEGER ConnectionId;
    PFILE_OBJECT BusFileObject;
    PDEVICE_OBJECT BusDeviceObject;
    KMUTEX BusLock;
    PKINTERRUPT Interrupt;
    ULONG InterruptVector;
    KAFFINITY InterruptAffinity;
    KINTERRUPT_MODE InterruptMode;
    BOOLEAN InterruptShared;
    BOOLEAN HaveConnection;
    BOOLEAN HaveInterrupt;
    BOOLEAN DescriptorValid;
    BOOLEAN Started;
    volatile LONG ResetPending;
    KEVENT ResetEvent;
    USHORT DescriptorAddress;
    HIDI2C_DESCRIPTOR Descriptor;
    PUCHAR ReportDescriptor;
    PUCHAR InputBuffer;
    ULONG InputBufferLength;
    KSPIN_LOCK QueueLock;
    LIST_ENTRY PendingReads;
    PUCHAR ReportQueue;
    ULONG ReportSlotLength;
    ULONG ReportQueueHead;
    ULONG ReportQueueCount;
} HIDI2C_DEVICE_EXTENSION, *PHIDI2C_DEVICE_EXTENSION;

static
PHIDI2C_DEVICE_EXTENSION
HidI2cGetExtension(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;

    return HidExtension->MiniDeviceExtension;
}

static
VOID
HidI2cDelay(
    _In_ ULONG Microseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -(LONGLONG)Microseconds * 10;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
NTSTATUS
HidI2cCallBus(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp,
    _In_ PKEVENT Event,
    _In_ PIO_STATUS_BLOCK IoStatus)
{
    NTSTATUS Status;

    IoGetNextIrpStackLocation(Irp)->FileObject = DeviceExtension->BusFileObject;
    Status = IoCallDriver(DeviceExtension->BusDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus->Status;
    }
    return Status;
}

static
NTSTATUS
HidI2cBusWrite(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    LARGE_INTEGER Offset;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;

    if (!DeviceExtension->BusFileObject)
        return STATUS_DEVICE_NOT_READY;
    Offset.QuadPart = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_WRITE, DeviceExtension->BusDeviceObject, Buffer, Length, &Offset, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    return HidI2cCallBus(DeviceExtension, Irp, &Event, &IoStatus);
}

static
NTSTATUS
HidI2cBusRead(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    LARGE_INTEGER Offset;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    if (!DeviceExtension->BusFileObject)
        return STATUS_DEVICE_NOT_READY;
    Offset.QuadPart = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ, DeviceExtension->BusDeviceObject, Buffer, Length, &Offset, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = HidI2cCallBus(DeviceExtension, Irp, &Event, &IoStatus);
    if (NT_SUCCESS(Status) && IoStatus.Information != Length)
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
    return Status;
}

static
NTSTATUS
HidI2cBusWriteRead(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(WriteLength) PVOID WriteBuffer,
    _In_ ULONG WriteLength,
    _Out_writes_bytes_(ReadLength) PVOID ReadBuffer,
    _In_ ULONG ReadLength)
{
    SPB_TRANSFER_LIST_AND_ENTRIES(2) Sequence;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;

    if (!DeviceExtension->BusFileObject)
        return STATUS_DEVICE_NOT_READY;
    SPB_TRANSFER_LIST_INIT(&Sequence.List, 2);
    Sequence.List.Transfers[0] = SPB_TRANSFER_LIST_ENTRY_INIT_NON_PAGED(SpbTransferDirectionToDevice, 0, WriteBuffer, WriteLength);
    Sequence.List.Transfers[1] = SPB_TRANSFER_LIST_ENTRY_INIT_NON_PAGED(SpbTransferDirectionFromDevice, 0, ReadBuffer, ReadLength);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_SPB_EXECUTE_SEQUENCE, DeviceExtension->BusDeviceObject, &Sequence, sizeof(Sequence), NULL, 0, FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    return HidI2cCallBus(DeviceExtension, Irp, &Event, &IoStatus);
}

static
VOID
HidI2cLockBus(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    KeWaitForSingleObject(&DeviceExtension->BusLock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
HidI2cUnlockBus(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    KeReleaseMutex(&DeviceExtension->BusLock, FALSE);
}

static
ULONG
HidI2cEncodeCommand(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(5) PUCHAR Buffer,
    _In_ UCHAR Opcode,
    _In_ UCHAR ReportType,
    _In_ UCHAR ReportId)
{
    ULONG Length = 0;

    Buffer[Length++] = (UCHAR)DeviceExtension->Descriptor.wCommandRegister;
    Buffer[Length++] = (UCHAR)(DeviceExtension->Descriptor.wCommandRegister >> 8);
    if (ReportId < 0x0f)
    {
        Buffer[Length++] = (UCHAR)((ReportType << 4) | ReportId);
        Buffer[Length++] = Opcode;
    }
    else
    {
        Buffer[Length++] = (UCHAR)((ReportType << 4) | 0x0f);
        Buffer[Length++] = Opcode;
        Buffer[Length++] = ReportId;
    }
    return Length;
}

static
NTSTATUS
HidI2cSetPower(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR PowerState)
{
    UCHAR Command[5];
    ULONG Length;
    NTSTATUS Status;

    HidI2cLockBus(DeviceExtension);
    Length = HidI2cEncodeCommand(DeviceExtension, Command, HIDI2C_OPCODE_SET_POWER, 0, PowerState);
    Status = HidI2cBusWrite(DeviceExtension, Command, Length);
    if (!NT_SUCCESS(Status) && PowerState == HIDI2C_POWER_ON)
    {
        HidI2cDelay(HIDI2C_WAKE_RETRY_DELAY_US);
        Status = HidI2cBusWrite(DeviceExtension, Command, Length);
    }
    HidI2cUnlockBus(DeviceExtension);
    if (NT_SUCCESS(Status) && PowerState == HIDI2C_POWER_ON)
        HidI2cDelay(HIDI2C_POWER_ON_DELAY_MS * 1000);
    return Status;
}

static
NTSTATUS
HidI2cGetReport(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR ReportType,
    _Inout_ PHID_XFER_PACKET Packet)
{
    UCHAR Command[7];
    PUCHAR Response;
    ULONG CommandLength;
    ULONG ResponseLength;
    ULONG ReportLength;
    ULONG Skip;
    NTSTATUS Status;

    if (!Packet->reportBuffer || !Packet->reportBufferLen)
        return STATUS_INVALID_PARAMETER;
    Skip = Packet->reportId ? 0 : 1;
    if (Packet->reportBufferLen <= Skip)
        return STATUS_INVALID_PARAMETER;
    ResponseLength = sizeof(USHORT) + Packet->reportBufferLen - Skip;
    Response = ExAllocatePoolWithTag(NonPagedPool, ResponseLength, HIDI2C_TAG);
    if (!Response)
        return STATUS_INSUFFICIENT_RESOURCES;

    CommandLength = HidI2cEncodeCommand(DeviceExtension, Command, HIDI2C_OPCODE_GET_REPORT, ReportType, Packet->reportId);
    Command[CommandLength++] = (UCHAR)DeviceExtension->Descriptor.wDataRegister;
    Command[CommandLength++] = (UCHAR)(DeviceExtension->Descriptor.wDataRegister >> 8);
    HidI2cLockBus(DeviceExtension);
    Status = HidI2cBusWriteRead(DeviceExtension, Command, CommandLength, Response, ResponseLength);
    HidI2cUnlockBus(DeviceExtension);
    if (NT_SUCCESS(Status))
    {
        ReportLength = Response[0] | ((ULONG)Response[1] << 8);
        if (ReportLength <= sizeof(USHORT))
        {
            Status = STATUS_DEVICE_DATA_ERROR;
        }
        else
        {
            ReportLength = min(ReportLength, ResponseLength) - sizeof(USHORT);
            if (Skip)
                Packet->reportBuffer[0] = 0;
            RtlCopyMemory(Packet->reportBuffer + Skip, Response + sizeof(USHORT), ReportLength);
            Packet->reportBufferLen = ReportLength + Skip;
        }
    }
    ExFreePoolWithTag(Response, HIDI2C_TAG);
    return Status;
}

static
NTSTATUS
HidI2cSetReport(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR ReportType,
    _In_ PHID_XFER_PACKET Packet,
    _In_ BOOLEAN UseOutputRegister)
{
    PUCHAR Command;
    ULONG DataLength;
    ULONG Length = 0;
    ULONG LengthOffset;
    NTSTATUS Status;

    if (!Packet->reportBuffer || !Packet->reportBufferLen)
        return STATUS_INVALID_PARAMETER;
    DataLength = Packet->reportBufferLen - 1;
    if (DataLength > HIDI2C_MAXIMUM_REPORT_LENGTH)
        return STATUS_INVALID_BUFFER_SIZE;
    Command = ExAllocatePoolWithTag(NonPagedPool, DataLength + HIDI2C_COMMAND_HEADER_LENGTH, HIDI2C_TAG);
    if (!Command)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (UseOutputRegister)
    {
        Command[Length++] = (UCHAR)DeviceExtension->Descriptor.wOutputRegister;
        Command[Length++] = (UCHAR)(DeviceExtension->Descriptor.wOutputRegister >> 8);
    }
    else
    {
        Length = HidI2cEncodeCommand(DeviceExtension, Command, HIDI2C_OPCODE_SET_REPORT, ReportType, Packet->reportId);
        Command[Length++] = (UCHAR)DeviceExtension->Descriptor.wDataRegister;
        Command[Length++] = (UCHAR)(DeviceExtension->Descriptor.wDataRegister >> 8);
    }
    LengthOffset = Length;
    Length += sizeof(USHORT);
    if (Packet->reportId)
        Command[Length++] = Packet->reportId;
    RtlCopyMemory(Command + Length, Packet->reportBuffer + 1, DataLength);
    Length += DataLength;
    Command[LengthOffset] = (UCHAR)(Length - LengthOffset);
    Command[LengthOffset + 1] = (UCHAR)((Length - LengthOffset) >> 8);
    HidI2cLockBus(DeviceExtension);
    Status = HidI2cBusWrite(DeviceExtension, Command, Length);
    HidI2cUnlockBus(DeviceExtension);
    ExFreePoolWithTag(Command, HIDI2C_TAG);
    return Status;
}

static
VOID
NTAPI
HidI2cCancelRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidI2cGetExtension(DeviceObject);
    KIRQL OldIrql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&DeviceExtension->QueueLock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&DeviceExtension->QueueLock, OldIrql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
PIRP
HidI2cRemovePendingRead(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    while (!IsListEmpty(&DeviceExtension->PendingReads))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&DeviceExtension->PendingReads);
        PIRP Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        if (IoSetCancelRoutine(Irp, NULL))
            return Irp;
        InitializeListHead(&Irp->Tail.Overlay.ListEntry);
    }
    return NULL;
}

static
ULONG
HidI2cCopyReport(
    _Inout_ PIRP Irp,
    _In_reads_bytes_(Length) const UCHAR *Report,
    _In_ ULONG Length)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);

    Length = min(Length, IoStack->Parameters.DeviceIoControl.OutputBufferLength);
    RtlCopyMemory(Irp->UserBuffer, Report, Length);
    return Length;
}

static
VOID
HidI2cDeliverReport(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(Length) const UCHAR *Report,
    _In_ ULONG Length)
{
    PIRP Irp;
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->QueueLock, &OldIrql);
    if (!DeviceExtension->Started)
    {
        KeReleaseSpinLock(&DeviceExtension->QueueLock, OldIrql);
        return;
    }
    Irp = HidI2cRemovePendingRead(DeviceExtension);
    if (!Irp)
    {
        PUCHAR Slot;
        ULONG Index;

        if (DeviceExtension->ReportQueueCount == HIDI2C_QUEUED_REPORTS)
        {
            DeviceExtension->ReportQueueHead = (DeviceExtension->ReportQueueHead + 1) % HIDI2C_QUEUED_REPORTS;
            DeviceExtension->ReportQueueCount--;
        }
        Index = (DeviceExtension->ReportQueueHead + DeviceExtension->ReportQueueCount) % HIDI2C_QUEUED_REPORTS;
        Slot = DeviceExtension->ReportQueue + Index * DeviceExtension->ReportSlotLength;
        Slot[0] = (UCHAR)Length;
        Slot[1] = (UCHAR)(Length >> 8);
        RtlCopyMemory(Slot + sizeof(USHORT), Report, Length);
        DeviceExtension->ReportQueueCount++;
    }
    KeReleaseSpinLock(&DeviceExtension->QueueLock, OldIrql);
    if (Irp)
    {
        Irp->IoStatus.Information = HidI2cCopyReport(Irp, Report, Length);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
VOID
HidI2cFailPendingReads(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS Status)
{
    PIRP Irp;
    KIRQL OldIrql;

    for (;;)
    {
        KeAcquireSpinLock(&DeviceExtension->QueueLock, &OldIrql);
        Irp = HidI2cRemovePendingRead(DeviceExtension);
        DeviceExtension->ReportQueueCount = 0;
        DeviceExtension->ReportQueueHead = 0;
        KeReleaseSpinLock(&DeviceExtension->QueueLock, OldIrql);
        if (!Irp)
            break;
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
NTSTATUS
HidI2cReadReport(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    KIRQL OldIrql;
    ULONG Length;

    if (!Irp->UserBuffer || !IoStack->Parameters.DeviceIoControl.OutputBufferLength)
    {
        Irp->IoStatus.Status = STATUS_INVALID_USER_BUFFER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_USER_BUFFER;
    }

    KeAcquireSpinLock(&DeviceExtension->QueueLock, &OldIrql);
    if (!DeviceExtension->Started)
    {
        KeReleaseSpinLock(&DeviceExtension->QueueLock, OldIrql);
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }
    if (DeviceExtension->ReportQueueCount)
    {
        PUCHAR Slot = DeviceExtension->ReportQueue + DeviceExtension->ReportQueueHead * DeviceExtension->ReportSlotLength;

        Length = HidI2cCopyReport(Irp, Slot + sizeof(USHORT), Slot[0] | ((ULONG)Slot[1] << 8));
        DeviceExtension->ReportQueueHead = (DeviceExtension->ReportQueueHead + 1) % HIDI2C_QUEUED_REPORTS;
        DeviceExtension->ReportQueueCount--;
        KeReleaseSpinLock(&DeviceExtension->QueueLock, OldIrql);
        Irp->IoStatus.Information = Length;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    IoMarkIrpPending(Irp);
    InsertTailList(&DeviceExtension->PendingReads, &Irp->Tail.Overlay.ListEntry);
    IoSetCancelRoutine(Irp, HidI2cCancelRead);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        KeReleaseSpinLock(&DeviceExtension->QueueLock, OldIrql);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_PENDING;
    }
    KeReleaseSpinLock(&DeviceExtension->QueueLock, OldIrql);
    return STATUS_PENDING;
}

static
BOOLEAN
NTAPI
HidI2cInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = ServiceContext;
    PUCHAR Buffer = DeviceExtension->InputBuffer;
    ULONG Length;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Interrupt);
    HidI2cLockBus(DeviceExtension);
    Status = HidI2cBusRead(DeviceExtension, Buffer, DeviceExtension->InputBufferLength);
    HidI2cUnlockBus(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("HIDI2C: input read failed, status=0x%08lx\n", Status);
        return TRUE;
    }
    Length = Buffer[0] | ((ULONG)Buffer[1] << 8);
    if (!Length)
    {
        if (InterlockedExchange(&DeviceExtension->ResetPending, 0))
            KeSetEvent(&DeviceExtension->ResetEvent, IO_NO_INCREMENT, FALSE);
        return TRUE;
    }
    if (Length <= sizeof(USHORT) || Length > DeviceExtension->InputBufferLength)
    {
        DPRINT("HIDI2C: malformed input report length %lu\n", Length);
        return TRUE;
    }
    HidI2cDeliverReport(DeviceExtension, Buffer + sizeof(USHORT), Length - sizeof(USHORT));
    return TRUE;
}

static
NTSTATUS
HidI2cEvaluateDescriptorAddress(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR InputBuffer[FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) + ACPI_METHOD_ARGUMENT_LENGTH(sizeof(GUID)) + 3 * ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG))];
    UCHAR OutputBuffer[FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONGLONG))];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX Input = (PACPI_EVAL_INPUT_BUFFER_COMPLEX)InputBuffer;
    PACPI_EVAL_OUTPUT_BUFFER Output = (PACPI_EVAL_OUTPUT_BUFFER)OutputBuffer;
    PACPI_METHOD_ARGUMENT Argument;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    RtlZeroMemory(InputBuffer, sizeof(InputBuffer));
    RtlZeroMemory(OutputBuffer, sizeof(OutputBuffer));
    Input->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    Input->MethodName[0] = '_';
    Input->MethodName[1] = 'D';
    Input->MethodName[2] = 'S';
    Input->MethodName[3] = 'M';
    Input->Size = sizeof(InputBuffer) - FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
    Input->ArgumentCount = 4;
    Argument = Input->Argument;
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Argument, &HIDI2C_DSM_GUID, sizeof(GUID));
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, HIDI2C_DSM_REVISION);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, HIDI2C_DSM_FUNCTION_DESCRIPTOR_ADDRESS);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    Argument->Type = ACPI_METHOD_ARGUMENT_PACKAGE;
    Argument->DataLength = sizeof(ULONG);
    Argument->Argument = 0;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD, DeviceExtension->LowerDevice, InputBuffer, sizeof(InputBuffer), OutputBuffer, sizeof(OutputBuffer), FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (!NT_SUCCESS(Status))
        return Status;
    if (IoStatus.Information < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) ||
        Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE ||
        Output->Count < 1 ||
        Output->Argument[0].Type != ACPI_METHOD_ARGUMENT_INTEGER)
    {
        return STATUS_ACPI_INVALID_DATA;
    }
    DeviceExtension->DescriptorAddress = (USHORT)Output->Argument[0].Argument;
    return STATUS_SUCCESS;
}

static
NTSTATUS
HidI2cParseResources(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_opt_ PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_LIST List;
    ULONG Index;

    DeviceExtension->HaveConnection = FALSE;
    DeviceExtension->HaveInterrupt = FALSE;
    if (!Resources || !Resources->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    List = &Resources->List[0].PartialResourceList;
    for (Index = 0; Index < List->Count; Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = &List->PartialDescriptors[Index];

        if (Descriptor->Type == CmResourceTypeConnection &&
            Descriptor->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
            Descriptor->u.Connection.Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C &&
            !DeviceExtension->HaveConnection)
        {
            DeviceExtension->ConnectionId.LowPart = Descriptor->u.Connection.IdLowPart;
            DeviceExtension->ConnectionId.HighPart = Descriptor->u.Connection.IdHighPart;
            DeviceExtension->HaveConnection = TRUE;
        }
        else if (Descriptor->Type == CmResourceTypeInterrupt && !DeviceExtension->HaveInterrupt)
        {
            DeviceExtension->InterruptVector = Descriptor->u.Interrupt.Vector;
            DeviceExtension->InterruptAffinity = Descriptor->u.Interrupt.Affinity;
            DeviceExtension->InterruptMode = (Descriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive;
            DeviceExtension->InterruptShared = Descriptor->ShareDisposition == CmResourceShareShared;
            DeviceExtension->HaveInterrupt = TRUE;
        }
    }
    if (!DeviceExtension->HaveConnection || !DeviceExtension->HaveInterrupt)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return STATUS_SUCCESS;
}

static
NTSTATUS
HidI2cOpenBus(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    WCHAR PathBuffer[RESOURCE_HUB_PATH_CHARS];
    UNICODE_STRING Path;
    NTSTATUS Status;

    Status = RtlStringCbPrintfW(PathBuffer, sizeof(PathBuffer), L"%ls%08lx%08lx", RESOURCE_HUB_DEVICE_NAME_PREFIX, DeviceExtension->ConnectionId.HighPart, DeviceExtension->ConnectionId.LowPart);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlInitUnicodeString(&Path, PathBuffer);
    return IoGetDeviceObjectPointer(&Path, FILE_READ_DATA | FILE_WRITE_DATA, &DeviceExtension->BusFileObject, &DeviceExtension->BusDeviceObject);
}

static
VOID
HidI2cReleaseHardware(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->QueueLock, &OldIrql);
    DeviceExtension->Started = FALSE;
    KeReleaseSpinLock(&DeviceExtension->QueueLock, OldIrql);
    HidI2cFailPendingReads(DeviceExtension, STATUS_DEVICE_NOT_CONNECTED);
    if (DeviceExtension->Interrupt)
    {
        IoDisconnectInterrupt(DeviceExtension->Interrupt);
        DeviceExtension->Interrupt = NULL;
    }
    if (DeviceExtension->BusFileObject)
    {
        if (DeviceExtension->DescriptorValid)
            HidI2cSetPower(DeviceExtension, HIDI2C_POWER_SLEEP);
        ObDereferenceObject(DeviceExtension->BusFileObject);
        DeviceExtension->BusFileObject = NULL;
        DeviceExtension->BusDeviceObject = NULL;
    }
    if (DeviceExtension->ReportDescriptor)
    {
        ExFreePoolWithTag(DeviceExtension->ReportDescriptor, HIDI2C_TAG);
        DeviceExtension->ReportDescriptor = NULL;
    }
    if (DeviceExtension->InputBuffer)
    {
        ExFreePoolWithTag(DeviceExtension->InputBuffer, HIDI2C_TAG);
        DeviceExtension->InputBuffer = NULL;
    }
    if (DeviceExtension->ReportQueue)
    {
        ExFreePoolWithTag(DeviceExtension->ReportQueue, HIDI2C_TAG);
        DeviceExtension->ReportQueue = NULL;
    }
    DeviceExtension->DescriptorValid = FALSE;
    RtlZeroMemory(&DeviceExtension->Descriptor, sizeof(DeviceExtension->Descriptor));
}

static
NTSTATUS
HidI2cReset(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR Command[5];
    LARGE_INTEGER Timeout;
    ULONG Length;
    NTSTATUS Status;

    Status = HidI2cSetPower(DeviceExtension, HIDI2C_POWER_ON);
    if (!NT_SUCCESS(Status))
        return Status;
    KeClearEvent(&DeviceExtension->ResetEvent);
    InterlockedExchange(&DeviceExtension->ResetPending, 1);
    HidI2cLockBus(DeviceExtension);
    Length = HidI2cEncodeCommand(DeviceExtension, Command, HIDI2C_OPCODE_RESET, 0, 0);
    Status = HidI2cBusWrite(DeviceExtension, Command, Length);
    HidI2cUnlockBus(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&DeviceExtension->ResetPending, 0);
        return Status;
    }
    Timeout.QuadPart = -(LONGLONG)HIDI2C_RESET_TIMEOUT_MS * 10000;
    if (KeWaitForSingleObject(&DeviceExtension->ResetEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
    {
        InterlockedExchange(&DeviceExtension->ResetPending, 0);
        DPRINT1("HIDI2C: device did not acknowledge the reset within %u ms\n", HIDI2C_RESET_TIMEOUT_MS);
    }
    return HidI2cSetPower(DeviceExtension, HIDI2C_POWER_ON);
}

static
NTSTATUS
HidI2cStartHardware(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_opt_ PCM_RESOURCE_LIST Resources)
{
    PHIDI2C_DESCRIPTOR Descriptor = &DeviceExtension->Descriptor;
    UCHAR Register[sizeof(USHORT)];
    KIRQL OldIrql;
    NTSTATUS Status;

    HidI2cReleaseHardware(DeviceExtension);
    Status = HidI2cParseResources(DeviceExtension, Resources);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HIDI2C: device has no I2C connection or interrupt resource\n");
        return Status;
    }
    Status = HidI2cEvaluateDescriptorAddress(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HIDI2C: _DSM did not return the HID descriptor address, status=0x%08lx\n", Status);
        return Status;
    }
    Status = HidI2cOpenBus(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HIDI2C: connection %08lx%08lx did not open, status=0x%08lx\n", DeviceExtension->ConnectionId.HighPart, DeviceExtension->ConnectionId.LowPart, Status);
        return Status;
    }

    Register[0] = (UCHAR)DeviceExtension->DescriptorAddress;
    Register[1] = (UCHAR)(DeviceExtension->DescriptorAddress >> 8);
    HidI2cLockBus(DeviceExtension);
    Status = HidI2cBusWriteRead(DeviceExtension, Register, sizeof(Register), Descriptor, sizeof(*Descriptor));
    if (!NT_SUCCESS(Status))
    {
        HidI2cDelay(HIDI2C_WAKE_RETRY_DELAY_US);
        Status = HidI2cBusWriteRead(DeviceExtension, Register, sizeof(Register), Descriptor, sizeof(*Descriptor));
    }
    HidI2cUnlockBus(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HIDI2C: HID descriptor read at 0x%04x failed, status=0x%08lx\n", DeviceExtension->DescriptorAddress, Status);
        goto Fail;
    }
    if (Descriptor->bcdVersion != HIDI2C_BCD_VERSION || Descriptor->wHIDDescLength != sizeof(*Descriptor) || !Descriptor->wReportDescLength || Descriptor->wMaxInputLength < sizeof(USHORT))
    {
        DPRINT1("HIDI2C: unsupported HID descriptor, version=0x%04x length=%u report=%u input=%u\n", Descriptor->bcdVersion, Descriptor->wHIDDescLength, Descriptor->wReportDescLength, Descriptor->wMaxInputLength);
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
        goto Fail;
    }
    DeviceExtension->DescriptorValid = TRUE;

    DeviceExtension->InputBufferLength = Descriptor->wMaxInputLength;
    DeviceExtension->ReportSlotLength = DeviceExtension->InputBufferLength;
    DeviceExtension->InputBuffer = ExAllocatePoolWithTag(NonPagedPool, DeviceExtension->InputBufferLength, HIDI2C_TAG);
    DeviceExtension->ReportQueue = ExAllocatePoolWithTag(NonPagedPool, DeviceExtension->ReportSlotLength * HIDI2C_QUEUED_REPORTS, HIDI2C_TAG);
    DeviceExtension->ReportDescriptor = ExAllocatePoolWithTag(NonPagedPool, Descriptor->wReportDescLength, HIDI2C_TAG);
    if (!DeviceExtension->InputBuffer || !DeviceExtension->ReportQueue || !DeviceExtension->ReportDescriptor)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }

    Status = IoConnectInterrupt(&DeviceExtension->Interrupt, HidI2cInterruptService, DeviceExtension, NULL, DeviceExtension->InterruptVector, PASSIVE_LEVEL, PASSIVE_LEVEL, DeviceExtension->InterruptMode, DeviceExtension->InterruptShared, DeviceExtension->InterruptAffinity, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DeviceExtension->Interrupt = NULL;
        DPRINT1("HIDI2C: passive-level interrupt on vector 0x%lx did not connect, status=0x%08lx\n", DeviceExtension->InterruptVector, Status);
        goto Fail;
    }

    Status = HidI2cReset(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HIDI2C: reset failed, status=0x%08lx\n", Status);
        goto Fail;
    }

    Register[0] = (UCHAR)Descriptor->wReportDescRegister;
    Register[1] = (UCHAR)(Descriptor->wReportDescRegister >> 8);
    HidI2cLockBus(DeviceExtension);
    Status = HidI2cBusWriteRead(DeviceExtension, Register, sizeof(Register), DeviceExtension->ReportDescriptor, Descriptor->wReportDescLength);
    HidI2cUnlockBus(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("HIDI2C: report descriptor read failed, status=0x%08lx\n", Status);
        goto Fail;
    }

    KeAcquireSpinLock(&DeviceExtension->QueueLock, &OldIrql);
    DeviceExtension->ReportQueueHead = 0;
    DeviceExtension->ReportQueueCount = 0;
    DeviceExtension->Started = TRUE;
    KeReleaseSpinLock(&DeviceExtension->QueueLock, OldIrql);
    DPRINT1("HIDI2C: device %04x:%04x version %04x started, report descriptor %u bytes, input %u bytes\n", Descriptor->wVendorID, Descriptor->wProductID, Descriptor->wVersionID, Descriptor->wReportDescLength, Descriptor->wMaxInputLength);
    return STATUS_SUCCESS;

Fail:
    HidI2cReleaseHardware(DeviceExtension);
    return Status;
}

static
NTSTATUS
HidI2cCompleteWithBuffer(
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
NTAPI
HidI2cInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidI2cGetExtension(DeviceObject);
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG IoControlCode = IoStack->Parameters.DeviceIoControl.IoControlCode;
    PHID_XFER_PACKET Packet;
    NTSTATUS Status;

    if (IoControlCode == IOCTL_HID_READ_REPORT)
        return HidI2cReadReport(DeviceExtension, Irp);

    Irp->IoStatus.Information = 0;
    if (!DeviceExtension->Started)
    {
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }

    switch (IoControlCode)
    {
        case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        {
            HID_DESCRIPTOR HidDescriptor;

            RtlZeroMemory(&HidDescriptor, sizeof(HidDescriptor));
            HidDescriptor.bLength = sizeof(HidDescriptor);
            HidDescriptor.bDescriptorType = HID_HID_DESCRIPTOR_TYPE;
            HidDescriptor.bcdHID = DeviceExtension->Descriptor.bcdVersion;
            HidDescriptor.bNumDescriptors = 1;
            HidDescriptor.DescriptorList[0].bReportType = HID_REPORT_DESCRIPTOR_TYPE;
            HidDescriptor.DescriptorList[0].wReportLength = DeviceExtension->Descriptor.wReportDescLength;
            return HidI2cCompleteWithBuffer(Irp, &HidDescriptor, sizeof(HidDescriptor));
        }

        case IOCTL_HID_GET_REPORT_DESCRIPTOR:
            return HidI2cCompleteWithBuffer(Irp, DeviceExtension->ReportDescriptor, DeviceExtension->Descriptor.wReportDescLength);

        case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
        {
            HID_DEVICE_ATTRIBUTES Attributes;

            RtlZeroMemory(&Attributes, sizeof(Attributes));
            Attributes.Size = sizeof(Attributes);
            Attributes.VendorID = DeviceExtension->Descriptor.wVendorID;
            Attributes.ProductID = DeviceExtension->Descriptor.wProductID;
            Attributes.VersionNumber = DeviceExtension->Descriptor.wVersionID;
            return HidI2cCompleteWithBuffer(Irp, &Attributes, sizeof(Attributes));
        }

        case IOCTL_HID_GET_FEATURE:
        case IOCTL_HID_GET_INPUT_REPORT:
            Packet = Irp->UserBuffer;
            if (!Packet)
                Status = STATUS_INVALID_PARAMETER;
            else
                Status = HidI2cGetReport(DeviceExtension, IoControlCode == IOCTL_HID_GET_FEATURE ? HIDI2C_REPORT_TYPE_FEATURE : HIDI2C_REPORT_TYPE_INPUT, Packet);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = Packet->reportBufferLen;
            break;

        case IOCTL_HID_SET_FEATURE:
        case IOCTL_HID_SET_OUTPUT_REPORT:
        case IOCTL_HID_WRITE_REPORT:
            Packet = Irp->UserBuffer;
            if (!Packet)
            {
                Status = STATUS_INVALID_PARAMETER;
            }
            else if (IoControlCode == IOCTL_HID_WRITE_REPORT && DeviceExtension->Descriptor.wMaxOutputLength)
            {
                Status = HidI2cSetReport(DeviceExtension, HIDI2C_REPORT_TYPE_OUTPUT, Packet, TRUE);
            }
            else
            {
                Status = HidI2cSetReport(DeviceExtension, IoControlCode == IOCTL_HID_SET_FEATURE ? HIDI2C_REPORT_TYPE_FEATURE : HIDI2C_REPORT_TYPE_OUTPUT, Packet, FALSE);
            }
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = Packet->reportBufferLen;
            break;

        case IOCTL_HID_ACTIVATE_DEVICE:
        case IOCTL_HID_DEACTIVATE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
HidI2cPnpCompletion(
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
HidI2cPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidI2cGetExtension(DeviceObject);
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    KEVENT Event;
    NTSTATUS Status;

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, HidI2cPnpCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status))
                Status = HidI2cStartHardware(DeviceExtension, IoStack->Parameters.StartDevice.AllocatedResourcesTranslated);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_REMOVE_DEVICE:
            HidI2cReleaseHardware(DeviceExtension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidI2cPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidI2cGetExtension(DeviceObject);

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidI2cPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidI2cGetExtension(DeviceObject);

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
HidI2cCreateClose(
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
HidI2cAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidExtension->MiniDeviceExtension;

    UNREFERENCED_PARAMETER(DriverObject);
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->DeviceObject = DeviceObject;
    DeviceExtension->LowerDevice = HidExtension->NextDeviceObject;
    KeInitializeMutex(&DeviceExtension->BusLock, 0);
    KeInitializeEvent(&DeviceExtension->ResetEvent, NotificationEvent, FALSE);
    KeInitializeSpinLock(&DeviceExtension->QueueLock);
    InitializeListHead(&DeviceExtension->PendingReads);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
HidI2cUnload(
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

    DriverObject->MajorFunction[IRP_MJ_CREATE] = HidI2cCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = HidI2cCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = HidI2cInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = HidI2cPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = HidI2cPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = HidI2cPassThrough;
    DriverObject->DriverExtension->AddDevice = HidI2cAddDevice;
    DriverObject->DriverUnload = HidI2cUnload;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Revision = HID_REVISION;
    Registration.DriverObject = DriverObject;
    Registration.RegistryPath = RegistryPath;
    Registration.DeviceExtensionSize = sizeof(HIDI2C_DEVICE_EXTENSION);
    Registration.DevicesArePolled = FALSE;
    return HidRegisterMinidriver(&Registration);
}
