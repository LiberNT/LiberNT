/*
 * PROJECT:     LiberNT Apple Power Management Processor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device power votes of the Apple silicon power management processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/applertkit.h>
#include <reactos/drivers/applepmp.h>
#include <reactos/drivers/fdtdrv.h>

#define PMP_TAG                     'pPpA'
#define PMP_ENDPOINT                0x20
#define PMP_CPU_CONTROL             0x44
#define PMP_CPU_CONTROL_RUN          0x10
#define PMP_BOOT_TIMEOUT_MS         2000
#define PMP_REPLY_TIMEOUT_MS        2000
#define PMP_MAX_SHARED              4
#define PMP_SHARED_PAGE_SIZE        0x4000
#define PMP_SHARED_ADDRESS_BITS     44

typedef struct _PMP_SHARED
{
    PVOID Allocation;
    SIZE_T AllocationSize;
    PVOID Virtual;
    ULONG64 Physical;
    ULONG Size;
} PMP_SHARED, *PPMP_SHARED;

typedef struct _APPLE_PMP
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
    FDT_DRIVER_TREE Tree;
    BOOLEAN TreeOpen;
    PUCHAR Coproc;
    ULONG CoprocSize;
    PUCHAR Mailbox;
    ULONG MailboxSize;
    PUCHAR Sram;
    ULONG64 SramBase;
    ULONG SramSize;
    PMP_SHARED Shared[PMP_MAX_SHARED];
    ULONG SharedCount;
    APPLE_RTKIT Rtkit;
    FAST_MUTEX Lock;
    BOOLEAN Running;
    volatile ULONG64 Expected;
    volatile BOOLEAN Acknowledged;
    volatile BOOLEAN Lost;
} APPLE_PMP, *PAPPLE_PMP;

/* The processor reaches memory without an IOMMU, so the address it is given is a physical one. */
static
PPMP_SHARED
PmpAllocateShared(
    _Inout_ PAPPLE_PMP Pmp,
    _In_ ULONG Size)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PPMP_SHARED Shared;
    ULONG64 Physical;
    SIZE_T Rounded;
    ULONG Skip;

    if (Pmp->SharedCount == PMP_MAX_SHARED || !Size)
        return NULL;
    Shared = &Pmp->Shared[Pmp->SharedCount];
    Rounded = ((SIZE_T)Size + PMP_SHARED_PAGE_SIZE - 1) & ~(SIZE_T)(PMP_SHARED_PAGE_SIZE - 1);
    Low.QuadPart = 0;
    High.QuadPart = (LONGLONG)((1ULL << PMP_SHARED_ADDRESS_BITS) - 1);
    Boundary.QuadPart = 0;
    Shared->AllocationSize = Rounded + PMP_SHARED_PAGE_SIZE - PAGE_SIZE;
    Shared->Allocation = MmAllocateContiguousMemorySpecifyCache(Shared->AllocationSize, Low, High, Boundary,
                                                                MmNonCached);
    if (!Shared->Allocation)
        return NULL;
    Physical = (ULONG64)MmGetPhysicalAddress(Shared->Allocation).QuadPart;
    Skip = (ULONG)(((Physical + PMP_SHARED_PAGE_SIZE - 1) & ~(ULONG64)(PMP_SHARED_PAGE_SIZE - 1)) - Physical);
    Shared->Virtual = (PUCHAR)Shared->Allocation + Skip;
    Shared->Physical = Physical + Skip;
    Shared->Size = (ULONG)Rounded;
    RtlZeroMemory(Shared->Virtual, Rounded);
    Pmp->SharedCount++;
    return Shared;
}

static
VOID
PmpFreeShared(
    _Inout_ PAPPLE_PMP Pmp)
{
    ULONG Index;

    for (Index = 0; Index < Pmp->SharedCount; ++Index)
    {
        if (Pmp->Shared[Index].Allocation)
            MmFreeContiguousMemorySpecifyCache(Pmp->Shared[Index].Allocation, Pmp->Shared[Index].AllocationSize,
                                               MmNonCached);
    }
    RtlZeroMemory(Pmp->Shared, sizeof(Pmp->Shared));
    Pmp->SharedCount = 0;
}

static
BOOLEAN
NTAPI
PmpShmemSetup(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG Size,
    _Inout_ PULONG64 Iova,
    _Out_ PVOID *Virtual,
    _Out_ PBOOLEAN Preassigned)
{
    PAPPLE_PMP Pmp = Context;
    PPMP_SHARED Shared;

    *Virtual = NULL;
    *Preassigned = TRUE;
    if (*Iova)
    {
        if (!Pmp->Sram || *Iova < Pmp->SramBase || *Iova + Size > Pmp->SramBase + Pmp->SramSize)
        {
            DbgPrint("applepmp: endpoint %u asked for %lu bytes at %I64x, outside its own memory\n", Endpoint, Size,
                     *Iova);
            return FALSE;
        }
        *Virtual = Pmp->Sram + (*Iova - Pmp->SramBase);
        return TRUE;
    }
    Shared = PmpAllocateShared(Pmp, Size);
    if (!Shared)
    {
        DbgPrint("applepmp: endpoint %u asked for %lu bytes that could not be allocated\n", Endpoint, Size);
        return FALSE;
    }
    *Iova = Shared->Physical;
    *Virtual = Shared->Virtual;
    *Preassigned = FALSE;
    DbgPrint("applepmp: endpoint %u gets %lu bytes at %I64x\n", Endpoint, Shared->Size, Shared->Physical);
    return TRUE;
}

static
VOID
NTAPI
PmpReceive(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG64 Message)
{
    PAPPLE_PMP Pmp = Context;
    ULONG Command = (ULONG)((Message >> APPLE_PMP_COMMAND_SHIFT) & 0xff);

    if (Endpoint != PMP_ENDPOINT || !(Message & APPLE_PMP_COMMAND_MARKER))
    {
        DbgPrint("applepmp: endpoint %#x sent %I64x\n", Endpoint, Message);
        return;
    }
    if (Command != APPLE_PMP_SET_POWER + 1)
        return;
    /* The processor echoes no request id, so a reply that is not the outstanding one loses the pairing. */
    if (Pmp->Expected && Message == Pmp->Expected)
        Pmp->Acknowledged = TRUE;
    else
        Pmp->Lost = TRUE;
}

static
NTSTATUS
PmpSetDevicePower(
    _Inout_ PAPPLE_PMP Pmp,
    _In_ USHORT Device,
    _In_ BOOLEAN Enable)
{
    ULONG64 Message, Deadline;
    LARGE_INTEGER Frequency, Counter;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Device > 15)
        return STATUS_INVALID_PARAMETER;
    ExAcquireFastMutex(&Pmp->Lock);
    if (!Pmp->Running || Pmp->Lost)
    {
        ExReleaseFastMutex(&Pmp->Lock);
        return Pmp->Lost ? STATUS_DEVICE_PROTOCOL_ERROR : STATUS_DEVICE_NOT_READY;
    }
    Message = ApplePmpPowerMessage(APPLE_PMP_SET_POWER, Device, Enable);
    Pmp->Expected = ApplePmpPowerReply(APPLE_PMP_SET_POWER);
    Pmp->Acknowledged = FALSE;
    if (!AppleRtkitSend(&Pmp->Rtkit, PMP_ENDPOINT, Message))
    {
        Pmp->Expected = 0;
        ExReleaseFastMutex(&Pmp->Lock);
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    Counter = KeQueryPerformanceCounter(&Frequency);
    Deadline = (ULONG64)Counter.QuadPart + (ULONG64)Frequency.QuadPart * PMP_REPLY_TIMEOUT_MS / 1000;
    while (!Pmp->Acknowledged && !Pmp->Lost && !Pmp->Rtkit.Failed)
    {
        AppleRtkitPoll(&Pmp->Rtkit);
        Counter = KeQueryPerformanceCounter(&Frequency);
        if ((ULONG64)Counter.QuadPart > Deadline)
            break;
        KeStallExecutionProcessor(100);
    }
    if (!Pmp->Acknowledged)
    {
        /* A late reply can no longer be paired with a request, so no further vote is attempted. */
        Pmp->Lost = TRUE;
        Status = Pmp->Rtkit.Failed ? STATUS_DEVICE_HARDWARE_ERROR : STATUS_IO_TIMEOUT;
    }
    Pmp->Expected = 0;
    ExReleaseFastMutex(&Pmp->Lock);
    DbgPrint("applepmp: device %u power %u: %lx\n", Device, Enable, Status);
    return Status;
}

static
VOID
PmpStop(
    _Inout_ PAPPLE_PMP Pmp)
{
    Pmp->Running = FALSE;
    PmpFreeShared(Pmp);
    if (Pmp->Sram)
        MmUnmapIoSpace(Pmp->Sram, Pmp->SramSize);
    Pmp->Sram = NULL;
    if (Pmp->Mailbox)
        MmUnmapIoSpace(Pmp->Mailbox, Pmp->MailboxSize);
    Pmp->Mailbox = NULL;
    if (Pmp->Coproc)
        MmUnmapIoSpace(Pmp->Coproc, Pmp->CoprocSize);
    Pmp->Coproc = NULL;
    if (Pmp->TreeOpen)
        FdtDriverClose(&Pmp->Tree);
    Pmp->TreeOpen = FALSE;
}

static
NTSTATUS
PmpStart(
    _Inout_ PAPPLE_PMP Pmp)
{
    ULONG MailboxPhandle, MailboxNode, Control;
    NTSTATUS Status;

    Status = FdtDriverOpen(Pmp->Pdo, &Pmp->Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    Pmp->TreeOpen = TRUE;
    if (!RiscvFdtReadU32(&Pmp->Tree.Fdt, Pmp->Tree.Node, "mboxes", &MailboxPhandle) ||
        (MailboxNode = FdtDriverFindPhandle(&Pmp->Tree, MailboxPhandle)) == RISCV_FDT_NO_NODE)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Pmp->Coproc = FdtDriverMapReg(&Pmp->Tree, Pmp->Tree.Node, 0, NULL, &Pmp->CoprocSize);
    Pmp->Sram = FdtDriverMapReg(&Pmp->Tree, Pmp->Tree.Node, 1, &Pmp->SramBase, &Pmp->SramSize);
    Pmp->Mailbox = FdtDriverMapReg(&Pmp->Tree, MailboxNode, 0, NULL, &Pmp->MailboxSize);
    if (!Pmp->Coproc || !Pmp->Sram || !Pmp->Mailbox)
        return STATUS_INSUFFICIENT_RESOURCES;

    Pmp->Rtkit.Name = "applepmp";
    Pmp->Rtkit.Mailbox = Pmp->Mailbox;
    Pmp->Rtkit.Context = Pmp;
    Pmp->Rtkit.ShmemSetup = PmpShmemSetup;
    Pmp->Rtkit.Receive = PmpReceive;
    /* The boot loader leaves this processor halted with its firmware in SRAM; it is started here. */
    Control = READ_REGISTER_ULONG((volatile ULONG *)(Pmp->Coproc + PMP_CPU_CONTROL));
    if (Control & PMP_CPU_CONTROL_RUN)
    {
        Status = AppleRtkitWake(&Pmp->Rtkit, PMP_BOOT_TIMEOUT_MS);
    }
    else
    {
        WRITE_REGISTER_ULONG((volatile ULONG *)(Pmp->Coproc + PMP_CPU_CONTROL), Control | PMP_CPU_CONTROL_RUN);
        Status = AppleRtkitBoot(&Pmp->Rtkit, PMP_BOOT_TIMEOUT_MS);
    }
    DbgPrint("applepmp: SRAM %I64x+%lx, control %08lx, %s %lx, RTKit %lu, endpoints %08lx %08lx\n", Pmp->SramBase,
             Pmp->SramSize, Control, (Control & PMP_CPU_CONTROL_RUN) ? "wake" : "boot", Status, Pmp->Rtkit.Version,
             Pmp->Rtkit.Endpoints[0], Pmp->Rtkit.Endpoints[1]);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!AppleRtkitHasEndpoint(&Pmp->Rtkit, PMP_ENDPOINT))
    {
        DbgPrint("applepmp: the processor has no power endpoint\n");
        return STATUS_NOT_SUPPORTED;
    }
    AppleRtkitStartEndpoint(&Pmp->Rtkit, PMP_ENDPOINT);
    Pmp->Running = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
PmpDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PMP Pmp = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PAPPLE_PMP_POWER_REQUEST Request = Irp->AssociatedIrp.SystemBuffer;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;

    if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_APPLE_PMP_SET_POWER)
    {
        if (Request && Stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(*Request))
            Status = PmpSetDevicePower(Pmp, Request->Device, Request->Enable);
        else
            Status = STATUS_INVALID_PARAMETER;
    }
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
PmpCreateClose(
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
PmpPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PMP Pmp = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Pmp->Lower, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
            {
                Status = PmpStart(Pmp);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("applepmp: the power management processor did not start: %lx\n", Status);
                    PmpStop(Pmp);
                }
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_STOP_DEVICE:
            PmpStop(Pmp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            PmpStop(Pmp);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Pmp->Lower, Irp);
            IoDetachDevice(Pmp->Lower);
            IoDeleteDevice(DeviceObject);
            return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Pmp->Lower, Irp);
}

static
NTSTATUS
NTAPI
PmpPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PMP Pmp = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Pmp->Lower, Irp);
}

static
NTSTATUS
NTAPI
PmpPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PMP Pmp = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Pmp->Lower, Irp);
}

static
NTSTATUS
NTAPI
PmpAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT Pdo)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(APPLE_PMP_DEVICE_NAME);
    PAPPLE_PMP Pmp;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(APPLE_PMP), &Name, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
                            FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Pmp = Fdo->DeviceExtension;
    RtlZeroMemory(Pmp, sizeof(*Pmp));
    ExInitializeFastMutex(&Pmp->Lock);
    Pmp->Self = Fdo;
    Pmp->Pdo = Pdo;
    Pmp->Lower = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (!Pmp->Lower)
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
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = PmpPassThrough;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = PmpCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = PmpCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PmpDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = PmpPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = PmpPower;
    DriverObject->DriverExtension->AddDevice = PmpAddDevice;
    return STATUS_SUCCESS;
}
