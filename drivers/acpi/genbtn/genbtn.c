/*
 * PROJECT:     LiberNT ACPI Generic Button Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ACPI0011 generic button device reported through the system button interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <initguid.h>
#include <poclass.h>
#include <acpiioct.h>

#define NDEBUG
#include <debug.h>

#define GENBTN_TAG              'nBnG'
#define GENBTN_MAX_BUTTONS      8
#define GENBTN_MAX_OUTPUT       (64 * 1024)
#define GENBTN_METHOD_DSD       'DSD_'
#define GENBTN_DESCRIPTOR_FIELDS 5
#define GENBTN_CONTROL          1
#define GENBTN_USAGE_PAGE_GENERIC 0x01
#define GENBTN_USAGE_POWER_DOWN 0x81
#define GENBTN_USAGE_SLEEP      0x82
#define GENBTN_USAGE_WAKE_UP    0x83

static const GUID GenBtnDescriptorUuid =
    { 0xfa6bd625, 0x9ce8, 0x470d, { 0xa2, 0xc7, 0xb3, 0xca, 0x36, 0xc4, 0x28, 0x2e } };

struct _GENBTN_EXTENSION;

typedef struct _GENBTN_BUTTON
{
    struct _GENBTN_EXTENSION *Extension;
    ULONG InterruptIndex;
    ULONG Event;
    PKINTERRUPT Interrupt;
    BOOLEAN Pressed;
} GENBTN_BUTTON, *PGENBTN_BUTTON;

typedef struct _GENBTN_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    KSPIN_LOCK Lock;
    KDPC Dpc;
    PIRP PendingIrp;
    ULONG PendingEvents;
    volatile LONG DpcEvents;
    ULONG Capabilities;
    ULONG ButtonCount;
    GENBTN_BUTTON Buttons[GENBTN_MAX_BUTTONS];
    UNICODE_STRING InterfaceName;
    BOOLEAN Started;
} GENBTN_EXTENSION, *PGENBTN_EXTENSION;

DRIVER_INITIALIZE DriverEntry;
static DRIVER_ADD_DEVICE GenBtnAddDevice;
static DRIVER_UNLOAD GenBtnUnload;
static DRIVER_DISPATCH GenBtnCreateClose;
static DRIVER_DISPATCH GenBtnDeviceControl;
static DRIVER_DISPATCH GenBtnPnp;
static DRIVER_DISPATCH GenBtnPower;
static DRIVER_DISPATCH GenBtnSystemControl;
static DRIVER_CANCEL GenBtnCancel;
static KDEFERRED_ROUTINE GenBtnDpc;
static KSERVICE_ROUTINE GenBtnIsr;
static IO_COMPLETION_ROUTINE GenBtnCompletion;

static
NTSTATUS
NTAPI
GenBtnCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
GenBtnForwardSynchronously(
    _In_ PGENBTN_EXTENSION Extension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, GenBtnCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(Extension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
GenBtnEvaluateDsd(
    _In_ PGENBTN_EXTENSION Extension,
    _Outptr_result_bytebuffer_(*OutputLength) PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength)
{
    ACPI_EVAL_INPUT_BUFFER Input;
    PACPI_EVAL_OUTPUT_BUFFER Buffer;
    ULONG BufferLength = PAGE_SIZE;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    *OutputBuffer = NULL;
    *OutputLength = 0;
    RtlZeroMemory(&Input, sizeof(Input));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    Input.MethodNameAsUlong = GENBTN_METHOD_DSD;

    for (;;)
    {
        Buffer = ExAllocatePoolZero(PagedPool, BufferLength, GENBTN_TAG);
        if (!Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;
        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD, Extension->LowerDevice,
                                            &Input, sizeof(Input), Buffer, BufferLength,
                                            FALSE, &Event, &IoStatus);
        if (!Irp)
        {
            ExFreePoolWithTag(Buffer, GENBTN_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Status = IoCallDriver(Extension->LowerDevice, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatus.Status;
        }
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;
        ExFreePoolWithTag(Buffer, GENBTN_TAG);
        if (IoStatus.Information <= BufferLength || IoStatus.Information > GENBTN_MAX_OUTPUT)
            return STATUS_ACPI_INVALID_DATA;
        BufferLength = (ULONG)IoStatus.Information;
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, GENBTN_TAG);
        return Status;
    }
    if (Buffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE ||
        Buffer->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) ||
        Buffer->Length > BufferLength)
    {
        ExFreePoolWithTag(Buffer, GENBTN_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }
    *OutputBuffer = Buffer;
    *OutputLength = Buffer->Length;
    return STATUS_SUCCESS;
}

static
BOOLEAN
GenBtnArgumentFits(
    _In_ PACPI_METHOD_ARGUMENT Argument,
    _In_ PUCHAR End)
{
    if ((PUCHAR)Argument + ACPI_METHOD_ARGUMENT_LENGTH(0) > End)
        return FALSE;
    return (PUCHAR)Argument + ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Argument) <= End;
}

static
BOOLEAN
GenBtnReadInteger(
    _In_ PACPI_METHOD_ARGUMENT Argument,
    _Out_ PULONG Value)
{
    if (Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER || Argument->DataLength < sizeof(ULONG))
        return FALSE;
    *Value = *(PULONG)Argument->Data;
    return TRUE;
}

static
ULONG
GenBtnUsageEvent(
    _In_ ULONG UsagePage,
    _In_ ULONG Usage)
{
    if (UsagePage != GENBTN_USAGE_PAGE_GENERIC)
        return 0;
    switch (Usage)
    {
        case GENBTN_USAGE_POWER_DOWN:
            return SYS_BUTTON_POWER;
        case GENBTN_USAGE_SLEEP:
            return SYS_BUTTON_SLEEP;
        case GENBTN_USAGE_WAKE_UP:
            return SYS_BUTTON_WAKE;
        default:
            return 0;
    }
}

static
VOID
GenBtnParseDescriptor(
    _Inout_ PGENBTN_EXTENSION Extension,
    _In_ PACPI_METHOD_ARGUMENT Descriptor)
{
    PACPI_METHOD_ARGUMENT Field = (PACPI_METHOD_ARGUMENT)Descriptor->Data;
    PUCHAR End = Descriptor->Data + Descriptor->DataLength;
    ULONG Values[GENBTN_DESCRIPTOR_FIELDS];
    PGENBTN_BUTTON Button;
    ULONG Event;
    ULONG Index;

    if (Descriptor->Type != ACPI_METHOD_ARGUMENT_PACKAGE)
        return;
    for (Index = 0; Index < GENBTN_DESCRIPTOR_FIELDS; Index++)
    {
        if (!GenBtnArgumentFits(Field, End) || !GenBtnReadInteger(Field, &Values[Index]))
            return;
        Field = ACPI_METHOD_NEXT_ARGUMENT(Field);
    }
    if (Values[0] != GENBTN_CONTROL)
        return;

    Event = GenBtnUsageEvent(Values[3], Values[4]);
    if (Event == 0 || Extension->ButtonCount >= GENBTN_MAX_BUTTONS)
        return;

    Button = &Extension->Buttons[Extension->ButtonCount++];
    Button->Extension = Extension;
    Button->InterruptIndex = Values[1];
    Button->Event = Event;
    Button->Interrupt = NULL;
    Button->Pressed = FALSE;
    DPRINT1("GENBTN: control usage %02lx:%02lx on interrupt %lu\n", Values[3], Values[4], Values[1]);
}

static
VOID
GenBtnParseDsd(
    _Inout_ PGENBTN_EXTENSION Extension,
    _In_ PACPI_EVAL_OUTPUT_BUFFER Output)
{
    PACPI_METHOD_ARGUMENT Argument = Output->Argument;
    PUCHAR End = (PUCHAR)Output + Output->Length;
    PACPI_METHOD_ARGUMENT Descriptor;
    PUCHAR DescriptorsEnd;
    BOOLEAN Match;
    ULONG Index;

    for (Index = 0; Index + 1 < Output->Count; Index += 2)
    {
        PACPI_METHOD_ARGUMENT Data;

        if (!GenBtnArgumentFits(Argument, End))
            return;
        Data = ACPI_METHOD_NEXT_ARGUMENT(Argument);
        if (!GenBtnArgumentFits(Data, End))
            return;

        Match = Argument->Type == ACPI_METHOD_ARGUMENT_BUFFER &&
                Argument->DataLength == sizeof(GUID) &&
                RtlCompareMemory(Argument->Data, &GenBtnDescriptorUuid, sizeof(GUID)) == sizeof(GUID);
        if (Match && Data->Type == ACPI_METHOD_ARGUMENT_PACKAGE)
        {
            Descriptor = (PACPI_METHOD_ARGUMENT)Data->Data;
            DescriptorsEnd = Data->Data + Data->DataLength;
            while (GenBtnArgumentFits(Descriptor, DescriptorsEnd))
            {
                GenBtnParseDescriptor(Extension, Descriptor);
                Descriptor = ACPI_METHOD_NEXT_ARGUMENT(Descriptor);
            }
        }
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Data);
    }
}

static
VOID
GenBtnReport(
    _In_ PGENBTN_EXTENSION Extension,
    _In_ ULONG Events)
{
    PIRP Irp;
    KIRQL Irql;

    KeAcquireSpinLock(&Extension->Lock, &Irql);
    Irp = Extension->PendingIrp;
    if (Irp && IoSetCancelRoutine(Irp, NULL))
    {
        Extension->PendingIrp = NULL;
        Events |= Extension->PendingEvents;
        Extension->PendingEvents = 0;
    }
    else
    {
        Irp = NULL;
        Extension->PendingEvents |= Events;
    }
    KeReleaseSpinLock(&Extension->Lock, Irql);

    if (Irp)
    {
        *(PULONG)Irp->AssociatedIrp.SystemBuffer = Events;
        Irp->IoStatus.Information = sizeof(ULONG);
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
VOID
NTAPI
GenBtnDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PGENBTN_EXTENSION Extension = DeferredContext;
    ULONG Events;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Events = (ULONG)InterlockedExchange(&Extension->DpcEvents, 0);
    if (Events)
        GenBtnReport(Extension, Events);
}

static
BOOLEAN
NTAPI
GenBtnIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PGENBTN_BUTTON Button = ServiceContext;
    PGENBTN_EXTENSION Extension = Button->Extension;

    UNREFERENCED_PARAMETER(Interrupt);

    Button->Pressed = !Button->Pressed;
    if (Button->Pressed && Extension->Started)
    {
        InterlockedOr(&Extension->DpcEvents, (LONG)Button->Event);
        KeInsertQueueDpc(&Extension->Dpc, NULL, NULL);
    }
    return TRUE;
}

static
VOID
GenBtnDisconnect(
    _Inout_ PGENBTN_EXTENSION Extension)
{
    ULONG Index;

    for (Index = 0; Index < Extension->ButtonCount; Index++)
    {
        if (Extension->Buttons[Index].Interrupt)
        {
            IoDisconnectInterrupt(Extension->Buttons[Index].Interrupt);
            Extension->Buttons[Index].Interrupt = NULL;
        }
    }
    KeFlushQueuedDpcs();
}

static
NTSTATUS
GenBtnConnect(
    _Inout_ PGENBTN_EXTENSION Extension,
    _In_ PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_LIST List;
    ULONG InterruptIndex = 0;
    ULONG Index;
    ULONG Button;
    NTSTATUS Status;

    if (!Resources || Resources->Count == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    List = &Resources->List[0].PartialResourceList;

    for (Index = 0; Index < List->Count; Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = &List->PartialDescriptors[Index];

        if (Descriptor->Type != CmResourceTypeInterrupt)
            continue;
        for (Button = 0; Button < Extension->ButtonCount; Button++)
        {
            PGENBTN_BUTTON Entry = &Extension->Buttons[Button];

            if (Entry->InterruptIndex != InterruptIndex || Entry->Interrupt)
                continue;
            Status = IoConnectInterrupt(&Entry->Interrupt, GenBtnIsr, Entry, NULL,
                                        Descriptor->u.Interrupt.Vector,
                                        (KIRQL)Descriptor->u.Interrupt.Level,
                                        (KIRQL)Descriptor->u.Interrupt.Level,
                                        (Descriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive,
                                        Descriptor->ShareDisposition == CmResourceShareShared,
                                        Descriptor->u.Interrupt.Affinity, FALSE);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("GENBTN: interrupt %lu not connected 0x%08lx\n", InterruptIndex, Status);
                Entry->Interrupt = NULL;
                continue;
            }
            Extension->Capabilities |= Entry->Event;
        }
        InterruptIndex++;
    }
    return Extension->Capabilities ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static
NTSTATUS
GenBtnStart(
    _Inout_ PGENBTN_EXTENSION Extension,
    _In_ PIO_STACK_LOCATION Stack)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    ULONG OutputLength;
    NTSTATUS Status;

    Extension->ButtonCount = 0;
    Extension->Capabilities = 0;
    Status = GenBtnEvaluateDsd(Extension, &Output, &OutputLength);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("GENBTN: _DSD evaluation failed 0x%08lx\n", Status);
        return Status;
    }
    GenBtnParseDsd(Extension, Output);
    ExFreePoolWithTag(Output, GENBTN_TAG);
    if (Extension->ButtonCount == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Extension->Started = TRUE;
    Status = GenBtnConnect(Extension, Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
    if (!NT_SUCCESS(Status))
    {
        Extension->Started = FALSE;
        GenBtnDisconnect(Extension);
        return Status;
    }

    Status = IoRegisterDeviceInterface(Extension->PhysicalDevice, &GUID_DEVICE_SYS_BUTTON, NULL, &Extension->InterfaceName);
    if (NT_SUCCESS(Status))
        Status = IoSetDeviceInterfaceState(&Extension->InterfaceName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        Extension->Started = FALSE;
        GenBtnDisconnect(Extension);
        if (Extension->InterfaceName.Buffer)
            RtlFreeUnicodeString(&Extension->InterfaceName);
        return Status;
    }
    DPRINT1("GENBTN: buttons 0x%lx ready\n", Extension->Capabilities);
    return STATUS_SUCCESS;
}

static
VOID
GenBtnStop(
    _Inout_ PGENBTN_EXTENSION Extension)
{
    PIRP Irp = NULL;
    KIRQL Irql;

    if (Extension->InterfaceName.Buffer)
    {
        if (!NT_SUCCESS(IoSetDeviceInterfaceState(&Extension->InterfaceName, FALSE)))
            DPRINT1("GENBTN: interface %wZ not disabled\n", &Extension->InterfaceName);
        RtlFreeUnicodeString(&Extension->InterfaceName);
    }
    Extension->Started = FALSE;
    GenBtnDisconnect(Extension);

    KeAcquireSpinLock(&Extension->Lock, &Irql);
    if (Extension->PendingIrp && IoSetCancelRoutine(Extension->PendingIrp, NULL))
    {
        Irp = Extension->PendingIrp;
        Extension->PendingIrp = NULL;
    }
    Extension->PendingEvents = 0;
    KeReleaseSpinLock(&Extension->Lock, Irql);
    if (Irp)
    {
        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

static
VOID
NTAPI
GenBtnCancel(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PGENBTN_EXTENSION Extension = DeviceObject->DeviceExtension;
    KIRQL Irql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&Extension->Lock, &Irql);
    if (Extension->PendingIrp == Irp)
        Extension->PendingIrp = NULL;
    KeReleaseSpinLock(&Extension->Lock, Irql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
NTSTATUS
NTAPI
GenBtnDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PGENBTN_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Events;
    KIRQL Irql;
    NTSTATUS Status;

    if (Stack->Parameters.DeviceIoControl.IoControlCode != IOCTL_GET_SYS_BUTTON_CAPS &&
        Stack->Parameters.DeviceIoControl.IoControlCode != IOCTL_GET_SYS_BUTTON_EVENT)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Extension->LowerDevice, Irp);
    }

    if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else if (!Extension->Started)
    {
        Status = STATUS_DEVICE_NOT_READY;
    }
    else if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_GET_SYS_BUTTON_CAPS)
    {
        *(PULONG)Irp->AssociatedIrp.SystemBuffer = Extension->Capabilities;
        Irp->IoStatus.Information = sizeof(ULONG);
        Status = STATUS_SUCCESS;
    }
    else
    {
        KeAcquireSpinLock(&Extension->Lock, &Irql);
        Events = Extension->PendingEvents;
        if (Events)
        {
            Extension->PendingEvents = 0;
            KeReleaseSpinLock(&Extension->Lock, Irql);
            *(PULONG)Irp->AssociatedIrp.SystemBuffer = Events;
            Irp->IoStatus.Information = sizeof(ULONG);
            Status = STATUS_SUCCESS;
        }
        else if (Extension->PendingIrp)
        {
            KeReleaseSpinLock(&Extension->Lock, Irql);
            Status = STATUS_DEVICE_BUSY;
        }
        else
        {
            IoMarkIrpPending(Irp);
            IoSetCancelRoutine(Irp, GenBtnCancel);
            if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
            {
                KeReleaseSpinLock(&Extension->Lock, Irql);
                Irp->IoStatus.Status = STATUS_CANCELLED;
                Irp->IoStatus.Information = 0;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_PENDING;
            }
            Extension->PendingIrp = Irp;
            KeReleaseSpinLock(&Extension->Lock, Irql);
            return STATUS_PENDING;
        }
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
GenBtnCreateClose(
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
GenBtnPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PGENBTN_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT LowerDevice = Extension->LowerDevice;
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = GenBtnForwardSynchronously(Extension, Irp);
            if (NT_SUCCESS(Status))
                Status = GenBtnStart(Extension, Stack);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            GenBtnStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            GenBtnStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(LowerDevice, Irp);
            IoDetachDevice(LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
GenBtnPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PGENBTN_EXTENSION Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
GenBtnSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PGENBTN_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
GenBtnAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PGENBTN_EXTENSION Extension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(GENBTN_EXTENSION), NULL, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Extension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Self = DeviceObject;
    Extension->PhysicalDevice = PhysicalDeviceObject;
    KeInitializeSpinLock(&Extension->Lock);
    KeInitializeDpc(&Extension->Dpc, GenBtnDpc, Extension);

    Extension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
GenBtnUnload(
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

    DriverObject->DriverExtension->AddDevice = GenBtnAddDevice;
    DriverObject->DriverUnload = GenBtnUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = GenBtnCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = GenBtnCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = GenBtnDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = GenBtnPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = GenBtnPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = GenBtnSystemControl;
    return STATUS_SUCCESS;
}
