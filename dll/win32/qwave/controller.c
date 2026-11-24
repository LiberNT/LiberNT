/*
 * PROJECT:     LiberNT qWAVE
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     qWAVE service: the LLTD QoS controller behind adaptive flows ([MS-LLTD] 3.3)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <winbase.h>
#include <winsvc.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <qos2.h>
#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <libernt/qwavedrv.h>
#include <libernt/lltd.h>
#include <libernt/drivers/lltdio.h>
#include "qwave_private.h"

#define QWAVE_RESPONSE_MS       100
#define QWAVE_ATTEMPTS          5
#define QWAVE_MONITOR_MS        1000
#define QWAVE_TRAIN             8
#define QWAVE_PROBEGAPS         3
#define QWAVE_ETHERNET_HEADER   14
#define QWAVE_BODY_OFFSET       (sizeof(LLTD_QOS_HEADER) - QWAVE_ETHERNET_HEADER)
#define QWAVE_TRAIN_FRAME       (LLTD_MAX_FRAME - QWAVE_ETHERNET_HEADER)
#define QWAVE_RECEIVE_BUFFER    (sizeof(LLTDIO_RECEIVED) + LLTD_MAX_FRAME)

typedef struct _QWAVE_SESSION
{
    LIST_ENTRY Link;
    QWAVE_ADDRESS Destination;
    ULONG References;
    HANDLE Lltdio;
    UCHAR LocalMac[6];
    UCHAR SinkMac[6];
    ULONG64 LocalFrequency;
    ULONG64 SinkFrequency;
    ULONG64 LocalLinkSpeed;
    ULONG64 SinkLinkSpeed;
    USHORT Sequence;
} QWAVE_SESSION, *PQWAVE_SESSION;

typedef struct _QWAVE_FLOW_BINDING
{
    LIST_ENTRY Link;
    ULONG FlowId;
    PQWAVE_SESSION Session;
    BOOLEAN Monitored;
} QWAVE_FLOW_BINDING, *PQWAVE_FLOW_BINDING;

typedef struct _QWAVE_CONTROLLER
{
    HANDLE Driver;
    HANDLE Thread;
    HANDLE StopEvent;
    QWAVE_REPORT_STATUS Report;
    LIST_ENTRY Sessions;
    LIST_ENTRY Flows;
} QWAVE_CONTROLLER, *PQWAVE_CONTROLLER;

static QWAVE_CONTROLLER QwaveControllerState;

static
NTSTATUS
QwaveOpenDeviceByName(
    _In_ PCWSTR DeviceName,
    _Out_ PHANDLE Handle)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK Iosb;

    RtlInitUnicodeString(&Name, DeviceName);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    return NtCreateFile(Handle, GENERIC_READ, &Attributes, &Iosb, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_OPEN, 0, NULL, 0);
}

/* Runs one overlapped request; gives up after TimeoutMs or when the stop event is set */
static
DWORD
QwaveDeviceIo(
    _In_ HANDLE Device,
    _In_ DWORD Code,
    _In_reads_bytes_opt_(InputLength) PVOID Input,
    _In_ DWORD InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ DWORD OutputLength,
    _Out_opt_ PDWORD Returned,
    _In_ DWORD TimeoutMs)
{
    HANDLE Waits[2];
    OVERLAPPED Overlapped = { 0 };
    DWORD Bytes = 0, Error = ERROR_SUCCESS, Wait;

    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Overlapped.hEvent)
        return GetLastError();
    if (!DeviceIoControl(Device, Code, Input, InputLength, Output, OutputLength, &Bytes, &Overlapped))
    {
        Error = GetLastError();
        if (Error == ERROR_IO_PENDING)
        {
            Waits[0] = Overlapped.hEvent;
            Waits[1] = QwaveControllerState.StopEvent;
            Wait = WaitForMultipleObjects(Waits[1] ? 2 : 1, Waits, FALSE, TimeoutMs);
            if (Wait != WAIT_OBJECT_0)
                CancelIoEx(Device, &Overlapped);
            Error = GetOverlappedResult(Device, &Overlapped, &Bytes, TRUE) ? ERROR_SUCCESS : GetLastError();
            if (Wait == WAIT_TIMEOUT && Error == ERROR_OPERATION_ABORTED)
                Error = WAIT_TIMEOUT;
        }
    }
    CloseHandle(Overlapped.hEvent);
    if (Returned)
        *Returned = Bytes;
    return Error;
}

static
USHORT
QwaveNextSequence(
    _Inout_ PQWAVE_SESSION Session)
{
    USHORT Sequence = Session->Sequence;

    /* Ones complement: 0xFFFF advances to 0x0001 ([MS-LLTD] 3.3.1) */
    Session->Sequence = (Sequence == 0xFFFF) ? 1 : (USHORT)(Sequence + 1);
    ASSERT(Sequence != 0);
    return Sequence;
}

/* Fills an LLTD frame from its demultiplex header on, padded with zeros to Length */
static
VOID
QwaveBuildFrame(
    _In_ PQWAVE_SESSION Session,
    _In_ UCHAR Function,
    _In_ USHORT Sequence,
    _In_reads_bytes_opt_(BodyLength) const VOID *Body,
    _In_ ULONG BodyLength,
    _Out_writes_bytes_(Length) PUCHAR Frame,
    _In_ ULONG Length)
{
    ASSERT(Length >= QWAVE_BODY_OFFSET + BodyLength);
    RtlZeroMemory(Frame, Length);
    Frame[0] = LLTD_VERSION;
    Frame[1] = LLTD_TOS_QOS_DIAGNOSTICS;
    Frame[3] = Function;
    RtlCopyMemory(Frame + 4, Session->SinkMac, 6);
    RtlCopyMemory(Frame + 10, Session->LocalMac, 6);
    *(PUSHORT)(Frame + 16) = htons(Sequence);
    if (BodyLength)
        RtlCopyMemory(Frame + QWAVE_BODY_OFFSET, Body, BodyLength);
}

static
DWORD
QwaveSendFrames(
    _In_ PQWAVE_SESSION Session,
    _In_reads_bytes_(Count * FrameLength) const UCHAR *Frames,
    _In_ ULONG Count,
    _In_ ULONG FrameLength,
    _In_ ULONG TimestampOffset)
{
    ULONG Size = sizeof(LLTDIO_SEND_INPUT) + Count * FrameLength;
    PLLTDIO_SEND_INPUT Input;
    DWORD Error;

    Input = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (!Input)
        return ERROR_NOT_ENOUGH_MEMORY;
    RtlCopyMemory(Input->Destination, Session->SinkMac, 6);
    Input->Count = Count;
    Input->FrameLength = FrameLength;
    Input->TimestampOffset = TimestampOffset;
    RtlCopyMemory(Input + 1, Frames, Count * FrameLength);
    Error = QwaveDeviceIo(Session->Lltdio, IOCTL_LLTDIO_SEND, Input, Size, NULL, 0, NULL, INFINITE);
    HeapFree(GetProcessHeap(), 0, Input);
    return Error;
}

static
DWORD
QwaveSendOne(
    _In_ PQWAVE_SESSION Session,
    _In_ UCHAR Function,
    _In_ USHORT Sequence,
    _In_reads_bytes_opt_(BodyLength) const VOID *Body,
    _In_ ULONG BodyLength,
    _In_ ULONG TimestampOffset)
{
    UCHAR Frame[LLTD_MIN_FRAME];

    QwaveBuildFrame(Session, Function, Sequence, Body, BodyLength, Frame, sizeof(Frame));
    return QwaveSendFrames(Session, Frame, 1, sizeof(Frame), TimestampOffset);
}

/* Waits for a frame of one of the two functions that answers Sequence; returns its body */
static
BOOL
QwaveReceive(
    _In_ PQWAVE_SESSION Session,
    _In_ UCHAR Function,
    _In_ UCHAR OtherFunction,
    _In_ USHORT Sequence,
    _In_ DWORD TimeoutMs,
    _Out_writes_bytes_(QWAVE_RECEIVE_BUFFER) PUCHAR Buffer,
    _Out_ PUCHAR Received,
    _Out_ const UCHAR **Body,
    _Out_ PULONG BodyLength,
    _Out_ PULONG64 Stamp)
{
    DWORD Start = GetTickCount(), Elapsed, Bytes;

    for (;;)
    {
        PLLTDIO_RECEIVED Frame = (PLLTDIO_RECEIVED)Buffer;
        const LLTD_QOS_HEADER *Header = (const LLTD_QOS_HEADER *)(Frame + 1);

        Elapsed = GetTickCount() - Start;
        if (Elapsed >= TimeoutMs)
            return FALSE;
        if (QwaveDeviceIo(Session->Lltdio, IOCTL_LLTDIO_RECEIVE, NULL, 0, Buffer, QWAVE_RECEIVE_BUFFER, &Bytes,
                          TimeoutMs - Elapsed) != ERROR_SUCCESS)
        {
            return FALSE;
        }
        if (Bytes < sizeof(*Frame) + sizeof(*Header) || Frame->Length < sizeof(*Header) ||
            (Header->Function != Function && Header->Function != OtherFunction) ||
            ntohs(Header->SequenceNumber) != Sequence || !RtlEqualMemory(Header->RealSource, Session->SinkMac, 6))
        {
            continue;
        }
        *Received = Header->Function;
        *Body = (const UCHAR *)(Header + 1);
        *BodyLength = Frame->Length - sizeof(*Header);
        *Stamp = Frame->ReceiveTimestamp;
        return TRUE;
    }
}

static
BOOL
QwaveSameDestination(
    _In_ const QWAVE_ADDRESS *A,
    _In_ const QWAVE_ADDRESS *B)
{
    return A->Family == B->Family && A->ScopeId == B->ScopeId && RtlEqualMemory(A->Address, B->Address, sizeof(A->Address));
}

/* The local and sink MAC addresses of an on-link destination */
static
DWORD
QwaveResolveSink(
    _In_ const QWAVE_ADDRESS *Destination,
    _Out_writes_(6) PUCHAR LocalMac,
    _Out_writes_(6) PUCHAR SinkMac)
{
    SOCKADDR_INET Target, Source;
    MIB_IPFORWARD_ROW2 Route;
    MIB_IPNET_ROW2 Neighbor;
    MIB_IF_ROW2 Interface;

    if (Destination->Family != AF_INET)
    {
        /* TODO: sinks behind IPv6 addresses need IPv6 neighbor resolution in ResolveIpNetEntry2 */
        return ERROR_NOT_SUPPORTED;
    }
    RtlZeroMemory(&Target, sizeof(Target));
    Target.Ipv4.sin_family = AF_INET;
    RtlCopyMemory(&Target.Ipv4.sin_addr, Destination->Address, sizeof(Target.Ipv4.sin_addr));
    if (GetBestRoute2(NULL, 0, NULL, &Target, 0, &Route, &Source) != NO_ERROR)
        return ERROR_HOST_UNREACHABLE;
    /* Network experiments run only to on-link destinations (QOSSetFlow documentation) */
    if (Route.NextHop.si_family == AF_INET && Route.NextHop.Ipv4.sin_addr.s_addr != 0 &&
        Route.NextHop.Ipv4.sin_addr.s_addr != Target.Ipv4.sin_addr.s_addr)
    {
        return ERROR_NOT_SUPPORTED;
    }

    RtlZeroMemory(&Interface, sizeof(Interface));
    Interface.InterfaceIndex = Route.InterfaceIndex;
    if (GetIfEntry2(&Interface) != NO_ERROR || Interface.PhysicalAddressLength != 6)
        return ERROR_NOT_SUPPORTED;
    RtlCopyMemory(LocalMac, Interface.PhysicalAddress, 6);

    RtlZeroMemory(&Neighbor, sizeof(Neighbor));
    Neighbor.Address = Target;
    Neighbor.InterfaceIndex = Route.InterfaceIndex;
    if (ResolveIpNetEntry2(&Neighbor, NULL) != NO_ERROR || Neighbor.PhysicalAddressLength != 6)
        return ERROR_HOST_UNREACHABLE;
    RtlCopyMemory(SinkMac, Neighbor.PhysicalAddress, 6);
    return ERROR_SUCCESS;
}

static
VOID
QwaveFreeSession(
    _In_ PQWAVE_SESSION Session)
{
    if (Session->Lltdio)
        NtClose(Session->Lltdio);
    HeapFree(GetProcessHeap(), 0, Session);
}

/* Sets up a network test session with the sink ([MS-LLTD] 3.3.4.1) */
static
DWORD
QwaveInitializeSink(
    _In_ PQWAVE_SESSION Session)
{
    UCHAR Buffer[QWAVE_RECEIVE_BUFFER];
    LLTD_QOS_INITIALIZE Initialize = { LLTD_INTERRUPT_MOD_EXISTING };
    USHORT Sequence = QwaveNextSequence(Session);
    const UCHAR *Body;
    ULONG BodyLength, Attempt;
    ULONG64 Stamp;
    UCHAR Function;

    for (Attempt = 0; Attempt < QWAVE_ATTEMPTS; Attempt++)
    {
        if (QwaveSendOne(Session, LLTD_QOS_INITIALIZE_SINK, Sequence, &Initialize, sizeof(Initialize),
                         LLTDIO_NO_TIMESTAMP) != ERROR_SUCCESS)
        {
            return ERROR_IO_DEVICE;
        }
        if (!QwaveReceive(Session, LLTD_QOS_READY, LLTD_QOS_ERROR, Sequence, QWAVE_RESPONSE_MS, Buffer, &Function,
                          &Body, &BodyLength, &Stamp))
        {
            continue;
        }
        if (Function == LLTD_QOS_ERROR)
        {
            USHORT Code = (BodyLength >= sizeof(USHORT)) ? ntohs(*(const USHORT *)Body) : LLTD_ERROR_RESOURCES;

            return (Code == LLTD_ERROR_BUSY) ? ERROR_RETRY : ERROR_NO_SYSTEM_RESOURCES;
        }
        if (BodyLength < sizeof(LLTD_QOS_READY_BODY))
            return ERROR_NOT_SUPPORTED;
        Session->SinkLinkSpeed = (ULONG64)ntohl(((const LLTD_QOS_READY_BODY *)Body)->SinkLinkSpeed) * 100;
        Session->SinkFrequency = RtlUlonglongByteSwap(((const LLTD_QOS_READY_BODY *)Body)->PerformanceCounterFrequency);
        return Session->SinkFrequency ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
    }
    /* No sink answered: the destination does not support adaptive flows (QOSAddSocketToFlow documentation) */
    return ERROR_NOT_SUPPORTED;
}

static
DWORD
QwaveOpenSession(
    _In_ PQWAVE_CONTROLLER Controller,
    _In_ const QWAVE_ADDRESS *Destination,
    _Out_ PQWAVE_SESSION *Out)
{
    LLTDIO_ADAPTER_INFO Info;
    PQWAVE_SESSION Session;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    DWORD Error, Bytes;

    *Out = NULL;
    for (Entry = Controller->Sessions.Flink; Entry != &Controller->Sessions; Entry = Entry->Flink)
    {
        Session = CONTAINING_RECORD(Entry, QWAVE_SESSION, Link);
        if (QwaveSameDestination(&Session->Destination, Destination))
        {
            Session->References++;
            *Out = Session;
            return ERROR_SUCCESS;
        }
    }

    Session = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Session));
    if (!Session)
        return ERROR_NOT_ENOUGH_MEMORY;
    Session->Destination = *Destination;
    Session->Destination.Port = 0;
    Session->References = 1;
    Session->Sequence = (USHORT)(GetTickCount() ^ GetCurrentProcessId());
    if (Session->Sequence == 0)
        Session->Sequence = 1;

    Error = QwaveResolveSink(Destination, Session->LocalMac, Session->SinkMac);
    if (Error == ERROR_SUCCESS)
    {
        Status = QwaveOpenDeviceByName(LLTDIO_DEVICE_NAME, &Session->Lltdio);
        Error = NT_SUCCESS(Status) ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
    }
    if (Error == ERROR_SUCCESS)
    {
        Error = QwaveDeviceIo(Session->Lltdio, IOCTL_LLTDIO_BIND, Session->LocalMac, 6, &Info, sizeof(Info), &Bytes,
                              INFINITE);
        if (Error != ERROR_SUCCESS)
            Error = ERROR_NOT_SUPPORTED;
    }
    if (Error == ERROR_SUCCESS)
    {
        Session->LocalFrequency = Info.PerformanceCounterFrequency;
        Session->LocalLinkSpeed = Info.LinkSpeed;
        Error = QwaveInitializeSink(Session);
    }
    if (Error != ERROR_SUCCESS)
    {
        QwaveFreeSession(Session);
        return Error;
    }
    InsertTailList(&Controller->Sessions, &Session->Link);
    *Out = Session;
    return ERROR_SUCCESS;
}

/* Ends the session once its last user is gone ([MS-LLTD] 3.3.4.2) */
static
VOID
QwaveReleaseSession(
    _In_ PQWAVE_SESSION Session)
{
    UCHAR Buffer[QWAVE_RECEIVE_BUFFER];
    const UCHAR *Body;
    ULONG BodyLength, Attempt;
    ULONG64 Stamp;
    USHORT Sequence;
    UCHAR Function;

    ASSERT(Session->References != 0);
    if (--Session->References != 0)
        return;
    RemoveEntryList(&Session->Link);
    Sequence = QwaveNextSequence(Session);
    for (Attempt = 0; Attempt < QWAVE_ATTEMPTS; Attempt++)
    {
        if (QwaveSendOne(Session, LLTD_QOS_RESET, Sequence, NULL, 0, LLTDIO_NO_TIMESTAMP) != ERROR_SUCCESS ||
            QwaveReceive(Session, LLTD_QOS_ACK, LLTD_QOS_ACK, Sequence, QWAVE_RESPONSE_MS, Buffer, &Function, &Body,
                         &BodyLength, &Stamp))
        {
            break;
        }
    }
    QwaveFreeSession(Session);
}

/* Measures the path to the sink: a timed probe train for the bottleneck and available bandwidth, probegaps for the RTT */
static
DWORD
QwaveMeasure(
    _In_ PQWAVE_SESSION Session,
    _Out_ PQWAVE_FUNDAMENTALS Fundamentals)
{
    static UCHAR Train[QWAVE_TRAIN * QWAVE_TRAIN_FRAME];
    UCHAR Buffer[QWAVE_RECEIVE_BUFFER];
    ULONG64 Arrival[QWAVE_TRAIN] = { 0 };
    ULONG64 MinGap = MAXULONG64, GapSum = 0, Capacity, Cap, Stamp, BestRtt = MAXULONG64;
    LLTD_QOS_PROBE_BODY Probe = { 0 };
    const UCHAR *Body;
    ULONG BodyLength, Index, Attempt, Gaps = 0, Count;
    USHORT Sequence;
    UCHAR Function;
    BOOL Answered = FALSE;

    RtlZeroMemory(Fundamentals, sizeof(*Fundamentals));

    Sequence = QwaveNextSequence(Session);
    for (Index = 0; Index < QWAVE_TRAIN; Index++)
    {
        Probe.TestType = LLTD_PROBE_TIMED;
        Probe.PacketId = (UCHAR)Index;
        QwaveBuildFrame(Session, LLTD_QOS_PROBE, Sequence, &Probe, sizeof(Probe), Train + Index * QWAVE_TRAIN_FRAME,
                        QWAVE_TRAIN_FRAME);
    }
    if (QwaveSendFrames(Session, Train, QWAVE_TRAIN, QWAVE_TRAIN_FRAME,
                        QWAVE_BODY_OFFSET + FIELD_OFFSET(LLTD_QOS_PROBE_BODY, ControllerTransmitTimestamp)) != ERROR_SUCCESS)
    {
        return ERROR_IO_DEVICE;
    }
    for (Attempt = 0; Attempt < QWAVE_ATTEMPTS && !Answered; Attempt++)
    {
        if (QwaveSendOne(Session, LLTD_QOS_QUERY, Sequence, NULL, 0, LLTDIO_NO_TIMESTAMP) != ERROR_SUCCESS)
            return ERROR_IO_DEVICE;
        Answered = QwaveReceive(Session, LLTD_QOS_QUERY_RESP, LLTD_QOS_QUERY_RESP, Sequence, QWAVE_RESPONSE_MS, Buffer,
                                &Function, &Body, &BodyLength, &Stamp);
    }
    if (!Answered || BodyLength < sizeof(USHORT))
        return ERROR_TIMEOUT;
    Count = ntohs(*(const USHORT *)Body);
    if (Count & LLTD_QUERY_RESP_ERROR)
        return ERROR_RETRY;
    Count &= LLTD_QUERY_RESP_COUNT_MASK;
    if (BodyLength < sizeof(USHORT) + Count * sizeof(LLTD_QOS_EVENT))
        return ERROR_RETRY;
    for (Index = 0; Index < Count; Index++)
    {
        const LLTD_QOS_EVENT *Event = (const LLTD_QOS_EVENT *)(Body + sizeof(USHORT)) + Index;

        if (Event->PacketId < QWAVE_TRAIN)
            Arrival[Event->PacketId] = RtlUlonglongByteSwap(Event->SinkReceiveTimestamp);
    }
    for (Index = 1; Index < QWAVE_TRAIN; Index++)
    {
        if (Arrival[Index] && Arrival[Index - 1] && Arrival[Index] > Arrival[Index - 1])
        {
            ULONG64 Gap = Arrival[Index] - Arrival[Index - 1];

            MinGap = min(MinGap, Gap);
            GapSum += Gap;
            Gaps++;
        }
    }
    if (Gaps == 0)
        return ERROR_RETRY;

    /* FIXME: the qWAVE bandwidth estimators are not documented; packet-pair dispersion stands in for them */
    Capacity = (ULONG64)LLTD_MAX_FRAME * 8 * Session->SinkFrequency / MinGap;
    Cap = Session->LocalLinkSpeed;
    if (Session->SinkLinkSpeed && (!Cap || Session->SinkLinkSpeed < Cap))
        Cap = Session->SinkLinkSpeed;
    if (Cap && Capacity > Cap)
        Capacity = Cap;
    Fundamentals->BottleneckBandwidthSet = TRUE;
    Fundamentals->BottleneckBandwidth = Capacity;
    Fundamentals->AvailableBandwidthSet = TRUE;
    Fundamentals->AvailableBandwidth = Capacity * MinGap * Gaps / GapSum;

    for (Index = 0; Index < QWAVE_PROBEGAPS; Index++)
    {
        const LLTD_QOS_PROBE_BODY *Reflected;
        ULONG64 ControllerSent, Held, Round;

        Sequence = QwaveNextSequence(Session);
        RtlZeroMemory(&Probe, sizeof(Probe));
        Probe.TestType = LLTD_PROBE_PROBEGAP_CONTROLLER;
        Probe.PacketId = (UCHAR)Index;
        if (QwaveSendOne(Session, LLTD_QOS_PROBE, Sequence, &Probe, sizeof(Probe),
                         QWAVE_BODY_OFFSET + FIELD_OFFSET(LLTD_QOS_PROBE_BODY, ControllerTransmitTimestamp)) != ERROR_SUCCESS ||
            !QwaveReceive(Session, LLTD_QOS_PROBE, LLTD_QOS_PROBE, Sequence, QWAVE_RESPONSE_MS, Buffer, &Function,
                          &Body, &BodyLength, &Stamp) ||
            BodyLength < sizeof(*Reflected))
        {
            continue;
        }
        Reflected = (const LLTD_QOS_PROBE_BODY *)Body;
        if (Reflected->TestType != LLTD_PROBE_PROBEGAP_SINK)
            continue;
        ControllerSent = RtlUlonglongByteSwap(Reflected->ControllerTransmitTimestamp);
        Held = RtlUlonglongByteSwap(Reflected->SinkTransmitTimestamp) - RtlUlonglongByteSwap(Reflected->SinkReceiveTimestamp);
        if (Stamp <= ControllerSent || !Session->LocalFrequency)
            continue;
        Round = (Stamp - ControllerSent) * 1000000 / Session->LocalFrequency;
        Held = Held * 1000000 / Session->SinkFrequency;
        BestRtt = min(BestRtt, (Round > Held) ? Round - Held : 0);
    }
    if (BestRtt != MAXULONG64)
    {
        Fundamentals->RttSet = TRUE;
        Fundamentals->Rtt = (ULONG)min(BestRtt, (ULONG64)MAXULONG);
    }
    return ERROR_SUCCESS;
}

static
PQWAVE_FLOW_BINDING
QwaveFindBinding(
    _In_ PQWAVE_CONTROLLER Controller,
    _In_ ULONG FlowId)
{
    PLIST_ENTRY Entry;

    for (Entry = Controller->Flows.Flink; Entry != &Controller->Flows; Entry = Entry->Flink)
    {
        PQWAVE_FLOW_BINDING Binding = CONTAINING_RECORD(Entry, QWAVE_FLOW_BINDING, Link);

        if (Binding->FlowId == FlowId)
            return Binding;
    }
    return NULL;
}

static
PQWAVE_SESSION
QwaveFindSession(
    _In_ PQWAVE_CONTROLLER Controller,
    _In_ const QWAVE_ADDRESS *Destination)
{
    PLIST_ENTRY Entry;

    for (Entry = Controller->Sessions.Flink; Entry != &Controller->Sessions; Entry = Entry->Flink)
    {
        PQWAVE_SESSION Session = CONTAINING_RECORD(Entry, QWAVE_SESSION, Link);

        if (QwaveSameDestination(&Session->Destination, Destination))
            return Session;
    }
    return NULL;
}

static
VOID
QwaveSendResult(
    _In_ PQWAVE_CONTROLLER Controller,
    _In_ DWORD Code,
    _In_ ULONG WorkId,
    _In_ ULONG FlowId,
    _In_ DWORD Error,
    _In_opt_ const QWAVE_FUNDAMENTALS *Fundamentals)
{
    QWAVE_WORK_RESULT Result = { 0 };

    Result.WorkId = WorkId;
    Result.FlowId = FlowId;
    Result.Error = Error;
    if (Fundamentals)
        Result.Fundamentals = *Fundamentals;
    (VOID)QwaveDeviceIo(Controller->Driver, Code, &Result, sizeof(Result), NULL, 0, NULL, INFINITE);
}

static
VOID
QwaveHandleWork(
    _In_ PQWAVE_CONTROLLER Controller,
    _In_ const QWAVE_WORK *Work)
{
    QWAVE_FUNDAMENTALS Fundamentals = { 0 };
    PQWAVE_FLOW_BINDING Binding;
    PQWAVE_SESSION Session;
    DWORD Error = ERROR_SUCCESS;

    switch (Work->Type)
    {
        case QWAVE_WORK_START_SESSION:
            Error = QwaveOpenSession(Controller, &Work->Destination, &Session);
            if (Error == ERROR_SUCCESS && Work->FlowId != 0)
            {
                Binding = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Binding));
                if (!Binding)
                {
                    QwaveReleaseSession(Session);
                    Error = ERROR_NOT_ENOUGH_MEMORY;
                }
                else
                {
                    Binding->FlowId = Work->FlowId;
                    Binding->Session = Session;
                    InsertTailList(&Controller->Flows, &Binding->Link);
                }
            }
            QwaveSendResult(Controller, IOCTL_QWAVE_SERVICE_COMPLETE, Work->WorkId, Work->FlowId, Error, NULL);
            break;

        case QWAVE_WORK_MEASURE:
            Binding = QwaveFindBinding(Controller, Work->FlowId);
            Error = Binding ? QwaveMeasure(Binding->Session, &Fundamentals) : ERROR_NOT_FOUND;
            QwaveSendResult(Controller, IOCTL_QWAVE_SERVICE_COMPLETE, Work->WorkId, Work->FlowId, Error, &Fundamentals);
            break;

        case QWAVE_WORK_MONITOR:
            Binding = QwaveFindBinding(Controller, Work->FlowId);
            if (Binding && !Binding->Monitored)
            {
                Binding->Monitored = TRUE;
                if (QwaveMeasure(Binding->Session, &Fundamentals) == ERROR_SUCCESS)
                {
                    QwaveSendResult(Controller, IOCTL_QWAVE_SERVICE_UPDATE, 0, Binding->FlowId, ERROR_SUCCESS,
                                    &Fundamentals);
                }
            }
            break;

        case QWAVE_WORK_STOP_SESSION:
            if (Work->FlowId != 0)
            {
                Binding = QwaveFindBinding(Controller, Work->FlowId);
                if (Binding)
                {
                    RemoveEntryList(&Binding->Link);
                    QwaveReleaseSession(Binding->Session);
                    HeapFree(GetProcessHeap(), 0, Binding);
                }
            }
            else
            {
                Session = QwaveFindSession(Controller, &Work->Destination);
                if (Session)
                    QwaveReleaseSession(Session);
            }
            break;

        default:
            break;
    }
}

static
BOOL
QwaveHasMonitors(
    _In_ PQWAVE_CONTROLLER Controller)
{
    PLIST_ENTRY Entry;

    for (Entry = Controller->Flows.Flink; Entry != &Controller->Flows; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, QWAVE_FLOW_BINDING, Link)->Monitored)
            return TRUE;
    }
    return FALSE;
}

static
VOID
QwaveRunMonitors(
    _In_ PQWAVE_CONTROLLER Controller)
{
    PLIST_ENTRY Entry;

    for (Entry = Controller->Flows.Flink; Entry != &Controller->Flows; Entry = Entry->Flink)
    {
        PQWAVE_FLOW_BINDING Binding = CONTAINING_RECORD(Entry, QWAVE_FLOW_BINDING, Link);
        QWAVE_FUNDAMENTALS Fundamentals;

        if (Binding->Monitored && QwaveMeasure(Binding->Session, &Fundamentals) == ERROR_SUCCESS)
            QwaveSendResult(Controller, IOCTL_QWAVE_SERVICE_UPDATE, 0, Binding->FlowId, ERROR_SUCCESS, &Fundamentals);
    }
}

static
DWORD
WINAPI
QwaveControllerThread(
    _In_ PVOID Parameter)
{
    PQWAVE_CONTROLLER Controller = Parameter;
    OVERLAPPED Overlapped = { 0 };
    HANDLE Waits[2];
    HMODULE Self = NULL;
    QWAVE_WORK Work;
    DWORD Bytes, Wait, Timeout, Error = ERROR_SUCCESS, Next = GetTickCount() + QWAVE_MONITOR_MS;
    BOOL Pending = FALSE;

    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)QwaveControllerThread, &Self);
    Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Waits[0] = Controller->StopEvent;
    Waits[1] = Overlapped.hEvent;

    while (Overlapped.hEvent)
    {
        if (!Pending)
        {
            ResetEvent(Overlapped.hEvent);
            if (DeviceIoControl(Controller->Driver, IOCTL_QWAVE_SERVICE_GET_WORK, NULL, 0, &Work, sizeof(Work), &Bytes,
                                &Overlapped))
            {
                QwaveHandleWork(Controller, &Work);
                continue;
            }
            if (GetLastError() != ERROR_IO_PENDING)
            {
                Error = GetLastError();
                break;
            }
            Pending = TRUE;
        }
        Timeout = INFINITE;
        if (QwaveHasMonitors(Controller))
            Timeout = ((LONG)(Next - GetTickCount()) > 0) ? Next - GetTickCount() : 0;
        Wait = WaitForMultipleObjects(2, Waits, FALSE, Timeout);
        if (Wait == WAIT_OBJECT_0)
            break;
        if (Wait == WAIT_OBJECT_0 + 1)
        {
            Pending = FALSE;
            if (!GetOverlappedResult(Controller->Driver, &Overlapped, &Bytes, FALSE))
            {
                Error = GetLastError();
                break;
            }
            QwaveHandleWork(Controller, &Work);
        }
        if ((LONG)(GetTickCount() - Next) >= 0)
        {
            QwaveRunMonitors(Controller);
            Next = GetTickCount() + QWAVE_MONITOR_MS;
        }
    }

    if (Pending)
    {
        CancelIoEx(Controller->Driver, &Overlapped);
        GetOverlappedResult(Controller->Driver, &Overlapped, &Bytes, TRUE);
    }
    while (!IsListEmpty(&Controller->Flows))
    {
        PQWAVE_FLOW_BINDING Binding = CONTAINING_RECORD(RemoveHeadList(&Controller->Flows), QWAVE_FLOW_BINDING, Link);

        QwaveReleaseSession(Binding->Session);
        HeapFree(GetProcessHeap(), 0, Binding);
    }
    while (!IsListEmpty(&Controller->Sessions))
    {
        PQWAVE_SESSION Session = CONTAINING_RECORD(Controller->Sessions.Flink, QWAVE_SESSION, Link);

        Session->References = 1;
        QwaveReleaseSession(Session);
    }
    if (Overlapped.hEvent)
        CloseHandle(Overlapped.hEvent);
    NtClose(Controller->Driver);
    Controller->Driver = NULL;
    CloseHandle(Controller->StopEvent);
    Controller->StopEvent = NULL;
    CloseHandle(Controller->Thread);
    Controller->Thread = NULL;
    Controller->Report(SERVICE_STOPPED, Error);
    if (Self)
        FreeLibraryAndExitThread(Self, Error);
    return Error;
}

DWORD
QwaveStartController(
    _In_ QWAVE_REPORT_STATUS Report,
    _Out_ PHANDLE ControllerHandle)
{
    PQWAVE_CONTROLLER Controller = &QwaveControllerState;
    NTSTATUS Status;
    DWORD Error;

    *ControllerHandle = NULL;
    if (Controller->Thread)
        return ERROR_SERVICE_ALREADY_RUNNING;
    InitializeListHead(&Controller->Sessions);
    InitializeListHead(&Controller->Flows);
    Controller->Report = Report;
    Controller->StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Controller->StopEvent)
        return GetLastError();
    Status = QwaveOpenDeviceByName(QWAVE_DEVICE_NAME, &Controller->Driver);
    if (!NT_SUCCESS(Status))
    {
        CloseHandle(Controller->StopEvent);
        Controller->StopEvent = NULL;
        return RtlNtStatusToDosError(Status);
    }
    Error = QwaveDeviceIo(Controller->Driver, IOCTL_QWAVE_SERVICE_REGISTER, NULL, 0, NULL, 0, NULL, INFINITE);
    if (Error == ERROR_SUCCESS)
    {
        Controller->Thread = CreateThread(NULL, 0, QwaveControllerThread, Controller, 0, NULL);
        if (!Controller->Thread)
            Error = GetLastError();
    }
    if (Error != ERROR_SUCCESS)
    {
        NtClose(Controller->Driver);
        Controller->Driver = NULL;
        CloseHandle(Controller->StopEvent);
        Controller->StopEvent = NULL;
        return Error;
    }
    *ControllerHandle = Controller->Thread;
    return ERROR_SUCCESS;
}

VOID
QwaveStopController(
    _In_opt_ HANDLE ControllerHandle)
{
    UNREFERENCED_PARAMETER(ControllerHandle);
    if (QwaveControllerState.StopEvent)
        SetEvent(QwaveControllerState.StopEvent);
}
