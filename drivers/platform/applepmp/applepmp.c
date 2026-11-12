/*
 * PROJECT:     LiberNT Apple Power Management Processor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device power votes of the Apple silicon power management processor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/applertkit.h>
#include <reactos/drivers/appleadt.h>
#include <reactos/drivers/appledart.h>
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/applepmp.h>
#include <reactos/drivers/fdtdrv.h>

#define PMP_TAG                     'pPpA'
#define PMP_ENDPOINT                0x20
#define PMP_CPU_CONTROL             0x44
#define PMP_CPU_CONTROL_RUN          0x10
#define PMP_BOOT_TIMEOUT_MS         2000
#define PMP_REPLY_TIMEOUT_MS        2000
#define PMP_MAX_SHARED              64
#define PMP_MAX_IOREGS              512
#define PMP_READY_TIMEOUT_MS        10000
#define PMP_POLL_MS                 20
#define PMP_ADT_MAX_LENGTH          0x200000
#define PMP_REQUEST_WORD_BITS       64
#define PMP_TUNABLE_NAME_SIZE       (sizeof("apple,tunable-") + APPLE_PMP_IOREG_NAME_SIZE)
#define PMP_SHARED_PAGE_SIZE        0x4000
#define PMP_SHARED_ADDRESS_BITS     44
#define PMP_DART_WINDOW_BASE        0x10000000ULL
#define PMP_DART_WINDOW_END         0x20000000ULL

typedef struct _PMP_SHARED
{
    PVOID Allocation;
    SIZE_T AllocationSize;
    PVOID Virtual;
    ULONG64 Physical;
    ULONG64 Iova;
    ULONG Size;
    BOOLEAN Freed;
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
    APPLE_DART_ADOPTED Dart;
    BOOLEAN DartAdopted;
    APPLE_RTKIT Rtkit;
    FAST_MUTEX Lock;
    BOOLEAN Running;
    volatile ULONG64 Expected;
    volatile BOOLEAN Acknowledged;
    volatile BOOLEAN Lost;
    ULONG64 ValueBuffer;
    BOOLEAN ValueBufferSet;
    BOOLEAN IoregConfirmed;
    ULONG IoregCount;
    ULONG IoregSizes[PMP_MAX_IOREGS];
    ULONG Requests;
    KEVENT WorkEvent;
    KEVENT StopEvent;
    KEVENT ReadyEvent;
    PKTHREAD Thread;
    volatile LONG Stop;
    volatile LONG ReadyTimedOut;
} APPLE_PMP, *PAPPLE_PMP;

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
    Shared->Iova = Shared->Physical;
    Shared->Freed = FALSE;
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
NTSTATUS
PmpMapShared(
    _Inout_ PAPPLE_PMP Pmp,
    _Inout_ PPMP_SHARED Shared)
{
    Shared->Iova = Shared->Physical;
    if (!Pmp->DartAdopted)
        return STATUS_SUCCESS;
    return AppleDartAdoptedMap(&Pmp->Dart, Shared->Physical, Shared->Size, &Shared->Iova);
}

static
PPMP_SHARED
PmpFindShared(
    _In_ PAPPLE_PMP Pmp,
    _In_ ULONG64 Iova)
{
    ULONG Index;

    for (Index = Pmp->SharedCount; Index-- > 0;)
    {
        if (!Pmp->Shared[Index].Freed && Pmp->Shared[Index].Iova == Iova)
            return &Pmp->Shared[Index];
    }
    return NULL;
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
        ULONG64 Physical = *Iova;

        if (Pmp->DartAdopted && !AppleDartAdoptedTranslate(&Pmp->Dart, *Iova, &Physical))
        {
            DbgPrint("applepmp: endpoint %u asked for %lu bytes at device address %I64x, which is not mapped\n",
                     Endpoint, Size, *Iova);
            return FALSE;
        }
        if (!Pmp->Sram || Physical < Pmp->SramBase || Physical + Size > Pmp->SramBase + Pmp->SramSize)
        {
            DbgPrint("applepmp: endpoint %u asked for %lu bytes at %I64x (%I64x), outside its own memory\n", Endpoint,
                     Size, *Iova, Physical);
            return FALSE;
        }
        *Virtual = Pmp->Sram + (Physical - Pmp->SramBase);
        return TRUE;
    }
    Shared = PmpAllocateShared(Pmp, Size);
    if (!Shared)
    {
        DbgPrint("applepmp: endpoint %u asked for %lu bytes that could not be allocated\n", Endpoint, Size);
        return FALSE;
    }
    {
        NTSTATUS Status = PmpMapShared(Pmp, Shared);

        if (!NT_SUCCESS(Status))
        {
            DbgPrint("applepmp: endpoint %u buffer at %I64x was not mapped for the processor: %lx\n", Endpoint,
                     Shared->Physical, Status);
            Shared->Freed = TRUE;
            return FALSE;
        }
    }
    *Iova = Shared->Iova;
    *Virtual = Shared->Virtual;
    *Preassigned = FALSE;
    DbgPrint("applepmp: endpoint %u gets %lu bytes at %I64x, device address %I64x\n", Endpoint, Shared->Size,
             Shared->Physical, Shared->Iova);
    return TRUE;
}

static
BOOLEAN
PmpServeRequest(
    _Inout_ PAPPLE_PMP Pmp,
    _In_ ULONG64 Message,
    _Out_ PULONG64 Reply)
{
    ULONG Opcode = ApplePmpRequestOpcode(Message);
    ULONG64 Address = Message & APPLE_PMP_REQUEST_ADDRESS_MASK;
    CHAR Property[PMP_TUNABLE_NAME_SIZE];
    PPMP_SHARED Shared, Value;
    const VOID *Data;
    ULONG Size, Length, Index;
    NTSTATUS Status;

    *Reply = 0;
    Pmp->Requests++;
    switch (Opcode)
    {
        case APPLE_PMP_GET_IOVA_TABLE:
            if (FdtGetProperty(&Pmp->Tree.Fdt, Pmp->Tree.Node, "apple,pio-ranges", &Length))
            {
                /* TODO: map apple,pio-ranges into the processor's DART and hand it their table */
                DbgPrint("applepmp: the processor's I/O ranges are not given to it\n");
            }
            *Reply = ApplePmpRequestReply(Opcode, 0);
            return TRUE;

        case APPLE_PMP_MALLOC:
            Size = (ULONG)(Message & APPLE_PMP_REQUEST_SIZE_MASK);
            Shared = Size ? PmpAllocateShared(Pmp, Size) : NULL;
            Status = Shared ? PmpMapShared(Pmp, Shared) : STATUS_INSUFFICIENT_RESOURCES;
            if (!NT_SUCCESS(Status))
            {
                if (Shared)
                    Shared->Freed = TRUE;
                DbgPrint("applepmp: %lu bytes asked by the processor were not given: %lx\n", Size, Status);
                return FALSE;
            }
            *Reply = ApplePmpRequestReply(Opcode, Shared->Iova);
            return TRUE;

        case APPLE_PMP_FREE:
            Shared = PmpFindShared(Pmp, Address);
            if (!Shared)
            {
                DbgPrint("applepmp: the processor freed %I64x, which it was not given\n", Address);
                return FALSE;
            }
            Shared->Freed = TRUE;
            *Reply = ApplePmpRequestReply(Opcode, 0);
            return TRUE;

        case APPLE_PMP_SET_BUF:
            Shared = PmpFindShared(Pmp, Address);
            if (!Shared || Shared->Size < sizeof(ULONG64) || Pmp->ValueBufferSet)
            {
                DbgPrint("applepmp: the value buffer at %I64x is not usable\n", Address);
                return FALSE;
            }
            KeMemoryBarrier();
            RtlCopyMemory(&Pmp->ValueBuffer, Shared->Virtual, sizeof(Pmp->ValueBuffer));
            Pmp->ValueBufferSet = TRUE;
            *Reply = ApplePmpRequestReply(Opcode, 0);
            return TRUE;

        case APPLE_PMP_REGISTER_IOREG:
            Shared = PmpFindShared(Pmp, Address);
            if (!Shared || Shared->Size < APPLE_PMP_IOREG_MESSAGE_SIZE || Pmp->IoregCount == PMP_MAX_IOREGS)
            {
                DbgPrint("applepmp: the register request at %I64x is not usable\n", Address);
                return FALSE;
            }
            KeMemoryBarrier();
            RtlCopyMemory(&Size, (PUCHAR)Shared->Virtual + APPLE_PMP_IOREG_SIZE_OFFSET, sizeof(Size));
            if (!Size && ApplePmpTunableName(Shared->Virtual, Property, sizeof(Property)))
            {
                Value = Pmp->ValueBufferSet ? PmpFindShared(Pmp, Pmp->ValueBuffer) : NULL;
                if (!Value)
                {
                    DbgPrint("applepmp: %s was asked for before the value buffer was given\n", Property);
                    return FALSE;
                }
                Data = FdtGetProperty(&Pmp->Tree.Fdt, Pmp->Tree.Node, Property, &Length);
                if (!Data)
                {
                    DbgPrint("applepmp: the tree has no %s\n", Property);
                }
                else if (Length > Value->Size)
                {
                    DbgPrint("applepmp: %s (%lu bytes) does not fit the %lu byte value buffer\n", Property, Length,
                             Value->Size);
                }
                else
                {
                    RtlCopyMemory(Value->Virtual, Data, Length);
                    KeMemoryBarrier();
                    Size = Length;
                }
            }
            Pmp->IoregSizes[Pmp->IoregCount++] = Size;
            *Reply = ApplePmpRequestReply(Opcode, ((ULONG64)Pmp->IoregCount << 32) | Size);
            return TRUE;

        case APPLE_PMP_SET_IOREG:
            Index = (ULONG)(Message & APPLE_PMP_REQUEST_INDEX_MASK);
            if (Index >= Pmp->IoregCount)
            {
                DbgPrint("applepmp: register %lu was not registered (%lu are)\n", Index, Pmp->IoregCount);
                return FALSE;
            }
            *Reply = ApplePmpRequestReply(Opcode, Pmp->IoregSizes[Index]);
            return TRUE;

        case APPLE_PMP_UPDATE_IOREG:
            if (!Pmp->IoregConfirmed)
            {
                DbgPrint("applepmp: the processor is set up after %lu requests, %lu registers\n", Pmp->Requests,
                         Pmp->IoregCount);
                Pmp->IoregConfirmed = TRUE;
                KeSetEvent(&Pmp->ReadyEvent, IO_NO_INCREMENT, FALSE);
            }
            *Reply = (ULONG64)APPLE_PMP_CONFIRM_IOREG << APPLE_PMP_REQUEST_SHIFT;
            return TRUE;
    }
    DbgPrint("applepmp: request %I64x is not one the driver knows\n", Message);
    return FALSE;
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
    ULONG64 Reply;

    if (Endpoint != PMP_ENDPOINT)
    {
        DbgPrint("applepmp: endpoint %#x sent %I64x\n", Endpoint, Message);
        return;
    }
    if (!(Message & APPLE_PMP_COMMAND_MARKER))
    {
        if (PmpServeRequest(Pmp, Message, &Reply) && !AppleRtkitSend(&Pmp->Rtkit, PMP_ENDPOINT, Reply))
            DbgPrint("applepmp: the answer %I64x to %I64x was not sent\n", Reply, Message);
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

    LARGE_INTEGER Timeout;

    if (Device > 15)
        return STATUS_INVALID_PARAMETER;
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Timeout.QuadPart = Pmp->ReadyTimedOut ? 0 : -(LONGLONG)PMP_READY_TIMEOUT_MS * 10000;
    if (Pmp->Running &&
        KeWaitForSingleObject(&Pmp->ReadyEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_TIMEOUT)
    {
        if (!InterlockedExchange(&Pmp->ReadyTimedOut, 1))
        {
            DbgPrint("applepmp: device %u power %u refused, the processor is not set up after %lu ms (%lu requests, "
                     "%lu registers, value buffer %s); later votes are refused at once\n", Device, Enable,
                     PMP_READY_TIMEOUT_MS, Pmp->Requests, Pmp->IoregCount, Pmp->ValueBufferSet ? "given" : "not given");
        }
        return STATUS_DEVICE_NOT_READY;
    }
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
    if (Pmp->Thread)
    {
        InterlockedExchange(&Pmp->Stop, 1);
        KeSetEvent(&Pmp->StopEvent, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(Pmp->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Pmp->Thread);
        Pmp->Thread = NULL;
    }
    AppleRtkitDisconnectInterrupt(&Pmp->Rtkit);
    if (!Pmp->Coproc ||
        !(READ_REGISTER_ULONG((volatile ULONG *)(Pmp->Coproc + PMP_CPU_CONTROL)) & PMP_CPU_CONTROL_RUN))
    {
        PmpFreeShared(Pmp);
    }
    if (Pmp->DartAdopted)
        AppleDartAdoptedRelease(&Pmp->Dart);
    Pmp->DartAdopted = FALSE;
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
PmpAdoptDart(
    _Inout_ PAPPLE_PMP Pmp)
{
    const VOID *Iommus;
    ULONG64 Phandle, Sid;
    ULONG Length;
    NTSTATUS Status;

    Iommus = FdtGetProperty(&Pmp->Tree.Fdt, Pmp->Tree.Node, "iommus", &Length);
    if (!Iommus)
    {
        DbgPrint("applepmp: the tree gives the processor no IOMMU, its buffers are given by physical address\n");
        return STATUS_SUCCESS;
    }
    if (!FdtReadCells(Iommus, Length, 0, 1, &Phandle) || !FdtReadCells(Iommus, Length, 1, 1, &Sid))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Status = AppleDartAdopt(&Pmp->Tree, FdtDriverFindPhandle(&Pmp->Tree, (ULONG)Phandle), (ULONG)Sid,
                            PMP_DART_WINDOW_BASE, PMP_DART_WINDOW_END, "applepmp", &Pmp->Dart);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applepmp: the processor's DART stream %I64u was not adopted: %lx\n", Sid, Status);
        return Status;
    }
    Pmp->DartAdopted = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
PmpPatchBootArguments(
    _Inout_ PAPPLE_PMP Pmp)
{
    static const struct
    {
        ULONG Key;
        PCSTR Property;
    } Arguments[] =
    {
        { APPLE_RTKIT_FOURCC('B', 'D', 'I', 'D'), "apple,board-id" },
        { APPLE_RTKIT_FOURCC('D', 'V', 'I', 'D'), "apple,dram-vendor-id" },
        { APPLE_RTKIT_FOURCC('D', 'C', 'A', 'P'), "apple,dram-capacity" },
        { APPLE_RTKIT_FOURCC('D', 'C', 'H', 'D'), "apple,dram-channel-disable" },
        { APPLE_RTKIT_FOURCC('P', 'M', 'C', '_'), "apple,pmc" },
        { APPLE_RTKIT_FOURCC('P', 'M', 'C', 'X'), "apple,pmc-msg-disabled" },
        { APPLE_RTKIT_FOURCC('C', 'V', 'A', 'R'), "apple,soc-chip-variant" },
    };
    APPLE_RTKIT_BOOT_ARGUMENT Patches[RTL_NUMBER_OF(Arguments) + 2];
    ULONG Index, Count = 0, Value;

    for (Index = 0; Index < RTL_NUMBER_OF(Arguments); ++Index)
    {
        if (!FdtReadU32(&Pmp->Tree.Fdt, Pmp->Tree.Node, Arguments[Index].Property, &Value))
            continue;
        Patches[Count].Key = Arguments[Index].Key;
        Patches[Count].Value = Value;
        Count++;
    }
    if (FdtReadU32(&Pmp->Tree.Fdt, Pmp->Tree.Node, "apple,pmc-pmgr", &Value))
    {
        Patches[Count].Key = APPLE_RTKIT_FOURCC('P', 'M', 'C', 'V');
        Patches[Count++].Value = Value & 1;
        Patches[Count].Key = APPLE_RTKIT_FOURCC('P', 'M', 'C', 'B');
        Patches[Count++].Value = (Value >> 3) & 1;
    }
    if (!Count)
    {
        DbgPrint("applepmp: the tree gives no boot arguments, the processor keeps its own\n");
        return;
    }
    AppleRtkitPatchBootArguments(Pmp->Sram, Pmp->SramSize, APPLE_PMP_BOOTARGS_OFFSET, APPLE_PMP_BOOTARGS_SIZE, Patches,
                                 Count, "applepmp");
}

static const PCSTR PmpDisplayDomains[] = { "disp_sys", "disp_fe", "disp_cpu" };
static const PCSTR PmpKeptDevices[] = { "DISP_SYS", "DISP_FE", "DISP_CPU", "ANS" };

static
NTSTATUS
PmpMapAdt(
    _In_ PAPPLE_PMP Pmp,
    _Out_ PVOID *Mapped,
    _Out_ PSIZE_T MappedSize,
    _Out_ PAPPLE_ADT Adt)
{
    ULONG Reserved, Node, Length;
    ULONG64 Base, Size;
    const VOID *Label, *Compatible;
    PHYSICAL_ADDRESS Address;

    *Mapped = NULL;
    *MappedSize = 0;
    if (!FdtFindNode(&Pmp->Tree.Fdt, "/reserved-memory", &Reserved, NULL))
        return STATUS_NOT_FOUND;
    for (Node = FdtFirstChild(&Pmp->Tree.Fdt, Reserved); Node != FDT_NO_NODE;
         Node = FdtNextSibling(&Pmp->Tree.Fdt, Node))
    {
        Compatible = FdtGetProperty(&Pmp->Tree.Fdt, Node, "compatible", &Length);
        if (!Compatible || !FdtStringListContains(Compatible, Length, "phram"))
            continue;
        Label = FdtGetProperty(&Pmp->Tree.Fdt, Node, "label", &Length);
        if (!Label || Length != sizeof("adt") || !RtlEqualMemory(Label, "adt", sizeof("adt")))
            continue;
        if (!FdtReadReg(&Pmp->Tree.Fdt, Node, Reserved, 0, &Base, &Size) || !Size || Size > PMP_ADT_MAX_LENGTH)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Address.QuadPart = (LONGLONG)Base;
        *Mapped = MmMapIoSpace(Address, (SIZE_T)Size, MmCached);
        if (!*Mapped)
            return STATUS_INSUFFICIENT_RESOURCES;
        *MappedSize = (SIZE_T)Size;
        if (!AppleAdtOpen(Adt, *Mapped, (ULONG)Size))
        {
            MmUnmapIoSpace(*Mapped, *MappedSize);
            *Mapped = NULL;
            *MappedSize = 0;
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

/* The processor keeps a block it manages powered only if the block's request is set when it starts. */
static
NTSTATUS
PmpKeepBlocksPowered(
    _Inout_ PAPPLE_PMP Pmp)
{
    ULONG Index, Item, Bit, Floors[RTL_NUMBER_OF(PmpDisplayDomains)];
    ULONG64 Table, Requested = 0, Before, After;
    volatile ULONG64 *Read, *Update;
    PHYSICAL_ADDRESS Address;
    SIZE_T MappedSize;
    PVOID Mapped;
    APPLE_ADT Adt;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Status = PmpMapAdt(Pmp, &Mapped, &MappedSize, &Adt);
    if (NT_SUCCESS(Status))
    {
        Status = AppleAdtPmpRequestWord(&Adt, &Table, &Index);
        for (Item = 0; NT_SUCCESS(Status) && Item < RTL_NUMBER_OF(PmpKeptDevices); ++Item)
        {
            if (!NT_SUCCESS(AppleAdtPmgrPmpBit(&Adt, PmpKeptDevices[Item], &Bit)) ||
                Bit == APPLE_PMP_NO_REQUEST_BIT)
            {
                continue;
            }
            if (Bit >= PMP_REQUEST_WORD_BITS)
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }
            Requested |= 1ULL << Bit;
        }
        MmUnmapIoSpace(Mapped, MappedSize);
    }
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applepmp: the power requests of the blocks in use cannot be found in the machine's tree (%lx)\n",
                 Status);
        return Status;
    }

    for (Item = 0; Item < RTL_NUMBER_OF(PmpDisplayDomains); ++Item)
    {
        Status = ApplePmgrRaiseMinimumState(&Pmp->Tree, PmpDisplayDomains[Item], APPLE_PMGR_PS_ACTIVE, &Floors[Item]);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("applepmp: the floor of power domain %s was not raised: %lx\n", PmpDisplayDomains[Item], Status);
            return Status;
        }
    }

    Address.QuadPart = (LONGLONG)(Table + (ULONG64)Index * APPLE_PMP_PTD_READ_STRIDE);
    Read = MmMapIoSpace(Address, sizeof(ULONG64), MmNonCached);
    Address.QuadPart = (LONGLONG)(Table + APPLE_PMP_PTD_UPDATE_OFFSET + (ULONG64)Index * APPLE_PMP_PTD_UPDATE_STRIDE);
    Update = MmMapIoSpace(Address, sizeof(ULONG64), MmNonCached);
    if (!Read || !Update)
    {
        if (Read)
            MmUnmapIoSpace((PVOID)Read, sizeof(ULONG64));
        if (Update)
            MmUnmapIoSpace((PVOID)Update, sizeof(ULONG64));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Before = READ_REGISTER_ULONG64((PULONG64)Read);
    if (Requested & ~Before)
        WRITE_REGISTER_ULONG64((PULONG64)Update, Before | Requested);
    KeMemoryBarrier();
    After = READ_REGISTER_ULONG64((PULONG64)Read);
    MmUnmapIoSpace((PVOID)Read, sizeof(ULONG64));
    MmUnmapIoSpace((PVOID)Update, sizeof(ULONG64));
    DbgPrint("applepmp: power requests at %I64x word %lu: %I64x, %I64x kept, now %I64x; display floors %lu %lu %lu raised "
             "to %u\n", Table, Index, Before, Requested, After, Floors[0], Floors[1], Floors[2], APPLE_PMGR_PS_ACTIVE);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
PmpWorker(
    _In_ PVOID Context)
{
    PAPPLE_PMP Pmp = Context;
    PVOID Objects[2] = { &Pmp->StopEvent, &Pmp->WorkEvent };
    LARGE_INTEGER Interval;
    ULONG WaitMs;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    while (!Pmp->Stop)
    {
        ExAcquireFastMutex(&Pmp->Lock);
        AppleRtkitPoll(&Pmp->Rtkit);
        ExReleaseFastMutex(&Pmp->Lock);
        WaitMs = AppleRtkitPollInterval(&Pmp->Rtkit, PMP_POLL_MS);
        Interval.QuadPart = -(LONGLONG)WaitMs * 10000;
        KeWaitForMultipleObjects(RTL_NUMBER_OF(Objects), Objects, WaitAny, Executive, KernelMode, FALSE,
                                 WaitMs == APPLE_RTKIT_NO_POLL ? NULL : &Interval, NULL);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NTSTATUS
PmpStart(
    _Inout_ PAPPLE_PMP Pmp,
    _In_opt_ PCM_RESOURCE_LIST Raw,
    _In_opt_ PCM_RESOURCE_LIST Translated)
{
    ULONG MailboxPhandle, MailboxNode, Control, Gsi;
    HANDLE ThreadHandle;
    BOOLEAN Edge;
    NTSTATUS Status;

    Pmp->ValueBufferSet = FALSE;
    Pmp->IoregConfirmed = FALSE;
    Pmp->IoregCount = 0;
    Pmp->Requests = 0;
    Pmp->Stop = 0;
    Pmp->ReadyTimedOut = 0;
    KeClearEvent(&Pmp->ReadyEvent);
    KeClearEvent(&Pmp->StopEvent);
    Status = FdtDriverOpen(Pmp->Pdo, &Pmp->Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    Pmp->TreeOpen = TRUE;
    if (!FdtReadU32(&Pmp->Tree.Fdt, Pmp->Tree.Node, "mboxes", &MailboxPhandle) ||
        (MailboxNode = FdtDriverFindPhandle(&Pmp->Tree, MailboxPhandle)) == FDT_NO_NODE)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Pmp->Coproc = FdtDriverMapReg(&Pmp->Tree, Pmp->Tree.Node, 0, NULL, &Pmp->CoprocSize);
    Pmp->Sram = FdtDriverMapReg(&Pmp->Tree, Pmp->Tree.Node, 1, &Pmp->SramBase, &Pmp->SramSize);
    Pmp->Mailbox = FdtDriverMapReg(&Pmp->Tree, MailboxNode, 0, NULL, &Pmp->MailboxSize);
    if (!Pmp->Coproc || !Pmp->Sram || !Pmp->Mailbox)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = PmpKeepBlocksPowered(Pmp);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PmpAdoptDart(Pmp);
    if (!NT_SUCCESS(Status))
        return Status;

    Pmp->Rtkit.Name = "applepmp";
    Pmp->Rtkit.Mailbox = Pmp->Mailbox;
    Pmp->Rtkit.Context = Pmp;
    Pmp->Rtkit.ShmemSetup = PmpShmemSetup;
    Pmp->Rtkit.Receive = PmpReceive;
    Status = AppleRtkitQueryInterrupt(&Pmp->Tree, Pmp->Tree.Node, &Gsi, &Edge);
    if (NT_SUCCESS(Status))
        AppleRtkitConnectInterrupt(&Pmp->Rtkit, Raw, Translated, Gsi, &Pmp->WorkEvent);
    else
        DbgPrint("applepmp: the mailbox declares no receive interrupt (%lx), polling\n", Status);
    /* The boot loader leaves this processor halted with its firmware in SRAM; it is started here. */
    Control = READ_REGISTER_ULONG((volatile ULONG *)(Pmp->Coproc + PMP_CPU_CONTROL));
    if (Control & PMP_CPU_CONTROL_RUN)
    {
        Status = AppleRtkitWake(&Pmp->Rtkit, PMP_BOOT_TIMEOUT_MS);
    }
    else
    {
        PmpPatchBootArguments(Pmp);
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
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, PmpWorker, Pmp);
    if (!NT_SUCCESS(Status))
        return Status;
    ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID *)&Pmp->Thread, NULL);
    ZwClose(ThreadHandle);
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
                Status = PmpStart(Pmp, Stack->Parameters.StartDevice.AllocatedResources,
                                  Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("applepmp: the power management processor did not start: %lx\n", Status);
                    PmpStop(Pmp);
                }
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            Status = IoForwardIrpSynchronously(Pmp->Lower, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status) || Status == STATUS_NOT_SUPPORTED)
            {
                FDT_DRIVER_TREE Tree;
                ULONG Gsi = 0;
                BOOLEAN Edge = FALSE;
                NTSTATUS FilterStatus = FdtDriverOpen(Pmp->Pdo, &Tree);

                if (NT_SUCCESS(FilterStatus))
                {
                    FilterStatus = AppleRtkitQueryInterrupt(&Tree, Tree.Node, &Gsi, &Edge);
                    FdtDriverClose(&Tree);
                }
                if (NT_SUCCESS(FilterStatus))
                    FilterStatus = AppleAddInterruptRequirement(Irp, Gsi, Edge);
                DbgPrint("applepmp: requesting the mailbox interrupt %lu: %lx\n", Gsi, FilterStatus);
                if (NT_SUCCESS(FilterStatus))
                    Status = STATUS_SUCCESS;
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
    KeInitializeEvent(&Pmp->WorkEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Pmp->StopEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Pmp->ReadyEvent, NotificationEvent, FALSE);
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
