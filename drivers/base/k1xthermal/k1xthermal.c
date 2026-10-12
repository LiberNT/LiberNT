/*
 * PROJECT:     LiberNT SpacemiT K1 thermal driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SpacemiT K1 temperature sensors as a thermal zone
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <initguid.h>
#include <poclass.h>

#define NDEBUG
#include <debug.h>

#define K1XTHERMAL_TAG              'hTiK'

#define K1X_APBC_TSEN               0xD401506CULL
#define K1X_APBC_TSEN_CLOCKS        0x3UL
#define K1X_APBC_TSEN_RESET         0x4UL

#define K1X_TSEN_SIZE               0x100
#define K1X_TSEN_PCTRL              0x00
#define K1X_TSEN_PCTRL_ENABLE       (1UL << 0)
#define K1X_TSEN_PCTRL_TEMP_MODE    (1UL << 3)
#define K1X_TSEN_PCTRL_RAW_SEL      (1UL << 7)
#define K1X_TSEN_PCTRL_CTUNE        0x00000F00UL
#define K1X_TSEN_PCTRL_SW_CTRL      0x003C0000UL
#define K1X_TSEN_PCTRL_AUTO         (1UL << 23)
#define K1X_TSEN_PCTRL2             0x04
#define K1X_TSEN_PCTRL2_SDM_CLOCK   0x0000C000UL
#define K1X_TSEN_BJT_ENABLE         0x08
#define K1X_TSEN_TIME_CTRL          0x0C
#define K1X_TSEN_TIME_MASK          0x00FFFFFFUL
#define K1X_TSEN_TIME_VALUE         ((0x3000UL << 8) | 0xF0UL | 0xFUL)
#define K1X_TSEN_INT_MASK           0x14
#define K1X_TSEN_INT_MASK_ALL       (1UL << 0)
#define K1X_TSEN_DATA               0x20
#define K1X_TSEN_OFFSET             278
#define K1X_TSEN_FIRST              1
#define K1X_TSEN_LAST               4

#define K1X_PASSIVE_CELSIUS         85
#define K1X_KELVIN_TENTHS(C)        ((ULONG)((C) * 10 + 2732))
#define K1X_SAMPLING_TENTHS         10

typedef struct _K1XTHERMAL_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    PUCHAR Registers;
    ULONG RegisterLength;
    ULONG Stamp;
    LONG LastCelsius;
    UNICODE_STRING InterfaceName;
} K1XTHERMAL_EXTENSION, *PK1XTHERMAL_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

static ULONG
K1ThermalRead(_In_ PK1XTHERMAL_EXTENSION Extension, _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Extension->Registers + Offset));
}

static VOID
K1ThermalUpdate(_In_ PK1XTHERMAL_EXTENSION Extension, _In_ ULONG Offset, _In_ ULONG Mask, _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + Offset),
                         (K1ThermalRead(Extension, Offset) & ~Mask) | (Value & Mask));
}

static LONG
K1ThermalSensorCelsius(_In_ PK1XTHERMAL_EXTENSION Extension, _In_ ULONG Sensor)
{
    ULONG Data = K1ThermalRead(Extension, K1X_TSEN_DATA + (Sensor / 2) * sizeof(ULONG));

    return (LONG)((Sensor & 1) ? (Data >> 16) : (Data & 0xFFFF)) - K1X_TSEN_OFFSET;
}

static LONG
K1ThermalHottest(_In_ PK1XTHERMAL_EXTENSION Extension)
{
    LONG Hottest = MINLONG;
    ULONG Sensor;

    for (Sensor = K1X_TSEN_FIRST; Sensor <= K1X_TSEN_LAST; ++Sensor)
        Hottest = max(Hottest, K1ThermalSensorCelsius(Extension, Sensor));
    if (Hottest != Extension->LastCelsius)
    {
        Extension->LastCelsius = Hottest;
        Extension->Stamp++;
    }
    return Hottest;
}

static NTSTATUS
K1ThermalPowerUp(_In_ PK1XTHERMAL_EXTENSION Extension)
{
    PHYSICAL_ADDRESS Address;
    LARGE_INTEGER Delay;
    PULONG Clock;
    ULONG Sensor;

    Address.QuadPart = K1X_APBC_TSEN;
    Clock = MmMapIoSpace(Address, sizeof(ULONG), MmNonCached);
    if (!Clock)
        return STATUS_INSUFFICIENT_RESOURCES;
    WRITE_REGISTER_ULONG(Clock, (READ_REGISTER_ULONG(Clock) | K1X_APBC_TSEN_CLOCKS) & ~K1X_APBC_TSEN_RESET);
    MmUnmapIoSpace(Clock, sizeof(ULONG));

    K1ThermalUpdate(Extension, K1X_TSEN_INT_MASK, MAXULONG, MAXULONG);
    K1ThermalUpdate(Extension, K1X_TSEN_TIME_CTRL, K1X_TSEN_TIME_MASK, K1X_TSEN_TIME_VALUE);
    K1ThermalUpdate(Extension, K1X_TSEN_PCTRL,
                    K1X_TSEN_PCTRL_RAW_SEL | K1X_TSEN_PCTRL_TEMP_MODE | K1X_TSEN_PCTRL_ENABLE |
                        K1X_TSEN_PCTRL_SW_CTRL | K1X_TSEN_PCTRL_CTUNE,
                    K1X_TSEN_PCTRL_RAW_SEL | K1X_TSEN_PCTRL_TEMP_MODE | K1X_TSEN_PCTRL_ENABLE);
    K1ThermalUpdate(Extension, K1X_TSEN_PCTRL2, K1X_TSEN_PCTRL2_SDM_CLOCK, 0);
    for (Sensor = K1X_TSEN_FIRST; Sensor <= K1X_TSEN_LAST; ++Sensor)
        K1ThermalUpdate(Extension, K1X_TSEN_BJT_ENABLE, 1UL << Sensor, 1UL << Sensor);
    K1ThermalUpdate(Extension, K1X_TSEN_PCTRL, K1X_TSEN_PCTRL_AUTO, K1X_TSEN_PCTRL_AUTO);

    Delay.QuadPart = -10000LL * 100;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    return STATUS_SUCCESS;
}

static NTSTATUS
K1ThermalStart(_In_ PK1XTHERMAL_EXTENSION Extension, _In_ PIO_STACK_LOCATION Stack)
{
    PCM_RESOURCE_LIST Resources = Stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    ULONG Index;
    NTSTATUS Status;

    if (!Resources || !Resources->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Index = 0; Index < Resources->List[0].PartialResourceList.Count; ++Index)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor =
            &Resources->List[0].PartialResourceList.PartialDescriptors[Index];

        if (Descriptor->Type == CmResourceTypeMemory && !Extension->Registers &&
            Descriptor->u.Memory.Length >= K1X_TSEN_SIZE)
        {
            Extension->RegisterLength = K1X_TSEN_SIZE;
            Extension->Registers = MmMapIoSpace(Descriptor->u.Memory.Start, K1X_TSEN_SIZE, MmNonCached);
        }
    }
    if (!Extension->Registers)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Status = K1ThermalPowerUp(Extension);
    if (!NT_SUCCESS(Status))
        return Status;
    DPRINT1("K1XTHERMAL: top %ld C, gpu %ld C, cluster0 %ld C, cluster1 %ld C\n",
            K1ThermalSensorCelsius(Extension, 1), K1ThermalSensorCelsius(Extension, 2),
            K1ThermalSensorCelsius(Extension, 3), K1ThermalSensorCelsius(Extension, 4));
    Extension->LastCelsius = K1ThermalHottest(Extension);

    Status = IoRegisterDeviceInterface(Extension->PhysicalDevice, &GUID_DEVICE_THERMAL_ZONE, NULL,
                                       &Extension->InterfaceName);
    if (NT_SUCCESS(Status))
        Status = IoSetDeviceInterfaceState(&Extension->InterfaceName, TRUE);
    return Status;
}

static VOID
K1ThermalStop(_In_ PK1XTHERMAL_EXTENSION Extension)
{
    if (Extension->InterfaceName.Buffer)
    {
        (VOID)IoSetDeviceInterfaceState(&Extension->InterfaceName, FALSE);
        RtlFreeUnicodeString(&Extension->InterfaceName);
    }
    if (Extension->Registers)
    {
        MmUnmapIoSpace(Extension->Registers, Extension->RegisterLength);
        Extension->Registers = NULL;
    }
}

static NTSTATUS NTAPI
K1ThermalDispatchDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XTHERMAL_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS Status;
    LONG Celsius;

    if (!Extension->Registers)
    {
        Status = STATUS_DEVICE_NOT_READY;
    }
    else if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_THERMAL_QUERY_INFORMATION)
    {
        PTHERMAL_INFORMATION Information = Irp->AssociatedIrp.SystemBuffer;

        if (OutputLength < sizeof(*Information))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            Celsius = K1ThermalHottest(Extension);
            RtlZeroMemory(Information, sizeof(*Information));
            Information->ThermalStamp = Extension->Stamp;
            Information->Processors = KeQueryActiveProcessors();
            Information->SamplingPeriod = K1X_SAMPLING_TENTHS;
            Information->CurrentTemperature = K1X_KELVIN_TENTHS(Celsius);
            Information->PassiveTripPoint = K1X_KELVIN_TENTHS(K1X_PASSIVE_CELSIUS);
            Irp->IoStatus.Information = sizeof(*Information);
            Status = STATUS_SUCCESS;
        }
    }
    else if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_THERMAL_READ_TEMPERATURE)
    {
        if (OutputLength < sizeof(ULONG))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            Celsius = K1ThermalHottest(Extension);
            *(PULONG)Irp->AssociatedIrp.SystemBuffer = K1X_KELVIN_TENTHS(Celsius);
            Irp->IoStatus.Information = sizeof(ULONG);
            Status = STATUS_SUCCESS;
        }
    }
    else
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Extension->LowerDevice, Irp);
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS NTAPI
K1ThermalDispatchPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XTHERMAL_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status
                                                                             : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = K1ThermalStart(Extension, Stack);
            if (!NT_SUCCESS(Status))
                DPRINT1("K1XTHERMAL: start failed 0x%08lx\n", Status);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_REMOVE_DEVICE:
            K1ThermalStop(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            IoDetachDevice(Extension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1ThermalDispatchPower(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XTHERMAL_EXTENSION Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1ThermalDispatchOther(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XTHERMAL_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1ThermalDispatchCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
K1ThermalAddDevice(_In_ PDRIVER_OBJECT DriverObject, _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PK1XTHERMAL_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(K1XTHERMAL_EXTENSION), NULL, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Self = Fdo;
    Extension->PhysicalDevice = PhysicalDeviceObject;
    Extension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = K1ThermalDispatchOther;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = K1ThermalDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = K1ThermalDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = K1ThermalDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = K1ThermalDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = K1ThermalDispatchPower;
    DriverObject->DriverExtension->AddDevice = K1ThermalAddDevice;
    return STATUS_SUCCESS;
}
