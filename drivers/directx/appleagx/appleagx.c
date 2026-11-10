/*
 * PROJECT:     LiberNT Apple Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Identification of the Apple silicon graphics processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/appleadt.h>
#include <reactos/drivers/applepmp.h>
#include <reactos/drivers/applertkit.h>
#include <reactos/drivers/appleuat.h>
#include <reactos/drivers/appleagxinit.h>
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
#define AGX_REGION_FIRMWARE_DATA    5
#define AGX_REGIONS                 6
#define AGX_BOOT_TIMEOUT_MS         2000
#define AGX_LISTEN_MS               500
#define AGX_READY_TIMEOUT_MS        3000
#define AGX_MAX_SHARED              8
#define AGX_SHARED_ALIGN            0x1000
#define AGX_ADT_MAX_LENGTH          0x200000
#define AGX_PART                    "gpu,t6030"

typedef struct _AGX_SHARED
{
    PVOID Virtual;
    ULONG64 Physical;
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
    APPLE_AGX_INIT Init;
    BOOLEAN Initialised;
    APPLE_AGX_RING Event;
    APPLE_AGX_RING Trace;
    APPLE_AGX_RING Stats;
    APPLE_AGX_RING DevCtl;
    BOOLEAN Bound;
    ULONG Drained;
    PVOID AdtCopy;
    ULONG AdtLength;
    APPLE_ADT Adt;
    PUCHAR Mailbox;
    ULONG MailboxSize;
    APPLE_RTKIT Rtkit;
    AGX_SHARED Shared[AGX_MAX_SHARED];
    ULONG SharedCount;
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

/* The boot loader exports the machine's own tree as a reserved region labelled "adt". */
static
VOID
AgxOpenAdt(
    _Inout_ PAPPLE_AGX Agx)
{
    ULONG Reserved, Node;
    ULONG64 Base, Size;
    PVOID Mapped;
    PHYSICAL_ADDRESS Address;
    const VOID *Label, *Compatible;
    ULONG Length;

    if (!FdtFindNode(&Agx->Tree.Fdt, "/reserved-memory", &Reserved, NULL))
        return;
    for (Node = FdtFirstChild(&Agx->Tree.Fdt, Reserved); Node != FDT_NO_NODE;
         Node = FdtNextSibling(&Agx->Tree.Fdt, Node))
    {
        Compatible = FdtGetProperty(&Agx->Tree.Fdt, Node, "compatible", &Length);
        if (!Compatible || !FdtStringListContains(Compatible, Length, "phram"))
            continue;
        Label = FdtGetProperty(&Agx->Tree.Fdt, Node, "label", &Length);
        if (!Label || Length != sizeof("adt") || !RtlEqualMemory(Label, "adt", sizeof("adt")))
            continue;
        if (!FdtReadReg(&Agx->Tree.Fdt, Node, Reserved, 0, &Base, &Size) || !Size ||
            Size > AGX_ADT_MAX_LENGTH)
        {
            continue;
        }
        Address.QuadPart = (LONGLONG)Base;
        Mapped = MmMapIoSpace(Address, (SIZE_T)Size, MmCached);
        if (!Mapped)
            return;
        Agx->AdtCopy = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)Size, AGX_TAG);
        if (Agx->AdtCopy)
        {
            RtlCopyMemory(Agx->AdtCopy, Mapped, (SIZE_T)Size);
            Agx->AdtLength = (ULONG)Size;
        }
        MmUnmapIoSpace(Mapped, (SIZE_T)Size);
        if (!Agx->AdtCopy)
            return;
        if (!AppleAdtOpen(&Agx->Adt, Agx->AdtCopy, Agx->AdtLength))
        {
            DbgPrint("appleagx: the exported tree at %I64x+%I64x could not be read\n", Base, Size);
            ExFreePoolWithTag(Agx->AdtCopy, AGX_TAG);
            Agx->AdtCopy = NULL;
            Agx->AdtLength = 0;
            return;
        }
        DbgPrint("appleagx: the machine's own tree is at %I64x+%I64x\n", Base, Size);
        return;
    }
}

/* The regions move with the firmware the boot chain loaded, so the machine's tree is asked first. */
static
BOOLEAN
AgxReadAdt(
    _Inout_ PAPPLE_AGX Agx)
{
    APPLE_AGX_DESCRIPTION Description;
    NTSTATUS Status;
    ULONG Index;

    if (!Agx->AdtCopy)
        return FALSE;
    Status = AppleAgxDescribe(&Agx->Adt, AGX_PART, APPLE_UAT_PAGE_SIZE, &Description);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the machine's tree does not describe the graphics processor: %lx\n", Status);
        return FALSE;
    }
    for (Index = 0; Index < APPLE_AGX_REGIONS; ++Index)
    {
        Agx->RegionBase[Index] = Description.RegionBase[Index];
        Agx->RegionSize[Index] = Description.RegionSize[Index];
        DbgPrint("appleagx: region %s at %I64x+%I64x\n", AgxRegionNames[Index], Agx->RegionBase[Index],
                 Agx->RegionSize[Index]);
    }
    if (Description.SegmentsKnown)
    {
        Agx->RegionBase[AGX_REGION_FIRMWARE_TEXT] = Description.Text.Physical;
        Agx->RegionSize[AGX_REGION_FIRMWARE_TEXT] = Description.Text.Size;
        Agx->RegionBase[AGX_REGION_FIRMWARE_DATA] = Description.Data.Physical;
        Agx->RegionSize[AGX_REGION_FIRMWARE_DATA] = Description.Data.Size;
        DbgPrint("appleagx: firmware text at %I64x+%lx and data at %I64x+%lx, mapped at %I64x, %s\n",
                 Description.Text.Physical, Description.Text.Size, Description.Data.Physical,
                 Description.Data.Size, Description.Text.Virtual,
                 Description.SegmentsPlaced ? "where the processor expects them" : "NOT where the processor expects them");
    }
    for (Index = 0; Index < Description.States; ++Index)
    {
        DbgPrint("appleagx: performance state %lu of %lu: %lu kHz at %lu mV, sram %lu mV\n", Index,
                 Description.States, Description.State[Index].Frequency / 1000,
                 Description.State[Index].Millivolts, Description.SramState[Index].Millivolts);
    }
    return TRUE;
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

    Regions = FdtGetProperty(&Agx->Tree.Fdt, Agx->Tree.Node, "memory-region", &Length);
    if (!Regions)
        return;
    for (Index = 0; Index < AGX_REGIONS; ++Index)
    {
        if (!FdtReadCells(Regions, Length, Index, 1, &Phandle))
            break;
        Node = FdtDriverFindPhandle(&Agx->Tree, (ULONG)Phandle);
        if (Node == FDT_NO_NODE || !FdtReadReg(&Agx->Tree.Fdt, Node, Agx->Tree.Node, 0, &Base, &Size))
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

/* The firmware asks the host to map a buffer it has already placed inside its own data segment,
 * never to choose one: an address outside that segment is refused. */
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
    PHYSICAL_ADDRESS Physical;
    PAPPLE_AGX Agx = Context;
    PAGX_SHARED Buffer;
    ULONG64 Base = Agx->RegionBase[AGX_REGION_FIRMWARE_DATA];
    ULONG64 Length = Agx->RegionSize[AGX_REGION_FIRMWARE_DATA];

    *Virtual = NULL;
    *Preassigned = TRUE;
    if (Agx->SharedCount == AGX_MAX_SHARED || !Size || !Base || !Length)
        return FALSE;
    if (!*Address || ((*Address | Size) & (AGX_SHARED_ALIGN - 1)) || *Address < Base ||
        Size > Length || *Address - Base > Length - Size)
    {
        DbgPrint("appleagx: endpoint %u asked for %lu bytes at %I64x, outside its data segment %I64x+%I64x\n",
                 Endpoint, Size, *Address, Base, Length);
        return FALSE;
    }
    Physical.QuadPart = (LONGLONG)*Address;
    Buffer = &Agx->Shared[Agx->SharedCount];
    Buffer->Virtual = MmMapIoSpace(Physical, Size, MmWriteCombined);
    if (!Buffer->Virtual)
        return FALSE;
    Buffer->Physical = *Address;
    Buffer->Size = Size;
    Agx->SharedCount++;
    *Virtual = Buffer->Virtual;
    DbgPrint("appleagx: endpoint %u keeps its %lu byte buffer at %I64x\n", Endpoint, Size, *Address);
    return TRUE;
}

/* The firmware produces on the receive rings and asks over the mailbox for them to be drained. */
static
VOID
AgxDrainRings(
    _Inout_ PAPPLE_AGX Agx)
{
    UCHAR Record[APPLE_AGX_EVENT_RECORD];
    ULONG Taken = 0;

    if (!Agx->Bound)
        return;
    while (AppleAgxRingTake(&Agx->Event, Record) && Taken < APPLE_AGX_RING_SLOTS)
    {
        ULONG64 Head;

        RtlCopyMemory(&Head, Record, sizeof(Head));
        DbgPrint("appleagx: event %I64x\n", Head);
        ++Taken;
    }
    while (AppleAgxRingTake(&Agx->Stats, Record) && Taken < 2 * APPLE_AGX_RING_SLOTS)
    {
        ULONG64 Head;

        RtlCopyMemory(&Head, Record, sizeof(Head));
        DbgPrint("appleagx: statistic %I64x\n", Head);
        ++Taken;
    }
    AppleAgxRingConsume(&Agx->Trace);
    Agx->Drained += Taken;
}

static
VOID
NTAPI
AgxReceive(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG64 Message)
{
    PAPPLE_AGX Agx = Context;

    if (Endpoint == APPLE_AGX_EP_FIRMWARE && (Message & ~0xffffffffffffULL) == APPLE_AGX_MESSAGE_RING_SERVICE)
    {
        AgxDrainRings(Agx);
        return;
    }
    DbgPrint("appleagx: endpoint %#x sent %I64x\n", Endpoint, Message);
}

/* A request is posted, the firmware is rung, and the record is waited for until it has been both
 * read and completed. A halted firmware is not posted to at all. */
static
NTSTATUS
AgxSubmit(
    _Inout_ PAPPLE_AGX Agx,
    _Inout_ PAPPLE_AGX_RING Ring,
    _In_ ULONG Channel,
    _In_ const VOID *Record,
    _In_ ULONG Timeout)
{
    LARGE_INTEGER Interval;
    ULONG Next, Waited;
    NTSTATUS Status;

    if (!Agx->Bound)
        return STATUS_INVALID_DEVICE_STATE;
    if (Agx->Init.Virtual[APPLE_AGX_OBJECT_CONTROL] &&
        *(volatile ULONG *)((PUCHAR)Agx->Init.Virtual[APPLE_AGX_OBJECT_CONTROL] + APPLE_AGX_CONTROL_HALTED))
    {
        return STATUS_DEVICE_POWERED_OFF;
    }
    Status = AppleAgxRingPost(Ring, Record, FALSE, &Next);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!AppleRtkitSend(&Agx->Rtkit, APPLE_AGX_EP_DOORBELL, AppleAgxRingDoorbell(Channel)))
        return STATUS_IO_TIMEOUT;
    Interval.QuadPart = -10000LL;
    for (Waited = 0; Waited < Timeout; ++Waited)
    {
        AppleRtkitPoll(&Agx->Rtkit);
        if (AppleAgxRingDone(Ring, Next))
            return STATUS_SUCCESS;
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }
    return STATUS_IO_TIMEOUT;
}

/* The rings live inside the initial data, so they are bound once it is in place. */
static
VOID
AgxBindRings(
    _Inout_ PAPPLE_AGX Agx)
{
    AppleAgxRingBind(&Agx->Event, Agx->Init.Virtual[APPLE_AGX_OBJECT_EVENT_STATE],
                     Agx->Init.Virtual[APPLE_AGX_OBJECT_EVENT_RING], APPLE_AGX_EVENT_RECORD);
    AppleAgxRingBind(&Agx->Trace, Agx->Init.Virtual[APPLE_AGX_OBJECT_TRACE_STATE],
                     Agx->Init.Virtual[APPLE_AGX_OBJECT_TRACE_RING], APPLE_AGX_EVENT_RECORD);
    AppleAgxRingBind(&Agx->Stats, Agx->Init.Virtual[APPLE_AGX_OBJECT_STATS_STATE],
                     Agx->Init.Virtual[APPLE_AGX_OBJECT_STATS_RING], APPLE_AGX_EVENT_RECORD);
    AppleAgxRingBind(&Agx->DevCtl, Agx->Init.Virtual[APPLE_AGX_OBJECT_DEVCTL_STATE],
                     Agx->Init.Virtual[APPLE_AGX_OBJECT_DEVCTL_RING], APPLE_AGX_DEVCTL_RECORD);
    Agx->Bound = TRUE;
}

static
VOID
AgxFreeShared(
    _Inout_ PAPPLE_AGX Agx)
{
    ULONG Index;

    for (Index = 0; Index < Agx->SharedCount; ++Index)
    {
        if (Agx->Shared[Index].Virtual)
            MmUnmapIoSpace(Agx->Shared[Index].Virtual, Agx->Shared[Index].Size);
    }
    RtlZeroMemory(Agx->Shared, sizeof(Agx->Shared));
    Agx->SharedCount = 0;
}

static
VOID
AgxListen(
    _Inout_ PAPPLE_AGX Agx,
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;
    ULONG Waited;

    Interval.QuadPart = -10000LL;
    for (Waited = 0; Waited < Milliseconds; ++Waited)
    {
        AppleRtkitPoll(&Agx->Rtkit);
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }
}

/* The firmware is given the addresses of everything it must reach, then told where to find them.
 * It answers by writing its readiness word, which is what the next line of the log reports. */
static
NTSTATUS
AgxHandInitialData(
    _Inout_ PAPPLE_AGX Agx)
{
    ULONG Waited;
    NTSTATUS Status;

    Status = AppleAgxInitCreate(&Agx->Init, &Agx->Adopted, "appleagx");
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the initial data needed %lu pages that were not available: %lx\n",
                 Agx->Init.Pages, Status);
        return Status;
    }
    Agx->Initialised = TRUE;
    Status = AppleAgxInitWrite(&Agx->Init);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the initial data was not written: %lx\n", Status);
        return Status;
    }
    Status = AppleAgxMapRegisters(&Agx->Init);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the firmware's register windows were not mapped: %lx\n", Status);
        return Status;
    }
    AgxBindRings(Agx);
    DbgPrint("appleagx: initial data of %lu pages written, root at %I64x\n", Agx->Init.Allocations,
             AppleAgxInitMessage() & APPLE_AGX_MESSAGE_ADDRESS_MASK);
    if (!AppleRtkitSend(&Agx->Rtkit, APPLE_AGX_EP_FIRMWARE, AppleAgxInitMessage()))
    {
        DbgPrint("appleagx: the firmware was not told where its initial data is\n");
        return STATUS_IO_TIMEOUT;
    }
    for (Waited = 0; Waited < AGX_READY_TIMEOUT_MS; Waited += 10)
    {
        AgxListen(Agx, 10);
        if (AppleAgxInitReady(&Agx->Init))
        {
            UCHAR Request[APPLE_AGX_DEVCTL_RECORD] = { 0 };

            AgxDrainRings(Agx);
            DbgPrint("appleagx: the firmware is ready after %lu ms, %lu records read\n", Waited, Agx->Drained);
            DbgPrint("appleagx: the device control ring answers an empty request: %lx\n",
                     AgxSubmit(Agx, &Agx->DevCtl, APPLE_AGX_DOORBELL_DEVCTL, Request, AGX_LISTEN_MS));
            return STATUS_SUCCESS;
        }
    }
    AgxDrainRings(Agx);
    DbgPrint("appleagx: the firmware did not become ready in %lu ms, %lu records read, halted %lu\n",
             AGX_READY_TIMEOUT_MS, Agx->Drained,
             Agx->Init.Virtual[APPLE_AGX_OBJECT_CONTROL]
                 ? *(volatile ULONG *)((PUCHAR)Agx->Init.Virtual[APPLE_AGX_OBJECT_CONTROL] +
                                       APPLE_AGX_CONTROL_HALTED)
                 : 0);
    return STATUS_DEVICE_NOT_READY;
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
    if (!NT_SUCCESS(Status) || !AppleRtkitHasEndpoint(&Agx->Rtkit, APPLE_AGX_EP_FIRMWARE))
        return NT_SUCCESS(Status) ? STATUS_DEVICE_PROTOCOL_ERROR : Status;
    AppleRtkitStartEndpoint(&Agx->Rtkit, APPLE_AGX_EP_FIRMWARE);
    if (AppleRtkitHasEndpoint(&Agx->Rtkit, APPLE_AGX_EP_DOORBELL))
        AppleRtkitStartEndpoint(&Agx->Rtkit, APPLE_AGX_EP_DOORBELL);
    AgxListen(Agx, AGX_LISTEN_MS);
    return AgxHandInitialData(Agx);
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
    Agx->Bound = FALSE;
    if (Agx->Initialised)
        AppleAgxInitRelease(&Agx->Init);
    Agx->Initialised = FALSE;
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
    if (Agx->AdtCopy)
        ExFreePoolWithTag(Agx->AdtCopy, AGX_TAG);
    Agx->AdtCopy = NULL;
    Agx->AdtLength = 0;
    RtlZeroMemory(&Agx->Adt, sizeof(Agx->Adt));
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
    if (FdtReadU32(&Agx->Tree.Fdt, Agx->Tree.Node, "mboxes", &MailboxPhandle) &&
        (MailboxNode = FdtDriverFindPhandle(&Agx->Tree, MailboxPhandle)) != FDT_NO_NODE)
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
    AgxOpenAdt(Agx);
    if (!AgxReadAdt(Agx))
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
