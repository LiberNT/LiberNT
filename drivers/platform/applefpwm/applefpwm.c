/*
 * PROJECT:     LiberNT Apple PWM Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon PWM controllers and the PWM LEDs they drive
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/applepmgr.h>
#include "fpwm.h"

#define FPWM_MAX_LEDS 4

typedef enum _FPWM_ROLE
{
    FpwmRoleNone,
    FpwmRoleController,
    FpwmRoleLeds
} FPWM_ROLE;

typedef struct _FPWM_DEVICE
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
    FPWM_ROLE Role;
    FDT_DRIVER_TREE Tree;
    BOOLEAN TreeOpen;
    LIST_ENTRY Link;
    BOOLEAN Listed;
    ULONG Phandle;
    PUCHAR Base;
    ULONG Size;
    ULONG64 Address;
    ULONG64 ClockHz;
    ULONG LedCount;
    PWM_LED Led[FPWM_MAX_LEDS];
    BOOLEAN Handled[FPWM_MAX_LEDS];
} FPWM_DEVICE, *PFPWM_DEVICE;

static FAST_MUTEX FpwmLock;
static LIST_ENTRY FpwmControllers;
static LIST_ENTRY FpwmLedDevices;

static
const CHAR *
FpwmDefaultName(
    _In_ PWM_LED_DEFAULT Default)
{
    switch (Default)
    {
        case PwmLedDefaultOn:
            return "on";
        case PwmLedDefaultKeep:
            return "keep";
        default:
            return "off";
    }
}

static
PFPWM_DEVICE
FpwmFindController(
    _In_ ULONG Phandle)
{
    PLIST_ENTRY Entry;
    PFPWM_DEVICE Controller;

    ASSERT(FpwmLock.Owner == KeGetCurrentThread());
    for (Entry = FpwmControllers.Flink; Entry != &FpwmControllers; Entry = Entry->Flink)
    {
        Controller = CONTAINING_RECORD(Entry, FPWM_DEVICE, Link);
        ASSERT(Controller->Role == FpwmRoleController && Controller->Listed && Controller->Base);
        if (Controller->Phandle && Controller->Phandle == Phandle)
            return Controller;
    }
    return NULL;
}

static
VOID
FpwmApplyLeds(VOID)
{
    PLIST_ENTRY Entry;
    PFPWM_DEVICE Leds, Controller;
    PPWM_LED Led;
    FPWM_OUTPUT Before, After;
    ULONG Index, Brightness;
    ULONG64 Duty;

    ASSERT(FpwmLock.Owner == KeGetCurrentThread());
    for (Entry = FpwmLedDevices.Flink; Entry != &FpwmLedDevices; Entry = Entry->Flink)
    {
        Leds = CONTAINING_RECORD(Entry, FPWM_DEVICE, Link);
        ASSERT(Leds->Role == FpwmRoleLeds && Leds->Listed && Leds->LedCount <= FPWM_MAX_LEDS);
        for (Index = 0; Index < Leds->LedCount; ++Index)
        {
            Led = &Leds->Led[Index];
            if (Leds->Handled[Index] || !(Controller = FpwmFindController(Led->Provider)))
                continue;
            Leds->Handled[Index] = TRUE;
            if (Led->Channel)
            {
                DbgPrint("applefpwm: LED %s asks for channel %lu of the PWM at %I64x, which has only channel 0\n",
                         Led->Name, Led->Channel, Controller->Address);
                continue;
            }
            FpwmReadOutput(Controller->Base, Controller->ClockHz, &Before);
            /* TODO: expose the LED as a HID LampArray for Dynamic Lighting; the start brightness stays */
            Brightness = PwmLedStartBrightness(Led, &Before);
            Duty = PwmLedDutyNs(Led, Brightness);
            FpwmApply(Controller->Base, Controller->ClockHz, TRUE, Led->PeriodNs, Duty);
            FpwmReadOutput(Controller->Base, Controller->ClockHz, &After);
            DbgPrint("applefpwm: LED %s set to brightness %lu of %lu (default-state %s, the PWM was %s at %I64u of "
                     "%I64u ns): duty %I64u of %I64u ns, control %08lx, %lu on and %lu off cycles\n",
                     Led->Name, Brightness, Led->MaxBrightness, FpwmDefaultName(Led->Default),
                     Before.Enabled ? "on" : "off", Before.DutyNs, Before.PeriodNs, After.DutyNs, After.PeriodNs,
                     After.Control, After.OnCycles, After.OffCycles);
        }
    }
}

static
VOID
FpwmStop(
    _Inout_ PFPWM_DEVICE Device)
{
    PLIST_ENTRY Entry;
    PFPWM_DEVICE Leds;
    ULONG Index;

    PAGED_CODE();

    ExAcquireFastMutex(&FpwmLock);
    if (Device->Listed)
    {
        RemoveEntryList(&Device->Link);
        Device->Listed = FALSE;
        if (Device->Role == FpwmRoleController)
        {
            for (Entry = FpwmLedDevices.Flink; Entry != &FpwmLedDevices; Entry = Entry->Flink)
            {
                Leds = CONTAINING_RECORD(Entry, FPWM_DEVICE, Link);
                for (Index = 0; Index < Leds->LedCount; ++Index)
                {
                    if (Leds->Led[Index].Provider == Device->Phandle)
                        Leds->Handled[Index] = FALSE;
                }
            }
        }
    }
    ExReleaseFastMutex(&FpwmLock);
    if (Device->Base)
        MmUnmapIoSpace(Device->Base, Device->Size);
    Device->Base = NULL;
    Device->Size = 0;
    if (Device->TreeOpen)
        FdtDriverClose(&Device->Tree);
    Device->TreeOpen = FALSE;
    Device->LedCount = 0;
    RtlZeroMemory(Device->Handled, sizeof(Device->Handled));
    Device->Role = FpwmRoleNone;
}

static
NTSTATUS
FpwmStartController(
    _Inout_ PFPWM_DEVICE Controller)
{
    PFDT Fdt = &Controller->Tree.Fdt;
    FPWM_OUTPUT Output;
    ULONG ClockPhandle, ClockNode, ClockHz;
    NTSTATUS Status;

    PAGED_CODE();

    if (!FdtReadU32(Fdt, Controller->Tree.Node, "phandle", &Controller->Phandle))
        Controller->Phandle = 0;
    if (!FdtReadU32(Fdt, Controller->Tree.Node, "clocks", &ClockPhandle) ||
        (ClockNode = FdtDriverFindPhandle(&Controller->Tree, ClockPhandle)) == FDT_NO_NODE ||
        !FdtReadU32(Fdt, ClockNode, "clock-frequency", &ClockHz) || !ClockHz || ClockHz > FPWM_NS_PER_SECOND)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Controller->ClockHz = ClockHz;
    Status = ApplePmgrPowerOnDevice(&Controller->Tree, Controller->Tree.Node);
    if (!NT_SUCCESS(Status))
        return Status;
    Controller->Base = FdtDriverMapReg(&Controller->Tree, Controller->Tree.Node, 0, &Controller->Address,
                                       &Controller->Size);
    if (!Controller->Base)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (Controller->Size < FPWM_REGISTER_SPAN)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    FpwmReadOutput(Controller->Base, Controller->ClockHz, &Output);
    DbgPrint("applefpwm: PWM at %I64x with a %I64u Hz clock is %s, duty %I64u of %I64u ns, control %08lx\n",
             Controller->Address, Controller->ClockHz, Output.Enabled ? "on" : "off", Output.DutyNs, Output.PeriodNs,
             Output.Control);

    ExAcquireFastMutex(&FpwmLock);
    InsertTailList(&FpwmControllers, &Controller->Link);
    Controller->Listed = TRUE;
    FpwmApplyLeds();
    ExReleaseFastMutex(&FpwmLock);
    return STATUS_SUCCESS;
}

static
NTSTATUS
FpwmStartLeds(
    _Inout_ PFPWM_DEVICE Leds)
{
    PFDT Fdt = &Leds->Tree.Fdt;
    PPWM_LED Led;
    const VOID *Property;
    ULONG Child, Provider, Cells, Length, Index;
    ULONG64 Phandle;
    NTSTATUS Status;

    PAGED_CODE();

    for (Child = FdtFirstChild(Fdt, Leds->Tree.Node); Child != FDT_NO_NODE; Child = FdtNextSibling(Fdt, Child))
    {
        Property = FdtGetProperty(Fdt, Child, "pwms", &Length);
        if (!Property)
            continue;
        if (Leds->LedCount == FPWM_MAX_LEDS)
        {
            DbgPrint("applefpwm: only the first %u PWM LEDs are driven\n", FPWM_MAX_LEDS);
            break;
        }
        if (!FdtReadCells(Property, Length, 0, 1, &Phandle) ||
            (Provider = FdtDriverFindPhandle(&Leds->Tree, (ULONG)Phandle)) == FDT_NO_NODE)
        {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Property = FdtGetProperty(Fdt, Provider, "compatible", &Length);
        if (!FdtStringListContains(Property, Length, "apple,s5l-fpwm"))
            return STATUS_NOT_SUPPORTED;
        if (!FdtReadU32(Fdt, Provider, "#pwm-cells", &Cells))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Led = &Leds->Led[Leds->LedCount];
        Status = PwmLedParse(Fdt, Child, Cells, Led);
        if (!NT_SUCCESS(Status))
            return Status;
        ASSERT(Led->Provider == (ULONG)Phandle);
        DbgPrint("applefpwm: LED %s uses PWM %lx channel %lu, period %lu ns, brightness up to %lu, default-state %s%s\n",
                 Led->Name, Led->Provider, Led->Channel, Led->PeriodNs, Led->MaxBrightness,
                 FpwmDefaultName(Led->Default), Led->ActiveLow ? ", active low" : "");
        Leds->LedCount++;
    }
    if (!Leds->LedCount)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    ExAcquireFastMutex(&FpwmLock);
    InsertTailList(&FpwmLedDevices, &Leds->Link);
    Leds->Listed = TRUE;
    FpwmApplyLeds();
    for (Index = 0; Index < Leds->LedCount; ++Index)
    {
        if (!Leds->Handled[Index])
            DbgPrint("applefpwm: LED %s waits for its PWM controller\n", Leds->Led[Index].Name);
    }
    ExReleaseFastMutex(&FpwmLock);
    return STATUS_SUCCESS;
}

static
NTSTATUS
FpwmStart(
    _Inout_ PFPWM_DEVICE Device)
{
    const VOID *Compatible;
    ULONG Length;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(!Device->TreeOpen && !Device->Listed && !Device->Base && Device->Role == FpwmRoleNone);

    Status = FdtDriverOpen(Device->Pdo, &Device->Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    Device->TreeOpen = TRUE;
    Compatible = FdtGetProperty(&Device->Tree.Fdt, Device->Tree.Node, "compatible", &Length);
    if (FdtStringListContains(Compatible, Length, "apple,s5l-fpwm"))
    {
        Device->Role = FpwmRoleController;
        return FpwmStartController(Device);
    }
    if (FdtStringListContains(Compatible, Length, "pwm-leds"))
    {
        Device->Role = FpwmRoleLeds;
        return FpwmStartLeds(Device);
    }
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
NTAPI
FpwmPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFPWM_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    PAGED_CODE();

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Device->Lower, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
            {
                Status = FpwmStart(Device);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("applefpwm: %s did not start: %lx\n",
                             Device->Role == FpwmRoleLeds ? "the PWM LEDs" : "the PWM controller", Status);
                    FpwmStop(Device);
                }
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_STOP_DEVICE:
            FpwmStop(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            FpwmStop(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Device->Lower, Irp);
            IoDetachDevice(Device->Lower);
            IoDeleteDevice(DeviceObject);
            return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->Lower, Irp);
}

static
NTSTATUS
NTAPI
FpwmPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFPWM_DEVICE Device = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Device->Lower, Irp);
}

static
NTSTATUS
NTAPI
FpwmPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFPWM_DEVICE Device = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->Lower, Irp);
}

static
NTSTATUS
NTAPI
FpwmAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT Pdo)
{
    PFPWM_DEVICE Device;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IoCreateDevice(DriverObject, sizeof(FPWM_DEVICE), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
                            FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Device = Fdo->DeviceExtension;
    RtlZeroMemory(Device, sizeof(*Device));
    Device->Self = Fdo;
    Device->Pdo = Pdo;
    Device->Tree.Node = FDT_NO_NODE;
    InitializeListHead(&Device->Link);
    Device->Lower = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (!Device->Lower)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    ExInitializeFastMutex(&FpwmLock);
    InitializeListHead(&FpwmControllers);
    InitializeListHead(&FpwmLedDevices);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = FpwmPassThrough;
    DriverObject->MajorFunction[IRP_MJ_PNP] = FpwmPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = FpwmPower;
    DriverObject->DriverExtension->AddDevice = FpwmAddDevice;
    return STATUS_SUCCESS;
}
