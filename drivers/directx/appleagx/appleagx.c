/*
 * PROJECT:     LiberNT Apple Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Identification of the Apple silicon graphics processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/applepmp.h>
#include <reactos/drivers/applertkit.h>
#include <reactos/drivers/appleuat.h>
#include <reactos/drivers/fdtdrv.h>

#define AGX_TAG                     'xgAA'
#define AGX_ASC_CONTROL             0x44
#define AGX_ASC_CONTROL_RUN         0x10
#define AGX_SGX_ID_WINDOW           0xd04000
#define AGX_SGX_CORE_WINDOW         0xe00000
#define AGX_SGX_WINDOW_SIZE         0x4000
#define AGX_ID_VERSION              0x00
#define AGX_ID_UNKNOWN08            0x08
#define AGX_ID_COUNTS1              0x10
#define AGX_ID_COUNTS2              0x14
#define AGX_ID_UNKNOWN18            0x18
#define AGX_ID_CLUSTERS             0x1c
#define AGX_CORE_MASKS              0x1500
#define AGX_MAX_CORE_MASKS          3
#define AGX_REGION_CONTEXTS         0
#define AGX_REGION_KERNEL_ROOT      1
#define AGX_REGION_HANDOFF          2
#define AGX_REGION_HOST_LEVEL       3
#define AGX_REGION_FIRMWARE_TEXT    4
#define AGX_REGIONS                 6
#define AGX_BOOT_TIMEOUT_MS         2000
#define AGX_LISTEN_MS               500
#define AGX_MAX_SHARED              8
#define AGX_SHARED_WINDOW_OFFSET    0x100000000ULL
#define AGX_SHARED_WINDOW_SIZE      0x100000000ULL

typedef struct _AGX_SHARED
{
    PVOID Allocation;
    SIZE_T AllocationSize;
    PVOID Virtual;
    ULONG64 Physical;
    ULONG64 Address;
    ULONG Size;
} AGX_SHARED, *PAGX_SHARED;

typedef struct _APPLE_AGX
{
    PFILE_OBJECT PmpFile;
    PDEVICE_OBJECT Pmp;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Lower;
    FDT_DRIVER_TREE Tree;
    BOOLEAN TreeOpen;
    PUCHAR Asc;
    ULONG AscSize;
    PUCHAR Id;
    ULONG IdSize;
    PUCHAR Core;
    ULONG CoreSize;
    ULONG64 RegionBase[AGX_REGIONS];
    ULONG64 RegionSize[AGX_REGIONS];
    APPLE_UAT_ADOPTED Adopted;
    BOOLEAN Owned;
    PUCHAR Mailbox;
    ULONG MailboxSize;
    APPLE_RTKIT Rtkit;
    AGX_SHARED Shared[AGX_MAX_SHARED];
    ULONG SharedCount;
    ULONG64 SharedNext;
} APPLE_AGX, *PAPPLE_AGX;

static const CHAR *const AgxRegionNames[AGX_REGIONS] = {
    "ttbs", "pagetables", "handoff", "pagetables-l2", "firmware-text", "firmware-data"
};

static
PCSTR
AgxGeneration(
    _In_ ULONG Version)
{
    switch ((Version >> 24) & 0xff)
    {
        case 4: return "G13";
        case 5: return "G14";
        case 6: return "G14X";
        case 7: return "G15";
        default: return "unknown";
    }
}

static
PCSTR
AgxVariant(
    _In_ ULONG Version)
{
    switch ((Version >> 16) & 0xff)
    {
        case 1: return "P";
        case 2: return "G";
        case 3: return "S";
        case 4: return "C";
        default: return "unknown";
    }
}

/* The graphics block answers only after the power processor has voted for it, so a read without the
 * vote can fault the machine. */
static
NTSTATUS
AgxPowerVote(
    _Inout_ PAPPLE_AGX Agx,
    _In_ BOOLEAN Enable)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(APPLE_PMP_DEVICE_NAME);
    APPLE_PMP_POWER_REQUEST Request;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    if (!Agx->Pmp)
    {
        Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &Agx->PmpFile, &Agx->Pmp);
        if (!NT_SUCCESS(Status))
        {
            Agx->Pmp = NULL;
            Agx->PmpFile = NULL;
            return Status;
        }
    }
    Request.Device = APPLE_PMP_DEVICE_GRAPHICS;
    Request.Enable = Enable;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_APPLE_PMP_SET_POWER, Agx->Pmp, &Request, sizeof(Request), NULL, 0,
                                        FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(Agx->Pmp, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

/* The firmware's own regions: the boot loader reserves them and their addresses never move. */
static
VOID
AgxReadRegions(
    _Inout_ PAPPLE_AGX Agx)
{
    const VOID *Regions;
    ULONG Length, Index, Node;
    ULONG64 Phandle, Base, Size;

    Regions = RiscvFdtGetProperty(&Agx->Tree.Fdt, Agx->Tree.Node, "memory-region", &Length);
    if (!Regions)
        return;
    for (Index = 0; Index < AGX_REGIONS; ++Index)
    {
        if (!RiscvFdtReadCells(Regions, Length, Index, 1, &Phandle))
            break;
        Node = FdtDriverFindPhandle(&Agx->Tree, (ULONG)Phandle);
        if (Node == RISCV_FDT_NO_NODE || !RiscvFdtReadReg(&Agx->Tree.Fdt, Node, Agx->Tree.Node, 0, &Base, &Size))
            continue;
        Agx->RegionBase[Index] = Base;
        Agx->RegionSize[Index] = Size;
        DbgPrint("appleagx: firmware region %s at %I64x+%I64x\n", AgxRegionNames[Index], Base, Size);
    }
}

/* The firmware may only be started cold, which its address table page shows by being all zero. */
static
BOOLEAN
AgxTableIsClear(
    _In_ PAPPLE_AGX Agx)
{
    PHYSICAL_ADDRESS Address;
    PULONG64 Table;
    ULONG64 Used = 0;
    ULONG Index, Count;

    if (!Agx->RegionBase[AGX_REGION_CONTEXTS] || Agx->RegionSize[AGX_REGION_CONTEXTS] < sizeof(ULONG64))
        return FALSE;
    Address.QuadPart = (LONGLONG)Agx->RegionBase[AGX_REGION_CONTEXTS];
    Count = (ULONG)min(Agx->RegionSize[AGX_REGION_CONTEXTS], APPLE_UAT_PAGE_SIZE) / sizeof(ULONG64);
    Table = MmMapIoSpace(Address, (SIZE_T)Count * sizeof(ULONG64), MmNonCached);
    if (!Table)
        return FALSE;
    for (Index = 0; Index < Count; ++Index)
        Used |= Table[Index];
    MmUnmapIoSpace(Table, (SIZE_T)Count * sizeof(ULONG64));
    DbgPrint("appleagx: the firmware address table is %s, so the processor is %s\n", Used ? "in use" : "empty",
             Used ? "already owned" : "cold and startable");
    return Used == 0;
}

/* The boot loader leaves the firmware image in memory; its first words say whether it survived. */
static
VOID
AgxReportFirmware(
    _In_ PAPPLE_AGX Agx)
{
    PHYSICAL_ADDRESS Address;
    PULONG Image;

    if (!Agx->RegionBase[AGX_REGION_FIRMWARE_TEXT] || Agx->RegionSize[AGX_REGION_FIRMWARE_TEXT] < 2 * sizeof(ULONG))
        return;
    Address.QuadPart = (LONGLONG)Agx->RegionBase[AGX_REGION_FIRMWARE_TEXT];
    Image = MmMapIoSpace(Address, 2 * sizeof(ULONG), MmNonCached);
    if (!Image)
        return;
    DbgPrint("appleagx: the firmware image of %I64x bytes starts with %08lx %08lx\n",
             Agx->RegionSize[AGX_REGION_FIRMWARE_TEXT], Image[0], Image[1]);
    MmUnmapIoSpace(Image, 2 * sizeof(ULONG));
}

/* The coprocessor reaches memory through the graphics tables, inside the window left to the host. */
static
BOOLEAN
NTAPI
AgxShmemSetup(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG Size,
    _Inout_ PULONG64 Address,
    _Out_ PVOID *Virtual,
    _Out_ PBOOLEAN Preassigned)
{
    const APPLE_UAT_PROTECTION Shared = { APPLE_UAT_ACCESS_FIRMWARE, APPLE_UAT_MEMORY_UNCACHED, FALSE, TRUE };
    PHYSICAL_ADDRESS Low, High, Boundary;
    PAPPLE_AGX Agx = Context;
    PAGX_SHARED Buffer;
    ULONG64 Physical;
    SIZE_T Rounded, Skip;
    NTSTATUS Status;

    *Virtual = NULL;
    *Preassigned = FALSE;
    if (!Agx->Owned || Agx->SharedCount == AGX_MAX_SHARED || !Size)
        return FALSE;
    if (*Address)
    {
        DbgPrint("appleagx: endpoint %u asked to keep its %lu byte buffer at %I64x\n", Endpoint, Size, *Address);
        return FALSE;
    }
    Rounded = ((SIZE_T)Size + APPLE_UAT_PAGE_SIZE - 1) & ~(SIZE_T)(APPLE_UAT_PAGE_SIZE - 1);
    if (Rounded > AGX_SHARED_WINDOW_SIZE - (Agx->SharedNext - AGX_SHARED_WINDOW_OFFSET))
        return FALSE;
    Buffer = &Agx->Shared[Agx->SharedCount];
    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = 0;
    Buffer->AllocationSize = Rounded + APPLE_UAT_PAGE_SIZE - PAGE_SIZE;
    Buffer->Allocation = MmAllocateContiguousMemorySpecifyCache(Buffer->AllocationSize, Low, High, Boundary,
                                                                MmNonCached);
    if (!Buffer->Allocation)
        return FALSE;
    Physical = (ULONG64)MmGetPhysicalAddress(Buffer->Allocation).QuadPart;
    Skip = (SIZE_T)(((Physical + APPLE_UAT_PAGE_SIZE - 1) & ~(ULONG64)(APPLE_UAT_PAGE_SIZE - 1)) - Physical);
    Buffer->Virtual = (PUCHAR)Buffer->Allocation + Skip;
    Buffer->Physical = Physical + Skip;
    Buffer->Size = (ULONG)Rounded;
    Buffer->Address = APPLE_UAT_HOST_BASE + Agx->SharedNext;
    RtlZeroMemory(Buffer->Virtual, Rounded);
    Status = AppleUatAdoptedMap(&Agx->Adopted, Buffer->Address, Buffer->Physical, Rounded, &Shared);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the buffer for endpoint %u was not mapped at %I64x: %lx\n", Endpoint, Buffer->Address,
                 Status);
        MmFreeContiguousMemorySpecifyCache(Buffer->Allocation, Buffer->AllocationSize, MmNonCached);
        RtlZeroMemory(Buffer, sizeof(*Buffer));
        return FALSE;
    }
    Agx->SharedNext += Rounded;
    Agx->SharedCount++;
    *Address = Buffer->Address;
    *Virtual = Buffer->Virtual;
    DbgPrint("appleagx: endpoint %u gets %lu bytes at %I64x (physical %I64x)\n", Endpoint, Buffer->Size,
             Buffer->Address, Buffer->Physical);
    return TRUE;
}

static
VOID
NTAPI
AgxReceive(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG64 Message)
{
    UNREFERENCED_PARAMETER(Context);
    DbgPrint("appleagx: endpoint %#x sent %I64x\n", Endpoint, Message);
}

static
VOID
AgxFreeShared(
    _Inout_ PAPPLE_AGX Agx)
{
    ULONG Index;

    for (Index = 0; Index < Agx->SharedCount; ++Index)
    {
        if (Agx->Shared[Index].Allocation)
            MmFreeContiguousMemorySpecifyCache(Agx->Shared[Index].Allocation, Agx->Shared[Index].AllocationSize,
                                               MmNonCached);
    }
    RtlZeroMemory(Agx->Shared, sizeof(Agx->Shared));
    Agx->SharedCount = 0;
    Agx->SharedNext = AGX_SHARED_WINDOW_OFFSET;
}

/* The firmware is started cold and says over the mailbox what it needs; the log keeps its answer. */
static
NTSTATUS
AgxBootFirmware(
    _Inout_ PAPPLE_AGX Agx,
    _In_ ULONG Control)
{
    NTSTATUS Status;

    if (!Agx->Mailbox)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Agx->Rtkit.Name = "appleagx";
    Agx->Rtkit.Mailbox = Agx->Mailbox;
    Agx->Rtkit.Context = Agx;
    Agx->Rtkit.ShmemSetup = AgxShmemSetup;
    Agx->Rtkit.Receive = AgxReceive;
    WRITE_REGISTER_ULONG((volatile ULONG *)(Agx->Asc + AGX_ASC_CONTROL), Control | AGX_ASC_CONTROL_RUN);
    Status = AppleRtkitBoot(&Agx->Rtkit, AGX_BOOT_TIMEOUT_MS);
    DbgPrint("appleagx: firmware boot %lx, RTKit %lu, endpoints %08lx %08lx, control %08lx\n", Status,
             Agx->Rtkit.Version, Agx->Rtkit.Endpoints[0], Agx->Rtkit.Endpoints[1],
             READ_REGISTER_ULONG((volatile ULONG *)(Agx->Asc + AGX_ASC_CONTROL)));
    if (NT_SUCCESS(Status) && AppleRtkitHasEndpoint(&Agx->Rtkit, RTKIT_EP_APP_START))
    {
        LARGE_INTEGER Interval;
        ULONG Waited;

        AppleRtkitStartEndpoint(&Agx->Rtkit, RTKIT_EP_APP_START);
        Interval.QuadPart = -10000LL;
        for (Waited = 0; Waited < AGX_LISTEN_MS; ++Waited)
        {
            AppleRtkitPoll(&Agx->Rtkit);
            KeDelayExecutionThread(KernelMode, FALSE, &Interval);
        }
    }
    return Status;
}

/* The firmware walks its own tables, so they are built before it is allowed to run. */
static
NTSTATUS
AgxAdoptAddressSpace(
    _Inout_ PAPPLE_AGX Agx)
{
    NTSTATUS Status;

    Status = AppleUatAdopt(&Agx->Adopted, Agx->RegionBase[AGX_REGION_HANDOFF],
                           Agx->RegionBase[AGX_REGION_CONTEXTS], Agx->RegionBase[AGX_REGION_KERNEL_ROOT],
                           Agx->RegionBase[AGX_REGION_HOST_LEVEL], "appleagx");
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the firmware address space was not adopted: %lx\n", Status);
        return Status;
    }
    Status = AppleUatAdoptedInstall(&Agx->Adopted);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the address tables were not installed: %lx\n", Status);
        AppleUatAdoptedRelease(&Agx->Adopted);
        return Status;
    }
    Agx->Owned = TRUE;
    DbgPrint("appleagx: address tables installed, context 0 lower %I64x upper %I64x, host window at %I64x\n",
             Agx->Adopted.Lower.TopPhysical, Agx->Adopted.KernelRootPhysical, APPLE_UAT_HOST_BASE);
    return STATUS_SUCCESS;
}

static
VOID
AgxStop(
    _Inout_ PAPPLE_AGX Agx)
{
    AgxFreeShared(Agx);
    if (Agx->Owned)
        AppleUatAdoptedRelease(&Agx->Adopted);
    Agx->Owned = FALSE;
    if (Agx->Mailbox)
        MmUnmapIoSpace(Agx->Mailbox, Agx->MailboxSize);
    Agx->Mailbox = NULL;
    if (Agx->Asc)
        AgxPowerVote(Agx, FALSE);
    if (Agx->PmpFile)
        ObDereferenceObject(Agx->PmpFile);
    Agx->PmpFile = NULL;
    Agx->Pmp = NULL;
    if (Agx->Core)
        MmUnmapIoSpace(Agx->Core, Agx->CoreSize);
    Agx->Core = NULL;
    if (Agx->Id)
        MmUnmapIoSpace(Agx->Id, Agx->IdSize);
    Agx->Id = NULL;
    if (Agx->Asc)
        MmUnmapIoSpace(Agx->Asc, Agx->AscSize);
    Agx->Asc = NULL;
    if (Agx->TreeOpen)
        FdtDriverClose(&Agx->Tree);
    Agx->TreeOpen = FALSE;
}

static
PUCHAR
AgxMapWindow(
    _In_ ULONG64 Base,
    _In_ ULONG Offset,
    _In_ ULONG Size)
{
    PHYSICAL_ADDRESS Address;

    Address.QuadPart = (LONGLONG)(Base + Offset);
    return MmMapIoSpace(Address, Size, MmNonCached);
}

static
NTSTATUS
AgxStart(
    _Inout_ PAPPLE_AGX Agx)
{
    ULONG64 AscBase, AscLength, SgxBase, SgxLength;
    ULONG Version, Counts1, Clusters, Control, Index, Mask, MailboxPhandle, MailboxNode;
    ULONG Masks[AGX_MAX_CORE_MASKS] = { 0 };
    ULONG CoresPerCluster, ClusterCount, Active = 0;
    NTSTATUS Status;

    Status = FdtDriverOpen(Agx->Pdo, &Agx->Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    Agx->TreeOpen = TRUE;
    if (!FdtDriverReadReg(&Agx->Tree, Agx->Tree.Node, 0, &AscBase, &AscLength) ||
        !FdtDriverReadReg(&Agx->Tree, Agx->Tree.Node, 1, &SgxBase, &SgxLength) ||
        AscLength < AGX_ASC_CONTROL + sizeof(ULONG) ||
        SgxLength < AGX_SGX_CORE_WINDOW + AGX_SGX_WINDOW_SIZE)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Status = ApplePmgrPowerOnDevice(&Agx->Tree, Agx->Tree.Node);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the graphics power domain did not turn on: %lx\n", Status);
        return Status;
    }
    Status = AgxPowerVote(Agx, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the power processor did not vote for the graphics block: %lx\n", Status);
        return Status;
    }

    Agx->AscSize = AGX_SGX_WINDOW_SIZE;
    Agx->IdSize = AGX_SGX_WINDOW_SIZE;
    Agx->CoreSize = AGX_SGX_WINDOW_SIZE;
    Agx->SharedNext = AGX_SHARED_WINDOW_OFFSET;
    if (RiscvFdtReadU32(&Agx->Tree.Fdt, Agx->Tree.Node, "mboxes", &MailboxPhandle) &&
        (MailboxNode = FdtDriverFindPhandle(&Agx->Tree, MailboxPhandle)) != RISCV_FDT_NO_NODE)
    {
        Agx->Mailbox = FdtDriverMapReg(&Agx->Tree, MailboxNode, 0, NULL, &Agx->MailboxSize);
    }
    Agx->Asc = AgxMapWindow(AscBase, 0, Agx->AscSize);
    Agx->Id = AgxMapWindow(SgxBase, AGX_SGX_ID_WINDOW, Agx->IdSize);
    Agx->Core = AgxMapWindow(SgxBase, AGX_SGX_CORE_WINDOW, Agx->CoreSize);
    if (!Agx->Asc || !Agx->Id || !Agx->Core)
        return STATUS_INSUFFICIENT_RESOURCES;

    Control = READ_REGISTER_ULONG((volatile ULONG *)(Agx->Asc + AGX_ASC_CONTROL));
    Version = READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_VERSION));
    Counts1 = READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_COUNTS1));
    Clusters = READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_CLUSTERS));
    DbgPrint("appleagx: identity %08lx %08lx %08lx %08lx %08lx %08lx\n", Version,
             READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_UNKNOWN08)), Counts1,
             READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_COUNTS2)),
             READ_REGISTER_ULONG((volatile ULONG *)(Agx->Id + AGX_ID_UNKNOWN18)), Clusters);

    CoresPerCluster = Counts1 & 0xff;
    ClusterCount = ((Counts1 >> 8) & 0xff) * ((Counts1 >> 16) & 0xf);
    for (Index = 0; Index < AGX_MAX_CORE_MASKS; ++Index)
    {
        Masks[Index] = READ_REGISTER_ULONG((volatile ULONG *)(Agx->Core + AGX_CORE_MASKS + 4 * Index));
        for (Mask = Masks[Index]; Mask; Mask &= Mask - 1)
            ++Active;
    }
    DbgPrint("appleagx: %s%s revision %lu.%lu, %lu clusters of %lu cores, %lu active (masks %08lx %08lx %08lx)\n",
             AgxGeneration(Version), AgxVariant(Version), (Version >> 8) & 0xff, Version & 0xff, ClusterCount,
             CoresPerCluster, Active, Masks[0], Masks[1], Masks[2]);
    DbgPrint("appleagx: coprocessor control %08lx (%s), firmware not started\n", Control,
             (Control & AGX_ASC_CONTROL_RUN) ? "running" : "halted");
    AgxReadRegions(Agx);
    AgxReportFirmware(Agx);
    if ((Control & AGX_ASC_CONTROL_RUN) || !AgxTableIsClear(Agx))
        return STATUS_SUCCESS;
    if (NT_SUCCESS(AgxAdoptAddressSpace(Agx)))
        AgxBootFirmware(Agx, Control);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AgxPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_AGX Agx = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Agx->Lower, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
            {
                Status = AgxStart(Agx);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("appleagx: the graphics processor was not identified: %lx\n", Status);
                    AgxStop(Agx);
                }
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_STOP_DEVICE:
            AgxStop(Agx);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            AgxStop(Agx);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Agx->Lower, Irp);
            IoDetachDevice(Agx->Lower);
            IoDeleteDevice(DeviceObject);
            return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Agx->Lower, Irp);
}

static
NTSTATUS
NTAPI
AgxPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_AGX Agx = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Agx->Lower, Irp);
}

static
NTSTATUS
NTAPI
AgxPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_AGX Agx = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Agx->Lower, Irp);
}

static
NTSTATUS
NTAPI
AgxAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT Pdo)
{
    PAPPLE_AGX Agx;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(APPLE_AGX), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Agx = Fdo->DeviceExtension;
    RtlZeroMemory(Agx, sizeof(*Agx));
    Agx->Self = Fdo;
    Agx->Pdo = Pdo;
    Agx->Lower = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (!Agx->Lower)
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
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = AgxPassThrough;
    DriverObject->MajorFunction[IRP_MJ_PNP] = AgxPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AgxPower;
    DriverObject->DriverExtension->AddDevice = AgxAddDevice;
    return STATUS_SUCCESS;
}
