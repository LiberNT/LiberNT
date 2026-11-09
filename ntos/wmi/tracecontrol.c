/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The trace sessions a kernel-mode caller starts, asks after and writes to
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "wmipriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

#define WMIP_STACK_WALK_APC_BITS 9
#define WMIP_STACK_WALK_APC_SHIFT (32 - WMIP_STACK_WALK_APC_BITS)
#define WMIP_STACK_WALK_APC_VALUES ((1UL << WMIP_STACK_WALK_APC_BITS) - 1)
#define WMIP_STACK_WALK_APC_MASK \
    ((LONG)(WMIP_STACK_WALK_APC_VALUES << WMIP_STACK_WALK_APC_SHIFT))

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
WmipLoggerRequest(
    _In_ ULONG FunctionCode,
    _Inout_ PWMI_LOGGER_INFORMATION LoggerInformation)
{
    ULONG Returned = 0;

    PAGED_CODE();

    if (LoggerInformation == NULL) return STATUS_INVALID_PARAMETER;

    if (LoggerInformation->Wnode.BufferSize < sizeof(WMI_LOGGER_INFORMATION))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return NtTraceControl(FunctionCode,
                          LoggerInformation,
                          sizeof(WMI_LOGGER_INFORMATION),
                          LoggerInformation,
                          sizeof(WMI_LOGGER_INFORMATION),
                          &Returned);
}

/* Reads the pairs of address and length a message carries off a caller's list */
static
NTSTATUS
WmipCollectMessageFields(
    _Out_writes_to_(MAX_MOF_FIELDS, *FieldCount) PMOF_FIELD Fields,
    _Out_ PULONG FieldCount,
    _In_ va_list Arguments)
{
    ULONG Count = 0;
    PVOID Address;
    ULONG Length;

    *FieldCount = 0;

    for (;;)
    {
        Address = va_arg(Arguments, PVOID);
        if (Address == NULL) break;

        Length = va_arg(Arguments, ULONG);

        if (Count == MAX_MOF_FIELDS) return STATUS_ARRAY_BOUNDS_EXCEEDED;
        if (Length > MAXUSHORT) return STATUS_BUFFER_OVERFLOW;

        Fields[Count].DataPtr = (ULONG64)(ULONG_PTR)Address;
        Fields[Count].Length = Length;
        Fields[Count].DataType = 0;
        Count++;
    }

    *FieldCount = Count;
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/* Starts a trace session described by the caller */
NTSTATUS
NTAPI
WmiStartTrace(
    _Inout_ PWMI_LOGGER_INFORMATION LoggerInformation)
{
    return WmipLoggerRequest(EtwStartLoggerCode, LoggerInformation);
}

/* Stops the session the caller names, by identifier or by name */
NTSTATUS
NTAPI
WmiStopTrace(
    _Inout_ PWMI_LOGGER_INFORMATION LoggerInformation)
{
    return WmipLoggerRequest(EtwStopLoggerCode, LoggerInformation);
}

/* Says what a session is doing without changing anything about it */
NTSTATUS
NTAPI
WmiQueryTrace(
    _Inout_ PWMI_LOGGER_INFORMATION LoggerInformation)
{
    return WmipLoggerRequest(EtwQueryLoggerCode, LoggerInformation);
}

/* Changes a running session: its file, its timers, what it collects */
NTSTATUS
NTAPI
WmiUpdateTrace(
    _Inout_ PWMI_LOGGER_INFORMATION LoggerInformation)
{
    return WmipLoggerRequest(EtwUpdateLoggerCode, LoggerInformation);
}

/* Makes a session write out everything it is still holding */
NTSTATUS
NTAPI
WmiFlushTrace(
    _Inout_ PWMI_LOGGER_INFORMATION LoggerInformation)
{
    return WmipLoggerRequest(EtwFlushLoggerCode, LoggerInformation);
}

LONGLONG
FASTCALL
WmiGetClock(
    _In_ ULONG ClockType,
    _In_opt_ PVOID Context)
{
    LARGE_INTEGER Value;

    UNREFERENCED_PARAMETER(Context);

    switch (ClockType)
    {
        case EVENT_TRACE_CLOCK_SYSTEMTIME:
            KeQuerySystemTime(&Value);
            return Value.QuadPart;

        case EVENT_TRACE_CLOCK_CPUCYCLE:
            /* TODO: read the processor's own cycle counter through the HAL */
        case EVENT_TRACE_CLOCK_RAW:
        case EVENT_TRACE_CLOCK_PERFCOUNTER:
        default:
            Value = KeQueryPerformanceCounter(NULL);
            return Value.QuadPart;
    }
}

NTSTATUS
FASTCALL
WmiTraceFastEvent(
    _In_ PWNODE_HEADER Wnode)
{
    if (Wnode == NULL) return STATUS_INVALID_PARAMETER;

    if (Wnode->BufferSize < sizeof(WNODE_HEADER))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return NtTraceEvent((HANDLE)(ULONG_PTR)Wnode->HistoricalContext,
                        ETW_NT_FLAGS_TRACE_HEADER,
                        Wnode->BufferSize,
                        (PEVENT_TRACE_HEADER)Wnode);
}

NTSTATUS
NTAPI
WmiTraceMessageVa(
    _In_ TRACEHANDLE LoggerHandle,
    _In_ ULONG MessageFlags,
    _In_ LPGUID MessageGuid,
    _In_ USHORT MessageNumber,
    _In_ va_list MessageArgList)
{
    struct
    {
        EVENT_TRACE_HEADER Header;
        MOF_FIELD Fields[MAX_MOF_FIELDS];
    } Message;
    NTSTATUS Status;
    ULONG FieldCount;
    ULONG Size;

    if (MessageGuid == NULL) return STATUS_INVALID_PARAMETER;

    Status = WmipCollectMessageFields(Message.Fields,
                                      &FieldCount,
                                      MessageArgList);
    if (!NT_SUCCESS(Status)) return Status;

    Size = sizeof(EVENT_TRACE_HEADER) + FieldCount * sizeof(MOF_FIELD);

    RtlZeroMemory(&Message.Header, sizeof(Message.Header));

    Message.Header.Size = (USHORT)Size;
    Message.Header.Flags = WNODE_FLAG_TRACED_GUID | WNODE_FLAG_USE_MOF_PTR;
    Message.Header.Guid = *MessageGuid;
    Message.Header.Class.Type = (UCHAR)MessageNumber;
    Message.Header.Class.Version = MessageNumber;

    /* TODO: carry the message flags through to the logger's message grouping */
    UNREFERENCED_PARAMETER(MessageFlags);

    return NtTraceEvent((HANDLE)(ULONG_PTR)LoggerHandle,
                        ETW_NT_FLAGS_TRACE_HEADER,
                        Size,
                        &Message.Header);
}

/* The same, for a caller that has its arguments to hand rather than a list */
NTSTATUS
__cdecl
WmiTraceMessage(
    _In_ TRACEHANDLE LoggerHandle,
    _In_ ULONG MessageFlags,
    _In_ LPGUID MessageGuid,
    _In_ USHORT MessageNumber,
    ...)
{
    va_list Arguments;
    NTSTATUS Status;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvarargs"
    va_start(Arguments, MessageNumber);
#pragma GCC diagnostic pop
    Status = WmiTraceMessageVa(LoggerHandle,
                               MessageFlags,
                               MessageGuid,
                               MessageNumber,
                               Arguments);
    va_end(Arguments);

    return Status;
}

NTSTATUS
NTAPI
WmiQueryTraceInformation(
    _In_ TRACE_INFORMATION_CLASS TraceInformationClass,
    _Out_writes_bytes_(TraceInformationLength) PVOID TraceInformation,
    _In_ ULONG TraceInformationLength,
    _Out_opt_ PULONG RequiredLength,
    _In_opt_ PVOID Buffer)
{
    WMI_LOGGER_INFORMATION Info;
    NTSTATUS Status;
    ULONG Needed;

    PAGED_CODE();

    if (TraceInformation == NULL) return STATUS_INVALID_PARAMETER;

    switch (TraceInformationClass)
    {
        case TraceHandleClass:
        case GlobalLoggerHandleClass:
        case EventLoggerHandleClass:
        case TraceHandleByNameClass:
            Needed = sizeof(TRACEHANDLE);
            break;

        case TraceIdClass:
            Needed = sizeof(GUID);
            break;

        case TraceEnableFlagsClass:
        case TraceEnableLevelClass:
        case LoggerEventsLostClass:
        case LoggerEventsLoggedClass:
            Needed = sizeof(ULONG);
            break;

        case TraceSessionSettingsClass:
            Needed = sizeof(ETW_TRACE_SESSION_SETTINGS);
            break;

        default:
            return STATUS_INVALID_INFO_CLASS;
    }

    if (RequiredLength != NULL) *RequiredLength = Needed;

    if (TraceInformationLength < Needed) return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(&Info, sizeof(Info));
    Info.Wnode.BufferSize = sizeof(Info);

    if (TraceInformationClass == TraceHandleByNameClass)
    {
        if (Buffer == NULL) return STATUS_INVALID_PARAMETER;

        Info.LoggerName = *(PUNICODE_STRING)Buffer;
    }
    else
    {
        if (Buffer == NULL) return STATUS_INVALID_PARAMETER;

        Info.Wnode.HistoricalContext = *(PTRACEHANDLE)Buffer;
    }

    Status = WmipLoggerRequest(EtwQueryLoggerCode, &Info);
    if (!NT_SUCCESS(Status)) return Status;

    switch (TraceInformationClass)
    {
        case TraceHandleClass:
        case GlobalLoggerHandleClass:
        case EventLoggerHandleClass:
        case TraceHandleByNameClass:
            *(PTRACEHANDLE)TraceInformation = Info.Wnode.HistoricalContext;
            break;

        case TraceIdClass:
            *(LPGUID)TraceInformation = Info.Wnode.Guid;
            break;

        case TraceEnableFlagsClass:
            *(PULONG)TraceInformation = Info.EnableFlags;
            break;

        case TraceEnableLevelClass:
            /* TODO: the enable level a provider was turned on with */
            *(PULONG)TraceInformation = 0;
            break;

        case LoggerEventsLostClass:
            *(PULONG)TraceInformation = Info.EventsLost;
            break;

        case LoggerEventsLoggedClass:
            *(PULONG)TraceInformation = Info.BuffersWritten;
            break;

        case TraceSessionSettingsClass:
        {
            PETW_TRACE_SESSION_SETTINGS Settings = TraceInformation;

            Settings->Version = 1;
            Settings->BufferSize = Info.BufferSize;
            Settings->MinimumBuffers = Info.MinimumBuffers;
            Settings->MaximumBuffers = Info.MaximumBuffers;
            Settings->LoggerMode = Info.LogFileMode;
            Settings->FlushTimer = Info.FlushTimer;
            Settings->FlushThreshold = (ULONG)Info.FlushThreshold;
            Settings->ClockType = Info.Wnode.ClientContext;
            break;
        }

        default:
            return STATUS_INVALID_INFO_CLASS;
    }

    return STATUS_SUCCESS;
}

ULONG
NTAPI
EtwpDisableStackWalkApc(VOID)
{
    PKTHREAD Thread = KeGetCurrentThread();
    LONG Previous;

    Previous = InterlockedOr((volatile LONG *)&Thread->ThreadFlags,
                             WMIP_STACK_WALK_APC_MASK);

    return ((ULONG)Previous >> WMIP_STACK_WALK_APC_SHIFT) &
           WMIP_STACK_WALK_APC_VALUES;
}

/* Puts the stack-walk flags back the way a matching disable found them */
VOID
NTAPI
EtwpReenableStackWalkApc(
    _In_ ULONG PreviousState)
{
    PKTHREAD Thread = KeGetCurrentThread();
    LONG Wanted;
    LONG Current;
    LONG Seen;

    Wanted = (LONG)((PreviousState & WMIP_STACK_WALK_APC_VALUES) <<
                    WMIP_STACK_WALK_APC_SHIFT);

    Current = ReadNoFence((volatile LONG *)&Thread->ThreadFlags);

    for (;;)
    {
        Seen = InterlockedCompareExchange(
                   (volatile LONG *)&Thread->ThreadFlags,
                   (Current & ~WMIP_STACK_WALK_APC_MASK) | Wanted,
                   Current);

        if (Seen == Current) return;

        Current = Seen;
    }
}

/* EOF */
