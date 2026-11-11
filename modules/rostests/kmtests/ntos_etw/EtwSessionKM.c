/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-mode trace sessions: clocks, trace information queries and traced events
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#define _WMIKM_
#include <ntwmi.h>

NTSTATUS NTAPI WmiStartTrace(_Inout_ PWMI_LOGGER_INFORMATION LoggerInformation);
NTSTATUS NTAPI WmiStopTrace(_Inout_ PWMI_LOGGER_INFORMATION LoggerInformation);
LONGLONG FASTCALL WmiGetClock(_In_ ULONG ClockType, _In_opt_ PVOID Context);
NTSTATUS __cdecl WmiTraceMessage(_In_ TRACEHANDLE LoggerHandle, _In_ ULONG MessageFlags,
                                 _In_ LPGUID MessageGuid, _In_ USHORT MessageNumber, ...);

static GUID KmtEtwMessageGuid = { 0x5d2f7a10, 0x83c4, 0x4e1b, { 0x9a, 0x61, 0x2b, 0x7e, 0x0c, 0x44, 0xd9, 0x31 } };

typedef struct _KMT_SESSION
{
    WMI_LOGGER_INFORMATION Info;
    WCHAR Name[64];
    ULONG LoggerId;
} KMT_SESSION, *PKMT_SESSION;

typedef struct _KMT_TRACED_EVENT
{
    EVENT_TRACE_HEADER Header;
    ULONG Data[2];
} KMT_TRACED_EVENT, *PKMT_TRACED_EVENT;

static
NTSTATUS
KmtStartSession(
    _Out_ PKMT_SESSION Session,
    _In_ PCWSTR Name,
    _In_ ULONG ClockType)
{
    NTSTATUS Status;

    RtlZeroMemory(Session, sizeof(*Session));
    RtlStringCbCopyW(Session->Name, sizeof(Session->Name), Name);

    Session->Info.Wnode.BufferSize = sizeof(Session->Info);
    Session->Info.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    Session->Info.Wnode.ClientContext = ClockType;
    Session->Info.BufferSize = 4;
    Session->Info.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    Session->Info.LoggerName.Buffer = Session->Name;
    Session->Info.LoggerName.Length = (USHORT)(wcslen(Session->Name) * sizeof(WCHAR));
    Session->Info.LoggerName.MaximumLength = sizeof(Session->Name);

    Status = WmiStartTrace(&Session->Info);
    if (NT_SUCCESS(Status))
        Session->LoggerId = (ULONG)Session->Info.Wnode.HistoricalContext;

    return Status;
}

static
VOID
KmtStopSession(
    _Inout_ PKMT_SESSION Session)
{
    NTSTATUS Status;

    Session->Info.Wnode.BufferSize = sizeof(Session->Info);
    Session->Info.Wnode.HistoricalContext = Session->LoggerId;
    Session->Info.LoggerName.Buffer = Session->Name;
    Session->Info.LoggerName.Length = (USHORT)(wcslen(Session->Name) * sizeof(WCHAR));
    Session->Info.LoggerName.MaximumLength = sizeof(Session->Name);

    Status = WmiStopTrace(&Session->Info);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static
ULONG
KmtEventsLogged(
    _In_ ULONG LoggerId)
{
    ULONG Logged = MAXULONG;
    NTSTATUS Status;

    Status = WmiQueryTraceInformation(LoggerEventsLoggedClass, &Logged, sizeof(Logged), NULL, &LoggerId);
    ok_eq_hex(Status, STATUS_SUCCESS);

    return Logged;
}

static
TRACEHANDLE
KmtTraceHandle(
    _In_ ULONG LoggerId)
{
    TRACEHANDLE Handle = 0;
    NTSTATUS Status;

    Status = WmiQueryTraceInformation(TraceHandleClass, &Handle, sizeof(Handle), NULL, &LoggerId);
    ok_eq_hex(Status, STATUS_SUCCESS);

    return Handle;
}

static
VOID
KmtInitTracedEvent(
    _Out_ PKMT_TRACED_EVENT Event,
    _In_ TRACEHANDLE Handle)
{
    RtlZeroMemory(Event, sizeof(*Event));
    Event->Header.Size = sizeof(*Event);
    Event->Header.Flags = WNODE_FLAG_TRACED_GUID;
    Event->Header.Guid = KmtEtwMessageGuid;
    Event->Header.Class.Type = EVENT_TRACE_TYPE_INFO;
    Event->Data[0] = 0x11223344;
    Event->Data[1] = 0x55667788;
    ((PWNODE_HEADER)Event)->HistoricalContext = Handle;
}

static
VOID
TestClocks(VOID)
{
    KMT_SESSION Session;
    ETW_TRACE_SESSION_SETTINGS Settings;
    LARGE_INTEGER Before, After;
    ULONG64 CyclesBefore, CyclesAfter;
    TRACEHANDLE Handle;
    LONGLONG Value;
    KIRQL OldIrql;
    NTSTATUS Status;

    /* The cycle counter clock reads the processor's own counter */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    CyclesBefore = ReadTimeStampCounter();
    Value = WmiGetClock(EVENT_TRACE_CLOCK_CPUCYCLE, NULL);
    CyclesAfter = ReadTimeStampCounter();
    KeLowerIrql(OldIrql);
    ok((ULONG64)Value >= CyclesBefore && (ULONG64)Value <= CyclesAfter,
       "cycle clock %I64u outside [%I64u, %I64u]\n", Value, CyclesBefore, CyclesAfter);

    KeQuerySystemTime(&Before);
    Value = WmiGetClock(EVENT_TRACE_CLOCK_SYSTEMTIME, NULL);
    KeQuerySystemTime(&After);
    ok(Value >= Before.QuadPart && Value <= After.QuadPart,
       "system clock %I64d outside [%I64d, %I64d]\n", Value, Before.QuadPart, After.QuadPart);

    /* A session keeps the cycle counter clock it was started with */
    Status = KmtStartSession(&Session, L"KmtEtwCycleClock", EVENT_TRACE_CLOCK_CPUCYCLE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok_eq_ulong(Session.Info.Wnode.ClientContext, (ULONG)EVENT_TRACE_CLOCK_CPUCYCLE);

    Handle = KmtTraceHandle(Session.LoggerId);
    RtlZeroMemory(&Settings, sizeof(Settings));
    Status = WmiQueryTraceInformation(TraceSessionSettingsClass, &Settings, sizeof(Settings), NULL, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Settings.ClockType, (ULONG)EVENT_TRACE_CLOCK_CPUCYCLE);

    KmtStopSession(&Session);
}

static
VOID
TestTraceInformation(VOID)
{
    KMT_SESSION SessionA, SessionB;
    ETW_TRACE_SESSION_SETTINGS Settings;
    TRACEHANDLE Handles[64];
    TRACEHANDLE HandleA, HandleB, Handle;
    UNICODE_STRING Name;
    WNODE_HEADER Wnode;
    BOOLEAN FoundA, FoundB;
    ULONG Value, Required, Lost, i;
    NTSTATUS Status;

    Status = KmtStartSession(&SessionA, L"KmtEtwSessionA", EVENT_TRACE_CLOCK_PERFCOUNTER);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = KmtStartSession(&SessionB, L"KmtEtwSessionB", EVENT_TRACE_CLOCK_PERFCOUNTER);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        KmtStopSession(&SessionA);
        return;
    }

    /* A logger identifier gives the session's handle, and the handle gives the identifier back */
    Required = 0;
    HandleA = 0;
    Status = WmiQueryTraceInformation(TraceHandleClass, &HandleA, sizeof(HandleA), &Required, &SessionA.LoggerId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Required, (ULONG)sizeof(TRACEHANDLE));
    HandleB = KmtTraceHandle(SessionB.LoggerId);
    ok(HandleA != HandleB, "two sessions share a handle\n");

    RtlZeroMemory(&Wnode, sizeof(Wnode));
    Wnode.BufferSize = sizeof(Wnode);
    Wnode.HistoricalContext = HandleA;
    Value = MAXULONG;
    Required = 0;
    Status = WmiQueryTraceInformation(TraceIdClass, &Value, sizeof(Value), &Required, &Wnode);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Value, SessionA.LoggerId);
    ok_eq_ulong(Required, (ULONG)sizeof(ULONG));

    Required = 0;
    Status = WmiQueryTraceInformation(TraceIdClass, &Value, sizeof(USHORT), &Required, &Wnode);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok_eq_ulong(Required, (ULONG)sizeof(ULONG));

    Status = WmiQueryTraceInformation(TraceIdClass, &Value, sizeof(Value), NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_MIX);

    Status = WmiQueryTraceInformation(TraceHandleClass, &Handle, sizeof(Handle), NULL, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_MIX);

    /* What an enable carried is read off the handle in the node */
    Status = WmiQueryTraceInformation(TraceEnableFlagsClass, &Value, sizeof(Value), NULL, &Wnode);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = WmiQueryTraceInformation(TraceEnableLevelClass, &Value, sizeof(Value), NULL, &Wnode);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* No global logger was started */
    Status = WmiQueryTraceInformation(GlobalLoggerHandleClass, &Handle, sizeof(Handle), NULL, NULL);
    ok_eq_hex(Status, STATUS_NOT_FOUND);

    /* Every running session */
    RtlZeroMemory(Handles, sizeof(Handles));
    Required = 0;
    Status = WmiQueryTraceInformation(AllLoggerHandlesClass, Handles, sizeof(Handles), &Required, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Required >= 2 * sizeof(TRACEHANDLE) && (Required % sizeof(TRACEHANDLE)) == 0, "Required = %lu\n", Required);
    FoundA = FoundB = FALSE;
    for (i = 0; i < Required / sizeof(TRACEHANDLE) && i < RTL_NUMBER_OF(Handles); i++)
    {
        if (Handles[i] == HandleA) FoundA = TRUE;
        if (Handles[i] == HandleB) FoundB = TRUE;
    }
    ok(FoundA, "session A missing from the handles\n");
    ok(FoundB, "session B missing from the handles\n");

    Status = WmiQueryTraceInformation(AllLoggerHandlesClass, Handles, sizeof(TRACEHANDLE), &Required, NULL);
    ok_eq_hex(Status, STATUS_MORE_ENTRIES);

    /* By name */
    RtlInitUnicodeString(&Name, L"KmtEtwSessionA");
    Handle = 0;
    Status = WmiQueryTraceInformation(TraceHandleByNameClass, &Handle, sizeof(Handle), NULL, &Name);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex64(Handle, HandleA);

    /* The settings the session runs with */
    RtlZeroMemory(&Settings, sizeof(Settings));
    Status = WmiQueryTraceInformation(TraceSessionSettingsClass, &Settings, sizeof(Settings), NULL, &HandleA);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Settings.BufferSize, 4UL);
    ok(Settings.LoggerMode & EVENT_TRACE_REAL_TIME_MODE, "LoggerMode = 0x%lx\n", Settings.LoggerMode);
    ok_eq_ulong(Settings.ClockType, (ULONG)EVENT_TRACE_CLOCK_PERFCOUNTER);

    /* Events logged and lost, counted per session */
    ok_eq_ulong(KmtEventsLogged(SessionA.LoggerId), 0UL);
    for (i = 0; i < 3; i++)
    {
        Status = WmiTraceMessage(HandleA, TRACE_MESSAGE_GUID, &KmtEtwMessageGuid, 1, &i, sizeof(i), NULL, (SIZE_T)0);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    ok_eq_ulong(KmtEventsLogged(SessionA.LoggerId), 3UL);
    ok_eq_ulong(KmtEventsLogged(SessionB.LoggerId), 0UL);

    Lost = MAXULONG;
    Status = WmiQueryTraceInformation(LoggerEventsLostClass, &Lost, sizeof(Lost), NULL, &SessionA.LoggerId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Lost, 0UL);

    /* A session that stopped no longer answers to its handle */
    KmtStopSession(&SessionB);
    Wnode.HistoricalContext = HandleB;
    Status = WmiQueryTraceInformation(TraceIdClass, &Value, sizeof(Value), NULL, &Wnode);
    ok_eq_hex(Status, STATUS_INVALID_HANDLE);

    KmtStopSession(&SessionA);
}

static
VOID
TestTracedWmiEvents(VOID)
{
    KMT_TRACED_EVENT Event;
    KMT_SESSION Session;
    TRACEHANDLE Handle;
    NTSTATUS Status;

    Status = KmtStartSession(&Session, L"KmtEtwTracedEvents", EVENT_TRACE_CLOCK_PERFCOUNTER);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Handle = KmtTraceHandle(Session.LoggerId);

    /* A traced event goes to the logger its handle names, from a buffer WMI does not take over */
    KmtInitTracedEvent(&Event, Handle);
    Status = IoWMIWriteEvent(&Event);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(KmtEventsLogged(Session.LoggerId), 1UL);

    KmtInitTracedEvent(&Event, Handle);
    Status = IoWMIWriteEvent(&Event);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(KmtEventsLogged(Session.LoggerId), 2UL);

    KmtStopSession(&Session);
}

START_TEST(EtwSessionKM)
{
    TestClocks();
    TestTraceInformation();
    TestTracedWmiEvents();
}

START_TEST(EtwSessionIrqlKM)
{
    KMT_TRACED_EVENT Event;
    KMT_SESSION Session;
    TRACEHANDLE Handle;
    NTSTATUS MessageStatus, EventStatus, HighStatus;
    ULONG Value = 0xCAFE;
    KIRQL OldIrql;
    NTSTATUS Status;

    Status = KmtStartSession(&Session, L"KmtEtwRaisedIrql", EVENT_TRACE_CLOCK_PERFCOUNTER);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Handle = KmtTraceHandle(Session.LoggerId);
    KmtInitTracedEvent(&Event, Handle);

    /* Messages and traced events are logged at the caller's IRQL, whatever it is */
    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    MessageStatus = WmiTraceMessage(Handle, TRACE_MESSAGE_GUID, &KmtEtwMessageGuid, 2, &Value, sizeof(Value), NULL, (SIZE_T)0);
    EventStatus = IoWMIWriteEvent(&Event);
    KeLowerIrql(OldIrql);
    ok_eq_hex(MessageStatus, STATUS_SUCCESS);
    ok_eq_hex(EventStatus, STATUS_SUCCESS);

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    HighStatus = WmiTraceMessage(Handle, TRACE_MESSAGE_GUID, &KmtEtwMessageGuid, 3, &Value, sizeof(Value), NULL, (SIZE_T)0);
    KeLowerIrql(OldIrql);
    ok_eq_hex(HighStatus, STATUS_SUCCESS);

    ok_eq_ulong(KmtEventsLogged(Session.LoggerId), 3UL);

    KmtStopSession(&Session);
}
