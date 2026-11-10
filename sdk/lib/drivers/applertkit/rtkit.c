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
RtkitReceive(
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

BOOLEAN
NTAPI
AppleRtkitPoll(
    _Inout_ PAPPLE_RTKIT Rtkit)
{
    UCHAR Endpoint;
    ULONG64 Message;
    ULONG Type;
    BOOLEAN Any = FALSE;

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
