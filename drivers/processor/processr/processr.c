/*
 * PROJECT:     LiberNT Processor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Processor device: start, registration with the kernel, notifications and passive cooling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "processr.h"
#include <aux_klib.h>

#define NDEBUG
#include <debug.h>

#define PROC_PENDING_PERFORMANCE    0x00000001
#define PROC_PENDING_IDLE           0x00000002
#define PROC_PENDING_HIGHEST        0x00000004
#define PROC_PENDING_THERMAL        0x00000008

static
NTSTATUS
ProcRequest(
    _In_ PPROC_DEVICE Device,
    _In_ PPM_PROCESSOR_REQUEST_TYPE Type)
{
    PPM_PROCESSOR_REQUEST Request;

    PAGED_CODE();

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Signature = PPM_PROCESSOR_REQUEST_SIGNATURE;
    Request.Size = sizeof(Request);
    Request.Type = Type;
    Request.Processor = Device->Number;
    Request.Handle = (PEPHANDLE)Device;
    Request.AcceptProcessorNotification = ProcAcceptProcessorNotification;

    return ZwPowerInformation(PowerInformationInternal, &Request, sizeof(Request), NULL, 0);
}

/* New idle states or a new highest performance are only taken by registering the processor again */
_Requires_lock_held_(Device->Lock)
static
VOID
ProcRegisterAgain(
    _Inout_ PPROC_DEVICE Device)
{
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(Device->Registered);

    Status = ProcRequest(Device, PpmProcessorUnregister);
    ASSERT(NT_SUCCESS(Status));
    Device->Registered = FALSE;
    ProcReleaseTables(Device);

    ProcParseIdleStates(Device);
    ProcParsePerformance(Device);
    if ((Device->IdleCount == 0) && (Device->PerfMethod == ProcPerfNone))
    {
        DPRINT1("Processor %lu: no idle or performance states left to manage\n", Device->Index);
        return;
    }

    ProcStartPerformance(Device);
    Status = ProcRequest(Device, PpmProcessorRegister);
    if (NT_SUCCESS(Status))
    {
        Device->Registered = TRUE;
        return;
    }

    DPRINT1("Processor %lu: not registered again: 0x%08lx\n", Device->Index, Status);
    ProcStopPerformance(Device);
    ProcReleaseTables(Device);
}

static
VOID
NTAPI
ProcWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PPROC_DEVICE Device = Context;
    LONG Pending;
    BOOLEAN Constraints = FALSE;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    ASSERT(Device != NULL);

    InterlockedExchange(&Device->WorkQueued, 0);
    Pending = InterlockedExchange(&Device->PendingNotify, 0);

    KeWaitForSingleObject(&Device->Lock, Executive, KernelMode, FALSE, NULL);
    if (Device->Started && Device->Registered)
    {
        if ((Pending & PROC_PENDING_PERFORMANCE) && ProcRefreshPpc(Device))
        {
            DPRINT1("Processor %lu: _PPC is now %lu\n", Device->Index, Device->Ppc);
            Constraints = TRUE;
        }

        if (Pending & PROC_PENDING_THERMAL)
            Constraints = TRUE;

        if (Pending & (PROC_PENDING_IDLE | PROC_PENDING_HIGHEST))
        {
            DPRINT1("Processor %lu: states changed (0x%lx), registering again\n", Device->Index, Pending);
            ProcRegisterAgain(Device);
            Constraints = FALSE;
        }

        if (Constraints && Device->Registered)
        {
            Status = ProcRequest(Device, PpmProcessorPerfConstraintsChanged);
            if (!NT_SUCCESS(Status))
                DPRINT1("Processor %lu: constraints were not taken: 0x%08lx\n", Device->Index, Status);
        }
    }
    KeReleaseMutex(&Device->Lock, FALSE);

    IoReleaseRemoveLock(&Device->RemoveLock, Device);
}

static
VOID
ProcQueueWork(
    _In_ PPROC_DEVICE Device,
    _In_ LONG Pending)
{
    InterlockedOr(&Device->PendingNotify, Pending);

    if (InterlockedExchange(&Device->WorkQueued, 1) != 0)
        return;

    if (!NT_SUCCESS(IoAcquireRemoveLock(&Device->RemoveLock, Device)))
    {
        InterlockedExchange(&Device->WorkQueued, 0);
        return;
    }

    IoQueueWorkItem(Device->WorkItem, ProcWorker, DelayedWorkQueue, Device);
}

static
VOID
NTAPI
ProcNotification(
    _In_ PVOID Context,
    _In_ ULONG NotifyCode)
{
    PPROC_DEVICE Device = Context;

    switch (NotifyCode)
    {
        case PROC_NOTIFY_PERFORMANCE:
            ProcQueueWork(Device, PROC_PENDING_PERFORMANCE);
            break;

        case PROC_NOTIFY_IDLE:
            ProcQueueWork(Device, PROC_PENDING_IDLE);
            break;

        case PROC_NOTIFY_HIGHEST:
            ProcQueueWork(Device, PROC_PENDING_HIGHEST);
            break;

        default:
            break;
    }
}

static
VOID
NTAPI
ProcThermalReference(
    _In_ PVOID Context)
{
    ObReferenceObject(((PPROC_DEVICE)Context)->Self);
}

static
VOID
NTAPI
ProcThermalDereference(
    _In_ PVOID Context)
{
    ObDereferenceObject(((PPROC_DEVICE)Context)->Self);
}

static
VOID
ProcPassiveCooling(
    _In_ PVOID Context,
    _In_ ULONG Percentage)
{
    PPROC_DEVICE Device = Context;

    InterlockedExchange(&Device->ThermalLimit, (LONG)min(Percentage, 100UL));
    ProcQueueWork(Device, PROC_PENDING_THERMAL);
}

static
NTSTATUS
ProcQueryThermalInterface(
    _In_ PPROC_DEVICE Device,
    _In_ PIO_STACK_LOCATION Stack)
{
    PTHERMAL_COOLING_INTERFACE Interface;

    if ((Stack->Parameters.QueryInterface.InterfaceType == NULL) ||
        !IsEqualGUID(Stack->Parameters.QueryInterface.InterfaceType, &GUID_THERMAL_COOLING_INTERFACE))
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (!Device->Started || (Device->PerfMethod == ProcPerfNone))
        return STATUS_NOT_SUPPORTED;

    if ((Stack->Parameters.QueryInterface.Interface == NULL) ||
        (Stack->Parameters.QueryInterface.Size < sizeof(*Interface)) ||
        (Stack->Parameters.QueryInterface.Version != THERMAL_COOLING_INTERFACE_VERSION))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Interface = (PTHERMAL_COOLING_INTERFACE)Stack->Parameters.QueryInterface.Interface;
    RtlZeroMemory(Interface, sizeof(*Interface));
    Interface->Size = sizeof(*Interface);
    Interface->Version = THERMAL_COOLING_INTERFACE_VERSION;
    Interface->Context = Device;
    Interface->InterfaceReference = ProcThermalReference;
    Interface->InterfaceDereference = ProcThermalDereference;
    Interface->Flags = ThermalDeviceFlagPassiveCooling;
    Interface->PassiveCooling = ProcPassiveCooling;
    Interface->InterfaceReference(Interface->Context);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ProcCompletion(
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
ProcForward(
    _In_ PPROC_DEVICE Device,
    _In_ PIRP Irp)
{
    KEVENT Event;

    PAGED_CODE();

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, ProcCompletion, &Event, TRUE, TRUE, TRUE);
    if (IoCallDriver(Device->Lower, Irp) == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    return Irp->IoStatus.Status;
}

static
VOID
ProcAcquireAcpiInterface(
    _Inout_ PPROC_DEVICE Device)
{
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    Irp = IoAllocateIrp(Device->Lower->StackSize, FALSE);
    if (Irp == NULL)
        return;

    RtlZeroMemory(&Device->AcpiInterface, sizeof(Device->AcpiInterface));
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_ACPI_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size = sizeof(Device->AcpiInterface);
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)&Device->AcpiInterface;
    IoSetCompletionRoutine(Irp, ProcCompletion, &Event, TRUE, TRUE, TRUE);
    if (IoCallDriver(Device->Lower, Irp) == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Processor %lu: no ACPI interface: 0x%08lx\n", Device->Index, Status);
        return;
    }

    Device->AcpiInterfaceHeld = TRUE;
    Status = Device->AcpiInterface.RegisterForDeviceNotifications(Device->AcpiInterface.Context,
                                                                   ProcNotification,
                                                                   Device);
    if (NT_SUCCESS(Status))
        Device->NotificationsRegistered = TRUE;
    else
        DPRINT1("Processor %lu: notifications not registered: 0x%08lx\n", Device->Index, Status);
}

static
VOID
ProcReleaseAcpiInterface(
    _Inout_ PPROC_DEVICE Device)
{
    PAGED_CODE();

    if (Device->NotificationsRegistered)
    {
        Device->AcpiInterface.UnregisterForDeviceNotifications(Device->AcpiInterface.Context, ProcNotification);
        Device->NotificationsRegistered = FALSE;
    }

    if (Device->AcpiInterfaceHeld)
    {
        Device->AcpiInterface.InterfaceDereference(Device->AcpiInterface.Context);
        Device->AcpiInterfaceHeld = FALSE;
    }
}

static
VOID
ProcStart(
    _Inout_ PPROC_DEVICE Device)
{
    NTSTATUS Status;

    PAGED_CODE();

    KeWaitForSingleObject(&Device->Lock, Executive, KernelMode, FALSE, NULL);
    ASSERT(!Device->Started && !Device->Registered);

    if (!ProcIdentify(Device))
    {
        KeReleaseMutex(&Device->Lock, FALSE);
        return;
    }

    ProcDeclareCapabilities(Device);
    ProcParseIdleStates(Device);
    ProcParsePerformance(Device);

    if ((Device->IdleCount == 0) && (Device->PerfMethod == ProcPerfNone))
    {
        DPRINT1("Processor %lu: no idle or performance states to manage\n", Device->Index);
        Device->Started = TRUE;
        KeReleaseMutex(&Device->Lock, FALSE);
        return;
    }

    ProcStartPerformance(Device);

    Status = ProcRequest(Device, PpmProcessorRegister);
    if (NT_SUCCESS(Status))
    {
        Device->Registered = TRUE;
    }
    else
    {
        DPRINT1("Processor %lu: not registered with the kernel: 0x%08lx\n", Device->Index, Status);
        ProcStopPerformance(Device);
        ProcReleaseTables(Device);
    }

    Device->Started = TRUE;
    KeReleaseMutex(&Device->Lock, FALSE);

    if (Device->Registered)
        ProcAcquireAcpiInterface(Device);
}

static
VOID
ProcStop(
    _Inout_ PPROC_DEVICE Device)
{
    NTSTATUS Status;

    PAGED_CODE();

    ProcReleaseAcpiInterface(Device);

    KeWaitForSingleObject(&Device->Lock, Executive, KernelMode, FALSE, NULL);
    if (Device->Registered)
    {
        Status = ProcRequest(Device, PpmProcessorUnregister);
        ASSERT(NT_SUCCESS(Status));
        Device->Registered = FALSE;
        ProcStopPerformance(Device);
        ProcReleaseTables(Device);
    }

    Device->Started = FALSE;
    KeReleaseMutex(&Device->Lock, FALSE);
}

static
NTSTATUS
NTAPI
ProcDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPROC_DEVICE Device = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    PAGED_CODE();

    Status = IoAcquireRemoveLock(&Device->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = ProcForward(Device, Irp);
            if (NT_SUCCESS(Status))
                ProcStart(Device);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoReleaseRemoveLock(&Device->RemoveLock, Irp);
            return Status;

        case IRP_MN_QUERY_INTERFACE:
            Status = ProcQueryThermalInterface(Device, Stack);
            if (Status != STATUS_NOT_SUPPORTED)
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                IoReleaseRemoveLock(&Device->RemoveLock, Irp);
                return Status;
            }
            break;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            ProcStop(Device);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            ProcStop(Device);
            IoReleaseRemoveLockAndWait(&Device->RemoveLock, Irp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Device->Lower, Irp);
            IoDetachDevice(Device->Lower);
            IoFreeWorkItem(Device->WorkItem);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(Device->Lower, Irp);
    IoReleaseRemoveLock(&Device->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
ProcDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPROC_DEVICE Device = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Device->Lower, Irp);
}

static
NTSTATUS
NTAPI
ProcDispatchPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPROC_DEVICE Device = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Device->Lower, Irp);
}

static
NTSTATUS
NTAPI
ProcAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_OBJECT DeviceObject;
    PPROC_DEVICE Device;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IoCreateDevice(DriverObject,
                            sizeof(*Device),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    Device = DeviceObject->DeviceExtension;
    RtlZeroMemory(Device, sizeof(*Device));
    Device->Self = DeviceObject;
    Device->Pdo = PhysicalDeviceObject;
    Device->ThermalLimit = 100;
    IoInitializeRemoveLock(&Device->RemoveLock, PROC_TAG, 0, 0);
    KeInitializeMutex(&Device->Lock, 0);

    Device->WorkItem = IoAllocateWorkItem(DeviceObject);
    if (Device->WorkItem == NULL)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &Device->Lower);
    if (!NT_SUCCESS(Status))
    {
        IoFreeWorkItem(Device->WorkItem);
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
ProcUnload(
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
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);

    Status = AuxKlibInitialize();
    if (!NT_SUCCESS(Status))
        return Status;

    ProcPccInitialize();
    ProcReadFixedTable();

    DriverObject->MajorFunction[IRP_MJ_PNP] = ProcDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = ProcDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = ProcDispatchPassThrough;
    DriverObject->DriverExtension->AddDevice = ProcAddDevice;
    DriverObject->DriverUnload = ProcUnload;
    return STATUS_SUCCESS;
}
