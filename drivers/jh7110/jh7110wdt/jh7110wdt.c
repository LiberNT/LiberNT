/*
 * PROJECT:     LiberNT StarFive JH7110 watchdog driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     StarFive JH7110 SoC watchdog timer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

#define NDEBUG
#include <debug.h>

#define JH_SYSCRG_BASE              0x13020000ULL
#define JH_SYSCRG_SIZE              0x400
#define JH_SYSCRG_RESET_ASSERT      0x2F8
#define JH_SYSCRG_RESET_STATUS      0x308
#define JH_SYSCLK_WDT_APB           122
#define JH_SYSCLK_WDT_CORE          123
#define JH_SYSRST_WDT_APB           109
#define JH_SYSRST_WDT_CORE          110
#define JH_CLK_ENABLE               (1UL << 31)

#define JH_WDT_SIZE                 0x1000
#define JH_WDT_LOAD                 0x000
#define JH_WDT_VALUE                0x004
#define JH_WDT_CONTROL              0x008
#define JH_WDT_INTCLR               0x00C
#define JH_WDT_IMS                  0x014
#define JH_WDT_LOCK                 0xC00
#define JH_WDT_UNLOCK_KEY           0x1ACCE551UL
#define JH_WDT_CONTROL_ENABLE       (1UL << 0)
#define JH_WDT_CONTROL_RESET        (1UL << 1)
#define JH_WDT_INTCLR_CLEAR         1UL
#define JH_WDT_HZ                   24000000ULL
#define JH_WDT_TIMEOUT_SECONDS      120UL
#define JH_WDT_PING_SECONDS         30L

typedef struct _JH7110WDT_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PUCHAR Registers;
    KSPIN_LOCK Lock;
    KTIMER Timer;
    KDPC Dpc;
    BOOLEAN Armed;
} JH7110WDT_EXTENSION, *PJH7110WDT_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

static ULONG
JhWdtRead(_In_ PJH7110WDT_EXTENSION Extension, _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Extension->Registers + Offset));
}

static VOID
JhWdtWrite(_In_ PJH7110WDT_EXTENSION Extension, _In_ ULONG Offset, _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + Offset), Value);
}

static VOID
JhWdtUnlock(_In_ PJH7110WDT_EXTENSION Extension)
{
    JhWdtWrite(Extension, JH_WDT_LOCK, JH_WDT_UNLOCK_KEY);
}

static VOID
JhWdtRelock(_In_ PJH7110WDT_EXTENSION Extension)
{
    JhWdtWrite(Extension, JH_WDT_LOCK, ~JH_WDT_UNLOCK_KEY);
}

static VOID
JhWdtHaltLocked(_In_ PJH7110WDT_EXTENSION Extension)
{
    Extension->Armed = FALSE;
    JhWdtUnlock(Extension);
    JhWdtWrite(Extension, JH_WDT_CONTROL, JhWdtRead(Extension, JH_WDT_CONTROL) & ~JH_WDT_CONTROL_ENABLE);
    JhWdtRelock(Extension);
}

static VOID
JhWdtHalt(_In_ PJH7110WDT_EXTENSION Extension)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    JhWdtHaltLocked(Extension);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
}

static VOID
JhWdtDisarm(_In_ PJH7110WDT_EXTENSION Extension)
{
    KeCancelTimer(&Extension->Timer);
    KeFlushQueuedDpcs();
    if (Extension->Armed)
        JhWdtHalt(Extension);
}

static VOID
JhWdtFeedLocked(_In_ PJH7110WDT_EXTENSION Extension)
{
    JhWdtUnlock(Extension);
    JhWdtWrite(Extension, JH_WDT_INTCLR, JH_WDT_INTCLR_CLEAR);
    JhWdtWrite(Extension, JH_WDT_LOAD, (ULONG)(JH_WDT_TIMEOUT_SECONDS * JH_WDT_HZ / 2));
    JhWdtRelock(Extension);
}

static VOID NTAPI
JhWdtPing(_In_ PKDPC Dpc, _In_opt_ PVOID Context, _In_opt_ PVOID Argument1, _In_opt_ PVOID Argument2)
{
    PJH7110WDT_EXTENSION Extension = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    KeAcquireSpinLockAtDpcLevel(&Extension->Lock);
    if (Extension->Armed)
    {
        if (KD_DEBUGGER_ENABLED)
            JhWdtHaltLocked(Extension);
        else
            JhWdtFeedLocked(Extension);
    }
    KeReleaseSpinLockFromDpcLevel(&Extension->Lock);
}

static VOID
JhWdtArm(_In_ PJH7110WDT_EXTENSION Extension)
{
    LARGE_INTEGER DueTime;
    KIRQL OldIrql;

    if (KD_DEBUGGER_ENABLED)
    {
        JhWdtHalt(Extension);
        DPRINT1("JH7110WDT: kernel debugger enabled, watchdog stays stopped\n");
        return;
    }

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    JhWdtUnlock(Extension);
    JhWdtWrite(Extension, JH_WDT_CONTROL, JhWdtRead(Extension, JH_WDT_CONTROL) & ~JH_WDT_CONTROL_ENABLE);
    JhWdtWrite(Extension, JH_WDT_CONTROL, JhWdtRead(Extension, JH_WDT_CONTROL) | JH_WDT_CONTROL_RESET);
    JhWdtWrite(Extension, JH_WDT_INTCLR, JH_WDT_INTCLR_CLEAR);
    JhWdtWrite(Extension, JH_WDT_LOAD, (ULONG)(JH_WDT_TIMEOUT_SECONDS * JH_WDT_HZ / 2));
    JhWdtWrite(Extension, JH_WDT_CONTROL, JhWdtRead(Extension, JH_WDT_CONTROL) | JH_WDT_CONTROL_ENABLE);
    JhWdtRelock(Extension);
    Extension->Armed = TRUE;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    DueTime.QuadPart = -10000000LL * JH_WDT_PING_SECONDS;
    KeSetTimerEx(&Extension->Timer, DueTime, JH_WDT_PING_SECONDS * 1000, &Extension->Dpc);
    DPRINT1("JH7110WDT: running, timeout %lu s, ping every %ld s\n", JH_WDT_TIMEOUT_SECONDS, JH_WDT_PING_SECONDS);
}

static BOOLEAN
JhWdtEnableClocks(VOID)
{
    PHYSICAL_ADDRESS Address;
    PUCHAR SysCrg;
    ULONG Mask, Elapsed;
    BOOLEAN Released = FALSE;

    Address.QuadPart = JH_SYSCRG_BASE;
    SysCrg = MmMapIoSpace(Address, JH_SYSCRG_SIZE, MmNonCached);
    if (!SysCrg)
        return FALSE;
    WRITE_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCLK_WDT_APB * sizeof(ULONG)),
                         READ_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCLK_WDT_APB * sizeof(ULONG))) | JH_CLK_ENABLE);
    WRITE_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCLK_WDT_CORE * sizeof(ULONG)),
                         READ_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCLK_WDT_CORE * sizeof(ULONG))) | JH_CLK_ENABLE);
    Mask = (1UL << (JH_SYSRST_WDT_APB % 32)) | (1UL << (JH_SYSRST_WDT_CORE % 32));
    WRITE_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCRG_RESET_ASSERT + (JH_SYSRST_WDT_APB / 32) * sizeof(ULONG)),
                         READ_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCRG_RESET_ASSERT +
                                                      (JH_SYSRST_WDT_APB / 32) * sizeof(ULONG))) & ~Mask);
    for (Elapsed = 0; Elapsed < 1000; Elapsed += 10)
    {
        if ((READ_REGISTER_ULONG((PULONG)(SysCrg + JH_SYSCRG_RESET_STATUS +
                                          (JH_SYSRST_WDT_APB / 32) * sizeof(ULONG))) & Mask) == Mask)
        {
            Released = TRUE;
            break;
        }
        KeStallExecutionProcessor(10);
    }
    MmUnmapIoSpace(SysCrg, JH_SYSCRG_SIZE);
    return Released;
}

static NTSTATUS
JhWdtStart(_In_ PJH7110WDT_EXTENSION Extension, _In_ PIO_STACK_LOCATION Stack)
{
    PCM_RESOURCE_LIST Resources = Stack->Parameters.StartDevice.AllocatedResourcesTranslated;
    ULONG Index;

    if (!Resources || !Resources->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Index = 0; Index < Resources->List[0].PartialResourceList.Count; ++Index)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor =
            &Resources->List[0].PartialResourceList.PartialDescriptors[Index];

        if (Descriptor->Type == CmResourceTypeMemory && !Extension->Registers &&
            Descriptor->u.Memory.Length >= JH_WDT_SIZE)
        {
            Extension->Registers = MmMapIoSpace(Descriptor->u.Memory.Start, JH_WDT_SIZE, MmNonCached);
        }
    }
    if (!Extension->Registers)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!JhWdtEnableClocks())
        return STATUS_IO_TIMEOUT;

    JhWdtArm(Extension);
    return STATUS_SUCCESS;
}

static VOID
JhWdtRelease(_In_ PJH7110WDT_EXTENSION Extension)
{
    if (Extension->Registers)
    {
        JhWdtDisarm(Extension);
        MmUnmapIoSpace(Extension->Registers, JH_WDT_SIZE);
        Extension->Registers = NULL;
    }
}

static NTSTATUS NTAPI
JhWdtDispatchPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PJH7110WDT_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status
                                                                             : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = JhWdtStart(Extension, Stack);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("JH7110WDT: start failed 0x%08lx\n", Status);
                JhWdtRelease(Extension);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            JhWdtRelease(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            JhWdtRelease(Extension);
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
JhWdtDispatchPower(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PJH7110WDT_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    if (Stack->MinorFunction == IRP_MN_SET_POWER && Stack->Parameters.Power.Type == SystemPowerState &&
        Extension->Registers)
    {
        if (Stack->Parameters.Power.State.SystemState > PowerSystemWorking)
            JhWdtDisarm(Extension);
        else if (!Extension->Armed)
            JhWdtArm(Extension);
    }
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
JhWdtDispatchOther(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PJH7110WDT_EXTENSION Extension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
JhWdtAddDevice(_In_ PDRIVER_OBJECT DriverObject, _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PJH7110WDT_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(JH7110WDT_EXTENSION), NULL, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Self = Fdo;
    KeInitializeSpinLock(&Extension->Lock);
    KeInitializeTimerEx(&Extension->Timer, NotificationTimer);
    KeInitializeDpc(&Extension->Dpc, JhWdtPing, Extension);
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
        DriverObject->MajorFunction[Index] = JhWdtDispatchOther;
    DriverObject->MajorFunction[IRP_MJ_PNP] = JhWdtDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = JhWdtDispatchPower;
    DriverObject->DriverExtension->AddDevice = JhWdtAddDevice;
    return STATUS_SUCCESS;
}
