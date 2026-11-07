/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Checks WmiTraceMessage and WmiTraceMessageVa with logger handles that name no session
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <kmt_test.h>
#include <evntrace.h>

NTKERNELAPI NTSTATUS __cdecl WmiTraceMessage(_In_ TRACEHANDLE LoggerHandle, _In_ ULONG MessageFlags, _In_ LPCGUID MessageGuid,
                                     _In_ USHORT MessageNumber, ...);
NTKERNELAPI NTSTATUS NTAPI WmiTraceMessageVa(_In_ TRACEHANDLE LoggerHandle, _In_ ULONG MessageFlags, _In_ LPCGUID MessageGuid,
                                       _In_ USHORT MessageNumber, _In_ va_list MessageArgList);

static const GUID KmtWmiMessageGuid = { 0x6c1d2e4f, 0x3a5b, 0x4c7d, { 0x8e, 0x9f, 0x10, 0x21, 0x32, 0x43, 0x54, 0x65 } };

static NTSTATUS
KmtTraceMessageVa(
    _In_ TRACEHANDLE LoggerHandle,
    _In_ ULONG MessageFlags,
    _In_ ULONG MessageNumber,
    ...)
{
    va_list Arguments;
    NTSTATUS Status;

    va_start(Arguments, MessageNumber);
    Status = WmiTraceMessageVa(LoggerHandle, MessageFlags, &KmtWmiMessageGuid, (USHORT)MessageNumber, Arguments);
    va_end(Arguments);
    return Status;
}

START_TEST(WmiTraceMessage)
{
    static const TRACEHANDLE Handles[] = { 0, 1, 0x12345678, (TRACEHANDLE)-1 };
    ULONG Value = 0x1234;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Handles); Index++)
    {
        ok_eq_hex(WmiTraceMessage(Handles[Index], TRACE_MESSAGE_SEQUENCE | TRACE_MESSAGE_GUID,
                                  &KmtWmiMessageGuid, 10, NULL),
                  STATUS_INVALID_HANDLE);
        ok_eq_hex(WmiTraceMessage(Handles[Index], TRACE_MESSAGE_SEQUENCE | TRACE_MESSAGE_GUID,
                                  &KmtWmiMessageGuid, 11, &Value, sizeof(Value), NULL),
                  STATUS_INVALID_HANDLE);
        ok_eq_hex(KmtTraceMessageVa(Handles[Index], TRACE_MESSAGE_SEQUENCE | TRACE_MESSAGE_GUID, 12,
                                    &Value, sizeof(Value), NULL),
                  STATUS_INVALID_HANDLE);
    }
}
