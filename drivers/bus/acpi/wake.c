/*
 * PROJECT:     LiberNT ACPI bus driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device wake: _PRW, wake GPEs and IRP_MN_WAIT_WAKE
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

#define ACPI_WAKE_TAG 'kWcA'

typedef struct _ACPI_WAKE_CONTEXT
{
    LIST_ENTRY Link;
    struct acpi_device *Device;
    PIRP Irp;
    BOOLEAN Armed;
    BOOLEAN WorkQueued;
    BOOLEAN Removed;
    WORK_QUEUE_ITEM WorkItem;
} ACPI_WAKE_CONTEXT, *PACPI_WAKE_CONTEXT;

static KSPIN_LOCK AcpiWakeLock;
static LIST_ENTRY AcpiWakeList;

static WORKER_THREAD_ROUTINE AcpiWakeWorker;
static DRIVER_CANCEL AcpiWakeCancel;

VOID
AcpiWakeInitialize(VOID)
{
    KeInitializeSpinLock(&AcpiWakeLock);
    InitializeListHead(&AcpiWakeList);
}

VOID
AcpiWakeProbeDevice(
    _Inout_ struct acpi_device *Device)
{
    ACPI_BUFFER Buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    PACPI_WAKE_CONTEXT Context;
    ACPI_OBJECT *Package;
    ACPI_OBJECT *Element;
    ACPI_HANDLE GpeDevice;
    UINT64 GpeNumber;
    UINT32 Index;
    ACPI_STATUS Status;
    KIRQL OldIrql;

    Status = AcpiEvaluateObject(Device->handle, "_PRW", NULL, &Buffer);
    if (ACPI_FAILURE(Status))
        return;

    Package = Buffer.Pointer;
    if (Package == NULL || Package->Type != ACPI_TYPE_PACKAGE || Package->Package.Count < 2)
        goto Done;

    Element = &Package->Package.Elements[0];
    if (Element->Type == ACPI_TYPE_INTEGER)
    {
        GpeDevice = NULL;
        GpeNumber = Element->Integer.Value;
    }
    else if (Element->Type == ACPI_TYPE_PACKAGE &&
             Element->Package.Count == 2 &&
             Element->Package.Elements[0].Type == ACPI_TYPE_LOCAL_REFERENCE &&
             Element->Package.Elements[1].Type == ACPI_TYPE_INTEGER)
    {
        GpeDevice = Element->Package.Elements[0].Reference.Handle;
        GpeNumber = Element->Package.Elements[1].Integer.Value;
    }
    else
    {
        goto Done;
    }

    Element = &Package->Package.Elements[1];
    if (Element->Type != ACPI_TYPE_INTEGER || GpeNumber > MAXULONG)
        goto Done;

    Device->wakeup.resources.count = 0;
    for (Index = 2; Index < Package->Package.Count; Index++)
    {
        if (Package->Package.Elements[Index].Type != ACPI_TYPE_LOCAL_REFERENCE ||
            Device->wakeup.resources.count == ACPI_MAX_HANDLES)
        {
            goto Done;
        }

        Device->wakeup.resources.handles[Device->wakeup.resources.count++] =
            Package->Package.Elements[Index].Reference.Handle;
    }

    Context = ExAllocatePoolZero(NonPagedPool, sizeof(*Context), ACPI_WAKE_TAG);
    if (Context == NULL)
        goto Done;

    Status = AcpiSetupGpeForWake(Device->handle, GpeDevice, (UINT32)GpeNumber);
    if (ACPI_FAILURE(Status))
    {
        DPRINT1("Wake GPE 0x%I64x of %s could not be set up: %s\n",
                GpeNumber, Device->pnp.bus_id, AcpiFormatException(Status));
        ExFreePoolWithTag(Context, ACPI_WAKE_TAG);
        goto Done;
    }

    Context->Device = Device;
    ExInitializeWorkItem(&Context->WorkItem, AcpiWakeWorker, Context);

    Device->wakeup.gpe_device = GpeDevice;
    Device->wakeup.gpe_number = GpeNumber;
    Device->wakeup.sleep_state = Element->Integer.Value;
    Device->wakeup.context = Context;
    Device->wakeup.flags.valid = 1;
    Device->flags.wake_capable = 1;

    KeAcquireSpinLock(&AcpiWakeLock, &OldIrql);
    InsertTailList(&AcpiWakeList, &Context->Link);
    KeReleaseSpinLock(&AcpiWakeLock, OldIrql);

    DPRINT1("%s can wake the system from S%I64u through GPE 0x%I64x\n",
            Device->pnp.bus_id, Device->wakeup.sleep_state, GpeNumber);

Done:
    if (Buffer.Pointer)
        AcpiOsFree(Buffer.Pointer);
}

static
SYSTEM_POWER_STATE
AcpiWakeSystemState(
    _In_ struct acpi_device *Device)
{
    ULONG SleepState = (ULONG)min(Device->wakeup.sleep_state, (ACPI_INTEGER)ACPI_STATE_S4);

    if (SleepState == ACPI_STATE_S0)
        return PowerSystemWorking;

    return (SYSTEM_POWER_STATE)(PowerSystemSleeping1 + SleepState - 1);
}

VOID
AcpiWakeQueryCapabilities(
    _In_ struct acpi_device *Device,
    _Inout_ PDEVICE_CAPABILITIES Capabilities)
{
    static const PCSTR SleepDeviceState[] = { "_S1D", "_S2D", "_S3D", "_S4D" };
    static const PCSTR SleepWakeState[] = { "_S1W", "_S2W", "_S3W", "_S4W" };
    unsigned long long Value;
    ULONG SleepState;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(SleepDeviceState); Index++)
    {
        if (ACPI_SUCCESS(acpi_evaluate_integer(Device->handle, (ACPI_STRING)SleepDeviceState[Index], NULL, &Value)) &&
            Value <= ACPI_STATE_D3_HOT)
        {
            Capabilities->DeviceState[PowerSystemSleeping1 + Index] = (DEVICE_POWER_STATE)(PowerDeviceD0 + Value);
        }
    }

    Capabilities->WakeFromD0 = FALSE;
    Capabilities->WakeFromD1 = FALSE;
    Capabilities->WakeFromD2 = FALSE;
    Capabilities->WakeFromD3 = FALSE;
    Capabilities->SystemWake = PowerSystemUnspecified;
    Capabilities->DeviceWake = PowerDeviceUnspecified;

    if (!Device->wakeup.flags.valid)
        return;

    SleepState = (ULONG)min(Device->wakeup.sleep_state, (ACPI_INTEGER)ACPI_STATE_S4);
    Capabilities->SystemWake = AcpiWakeSystemState(Device);
    if (SleepState == ACPI_STATE_S0)
    {
        Capabilities->DeviceWake = PowerDeviceD0;
    }
    else
    {
        Capabilities->DeviceWake = Capabilities->DeviceState[Capabilities->SystemWake];
        if (ACPI_SUCCESS(acpi_evaluate_integer(Device->handle, (ACPI_STRING)SleepWakeState[SleepState - 1], NULL, &Value)) &&
            Value <= ACPI_STATE_D3_HOT)
        {
            Capabilities->DeviceWake = (DEVICE_POWER_STATE)(PowerDeviceD0 + Value);
        }
        if (Capabilities->DeviceWake == PowerDeviceUnspecified)
            Capabilities->DeviceWake = PowerDeviceD0;
    }

    Capabilities->WakeFromD0 = TRUE;
    Capabilities->WakeFromD1 = Capabilities->DeviceWake >= PowerDeviceD1;
    Capabilities->WakeFromD2 = Capabilities->DeviceWake >= PowerDeviceD2;
    Capabilities->WakeFromD3 = Capabilities->DeviceWake >= PowerDeviceD3;
}

static
VOID
AcpiWakeQueueWorkLocked(
    _Inout_ PACPI_WAKE_CONTEXT Context)
{
    if (Context->WorkQueued)
        return;

    Context->WorkQueued = TRUE;
    ExQueueWorkItem(&Context->WorkItem, DelayedWorkQueue);
}

static
VOID
NTAPI
AcpiWakeWorker(
    _In_ PVOID Parameter)
{
    PACPI_WAKE_CONTEXT Context = Parameter;
    struct acpi_device *Device = Context->Device;
    BOOLEAN Wanted;
    BOOLEAN Free;
    KIRQL OldIrql;

    for (;;)
    {
        KeAcquireSpinLock(&AcpiWakeLock, &OldIrql);
        Wanted = (Context->Irp != NULL) && !Context->Removed;
        if (Wanted == Context->Armed)
        {
            Context->WorkQueued = FALSE;
            Free = Context->Removed;
            KeReleaseSpinLock(&AcpiWakeLock, OldIrql);
            if (Free)
                ExFreePoolWithTag(Context, ACPI_WAKE_TAG);
            return;
        }
        KeReleaseSpinLock(&AcpiWakeLock, OldIrql);

        if (Wanted)
        {
            acpi_enable_wakeup_device_power(Device, (int)Device->wakeup.sleep_state);
            AcpiSetGpeWakeMask(Device->wakeup.gpe_device, (UINT32)Device->wakeup.gpe_number, ACPI_GPE_ENABLE);
            DPRINT1("%s is armed to wake the system\n", Device->pnp.bus_id);
        }
        else
        {
            AcpiSetGpeWakeMask(Device->wakeup.gpe_device, (UINT32)Device->wakeup.gpe_number, ACPI_GPE_DISABLE);
            acpi_disable_wakeup_device_power(Device);
        }

        Context->Armed = Wanted;
    }
}

static
PIRP
AcpiWakeTakeIrpLocked(
    _Inout_ PACPI_WAKE_CONTEXT Context)
{
    PIRP Irp = Context->Irp;

    if (Irp == NULL || IoSetCancelRoutine(Irp, NULL) == NULL)
        return NULL;

    Context->Irp = NULL;
    AcpiWakeQueueWorkLocked(Context);
    return Irp;
}

static
VOID
NTAPI
AcpiWakeCancel(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PACPI_WAKE_CONTEXT Context = Irp->Tail.Overlay.DriverContext[0];
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&AcpiWakeLock, &OldIrql);
    if (Context->Irp == Irp)
    {
        Context->Irp = NULL;
        AcpiWakeQueueWorkLocked(Context);
    }
    KeReleaseSpinLock(&AcpiWakeLock, OldIrql);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

NTSTATUS
AcpiWakeQueue(
    _In_ struct acpi_device *Device,
    _Inout_ PIRP Irp,
    _In_ SYSTEM_POWER_STATE SystemState)
{
    PACPI_WAKE_CONTEXT Context = Device->wakeup.context;
    KIRQL OldIrql;

    if (!Device->wakeup.flags.valid || Context == NULL)
        return STATUS_NOT_SUPPORTED;

    if (SystemState > AcpiWakeSystemState(Device))
        return STATUS_INVALID_DEVICE_STATE;

    KeAcquireSpinLock(&AcpiWakeLock, &OldIrql);
    if (Context->Irp != NULL || Context->Removed)
    {
        KeReleaseSpinLock(&AcpiWakeLock, OldIrql);
        return STATUS_DEVICE_BUSY;
    }

    IoMarkIrpPending(Irp);
    Irp->Tail.Overlay.DriverContext[0] = Context;
    Context->Irp = Irp;
    IoSetCancelRoutine(Irp, AcpiWakeCancel);
    if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL) != NULL)
    {
        Context->Irp = NULL;
        KeReleaseSpinLock(&AcpiWakeLock, OldIrql);
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_PENDING;
    }

    AcpiWakeQueueWorkLocked(Context);
    KeReleaseSpinLock(&AcpiWakeLock, OldIrql);
    return STATUS_PENDING;
}

NTSTATUS
AcpiWakeQueueIrp(
    _Inout_ PPDO_DEVICE_DATA PdoData,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    struct acpi_device *Device = NULL;
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);

    if (PdoData->AcpiHandle == NULL || acpi_bus_get_device(PdoData->AcpiHandle, &Device) != 0 || Device == NULL)
        Status = STATUS_NOT_SUPPORTED;
    else
        Status = AcpiWakeQueue(Device, Irp, Stack->Parameters.WaitWake.PowerState);

    if (Status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    return Status;
}

static
VOID
AcpiWakeComplete(
    _In_ ACPI_HANDLE Handle,
    _In_ NTSTATUS Status)
{
    struct acpi_device *Device = NULL;
    PACPI_WAKE_CONTEXT Context;
    KIRQL OldIrql;
    PIRP Irp;

    if (acpi_bus_get_device(Handle, &Device) != 0 || Device == NULL)
        return;

    Context = Device->wakeup.context;
    if (Context == NULL)
        return;

    KeAcquireSpinLock(&AcpiWakeLock, &OldIrql);
    Irp = AcpiWakeTakeIrpLocked(Context);
    KeReleaseSpinLock(&AcpiWakeLock, OldIrql);

    if (Irp == NULL)
        return;

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

VOID
AcpiWakeSignal(
    _In_ ACPI_HANDLE Handle)
{
    AcpiWakeComplete(Handle, STATUS_SUCCESS);
}

VOID
AcpiWakeCancelDevice(
    _In_ ACPI_HANDLE Handle)
{
    AcpiWakeComplete(Handle, STATUS_NO_SUCH_DEVICE);
}

VOID
AcpiWakeRemoveDevice(
    _Inout_ struct acpi_device *Device)
{
    PACPI_WAKE_CONTEXT Context = Device->wakeup.context;
    BOOLEAN Free = FALSE;
    KIRQL OldIrql;

    if (Context == NULL)
        return;

    AcpiWakeComplete(Device->handle, STATUS_NO_SUCH_DEVICE);

    KeAcquireSpinLock(&AcpiWakeLock, &OldIrql);
    RemoveEntryList(&Context->Link);
    Context->Removed = TRUE;
    Device->wakeup.context = NULL;
    if (!Context->WorkQueued)
    {
        if (Context->Armed)
            AcpiWakeQueueWorkLocked(Context);
        else
            Free = TRUE;
    }
    KeReleaseSpinLock(&AcpiWakeLock, OldIrql);

    if (Free)
        ExFreePoolWithTag(Context, ACPI_WAKE_TAG);
}

VOID
AcpiWakeCheckSources(VOID)
{
    struct acpi_device *Device;
    PACPI_WAKE_CONTEXT Context;
    ACPI_EVENT_STATUS EventStatus;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    PIRP Irp;

    KeAcquireSpinLock(&AcpiWakeLock, &OldIrql);
    for (Entry = AcpiWakeList.Flink; Entry != &AcpiWakeList; Entry = Entry->Flink)
    {
        Context = CONTAINING_RECORD(Entry, ACPI_WAKE_CONTEXT, Link);
        Device = Context->Device;
        if (Context->Irp == NULL ||
            ACPI_FAILURE(AcpiGetGpeStatus(Device->wakeup.gpe_device, (UINT32)Device->wakeup.gpe_number, &EventStatus)) ||
            !(EventStatus & ACPI_EVENT_FLAG_STATUS_SET))
        {
            continue;
        }

        AcpiClearGpe(Device->wakeup.gpe_device, (UINT32)Device->wakeup.gpe_number);
        Irp = AcpiWakeTakeIrpLocked(Context);
        if (Irp == NULL)
            continue;

        DPRINT1("%s woke the system\n", Device->pnp.bus_id);
        KeReleaseSpinLock(&AcpiWakeLock, OldIrql);

        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        KeAcquireSpinLock(&AcpiWakeLock, &OldIrql);
        Entry = &AcpiWakeList;
    }
    KeReleaseSpinLock(&AcpiWakeLock, OldIrql);
}

static
NTSTATUS
AcpiWakeFindPciDevice(
    _In_ PIRP Irp,
    _Out_ PACPI_PCI_WAKE_INPUT_BUFFER *Input,
    _Out_ struct acpi_device **Device)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    ACPI_HANDLE Handle = NULL;

    *Device = NULL;
    *Input = Irp->AssociatedIrp.SystemBuffer;
    if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ACPI_PCI_WAKE_INPUT_BUFFER) ||
        *Input == NULL ||
        (*Input)->Signature != ACPI_PCI_WAKE_INPUT_BUFFER_SIGNATURE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!AcpiFindPciDeviceInNamespace((*Input)->Segment, (*Input)->Bus, (*Input)->Device, (*Input)->Function, &Handle) ||
        acpi_bus_get_device(Handle, Device) != 0 ||
        *Device == NULL)
    {
        return STATUS_NOT_FOUND;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
AcpiQueryWakeForPciDeviceIoctl(
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);
    PACPI_PCI_WAKE_INPUT_BUFFER Input;
    PACPI_PCI_WAKE_INFORMATION Output;
    DEVICE_CAPABILITIES Capabilities;
    struct acpi_device *Device;
    NTSTATUS Status;
    ULONG Index;

    if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ACPI_PCI_WAKE_INFORMATION))
        return STATUS_BUFFER_TOO_SMALL;

    Status = AcpiWakeFindPciDevice(Irp, &Input, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Capabilities, sizeof(Capabilities));
    for (Index = PowerSystemSleeping1; Index < PowerSystemMaximum; Index++)
        Capabilities.DeviceState[Index] = PowerDeviceUnspecified;
    AcpiWakeQueryCapabilities(Device, &Capabilities);

    Output = Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(Output, sizeof(*Output));
    Output->SystemWake = Capabilities.SystemWake;
    Output->DeviceWake = Capabilities.DeviceWake;
    for (Index = 0; Index < PowerSystemMaximum; Index++)
        Output->DeviceState[Index] = Capabilities.DeviceState[Index];

    Irp->IoStatus.Information = sizeof(*Output);
    return STATUS_SUCCESS;
}

NTSTATUS
AcpiWaitWakeForPciDeviceIoctl(
    _Inout_ PIRP Irp)
{
    PACPI_PCI_WAKE_INPUT_BUFFER Input;
    struct acpi_device *Device;
    NTSTATUS Status;

    Status = AcpiWakeFindPciDevice(Irp, &Input, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    return AcpiWakeQueue(Device, Irp, (SYSTEM_POWER_STATE)Input->SystemState);
}
