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
typedef struct _KMT_SESSION
{
    WMI_LOGGER_INFORMATION Info;
    WCHAR Name[64];
    ULONG LoggerId;
} KMT_SESSION, *PKMT_SESSION;

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

START_TEST(EtwSessionKM)
{
    TestClocks();
}
