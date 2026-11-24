/*
 * PROJECT:     LiberNT LLTD
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LLTD responder: QoS sink and cross-traffic responder ([MS-LLTD] 3.7, 3.8)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ndis.h>
#include <libernt/lltd.h>

#define NDEBUG
#include <debug.h>

#define TAG_RSPNDR 'dpsR'

#define RSPNDR_MAX_SESSIONS         10
#define RSPNDR_BUCKETS              4
#define RSPNDR_HISTORY              30
#define RSPNDR_INACTIVITY_MS        30000
#define RSPNDR_SESSION_IDLE         (2LL * 60 * 10000000)
#define RSPNDR_LEASE                (5LL * 60 * 10000000)
#define RSPNDR_SECOND               (10000000LL)

typedef struct _RSPNDR_BUCKET
{
    USHORT Sequence;
    ULONG Count;
    ULONG64 Age;
    LLTD_QOS_EVENT Events[LLTD_MAX_TIMED_PROBES];
} RSPNDR_BUCKET, *PRSPNDR_BUCKET;

typedef struct _RSPNDR_SESSION
{
    LIST_ENTRY Link;
    UCHAR Controller[6];
    UCHAR InterruptMod;
    BOOLEAN Error;
    ULONG64 LastActive;
    ULONG64 NextAge;
    RSPNDR_BUCKET Buckets[RSPNDR_BUCKETS];
} RSPNDR_SESSION, *PRSPNDR_SESSION;

typedef struct _RSPNDR_COUNTERS
{
    ULONG64 BytesReceived;
    ULONG64 PacketsReceived;
    ULONG64 BytesSent;
    ULONG64 PacketsSent;
} RSPNDR_COUNTERS, *PRSPNDR_COUNTERS;

typedef struct _RSPNDR_ADAPTER
{
    LIST_ENTRY Link;
    NDIS_HANDLE BindingHandle;
    NDIS_HANDLE NblPool;
    UCHAR Mac[6];
    ULONG64 LinkSpeed;
    KSPIN_LOCK Lock;
    LIST_ENTRY Sessions;
    ULONG SessionCount;
    BOOLEAN Paused;
    BOOLEAN Unbinding;
    LONG PendingSends;
    KEVENT SendsDone;
    KEVENT AsyncEvent;
    NDIS_STATUS AsyncStatus;
    KEVENT RequestDone;
    NDIS_STATUS RequestStatus;
    KTIMER InactivityTimer;
    KDPC InactivityDpc;
    BOOLEAN InactivityRunning;
    KTIMER SnapshotTimer;
    KDPC SnapshotDpc;
    BOOLEAN SnapshotRunning;
    ULONG64 LeaseExpiry;
    PIO_WORKITEM SampleWorkItem;
    BOOLEAN SampleQueued;
    KEVENT SampleIdle;
    BOOLEAN StatisticsSupported;
    RSPNDR_COUNTERS Received;
    RSPNDR_COUNTERS History[RSPNDR_HISTORY + 1];
    ULONG HistorySamples;
    ULONG HistoryNext;
    ULONG64 LastSampleTime;
} RSPNDR_ADAPTER, *PRSPNDR_ADAPTER;

typedef struct _RSPNDR_SEND
{
    PRSPNDR_ADAPTER Adapter;
    PMDL Mdl;
    ULONG Length;
    UCHAR Frame[ANYSIZE_ARRAY];
} RSPNDR_SEND, *PRSPNDR_SEND;

static NDIS_HANDLE RspndrProtocolHandle;
static PDEVICE_OBJECT RspndrDeviceObject;
static LIST_ENTRY RspndrAdapters;
static KSPIN_LOCK RspndrAdaptersLock;

static
ULONG64
RspndrNow(VOID)
{
    return KeQueryInterruptTime();
}

static
VOID
RspndrAssertLocked(
    _In_ PRSPNDR_ADAPTER Adapter)
{
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(KeTestSpinLock(&Adapter->Lock) == FALSE);
    UNREFERENCED_PARAMETER(Adapter);
}

static
BOOLEAN
RspndrIsUnicast(
    _In_reads_(6) const UCHAR *Mac)
{
    return (Mac[0] & 1) == 0;
}

static
PRSPNDR_SESSION
RspndrFindSessionLocked(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_reads_(6) const UCHAR *Controller)
{
    PLIST_ENTRY Entry;

    RspndrAssertLocked(Adapter);
    for (Entry = Adapter->Sessions.Flink; Entry != &Adapter->Sessions; Entry = Entry->Flink)
    {
        PRSPNDR_SESSION Session = CONTAINING_RECORD(Entry, RSPNDR_SESSION, Link);

        if (RtlEqualMemory(Session->Controller, Controller, 6))
            return Session;
    }
    return NULL;
}

static
VOID
RspndrDeleteSessionLocked(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_ PRSPNDR_SESSION Session)
{
    RspndrAssertLocked(Adapter);
    ASSERT(Adapter->SessionCount != 0);

    RemoveEntryList(&Session->Link);
    Adapter->SessionCount--;
    ASSERT((Adapter->SessionCount == 0) == IsListEmpty(&Adapter->Sessions));
    ExFreePoolWithTag(Session, TAG_RSPNDR);
    if (Adapter->SessionCount == 0 && Adapter->InactivityRunning)
    {
        KeCancelTimer(&Adapter->InactivityTimer);
        Adapter->InactivityRunning = FALSE;
    }
}

/* Allocates a frame addressed back to the sender of Request, with its LLTD header filled in */
static
PRSPNDR_SEND
RspndrAllocateReply(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_ const LLTD_QOS_HEADER *Request,
    _In_ UCHAR Function,
    _In_ ULONG BodyLength)
{
    ULONG Length = max(sizeof(LLTD_QOS_HEADER) + BodyLength, LLTD_MIN_FRAME);
    PLLTD_QOS_HEADER Header;
    PRSPNDR_SEND Send;

    ASSERT(Length <= LLTD_MAX_FRAME);
    Send = ExAllocatePoolZero(NonPagedPoolNx, FIELD_OFFSET(RSPNDR_SEND, Frame) + Length, TAG_RSPNDR);
    if (!Send)
        return NULL;
    Send->Adapter = Adapter;
    Send->Length = Length;
    Header = (PLLTD_QOS_HEADER)Send->Frame;
    RtlCopyMemory(Header->Destination, Request->Source, 6);
    RtlCopyMemory(Header->Source, Adapter->Mac, 6);
    Header->EtherType = RtlUshortByteSwap(LLTD_ETHERTYPE);
    Header->Version = LLTD_VERSION;
    Header->TypeOfService = LLTD_TOS_QOS_DIAGNOSTICS;
    Header->Function = Function;
    RtlCopyMemory(Header->RealDestination, Request->RealSource, 6);
    RtlCopyMemory(Header->RealSource, Adapter->Mac, 6);
    Header->SequenceNumber = Request->SequenceNumber;
    return Send;
}

static
VOID
RspndrFreeSend(
    _In_ PRSPNDR_SEND Send)
{
    if (Send->Mdl)
        NdisFreeMdl(Send->Mdl);
    ExFreePoolWithTag(Send, TAG_RSPNDR);
}

/* Sends Send; when SinkTransmit is not NULL it receives the time stamp sampled just before the send */
static
VOID
RspndrSend(
    _In_ PRSPNDR_SEND Send,
    _In_ UCHAR UserPriority,
    _Out_opt_ PULONG64 SinkTransmit)
{
    PRSPNDR_ADAPTER Adapter = Send->Adapter;
    PNET_BUFFER_LIST Nbl;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    if (Adapter->Paused || Adapter->Unbinding)
    {
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        RspndrFreeSend(Send);
        return;
    }
    Adapter->PendingSends++;
    KeClearEvent(&Adapter->SendsDone);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    Send->Mdl = NdisAllocateMdl(Adapter->BindingHandle, Send->Frame, Send->Length);
    Nbl = Send->Mdl ? NdisAllocateNetBufferAndNetBufferList(Adapter->NblPool, 0, 0, Send->Mdl, 0, Send->Length) : NULL;
    if (!Nbl)
    {
        RspndrFreeSend(Send);
        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
        ASSERT(Adapter->PendingSends > 0);
        if (--Adapter->PendingSends == 0)
            KeSetEvent(&Adapter->SendsDone, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        return;
    }
    Nbl->SourceHandle = Adapter->BindingHandle;
    Nbl->ProtocolReserved[0] = Send;
    if (UserPriority != 0)
    {
        NDIS_NET_BUFFER_LIST_8021Q_INFO Tag;

        Tag.Value = NULL;
        Tag.TagHeader.UserPriority = UserPriority;
        NET_BUFFER_LIST_INFO(Nbl, Ieee8021QNetBufferListInfo) = Tag.Value;
    }
    if (SinkTransmit)
        *SinkTransmit = RtlUlonglongByteSwap((ULONG64)KeQueryPerformanceCounter(NULL).QuadPart);
    NdisSendNetBufferLists(Adapter->BindingHandle, Nbl, NDIS_DEFAULT_PORT_NUMBER,
                           (KeGetCurrentIrql() == DISPATCH_LEVEL) ? NDIS_SEND_FLAGS_DISPATCH_LEVEL : 0);
}

static
VOID
NTAPI
RspndrSendComplete(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG SendCompleteFlags)
{
    PRSPNDR_ADAPTER Adapter = ProtocolBindingContext;
    PNET_BUFFER_LIST Nbl, Next;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(SendCompleteFlags);

    for (Nbl = NetBufferLists; Nbl; Nbl = Next)
    {
        PRSPNDR_SEND Send = Nbl->ProtocolReserved[0];

        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        ASSERT(Send && Send->Adapter == Adapter);
        NdisFreeNetBufferList(Nbl);
        RspndrFreeSend(Send);

        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
        ASSERT(Adapter->PendingSends > 0);
        if (--Adapter->PendingSends == 0)
            KeSetEvent(&Adapter->SendsDone, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    }
}

static
VOID
RspndrSendError(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_ const LLTD_QOS_HEADER *Request,
    _In_ USHORT ErrorCode)
{
    PRSPNDR_SEND Send = RspndrAllocateReply(Adapter, Request, LLTD_QOS_ERROR, sizeof(LLTD_QOS_ERROR_BODY));

    if (!Send)
        return;
    ((PLLTD_QOS_ERROR_BODY)(Send->Frame + sizeof(LLTD_QOS_HEADER)))->ErrorCode = RtlUshortByteSwap(ErrorCode);
    RspndrSend(Send, 0, NULL);
}

static
VOID
RspndrSendReady(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_ const LLTD_QOS_HEADER *Request)
{
    PRSPNDR_SEND Send = RspndrAllocateReply(Adapter, Request, LLTD_QOS_READY, sizeof(LLTD_QOS_READY_BODY));
    PLLTD_QOS_READY_BODY Ready;
    LARGE_INTEGER Frequency;
    ULONG64 Speed;

    if (!Send)
        return;
    KeQueryPerformanceCounter(&Frequency);
    Speed = Adapter->LinkSpeed / 100;
    Ready = (PLLTD_QOS_READY_BODY)(Send->Frame + sizeof(LLTD_QOS_HEADER));
    Ready->SinkLinkSpeed = RtlUlongByteSwap((ULONG)min(Speed, (ULONG64)MAXULONG));
    Ready->PerformanceCounterFrequency = RtlUlonglongByteSwap((ULONG64)Frequency.QuadPart);
    RspndrSend(Send, 0, NULL);
}

static
VOID
RspndrInitializeSink(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_ const LLTD_QOS_HEADER *Header,
    _In_ ULONG Length)
{
    const LLTD_QOS_INITIALIZE *Initialize = (const LLTD_QOS_INITIALIZE *)(Header + 1);
    PRSPNDR_SESSION Session;
    USHORT Error = MAXUSHORT;

    if (Length < sizeof(*Header) + sizeof(*Initialize))
        return;

    KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
    Session = RspndrFindSessionLocked(Adapter, Header->RealSource);
    if (!Session && Initialize->InterruptMod != LLTD_INTERRUPT_MOD_EXISTING)
    {
        /* FIXME: the interrupt moderation of the interface cannot be disabled; such sessions are refused */
        Error = LLTD_ERROR_INTERRUPT_MOD;
    }
    else if (!Session && Adapter->SessionCount >= RSPNDR_MAX_SESSIONS)
    {
        Error = LLTD_ERROR_BUSY;
    }
    else if (!Session)
    {
        Session = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Session), TAG_RSPNDR);
        if (!Session)
        {
            Error = LLTD_ERROR_RESOURCES;
        }
        else
        {
            RtlCopyMemory(Session->Controller, Header->RealSource, 6);
            Session->InterruptMod = Initialize->InterruptMod;
            Session->LastActive = RspndrNow();
            InsertTailList(&Adapter->Sessions, &Session->Link);
            Adapter->SessionCount++;
            ASSERT(Adapter->SessionCount <= RSPNDR_MAX_SESSIONS);
            if (!Adapter->InactivityRunning)
            {
                LARGE_INTEGER Due;

                Due.QuadPart = -10000LL * RSPNDR_INACTIVITY_MS;
                KeSetTimerEx(&Adapter->InactivityTimer, Due, RSPNDR_INACTIVITY_MS, &Adapter->InactivityDpc);
                Adapter->InactivityRunning = TRUE;
            }
        }
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);

    if (Error != MAXUSHORT)
        RspndrSendError(Adapter, Header, Error);
    else
        RspndrSendReady(Adapter, Header);
}

static
PRSPNDR_BUCKET
RspndrBucketLocked(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_ PRSPNDR_SESSION Session,
    _In_ USHORT Sequence,
    _In_ BOOLEAN Create)
{
    PRSPNDR_BUCKET Oldest = NULL;
    ULONG Index;

    RspndrAssertLocked(Adapter);
    for (Index = 0; Index < RSPNDR_BUCKETS; Index++)
    {
        PRSPNDR_BUCKET Bucket = &Session->Buckets[Index];

        if (Bucket->Age != 0 && Bucket->Sequence == Sequence)
            return Bucket;
        if (!Oldest || Bucket->Age < Oldest->Age)
            Oldest = Bucket;
    }
    if (!Create)
        return NULL;
    ASSERT(Oldest != NULL);
    Oldest->Sequence = Sequence;
    Oldest->Count = 0;
    Oldest->Age = ++Session->NextAge;
    return Oldest;
}

static
VOID
RspndrProbe(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_ const LLTD_QOS_HEADER *Header,
    _In_ ULONG Length,
    _In_ ULONG64 Received)
{
    const LLTD_QOS_PROBE_BODY *Probe = (const LLTD_QOS_PROBE_BODY *)(Header + 1);
    PLLTD_QOS_PROBE_BODY Reply;
    PLLTD_QOS_HEADER ReplyHeader;
    PRSPNDR_SESSION Session;
    PRSPNDR_BUCKET Bucket;
    PRSPNDR_SEND Send;

    if (Length < sizeof(*Header) + sizeof(*Probe))
        return;

    KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
    Session = RspndrFindSessionLocked(Adapter, Header->RealSource);
    if (Session && Probe->TestType == LLTD_PROBE_TIMED)
    {
        Session->LastActive = RspndrNow();
        Bucket = RspndrBucketLocked(Adapter, Session, Header->SequenceNumber, TRUE);
        if (Bucket->Count < LLTD_MAX_TIMED_PROBES)
        {
            PLLTD_QOS_EVENT Event = &Bucket->Events[Bucket->Count++];

            Event->ControllerTransmitTimestamp = Probe->ControllerTransmitTimestamp;
            Event->SinkReceiveTimestamp = RtlUlonglongByteSwap(Received);
            Event->PacketId = Probe->PacketId;
            Event->Reserved = 0;
        }
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);

    if (!Session || Probe->TestType != LLTD_PROBE_PROBEGAP_CONTROLLER)
        return;

    Send = RspndrAllocateReply(Adapter, Header, LLTD_QOS_PROBE, Length - sizeof(*Header));
    if (!Send)
        return;
    ReplyHeader = (PLLTD_QOS_HEADER)Send->Frame;
    RtlCopyMemory(Send->Frame + sizeof(*Header), Header + 1, Length - sizeof(*Header));
    ASSERT(ReplyHeader->SequenceNumber == Header->SequenceNumber);
    Reply = (PLLTD_QOS_PROBE_BODY)(ReplyHeader + 1);
    Reply->SinkReceiveTimestamp = RtlUlonglongByteSwap(Received);
    Reply->TestType = LLTD_PROBE_PROBEGAP_SINK;
    /* FIXME: the tag priority reaches the wire only through miniports that insert 802.1Q tags */
    RspndrSend(Send, (Probe->TagPriority & LLTD_PROBE_TAG) ? (Probe->TagPriority & 7) : 0, &Reply->SinkTransmitTimestamp);
}

static
VOID
RspndrQuery(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_ const LLTD_QOS_HEADER *Header)
{
    PRSPNDR_SESSION Session;
    PRSPNDR_BUCKET Bucket = NULL;
    PRSPNDR_SEND Send;
    PUSHORT Count;
    USHORT Value;

    Send = RspndrAllocateReply(Adapter, Header, LLTD_QOS_QUERY_RESP,
                               sizeof(USHORT) + LLTD_MAX_TIMED_PROBES * sizeof(LLTD_QOS_EVENT));
    if (!Send)
        return;
    Count = (PUSHORT)(Send->Frame + sizeof(LLTD_QOS_HEADER));

    KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
    Session = RspndrFindSessionLocked(Adapter, Header->RealSource);
    if (Session)
    {
        Session->LastActive = RspndrNow();
        Bucket = RspndrBucketLocked(Adapter, Session, Header->SequenceNumber, FALSE);
    }
    if (Bucket)
    {
        ASSERT(Bucket->Count <= LLTD_MAX_TIMED_PROBES);
        Value = Session->Error ? LLTD_QUERY_RESP_ERROR : (USHORT)Bucket->Count;
        *Count = RtlUshortByteSwap(Value);
        if (!Session->Error)
            RtlCopyMemory(Count + 1, Bucket->Events, Bucket->Count * sizeof(LLTD_QOS_EVENT));
        Send->Length = max(sizeof(LLTD_QOS_HEADER) + sizeof(USHORT) + (Session->Error ? 0 : Bucket->Count) *
                           sizeof(LLTD_QOS_EVENT), LLTD_MIN_FRAME);
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);

    if (!Bucket)
    {
        RspndrFreeSend(Send);
        return;
    }
    RspndrSend(Send, 0, NULL);
}

static
VOID
RspndrReset(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_ const LLTD_QOS_HEADER *Header)
{
    PRSPNDR_SESSION Session;
    PRSPNDR_SEND Send;

    KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
    Session = RspndrFindSessionLocked(Adapter, Header->RealSource);
    if (Session)
        RspndrDeleteSessionLocked(Adapter, Session);
    KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);

    if (!Session)
        return;
    Send = RspndrAllocateReply(Adapter, Header, LLTD_QOS_ACK, 0);
    if (Send)
        RspndrSend(Send, 0, NULL);
}

static IO_WORKITEM_ROUTINE RspndrSampleWork;

static
VOID
RspndrCounterLease(
    _In_ PRSPNDR_ADAPTER Adapter)
{
    BOOLEAN Queue = FALSE;

    KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
    Adapter->LeaseExpiry = RspndrNow() + RSPNDR_LEASE;
    if (!Adapter->SnapshotRunning && !Adapter->Unbinding)
    {
        LARGE_INTEGER Due;

        Due.QuadPart = -RSPNDR_SECOND;
        Adapter->HistorySamples = 0;
        Adapter->HistoryNext = 0;
        KeSetTimerEx(&Adapter->SnapshotTimer, Due, 1000, &Adapter->SnapshotDpc);
        Adapter->SnapshotRunning = TRUE;
        if (!Adapter->SampleQueued)
        {
            Adapter->SampleQueued = TRUE;
            KeClearEvent(&Adapter->SampleIdle);
            Queue = TRUE;
        }
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);

    if (Queue)
        IoQueueWorkItem(Adapter->SampleWorkItem, RspndrSampleWork, DelayedWorkQueue, Adapter);
}

static
USHORT
RspndrScaled(
    _In_ ULONG64 Value,
    _In_ ULONG64 Unit)
{
    return (USHORT)min(Value / Unit, (ULONG64)MAXUSHORT);
}

static
VOID
RspndrCounterSnapshot(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_ const LLTD_QOS_HEADER *Header,
    _In_ ULONG Length)
{
    const LLTD_QOS_SNAPSHOT_REQUEST *Request = (const LLTD_QOS_SNAPSHOT_REQUEST *)(Header + 1);
    PLLTD_QOS_COUNTER_RESULT_BODY Result;
    PLLTD_QOS_SNAPSHOT Snapshot;
    RSPNDR_COUNTERS Previous, Current;
    ULONG64 ByteUnit, PacketUnit, Now;
    ULONG Count, Index, First;
    PRSPNDR_SEND Send;
    UCHAR ByteScale, PacketScale;

    if (Length < sizeof(*Header) + sizeof(*Request) || !RtlEqualMemory(Header->RealDestination, Adapter->Mac, 6))
        return;

    ByteScale = (UCHAR)min(Adapter->LinkSpeed / 8 / 1024 / MAXUSHORT, 255);
    PacketScale = (UCHAR)min(Adapter->LinkSpeed / (64 * 8) / MAXUSHORT, 255);
    ByteUnit = 1024ULL * (ByteScale + 1);
    PacketUnit = PacketScale + 1;

    Send = RspndrAllocateReply(Adapter, Header, LLTD_QOS_COUNTER_RESULT,
                               sizeof(*Result) + (RSPNDR_HISTORY + 1) * sizeof(LLTD_QOS_SNAPSHOT));
    if (!Send)
        return;
    Result = (PLLTD_QOS_COUNTER_RESULT_BODY)(Send->Frame + sizeof(LLTD_QOS_HEADER));
    Snapshot = (PLLTD_QOS_SNAPSHOT)(Result + 1);

    KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
    Now = RspndrNow();
    ASSERT(Adapter->HistorySamples <= RSPNDR_HISTORY + 1);
    Count = min((ULONG)Request->HistorySize, Adapter->HistorySamples ? Adapter->HistorySamples - 1 : 0);
    First = (Adapter->HistoryNext + (RSPNDR_HISTORY + 1) - Count - 1) % (RSPNDR_HISTORY + 1);
    for (Index = 0; Index < Count; Index++)
    {
        Previous = Adapter->History[(First + Index) % (RSPNDR_HISTORY + 1)];
        Current = Adapter->History[(First + Index + 1) % (RSPNDR_HISTORY + 1)];
        Snapshot[Index].BytesReceived = RtlUshortByteSwap(RspndrScaled(Current.BytesReceived - Previous.BytesReceived, ByteUnit));
        Snapshot[Index].PacketsReceived = RtlUshortByteSwap(RspndrScaled(Current.PacketsReceived - Previous.PacketsReceived, PacketUnit));
        Snapshot[Index].BytesSent = RtlUshortByteSwap(RspndrScaled(Current.BytesSent - Previous.BytesSent, ByteUnit));
        Snapshot[Index].PacketsSent = RtlUshortByteSwap(RspndrScaled(Current.PacketsSent - Previous.PacketsSent, PacketUnit));
    }
    Previous = (Adapter->SnapshotRunning && Adapter->HistorySamples != 0) ?
               Adapter->History[(Adapter->HistoryNext + RSPNDR_HISTORY) % (RSPNDR_HISTORY + 1)] : Adapter->Received;
    Current = Adapter->Received;
    /* FIXME: sent counters advance only at the 1-second samples, so the sub-second snapshot reports none */
    Current.BytesSent = Previous.BytesSent;
    Current.PacketsSent = Previous.PacketsSent;
    Snapshot[Count].BytesReceived = RtlUshortByteSwap(RspndrScaled(Current.BytesReceived - Previous.BytesReceived, ByteUnit));
    Snapshot[Count].PacketsReceived = RtlUshortByteSwap(RspndrScaled(Current.PacketsReceived - Previous.PacketsReceived, PacketUnit));
    Snapshot[Count].BytesSent = 0;
    Snapshot[Count].PacketsSent = 0;
    Result->SubsecondSpan = Adapter->SnapshotRunning ?
                            (UCHAR)min((Now - Adapter->LastSampleTime) * 256 / RSPNDR_SECOND, 255) : 0;
    KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);

    Result->ByteScale = ByteScale;
    Result->PacketScale = PacketScale;
    Result->HistorySize = (UCHAR)Count;
    Send->Length = max(sizeof(LLTD_QOS_HEADER) + sizeof(*Result) + (Count + 1) * sizeof(LLTD_QOS_SNAPSHOT),
                       LLTD_MIN_FRAME);
    RspndrSend(Send, 0, NULL);
}

static
VOID
RspndrProcessFrame(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length,
    _In_ ULONG64 Received)
{
    const LLTD_QOS_HEADER *Header = (const LLTD_QOS_HEADER *)Frame;

    if (Length < sizeof(*Header) || Header->EtherType != RtlUshortByteSwap(LLTD_ETHERTYPE) ||
        Header->Version != LLTD_VERSION || Header->TypeOfService != LLTD_TOS_QOS_DIAGNOSTICS)
    {
        return;
    }

    if (Header->Function == LLTD_QOS_COUNTER_LEASE)
    {
        RspndrCounterLease(Adapter);
        return;
    }
    if (Header->Function == LLTD_QOS_COUNTER_SNAPSHOT)
    {
        if (Header->SequenceNumber != 0)
            RspndrCounterSnapshot(Adapter, Header, Length);
        return;
    }
    if (!RspndrIsUnicast(Header->RealSource) || !RtlEqualMemory(Header->RealDestination, Adapter->Mac, 6) ||
        Header->SequenceNumber == 0)
    {
        return;
    }
    switch (Header->Function)
    {
        case LLTD_QOS_INITIALIZE_SINK:
            RspndrInitializeSink(Adapter, Header, Length);
            break;
        case LLTD_QOS_PROBE:
            RspndrProbe(Adapter, Header, Length, Received);
            break;
        case LLTD_QOS_QUERY:
            RspndrQuery(Adapter, Header);
            break;
        case LLTD_QOS_RESET:
            RspndrReset(Adapter, Header);
            break;
        default:
            break;
    }
}

static
VOID
NTAPI
RspndrReceive(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG NumberOfNetBufferLists,
    _In_ ULONG ReceiveFlags)
{
    PRSPNDR_ADAPTER Adapter = ProtocolBindingContext;
    ULONG64 Received = (ULONG64)KeQueryPerformanceCounter(NULL).QuadPart;
    UCHAR Storage[LLTD_MAX_FRAME];
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER Nb;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(NumberOfNetBufferLists);

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    for (Nbl = NetBufferLists; Nbl; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
    {
        for (Nb = NET_BUFFER_LIST_FIRST_NB(Nbl); Nb; Nb = NET_BUFFER_NEXT_NB(Nb))
        {
            ULONG Length = NET_BUFFER_DATA_LENGTH(Nb);
            PUCHAR Data;

            KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
            Adapter->Received.BytesReceived += Length;
            Adapter->Received.PacketsReceived++;
            KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);

            if (Length < sizeof(LLTD_QOS_HEADER) || Adapter->Unbinding)
                continue;
            Length = min(Length, LLTD_MAX_FRAME);
            Data = NdisGetDataBuffer(Nb, Length, Storage, 1, 0);
            if (Data)
                RspndrProcessFrame(Adapter, Data, Length, Received);
        }
    }
    KeLowerIrql(OldIrql);

    if (NDIS_TEST_RECEIVE_CAN_PEND(ReceiveFlags))
    {
        NdisReturnNetBufferLists(Adapter->BindingHandle, NetBufferLists,
                                 NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(ReceiveFlags) ? NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0);
    }
}

static
NDIS_STATUS
RspndrOidRequest(
    _In_ PRSPNDR_ADAPTER Adapter,
    _In_ NDIS_REQUEST_TYPE Type,
    _In_ NDIS_OID Oid,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    NDIS_OID_REQUEST Request;
    NDIS_STATUS Status;

    PAGED_CODE();

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    Request.Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
    Request.RequestType = Type;
    if (Type == NdisRequestSetInformation)
    {
        Request.DATA.SET_INFORMATION.Oid = Oid;
        Request.DATA.SET_INFORMATION.InformationBuffer = Buffer;
        Request.DATA.SET_INFORMATION.InformationBufferLength = Length;
    }
    else
    {
        Request.DATA.QUERY_INFORMATION.Oid = Oid;
        Request.DATA.QUERY_INFORMATION.InformationBuffer = Buffer;
        Request.DATA.QUERY_INFORMATION.InformationBufferLength = Length;
    }
    KeClearEvent(&Adapter->RequestDone);
    Status = NdisOidRequest(Adapter->BindingHandle, &Request);
    if (Status == NDIS_STATUS_PENDING)
    {
        KeWaitForSingleObject(&Adapter->RequestDone, Executive, KernelMode, FALSE, NULL);
        Status = Adapter->RequestStatus;
    }
    return Status;
}

static
VOID
NTAPI
RspndrOidRequestComplete(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    PRSPNDR_ADAPTER Adapter = ProtocolBindingContext;

    UNREFERENCED_PARAMETER(OidRequest);
    Adapter->RequestStatus = Status;
    KeSetEvent(&Adapter->RequestDone, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
RspndrSampleWork(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PRSPNDR_ADAPTER Adapter = Context;
    NDIS_STATISTICS_INFO Statistics;
    RSPNDR_COUNTERS Sample;
    NDIS_STATUS Status = NDIS_STATUS_NOT_SUPPORTED;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);
    PAGED_CODE();
    ASSERT(Adapter != NULL);

    RtlZeroMemory(&Statistics, sizeof(Statistics));
    if (Adapter->StatisticsSupported)
    {
        Status = RspndrOidRequest(Adapter, NdisRequestQueryStatistics, OID_GEN_STATISTICS, &Statistics,
                                  sizeof(Statistics));
    }

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    Sample = Adapter->Received;
    if (Status == NDIS_STATUS_SUCCESS)
    {
        Sample.BytesSent = Statistics.ifHCOutOctets;
        Sample.PacketsSent = Statistics.ifHCOutUcastPkts + Statistics.ifHCOutMulticastPkts +
                             Statistics.ifHCOutBroadcastPkts;
    }
    else
    {
        /* FIXME: without OID_GEN_STATISTICS from the miniport no sent counters are reported */
        Adapter->StatisticsSupported = FALSE;
    }
    if (Adapter->SnapshotRunning)
    {
        Adapter->History[Adapter->HistoryNext] = Sample;
        Adapter->HistoryNext = (Adapter->HistoryNext + 1) % (RSPNDR_HISTORY + 1);
        if (Adapter->HistorySamples < RSPNDR_HISTORY + 1)
            Adapter->HistorySamples++;
        Adapter->LastSampleTime = RspndrNow();
    }
    ASSERT(Adapter->SampleQueued);
    Adapter->SampleQueued = FALSE;
    KeSetEvent(&Adapter->SampleIdle, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
}

static KDEFERRED_ROUTINE RspndrSnapshotDpc;

static
VOID
NTAPI
RspndrSnapshotDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PRSPNDR_ADAPTER Adapter = DeferredContext;
    BOOLEAN Queue = FALSE;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
    if (Adapter->SnapshotRunning && RspndrNow() >= Adapter->LeaseExpiry)
    {
        KeCancelTimer(&Adapter->SnapshotTimer);
        Adapter->SnapshotRunning = FALSE;
    }
    else if (Adapter->SnapshotRunning && !Adapter->SampleQueued && !Adapter->Unbinding)
    {
        Adapter->SampleQueued = TRUE;
        KeClearEvent(&Adapter->SampleIdle);
        Queue = TRUE;
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);

    if (Queue)
        IoQueueWorkItem(Adapter->SampleWorkItem, RspndrSampleWork, DelayedWorkQueue, Adapter);
}

static KDEFERRED_ROUTINE RspndrInactivityDpc;

static
VOID
NTAPI
RspndrInactivityDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PRSPNDR_ADAPTER Adapter = DeferredContext;
    PLIST_ENTRY Entry, Next;
    ULONG64 Now = RspndrNow();

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeAcquireSpinLockAtDpcLevel(&Adapter->Lock);
    for (Entry = Adapter->Sessions.Flink; Entry != &Adapter->Sessions; Entry = Next)
    {
        PRSPNDR_SESSION Session = CONTAINING_RECORD(Entry, RSPNDR_SESSION, Link);

        Next = Entry->Flink;
        if (Now - Session->LastActive >= RSPNDR_SESSION_IDLE)
            RspndrDeleteSessionLocked(Adapter, Session);
    }
    KeReleaseSpinLockFromDpcLevel(&Adapter->Lock);
}

static
VOID
RspndrFreeAdapter(
    _In_ PRSPNDR_ADAPTER Adapter)
{
    if (Adapter->SampleWorkItem)
        IoFreeWorkItem(Adapter->SampleWorkItem);
    if (Adapter->NblPool)
        NdisFreeNetBufferListPool(Adapter->NblPool);
    ExFreePoolWithTag(Adapter, TAG_RSPNDR);
}

static
NDIS_STATUS
NTAPI
RspndrBindAdapter(
    _In_ NDIS_HANDLE ProtocolDriverContext,
    _In_ NDIS_HANDLE BindContext,
    _In_ PNDIS_BIND_PARAMETERS BindParameters)
{
    NDIS_MEDIUM Media[] = { NdisMedium802_3 };
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    NDIS_OPEN_PARAMETERS OpenParameters;
    PRSPNDR_ADAPTER Adapter;
    NDIS_STATUS Status;
    ULONG Filter;
    UINT Medium = 0;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(ProtocolDriverContext);
    PAGED_CODE();

    if (BindParameters->MacAddressLength != 6)
        return NDIS_STATUS_NOT_SUPPORTED;

    Adapter = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Adapter), TAG_RSPNDR);
    if (!Adapter)
        return NDIS_STATUS_RESOURCES;
    KeInitializeSpinLock(&Adapter->Lock);
    InitializeListHead(&Adapter->Sessions);
    KeInitializeEvent(&Adapter->SendsDone, NotificationEvent, TRUE);
    KeInitializeEvent(&Adapter->AsyncEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Adapter->RequestDone, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->SampleIdle, NotificationEvent, TRUE);
    KeInitializeTimer(&Adapter->InactivityTimer);
    KeInitializeDpc(&Adapter->InactivityDpc, RspndrInactivityDpc, Adapter);
    KeInitializeTimer(&Adapter->SnapshotTimer);
    KeInitializeDpc(&Adapter->SnapshotDpc, RspndrSnapshotDpc, Adapter);
    RtlCopyMemory(Adapter->Mac, BindParameters->CurrentMacAddress, 6);
    Adapter->LinkSpeed = BindParameters->XmitLinkSpeed;
    Adapter->Paused = TRUE;
    Adapter->StatisticsSupported = TRUE;

    RtlZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = TAG_RSPNDR;
    Adapter->NblPool = NdisAllocateNetBufferListPool(RspndrProtocolHandle, &PoolParameters);
    Adapter->SampleWorkItem = IoAllocateWorkItem(RspndrDeviceObject);
    if (!Adapter->NblPool || !Adapter->SampleWorkItem)
    {
        RspndrFreeAdapter(Adapter);
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(&OpenParameters, sizeof(OpenParameters));
    OpenParameters.Header.Type = NDIS_OBJECT_TYPE_OPEN_PARAMETERS;
    OpenParameters.Header.Revision = NDIS_OPEN_PARAMETERS_REVISION_1;
    OpenParameters.Header.Size = NDIS_SIZEOF_OPEN_PARAMETERS_REVISION_1;
    OpenParameters.AdapterName = BindParameters->AdapterName;
    OpenParameters.MediumArray = Media;
    OpenParameters.MediumArraySize = ARRAYSIZE(Media);
    OpenParameters.SelectedMediumIndex = &Medium;
    Status = NdisOpenAdapterEx(RspndrProtocolHandle, Adapter, &OpenParameters, BindContext, &Adapter->BindingHandle);
    if (Status == NDIS_STATUS_PENDING)
    {
        KeWaitForSingleObject(&Adapter->AsyncEvent, Executive, KernelMode, FALSE, NULL);
        Status = Adapter->AsyncStatus;
    }
    if (Status != NDIS_STATUS_SUCCESS)
    {
        RspndrFreeAdapter(Adapter);
        return Status;
    }

    Filter = NDIS_PACKET_TYPE_DIRECTED | NDIS_PACKET_TYPE_BROADCAST;
    Status = RspndrOidRequest(Adapter, NdisRequestSetInformation, OID_GEN_CURRENT_PACKET_FILTER, &Filter, sizeof(Filter));
    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("RSPNDR: setting the packet filter failed 0x%08lx\n", Status);

    KeAcquireSpinLock(&RspndrAdaptersLock, &OldIrql);
    InsertTailList(&RspndrAdapters, &Adapter->Link);
    KeReleaseSpinLock(&RspndrAdaptersLock, OldIrql);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    Adapter->Paused = FALSE;
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
RspndrOpenAdapterComplete(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ NDIS_STATUS Status)
{
    PRSPNDR_ADAPTER Adapter = ProtocolBindingContext;

    Adapter->AsyncStatus = Status;
    KeSetEvent(&Adapter->AsyncEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
RspndrCloseAdapterComplete(
    _In_ NDIS_HANDLE ProtocolBindingContext)
{
    PRSPNDR_ADAPTER Adapter = ProtocolBindingContext;

    Adapter->AsyncStatus = NDIS_STATUS_SUCCESS;
    KeSetEvent(&Adapter->AsyncEvent, IO_NO_INCREMENT, FALSE);
}

static
NDIS_STATUS
NTAPI
RspndrUnbindAdapter(
    _In_ NDIS_HANDLE UnbindContext,
    _In_ NDIS_HANDLE ProtocolBindingContext)
{
    PRSPNDR_ADAPTER Adapter = ProtocolBindingContext;
    NDIS_STATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(UnbindContext);
    PAGED_CODE();

    KeAcquireSpinLock(&RspndrAdaptersLock, &OldIrql);
    RemoveEntryList(&Adapter->Link);
    KeReleaseSpinLock(&RspndrAdaptersLock, OldIrql);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    Adapter->Unbinding = TRUE;
    KeCancelTimer(&Adapter->InactivityTimer);
    KeCancelTimer(&Adapter->SnapshotTimer);
    Adapter->InactivityRunning = FALSE;
    Adapter->SnapshotRunning = FALSE;
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    KeFlushQueuedDpcs();
    KeWaitForSingleObject(&Adapter->SampleIdle, Executive, KernelMode, FALSE, NULL);
    KeWaitForSingleObject(&Adapter->SendsDone, Executive, KernelMode, FALSE, NULL);

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    while (!IsListEmpty(&Adapter->Sessions))
        RspndrDeleteSessionLocked(Adapter, CONTAINING_RECORD(Adapter->Sessions.Flink, RSPNDR_SESSION, Link));
    ASSERT(Adapter->PendingSends == 0);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);

    Status = NdisCloseAdapterEx(Adapter->BindingHandle);
    if (Status == NDIS_STATUS_PENDING)
    {
        KeWaitForSingleObject(&Adapter->AsyncEvent, Executive, KernelMode, FALSE, NULL);
        Status = Adapter->AsyncStatus;
    }
    RspndrFreeAdapter(Adapter);
    return Status;
}

static
NDIS_STATUS
NTAPI
RspndrNetPnPEvent(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PRSPNDR_ADAPTER Adapter = ProtocolBindingContext;
    KIRQL OldIrql;

    if (!Adapter)
        return NDIS_STATUS_SUCCESS;
    switch (NetPnPEventNotification->NetPnPEvent.NetEvent)
    {
        case NetEventPause:
            KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
            Adapter->Paused = TRUE;
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            KeWaitForSingleObject(&Adapter->SendsDone, Executive, KernelMode, FALSE, NULL);
            return NDIS_STATUS_SUCCESS;
        case NetEventRestart:
            KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
            Adapter->Paused = FALSE;
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            return NDIS_STATUS_SUCCESS;
        default:
            return NDIS_STATUS_SUCCESS;
    }
}

static
VOID
NTAPI
RspndrStatus(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
    PRSPNDR_ADAPTER Adapter = ProtocolBindingContext;

    if (StatusIndication->StatusCode == NDIS_STATUS_LINK_STATE &&
        StatusIndication->StatusBufferSize >= sizeof(NDIS_LINK_STATE))
    {
        Adapter->LinkSpeed = ((PNDIS_LINK_STATE)StatusIndication->StatusBuffer)->XmitLinkSpeed;
    }
}

static DRIVER_UNLOAD RspndrUnload;

static
VOID
NTAPI
RspndrUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    PAGED_CODE();

    NdisDeregisterProtocolDriver(RspndrProtocolHandle);
    ASSERT(IsListEmpty(&RspndrAdapters));
    IoDeleteDevice(RspndrDeviceObject);
}

DRIVER_INITIALIZE DriverEntry;

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_PROTOCOL_DRIVER_CHARACTERISTICS Characteristics;
    NDIS_STRING Name = RTL_CONSTANT_STRING(L"RSPNDR");
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    PAGED_CODE();

    InitializeListHead(&RspndrAdapters);
    KeInitializeSpinLock(&RspndrAdaptersLock);

    Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_NETWORK, FILE_DEVICE_SECURE_OPEN, FALSE,
                            &RspndrDeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    /* TODO: the topology discovery and quick discovery responder roles ([MS-LLTD] 3.5, 3.6) */
    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_PROTOCOL_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.MajorNdisVersion = 6;
    Characteristics.MinorNdisVersion = 0;
    Characteristics.Name = Name;
    Characteristics.BindAdapterHandlerEx = RspndrBindAdapter;
    Characteristics.UnbindAdapterHandlerEx = RspndrUnbindAdapter;
    Characteristics.OpenAdapterCompleteHandlerEx = RspndrOpenAdapterComplete;
    Characteristics.CloseAdapterCompleteHandlerEx = RspndrCloseAdapterComplete;
    Characteristics.NetPnPEventHandler = RspndrNetPnPEvent;
    Characteristics.OidRequestCompleteHandler = RspndrOidRequestComplete;
    Characteristics.StatusHandlerEx = RspndrStatus;
    Characteristics.ReceiveNetBufferListsHandler = RspndrReceive;
    Characteristics.SendNetBufferListsCompleteHandler = RspndrSendComplete;

    Status = NdisRegisterProtocolDriver(NULL, &Characteristics, &RspndrProtocolHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("RSPNDR: NdisRegisterProtocolDriver failed 0x%08lx\n", Status);
        IoDeleteDevice(RspndrDeviceObject);
        return Status;
    }
    DriverObject->DriverUnload = RspndrUnload;
    return STATUS_SUCCESS;
}
