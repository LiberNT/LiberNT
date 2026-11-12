/*
 * PROJECT:     LiberNT Apple Silicon Battery Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Battery miniclass driver for the battery reported by the Apple SMC
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <batclass.h>
#include <reactos/drivers/applesmc.h>

#define APPLE_BATTERY_TAG           'tBpA'
#define APPLE_BATTERY_ID            1
#define APPLE_CELL_MILLIVOLTS       3800
#define APPLE_DEFAULT_CELLS         3
#define APPLE_CRITICAL_PERCENT      5

typedef struct _APPLE_BATTERY
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Lower;
    PDEVICE_OBJECT Pdo;
    PVOID ClassData;
    PFILE_OBJECT SmcFile;
    PDEVICE_OBJECT Smc;
    ULONG NominalMilliVolts;
} APPLE_BATTERY, *PAPPLE_BATTERY;

DRIVER_INITIALIZE DriverEntry;

static
NTSTATUS
BattReadKey(
    _In_ PAPPLE_BATTERY Battery,
    _In_ ULONG Key,
    _Out_writes_bytes_(Size) PVOID Value,
    _In_ ULONG Size)
{
    APPLE_SMC_KEY_DATA Data;
    IO_STATUS_BLOCK Io;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    RtlZeroMemory(Value, Size);
    if (!Battery->Smc || Size > APPLE_SMC_MAX_DATA)
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&Data, sizeof(Data));
    Data.Key = Key;
    Data.Length = Size;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_APPLE_SMC_READ_KEY, Battery->Smc, &Data, sizeof(Data), &Data,
                                        sizeof(Data), FALSE, &Event, &Io);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(Battery->Smc, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Io.Status;
    }
    if (NT_SUCCESS(Status))
        RtlCopyMemory(Value, Data.Data, Size);
    return Status;
}

static
ULONG
BattMilliwattHours(
    _In_ PAPPLE_BATTERY Battery,
    _In_ ULONG MilliampHours)
{
    return (ULONG)(((ULONG64)MilliampHours * Battery->NominalMilliVolts) / 1000);
}

static
NTSTATUS
BattFullCapacity(
    _In_ PAPPLE_BATTERY Battery,
    _Out_ PULONG Capacity)
{
    USHORT Full;
    NTSTATUS Status = BattReadKey(Battery, APPLE_SMC_KEY('B', '0', 'F', 'C'), &Full, sizeof(Full));

    *Capacity = NT_SUCCESS(Status) ? BattMilliwattHours(Battery, Full) : BATTERY_UNKNOWN_CAPACITY;
    return Status;
}

static
NTSTATUS
NTAPI
BattQueryTag(
    _In_ PVOID Context,
    _Out_ PULONG BatteryTag)
{
    UCHAR Percent;
    NTSTATUS Status = BattReadKey(Context, APPLE_SMC_KEY('B', 'U', 'I', 'C'), &Percent, sizeof(Percent));

    *BatteryTag = NT_SUCCESS(Status) ? APPLE_BATTERY_ID : BATTERY_TAG_INVALID;
    return NT_SUCCESS(Status) ? STATUS_SUCCESS : STATUS_NO_SUCH_DEVICE;
}

static
NTSTATUS
BattReturn(
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ ULONG Size,
    _Out_writes_bytes_to_(BufferLength, *ReturnedLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnedLength)
{
    *ReturnedLength = Size;
    if (BufferLength < Size)
        return STATUS_BUFFER_TOO_SMALL;
    RtlCopyMemory(Buffer, Data, Size);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BattQueryInformation(
    _In_ PVOID Context,
    _In_ ULONG BatteryTag,
    _In_ BATTERY_QUERY_INFORMATION_LEVEL Level,
    _In_ LONG AtRate,
    _Out_writes_bytes_to_(BufferLength, *ReturnedLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG ReturnedLength)
{
    static const WCHAR DeviceName[] = L"Internal Battery";
    static const WCHAR UniqueId[] = L"AppleSmcBattery";
    PAPPLE_BATTERY Battery = Context;
    BATTERY_INFORMATION Information;
    BATTERY_REPORTING_SCALE Scale;
    USHORT Design, Cycles, Minutes;
    ULONG Online, Seconds;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(AtRate);
    *ReturnedLength = 0;
    if (BatteryTag != APPLE_BATTERY_ID)
        return STATUS_NO_SUCH_DEVICE;
    switch (Level)
    {
        case BatteryInformation:
            RtlZeroMemory(&Information, sizeof(Information));
            Information.Capabilities = BATTERY_SYSTEM_BATTERY;
            Information.Technology = 1;
            RtlCopyMemory(Information.Chemistry, "LION", sizeof(Information.Chemistry));
            Status = BattReadKey(Battery, APPLE_SMC_KEY('B', '0', 'D', 'C'), &Design, sizeof(Design));
            Information.DesignedCapacity = NT_SUCCESS(Status) ? BattMilliwattHours(Battery, Design)
                                                              : BATTERY_UNKNOWN_CAPACITY;
            BattFullCapacity(Battery, &Information.FullChargedCapacity);
            if (Information.FullChargedCapacity != BATTERY_UNKNOWN_CAPACITY)
            {
                Information.DefaultAlert1 = Information.FullChargedCapacity * APPLE_CRITICAL_PERCENT / 100;
                Information.DefaultAlert2 = Information.FullChargedCapacity * 2 * APPLE_CRITICAL_PERCENT / 100;
            }
            if (NT_SUCCESS(BattReadKey(Battery, APPLE_SMC_KEY('B', '0', 'C', 'T'), &Cycles, sizeof(Cycles))))
                Information.CycleCount = Cycles;
            return BattReturn(&Information, sizeof(Information), Buffer, BufferLength, ReturnedLength);

        case BatteryGranularityInformation:
            BattFullCapacity(Battery, &Scale.Capacity);
            Scale.Granularity = (Scale.Capacity != BATTERY_UNKNOWN_CAPACITY) ? max(Scale.Capacity / 100, 1) : 1;
            return BattReturn(&Scale, sizeof(Scale), Buffer, BufferLength, ReturnedLength);

        case BatteryEstimatedTime:
            Seconds = BATTERY_UNKNOWN_TIME;
            if (NT_SUCCESS(BattReadKey(Battery, APPLE_SMC_KEY('C', 'H', 'I', 'S'), &Online, sizeof(Online))) &&
                !Online && NT_SUCCESS(BattReadKey(Battery, APPLE_SMC_KEY('B', '0', 'T', 'E'), &Minutes, sizeof(Minutes))) &&
                Minutes && Minutes != 0xffff)
            {
                Seconds = (ULONG)Minutes * 60;
            }
            return BattReturn(&Seconds, sizeof(Seconds), Buffer, BufferLength, ReturnedLength);

        case BatteryDeviceName:
            return BattReturn(DeviceName, sizeof(DeviceName), Buffer, BufferLength, ReturnedLength);

        case BatteryUniqueID:
            return BattReturn(UniqueId, sizeof(UniqueId), Buffer, BufferLength, ReturnedLength);

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

static
NTSTATUS
NTAPI
BattQueryStatus(
    _In_ PVOID Context,
    _In_ ULONG BatteryTag,
    _Out_ PBATTERY_STATUS BatteryStatus)
{
    PAPPLE_BATTERY Battery = Context;
    ULONG Online = 0, Full;
    UCHAR Percent, Charging = 0, FullFlag = 0;
    USHORT Voltage;
    LONG Rate;
    NTSTATUS Status;

    RtlZeroMemory(BatteryStatus, sizeof(*BatteryStatus));
    if (BatteryTag != APPLE_BATTERY_ID)
        return STATUS_NO_SUCH_DEVICE;
    Status = BattReadKey(Battery, APPLE_SMC_KEY('B', 'U', 'I', 'C'), &Percent, sizeof(Percent));
    if (!NT_SUCCESS(Status))
        return STATUS_NO_SUCH_DEVICE;
    BattReadKey(Battery, APPLE_SMC_KEY('C', 'H', 'I', 'S'), &Online, sizeof(Online));
    BattReadKey(Battery, APPLE_SMC_KEY('B', 'S', 'F', 'C'), &FullFlag, sizeof(FullFlag));
    BattReadKey(Battery, APPLE_SMC_KEY('C', 'H', 'S', 'C'), &Charging, sizeof(Charging));
    if (Online)
    {
        BatteryStatus->PowerState = BATTERY_POWER_ON_LINE;
        if (Charging && !FullFlag)
            BatteryStatus->PowerState |= BATTERY_CHARGING;
    }
    else
    {
        BatteryStatus->PowerState = BATTERY_DISCHARGING;
        if (Percent <= APPLE_CRITICAL_PERCENT)
            BatteryStatus->PowerState |= BATTERY_CRITICAL;
    }
    BattFullCapacity(Battery, &Full);
    BatteryStatus->Capacity = (Full != BATTERY_UNKNOWN_CAPACITY) ? (ULONG)(((ULONG64)Full * min(Percent, 100)) / 100)
                                                                  : BATTERY_UNKNOWN_CAPACITY;
    BatteryStatus->Voltage = NT_SUCCESS(BattReadKey(Battery, APPLE_SMC_KEY('B', '0', 'A', 'V'), &Voltage,
                                                    sizeof(Voltage))) ? Voltage : BATTERY_UNKNOWN_VOLTAGE;
    if (NT_SUCCESS(BattReadKey(Battery, APPLE_SMC_KEY('B', '0', 'A', 'P'), &Rate, sizeof(Rate))))
    {
        if (Rate < 0)
            Rate = -Rate;
        if (BatteryStatus->PowerState & BATTERY_DISCHARGING)
            Rate = -Rate;
        else if (!(BatteryStatus->PowerState & BATTERY_CHARGING))
            Rate = 0;
        BatteryStatus->Rate = Rate;
    }
    else
    {
        BatteryStatus->Rate = (LONG)BATTERY_UNKNOWN_RATE;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BattSetStatusNotify(
    _In_ PVOID Context,
    _In_ ULONG BatteryTag,
    _In_ PBATTERY_NOTIFY BatteryNotify)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(BatteryTag);
    UNREFERENCED_PARAMETER(BatteryNotify);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
BattDisableStatusNotify(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BattSetInformation(
    _In_ PVOID Context,
    _In_ ULONG BatteryTag,
    _In_ BATTERY_SET_INFORMATION_LEVEL Level,
    _In_opt_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(BatteryTag);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(Buffer);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
BattStart(
    _Inout_ PAPPLE_BATTERY Battery)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(APPLE_SMC_DEVICE_NAME);
    BATTERY_MINIPORT_INFO Miniport;
    UCHAR Cells;
    NTSTATUS Status;

    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &Battery->SmcFile, &Battery->Smc);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!NT_SUCCESS(BattReadKey(Battery, APPLE_SMC_KEY('B', 'N', 'C', 'B'), &Cells, sizeof(Cells))) || !Cells)
        Cells = APPLE_DEFAULT_CELLS;
    Battery->NominalMilliVolts = APPLE_CELL_MILLIVOLTS * Cells;
    RtlZeroMemory(&Miniport, sizeof(Miniport));
    Miniport.MajorVersion = BATTERY_CLASS_MAJOR_VERSION;
    Miniport.MinorVersion = BATTERY_CLASS_MINOR_VERSION;
    Miniport.Context = Battery;
    Miniport.QueryTag = BattQueryTag;
    Miniport.QueryInformation = BattQueryInformation;
    Miniport.SetInformation = BattSetInformation;
    Miniport.QueryStatus = BattQueryStatus;
    Miniport.SetStatusNotify = BattSetStatusNotify;
    Miniport.DisableStatusNotify = BattDisableStatusNotify;
    Miniport.Pdo = Battery->Pdo;
    Status = BatteryClassInitializeDevice(&Miniport, &Battery->ClassData);
    DbgPrint("applebatt: %u cells, battery class %lx\n", Cells, Status);
    return Status;
}

static
VOID
BattStop(
    _Inout_ PAPPLE_BATTERY Battery)
{
    if (Battery->ClassData)
        BatteryClassUnload(Battery->ClassData);
    Battery->ClassData = NULL;
    if (Battery->SmcFile)
        ObDereferenceObject(Battery->SmcFile);
    Battery->SmcFile = NULL;
    Battery->Smc = NULL;
}

static
NTSTATUS
NTAPI
BattPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_BATTERY Battery = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Battery->Lower, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
            {
                Status = BattStart(Battery);
                if (!NT_SUCCESS(Status))
                    BattStop(Battery);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_STOP_DEVICE:
            BattStop(Battery);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            BattStop(Battery);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Battery->Lower, Irp);
            IoDetachDevice(Battery->Lower);
            IoDeleteDevice(DeviceObject);
            return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Battery->Lower, Irp);
}

static
NTSTATUS
NTAPI
BattDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_BATTERY Battery = DeviceObject->DeviceExtension;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    if (Battery->ClassData)
        Status = BatteryClassIoctl(Battery->ClassData, Irp);
    if (Status == STATUS_NOT_SUPPORTED)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Battery->Lower, Irp);
    }
    return Status;
}

static
NTSTATUS
NTAPI
BattCreateClose(
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
BattPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_BATTERY Battery = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Battery->Lower, Irp);
}

static
NTSTATUS
NTAPI
BattPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_BATTERY Battery = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Battery->Lower, Irp);
}

static
NTSTATUS
NTAPI
BattAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT Pdo)
{
    PAPPLE_BATTERY Battery;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(APPLE_BATTERY), NULL, FILE_DEVICE_BATTERY, FILE_DEVICE_SECURE_OPEN,
                            FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Battery = Fdo->DeviceExtension;
    RtlZeroMemory(Battery, sizeof(*Battery));
    Battery->Self = Fdo;
    Battery->Pdo = Pdo;
    Battery->Lower = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (!Battery->Lower)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->MajorFunction[IRP_MJ_CREATE] = BattCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = BattCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = BattDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = BattPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = BattPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = BattPassThrough;
    DriverObject->DriverExtension->AddDevice = BattAddDevice;
    return STATUS_SUCCESS;
}
