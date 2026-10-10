/*
 * PROJECT:     LiberNT StarFive JH7110 thermal driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     StarFive JH7110 temperature sensor as a thermal zone
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <initguid.h>
#include <poclass.h>

#define NDEBUG
#include <debug.h>

#define JH_SYSCRG_BASE              0x13020000ULL
#define JH_SYSCRG_SIZE              0x400
#define JH_SYSCRG_RESET_ASSERT      0x2F8
#define JH_SYSCRG_RESET_STATUS      0x308
#define JH_SYSCLK_TEMP_APB          129
#define JH_SYSCLK_TEMP_CORE         130
#define JH_SYSRST_TEMP_APB          123
#define JH_SYSRST_TEMP_CORE         124
#define JH_CLK_ENABLE               (1UL << 31)

#define JH_TEMP_SIZE                0x4
#define JH_TEMP_CONTROL             0x0
#define JH_TEMP_RSTN                (1UL << 0)
#define JH_TEMP_PD                  (1UL << 1)
#define JH_TEMP_RUN                 (1UL << 2)
#define JH_TEMP_DOUT_SHIFT          16
#define JH_TEMP_DOUT_MASK           0xFFFUL
#define JH_TEMP_Y1000               237500LL
#define JH_TEMP_Z                   4094LL
#define JH_TEMP_K1000               81100LL

#define JH_PASSIVE_CELSIUS          85
#define JH_KELVIN_TENTHS(C)         ((ULONG)((C) * 10 + 2732))
#define JH_SAMPLING_TENTHS          10

typedef struct _JH7110THERMAL_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    PUCHAR Registers;
    ULONG Stamp;
    UNICODE_STRING InterfaceName;
} JH7110THERMAL_EXTENSION, *PJH7110THERMAL_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

static LONG
JhThermalCelsius(_In_ PJH7110THERMAL_EXTENSION Extension)
{
    ULONG Data = (READ_REGISTER_ULONG((PULONG)(Extension->Registers + JH_TEMP_CONTROL)) >> JH_TEMP_DOUT_SHIFT) &
                 JH_TEMP_DOUT_MASK;

    return (LONG)(((LONGLONG)Data * JH_TEMP_Y1000 / JH_TEMP_Z - JH_TEMP_K1000) / 1000);
}

static BOOLEAN
JhThermalEnableClocks(VOID)
{
    PHYSICAL_ADDRESS Address;
    PUCHAR SysCrg;
    ULONG Mask, Elapsed;
    BOOLEAN Released = FALSE;

    Address.QuadPart = JH_SYSCRG_BASE;
    SysCrg = MmMapIoSpace(Address, JH_SYSCRG_SIZE, MmNonCached);
    if (!SysCrg)
        return FALSE;
    WRITE_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCLK_TEMP_APB * sizeof(ULONG)),
                         READ_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCLK_TEMP_APB * sizeof(ULONG))) | JH_CLK_ENABLE);
    WRITE_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCLK_TEMP_CORE * sizeof(ULONG)),
                         READ_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCLK_TEMP_CORE * sizeof(ULONG))) | JH_CLK_ENABLE);
    Mask = (1UL << (JH_SYSRST_TEMP_APB % 32)) | (1UL << (JH_SYSRST_TEMP_CORE % 32));
    WRITE_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCRG_RESET_ASSERT + (JH_SYSRST_TEMP_APB / 32) * sizeof(ULONG)),
                         READ_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCRG_RESET_ASSERT +
                                                      (JH_SYSRST_TEMP_APB / 32) * sizeof(ULONG))) & ~Mask);
    for (Elapsed = 0; Elapsed < 1000; Elapsed += 10)
    {
        if ((READ_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCRG_RESET_STATUS +
                                          (JH_SYSRST_TEMP_APB / 32) * sizeof(ULONG))) & Mask) == Mask)
        {
            Released = TRUE;
            break;
        }
        KeStallExecutionProcessor(10);
    }
    MmUnmapIoSpace(SysCrg, JH_SYSCRG_SIZE);
    return Released;
}

static VOID
JhThermalPowerUp(_In_ PJH7110THERMAL_EXTENSION Extension)
{
    LARGE_INTEGER Delay;

    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + JH_TEMP_CONTROL), JH_TEMP_PD);
    KeStallExecutionProcessor(1);
    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + JH_TEMP_CONTROL), 0);
    KeStallExecutionProcessor(100);
    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + JH_TEMP_CONTROL), JH_TEMP_RSTN);
    KeStallExecutionProcessor(1);
    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + JH_TEMP_CONTROL), JH_TEMP_RSTN | JH_TEMP_RUN);
    Delay.QuadPart = -20 * 10000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}

static NTSTATUS
JhThermalStart(_In_ PJH7110THERMAL_EXTENSION Extension, _In_ PIO_STACK_LOCATION Stack)
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
            Descriptor->u.Memory.Length >= JH_TEMP_SIZE)
        {
            Extension->Registers = MmMapIoSpace(Descriptor->u.Memory.Start, JH_TEMP_SIZE, MmNonCached);
        }
    }
    if (!Extension->Registers)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!JhThermalEnableClocks())
        return STATUS_IO_TIMEOUT;

    JhThermalPowerUp(Extension);
    DPRINT1("JH7110THERMAL: SoC %ld C\n", JhThermalCelsius(Extension));

    Status = IoRegisterDeviceInterface(Extension->PhysicalDevice, &GUID_DEVICE_THERMAL_ZONE, NULL,
                                       &Extension->InterfaceName);
    if (NT_SUCCESS(Status))
        Status = IoSetDeviceInterfaceState(&Extension->InterfaceName, TRUE);
    return Status;
}

static VOID
JhThermalStop(_In_ PJH7110THERMAL_EXTENSION Extension)
{
    if (Extension->InterfaceName.Buffer)
    {
        (VOID)IoSetDeviceInterfaceState(&Extension->InterfaceName, FALSE);
        RtlFreeUnicodeString(&Extension->InterfaceName);
    }
    if (Extension->Registers)
    {
        WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + JH_TEMP_CONTROL), JH_TEMP_PD);
        MmUnmapIoSpace(Extension->Registers, JH_TEMP_SIZE);
        Extension->Registers = NULL;
    }
}

static NTSTATUS NTAPI
JhThermalDispatchDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PJH7110THERMAL_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS Status;

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
            RtlZeroMemory(Information, sizeof(*Information));
            Information->ThermalStamp = Extension->Stamp;
            Information->Processors = KeQueryActiveProcessors();
            Information->SamplingPeriod = JH_SAMPLING_TENTHS;
            Information->CurrentTemperature = JH_KELVIN_TENTHS(JhThermalCelsius(Extension));
            Information->PassiveTripPoint = JH_KELVIN_TENTHS(JH_PASSIVE_CELSIUS);
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
            *(PULONG)Irp->AssociatedIrp.SystemBuffer = JH_KELVIN_TENTHS(JhThermalCelsius(Extension));
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
JhThermalDispatchPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PJH7110THERMAL_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status
                                                                             : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = JhThermalStart(Extension, Stack);
            if (!NT_SUCCESS(Status))
                DPRINT1("JH7110THERMAL: start failed 0x%08lx\n", Status);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_REMOVE_DEVICE:
            JhThermalStop(Extension);
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
JhThermalDispatchPower(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PJH7110THERMAL_EXTENSION Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
JhThermalDispatchOther(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PJH7110THERMAL_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
JhThermalDispatchCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
JhThermalAddDevice(_In_ PDRIVER_OBJECT DriverObject, _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PJH7110THERMAL_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(JH7110THERMAL_EXTENSION), NULL, FILE_DEVICE_UNKNOWN,
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
        DriverObject->MajorFunction[Index] = JhThermalDispatchOther;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = JhThermalDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = JhThermalDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = JhThermalDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = JhThermalDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = JhThermalDispatchPower;
    DriverObject->DriverExtension->AddDevice = JhThermalAddDevice;
    return STATUS_SUCCESS;
}
