/*
 * PROJECT:     LiberNT SpacemiT K1 watchdog driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SpacemiT K1 SoC watchdog timer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

#define NDEBUG
#include <debug.h>

#define K1X_MPMU_WDTPCR             0xD4050200ULL
#define K1X_MPMU_WDTPCR_CLOCKS      0x3UL
#define K1X_MPMU_WDTPCR_RESET       0x4UL
#define K1X_MPMU_APRR_WDTR          (1UL << 4)

#define K1X_WDT_SIZE                0x100
#define K1X_WDT_WFAR                0xB0
#define K1X_WDT_WSAR                0xB4
#define K1X_WDT_WMER                0xB8
#define K1X_WDT_WMR                 0xBC
#define K1X_WDT_WSR                 0xC0
#define K1X_WDT_WCR                 0xC8
#define K1X_WDT_WVR                 0xCC
#define K1X_WDT_KEY1                0xBABAUL
#define K1X_WDT_KEY2                0xEB10UL
#define K1X_WDT_WMER_RUN            0x3UL
#define K1X_WDT_HZ                  256UL
#define K1X_WDT_TIMEOUT_SECONDS     255UL
#define K1X_WDT_PING_SECONDS        30L

typedef struct _K1XWDT_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PUCHAR Registers;
    PULONG Aprr;
    KSPIN_LOCK Lock;
    KTIMER Timer;
    KDPC Dpc;
    BOOLEAN Armed;
} K1XWDT_EXTENSION, *PK1XWDT_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

static VOID
K1WdtWrite(_In_ PK1XWDT_EXTENSION Extension, _In_ ULONG Offset, _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + K1X_WDT_WFAR), K1X_WDT_KEY1);
    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + K1X_WDT_WSAR), K1X_WDT_KEY2);
    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + Offset), Value);
}

static ULONG
K1WdtRead(_In_ PK1XWDT_EXTENSION Extension, _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Extension->Registers + Offset));
}

static VOID
K1WdtHalt(_In_ PK1XWDT_EXTENSION Extension)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Extension->Armed = FALSE;
    K1WdtWrite(Extension, K1X_WDT_WCR, 1);
    K1WdtWrite(Extension, K1X_WDT_WMER, 0);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
}

static VOID
K1WdtDisarm(_In_ PK1XWDT_EXTENSION Extension)
{
    KeCancelTimer(&Extension->Timer);
    KeFlushQueuedDpcs();
    if (Extension->Armed)
        K1WdtHalt(Extension);
}

static VOID NTAPI
K1WdtPing(_In_ PKDPC Dpc, _In_opt_ PVOID Context, _In_opt_ PVOID Argument1, _In_opt_ PVOID Argument2)
{
    PK1XWDT_EXTENSION Extension = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    KeAcquireSpinLockAtDpcLevel(&Extension->Lock);
    if (Extension->Armed)
    {
        if (KD_DEBUGGER_ENABLED)
        {
            Extension->Armed = FALSE;
            K1WdtWrite(Extension, K1X_WDT_WCR, 1);
            K1WdtWrite(Extension, K1X_WDT_WMER, 0);
        }
        else
        {
            K1WdtWrite(Extension, K1X_WDT_WCR, 1);
        }
    }
    KeReleaseSpinLockFromDpcLevel(&Extension->Lock);
}

static VOID
K1WdtArm(_In_ PK1XWDT_EXTENSION Extension)
{
    LARGE_INTEGER DueTime;
    KIRQL OldIrql;

    if (KD_DEBUGGER_ENABLED)
    {
        K1WdtHalt(Extension);
        DPRINT1("K1XWDT: kernel debugger enabled, watchdog stays stopped\n");
        return;
    }

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    K1WdtWrite(Extension, K1X_WDT_WCR, 1);
    K1WdtWrite(Extension, K1X_WDT_WSR, 0);
    K1WdtWrite(Extension, K1X_WDT_WMR, K1X_WDT_TIMEOUT_SECONDS * K1X_WDT_HZ);
    K1WdtWrite(Extension, K1X_WDT_WMER, K1X_WDT_WMER_RUN);
    K1WdtWrite(Extension, K1X_WDT_WCR, 1);
    WRITE_REGISTER_ULONG(Extension->Aprr, READ_REGISTER_ULONG(Extension->Aprr) | K1X_MPMU_APRR_WDTR);
    Extension->Armed = TRUE;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    DueTime.QuadPart = -10000000LL * K1X_WDT_PING_SECONDS;
    KeSetTimerEx(&Extension->Timer, DueTime, K1X_WDT_PING_SECONDS * 1000, &Extension->Dpc);
    DPRINT1("K1XWDT: running, timeout %lu s, ping every %ld s\n", K1X_WDT_TIMEOUT_SECONDS,
            K1X_WDT_PING_SECONDS);
}

static NTSTATUS
K1WdtStart(_In_ PK1XWDT_EXTENSION Extension, _In_ PIO_STACK_LOCATION Stack)
{
    PCM_RESOURCE_LIST Resources = Stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    PHYSICAL_ADDRESS Address;
    PULONG Clock;
    ULONG Index;

    if (!Resources || !Resources->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Index = 0; Index < Resources->List[0].PartialResourceList.Count; ++Index)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor =
            &Resources->List[0].PartialResourceList.PartialDescriptors[Index];

        if (Descriptor->Type != CmResourceTypeMemory)
            continue;
        if (!Extension->Registers && Descriptor->u.Memory.Length >= K1X_WDT_SIZE)
            Extension->Registers = MmMapIoSpace(Descriptor->u.Memory.Start, K1X_WDT_SIZE, MmNonCached);
        else if (!Extension->Aprr && Descriptor->u.Memory.Length >= sizeof(ULONG))
            Extension->Aprr = MmMapIoSpace(Descriptor->u.Memory.Start, sizeof(ULONG), MmNonCached);
    }
    if (!Extension->Registers || !Extension->Aprr)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Address.QuadPart = K1X_MPMU_WDTPCR;
    Clock = MmMapIoSpace(Address, sizeof(ULONG), MmNonCached);
    if (!Clock)
        return STATUS_INSUFFICIENT_RESOURCES;
    WRITE_REGISTER_ULONG(Clock, (READ_REGISTER_ULONG(Clock) | K1X_MPMU_WDTPCR_CLOCKS) & ~K1X_MPMU_WDTPCR_RESET);
    MmUnmapIoSpace(Clock, sizeof(ULONG));

    DPRINT1("K1XWDT: last reset %s the watchdog (status 0x%lx, enable 0x%lx, count 0x%lx)\n",
            K1WdtRead(Extension, K1X_WDT_WSR) ? "by" : "not by", K1WdtRead(Extension, K1X_WDT_WSR),
            K1WdtRead(Extension, K1X_WDT_WMER), K1WdtRead(Extension, K1X_WDT_WVR));
    K1WdtArm(Extension);
    return STATUS_SUCCESS;
}

static VOID
K1WdtRelease(_In_ PK1XWDT_EXTENSION Extension)
{
    K1WdtDisarm(Extension);
    if (Extension->Registers)
    {
        MmUnmapIoSpace(Extension->Registers, K1X_WDT_SIZE);
        Extension->Registers = NULL;
    }
    if (Extension->Aprr)
    {
        MmUnmapIoSpace(Extension->Aprr, sizeof(ULONG));
        Extension->Aprr = NULL;
    }
}

static NTSTATUS NTAPI
K1WdtDispatchPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XWDT_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status
                                                                             : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = K1WdtStart(Extension, Stack);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("K1XWDT: start failed 0x%08lx\n", Status);
                K1WdtRelease(Extension);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            K1WdtRelease(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            K1WdtRelease(Extension);
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
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1WdtDispatchPower(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XWDT_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    if (Stack->MinorFunction == IRP_MN_SET_POWER && Stack->Parameters.Power.Type == SystemPowerState &&
        Extension->Registers)
    {
        if (Stack->Parameters.Power.State.SystemState > PowerSystemWorking)
            K1WdtDisarm(Extension);
        else if (!Extension->Armed)
            K1WdtArm(Extension);
    }
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1WdtDispatchOther(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XWDT_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1WdtAddDevice(_In_ PDRIVER_OBJECT DriverObject, _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PK1XWDT_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(K1XWDT_EXTENSION), NULL, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Self = Fdo;
    KeInitializeSpinLock(&Extension->Lock);
    KeInitializeTimerEx(&Extension->Timer, NotificationTimer);
    KeInitializeDpc(&Extension->Dpc, K1WdtPing, Extension);
    Extension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = K1WdtDispatchOther;
    DriverObject->MajorFunction[IRP_MJ_PNP] = K1WdtDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = K1WdtDispatchPower;
    DriverObject->DriverExtension->AddDevice = K1WdtAddDevice;
    return STATUS_SUCCESS;
}
