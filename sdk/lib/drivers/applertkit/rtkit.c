/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mailbox and RTKit management protocol of Apple silicon coprocessors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applertkit.h>
#include <reactos/drivers/appleadmac.h>

#define MBOX_CONTROL_FULL       0x00010000
#define MBOX_CONTROL_EMPTY      0x00020000
#define MBOX_A2I_CONTROL        0x110
#define MBOX_A2I_SEND0          0x800
#define MBOX_A2I_SEND1          0x808
#define MBOX_I2A_CONTROL        0x114
#define MBOX_I2A_RECV0          0x830
#define MBOX_I2A_RECV1          0x838

#define MGMT_TYPE_SHIFT         52
#define MGMT_HELLO              0x01
#define MGMT_HELLO_REPLY        0x02
#define MGMT_STARTEP            0x05
#define MGMT_SET_IOP_PWR_STATE  0x06
#define MGMT_IOP_PWR_STATE_ACK  0x07
#define MGMT_EPMAP              0x08
#define MGMT_SET_AP_PWR_STATE   0x0b

#define RTKIT_MIN_VERSION       11
#define RTKIT_MAX_VERSION       12

#define RTKIT_MAX_INTERRUPT_CELLS   4

#define BUFFER_REQUEST          1
#define SYSLOG_LOG              5
#define SYSLOG_INIT             8

static
ULONG64
RtkitNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000;
}

static
VOID
RtkitSleepMs(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        KeStallExecutionProcessor(Milliseconds * 1000);
        return;
    }
    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
ULONG
RtkitControl(
    _In_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Rtkit->Mailbox + Offset));
}

BOOLEAN
NTAPI
AppleRtkitSend(
    _In_ PAPPLE_RTKIT Rtkit,
    _In_ UCHAR Endpoint,
    _In_ ULONG64 Message)
{
    ULONG64 Deadline = RtkitNowMs() + 200;

    while (RtkitControl(Rtkit, MBOX_A2I_CONTROL) & MBOX_CONTROL_FULL)
    {
        if (RtkitNowMs() > Deadline)
        {
            DbgPrint("%s: mailbox to the coprocessor stayed full\n", Rtkit->Name);
            return FALSE;
        }
        KeStallExecutionProcessor(10);
    }
    KeMemoryBarrier();
    *(volatile ULONG64 *)(Rtkit->Mailbox + MBOX_A2I_SEND0) = Message;
    *(volatile ULONG64 *)(Rtkit->Mailbox + MBOX_A2I_SEND1) = Endpoint;
    return TRUE;
}

static
BOOLEAN
RtkitReceiveHardware(
    _In_ PAPPLE_RTKIT Rtkit,
    _Out_ PUCHAR Endpoint,
    _Out_ PULONG64 Message)
{
    if (RtkitControl(Rtkit, MBOX_I2A_CONTROL) & MBOX_CONTROL_EMPTY)
        return FALSE;
    *Message = *(volatile ULONG64 *)(Rtkit->Mailbox + MBOX_I2A_RECV0);
    *Endpoint = (UCHAR)*(volatile ULONG64 *)(Rtkit->Mailbox + MBOX_I2A_RECV1);
    KeMemoryBarrier();
    return TRUE;
}

static
BOOLEAN
RtkitQueueTake(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _Out_ PUCHAR Endpoint,
    _Out_ PULONG64 Message)
{
    ULONG Slot;

    ASSERT(Rtkit->QueueTail - Rtkit->QueueHead <= APPLE_RTKIT_QUEUE_SLOTS);
    if (Rtkit->QueueHead == Rtkit->QueueTail)
        return FALSE;
    Slot = Rtkit->QueueHead % APPLE_RTKIT_QUEUE_SLOTS;
    *Endpoint = Rtkit->QueueEndpoint[Slot];
    *Message = Rtkit->QueueMessage[Slot];
    Rtkit->QueueHead++;
    return TRUE;
}

ULONG
NTAPI
AppleRtkitDrainMailbox(
    _Inout_ PAPPLE_RTKIT Rtkit)
{
    UCHAR Endpoint;
    ULONG64 Message;
    ULONG Count = 0, Slot;

    ASSERT(Rtkit->Mailbox);
    while (Count < APPLE_RTKIT_QUEUE_SLOTS && RtkitReceiveHardware(Rtkit, &Endpoint, &Message))
    {
        ++Count;
        ASSERT(Rtkit->QueueTail - Rtkit->QueueHead <= APPLE_RTKIT_QUEUE_SLOTS);
        if (Rtkit->QueueTail - Rtkit->QueueHead == APPLE_RTKIT_QUEUE_SLOTS)
        {
            Rtkit->QueueLost++;
            continue;
        }
        Slot = Rtkit->QueueTail % APPLE_RTKIT_QUEUE_SLOTS;
        Rtkit->QueueEndpoint[Slot] = Endpoint;
        Rtkit->QueueMessage[Slot] = Message;
        Rtkit->QueueTail++;
    }
    return Count;
}

typedef struct _RTKIT_TAKE
{
    PAPPLE_RTKIT Rtkit;
    PUCHAR Endpoint;
    PULONG64 Message;
} RTKIT_TAKE, *PRTKIT_TAKE;

static
BOOLEAN
NTAPI
RtkitTakeSynchronized(
    _In_opt_ PVOID Context)
{
    PRTKIT_TAKE Take = Context;

    ASSERT(Take && Take->Rtkit->Interrupt);
    if (RtkitQueueTake(Take->Rtkit, Take->Endpoint, Take->Message))
        return TRUE;
    return RtkitReceiveHardware(Take->Rtkit, Take->Endpoint, Take->Message);
}

static
BOOLEAN
RtkitReceive(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _Out_ PUCHAR Endpoint,
    _Out_ PULONG64 Message)
{
    RTKIT_TAKE Take;

    if (Rtkit->Interrupt)
    {
        Take.Rtkit = Rtkit;
        Take.Endpoint = Endpoint;
        Take.Message = Message;
        return KeSynchronizeExecution(Rtkit->Interrupt, RtkitTakeSynchronized, &Take);
    }
    if (RtkitQueueTake(Rtkit, Endpoint, Message))
        return TRUE;
    return RtkitReceiveHardware(Rtkit, Endpoint, Message);
}

static
VOID
RtkitManagementSend(
    _In_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG Type,
    _In_ ULONG64 Message)
{
    Message &= ~(0xffULL << MGMT_TYPE_SHIFT);
    Message |= (ULONG64)Type << MGMT_TYPE_SHIFT;
    AppleRtkitSend(Rtkit, RTKIT_EP_MGMT, Message);
}

BOOLEAN
NTAPI
AppleRtkitHasEndpoint(
    _In_ PAPPLE_RTKIT Rtkit,
    _In_ UCHAR Endpoint)
{
    return (Rtkit->Endpoints[Endpoint >> 5] & (1UL << (Endpoint & 31))) != 0;
}

VOID
NTAPI
AppleRtkitStartEndpoint(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ UCHAR Endpoint)
{
    RtkitManagementSend(Rtkit, MGMT_STARTEP, ((ULONG64)Endpoint << 32) | 2);
}

static
VOID
RtkitBufferRequest(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ UCHAR Endpoint,
    _In_ ULONG64 Message)
{
    ULONG Size;
    ULONG64 Iova, Reply;
    PVOID Virtual = NULL;
    BOOLEAN Preassigned = FALSE;

    if (Endpoint == RTKIT_EP_OSLOG)
    {
        Size = (ULONG)((Message >> 36) & 0xfffff);
        Iova = (Message & 0xfffffffffULL) << 12;
    }
    else
    {
        Size = (ULONG)(((Message >> 44) & 0xff) << 12);
        Iova = Message & 0xfffffffffffULL;
    }
    DbgPrint("%s: endpoint %u asks for a %lu byte buffer at %I64x\n", Rtkit->Name, Endpoint, Size, Iova);
    if (!Size || !Rtkit->ShmemSetup ||
        !Rtkit->ShmemSetup(Rtkit->Context, Endpoint, Size, &Iova, &Virtual, &Preassigned))
    {
        DbgPrint("%s: could not provide the buffer for endpoint %u\n", Rtkit->Name, Endpoint);
        return;
    }
    if (Endpoint == RTKIT_EP_SYSLOG)
    {
        Rtkit->SyslogBuffer = Virtual;
        Rtkit->SyslogBufferSize = Size;
    }
    if (Endpoint == RTKIT_EP_CRASHLOG)
    {
        Rtkit->CrashlogBuffer = Virtual;
        Rtkit->CrashlogBufferSize = Size;
    }
    if (Preassigned)
        return;
    if (Endpoint == RTKIT_EP_OSLOG)
        Reply = (1ULL << 56) | ((ULONG64)Size << 36) | (Iova >> 12);
    else
        Reply = ((ULONG64)BUFFER_REQUEST << 52) | ((ULONG64)(Size >> 12) << 44) | Iova;
    AppleRtkitSend(Rtkit, Endpoint, Reply);
}

static
VOID
RtkitSyslog(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG64 Message)
{
    ULONG Type = (ULONG)((Message >> 52) & 0xff);

    switch (Type)
    {
        case BUFFER_REQUEST:
            RtkitBufferRequest(Rtkit, RTKIT_EP_SYSLOG, Message);
            break;

        case SYSLOG_INIT:
            Rtkit->SyslogEntries = (ULONG)(Message & 0xff);
            Rtkit->SyslogMessageSize = (ULONG)((Message >> 24) & 0xff);
            break;

        case SYSLOG_LOG:
        {
            ULONG Index = (ULONG)(Message & 0xff);
            ULONG EntrySize = 0x20 + Rtkit->SyslogMessageSize;
            CHAR Context[24], Text[129];
            ULONG Length;

            if (Rtkit->SyslogBuffer && Rtkit->SyslogMessageSize && Rtkit->SyslogMessageSize < sizeof(Text) &&
                (Index + 1) * EntrySize <= Rtkit->SyslogBufferSize)
            {
                const UCHAR *Entry = (const UCHAR *)Rtkit->SyslogBuffer + Index * EntrySize;
                ULONG Byte;

                for (Byte = 0; Byte < sizeof(Context); ++Byte)
                    Context[Byte] = (CHAR)READ_REGISTER_UCHAR((volatile UCHAR *)(Entry + 8 + Byte));
                Context[sizeof(Context) - 1] = '\0';
                for (Byte = 0; Byte < Rtkit->SyslogMessageSize; ++Byte)
                    Text[Byte] = (CHAR)READ_REGISTER_UCHAR((volatile UCHAR *)(Entry + 8 + sizeof(Context) + Byte));
                Text[Rtkit->SyslogMessageSize] = '\0';
                for (Length = (ULONG)strlen(Text);
                     Length && (Text[Length - 1] == '\n' || Text[Length - 1] == '\r' || Text[Length - 1] == ' ');
                     --Length)
                    Text[Length - 1] = '\0';
                DbgPrint("%s: coprocessor says %s: %s\n", Rtkit->Name, Context, Text);
            }
            AppleRtkitSend(Rtkit, RTKIT_EP_SYSLOG, Message);
            break;
        }

        default:
            DbgPrint("%s: unknown syslog message %I64x\n", Rtkit->Name, Message);
            break;
    }
}

static
VOID
RtkitManagement(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG64 Message)
{
    ULONG Type = (ULONG)((Message >> MGMT_TYPE_SHIFT) & 0xff);

    switch (Type)
    {
        case MGMT_HELLO:
        {
            ULONG MinVersion = (ULONG)(Message & 0xffff);
            ULONG MaxVersion = (ULONG)((Message >> 16) & 0xffff);
            ULONG Version = min(MaxVersion, RTKIT_MAX_VERSION);

            DbgPrint("%s: coprocessor speaks RTKit %lu to %lu\n", Rtkit->Name, MinVersion, MaxVersion);
            if (MinVersion > RTKIT_MAX_VERSION || MaxVersion < RTKIT_MIN_VERSION)
            {
                Rtkit->Failed = TRUE;
                break;
            }
            Rtkit->Version = Version;
            RtkitManagementSend(Rtkit, MGMT_HELLO_REPLY, ((ULONG64)Version << 16) | Version);
            break;
        }

        case MGMT_EPMAP:
        {
            ULONG Bitmap = (ULONG)Message;
            ULONG Base = (ULONG)((Message >> 32) & 0x7);
            BOOLEAN Last = (Message & (1ULL << 51)) != 0;
            UCHAR Endpoint;

            Rtkit->Endpoints[Base] |= Bitmap;
            RtkitManagementSend(Rtkit, MGMT_EPMAP, ((ULONG64)Base << 32) | (Last ? (1ULL << 51) : 1));
            if (!Last)
                break;
            for (Endpoint = 1; Endpoint < RTKIT_EP_APP_START; ++Endpoint)
            {
                if (!AppleRtkitHasEndpoint(Rtkit, Endpoint))
                    continue;
                if (Endpoint == RTKIT_EP_SYSLOG || Endpoint == RTKIT_EP_CRASHLOG || Endpoint == RTKIT_EP_DEBUG ||
                    Endpoint == RTKIT_EP_IOREPORT || Endpoint == RTKIT_EP_OSLOG)
                    AppleRtkitStartEndpoint(Rtkit, Endpoint);
            }
            DbgPrint("%s: coprocessor endpoints %08lx %08lx\n", Rtkit->Name, Rtkit->Endpoints[0], Rtkit->Endpoints[1]);
            Rtkit->EpmapDone = TRUE;
            break;
        }

        case MGMT_IOP_PWR_STATE_ACK:
            Rtkit->IopState = (ULONG)(Message & 0xffff);
            Rtkit->IopAcked = TRUE;
            DbgPrint("%s: coprocessor power state %lx\n", Rtkit->Name, Rtkit->IopState);
            break;

        case MGMT_SET_AP_PWR_STATE:
            Rtkit->ApState = (ULONG)(Message & 0xffff);
            Rtkit->ApAcked = TRUE;
            DbgPrint("%s: host power state acknowledged as %lx\n", Rtkit->Name, Rtkit->ApState);
            break;

        default:
            DbgPrint("%s: unknown management message %I64x\n", Rtkit->Name, Message);
            break;
    }
}

static
VOID
RtkitCrashString(
    _Out_writes_z_(Capacity) PCHAR Out,
    _In_ ULONG Capacity,
    _In_reads_bytes_(Length) const UCHAR *Text,
    _In_ ULONG Length)
{
    ULONG Used = (ULONG)strlen(Out), Index;

    if (Used && Used + 3 < Capacity)
    {
        Out[Used++] = ' ';
        Out[Used++] = '|';
        Out[Used++] = ' ';
    }
    for (Index = 0; Index < Length && Text[Index] && Used + 1 < Capacity; ++Index)
        Out[Used++] = (Text[Index] >= 0x20 && Text[Index] < 0x7f) ? (CHAR)Text[Index] : '.';
    Out[Used] = '\0';
}

static
ULONG64
RtkitCrashQuad(
    _In_ const UCHAR *At)
{
    ULONG64 Value;

    RtlCopyMemory(&Value, At, sizeof(Value));
    return Value;
}

BOOLEAN
NTAPI
AppleRtkitParseCrashlog(
    _In_reads_bytes_(Size) const VOID *Log,
    _In_ ULONG Size,
    _Out_ PAPPLE_RTKIT_CRASH Crash)
{
    const UCHAR *Bytes = Log;
    ULONG Total, Offset, Length, Payload;

    RtlZeroMemory(Crash, sizeof(*Crash));
    if (!Log || Size < 0x20 || RtlCompareMemory(Bytes, "EHLC", 4) != 4)
        return FALSE;
    RtlCopyMemory(&Total, Bytes + 8, sizeof(Total));
    if (Total < 0x20 || Total > Size)
        Total = Size;
    for (Offset = 0x20; Offset + 0x10 <= Total; Offset += Length)
    {
        if (RtlCompareMemory(Bytes + Offset, "EHLC", 4) == 4)
            break;
        RtlCopyMemory(&Length, Bytes + Offset + 12, sizeof(Length));
        if (Length < 0x10 || Length > Total - Offset)
            break;
        Payload = Length - 0x10;
        if (RtlCompareMemory(Bytes + Offset, "revC", 4) == 4 && Payload > 0x10)
        {
            Crash->Version[0] = '\0';
            RtkitCrashString(Crash->Version, sizeof(Crash->Version), Bytes + Offset + 0x20, Payload - 0x10);
        }
        else if (RtlCompareMemory(Bytes + Offset, "rtsC", 4) == 4 && Payload > 4)
        {
            RtkitCrashString(Crash->Message, sizeof(Crash->Message), Bytes + Offset + 0x14, Payload - 4);
        }
        else if (RtlCompareMemory(Bytes + Offset, "8grC", 4) == 4 && Payload >= 0x348)
        {
            const UCHAR *Regs = Bytes + Offset + 0x10;

            Crash->Registers = TRUE;
            Crash->Lr = RtkitCrashQuad(Regs + 0xf8);
            Crash->Sp = RtkitCrashQuad(Regs + 0x100);
            Crash->Pc = RtkitCrashQuad(Regs + 0x108);
            Crash->Psr = RtkitCrashQuad(Regs + 0x110);
            Crash->Far = RtkitCrashQuad(Regs + 0x330);
            Crash->Esr = RtkitCrashQuad(Regs + 0x340);
        }
        else if (RtlCompareMemory(Bytes + Offset, "tscC", 4) == 4 && Payload > 8)
        {
            ULONG At;

            for (At = 8; At + 8 <= Payload && Crash->StackDepth < APPLE_RTKIT_CRASH_STACK; At += 8)
            {
                ULONG64 Frame = RtkitCrashQuad(Bytes + Offset + 0x10 + At);

                if (!Frame)
                    break;
                Crash->Stack[Crash->StackDepth++] = Frame;
            }
        }
    }
    return TRUE;
}

#define RTKIT_CRASHLOG_MAX          0x10000

static
VOID
RtkitReportCrash(
    _Inout_ PAPPLE_RTKIT Rtkit)
{
    APPLE_RTKIT_CRASH Crash;
    PUCHAR Copy;
    ULONG Size, Index;

    if (!Rtkit->CrashlogBuffer || !Rtkit->CrashlogBufferSize)
    {
        DbgPrint("%s: the coprocessor gave no crash log buffer\n", Rtkit->Name);
        return;
    }
    Size = min(Rtkit->CrashlogBufferSize, RTKIT_CRASHLOG_MAX);
    Copy = ExAllocatePoolZero(NonPagedPool, Size, 'cRpA');
    if (!Copy)
        return;
    for (Index = 0; Index < Size; ++Index)
        Copy[Index] = READ_REGISTER_UCHAR((volatile UCHAR *)Rtkit->CrashlogBuffer + Index);
    if (!AppleRtkitParseCrashlog(Copy, Size, &Crash))
    {
        DbgPrint("%s: the crash log is not readable (%02x %02x %02x %02x)\n", Rtkit->Name, Copy[0], Copy[1], Copy[2],
                 Copy[3]);
    }
    else
    {
        DbgPrint("%s: crash in firmware %s: %s\n", Rtkit->Name, Crash.Version[0] ? Crash.Version : "?",
                 Crash.Message[0] ? Crash.Message : "(no message)");
        if (Crash.Registers)
        {
            DbgPrint("%s: crash at pc %I64x lr %I64x sp %I64x psr %I64x far %I64x esr %I64x\n", Rtkit->Name,
                     Crash.Pc, Crash.Lr, Crash.Sp, Crash.Psr, Crash.Far, Crash.Esr);
        }
        for (Index = 0; Index < Crash.StackDepth; ++Index)
            DbgPrint("%s: crash stack %lu: %I64x\n", Rtkit->Name, Index, Crash.Stack[Index]);
    }
    ExFreePoolWithTag(Copy, 'cRpA');
}

BOOLEAN
NTAPI
AppleRtkitPoll(
    _Inout_ PAPPLE_RTKIT Rtkit)
{
    UCHAR Endpoint;
    ULONG64 Message;
    ULONG Type, Lost;
    BOOLEAN Any = FALSE;

    Lost = Rtkit->QueueLost;
    if (Lost != Rtkit->QueueLostReported)
    {
        DbgPrint("%s: %lu coprocessor messages were lost, the receive queue was full\n", Rtkit->Name,
                 Lost - Rtkit->QueueLostReported);
        Rtkit->QueueLostReported = Lost;
    }
    while (RtkitReceive(Rtkit, &Endpoint, &Message))
    {
        Any = TRUE;
        Type = (ULONG)((Message >> 52) & 0xff);
        switch (Endpoint)
        {
            case RTKIT_EP_MGMT:
                RtkitManagement(Rtkit, Message);
                break;

            case RTKIT_EP_CRASHLOG:
                if (!Rtkit->CrashlogSeen)
                {
                    Rtkit->CrashlogSeen = TRUE;
                    RtkitBufferRequest(Rtkit, Endpoint, Message);
                }
                else
                {
                    DbgPrint("%s: the coprocessor crashed (%I64x)\n", Rtkit->Name, Message);
                    RtkitReportCrash(Rtkit);
                    Rtkit->Failed = TRUE;
                }
                break;

            case RTKIT_EP_SYSLOG:
                RtkitSyslog(Rtkit, Message);
                break;

            case RTKIT_EP_IOREPORT:
                if (Type == BUFFER_REQUEST)
                    RtkitBufferRequest(Rtkit, Endpoint, Message);
                else if (Type == 0x8 || Type == 0xc)
                    AppleRtkitSend(Rtkit, Endpoint, Message);
                break;

            case RTKIT_EP_OSLOG:
                if (((Message >> 56) & 0xff) == BUFFER_REQUEST)
                    RtkitBufferRequest(Rtkit, Endpoint, Message);
                break;

            default:
                if (Endpoint >= RTKIT_EP_APP_START && Rtkit->Receive)
                    Rtkit->Receive(Rtkit->Context, Endpoint, Message);
                else
                    DbgPrint("%s: message %I64x on endpoint %u\n", Rtkit->Name, Message, Endpoint);
                break;
        }
    }
    return Any;
}

ULONG
NTAPI
AppleRtkitPollInterval(
    _In_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG PollMs)
{
    if (!Rtkit->Interrupt)
        return PollMs;
    if (!Rtkit->Interrupts)
        return max(PollMs, APPLE_RTKIT_FALLBACK_POLL_MS);
    return APPLE_RTKIT_NO_POLL;
}

NTSTATUS
NTAPI
AppleRtkitQueryInterrupt(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered)
{
    const VOID *Names, *Interrupts;
    const CHAR *Name, *Compatible;
    ULONG Phandle, Mailbox, Controller, Parent, CellCount, Cursor = 0, Index, Cell;
    ULONG NamesLength, NameLength, Length, CompatibleLength;
    ULONG Cells[RTKIT_MAX_INTERRUPT_CELLS];
    ULONG64 Value;

    *Gsi = 0;
    *EdgeTriggered = FALSE;
    if (!FdtReadU32(&Tree->Fdt, Node, "mboxes", &Phandle) ||
        (Mailbox = FdtDriverFindPhandle(Tree, Phandle)) == FDT_NO_NODE)
    {
        return STATUS_NOT_FOUND;
    }
    Names = FdtGetProperty(&Tree->Fdt, Mailbox, "interrupt-names", &NamesLength);
    for (Index = 0; (Name = FdtNextString(Names, NamesLength, &Cursor, &NameLength)) != NULL; ++Index)
    {
        if (NameLength == sizeof("recv-not-empty") - 1 && !memcmp(Name, "recv-not-empty", NameLength))
            break;
    }
    if (!Name)
        return STATUS_NOT_FOUND;
    for (Parent = Mailbox; !FdtReadU32(&Tree->Fdt, Parent, "interrupt-parent", &Phandle);)
    {
        Parent = FdtDriverFindParentNode(Tree, Parent);
        if (Parent == FDT_NO_NODE)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Controller = FdtDriverFindPhandle(Tree, Phandle);
    if (Controller == FDT_NO_NODE || !FdtReadU32(&Tree->Fdt, Controller, "#interrupt-cells", &CellCount) ||
        !CellCount || CellCount > RTKIT_MAX_INTERRUPT_CELLS)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Interrupts = FdtGetProperty(&Tree->Fdt, Mailbox, "interrupts", &Length);
    for (Cell = 0; Cell < CellCount; ++Cell)
    {
        if (!Interrupts || !FdtReadCells(Interrupts, Length, Index * CellCount + Cell, 1, &Value))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        Cells[Cell] = (ULONG)Value;
    }
    Compatible = FdtGetProperty(&Tree->Fdt, Controller, "compatible", &CompatibleLength);
    if (!Compatible ||
        !HalArm64TranslateDeviceTreeInterrupt(Compatible, CompatibleLength, Cells, CellCount, Gsi, EdgeTriggered))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtkitWaitFor(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ volatile BOOLEAN *Flag,
    _In_ ULONG TimeoutMs)
{
    ULONG64 Deadline = RtkitNowMs() + TimeoutMs;

    while (!*Flag && !Rtkit->Failed)
    {
        if (!AppleRtkitPoll(Rtkit))
        {
            if (RtkitNowMs() > Deadline)
                return STATUS_IO_TIMEOUT;
            RtkitSleepMs(1);
        }
    }
    return Rtkit->Failed ? STATUS_DEVICE_HARDWARE_ERROR : STATUS_SUCCESS;
}

static
NTSTATUS
RtkitStart(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG TimeoutMs,
    _In_ BOOLEAN Wake)
{
    NTSTATUS Status;

    DbgPrint("%s: mailbox %08lx/%08lx, %s the coprocessor\n", Rtkit->Name,
             RtkitControl(Rtkit, MBOX_A2I_CONTROL), RtkitControl(Rtkit, MBOX_I2A_CONTROL), Wake ? "waking" : "booting");
    Rtkit->IopAcked = FALSE;
    Rtkit->ApAcked = FALSE;
    if (Wake)
        RtkitManagementSend(Rtkit, MGMT_SET_IOP_PWR_STATE, RTKIT_PWR_STATE_INIT);
    Status = RtkitWaitFor(Rtkit, &Rtkit->EpmapDone, TimeoutMs);
    if (NT_SUCCESS(Status))
        Status = RtkitWaitFor(Rtkit, &Rtkit->IopAcked, TimeoutMs);
    if (Status == STATUS_IO_TIMEOUT)
    {
        DbgPrint("%s: coprocessor did not boot (endpoints %s, power %s)\n", Rtkit->Name,
                 Rtkit->EpmapDone ? "known" : "unknown", Rtkit->IopAcked ? "acknowledged" : "silent");
    }
    if (!NT_SUCCESS(Status))
        return Status;

    RtkitManagementSend(Rtkit, MGMT_SET_AP_PWR_STATE, RTKIT_PWR_STATE_ON);
    Status = RtkitWaitFor(Rtkit, &Rtkit->ApAcked, TimeoutMs);
    if (!NT_SUCCESS(Status) || (Rtkit->ApState & 0xff) != RTKIT_PWR_STATE_ON)
    {
        DbgPrint("%s: coprocessor did not accept the host power state (%lx)\n", Rtkit->Name, Status);
        return NT_SUCCESS(Status) ? STATUS_DEVICE_PROTOCOL_ERROR : Status;
    }
    DbgPrint("%s: coprocessor is running (RTKit %lu)\n", Rtkit->Name, Rtkit->Version);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleRtkitWake(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG TimeoutMs)
{
    return RtkitStart(Rtkit, TimeoutMs, TRUE);
}

NTSTATUS
NTAPI
AppleRtkitBoot(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG TimeoutMs)
{
    return RtkitStart(Rtkit, TimeoutMs, FALSE);
}

NTSTATUS
NTAPI
AppleRtkitSetPowerState(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_ ULONG ApState,
    _In_ ULONG IopState,
    _In_ ULONG TimeoutMs)
{
    NTSTATUS Status;

    Rtkit->ApAcked = FALSE;
    RtkitManagementSend(Rtkit, MGMT_SET_AP_PWR_STATE, ApState);
    Status = RtkitWaitFor(Rtkit, &Rtkit->ApAcked, TimeoutMs);
    if (!NT_SUCCESS(Status))
        return Status;
    Rtkit->IopAcked = FALSE;
    RtkitManagementSend(Rtkit, MGMT_SET_IOP_PWR_STATE, IopState);
    return RtkitWaitFor(Rtkit, &Rtkit->IopAcked, TimeoutMs);
}
