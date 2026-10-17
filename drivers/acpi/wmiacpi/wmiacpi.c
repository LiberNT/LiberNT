/*
 * PROJECT:     LiberNT ACPI WMI Mapping Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Maps the WMI data blocks described by ACPI _WDG objects to WMI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <wmistr.h>
#include <wmilib.h>
#include <acpiioct.h>
#include <initguid.h>
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

#define TAG_WMIACPI 'AimW'

#define WMIACPI_FLAG_EXPENSIVE 0x01
#define WMIACPI_FLAG_METHOD 0x02
#define WMIACPI_FLAG_STRING 0x04
#define WMIACPI_FLAG_EVENT 0x08

typedef struct _WMIACPI_WDG_ENTRY
{
    GUID Guid;
    union
    {
        CHAR ObjectId[2];
        struct
        {
            UCHAR NotifyId;
            UCHAR Reserved;
        };
    };
    UCHAR InstanceCount;
    UCHAR Flags;
} WMIACPI_WDG_ENTRY, *PWMIACPI_WDG_ENTRY;

C_ASSERT(sizeof(WMIACPI_WDG_ENTRY) == 20);

typedef struct _WMIACPI_BLOCK
{
    GUID Guid;
    CHAR ObjectId[2];
    UCHAR NotifyId;
    UCHAR Flags;
    BOOLEAN EventsEnabled;
} WMIACPI_BLOCK, *PWMIACPI_BLOCK;

typedef struct _WMIACPI_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    WMILIB_CONTEXT WmiLibContext;
    PWMIGUIDREGINFO GuidList;
    PWMIACPI_BLOCK Blocks;
    ULONG BlockCount;
    ACPI_INTERFACE_STANDARD AcpiInterface;
    BOOLEAN AcpiInterfaceValid;
    BOOLEAN NotificationsRegistered;
    BOOLEAN WmiRegistered;
} WMIACPI_EXTENSION, *PWMIACPI_EXTENSION;

typedef struct _WMIACPI_NOTIFY_WORK
{
    PIO_WORKITEM WorkItem;
    PWMIACPI_EXTENSION Extension;
    ULONG NotifyCode;
} WMIACPI_NOTIFY_WORK, *PWMIACPI_NOTIFY_WORK;

DRIVER_INITIALIZE DriverEntry;
static DRIVER_ADD_DEVICE WmiAcpiAddDevice;
static DRIVER_UNLOAD WmiAcpiUnload;
_Dispatch_type_(IRP_MJ_PNP) static DRIVER_DISPATCH WmiAcpiPnp;
_Dispatch_type_(IRP_MJ_POWER) static DRIVER_DISPATCH WmiAcpiPower;
_Dispatch_type_(IRP_MJ_SYSTEM_CONTROL) static DRIVER_DISPATCH WmiAcpiSystemControl;
_Dispatch_type_(IRP_MJ_CREATE) _Dispatch_type_(IRP_MJ_CLOSE) static DRIVER_DISPATCH WmiAcpiCreateClose;
static IO_WORKITEM_ROUTINE WmiAcpiNotifyWorker;

static UNICODE_STRING WmiAcpiRegistryPath;

static
NTSTATUS
WmiAcpiSendIoctl(
    _In_ PWMIACPI_EXTENSION Extension,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_(InputLength) PVOID Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        Extension->LowerDevice,
                                        Input,
                                        InputLength,
                                        Output,
                                        OutputLength,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(Extension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

static
VOID
WmiAcpiMethodName(
    _Out_writes_(4) PCHAR Name,
    _In_ CHAR Prefix0,
    _In_ CHAR Prefix1,
    _In_ PWMIACPI_BLOCK Block,
    _In_ BOOLEAN UseNotifyId)
{
    static const CHAR Hex[] = "0123456789ABCDEF";

    Name[0] = Prefix0;
    Name[1] = Prefix1;
    if (UseNotifyId)
    {
        Name[2] = Hex[(Block->NotifyId >> 4) & 0xF];
        Name[3] = Hex[Block->NotifyId & 0xF];
    }
    else
    {
        Name[2] = Block->ObjectId[0];
        Name[3] = Block->ObjectId[1];
    }
}

static
PVOID
WmiAcpiBuildInput(
    _In_reads_(4) PCHAR MethodName,
    _In_ ULONG ArgumentCount,
    _In_ ULONG Argument0,
    _In_ ULONG Argument1,
    _In_ BOOLEAN HasData,
    _In_ BOOLEAN DataIsString,
    _In_reads_bytes_opt_(DataSize) PVOID Data,
    _In_ ULONG DataSize,
    _Out_ PULONG InputSize)
{
    PACPI_EVAL_INPUT_BUFFER_COMPLEX Complex;
    PACPI_EVAL_INPUT_BUFFER Simple;
    PACPI_METHOD_ARGUMENT Argument;

    if (!ArgumentCount && !HasData)
    {
        *InputSize = sizeof(ACPI_EVAL_INPUT_BUFFER);
        Simple = ExAllocatePoolZero(NonPagedPoolNx, *InputSize, TAG_WMIACPI);
        if (Simple)
        {
            Simple->Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
            RtlCopyMemory(Simple->MethodName, MethodName, 4);
        }
        return Simple;
    }

    *InputSize = FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) +
                 ArgumentCount * ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) +
                 (HasData ? ACPI_METHOD_ARGUMENT_LENGTH(DataSize) : 0);
    Complex = ExAllocatePoolZero(NonPagedPoolNx, *InputSize, TAG_WMIACPI);
    if (!Complex)
        return NULL;

    Complex->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    RtlCopyMemory(Complex->MethodName, MethodName, 4);
    Complex->Size = *InputSize - FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
    Complex->ArgumentCount = ArgumentCount + (HasData ? 1 : 0);

    Argument = Complex->Argument;
    if (ArgumentCount > 0)
    {
        ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, Argument0);
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }
    if (ArgumentCount > 1)
    {
        ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, Argument1);
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }
    if (HasData)
    {
        Argument->Type = DataIsString ? ACPI_METHOD_ARGUMENT_STRING : ACPI_METHOD_ARGUMENT_BUFFER;
        Argument->DataLength = (USHORT)DataSize;
        if (DataSize)
            RtlCopyMemory(Argument->Data, Data, DataSize);
    }

    return Complex;
}

static
NTSTATUS
WmiAcpiEvaluate(
    _In_ PWMIACPI_EXTENSION Extension,
    _In_reads_(4) PCHAR MethodName,
    _In_ ULONG ArgumentCount,
    _In_ ULONG Argument0,
    _In_ ULONG Argument1,
    _In_ BOOLEAN HasData,
    _In_ BOOLEAN DataIsString,
    _In_reads_bytes_opt_(DataSize) PVOID Data,
    _In_ ULONG DataSize,
    _Out_ PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    ULONG InputSize, OutputSize;
    PVOID Input;
    NTSTATUS Status;

    *OutputBuffer = NULL;

    if (HasData && DataSize > MAXUSHORT)
        return STATUS_INVALID_PARAMETER;

    Input = WmiAcpiBuildInput(MethodName, ArgumentCount, Argument0, Argument1,
                              HasData, DataIsString, Data, DataSize, &InputSize);
    if (!Input)
        return STATUS_INSUFFICIENT_RESOURCES;

    OutputSize = sizeof(ACPI_EVAL_OUTPUT_BUFFER) + 0x100;
    for (;;)
    {
        Output = ExAllocatePoolZero(NonPagedPoolNx, OutputSize, TAG_WMIACPI);
        if (!Output)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = WmiAcpiSendIoctl(Extension, IOCTL_ACPI_EVAL_METHOD, Input, InputSize, Output, OutputSize);
        if (Status == STATUS_BUFFER_OVERFLOW &&
            Output->Signature == ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE &&
            Output->Length > OutputSize &&
            Output->Length < 0x100000)
        {
            OutputSize = Output->Length;
            ExFreePoolWithTag(Output, TAG_WMIACPI);
            continue;
        }

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Output, TAG_WMIACPI);
            break;
        }

        *OutputBuffer = Output;
        break;
    }

    ExFreePoolWithTag(Input, TAG_WMIACPI);
    return Status;
}

static
ULONG
WmiAcpiConvertArgument(
    _In_ PACPI_METHOD_ARGUMENT Argument,
    _In_ BOOLEAN StringBlock,
    _Out_writes_bytes_opt_(Available) PUCHAR Buffer,
    _In_ ULONG Available,
    _In_ ULONG Offset)
{
    PACPI_METHOD_ARGUMENT Element, End;
    ULONG Length, Index;
    USHORT Count;

    switch (Argument->Type)
    {
        case ACPI_METHOD_ARGUMENT_INTEGER:
            Offset = ALIGN_UP_BY(Offset, sizeof(ULONG));
            if (Buffer && Offset + sizeof(ULONG) <= Available)
                RtlCopyMemory(Buffer + Offset, &Argument->Argument, sizeof(ULONG));
            return Offset + sizeof(ULONG);

        case ACPI_METHOD_ARGUMENT_STRING:
            Length = Argument->DataLength;
            while (Length && Argument->Data[Length - 1] == 0)
                Length--;
            if (!StringBlock)
            {
                if (Buffer && Offset + Argument->DataLength <= Available)
                    RtlCopyMemory(Buffer + Offset, Argument->Data, Argument->DataLength);
                return Offset + Argument->DataLength;
            }

            Offset = ALIGN_UP_BY(Offset, sizeof(USHORT));
            Count = (USHORT)(Length * sizeof(WCHAR));
            if (Buffer && Offset + sizeof(USHORT) + Count <= Available)
            {
                RtlCopyMemory(Buffer + Offset, &Count, sizeof(USHORT));
                for (Index = 0; Index < Length; Index++)
                {
                    WCHAR Char = Argument->Data[Index];
                    RtlCopyMemory(Buffer + Offset + sizeof(USHORT) + Index * sizeof(WCHAR), &Char, sizeof(WCHAR));
                }
            }
            return Offset + sizeof(USHORT) + Count;

        case ACPI_METHOD_ARGUMENT_BUFFER:
            if (Buffer && Offset + Argument->DataLength <= Available)
                RtlCopyMemory(Buffer + Offset, Argument->Data, Argument->DataLength);
            return Offset + Argument->DataLength;

        case ACPI_METHOD_ARGUMENT_PACKAGE:
            Element = (PACPI_METHOD_ARGUMENT)Argument->Data;
            End = (PACPI_METHOD_ARGUMENT)(Argument->Data + Argument->DataLength);
            while (Element < End)
            {
                Offset = WmiAcpiConvertArgument(Element, StringBlock, Buffer, Available, Offset);
                Element = ACPI_METHOD_NEXT_ARGUMENT(Element);
            }
            return Offset;

        default:
            return Offset;
    }
}

static
ULONG
WmiAcpiConvertOutput(
    _In_opt_ PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ BOOLEAN StringBlock,
    _Out_writes_bytes_opt_(Available) PUCHAR Buffer,
    _In_ ULONG Available)
{
    PACPI_METHOD_ARGUMENT Argument;
    ULONG Index, Offset = 0;

    if (!Output)
        return 0;

    Argument = Output->Argument;
    for (Index = 0; Index < Output->Count; Index++)
    {
        Offset = WmiAcpiConvertArgument(Argument, StringBlock, Buffer, Available, Offset);
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }

    return Offset;
}

static
NTSTATUS
WmiAcpiPrepareInput(
    _In_ PWMIACPI_BLOCK Block,
    _In_reads_bytes_(InSize) PUCHAR In,
    _In_ ULONG InSize,
    _Out_ PVOID *Data,
    _Out_ PULONG DataSize,
    _Out_ PBOOLEAN IsString)
{
    USHORT Count;
    PCHAR String;
    ULONG Index;

    *Data = In;
    *DataSize = InSize;
    *IsString = FALSE;

    if (!(Block->Flags & WMIACPI_FLAG_STRING))
        return STATUS_SUCCESS;

    *IsString = TRUE;
    Count = 0;
    if (InSize >= sizeof(USHORT))
        RtlCopyMemory(&Count, In, sizeof(USHORT));
    if (Count > InSize - min(InSize, sizeof(USHORT)))
        return STATUS_INVALID_PARAMETER;

    String = ExAllocatePoolZero(NonPagedPoolNx, Count / sizeof(WCHAR) + 1, TAG_WMIACPI);
    if (!String)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < Count / sizeof(WCHAR); Index++)
    {
        WCHAR Char;
        RtlCopyMemory(&Char, In + sizeof(USHORT) + Index * sizeof(WCHAR), sizeof(WCHAR));
        String[Index] = (Char < 0x80) ? (CHAR)Char : '?';
    }

    *Data = String;
    *DataSize = Count / sizeof(WCHAR) + 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
WmiAcpiQueryRegInfo(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PULONG RegFlags,
    _Inout_ PUNICODE_STRING InstanceName,
    _Outptr_result_maybenull_ PUNICODE_STRING *RegistryPath,
    _Inout_ PUNICODE_STRING MofResourceName,
    _Outptr_result_maybenull_ PDEVICE_OBJECT *Pdo)
{
    PWMIACPI_EXTENSION Extension = DeviceObject->DeviceExtension;

    UNREFERENCED_PARAMETER(InstanceName);
    UNREFERENCED_PARAMETER(MofResourceName);

    *RegFlags = WMIREG_FLAG_INSTANCE_PDO;
    *RegistryPath = &WmiAcpiRegistryPath;
    *Pdo = Extension->Pdo;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
WmiAcpiQueryDataBlock(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ ULONG GuidIndex,
    _In_ ULONG InstanceIndex,
    _In_ ULONG InstanceCount,
    _Out_writes_opt_(InstanceCount) PULONG InstanceLengthArray,
    _In_ ULONG BufferAvail,
    _Out_writes_bytes_opt_(BufferAvail) PUCHAR Buffer)
{
    PWMIACPI_EXTENSION Extension = DeviceObject->DeviceExtension;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PWMIACPI_BLOCK Block;
    ULONG Index, Offset = 0, Length, Needed = 0;
    BOOLEAN TooSmall = FALSE;
    CHAR Name[4];
    NTSTATUS Status = STATUS_SUCCESS;

    if (GuidIndex >= Extension->BlockCount)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_GUID_NOT_FOUND, 0, IO_NO_INCREMENT);

    Block = &Extension->Blocks[GuidIndex];
    if (InstanceIndex + InstanceCount > Extension->GuidList[GuidIndex].InstanceCount)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_INSTANCE_NOT_FOUND, 0, IO_NO_INCREMENT);

    WmiAcpiMethodName(Name, 'W', 'Q', Block, FALSE);

    for (Index = 0; Index < InstanceCount; Index++)
    {
        Output = NULL;
        if (!(Block->Flags & WMIACPI_FLAG_METHOD))
        {
            Status = WmiAcpiEvaluate(Extension, Name, 1, InstanceIndex + Index, 0,
                                     FALSE, FALSE, NULL, 0, &Output);
            if (!NT_SUCCESS(Status))
                break;
        }

        Offset = ALIGN_UP_BY(Offset, 8);
        Length = WmiAcpiConvertOutput(Output, (Block->Flags & WMIACPI_FLAG_STRING) != 0, NULL, 0);
        if (!TooSmall && Buffer && Offset + Length <= BufferAvail)
        {
            WmiAcpiConvertOutput(Output, (Block->Flags & WMIACPI_FLAG_STRING) != 0, Buffer + Offset, Length);
            if (InstanceLengthArray)
                InstanceLengthArray[Index] = Length;
        }
        else
        {
            TooSmall = TRUE;
        }

        Offset += Length;
        Needed = Offset;
        if (Output)
            ExFreePoolWithTag(Output, TAG_WMIACPI);
    }

    if (!NT_SUCCESS(Status))
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_GUID_NOT_FOUND, 0, IO_NO_INCREMENT);

    return WmiCompleteRequest(DeviceObject,
                              Irp,
                              TooSmall ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS,
                              Needed,
                              IO_NO_INCREMENT);
}

static
NTSTATUS
WmiAcpiSet(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ ULONG GuidIndex,
    _In_ ULONG InstanceIndex,
    _In_ ULONG BufferSize,
    _In_reads_bytes_(BufferSize) PUCHAR Buffer)
{
    PWMIACPI_EXTENSION Extension = DeviceObject->DeviceExtension;
    PACPI_EVAL_OUTPUT_BUFFER Output = NULL;
    PWMIACPI_BLOCK Block;
    BOOLEAN IsString;
    PVOID Data;
    ULONG DataSize;
    CHAR Name[4];
    NTSTATUS Status;

    if (GuidIndex >= Extension->BlockCount)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_GUID_NOT_FOUND, 0, IO_NO_INCREMENT);

    Block = &Extension->Blocks[GuidIndex];
    if (Block->Flags & WMIACPI_FLAG_METHOD)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_READ_ONLY, 0, IO_NO_INCREMENT);

    Status = WmiAcpiPrepareInput(Block, Buffer, BufferSize, &Data, &DataSize, &IsString);
    if (!NT_SUCCESS(Status))
        return WmiCompleteRequest(DeviceObject, Irp, Status, 0, IO_NO_INCREMENT);

    WmiAcpiMethodName(Name, 'W', 'S', Block, FALSE);
    Status = WmiAcpiEvaluate(Extension, Name, 1, InstanceIndex, 0, TRUE, IsString, Data, DataSize, &Output);
    if (Data != Buffer)
        ExFreePoolWithTag(Data, TAG_WMIACPI);
    if (Output)
        ExFreePoolWithTag(Output, TAG_WMIACPI);

    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        Status = STATUS_WMI_READ_ONLY;
    else if (!NT_SUCCESS(Status))
        Status = STATUS_WMI_SET_FAILURE;

    return WmiCompleteRequest(DeviceObject, Irp, Status, 0, IO_NO_INCREMENT);
}

static
NTSTATUS
NTAPI
WmiAcpiSetDataBlock(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ ULONG GuidIndex,
    _In_ ULONG InstanceIndex,
    _In_ ULONG BufferSize,
    _In_reads_bytes_(BufferSize) PUCHAR Buffer)
{
    return WmiAcpiSet(DeviceObject, Irp, GuidIndex, InstanceIndex, BufferSize, Buffer);
}

static
NTSTATUS
NTAPI
WmiAcpiExecuteMethod(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ ULONG GuidIndex,
    _In_ ULONG InstanceIndex,
    _In_ ULONG MethodId,
    _In_ ULONG InBufferSize,
    _In_ ULONG OutBufferSize,
    _Inout_updates_bytes_(max(InBufferSize, OutBufferSize)) PUCHAR Buffer)
{
    PWMIACPI_EXTENSION Extension = DeviceObject->DeviceExtension;
    PACPI_EVAL_OUTPUT_BUFFER Output = NULL;
    PWMIACPI_BLOCK Block;
    BOOLEAN IsString, StringBlock;
    PVOID Data;
    ULONG DataSize, Needed;
    CHAR Name[4];
    NTSTATUS Status;

    if (GuidIndex >= Extension->BlockCount)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_GUID_NOT_FOUND, 0, IO_NO_INCREMENT);

    Block = &Extension->Blocks[GuidIndex];
    if (!(Block->Flags & WMIACPI_FLAG_METHOD))
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_INVALID_DEVICE_REQUEST, 0, IO_NO_INCREMENT);

    Status = WmiAcpiPrepareInput(Block, Buffer, InBufferSize, &Data, &DataSize, &IsString);
    if (!NT_SUCCESS(Status))
        return WmiCompleteRequest(DeviceObject, Irp, Status, 0, IO_NO_INCREMENT);

    WmiAcpiMethodName(Name, 'W', 'M', Block, FALSE);
    Status = WmiAcpiEvaluate(Extension, Name, 2, InstanceIndex, MethodId, TRUE, IsString, Data, DataSize, &Output);
    if (Data != Buffer)
        ExFreePoolWithTag(Data, TAG_WMIACPI);

    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
            Status = STATUS_WMI_ITEMID_NOT_FOUND;
        return WmiCompleteRequest(DeviceObject, Irp, Status, 0, IO_NO_INCREMENT);
    }

    StringBlock = (Block->Flags & WMIACPI_FLAG_STRING) != 0;
    Needed = WmiAcpiConvertOutput(Output, StringBlock, NULL, 0);
    if (Needed > OutBufferSize)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        WmiAcpiConvertOutput(Output, StringBlock, Buffer, OutBufferSize);
        Status = STATUS_SUCCESS;
    }

    if (Output)
        ExFreePoolWithTag(Output, TAG_WMIACPI);
    return WmiCompleteRequest(DeviceObject, Irp, Status, Needed, IO_NO_INCREMENT);
}

static
NTSTATUS
NTAPI
WmiAcpiFunctionControl(
    _Inout_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ ULONG GuidIndex,
    _In_ WMIENABLEDISABLECONTROL Function,
    _In_ BOOLEAN Enable)
{
    PWMIACPI_EXTENSION Extension = DeviceObject->DeviceExtension;
    PACPI_EVAL_OUTPUT_BUFFER Output = NULL;
    PWMIACPI_BLOCK Block;
    CHAR Name[4];
    NTSTATUS Status;

    if (GuidIndex >= Extension->BlockCount)
        return WmiCompleteRequest(DeviceObject, Irp, STATUS_WMI_GUID_NOT_FOUND, 0, IO_NO_INCREMENT);

    Block = &Extension->Blocks[GuidIndex];
    if (Function == WmiEventControl)
    {
        if (!(Block->Flags & WMIACPI_FLAG_EVENT))
            return WmiCompleteRequest(DeviceObject, Irp, STATUS_SUCCESS, 0, IO_NO_INCREMENT);
        WmiAcpiMethodName(Name, 'W', 'E', Block, TRUE);
    }
    else
    {
        if (!(Block->Flags & WMIACPI_FLAG_EXPENSIVE))
            return WmiCompleteRequest(DeviceObject, Irp, STATUS_SUCCESS, 0, IO_NO_INCREMENT);
        WmiAcpiMethodName(Name, 'W', 'C', Block, FALSE);
    }

    Status = WmiAcpiEvaluate(Extension, Name, 1, Enable ? 1 : 0, 0, FALSE, FALSE, NULL, 0, &Output);
    if (Output)
        ExFreePoolWithTag(Output, TAG_WMIACPI);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        Status = STATUS_SUCCESS;

    if (NT_SUCCESS(Status) && Function == WmiEventControl)
        Block->EventsEnabled = Enable;

    return WmiCompleteRequest(DeviceObject, Irp, Status, 0, IO_NO_INCREMENT);
}

static
VOID
NTAPI
WmiAcpiNotifyWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PWMIACPI_NOTIFY_WORK Work = Context;
    PWMIACPI_EXTENSION Extension = Work->Extension;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PWMIACPI_BLOCK Block;
    ULONG Index, Size;
    PUCHAR EventData;
    CHAR Name[4] = { '_', 'W', 'E', 'D' };
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    for (Index = 0; Index < Extension->BlockCount; Index++)
    {
        Block = &Extension->Blocks[Index];
        if (!(Block->Flags & WMIACPI_FLAG_EVENT) || Block->NotifyId != Work->NotifyCode)
            continue;

        Output = NULL;
        Status = WmiAcpiEvaluate(Extension, Name, 1, Work->NotifyCode, 0, FALSE, FALSE, NULL, 0, &Output);
        if (!NT_SUCCESS(Status))
            Output = NULL;

        if (Block->EventsEnabled)
        {
            Size = WmiAcpiConvertOutput(Output, (Block->Flags & WMIACPI_FLAG_STRING) != 0, NULL, 0);
            EventData = Size ? ExAllocatePoolZero(NonPagedPoolNx, Size, TAG_WMIACPI) : NULL;
            if (EventData)
                WmiAcpiConvertOutput(Output, (Block->Flags & WMIACPI_FLAG_STRING) != 0, EventData, Size);
            WmiFireEvent(Extension->Self, &Block->Guid, 0, EventData ? Size : 0, EventData);
        }

        if (Output)
            ExFreePoolWithTag(Output, TAG_WMIACPI);
        break;
    }

    IoReleaseRemoveLock(&Extension->RemoveLock, Work);
    IoFreeWorkItem(Work->WorkItem);
    ExFreePoolWithTag(Work, TAG_WMIACPI);
}

static
VOID
NTAPI
WmiAcpiNotify(
    _In_ PVOID Context,
    _In_ ULONG NotifyCode)
{
    PWMIACPI_EXTENSION Extension = Context;
    PWMIACPI_NOTIFY_WORK Work;

    Work = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Work), TAG_WMIACPI);
    if (!Work)
        return;

    Work->WorkItem = IoAllocateWorkItem(Extension->Self);
    if (!Work->WorkItem || !NT_SUCCESS(IoAcquireRemoveLock(&Extension->RemoveLock, Work)))
    {
        if (Work->WorkItem)
            IoFreeWorkItem(Work->WorkItem);
        ExFreePoolWithTag(Work, TAG_WMIACPI);
        return;
    }

    Work->Extension = Extension;
    Work->NotifyCode = NotifyCode;
    IoQueueWorkItem(Work->WorkItem, WmiAcpiNotifyWorker, DelayedWorkQueue, Work);
}

static
NTSTATUS
WmiAcpiQueryAcpiInterface(
    _In_ PWMIACPI_EXTENSION Extension)
{
    PIO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Extension->LowerDevice, NULL, 0, NULL, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_ACPI_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size = sizeof(ACPI_INTERFACE_STANDARD);
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)&Extension->AcpiInterface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(Extension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

static
NTSTATUS
WmiAcpiLoadBlocks(
    _In_ PWMIACPI_EXTENSION Extension)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PWMIACPI_WDG_ENTRY Entries;
    ULONG Count, Index;
    CHAR Name[4] = { '_', 'W', 'D', 'G' };
    NTSTATUS Status;

    Status = WmiAcpiEvaluate(Extension, Name, 0, 0, 0, FALSE, FALSE, NULL, 0, &Output);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!Output->Count ||
        Output->Argument[0].Type != ACPI_METHOD_ARGUMENT_BUFFER ||
        Output->Argument[0].DataLength < sizeof(WMIACPI_WDG_ENTRY))
    {
        ExFreePoolWithTag(Output, TAG_WMIACPI);
        return STATUS_INVALID_PARAMETER;
    }

    Count = Output->Argument[0].DataLength / sizeof(WMIACPI_WDG_ENTRY);
    Entries = (PWMIACPI_WDG_ENTRY)Output->Argument[0].Data;

    Extension->Blocks = ExAllocatePoolZero(NonPagedPoolNx, Count * sizeof(WMIACPI_BLOCK), TAG_WMIACPI);
    Extension->GuidList = ExAllocatePoolZero(NonPagedPoolNx, Count * sizeof(WMIGUIDREGINFO), TAG_WMIACPI);
    if (!Extension->Blocks || !Extension->GuidList)
    {
        ExFreePoolWithTag(Output, TAG_WMIACPI);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Index = 0; Index < Count; Index++)
    {
        PWMIACPI_BLOCK Block = &Extension->Blocks[Index];

        RtlCopyMemory(&Block->Guid, &Entries[Index].Guid, sizeof(GUID));
        Block->ObjectId[0] = Entries[Index].ObjectId[0];
        Block->ObjectId[1] = Entries[Index].ObjectId[1];
        Block->NotifyId = Entries[Index].NotifyId;
        Block->Flags = Entries[Index].Flags;

        Extension->GuidList[Index].Guid = &Block->Guid;
        Extension->GuidList[Index].InstanceCount = (Block->Flags & WMIACPI_FLAG_EVENT) ?
                                                   1 : max(Entries[Index].InstanceCount, 1);
        if (Block->Flags & WMIACPI_FLAG_EVENT)
            Extension->GuidList[Index].Flags |= WMIREG_FLAG_EVENT_ONLY_GUID;
        if (Block->Flags & WMIACPI_FLAG_EXPENSIVE)
            Extension->GuidList[Index].Flags |= WMIREG_FLAG_EXPENSIVE;
    }

    Extension->BlockCount = Count;
    ExFreePoolWithTag(Output, TAG_WMIACPI);
    return STATUS_SUCCESS;
}

static
VOID
WmiAcpiStop(
    _In_ PWMIACPI_EXTENSION Extension)
{
    if (Extension->WmiRegistered)
    {
        IoWMIRegistrationControl(Extension->Self, WMIREG_ACTION_DEREGISTER);
        Extension->WmiRegistered = FALSE;
    }

    if (Extension->NotificationsRegistered)
    {
        Extension->AcpiInterface.UnregisterForDeviceNotifications(Extension->AcpiInterface.Context,
                                                                  WmiAcpiNotify);
        Extension->NotificationsRegistered = FALSE;
    }

    if (Extension->AcpiInterfaceValid)
    {
        Extension->AcpiInterface.InterfaceDereference(Extension->AcpiInterface.Context);
        Extension->AcpiInterfaceValid = FALSE;
    }
}

static
NTSTATUS
WmiAcpiStart(
    _In_ PWMIACPI_EXTENSION Extension)
{
    ULONG Index;
    BOOLEAN HasEvents = FALSE;
    NTSTATUS Status;

    if (!Extension->Blocks)
    {
        Status = WmiAcpiLoadBlocks(Extension);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("WMIACPI: _WDG evaluation failed: 0x%lx\n", Status);
            return STATUS_SUCCESS;
        }
    }

    Extension->WmiLibContext.GuidCount = Extension->BlockCount;
    Extension->WmiLibContext.GuidList = Extension->GuidList;
    Extension->WmiLibContext.QueryWmiRegInfo = WmiAcpiQueryRegInfo;
    Extension->WmiLibContext.QueryWmiDataBlock = WmiAcpiQueryDataBlock;
    Extension->WmiLibContext.SetWmiDataBlock = WmiAcpiSetDataBlock;
    Extension->WmiLibContext.ExecuteWmiMethod = WmiAcpiExecuteMethod;
    Extension->WmiLibContext.WmiFunctionControl = WmiAcpiFunctionControl;

    for (Index = 0; Index < Extension->BlockCount; Index++)
    {
        if (Extension->Blocks[Index].Flags & WMIACPI_FLAG_EVENT)
            HasEvents = TRUE;
    }

    if (HasEvents)
    {
        Status = WmiAcpiQueryAcpiInterface(Extension);
        if (NT_SUCCESS(Status))
        {
            Extension->AcpiInterfaceValid = TRUE;
            Status = Extension->AcpiInterface.RegisterForDeviceNotifications(Extension->AcpiInterface.Context,
                                                                             WmiAcpiNotify,
                                                                             Extension);
            Extension->NotificationsRegistered = NT_SUCCESS(Status);
        }
        if (!NT_SUCCESS(Status))
            DPRINT1("WMIACPI: event notifications unavailable: 0x%lx\n", Status);
    }

    Status = IoWMIRegistrationControl(Extension->Self, WMIREG_ACTION_REGISTER);
    Extension->WmiRegistered = NT_SUCCESS(Status);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
WmiAcpiStartCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
WmiAcpiPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PWMIACPI_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    KEVENT Event;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, WmiAcpiStartCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status))
                Status = WmiAcpiStart(Extension);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_STOP_DEVICE:
            WmiAcpiStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            WmiAcpiStop(Extension);
            IoReleaseRemoveLockAndWait(&Extension->RemoveLock, Irp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            IoDetachDevice(Extension->LowerDevice);
            if (Extension->Blocks)
                ExFreePoolWithTag(Extension->Blocks, TAG_WMIACPI);
            if (Extension->GuidList)
                ExFreePoolWithTag(Extension->GuidList, TAG_WMIACPI);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(Extension->LowerDevice, Irp);
    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
WmiAcpiPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PWMIACPI_EXTENSION Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
WmiAcpiSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PWMIACPI_EXTENSION Extension = DeviceObject->DeviceExtension;
    SYSCTL_IRP_DISPOSITION Disposition;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&Extension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    if (!Extension->WmiRegistered)
    {
        Disposition = IrpForward;
    }
    else
    {
        Status = WmiSystemControl(&Extension->WmiLibContext, DeviceObject, Irp, &Disposition);
    }

    switch (Disposition)
    {
        case IrpProcessed:
            break;

        case IrpNotCompleted:
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            break;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            break;
    }

    IoReleaseRemoveLock(&Extension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
WmiAcpiCreateClose(
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
WmiAcpiAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PWMIACPI_EXTENSION Extension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(WMIACPI_EXTENSION),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Extension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Self = DeviceObject;
    Extension->Pdo = PhysicalDeviceObject;
    IoInitializeRemoveLock(&Extension->RemoveLock, TAG_WMIACPI, 0, 0);

    Extension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
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
WmiAcpiUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (WmiAcpiRegistryPath.Buffer)
        ExFreePoolWithTag(WmiAcpiRegistryPath.Buffer, TAG_WMIACPI);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WmiAcpiRegistryPath.Buffer = ExAllocatePoolWithTag(NonPagedPoolNx,
                                                       RegistryPath->Length + sizeof(WCHAR),
                                                       TAG_WMIACPI);
    if (!WmiAcpiRegistryPath.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(WmiAcpiRegistryPath.Buffer, RegistryPath->Buffer, RegistryPath->Length);
    WmiAcpiRegistryPath.Length = RegistryPath->Length;
    WmiAcpiRegistryPath.MaximumLength = RegistryPath->Length + sizeof(WCHAR);
    WmiAcpiRegistryPath.Buffer[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    DriverObject->DriverExtension->AddDevice = WmiAcpiAddDevice;
    DriverObject->DriverUnload = WmiAcpiUnload;
    DriverObject->MajorFunction[IRP_MJ_PNP] = WmiAcpiPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = WmiAcpiPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = WmiAcpiSystemControl;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = WmiAcpiCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = WmiAcpiCreateClose;
    return STATUS_SUCCESS;
}
