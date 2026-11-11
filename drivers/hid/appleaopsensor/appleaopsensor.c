/*
 * PROJECT:     LiberNT Apple AOP Sensor HID Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     HID sensor collections for the ambient light and lid angle sensors behind the Apple AOP
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <hidport.h>
#include <initguid.h>
#include <reactos/drivers/appleaop.h>

#define SENSOR_TAG 'sOpA'

#define HID_USAGE_PAGE_SENSOR                           0x0020
#define HID_USAGE_SENSORS_LIGHT_AMBIENT_LIGHT           0x0041
#define HID_USAGE_SENSORS_OTHER_HINGE_ANGLE             0x00E4
#define HID_USAGE_SENSORS_EVENT_SENSOR_STATE            0x0201
#define HID_USAGE_SENSORS_EVENT_SENSOR_EVENT            0x0202
#define HID_USAGE_SENSORS_PROPERTY_REPORT_INTERVAL      0x030E
#define HID_USAGE_SENSORS_PROPERTY_REPORTING_STATE      0x0316
#define HID_USAGE_SENSORS_PROPERTY_POWER_STATE          0x0319
#define HID_USAGE_SENSORS_DATA_FIELD_ILLUMINANCE        0x04D1
#define HID_USAGE_SENSORS_DATA_FIELD_HINGE_ANGLE        0x05E1
#define HID_USAGE_SENSORS_SENSOR_STATE_UNDEFINED        0x0800
#define HID_USAGE_SENSORS_SENSOR_EVENT_UNKNOWN          0x0810
#define HID_USAGE_SENSORS_REPORTING_STATE_REPORT_NO_EVENTS 0x0840
#define HID_USAGE_SENSORS_POWER_STATE_UNDEFINED         0x0850

#define HID_ITEM_USAGE_PAGE(Page)       0x05, (UCHAR)(Page)
#define HID_ITEM_USAGE(Usage)           0x0a, (UCHAR)((Usage) & 0xff), (UCHAR)(((Usage) >> 8) & 0xff)
#define HID_ITEM_COLLECTION(Type)       0xa1, (UCHAR)(Type)
#define HID_ITEM_END_COLLECTION         0xc0
#define HID_ITEM_REPORT_ID(Id)          0x85, (UCHAR)(Id)
#define HID_ITEM_LOGICAL_MIN(Value)     0x15, (UCHAR)(Value)
#define HID_ITEM_LOGICAL_MAX(Value)     0x25, (UCHAR)(Value)
#define HID_ITEM_LOGICAL_MAX32(Value)   0x27, (UCHAR)((Value) & 0xff), (UCHAR)(((Value) >> 8) & 0xff), \
                                        (UCHAR)(((Value) >> 16) & 0xff), (UCHAR)(((Value) >> 24) & 0xff)
#define HID_ITEM_REPORT_SIZE(Bits)      0x75, (UCHAR)(Bits)
#define HID_ITEM_REPORT_COUNT(Count)    0x95, (UCHAR)(Count)
#define HID_ITEM_UNIT8(Unit)            0x65, (UCHAR)(Unit)
#define HID_ITEM_UNIT32(Unit)           0x67, (UCHAR)((Unit) & 0xff), (UCHAR)(((Unit) >> 8) & 0xff), \
                                        (UCHAR)(((Unit) >> 16) & 0xff), (UCHAR)(((Unit) >> 24) & 0xff)
#define HID_ITEM_UNIT_EXPONENT(Value)   0x55, (UCHAR)(Value)
#define HID_ITEM_INPUT(Flags)           0x81, (UCHAR)(Flags)
#define HID_ITEM_FEATURE(Flags)         0xb1, (UCHAR)(Flags)

#define HID_COLLECTION_PHYSICAL         0x00
#define HID_COLLECTION_LOGICAL          0x02
#define HID_MAIN_DATA_ARRAY_ABS         0x00
#define HID_MAIN_DATA_VAR_ABS           0x02
#define HID_UNIT_LUX                    0x010000E1UL
#define HID_UNIT_DEGREES                0x14

#define SENSOR_STATES                   7
#define SENSOR_EVENTS                   17
#define SENSOR_REPORTING_STATES         6
#define SENSOR_POWER_STATES             6
#define SENSOR_STATE_READY              1
#define SENSOR_STATE_NO_DATA            3
#define SENSOR_EVENT_DATA_UPDATED       3
#define SENSOR_EVENT_POLL_RESPONSE      4
#define SENSOR_REPORTING_NO_EVENTS      0
#define SENSOR_REPORTING_ALL_EVENTS     1
#define SENSOR_POWER_D0                 1
#define SENSOR_REPORT_ID                1
#define SENSOR_DEFAULT_INTERVAL_MS      200
#define SENSOR_ANGLE_MAX                360
#define SENSOR_VALUE_MAX                0x7fffffffUL

#define HID_ITEM_SELECTORS_7(First) \
    HID_ITEM_USAGE(First), HID_ITEM_USAGE((First) + 1), HID_ITEM_USAGE((First) + 2), HID_ITEM_USAGE((First) + 3), \
    HID_ITEM_USAGE((First) + 4), HID_ITEM_USAGE((First) + 5), HID_ITEM_USAGE((First) + 6)
#define HID_ITEM_SELECTORS_6(First) \
    HID_ITEM_USAGE(First), HID_ITEM_USAGE((First) + 1), HID_ITEM_USAGE((First) + 2), HID_ITEM_USAGE((First) + 3), \
    HID_ITEM_USAGE((First) + 4), HID_ITEM_USAGE((First) + 5)
#define HID_ITEM_SELECTORS_17(First) \
    HID_ITEM_SELECTORS_7(First), HID_ITEM_SELECTORS_7((First) + 7), HID_ITEM_USAGE((First) + 14), \
    HID_ITEM_USAGE((First) + 15), HID_ITEM_USAGE((First) + 16)

#define HID_SENSOR_NARY(Usage, Count, Selectors, Main) \
    HID_ITEM_USAGE(Usage), HID_ITEM_LOGICAL_MIN(0), HID_ITEM_LOGICAL_MAX((Count) - 1), HID_ITEM_REPORT_SIZE(8), \
    HID_ITEM_REPORT_COUNT(1), HID_ITEM_COLLECTION(HID_COLLECTION_LOGICAL), Selectors, Main(HID_MAIN_DATA_ARRAY_ABS), \
    HID_ITEM_END_COLLECTION

#define HID_SENSOR_COMMON \
    HID_ITEM_REPORT_ID(SENSOR_REPORT_ID), \
    HID_SENSOR_NARY(HID_USAGE_SENSORS_PROPERTY_REPORTING_STATE, SENSOR_REPORTING_STATES, \
                    HID_ITEM_SELECTORS_6(HID_USAGE_SENSORS_REPORTING_STATE_REPORT_NO_EVENTS), HID_ITEM_FEATURE), \
    HID_SENSOR_NARY(HID_USAGE_SENSORS_PROPERTY_POWER_STATE, SENSOR_POWER_STATES, \
                    HID_ITEM_SELECTORS_6(HID_USAGE_SENSORS_POWER_STATE_UNDEFINED), HID_ITEM_FEATURE), \
    HID_SENSOR_NARY(HID_USAGE_SENSORS_EVENT_SENSOR_STATE, SENSOR_STATES, \
                    HID_ITEM_SELECTORS_7(HID_USAGE_SENSORS_SENSOR_STATE_UNDEFINED), HID_ITEM_FEATURE), \
    HID_ITEM_USAGE(HID_USAGE_SENSORS_PROPERTY_REPORT_INTERVAL), HID_ITEM_LOGICAL_MIN(0), \
    HID_ITEM_LOGICAL_MAX32(SENSOR_VALUE_MAX), HID_ITEM_REPORT_SIZE(32), HID_ITEM_REPORT_COUNT(1), \
    HID_ITEM_UNIT_EXPONENT(0), HID_ITEM_FEATURE(HID_MAIN_DATA_VAR_ABS), \
    HID_SENSOR_NARY(HID_USAGE_SENSORS_EVENT_SENSOR_STATE, SENSOR_STATES, \
                    HID_ITEM_SELECTORS_7(HID_USAGE_SENSORS_SENSOR_STATE_UNDEFINED), HID_ITEM_INPUT), \
    HID_SENSOR_NARY(HID_USAGE_SENSORS_EVENT_SENSOR_EVENT, SENSOR_EVENTS, \
                    HID_ITEM_SELECTORS_17(HID_USAGE_SENSORS_SENSOR_EVENT_UNKNOWN), HID_ITEM_INPUT)

static const UCHAR SensorAmbientLightDescriptor[] =
{
    HID_ITEM_USAGE_PAGE(HID_USAGE_PAGE_SENSOR),
    HID_ITEM_USAGE(HID_USAGE_SENSORS_LIGHT_AMBIENT_LIGHT),
    HID_ITEM_COLLECTION(HID_COLLECTION_PHYSICAL),
    HID_SENSOR_COMMON,
    HID_ITEM_USAGE(HID_USAGE_SENSORS_DATA_FIELD_ILLUMINANCE), HID_ITEM_LOGICAL_MIN(0),
    HID_ITEM_LOGICAL_MAX32(SENSOR_VALUE_MAX), HID_ITEM_UNIT32(HID_UNIT_LUX), HID_ITEM_UNIT_EXPONENT(0),
    HID_ITEM_REPORT_SIZE(32), HID_ITEM_REPORT_COUNT(1), HID_ITEM_INPUT(HID_MAIN_DATA_VAR_ABS),
    HID_ITEM_UNIT8(0),
    HID_ITEM_END_COLLECTION,
};

static const UCHAR SensorLidAngleDescriptor[] =
{
    HID_ITEM_USAGE_PAGE(HID_USAGE_PAGE_SENSOR),
    HID_ITEM_USAGE(HID_USAGE_SENSORS_OTHER_HINGE_ANGLE),
    HID_ITEM_COLLECTION(HID_COLLECTION_PHYSICAL),
    HID_SENSOR_COMMON,
    HID_ITEM_USAGE(HID_USAGE_SENSORS_DATA_FIELD_HINGE_ANGLE), HID_ITEM_LOGICAL_MIN(0),
    HID_ITEM_LOGICAL_MAX32(SENSOR_ANGLE_MAX), HID_ITEM_UNIT8(HID_UNIT_DEGREES), HID_ITEM_UNIT_EXPONENT(0),
    HID_ITEM_REPORT_SIZE(32), HID_ITEM_REPORT_COUNT(1), HID_ITEM_INPUT(HID_MAIN_DATA_VAR_ABS),
    HID_ITEM_UNIT8(0),
    HID_ITEM_END_COLLECTION,
};

#pragma pack(push, 1)
typedef struct _SENSOR_FEATURE_REPORT
{
    UCHAR ReportId;
    UCHAR ReportingState;
    UCHAR PowerState;
    UCHAR SensorState;
    ULONG ReportInterval;
} SENSOR_FEATURE_REPORT, *PSENSOR_FEATURE_REPORT;

typedef struct _SENSOR_INPUT_REPORT
{
    UCHAR ReportId;
    UCHAR SensorState;
    UCHAR SensorEvent;
    ULONG Value;
} SENSOR_INPUT_REPORT, *PSENSOR_INPUT_REPORT;
#pragma pack(pop)

C_ASSERT(sizeof(SENSOR_FEATURE_REPORT) == 8);
C_ASSERT(sizeof(SENSOR_INPUT_REPORT) == 7);

typedef struct _SENSOR_DEVICE
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT Pdo;
    APPLE_AOP_FUNCTION_INTERFACE Interface;
    BOOLEAN InterfaceHeld;
    BOOLEAN Subscribed;
    const UCHAR *Descriptor;
    ULONG DescriptorLength;
    USHORT ProductId;

    KSPIN_LOCK Lock;
    LIST_ENTRY PendingReads;
    BOOLEAN Started;
    BOOLEAN HaveValue;
    BOOLEAN Undelivered;
    ULONG Value;
    UCHAR ReportingState;
    UCHAR PowerState;
    ULONG ReportInterval;
} SENSOR_DEVICE, *PSENSOR_DEVICE;

static
PSENSOR_DEVICE
SensorGetDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return ((PHID_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->MiniDeviceExtension;
}

static
VOID
SensorBuildInput(
    _In_ PSENSOR_DEVICE Device,
    _In_ UCHAR Event,
    _Out_ PSENSOR_INPUT_REPORT Report)
{
    Report->ReportId = SENSOR_REPORT_ID;
    Report->SensorState = Device->HaveValue ? SENSOR_STATE_READY : SENSOR_STATE_NO_DATA;
    Report->SensorEvent = Event;
    Report->Value = Device->Value;
}

static
VOID
SensorCompleteRead(
    _Inout_ PIRP Irp,
    _In_ const SENSOR_INPUT_REPORT *Report)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Length = min((ULONG)sizeof(*Report), Stack->Parameters.DeviceIoControl.OutputBufferLength);

    RtlCopyMemory(Irp->UserBuffer, Report, Length);
    Irp->IoStatus.Information = Length;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
PIRP
SensorTakeRead(
    _Inout_ PSENSOR_DEVICE Device)
{
    PLIST_ENTRY Entry;
    PIRP Irp;

    while (!IsListEmpty(&Device->PendingReads))
    {
        Entry = RemoveHeadList(&Device->PendingReads);
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        if (IoSetCancelRoutine(Irp, NULL))
            return Irp;
        InitializeListHead(&Irp->Tail.Overlay.ListEntry);
    }
    return NULL;
}

static
VOID
NTAPI
SensorSample(
    _In_ PVOID Context,
    _In_ ULONG Value)
{
    PSENSOR_DEVICE Device = Context;
    SENSOR_INPUT_REPORT Report;
    PIRP Irp = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Device->Value = Value;
    Device->HaveValue = TRUE;
    if (Device->Started && Device->ReportingState != SENSOR_REPORTING_NO_EVENTS)
    {
        Irp = SensorTakeRead(Device);
        Device->Undelivered = (Irp == NULL);
        SensorBuildInput(Device, SENSOR_EVENT_DATA_UPDATED, &Report);
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    if (Irp)
        SensorCompleteRead(Irp, &Report);
}

static
VOID
NTAPI
SensorCancelRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSENSOR_DEVICE Device = SensorGetDevice(DeviceObject);
    KIRQL OldIrql;

    IoReleaseCancelSpinLock(Irp->CancelIrql);
    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
NTSTATUS
SensorReadReport(
    _Inout_ PSENSOR_DEVICE Device,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    SENSOR_INPUT_REPORT Report;
    KIRQL OldIrql;

    if (!Irp->UserBuffer || Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(Report))
    {
        Irp->IoStatus.Status = STATUS_INVALID_USER_BUFFER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_USER_BUFFER;
    }
    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (!Device->Started)
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }
    if (Device->Undelivered && Device->ReportingState != SENSOR_REPORTING_NO_EVENTS)
    {
        Device->Undelivered = FALSE;
        SensorBuildInput(Device, SENSOR_EVENT_DATA_UPDATED, &Report);
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        SensorCompleteRead(Irp, &Report);
        return STATUS_SUCCESS;
    }
    IoMarkIrpPending(Irp);
    InsertTailList(&Device->PendingReads, &Irp->Tail.Overlay.ListEntry);
    IoSetCancelRoutine(Irp, SensorCancelRead);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
    {
        RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_PENDING;
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    return STATUS_PENDING;
}

static
NTSTATUS
SensorCompleteWithBuffer(
    _Inout_ PIRP Irp,
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Irp->UserBuffer || Stack->Parameters.DeviceIoControl.OutputBufferLength < Length)
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
SensorTransfer(
    _Inout_ PSENSOR_DEVICE Device,
    _Inout_ PIRP Irp,
    _In_ ULONG Code)
{
    PHID_XFER_PACKET Packet = Irp->UserBuffer;
    SENSOR_FEATURE_REPORT Feature;
    SENSOR_INPUT_REPORT Input;
    KIRQL OldIrql;

    if (!Packet || !Packet->reportBuffer || Packet->reportId != SENSOR_REPORT_ID)
        return STATUS_INVALID_PARAMETER;
    if (Code == IOCTL_HID_SET_FEATURE)
    {
        if (Packet->reportBufferLen < sizeof(Feature))
            return STATUS_INVALID_BUFFER_SIZE;
        RtlCopyMemory(&Feature, Packet->reportBuffer, sizeof(Feature));
        if (Feature.ReportingState >= SENSOR_REPORTING_STATES || Feature.PowerState >= SENSOR_POWER_STATES)
            return STATUS_INVALID_PARAMETER;
        KeAcquireSpinLock(&Device->Lock, &OldIrql);
        Device->ReportingState = Feature.ReportingState;
        Device->PowerState = Feature.PowerState;
        /* TODO: forward the report interval to the AOP, which keeps the one it was enabled with */
        Device->ReportInterval = Feature.ReportInterval;
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        Irp->IoStatus.Information = sizeof(Feature);
        return STATUS_SUCCESS;
    }
    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Feature.ReportId = SENSOR_REPORT_ID;
    Feature.ReportingState = Device->ReportingState;
    Feature.PowerState = Device->PowerState;
    Feature.SensorState = Device->HaveValue ? SENSOR_STATE_READY : SENSOR_STATE_NO_DATA;
    Feature.ReportInterval = Device->ReportInterval;
    SensorBuildInput(Device, SENSOR_EVENT_POLL_RESPONSE, &Input);
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    if (Code == IOCTL_HID_GET_FEATURE)
    {
        if (Packet->reportBufferLen < sizeof(Feature))
            return STATUS_BUFFER_TOO_SMALL;
        RtlCopyMemory(Packet->reportBuffer, &Feature, sizeof(Feature));
        Irp->IoStatus.Information = sizeof(Feature);
        return STATUS_SUCCESS;
    }
    if (Packet->reportBufferLen < sizeof(Input))
        return STATUS_BUFFER_TOO_SMALL;
    RtlCopyMemory(Packet->reportBuffer, &Input, sizeof(Input));
    Irp->IoStatus.Information = sizeof(Input);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
SensorInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSENSOR_DEVICE Device = SensorGetDevice(DeviceObject);
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG Code = Stack->Parameters.DeviceIoControl.IoControlCode;
    NTSTATUS Status;

    if (Code == IOCTL_HID_READ_REPORT)
        return SensorReadReport(Device, Irp);
    Irp->IoStatus.Information = 0;
    if (!Device->Started)
    {
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_READY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_NOT_READY;
    }
    switch (Code)
    {
        case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        {
            HID_DESCRIPTOR Descriptor;

            RtlZeroMemory(&Descriptor, sizeof(Descriptor));
            Descriptor.bLength = sizeof(Descriptor);
            Descriptor.bDescriptorType = HID_HID_DESCRIPTOR_TYPE;
            Descriptor.bcdHID = 0x0111;
            Descriptor.bNumDescriptors = 1;
            Descriptor.DescriptorList[0].bReportType = HID_REPORT_DESCRIPTOR_TYPE;
            Descriptor.DescriptorList[0].wReportLength = (USHORT)Device->DescriptorLength;
            return SensorCompleteWithBuffer(Irp, &Descriptor, sizeof(Descriptor));
        }

        case IOCTL_HID_GET_REPORT_DESCRIPTOR:
            return SensorCompleteWithBuffer(Irp, Device->Descriptor, Device->DescriptorLength);

        case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
        {
            HID_DEVICE_ATTRIBUTES Attributes;

            RtlZeroMemory(&Attributes, sizeof(Attributes));
            Attributes.Size = sizeof(Attributes);
            Attributes.VendorID = 0x05ac;
            Attributes.ProductID = Device->ProductId;
            return SensorCompleteWithBuffer(Irp, &Attributes, sizeof(Attributes));
        }

        case IOCTL_HID_GET_FEATURE:
        case IOCTL_HID_SET_FEATURE:
        case IOCTL_HID_GET_INPUT_REPORT:
            Status = SensorTransfer(Device, Irp, Code);
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
SensorQueryInterface(
    _Inout_ PSENSOR_DEVICE Device)
{
    PIO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK Io;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    RtlZeroMemory(&Device->Interface, sizeof(Device->Interface));
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Device->LowerDevice, NULL, 0, NULL, &Event, &Io);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_APPLE_AOP_FUNCTION_INTERFACE;
    Stack->Parameters.QueryInterface.Size = sizeof(Device->Interface);
    Stack->Parameters.QueryInterface.Version = APPLE_AOP_FUNCTION_INTERFACE_VERSION;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)&Device->Interface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    Status = IoCallDriver(Device->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Io.Status;
    }
    if (!NT_SUCCESS(Status))
        return Status;
    if (Device->Interface.Size < sizeof(Device->Interface) || !Device->Interface.Subscribe ||
        !Device->Interface.ReadSample || !Device->Interface.InterfaceDereference)
    {
        if (Device->Interface.InterfaceDereference)
            Device->Interface.InterfaceDereference(Device->Interface.Context);
        RtlZeroMemory(&Device->Interface, sizeof(Device->Interface));
        return STATUS_NOT_SUPPORTED;
    }
    Device->InterfaceHeld = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
SensorStop(
    _Inout_ PSENSOR_DEVICE Device)
{
    LIST_ENTRY Flushed;
    PIRP Irp;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (Device->Subscribed)
        Device->Interface.Subscribe(Device->Interface.Context, NULL, NULL);
    Device->Subscribed = FALSE;
    InitializeListHead(&Flushed);
    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Device->Started = FALSE;
    while ((Irp = SensorTakeRead(Device)) != NULL)
        InsertTailList(&Flushed, &Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    while (!IsListEmpty(&Flushed))
    {
        Irp = CONTAINING_RECORD(RemoveHeadList(&Flushed), IRP, Tail.Overlay.ListEntry);
        Irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    if (Device->InterfaceHeld)
        Device->Interface.InterfaceDereference(Device->Interface.Context);
    Device->InterfaceHeld = FALSE;
}

static
NTSTATUS
SensorStart(
    _Inout_ PSENSOR_DEVICE Device)
{
    ULONG Value;
    KIRQL OldIrql;
    NTSTATUS Status;

    Status = SensorQueryInterface(Device);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaopsensor: the AOP interface is unavailable: %lx\n", Status);
        return Status;
    }
    switch (Device->Interface.Function)
    {
        case AppleAopAmbientLight:
            Device->Descriptor = SensorAmbientLightDescriptor;
            Device->DescriptorLength = sizeof(SensorAmbientLightDescriptor);
            Device->ProductId = 0;
            break;

        case AppleAopLidAngle:
            Device->Descriptor = SensorLidAngleDescriptor;
            Device->DescriptorLength = sizeof(SensorLidAngleDescriptor);
            Device->ProductId = 0x8104;
            break;

        default:
            DbgPrint("appleaopsensor: AOP function %u is not a sensor\n", Device->Interface.Function);
            SensorStop(Device);
            return STATUS_NOT_SUPPORTED;
    }
    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Device->ReportingState = SENSOR_REPORTING_ALL_EVENTS;
    Device->PowerState = SENSOR_POWER_D0;
    Device->ReportInterval = SENSOR_DEFAULT_INTERVAL_MS;
    Device->Started = TRUE;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    Status = Device->Interface.Subscribe(Device->Interface.Context, SensorSample, Device);
    if (!NT_SUCCESS(Status))
    {
        SensorStop(Device);
        return Status;
    }
    Device->Subscribed = TRUE;
    if (NT_SUCCESS(Device->Interface.ReadSample(Device->Interface.Context, &Value)))
        SensorSample(Device, Value);
    DbgPrint("appleaopsensor: %s HID sensor started\n",
             Device->Interface.Function == AppleAopAmbientLight ? "ambient light" : "lid angle");
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
SensorPnpCompletion(
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
SensorPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSENSOR_DEVICE Device = SensorGetDevice(DeviceObject);
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    KEVENT Event;
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, SensorPnpCompletion, &Event, TRUE, TRUE, TRUE);
            Status = IoCallDriver(Device->LowerDevice, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }
            if (NT_SUCCESS(Status))
                Status = SensorStart(Device);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_REMOVE_DEVICE:
            SensorStop(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
SensorPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSENSOR_DEVICE Device = SensorGetDevice(DeviceObject);

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
SensorPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PSENSOR_DEVICE Device = SensorGetDevice(DeviceObject);

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
SensorCreateClose(
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
SensorAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PSENSOR_DEVICE Device = HidExtension->MiniDeviceExtension;

    UNREFERENCED_PARAMETER(DriverObject);
    RtlZeroMemory(Device, sizeof(*Device));
    Device->DeviceObject = DeviceObject;
    Device->LowerDevice = HidExtension->NextDeviceObject;
    Device->Pdo = HidExtension->PhysicalDeviceObject;
    KeInitializeSpinLock(&Device->Lock);
    InitializeListHead(&Device->PendingReads);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
SensorUnload(
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

    DriverObject->MajorFunction[IRP_MJ_CREATE] = SensorCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = SensorCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = SensorInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = SensorPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = SensorPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = SensorPassThrough;
    DriverObject->DriverExtension->AddDevice = SensorAddDevice;
    DriverObject->DriverUnload = SensorUnload;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Revision = HID_REVISION;
    Registration.DriverObject = DriverObject;
    Registration.RegistryPath = RegistryPath;
    Registration.DeviceExtensionSize = sizeof(SENSOR_DEVICE);
    Registration.DevicesArePolled = FALSE;
    return HidRegisterMinidriver(&Registration);
}
