/*
 * PROJECT:     LiberNT Apple Display Coprocessor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RTKit, EPIC transport and iboot display service of the Apple silicon display coprocessor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "appledcp.h"

#define DCP_ASC_CPU_CONTROL         0x44
#define DCP_ASC_CPU_CONTROL_RUN     0x10
#define DCP_WAKE_TIMEOUT_MS         2000
#define DCP_REPLY_TIMEOUT_MS        2000
#define DCP_POLL_MS                 5
/* Device addresses the firmware uses are the low bits only; the 1 TiB prefix of vm-base is a tag. */
#define DCP_DART_WINDOW_BASE        0x10000000ULL
#define DCP_DART_WINDOW_END         0x20000000ULL
#define DCP_SURFACE_WINDOW_BASE     0x4000ULL
#define DCP_SURFACE_WINDOW_END      0x10000000ULL
#define DCP_IBOOT_ENDPOINT          0x23
#define DCP_IBOOT_BUFFER_SIZE       0x4000

#define EPIC_MESSAGE_TYPE_SHIFT     48
#define EPIC_INIT                   0x80
#define EPIC_INIT_ACK               0xa0
#define EPIC_GETBUF                 0x89
#define EPIC_GETBUF_ACK             0xa1
#define EPIC_INIT_TX                0x8a
#define EPIC_INIT_RX                0x8b
#define EPIC_START                  0xa3
#define EPIC_START_ACK              0x86
#define EPIC_SEND                   0xa2
#define EPIC_RECV                   0x85
#define EPIC_SHUTDOWN_ACK           0xc1
#define EPIC_BLOCK_SHIFT            6
#define EPIC_BLOCK_SIZE             (1UL << EPIC_BLOCK_SHIFT)
#define EPIC_QUEUE_MAGIC            0x20504f49
#define EPIC_RING_HEADER_SIZE       0xc0
#define EPIC_RING_READ              0x40
#define EPIC_RING_WRITE             0x80
#define EPIC_TYPE_NOTIFY            0
#define EPIC_TYPE_COMMAND           3
#define EPIC_TYPE_REPLY             4
#define EPIC_CATEGORY_REPORT        0x00
#define EPIC_CATEGORY_NOTIFY        0x10
#define EPIC_CATEGORY_REPLY         0x20
#define EPIC_CATEGORY_COMMAND       0x30
#define EPIC_SUBTYPE_ANNOUNCE       0x30
#define EPIC_SUBTYPE_STD_SERVICE    0xc0
#define EPIC_SERVICE_NAME_LENGTH    32

#define IBOOT_SET_SURFACE           1
#define IBOOT_SET_POWER             2
#define IBOOT_GET_HPD               3
#define IBOOT_GET_TIMING_MODES      4
#define IBOOT_GET_COLOR_MODES       5
#define IBOOT_SWAP_BEGIN            15
#define IBOOT_SWAP_SET_LAYER        16
#define IBOOT_SWAP_END              18
#define IBOOT_SWAP_END_SIZE         12

static const CHAR DcpDisplayService[] = "disp0-service";

#pragma pack(push, 1)
typedef struct _EPIC_QUEUE_ENTRY
{
    ULONG Magic;
    ULONG Size;
    ULONG Channel;
    ULONG Type;
} EPIC_QUEUE_ENTRY, *PEPIC_QUEUE_ENTRY;

typedef struct _EPIC_HEADER
{
    UCHAR Version;
    USHORT Sequence;
    UCHAR Padding;
    ULONG Unknown;
    ULONG64 Timestamp;
} EPIC_HEADER, *PEPIC_HEADER;

typedef struct _EPIC_SUB_HEADER
{
    ULONG Length;
    UCHAR Version;
    UCHAR Category;
    USHORT Type;
    ULONG64 Timestamp;
    USHORT Sequence;
    UCHAR Unknown;
    UCHAR Flags;
    ULONG InlineLength;
} EPIC_SUB_HEADER, *PEPIC_SUB_HEADER;

typedef struct _EPIC_COMMAND
{
    ULONG ReturnCode;
    ULONG64 RxBuffer;
    ULONG64 TxBuffer;
    ULONG RxLength;
    ULONG TxLength;
    UCHAR RxCookie;
    UCHAR TxCookie;
} EPIC_COMMAND, *PEPIC_COMMAND;

typedef struct _EPIC_COMMAND_MESSAGE
{
    EPIC_HEADER Header;
    EPIC_SUB_HEADER Sub;
    EPIC_COMMAND Command;
} EPIC_COMMAND_MESSAGE;

typedef struct _IBOOT_TX_HEADER
{
    ULONG Op;
    ULONG Length;
    ULONG Unknown1;
    ULONG Unknown2;
} IBOOT_TX_HEADER, *PIBOOT_TX_HEADER;

typedef struct _IBOOT_RX_HEADER
{
    ULONG Op;
    ULONG Length;
} IBOOT_RX_HEADER, *PIBOOT_RX_HEADER;

typedef struct _IBOOT_HPD
{
    UCHAR Hpd;
    UCHAR Padding[3];
    ULONG TimingCount;
    ULONG ColorCount;
} IBOOT_HPD;

typedef struct _IBOOT_SWAP_LAYER
{
    ULONG Unknown;
    ULONG LayerId;
    DCP_LAYER Layer;
    ULONG Unknown3;
    ULONG Unknown4;
    ULONG Source[4];
    ULONG Destination[4];
    ULONG Unknown2;
} IBOOT_SWAP_LAYER;
#pragma pack(pop)

C_ASSERT(sizeof(EPIC_QUEUE_ENTRY) == 16);
C_ASSERT(sizeof(EPIC_HEADER) == 16);
C_ASSERT(sizeof(EPIC_SUB_HEADER) == 24);
C_ASSERT(sizeof(EPIC_COMMAND) == 30);
C_ASSERT(sizeof(IBOOT_TX_HEADER) == 16);
C_ASSERT(sizeof(IBOOT_SWAP_LAYER) == 216);

static
VOID
DcpSleepMs(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Delay;

    Delay.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}

static
ULONG64
DcpNowMs(VOID)
{
    LARGE_INTEGER Frequency, Counter = KeQueryPerformanceCounter(&Frequency);

    return (ULONG64)Counter.QuadPart * 1000 / (ULONG64)Frequency.QuadPart;
}

static
ULONG
DcpRingRead(
    _In_ PUCHAR Header,
    _In_ ULONG Offset)
{
    KeMemoryBarrier();
    return *(volatile ULONG *)(Header + Offset);
}

static
VOID
DcpRingWrite(
    _In_ PUCHAR Header,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    KeMemoryBarrier();
    *(volatile ULONG *)(Header + Offset) = Value;
    KeMemoryBarrier();
}

static
PDCP_SHARED
DcpAllocateShared(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ SIZE_T Size)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PDCP_SHARED Shared;
    ULONG64 Physical;
    ULONG Skip;
    NTSTATUS Status;

    if (Dcp->SharedCount == DCP_MAX_SHARED || !Size)
        return NULL;
    Shared = &Dcp->Shared[Dcp->SharedCount];
    Size = (Size + DCP_DART_PAGE_SIZE - 1) & ~(SIZE_T)(DCP_DART_PAGE_SIZE - 1);
    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = 0;
    Shared->AllocationSize = Size + DCP_DART_PAGE_SIZE - PAGE_SIZE;
    Shared->Allocation = MmAllocateContiguousMemorySpecifyCache(Shared->AllocationSize, Low, High, Boundary,
                                                                MmNonCached);
    if (!Shared->Allocation)
        return NULL;
    Physical = (ULONG64)MmGetPhysicalAddress(Shared->Allocation).QuadPart;
    Skip = (ULONG)(((Physical + DCP_DART_PAGE_SIZE - 1) & ~(ULONG64)(DCP_DART_PAGE_SIZE - 1)) - Physical);
    Shared->Virtual = (PUCHAR)Shared->Allocation + Skip;
    Shared->Physical = Physical + Skip;
    Shared->Size = Size;
    RtlZeroMemory(Shared->Virtual, Size);
    Status = AppleDartAdoptedMap(&Dcp->DcpDart, Shared->Physical, Size, &Shared->Iova);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appledcp: a %Iu byte shared buffer could not be mapped: %lx\n", Size, Status);
        MmFreeContiguousMemorySpecifyCache(Shared->Allocation, Shared->AllocationSize, MmNonCached);
        RtlZeroMemory(Shared, sizeof(*Shared));
        return NULL;
    }
    Dcp->SharedCount++;
    return Shared;
}

static
BOOLEAN
NTAPI
DcpShmemSetup(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG Size,
    _Inout_ PULONG64 Iova,
    _Out_ PVOID *Virtual,
    _Out_ PBOOLEAN Preassigned)
{
    PAPPLE_DCP Dcp = Context;
    PHYSICAL_ADDRESS Address;
    PDCP_SHARED Shared;
    ULONG64 Physical;

    *Virtual = NULL;
    *Preassigned = FALSE;
    if (*Iova)
    {
        if (Dcp->SharedCount == DCP_MAX_SHARED || !AppleDartAdoptedTranslate(&Dcp->DcpDart, *Iova, &Physical))
        {
            DbgPrint("appledcp: endpoint %u buffer at device address %I64x is not mapped\n", Endpoint, *Iova);
            return FALSE;
        }
        Shared = &Dcp->Shared[Dcp->SharedCount];
        Address.QuadPart = (LONGLONG)Physical;
        Shared->Virtual = MmMapIoSpace(Address, Size, MmNonCached);
        if (!Shared->Virtual)
            return FALSE;
        Shared->Physical = Physical;
        Shared->Size = Size;
        Shared->Iova = *Iova;
        Dcp->SharedCount++;
        *Preassigned = TRUE;
        DbgPrint("appledcp: endpoint %u keeps its %lu byte buffer at %I64x (physical %I64x)\n", Endpoint, Size, *Iova,
                 Physical);
    }
    else
    {
        Shared = DcpAllocateShared(Dcp, Size);
        if (!Shared)
            return FALSE;
        *Iova = Shared->Iova;
    }
    *Virtual = Shared->Virtual;
    return TRUE;
}

static
VOID
DcpEpicRingInit(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ UCHAR Type,
    _In_ ULONG64 Message)
{
    ULONG Offset = (ULONG)((Message >> 32) & 0xffff) << EPIC_BLOCK_SHIFT;
    ULONG Size = (ULONG)((Message >> 16) & 0xffff) << EPIC_BLOCK_SHIFT;
    USHORT Tag = (USHORT)(Message & 0xffff);
    PDCP_RING Ring = (Type == EPIC_INIT_RX) ? &Dcp->Rx : &Dcp->Tx;
    ULONG Data;

    if (!Dcp->EpicBuffer || Tag != Dcp->EpicTag || Size <= EPIC_RING_HEADER_SIZE ||
        (SIZE_T)Offset + Size > Dcp->EpicBuffer->Size)
    {
        DbgPrint("appledcp: %s ring at %lx+%lx tag %x does not fit the shared buffer\n",
                 Type == EPIC_INIT_RX ? "receive" : "transmit", Offset, Size, Tag);
        Dcp->EpicFailed = TRUE;
        return;
    }
    Ring->Header = Dcp->EpicBuffer->Virtual + Offset;
    Data = DcpRingRead(Ring->Header, 0);
    if (Data + EPIC_RING_HEADER_SIZE != Size)
    {
        DbgPrint("appledcp: %s ring size %lx does not match its header %lx\n",
                 Type == EPIC_INIT_RX ? "receive" : "transmit", Size, Data);
        Dcp->EpicFailed = TRUE;
        return;
    }
    Ring->Data = Ring->Header + EPIC_RING_HEADER_SIZE;
    Ring->Size = Data;
    Ring->Ready = TRUE;
    if (Dcp->Rx.Ready && Dcp->Tx.Ready)
        AppleRtkitSend(&Dcp->Rtkit, DCP_IBOOT_ENDPOINT, (ULONG64)EPIC_START << EPIC_MESSAGE_TYPE_SHIFT);
}

static
VOID
NTAPI
DcpReceive(
    _In_ PVOID Context,
    _In_ UCHAR Endpoint,
    _In_ ULONG64 Message)
{
    PAPPLE_DCP Dcp = Context;
    UCHAR Type = (UCHAR)(Message >> EPIC_MESSAGE_TYPE_SHIFT);
    SIZE_T Size;

    if (Endpoint != DCP_IBOOT_ENDPOINT)
    {
        DbgPrint("appledcp: endpoint %#x sent %I64x\n", Endpoint, Message);
        return;
    }
    switch (Type)
    {
        case EPIC_INIT_ACK:
        case EPIC_RECV:
            break;

        case EPIC_GETBUF:
            Size = (SIZE_T)((Message >> 16) & 0xffff) << EPIC_BLOCK_SHIFT;
            Dcp->EpicTag = (USHORT)(Message & 0xffff);
            if (!Dcp->EpicBuffer)
                Dcp->EpicBuffer = DcpAllocateShared(Dcp, Size);
            if (!Dcp->EpicBuffer || Dcp->EpicBuffer->Size < Size)
            {
                DbgPrint("appledcp: no %Iu byte buffer for the display service\n", Size);
                Dcp->EpicFailed = TRUE;
                break;
            }
            DbgPrint("appledcp: display service buffer of %Iu bytes at %I64x, tag %x\n", Size, Dcp->EpicBuffer->Iova,
                     Dcp->EpicTag);
            AppleRtkitSend(&Dcp->Rtkit, Endpoint,
                           ((ULONG64)EPIC_GETBUF_ACK << EPIC_MESSAGE_TYPE_SHIFT) |
                               (Dcp->EpicBuffer->Iova & 0xffffffffffffULL));
            break;

        case EPIC_INIT_TX:
        case EPIC_INIT_RX:
            DcpEpicRingInit(Dcp, Type, Message);
            break;

        case EPIC_START_ACK:
            Dcp->EpicStarted = TRUE;
            break;

        case EPIC_SHUTDOWN_ACK:
            Dcp->EpicStarted = FALSE;
            break;

        default:
            DbgPrint("appledcp: display service message %I64x\n", Message);
            break;
    }
}

static
BOOLEAN
DcpEpicPeek(
    _Inout_ PAPPLE_DCP Dcp,
    _Out_ PEPIC_QUEUE_ENTRY *Entry)
{
    PDCP_RING Ring = &Dcp->Rx;
    ULONG Read, Write;
    PEPIC_QUEUE_ENTRY Header;

    *Entry = NULL;
    if (!Ring->Ready)
        return FALSE;
    Read = DcpRingRead(Ring->Header, EPIC_RING_READ);
    Write = DcpRingRead(Ring->Header, EPIC_RING_WRITE);
    if (Read == Write || Read >= Ring->Size)
        return FALSE;
    Header = (PEPIC_QUEUE_ENTRY)(Ring->Data + Read);
    if (Header->Magic == EPIC_QUEUE_MAGIC && (ULONG64)Read + sizeof(*Header) + Header->Size > Ring->Size)
    {
        Read = 0;
        Header = (PEPIC_QUEUE_ENTRY)Ring->Data;
        DcpRingWrite(Ring->Header, EPIC_RING_READ, 0);
    }
    if (Header->Magic != EPIC_QUEUE_MAGIC || (ULONG64)Read + sizeof(*Header) + Header->Size > Ring->Size)
    {
        DbgPrint("appledcp: bad display service entry at %lx (magic %08lx, size %lx)\n", Read, Header->Magic,
                 Header->Size);
        Dcp->EpicFailed = TRUE;
        return FALSE;
    }
    *Entry = Header;
    return TRUE;
}

static
VOID
DcpEpicAcknowledge(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ PEPIC_QUEUE_ENTRY Entry)
{
    PDCP_RING Ring = &Dcp->Rx;
    ULONG Read = (ULONG)((PUCHAR)Entry - Ring->Data);

    Read = (Read + sizeof(*Entry) + Entry->Size + EPIC_BLOCK_SIZE - 1) & ~(EPIC_BLOCK_SIZE - 1);
    if (Read >= Ring->Size)
        Read = 0;
    DcpRingWrite(Ring->Header, EPIC_RING_READ, Read);
}

static
NTSTATUS
DcpEpicSend(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ ULONG Type,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ ULONG Size)
{
    PDCP_RING Ring = &Dcp->Tx;
    PEPIC_QUEUE_ENTRY Entry;
    ULONG Read, Write, Advance;
    BOOLEAN FitsAbove;

    if (!Ring->Ready)
        return STATUS_DEVICE_NOT_READY;
    Read = DcpRingRead(Ring->Header, EPIC_RING_READ);
    Write = DcpRingRead(Ring->Header, EPIC_RING_WRITE);
    Advance = (sizeof(*Entry) + Size + EPIC_BLOCK_SIZE - 1) & ~(EPIC_BLOCK_SIZE - 1);
    if (Write >= Ring->Size || Read >= Ring->Size || Advance > Ring->Size)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    if (Write < Read && Advance >= Read - Write)
        return STATUS_DEVICE_BUSY;
    if (Write >= Read)
    {
        FitsAbove = Advance < Ring->Size - Write || (Advance == Ring->Size - Write && Read != 0);
        if (!FitsAbove && Advance >= Read)
            return STATUS_DEVICE_BUSY;
    }
    Entry = (PEPIC_QUEUE_ENTRY)(Ring->Data + Write);
    Entry->Magic = EPIC_QUEUE_MAGIC;
    Entry->Size = Size;
    Entry->Channel = Dcp->Channel;
    Entry->Type = Type;
    Write += sizeof(*Entry);
    if (Size > Ring->Size - Write)
    {
        RtlCopyMemory(Ring->Data, Entry, sizeof(*Entry));
        Entry = (PEPIC_QUEUE_ENTRY)Ring->Data;
        Write = sizeof(*Entry);
    }
    Write = (Write + Size + EPIC_BLOCK_SIZE - 1) & ~(EPIC_BLOCK_SIZE - 1);
    if (Write >= Ring->Size)
        Write = 0;
    RtlCopyMemory(Entry + 1, Data, Size);
    DcpRingWrite(Ring->Header, EPIC_RING_WRITE, Write);
    return AppleRtkitSend(&Dcp->Rtkit, DCP_IBOOT_ENDPOINT,
                          ((ULONG64)EPIC_SEND << EPIC_MESSAGE_TYPE_SHIFT) | Write) ? STATUS_SUCCESS
                                                                                   : STATUS_DEVICE_NOT_CONNECTED;
}

static
PEPIC_SUB_HEADER
DcpEpicSubHeader(
    _In_ PEPIC_QUEUE_ENTRY Entry)
{
    if (Entry->Size < sizeof(EPIC_HEADER) + sizeof(EPIC_SUB_HEADER))
        return NULL;
    return (PEPIC_SUB_HEADER)((PUCHAR)(Entry + 1) + sizeof(EPIC_HEADER));
}

static
BOOLEAN
DcpContains(
    _In_reads_bytes_(Length) const CHAR *Data,
    _In_ ULONG Length,
    _In_z_ const CHAR *Text)
{
    SIZE_T TextLength = strlen(Text);
    ULONG Index;

    for (Index = 0; Length >= TextLength && Index <= Length - TextLength; ++Index)
    {
        if (RtlCompareMemory(Data + Index, Text, TextLength) == TextLength)
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
DcpPollOnce(
    _Inout_ PAPPLE_DCP Dcp)
{
    BOOLEAN Failed;

    ExAcquireFastMutex(&Dcp->Lock);
    AppleRtkitPoll(&Dcp->Rtkit);
    Failed = Dcp->Rtkit.Failed || Dcp->EpicFailed;
    ExReleaseFastMutex(&Dcp->Lock);
    return !Failed;
}

static
VOID
NTAPI
DcpPollThread(
    _In_ PVOID Parameter)
{
    PAPPLE_DCP Dcp = Parameter;
    PVOID Objects[2] = { &Dcp->Stop, &Dcp->Wake };
    PEPIC_QUEUE_ENTRY Entry;
    PEPIC_SUB_HEADER Sub;
    LARGE_INTEGER Interval;
    BOOLEAN Failed;
    ULONG WaitMs;

    for (;;)
    {
        WaitMs = AppleRtkitPollInterval(&Dcp->Rtkit, DCP_POLL_MS);
        Interval.QuadPart = -(LONGLONG)WaitMs * 10000;
        if (KeWaitForMultipleObjects(RTL_NUMBER_OF(Objects), Objects, WaitAny, Executive, KernelMode, FALSE,
                                     WaitMs == APPLE_RTKIT_NO_POLL ? NULL : &Interval, NULL) == STATUS_WAIT_0)
        {
            break;
        }
        ExAcquireFastMutex(&Dcp->Lock);
        AppleRtkitPoll(&Dcp->Rtkit);
        while (Dcp->ServiceFound && !Dcp->CommandBusy && DcpEpicPeek(Dcp, &Entry))
        {
            Sub = DcpEpicSubHeader(Entry);
            DbgPrint("appledcp: display service sent type %lu category %x subtype %x on channel %lu\n", Entry->Type,
                     Sub ? Sub->Category : 0xff, Sub ? Sub->Type : 0xffff, Entry->Channel);
            DcpEpicAcknowledge(Dcp, Entry);
        }
        Failed = Dcp->Rtkit.Failed;
        ExReleaseFastMutex(&Dcp->Lock);
        if (Failed)
        {
            DbgPrint("appledcp: the display coprocessor stopped responding\n");
            break;
        }
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
DcpLogFirmware(
    _In_ PAPPLE_DCP Dcp)
{
    const CHAR *Version;
    ULONG Chosen, Length;

    if (!FdtFindNode(&Dcp->Tree.Fdt, "/chosen", &Chosen, NULL))
        return;
    Version = FdtGetProperty(&Dcp->Tree.Fdt, Chosen, "asahi,os-fw-version", &Length);
    if (Version && Length && Version[Length - 1] == '\0')
        DbgPrint("appledcp: firmware set %s\n", Version);
}

static
NTSTATUS
DcpAdopt(
    _Inout_ PAPPLE_DCP Dcp,
    _In_z_ PCSTR Property,
    _Out_ PAPPLE_DART_ADOPTED Adopted)
{
    ULONG64 Phandle, Sid;
    const VOID *Iommus;
    ULONG Length;

    Iommus = FdtGetProperty(&Dcp->Tree.Fdt, Dcp->Tree.Node, Property, &Length);
    if (!Iommus || !FdtReadCells(Iommus, Length, 0, 1, &Phandle) ||
        !FdtReadCells(Iommus, Length, 1, 1, &Sid))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return AppleDartAdopt(&Dcp->Tree, FdtDriverFindPhandle(&Dcp->Tree, (ULONG)Phandle), (ULONG)Sid,
                          DCP_DART_WINDOW_BASE, DCP_DART_WINDOW_END, "appledcp", Adopted);
}

VOID
DcpClose(
    _In_ PAPPLE_DCP Dcp)
{
    if (Dcp->Thread)
    {
        KeSetEvent(&Dcp->Stop, IO_NO_INCREMENT, FALSE);
        KeWaitForSingleObject(Dcp->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Dcp->Thread);
        Dcp->Thread = NULL;
    }
    AppleRtkitDisconnectInterrupt(&Dcp->Rtkit);
    /* The coprocessor keeps running and using its buffers, so they stay allocated. */
    AppleDartAdoptedRelease(&Dcp->DispDart);
    AppleDartAdoptedRelease(&Dcp->DcpDart);
    if (Dcp->Mailbox)
        MmUnmapIoSpace(Dcp->Mailbox, Dcp->MailboxSize);
    if (Dcp->Coproc)
        MmUnmapIoSpace(Dcp->Coproc, Dcp->CoprocSize);
    if (Dcp->TreeOpen)
        FdtDriverClose(&Dcp->Tree);
    ExFreePoolWithTag(Dcp, APPLE_DCP_TAG);
}

NTSTATUS
DcpOpen(
    _In_ PDEVICE_OBJECT Pdo,
    _In_opt_ PDCP_INTERRUPT_RESOURCES Interrupt,
    _Outptr_ PAPPLE_DCP *Out)
{
    ULONG MailboxPhandle, MailboxNode, Control;
    PAPPLE_DCP Dcp;
    HANDLE Thread;
    NTSTATUS Status;

    *Out = NULL;
    Dcp = ExAllocatePoolZero(NonPagedPool, sizeof(*Dcp), APPLE_DCP_TAG);
    if (!Dcp)
        return STATUS_INSUFFICIENT_RESOURCES;
    ExInitializeFastMutex(&Dcp->Lock);
    ExInitializeFastMutex(&Dcp->CommandLock);
    KeInitializeEvent(&Dcp->Stop, NotificationEvent, FALSE);
    KeInitializeEvent(&Dcp->Wake, SynchronizationEvent, FALSE);
    Status = FdtDriverOpen(Pdo, &Dcp->Tree);
    if (!NT_SUCCESS(Status))
        goto Fail;
    Dcp->TreeOpen = TRUE;
    DcpLogFirmware(Dcp);
    if (!FdtReadU32(&Dcp->Tree.Fdt, Dcp->Tree.Node, "mboxes", &MailboxPhandle) ||
        (MailboxNode = FdtDriverFindPhandle(&Dcp->Tree, MailboxPhandle)) == FDT_NO_NODE)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Fail;
    }
    Dcp->Coproc = FdtDriverMapReg(&Dcp->Tree, Dcp->Tree.Node, 0, NULL, &Dcp->CoprocSize);
    Dcp->Mailbox = FdtDriverMapReg(&Dcp->Tree, MailboxNode, 0, NULL, &Dcp->MailboxSize);
    if (!Dcp->Coproc || !Dcp->Mailbox)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }

    Control = READ_REGISTER_ULONG((volatile ULONG *)(Dcp->Coproc + DCP_ASC_CPU_CONTROL));
    DbgPrint("appledcp: display coprocessor control %08lx\n", Control);
    if (!(Control & DCP_ASC_CPU_CONTROL_RUN))
    {
        DbgPrint("appledcp: the display coprocessor is not running, leaving it alone\n");
        Status = STATUS_DEVICE_NOT_READY;
        goto Fail;
    }

    Status = DcpAdopt(Dcp, "iommus", &Dcp->DcpDart);
    if (NT_SUCCESS(Status))
        Status = DcpAdopt(Dcp, "apple,disp-iommus", &Dcp->DispDart);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appledcp: the DART streams the firmware set up cannot be used: %lx\n", Status);
        goto Fail;
    }
    DbgPrint("appledcp: DCP stream %lu (%lu levels, tables %I64x), display stream %lu (%lu levels, tables %I64x)\n",
             Dcp->DcpDart.Sid, Dcp->DcpDart.Levels, Dcp->DcpDart.TopPhysical, Dcp->DispDart.Sid,
             Dcp->DispDart.Levels, Dcp->DispDart.TopPhysical);

    Dcp->Rtkit.Name = "appledcp";
    Dcp->Rtkit.Mailbox = Dcp->Mailbox;
    Dcp->Rtkit.Context = Dcp;
    Dcp->Rtkit.ShmemSetup = DcpShmemSetup;
    Dcp->Rtkit.Receive = DcpReceive;
    if (Interrupt && Interrupt->Valid)
        AppleRtkitConnectInterrupt(&Dcp->Rtkit, &Interrupt->Raw, &Interrupt->Translated, Interrupt->Gsi, &Dcp->Wake);
    else
        DbgPrint("appledcp: no mailbox interrupt was assigned, polling\n");
    ExAcquireFastMutex(&Dcp->Lock);
    Status = AppleRtkitWake(&Dcp->Rtkit, DCP_WAKE_TIMEOUT_MS);
    ExReleaseFastMutex(&Dcp->Lock);
    DbgPrint("appledcp: wake %lx, RTKit %lu, endpoints %08lx %08lx, %lu buffers\n", Status, Dcp->Rtkit.Version,
             Dcp->Rtkit.Endpoints[0], Dcp->Rtkit.Endpoints[1], Dcp->SharedCount);
    if (!NT_SUCCESS(Status))
        goto Fail;

    Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, DcpPollThread, Dcp);
    if (!NT_SUCCESS(Status))
        goto Fail;
    Status = ObReferenceObjectByHandle(Thread, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Dcp->Thread, NULL);
    ZwClose(Thread);
    if (!NT_SUCCESS(Status))
    {
        KeSetEvent(&Dcp->Stop, IO_NO_INCREMENT, FALSE);
        Dcp->Thread = NULL;
        goto Fail;
    }
    *Out = Dcp;
    return STATUS_SUCCESS;

Fail:
    DcpClose(Dcp);
    return Status;
}

NTSTATUS
DcpStartDisplayService(
    _Inout_ PAPPLE_DCP Dcp)
{
    PEPIC_QUEUE_ENTRY Entry;
    PEPIC_SUB_HEADER Sub;
    const CHAR *Name;
    CHAR Printable[EPIC_SERVICE_NAME_LENGTH + 1];
    ULONG64 Deadline;
    ULONG Length;
    BOOLEAN Healthy = TRUE;

    if (!AppleRtkitHasEndpoint(&Dcp->Rtkit, DCP_IBOOT_ENDPOINT))
    {
        DbgPrint("appledcp: the display coprocessor has no display service endpoint\n");
        return STATUS_NOT_SUPPORTED;
    }
    ExAcquireFastMutex(&Dcp->Lock);
    Dcp->CommandBusy = TRUE;
    AppleRtkitStartEndpoint(&Dcp->Rtkit, DCP_IBOOT_ENDPOINT);
    AppleRtkitSend(&Dcp->Rtkit, DCP_IBOOT_ENDPOINT, (ULONG64)EPIC_INIT << EPIC_MESSAGE_TYPE_SHIFT);
    ExReleaseFastMutex(&Dcp->Lock);

    Deadline = DcpNowMs() + DCP_REPLY_TIMEOUT_MS;
    while (!Dcp->EpicStarted && Healthy && DcpNowMs() < Deadline)
    {
        Healthy = DcpPollOnce(Dcp);
        DcpSleepMs(1);
    }
    if (!Dcp->EpicStarted)
    {
        DbgPrint("appledcp: the display service did not start (rings %u/%u, %s)\n", Dcp->Tx.Ready, Dcp->Rx.Ready,
                 Healthy ? "timeout" : "failure");
        Dcp->CommandBusy = FALSE;
        return Healthy ? STATUS_IO_TIMEOUT : STATUS_DEVICE_PROTOCOL_ERROR;
    }

    Deadline = DcpNowMs() + DCP_REPLY_TIMEOUT_MS;
    while (!Dcp->ServiceFound && Healthy && DcpNowMs() < Deadline)
    {
        ExAcquireFastMutex(&Dcp->Lock);
        AppleRtkitPoll(&Dcp->Rtkit);
        while (!Dcp->ServiceFound && DcpEpicPeek(Dcp, &Entry))
        {
            Sub = DcpEpicSubHeader(Entry);
            if (Sub && (Entry->Type == EPIC_TYPE_NOTIFY || Entry->Type == EPIC_TYPE_REPLY) &&
                Sub->Category == EPIC_CATEGORY_REPORT && Sub->Type == EPIC_SUBTYPE_ANNOUNCE &&
                Entry->Size >= sizeof(EPIC_HEADER) + sizeof(EPIC_SUB_HEADER) + EPIC_SERVICE_NAME_LENGTH)
            {
                Name = (const CHAR *)(Sub + 1);
                Length = Entry->Size - sizeof(EPIC_HEADER) - sizeof(EPIC_SUB_HEADER);
                RtlCopyMemory(Printable, Name, EPIC_SERVICE_NAME_LENGTH);
                Printable[EPIC_SERVICE_NAME_LENGTH] = '\0';
                DbgPrint("appledcp: service '%s' announced on channel %lu (%lu bytes)\n", Printable, Entry->Channel,
                         Length);
                if (DcpContains(Name, Length, DcpDisplayService))
                {
                    Dcp->Channel = Entry->Channel;
                    Dcp->ServiceFound = TRUE;
                }
            }
            DcpEpicAcknowledge(Dcp, Entry);
        }
        Healthy = !Dcp->Rtkit.Failed && !Dcp->EpicFailed;
        ExReleaseFastMutex(&Dcp->Lock);
        if (!Dcp->ServiceFound)
            DcpSleepMs(1);
    }
    if (!Dcp->ServiceFound)
    {
        DbgPrint("appledcp: no %s was announced\n", DcpDisplayService);
        Dcp->CommandBusy = FALSE;
        return STATUS_NOT_FOUND;
    }

    ExAcquireFastMutex(&Dcp->Lock);
    Dcp->CommandTx = DcpAllocateShared(Dcp, DCP_IBOOT_BUFFER_SIZE);
    Dcp->CommandRx = DcpAllocateShared(Dcp, DCP_IBOOT_BUFFER_SIZE);
    Dcp->CommandBusy = FALSE;
    ExReleaseFastMutex(&Dcp->Lock);
    return Dcp->CommandTx && Dcp->CommandRx ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static
NTSTATUS
DcpCommandLocked(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ ULONG Op,
    _In_reads_bytes_opt_(InputSize) const VOID *Input,
    _In_ ULONG InputSize,
    _Out_writes_bytes_to_opt_(OutputCapacity, *OutputSize) PVOID Output,
    _In_ ULONG OutputCapacity,
    _Out_opt_ PULONG OutputSize)
{
    EPIC_COMMAND_MESSAGE Message;
    PIBOOT_TX_HEADER Request;
    PEPIC_QUEUE_ENTRY Entry;
    PEPIC_SUB_HEADER Sub;
    PEPIC_COMMAND Reply;
    ULONG ReturnCode = 0, ReplyLength = 0;
    BOOLEAN Done = FALSE, Healthy = TRUE;
    ULONG64 Deadline;
    NTSTATUS Status;

    if (OutputSize)
        *OutputSize = 0;
    if (!Dcp->ServiceFound || !Dcp->CommandTx || !Dcp->CommandRx ||
        InputSize > DCP_IBOOT_BUFFER_SIZE - sizeof(IBOOT_TX_HEADER))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    ExAcquireFastMutex(&Dcp->Lock);
    Dcp->CommandBusy = TRUE;
    Request = (PIBOOT_TX_HEADER)Dcp->CommandTx->Virtual;
    Request->Op = Op;
    Request->Length = sizeof(*Request) + InputSize;
    Request->Unknown1 = 0;
    Request->Unknown2 = 0;
    if (InputSize)
        RtlCopyMemory(Request + 1, Input, InputSize);
    RtlZeroMemory(&Message, sizeof(Message));
    Message.Header.Version = 2;
    Message.Header.Sequence = Dcp->EpicSequence++;
    Message.Sub.Length = sizeof(Message.Command);
    Message.Sub.Version = 4;
    Message.Sub.Category = EPIC_CATEGORY_COMMAND;
    Message.Sub.Type = EPIC_SUBTYPE_STD_SERVICE;
    Message.Command.TxBuffer = Dcp->CommandTx->Iova;
    Message.Command.TxLength = sizeof(*Request) + InputSize;
    Message.Command.RxBuffer = Dcp->CommandRx->Iova;
    Message.Command.RxLength = DCP_IBOOT_BUFFER_SIZE;
    Status = DcpEpicSend(Dcp, EPIC_TYPE_COMMAND, &Message, sizeof(Message));
    ExReleaseFastMutex(&Dcp->Lock);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appledcp: display command %lu not sent: %lx\n", Op, Status);
        Dcp->CommandBusy = FALSE;
        return Status;
    }

    Deadline = DcpNowMs() + DCP_REPLY_TIMEOUT_MS;
    while (!Done && Healthy && DcpNowMs() < Deadline)
    {
        ExAcquireFastMutex(&Dcp->Lock);
        AppleRtkitPoll(&Dcp->Rtkit);
        while (!Done && DcpEpicPeek(Dcp, &Entry))
        {
            Sub = DcpEpicSubHeader(Entry);
            if (Sub && (Entry->Type == EPIC_TYPE_REPLY || Entry->Type == EPIC_TYPE_NOTIFY) &&
                Sub->Category == EPIC_CATEGORY_REPLY && Sub->Type == EPIC_SUBTYPE_STD_SERVICE &&
                Entry->Size >= sizeof(EPIC_HEADER) + sizeof(EPIC_SUB_HEADER) + sizeof(EPIC_COMMAND))
            {
                Reply = (PEPIC_COMMAND)(Sub + 1);
                ReturnCode = Reply->ReturnCode;
                ReplyLength = min(Reply->RxLength, (ULONG)DCP_IBOOT_BUFFER_SIZE);
                Done = TRUE;
            }
            else
            {
                DbgPrint("appledcp: display service sent type %lu category %x subtype %x during command %lu\n",
                         Entry->Type, Sub ? Sub->Category : 0xff, Sub ? Sub->Type : 0xffff, Op);
            }
            DcpEpicAcknowledge(Dcp, Entry);
        }
        Healthy = !Dcp->Rtkit.Failed && !Dcp->EpicFailed;
        ExReleaseFastMutex(&Dcp->Lock);
        if (!Done)
            DcpSleepMs(1);
    }
    if (Done && !ReturnCode && Output && OutputCapacity)
    {
        RtlCopyMemory(Output, Dcp->CommandRx->Virtual, min(ReplyLength, OutputCapacity));
        if (OutputSize)
            *OutputSize = min(ReplyLength, OutputCapacity);
    }
    Dcp->CommandBusy = FALSE;
    if (!Done)
    {
        DbgPrint("appledcp: display command %lu got no reply (%s)\n", Op, Healthy ? "timeout" : "failure");
        return Healthy ? STATUS_IO_TIMEOUT : STATUS_DEVICE_PROTOCOL_ERROR;
    }
    if (ReturnCode)
    {
        DbgPrint("appledcp: display command %lu failed with %08lx\n", Op, ReturnCode);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
DcpCommand(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ ULONG Op,
    _In_reads_bytes_opt_(InputSize) const VOID *Input,
    _In_ ULONG InputSize,
    _Out_writes_bytes_to_opt_(OutputCapacity, *OutputSize) PVOID Output,
    _In_ ULONG OutputCapacity,
    _Out_opt_ PULONG OutputSize)
{
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ExAcquireFastMutex(&Dcp->CommandLock);
    Status = DcpCommandLocked(Dcp, Op, Input, InputSize, Output, OutputCapacity, OutputSize);
    ExReleaseFastMutex(&Dcp->CommandLock);
    return Status;
}

NTSTATUS
DcpGetHotPlug(
    _Inout_ PAPPLE_DCP Dcp,
    _Out_ PBOOLEAN Connected,
    _Out_ PULONG TimingModes,
    _Out_ PULONG ColorModes)
{
    UCHAR Reply[sizeof(IBOOT_RX_HEADER) + sizeof(IBOOT_HPD)];
    IBOOT_HPD Hpd;
    ULONG Length;
    NTSTATUS Status;

    *Connected = FALSE;
    *TimingModes = 0;
    *ColorModes = 0;
    Status = DcpCommand(Dcp, IBOOT_GET_HPD, NULL, 0, Reply, sizeof(Reply), &Length);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Length < sizeof(Reply))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    RtlCopyMemory(&Hpd, Reply + sizeof(IBOOT_RX_HEADER), sizeof(Hpd));
    *Connected = Hpd.Hpd != 0;
    *TimingModes = Hpd.TimingCount;
    *ColorModes = Hpd.ColorCount;
    return STATUS_SUCCESS;
}

static
NTSTATUS
DcpGetModes(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ ULONG Op,
    _Out_writes_bytes_(Capacity * 24) PVOID Modes,
    _In_ ULONG Capacity,
    _Out_ PULONG Count)
{
    PUCHAR Reply;
    ULONG Length, Reported;
    NTSTATUS Status;

    *Count = 0;
    Reply = ExAllocatePoolZero(NonPagedPool, DCP_IBOOT_BUFFER_SIZE, APPLE_DCP_TAG);
    if (!Reply)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = DcpCommand(Dcp, Op, NULL, 0, Reply, DCP_IBOOT_BUFFER_SIZE, &Length);
    if (NT_SUCCESS(Status) && Length < sizeof(IBOOT_RX_HEADER) + sizeof(ULONG))
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(&Reported, Reply + sizeof(IBOOT_RX_HEADER), sizeof(Reported));
        *Count = min(Reported, Capacity);
        *Count = min(*Count, (Length - (ULONG)sizeof(IBOOT_RX_HEADER) - (ULONG)sizeof(ULONG)) / 24);
        RtlCopyMemory(Modes, Reply + sizeof(IBOOT_RX_HEADER) + sizeof(ULONG), (SIZE_T)*Count * 24);
    }
    ExFreePoolWithTag(Reply, APPLE_DCP_TAG);
    return Status;
}

NTSTATUS
DcpGetTimingModes(
    _Inout_ PAPPLE_DCP Dcp,
    _Out_writes_to_(Capacity, *Count) PDCP_TIMING_MODE Modes,
    _In_ ULONG Capacity,
    _Out_ PULONG Count)
{
    return DcpGetModes(Dcp, IBOOT_GET_TIMING_MODES, Modes, Capacity, Count);
}

NTSTATUS
DcpGetColorModes(
    _Inout_ PAPPLE_DCP Dcp,
    _Out_writes_to_(Capacity, *Count) PDCP_COLOR_MODE Modes,
    _In_ ULONG Capacity,
    _Out_ PULONG Count)
{
    return DcpGetModes(Dcp, IBOOT_GET_COLOR_MODES, Modes, Capacity, Count);
}

NTSTATUS
DcpMapSurface(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ ULONG64 Physical,
    _In_ SIZE_T Size,
    _Out_ PULONG64 Iova)
{
    ULONG64 Span, Start = DCP_SURFACE_WINDOW_BASE, Page = 0;
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;

    *Iova = 0;
    if (Physical & (DCP_DART_PAGE_SIZE - 1))
        return STATUS_INVALID_PARAMETER;
    Span = (Size + DCP_DART_PAGE_SIZE - 1) & ~(ULONG64)(DCP_DART_PAGE_SIZE - 1);
    ExAcquireFastMutex(&Dcp->Lock);
    while (Start + Span <= DCP_SURFACE_WINDOW_END)
    {
        if (Page == Span)
        {
            Status = AppleDartAdoptedMapAt(&Dcp->DispDart, Start, Physical, (SIZE_T)Span);
            if (NT_SUCCESS(Status))
                Status = AppleDartAdoptedMapAt(&Dcp->DcpDart, Start, Physical, (SIZE_T)Span);
            if (NT_SUCCESS(Status))
                *Iova = Start;
            break;
        }
        if (!AppleDartAdoptedRangeFree(&Dcp->DcpDart, Start + Page, DCP_DART_PAGE_SIZE) ||
            !AppleDartAdoptedRangeFree(&Dcp->DispDart, Start + Page, DCP_DART_PAGE_SIZE))
        {
            Start += Page + DCP_DART_PAGE_SIZE;
            Page = 0;
            continue;
        }
        Page += DCP_DART_PAGE_SIZE;
    }
    ExReleaseFastMutex(&Dcp->Lock);
    DbgPrint("appledcp: surface %I64x+%I64x at device address %I64x: %lx\n", Physical, Span, *Iova, Status);
    return Status;
}

NTSTATUS
DcpSetSurface(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ const DCP_LAYER *Layer)
{
    return DcpCommand(Dcp, IBOOT_SET_SURFACE, Layer, sizeof(*Layer), NULL, 0, NULL);
}

NTSTATUS
DcpSwapLayer(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ const DCP_LAYER *Layer)
{
    UCHAR End[IBOOT_SWAP_END_SIZE] = { 0 };
    IBOOT_SWAP_LAYER Swap;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    RtlZeroMemory(&Swap, sizeof(Swap));
    Swap.Layer = *Layer;
    Swap.Source[0] = Swap.Destination[0] = Layer->Width;
    Swap.Source[1] = Swap.Destination[1] = Layer->Height;
    ExAcquireFastMutex(&Dcp->CommandLock);
    Status = DcpCommandLocked(Dcp, IBOOT_SWAP_BEGIN, NULL, 0, NULL, 0, NULL);
    if (NT_SUCCESS(Status))
        Status = DcpCommandLocked(Dcp, IBOOT_SWAP_SET_LAYER, &Swap, sizeof(Swap), NULL, 0, NULL);
    if (NT_SUCCESS(Status))
        Status = DcpCommandLocked(Dcp, IBOOT_SWAP_END, End, sizeof(End), NULL, 0, NULL);
    ExReleaseFastMutex(&Dcp->CommandLock);
    return Status;
}

NTSTATUS
DcpSetPower(
    _Inout_ PAPPLE_DCP Dcp,
    _In_ BOOLEAN On)
{
    UCHAR Power = On ? 1 : 0;

    return DcpCommand(Dcp, IBOOT_SET_POWER, &Power, sizeof(Power), NULL, 0, NULL);
}
