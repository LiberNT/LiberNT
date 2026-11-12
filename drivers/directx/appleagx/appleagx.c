/*
 * PROJECT:     LiberNT Apple Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Identification of the Apple silicon graphics processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appleagxp.h"
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/applepmp.h>
#include <pseh/pseh2.h>

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
#define AGX_BOOT_TIMEOUT_MS         2000
#define AGX_LISTEN_MS               500
#define AGX_READY_TIMEOUT_MS        3000
#define AGX_DEVCTL_TIMEOUT_MS       2000
#define AGX_SHARED_ALIGN            0x1000
#define AGX_ADT_MAX_LENGTH          0x200000
#define AGX_PART                    "gpu,t6030"
#define AGX_PMP_WAIT_MS             10000
#define AGX_PMP_POLL_MS             100

#define AGX_CLIENT_TAG              'cGxA'
#define AGX_FIRST_BINDINGS          64
#define AGX_MAX_BINDINGS            65536
#define AGX_MAX_BIND_OPS            4096


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
    Agx->Description = Description;
    Agx->Described = TRUE;
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
        ULONG Words[APPLE_AGX_RECORD_WORDS];

        RtlCopyMemory(Words, Record, sizeof(Words));
        AgxEngineEvent(Agx, Words);
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

NTSTATUS
AgxDeviceControlPost(
    _Inout_ PAPPLE_AGX Agx,
    _In_ const VOID *Record,
    _In_ ULONG Timeout)
{
    return AgxSubmit(Agx, &Agx->DevCtl, APPLE_AGX_DOORBELL_DEVCTL, Record, Timeout);
}

/* The firmware imports its globals and starts on two device controls; only then does it say it
 * is ready. */
static
NTSTATUS
AgxDeviceControlOp(
    _Inout_ PAPPLE_AGX Agx,
    _In_ ULONG Opcode)
{
    ULONG Request[APPLE_AGX_RECORD_WORDS] = { 0 };
    NTSTATUS Status;

    Request[0] = Opcode;
    Status = AgxSubmit(Agx, &Agx->DevCtl, APPLE_AGX_DOORBELL_DEVCTL, Request, AGX_DEVCTL_TIMEOUT_MS);
    DbgPrint("appleagx: device control %#lx: %lx\n", Opcode, Status);
    return Status;
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
    if (Agx->Described)
    {
        ULONG Top = 0;

        Status = AppleAgxWriteStates(&Agx->Init, &Agx->Description, &Top);
        DbgPrint("appleagx: %lu operating points of %lu, up to %lu kHz: %lx\n", APPLE_AGX_DVFS_STATES,
                 Agx->Description.States, Top, Status);
        if (!NT_SUCCESS(Status))
            return Status;
        Agx->Reported.TopKilohertz = Top;
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
    Status = AgxDeviceControlOp(Agx, APPLE_AGX_DEVCTL_IMPORT_GLOBALS);
    if (NT_SUCCESS(Status))
        Status = AgxDeviceControlOp(Agx, APPLE_AGX_DEVCTL_START);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Waited = 0; Waited < AGX_READY_TIMEOUT_MS; Waited += 10)
    {
        AgxListen(Agx, 10);
        AgxDrainRings(Agx);
        if (AppleAgxInitReady(&Agx->Init))
        {
            DbgPrint("appleagx: the firmware is ready after %lu ms, %lu records read\n", Waited, Agx->Drained);
            Status = AgxDeviceControlOp(Agx, APPLE_AGX_DEVCTL_IMPORT_GLOBALS);
            if (!NT_SUCCESS(Status))
                return Status;
            AgxDrainRings(Agx);
            Status = AgxEngineStart(Agx);
            DbgPrint("appleagx: render and compute queues built: %lx\n", Status);
            if (!NT_SUCCESS(Status))
                return Status;
            Agx->Reported.State = APPLE_AGX_STATE_READY;
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
    if (NT_SUCCESS(Status))
        Agx->Reported.State = APPLE_AGX_STATE_STARTED;
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
    Agx->Reported.State = APPLE_AGX_STATE_ADOPTED;
    DbgPrint("appleagx: address tables installed, context 0 lower %I64x upper %I64x, host window at %I64x\n",
             Agx->Adopted.Lower.TopPhysical, Agx->Adopted.KernelRootPhysical, APPLE_UAT_HOST_BASE);
    return STATUS_SUCCESS;
}

static
VOID
AgxStop(
    _Inout_ PAPPLE_AGX Agx)
{
    KeSetEvent(&Agx->StopEvent, IO_NO_INCREMENT, FALSE);
    if (Agx->StartThread)
    {
        KeWaitForSingleObject(Agx->StartThread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Agx->StartThread);
        Agx->StartThread = NULL;
    }
    AgxEngineStop(Agx);
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
    ULONG CoresPerCluster, ClusterCount, Active = 0, Waited;
    LARGE_INTEGER Timeout;
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
    for (Waited = 0;; Waited += AGX_PMP_POLL_MS)
    {
        Status = AgxPowerVote(Agx, TRUE);
        if (NT_SUCCESS(Status) || (Status != STATUS_DEVICE_NOT_READY && Status != STATUS_OBJECT_NAME_NOT_FOUND) ||
            Waited >= AGX_PMP_WAIT_MS)
        {
            break;
        }
        Timeout.QuadPart = -10000LL * AGX_PMP_POLL_MS;
        if (KeWaitForSingleObject(&Agx->StopEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_SUCCESS)
            return STATUS_CANCELLED;
    }
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleagx: the power processor did not vote for the graphics block in %lu ms: %lx\n", Waited, Status);
        return Status;
    }
    DbgPrint("appleagx: the power processor voted for the graphics block after %lu ms\n", Waited);

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
    Agx->IdVersion = Version;
    Agx->CoreWord = Masks[0] | ((ULONG64)Masks[1] << 32);
    KeMemoryBarrier();
    Agx->Reported.State = APPLE_AGX_STATE_IDENTIFIED;
    Agx->Reported.Generation = (Version >> 24) & 0xff;
    Agx->Reported.Variant = (Version >> 16) & 0xff;
    Agx->Reported.Revision = Version & 0xffff;
    Agx->Reported.Clusters = ClusterCount;
    Agx->Reported.CoresPerCluster = CoresPerCluster;
    Agx->Reported.ActiveCores = Active;
    DbgPrint("appleagx: coprocessor control %08lx (%s), firmware not started\n", Control,
             (Control & AGX_ASC_CONTROL_RUN) ? "running" : "halted");
    AgxOpenAdt(Agx);
    if (Agx->AdtCopy)
        AppleAdtReadU32(&Agx->Adt, AppleAdtPath(&Agx->Adt, "/chosen"), "chip-id", &Agx->ChipId);
    if (!AgxReadAdt(Agx))
        AgxReadRegions(Agx);
    AgxReportFirmware(Agx);
    if ((Control & AGX_ASC_CONTROL_RUN) || !AgxTableIsClear(Agx))
        return STATUS_SUCCESS;
    if (NT_SUCCESS(AgxAdoptAddressSpace(Agx)))
        AgxBootFirmware(Agx, Control);
    return STATUS_SUCCESS;
}

/* The display driver asks how far the graphics processor has come; rendering on it is offered
 * only once its firmware is ready, so until then the answer keeps the software path in use. */
static
NTSTATUS
AgxGetParams(
    _In_ PAPPLE_AGX Agx,
    _Inout_ PVOID Buffer,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Written)
{
    DRM_ASAHI_PARAMS_GLOBAL Params;
    ULONG Kilohertz = Agx->Reported.TopKilohertz, Index;
    NTSTATUS Status;

    *Written = 0;
    if (Agx->Reported.State < APPLE_AGX_STATE_READY)
        return STATUS_DEVICE_NOT_READY;
    KeMemoryBarrier();
    if (InputLength >= sizeof(ULONG) && *(PULONG)Buffer != 0)
        return STATUS_INVALID_PARAMETER;
    if (Agx->Described)
    {
        for (Index = 0; Index < Agx->Description.States; ++Index)
            Kilohertz = max(Kilohertz, Agx->Description.State[Index].Frequency / 1000);
    }
    Status = AppleAgxDescribeParams(Agx->IdVersion, Agx->Reported.Clusters, Agx->CoreWord, Agx->ChipId, Kilohertz,
                                    &Params);
    if (!NT_SUCCESS(Status))
        return Status;
    *Written = min(OutputLength, sizeof(Params));
    RtlCopyMemory(Buffer, &Params, *Written);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
AgxObjectReference(
    _In_ PVOID Object,
    _In_ LONG Delta)
{
    PAGX_OBJECT Entry = Object;

    if (InterlockedExchangeAdd(&Entry->References, Delta) + Delta == 0)
    {
        MmFreePagesFromMdl(Entry->Mdl);
        ExFreePool(Entry->Mdl);
        ExFreePoolWithTag(Entry, AGX_CLIENT_TAG);
    }
}

static
MEMORY_CACHING_TYPE
AgxObjectCache(
    _In_ PAGX_OBJECT Object)
{
    return (Object->Flags & DRM_ASAHI_GEM_WRITEBACK) ? MmCached : MmWriteCombined;
}

static
VOID
AgxObjectUnmap(
    _In_ PAGX_CLIENT Client,
    _Inout_ PAGX_OBJECT Object)
{
    /* FIXME: a mapping made by a process that is no longer the caller keeps its object alive until reboot. */
    if (!Object->UserAddress || PsGetCurrentProcess() != Client->Process)
        return;
    MmUnmapLockedPages(Object->UserAddress, Object->Mdl);
    Object->UserAddress = NULL;
    AgxObjectReference(Object, -1);
}

static
PAGX_VM
AgxClientVm(
    _In_ PAGX_CLIENT Client,
    _In_ ULONG VmId)
{
    return (VmId && VmId <= AGX_MAX_VMS) ? Client->Vm[VmId - 1] : NULL;
}

static
PAGX_OBJECT
AgxClientObject(
    _In_ PAGX_CLIENT Client,
    _In_ ULONG Handle)
{
    return (Handle && Handle <= AGX_MAX_OBJECTS) ? Client->Object[Handle - 1] : NULL;
}

static
NTSTATUS
AgxBindingsGrow(
    _Inout_ PAGX_VM Vm)
{
    ULONG Capacity = Vm->Bindings.Capacity * 2;
    PAPPLE_AGX_BINDING Entry;

    if (Capacity > AGX_MAX_BINDINGS)
        return STATUS_INSUFFICIENT_RESOURCES;
    Entry = ExAllocatePoolZero(PagedPool, Capacity * sizeof(*Entry), AGX_CLIENT_TAG);
    if (!Entry)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Entry, Vm->Bindings.Entry, Vm->Bindings.Count * sizeof(*Entry));
    ExFreePoolWithTag(Vm->Bindings.Entry, AGX_CLIENT_TAG);
    Vm->Bindings.Entry = Entry;
    Vm->Bindings.Capacity = Capacity;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AgxVmUnbind(
    _Inout_ PAGX_VM Vm,
    _In_ ULONG64 Address,
    _In_ ULONG64 Range)
{
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < Vm->Bindings.Count; ++Index)
    {
        const APPLE_AGX_BINDING *Entry = &Vm->Bindings.Entry[Index];
        ULONG64 Start = max(Address, Entry->Address), End = min(Address + Range, Entry->Address + Entry->Range);

        if (Start < End)
        {
            Status = AppleUatUnmap(&Vm->Uat, Start, (SIZE_T)(End - Start));
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }
    while ((Status = AppleAgxBindingRemove(&Vm->Bindings, Address, Range)) == STATUS_INSUFFICIENT_RESOURCES)
    {
        Status = AgxBindingsGrow(Vm);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return Status;
}

static
NTSTATUS
AgxVmMap(
    _Inout_ PAGX_VM Vm,
    _In_ PAGX_OBJECT Object,
    _In_ const DRM_ASAHI_GEM_BIND_OP *Op,
    _In_ const APPLE_UAT_PROTECTION *Protection)
{
    APPLE_AGX_BINDING Binding;
    PPFN_NUMBER Frames = MmGetMdlPfnArray(Object->Mdl);
    ULONG64 Page, At;
    NTSTATUS Status;

    Binding.Address = Op->Addr;
    Binding.Range = Op->Range;
    Binding.Offset = Op->Offset;
    Binding.Object = Object;
    Binding.SinglePage = (Op->Flags & DRM_ASAHI_BIND_SINGLE_PAGE) != 0;
    while ((Status = AppleAgxBindingInsert(&Vm->Bindings, &Binding)) == STATUS_INSUFFICIENT_RESOURCES)
    {
        Status = AgxBindingsGrow(Vm);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    if (!NT_SUCCESS(Status))
        return Status;
    for (Page = 0; Page < Op->Range; Page += APPLE_UAT_PAGE_SIZE)
    {
        At = Op->Offset + (Binding.SinglePage ? 0 : Page);
        Status = AppleUatMap(&Vm->Uat, Op->Addr + Page, (ULONG64)Frames[At >> PAGE_SHIFT] << PAGE_SHIFT,
                             APPLE_UAT_PAGE_SIZE, Protection);
        if (!NT_SUCCESS(Status))
        {
            AgxVmUnbind(Vm, Op->Addr, Op->Range);
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

static
VOID
AgxVmFree(
    _In_ PAGX_VM Vm)
{
    ULONG Index;

    for (Index = 0; Index < Vm->Bindings.Count; ++Index)
        AppleUatUnmap(&Vm->Uat, Vm->Bindings.Entry[Index].Address, (SIZE_T)Vm->Bindings.Entry[Index].Range);
    while (Vm->Bindings.Count)
    {
        Vm->Bindings.Reference(Vm->Bindings.Entry[Vm->Bindings.Count - 1].Object, -1);
        --Vm->Bindings.Count;
    }
    AgxEngineRelease(Vm);
    AppleUatDestroy(&Vm->Uat);
    ExFreePoolWithTag(Vm->Bindings.Entry, AGX_CLIENT_TAG);
    ExFreePoolWithTag(Vm, AGX_CLIENT_TAG);
}

static
NTSTATUS
AgxVmCreate(
    _Inout_ PAGX_CLIENT Client,
    _Inout_ PDRM_ASAHI_VM_CREATE Create)
{
    APPLE_AGX_VM_RANGES Ranges;
    PAGX_VM Vm;
    ULONG Index;
    NTSTATUS Status;

    Status = AppleAgxDescribeVm(Create, &Ranges);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Index = 0; Index < AGX_MAX_VMS && Client->Vm[Index]; ++Index)
        ;
    if (Index == AGX_MAX_VMS)
        return STATUS_INSUFFICIENT_RESOURCES;
    Vm = ExAllocatePoolZero(PagedPool, sizeof(*Vm), AGX_CLIENT_TAG);
    if (!Vm)
        return STATUS_INSUFFICIENT_RESOURCES;
    Vm->Bindings.Entry = ExAllocatePoolZero(PagedPool, AGX_FIRST_BINDINGS * sizeof(APPLE_AGX_BINDING), AGX_CLIENT_TAG);
    if (!Vm->Bindings.Entry)
    {
        ExFreePoolWithTag(Vm, AGX_CLIENT_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Vm->Bindings.Capacity = AGX_FIRST_BINDINGS;
    Vm->Bindings.Reference = AgxObjectReference;
    Status = AppleUatCreate(&Vm->Uat, FALSE, "appleagx client");
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Vm->Bindings.Entry, AGX_CLIENT_TAG);
        ExFreePoolWithTag(Vm, AGX_CLIENT_TAG);
        return Status;
    }
    Vm->Ranges = Ranges;
    Client->Vm[Index] = Vm;
    Create->VmId = Index + 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AgxGemCreate(
    _Inout_ PAGX_CLIENT Client,
    _Inout_ PDRM_ASAHI_GEM_CREATE Create)
{
    PHYSICAL_ADDRESS Low, High, Chunk;
    PAGX_OBJECT Object;
    ULONG64 Size;
    ULONG Index;
    NTSTATUS Status;

    Status = AppleAgxGemSize(Create, &Size);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((Create->Flags & DRM_ASAHI_GEM_VM_PRIVATE) && !AgxClientVm(Client, Create->VmId))
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < AGX_MAX_OBJECTS && Client->Object[Index]; ++Index)
        ;
    if (Index == AGX_MAX_OBJECTS)
        return STATUS_INSUFFICIENT_RESOURCES;
    Object = ExAllocatePoolZero(NonPagedPool, sizeof(*Object), AGX_CLIENT_TAG);
    if (!Object)
        return STATUS_INSUFFICIENT_RESOURCES;
    Object->Flags = Create->Flags;
    Object->VmId = Create->VmId;
    Object->Size = Size;
    Low.QuadPart = 0;
    High.QuadPart = (LONGLONG)((1ULL << APPLE_UAT_OUTPUT_BITS) - 1);
    Chunk.QuadPart = APPLE_UAT_PAGE_SIZE;
    Object->Mdl = MmAllocatePagesForMdlEx(Low, High, Chunk, (SIZE_T)Size, AgxObjectCache(Object),
                                          MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED);
    if (!Object->Mdl || MmGetMdlByteCount(Object->Mdl) != Size)
    {
        if (Object->Mdl)
        {
            MmFreePagesFromMdl(Object->Mdl);
            ExFreePool(Object->Mdl);
        }
        ExFreePoolWithTag(Object, AGX_CLIENT_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Object->References = 1;
    Client->Object[Index] = Object;
    Create->Handle = Index + 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AgxGemMap(
    _Inout_ PAGX_CLIENT Client,
    _Inout_ PAPPLE_AGX_GEM_HANDLE Map)
{
    PAGX_OBJECT Object = AgxClientObject(Client, Map->Handle);
    PVOID Address = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Object || Map->Flags)
        return STATUS_INVALID_PARAMETER;
    if (PsGetCurrentProcess() != Client->Process)
        return STATUS_ACCESS_DENIED;
    if (!Object->UserAddress)
    {
        _SEH2_TRY
        {
            Address = MmMapLockedPagesSpecifyCache(Object->Mdl, UserMode, AgxObjectCache(Object), NULL, FALSE,
                                                   NormalPagePriority | MdlMappingNoExecute);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status))
            return Status;
        if (!Address)
            return STATUS_INSUFFICIENT_RESOURCES;
        Object->UserAddress = Address;
        AgxObjectReference(Object, 1);
    }
    Map->Address = (ULONG64)(ULONG_PTR)Object->UserAddress;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AgxGemClose(
    _Inout_ PAGX_CLIENT Client,
    _In_ ULONG Handle)
{
    PAGX_OBJECT Object = AgxClientObject(Client, Handle);

    if (!Object)
        return STATUS_INVALID_PARAMETER;
    Client->Object[Handle - 1] = NULL;
    AgxObjectUnmap(Client, Object);
    AgxObjectReference(Object, -1);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AgxVmBind(
    _Inout_ PAGX_CLIENT Client,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    const DRM_ASAHI_VM_BIND *Bind = Buffer;
    DRM_ASAHI_GEM_BIND_OP Op;
    APPLE_UAT_PROTECTION Protection;
    PAGX_OBJECT Object;
    PAGX_VM Vm;
    ULONG Index, Pass;
    NTSTATUS Status;

    if (Length < sizeof(*Bind) || Bind->Pad || Bind->Userptr || !Bind->NumBinds || Bind->NumBinds > AGX_MAX_BIND_OPS ||
        Bind->Stride < sizeof(Op) || (ULONG64)Bind->NumBinds * Bind->Stride > Length - sizeof(*Bind))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Vm = AgxClientVm(Client, Bind->VmId);
    if (!Vm)
        return STATUS_INVALID_PARAMETER;
    for (Pass = 0; Pass < 2; ++Pass)
    {
        for (Index = 0; Index < Bind->NumBinds; ++Index)
        {
            RtlCopyMemory(&Op, (PUCHAR)(Bind + 1) + (SIZE_T)Index * Bind->Stride, sizeof(Op));
            Object = NULL;
            if (!(Op.Flags & DRM_ASAHI_BIND_UNBIND))
            {
                Object = AgxClientObject(Client, Op.Handle);
                if (!Object || ((Object->Flags & DRM_ASAHI_GEM_VM_PRIVATE) && Object->VmId != Bind->VmId))
                    return STATUS_INVALID_PARAMETER;
            }
            Status = AppleAgxCheckBind(&Op, &Vm->Ranges, Object ? Object->Size : 0, &Protection);
            if (NT_SUCCESS(Status) && Object && AppleAgxClientReserved(Op.Addr, Op.Range))
                Status = STATUS_CONFLICTING_ADDRESSES;
            if (!NT_SUCCESS(Status) || Pass == 0)
            {
                if (!NT_SUCCESS(Status))
                    return Status;
                continue;
            }
            /* FIXME: an operation failing here leaves the ones before it applied. */
            Status = Object ? AgxVmMap(Vm, Object, &Op, &Protection) : AgxVmUnbind(Vm, Op.Addr, Op.Range);
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }
    return STATUS_SUCCESS;
}

/* Every queue runs on the device's own render and compute queues; what a client queue keeps is
 * the address space it runs in and where its shaders are. */
static
NTSTATUS
AgxQueueCreate(
    _Inout_ PAGX_CLIENT Client,
    _Inout_ PDRM_ASAHI_QUEUE_CREATE Create)
{
    PAGX_VM Vm = AgxClientVm(Client, Create->VmId);
    PAGX_QUEUE Queue;
    ULONG Index;

    if (!Vm || Create->Flags || Create->Priority > DRM_ASAHI_PRIORITY_MEDIUM ||
        (Create->UscExecBase & 0xffffffffULL) || Create->UscExecBase < Vm->Ranges.UserStart ||
        Create->UscExecBase > Vm->Ranges.UserEnd || Vm->Ranges.UserEnd - Create->UscExecBase < 0x100000000ULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < AGX_MAX_QUEUES && Client->Queue[Index]; ++Index)
        ;
    if (Index == AGX_MAX_QUEUES)
        return STATUS_INSUFFICIENT_RESOURCES;
    Queue = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Queue), AGX_CLIENT_TAG);
    if (!Queue)
        return STATUS_INSUFFICIENT_RESOURCES;
    Queue->VmId = Create->VmId;
    Queue->Priority = Create->Priority;
    Queue->UscBase = Create->UscExecBase;
    Client->Queue[Index] = Queue;
    Create->QueueId = Index + 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AgxQueueDestroy(
    _Inout_ PAGX_CLIENT Client,
    _In_ const DRM_ASAHI_QUEUE_DESTROY *Destroy)
{
    if (Destroy->Pad || !Destroy->QueueId || Destroy->QueueId > AGX_MAX_QUEUES || !Client->Queue[Destroy->QueueId - 1])
        return STATUS_INVALID_PARAMETER;
    ExFreePoolWithTag(Client->Queue[Destroy->QueueId - 1], AGX_CLIENT_TAG);
    Client->Queue[Destroy->QueueId - 1] = NULL;
    return STATUS_SUCCESS;
}

/* A timestamp buffer is bound for the firmware to write the times a command asks for; the object
 * stays alive for as long as it is bound. */
static
NTSTATUS
AgxBindObject(
    _Inout_ PAGX_CLIENT Client,
    _Inout_ PDRM_ASAHI_GEM_BIND_OBJECT Bind)
{
    PAGX_OBJECT Object;
    PAGX_BOUND Bound;
    ULONG64 Address;
    ULONG Index;
    NTSTATUS Status;

    if (Bind->Pad || Bind->VmId)
        return STATUS_INVALID_PARAMETER;
    if (Bind->Op == DRM_ASAHI_BIND_OBJECT_OP_UNBIND)
    {
        if (!Bind->ObjectHandle || Bind->ObjectHandle > AGX_MAX_BOUND || !Client->Bound[Bind->ObjectHandle - 1].Object)
            return STATUS_INVALID_PARAMETER;
        Bound = &Client->Bound[Bind->ObjectHandle - 1];
        AgxEngineUnmapArena(Client->Agx, Bound->Address, Bound->Range);
        AgxObjectReference(Bound->Object, -1);
        RtlZeroMemory(Bound, sizeof(*Bound));
        return STATUS_SUCCESS;
    }
    Object = AgxClientObject(Client, Bind->Handle);
    if (Bind->Op != DRM_ASAHI_BIND_OBJECT_OP_BIND || Bind->Flags != DRM_ASAHI_BIND_OBJECT_USAGE_TIMESTAMPS ||
        !Object || (Object->Flags & DRM_ASAHI_GEM_VM_PRIVATE) || !Bind->Range ||
        ((Bind->Offset | Bind->Range) & (APPLE_UAT_PAGE_SIZE - 1)) || Bind->Offset >= Object->Size ||
        Bind->Range > Object->Size - Bind->Offset)
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = 0; Index < AGX_MAX_BOUND && Client->Bound[Index].Object; ++Index)
        ;
    if (Index == AGX_MAX_BOUND)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = AgxEngineMapArena(Client->Agx, Object->Mdl, Bind->Offset, Bind->Range, &Address);
    if (!NT_SUCCESS(Status))
        return Status;
    AgxObjectReference(Object, 1);
    Client->Bound[Index].Object = Object;
    Client->Bound[Index].Address = Address;
    Client->Bound[Index].Range = Bind->Range;
    Bind->ObjectHandle = Index + 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AgxClientControl(
    _Inout_ PAGX_CLIENT Client,
    _In_ ULONG Code,
    _Inout_ PVOID Buffer,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Written)
{
    PAGX_VM Vm;
    NTSTATUS Status;
    ULONG Size;

    *Written = 0;
    switch (Code)
    {
        case IOCTL_APPLE_AGX_VM_CREATE: Size = sizeof(DRM_ASAHI_VM_CREATE); break;
        case IOCTL_APPLE_AGX_VM_DESTROY: Size = sizeof(DRM_ASAHI_VM_DESTROY); break;
        case IOCTL_APPLE_AGX_GEM_CREATE: Size = sizeof(DRM_ASAHI_GEM_CREATE); break;
        case IOCTL_APPLE_AGX_GEM_CLOSE:
        case IOCTL_APPLE_AGX_GEM_MAP: Size = sizeof(APPLE_AGX_GEM_HANDLE); break;
        case IOCTL_APPLE_AGX_VM_BIND: Size = sizeof(DRM_ASAHI_VM_BIND); break;
        case IOCTL_APPLE_AGX_QUEUE_CREATE: Size = sizeof(DRM_ASAHI_QUEUE_CREATE); break;
        case IOCTL_APPLE_AGX_QUEUE_DESTROY: Size = sizeof(DRM_ASAHI_QUEUE_DESTROY); break;
        case IOCTL_APPLE_AGX_SUBMIT: Size = sizeof(DRM_ASAHI_SUBMIT); break;
        case IOCTL_APPLE_AGX_GEM_BIND_OBJECT: Size = sizeof(DRM_ASAHI_GEM_BIND_OBJECT); break;
        default: return STATUS_INVALID_DEVICE_REQUEST;
    }
    if (InputLength < Size)
        return STATUS_INVALID_PARAMETER;
    ExAcquireFastMutex(&Client->Lock);
    switch (Code)
    {
        case IOCTL_APPLE_AGX_VM_CREATE:
            Status = AgxVmCreate(Client, Buffer);
            break;
        case IOCTL_APPLE_AGX_VM_DESTROY:
            Vm = AgxClientVm(Client, ((PDRM_ASAHI_VM_DESTROY)Buffer)->VmId);
            Status = (Vm && !((PDRM_ASAHI_VM_DESTROY)Buffer)->Pad) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
            if (NT_SUCCESS(Status))
            {
                Client->Vm[((PDRM_ASAHI_VM_DESTROY)Buffer)->VmId - 1] = NULL;
                AgxVmFree(Vm);
            }
            break;
        case IOCTL_APPLE_AGX_GEM_CREATE:
            Status = AgxGemCreate(Client, Buffer);
            break;
        case IOCTL_APPLE_AGX_GEM_CLOSE:
            Status = AgxGemClose(Client, ((PAPPLE_AGX_GEM_HANDLE)Buffer)->Handle);
            break;
        case IOCTL_APPLE_AGX_GEM_MAP:
            Status = AgxGemMap(Client, Buffer);
            break;
        case IOCTL_APPLE_AGX_QUEUE_CREATE:
            Status = AgxQueueCreate(Client, Buffer);
            break;
        case IOCTL_APPLE_AGX_QUEUE_DESTROY:
            Status = AgxQueueDestroy(Client, Buffer);
            break;
        case IOCTL_APPLE_AGX_SUBMIT:
            Status = AgxSubmitCommands(Client->Agx, Client, Buffer, InputLength);
            break;
        case IOCTL_APPLE_AGX_GEM_BIND_OBJECT:
            Status = AgxBindObject(Client, Buffer);
            break;
        default:
            Status = AgxVmBind(Client, Buffer, InputLength);
            break;
    }
    ExReleaseFastMutex(&Client->Lock);
    if (NT_SUCCESS(Status) && Code != IOCTL_APPLE_AGX_VM_BIND && Code != IOCTL_APPLE_AGX_VM_DESTROY &&
        Code != IOCTL_APPLE_AGX_GEM_CLOSE && Code != IOCTL_APPLE_AGX_QUEUE_DESTROY && Code != IOCTL_APPLE_AGX_SUBMIT)
    {
        *Written = min(OutputLength, Size);
    }
    return Status;
}

static
NTSTATUS
NTAPI
AgxCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PAGX_CLIENT Client;
    NTSTATUS Status = STATUS_SUCCESS;

    Client = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Client), AGX_CLIENT_TAG);
    if (!Client)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        ExInitializeFastMutex(&Client->Lock);
        Client->Agx = DeviceObject->DeviceExtension;
        Client->Process = PsGetCurrentProcess();
        ObReferenceObject(Client->Process);
        Stack->FileObject->FsContext = Client;
    }
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
AgxCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAGX_CLIENT Client = IoGetCurrentIrpStackLocation(Irp)->FileObject->FsContext;
    ULONG Index;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (Client)
    {
        ExAcquireFastMutex(&Client->Lock);
        for (Index = 0; Index < AGX_MAX_BOUND; ++Index)
        {
            if (Client->Bound[Index].Object)
            {
                AgxEngineUnmapArena(Client->Agx, Client->Bound[Index].Address, Client->Bound[Index].Range);
                AgxObjectReference(Client->Bound[Index].Object, -1);
                RtlZeroMemory(&Client->Bound[Index], sizeof(Client->Bound[Index]));
            }
        }
        for (Index = 0; Index < AGX_MAX_OBJECTS; ++Index)
        {
            if (Client->Object[Index])
                AgxGemClose(Client, Index + 1);
        }
        for (Index = 0; Index < AGX_MAX_QUEUES; ++Index)
        {
            if (Client->Queue[Index])
            {
                ExFreePoolWithTag(Client->Queue[Index], AGX_CLIENT_TAG);
                Client->Queue[Index] = NULL;
            }
        }
        for (Index = 0; Index < AGX_MAX_VMS; ++Index)
        {
            if (Client->Vm[Index])
            {
                AgxVmFree(Client->Vm[Index]);
                Client->Vm[Index] = NULL;
            }
        }
        ExReleaseFastMutex(&Client->Lock);
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AgxClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PFILE_OBJECT FileObject = IoGetCurrentIrpStackLocation(Irp)->FileObject;
    PAGX_CLIENT Client = FileObject->FsContext;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (Client)
    {
        FileObject->FsContext = NULL;
        ObDereferenceObject(Client->Process);
        ExFreePoolWithTag(Client, AGX_CLIENT_TAG);
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AgxDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PAPPLE_AGX Agx = DeviceObject->DeviceExtension;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;

    Irp->IoStatus.Information = 0;
    if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_APPLE_AGX_GET_PARAMS)
    {
        Status = AgxGetParams(Agx, Irp->AssociatedIrp.SystemBuffer,
                              Stack->Parameters.DeviceIoControl.InputBufferLength,
                              Stack->Parameters.DeviceIoControl.OutputBufferLength,
                              &Irp->IoStatus.Information);
    }
    else if (Stack->Parameters.DeviceIoControl.IoControlCode != IOCTL_APPLE_AGX_QUERY_STATE)
    {
        Status = Stack->FileObject && Stack->FileObject->FsContext ?
                 AgxClientControl(Stack->FileObject->FsContext, Stack->Parameters.DeviceIoControl.IoControlCode,
                                  Irp->AssociatedIrp.SystemBuffer,
                                  Stack->Parameters.DeviceIoControl.InputBufferLength,
                                  Stack->Parameters.DeviceIoControl.OutputBufferLength,
                                  &Irp->IoStatus.Information) :
                 STATUS_INVALID_DEVICE_REQUEST;
    }
    else
    {
        if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(APPLE_AGX_REPORT))
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            Agx->Reported.Records = Agx->Drained;
            RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Agx->Reported, sizeof(Agx->Reported));
            Irp->IoStatus.Information = sizeof(Agx->Reported);
            Status = STATUS_SUCCESS;
        }
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
VOID
NTAPI
AgxStartThread(
    _In_ PVOID Context)
{
    PAPPLE_AGX Agx = Context;
    NTSTATUS Status;

    Status = AgxStart(Agx);
    if (!NT_SUCCESS(Status))
        DbgPrint("appleagx: the graphics processor was not identified: %lx\n", Status);
    PsTerminateSystemThread(Status);
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
                HANDLE Thread;

                KeClearEvent(&Agx->StopEvent);
                Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, AgxStartThread, Agx);
                if (NT_SUCCESS(Status))
                {
                    Status = ObReferenceObjectByHandle(Thread, SYNCHRONIZE, *PsThreadType, KernelMode,
                                                       (PVOID *)&Agx->StartThread, NULL);
                    ZwClose(Thread);
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
        {
            UNICODE_STRING Link = RTL_CONSTANT_STRING(APPLE_AGX_DOS_DEVICE_NAME);

            IoDeleteSymbolicLink(&Link);
            AgxStop(Agx);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Agx->Lower, Irp);
            IoDetachDevice(Agx->Lower);
            IoDeleteDevice(DeviceObject);
            return Status;
        }
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
    UNICODE_STRING Name = RTL_CONSTANT_STRING(APPLE_AGX_DEVICE_NAME);
    UNICODE_STRING Link = RTL_CONSTANT_STRING(APPLE_AGX_DOS_DEVICE_NAME);
    PAPPLE_AGX Agx;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(APPLE_AGX), &Name, FILE_DEVICE_VIDEO, FILE_DEVICE_SECURE_OPEN,
                            FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Agx = Fdo->DeviceExtension;
    RtlZeroMemory(Agx, sizeof(*Agx));
    Agx->Self = Fdo;
    Agx->Pdo = Pdo;
    KeInitializeEvent(&Agx->StopEvent, NotificationEvent, FALSE);
    ExInitializeFastMutex(&Agx->Engine.Lock);
    Agx->Lower = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (!Agx->Lower)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Status = IoCreateSymbolicLink(&Link, &Name);
    if (!NT_SUCCESS(Status))
    {
        IoDetachDevice(Agx->Lower);
        IoDeleteDevice(Fdo);
        return Status;
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
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AgxDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = AgxCreate;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = AgxCleanup;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = AgxClose;
    DriverObject->MajorFunction[IRP_MJ_PNP] = AgxPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AgxPower;
    DriverObject->DriverExtension->AddDevice = AgxAddDevice;
    return STATUS_SUCCESS;
}
