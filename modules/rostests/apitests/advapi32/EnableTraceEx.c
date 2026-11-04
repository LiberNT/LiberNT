/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for EnableTraceEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <wmistr.h>
#include <evntprov.h>
#include <evntrace.h>
#include <strsafe.h>

#define SESSION_NAME L"LiberNT EnableTraceEx test"

typedef ULONG (WINAPI *PFN_ENABLE_TRACE_EX)(LPCGUID, LPCGUID, TRACEHANDLE, ULONG, UCHAR, ULONGLONG, ULONGLONG, ULONG,
                                            PEVENT_FILTER_DESCRIPTOR);

static const GUID ProviderGuid = {0x5e0b3c71, 0x2d4a, 0x4f8e, {0x9b, 0x61, 0x7a, 0x13, 0xc2, 0x58, 0x04, 0xd9}};
static const GUID SessionGuid = {0x9c4f6a12, 0x81e3, 0x4b07, {0xa5, 0x2c, 0x3d, 0x96, 0x0e, 0x71, 0xb8, 0x4f}};

static void
ResetProperties(
    _Out_ PEVENT_TRACE_PROPERTIES Properties,
    _In_ ULONG Size,
    _In_ PCWSTR Path)
{
    ZeroMemory(Properties, Size);
    Properties->Wnode.BufferSize = Size;
    Properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    Properties->Wnode.ClientContext = 1;
    Properties->Wnode.Guid = SessionGuid;
    Properties->BufferSize = 64;
    Properties->MinimumBuffers = 4;
    Properties->MaximumBuffers = 8;
    Properties->LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL;
    Properties->LoggerNameOffset = sizeof(*Properties);
    Properties->LogFileNameOffset = sizeof(*Properties) + MAX_PATH * sizeof(WCHAR);
    StringCchCopyW((PWSTR)((PBYTE)Properties + Properties->LogFileNameOffset), MAX_PATH, Path);
}

static void
TestSession(
    _In_ PFN_ENABLE_TRACE_EX pEnableTraceEx,
    _In_ TRACEHANDLE Session)
{
    EVENT_FILTER_DESCRIPTOR Filter;
    ULONG Status;

    Status = pEnableTraceEx(&ProviderGuid, NULL, Session, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
                            0x10, 0, 0, NULL);
    ok_long(Status, ERROR_SUCCESS);
    Status = pEnableTraceEx(&ProviderGuid, &SessionGuid, Session, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                            TRACE_LEVEL_INFORMATION, 0x30, 0x10, 0, NULL);
    ok_long(Status, ERROR_SUCCESS);
    Status = pEnableTraceEx(&ProviderGuid, NULL, Session, EVENT_CONTROL_CODE_CAPTURE_STATE, TRACE_LEVEL_VERBOSE, 0, 0,
                            0, NULL);
    ok_long(Status, ERROR_SUCCESS);
    Status = pEnableTraceEx(&ProviderGuid, NULL, Session, 3, TRACE_LEVEL_VERBOSE, 0, 0, 0, NULL);
    ok_long(Status, ERROR_INVALID_PARAMETER);

    ZeroMemory(&Filter, sizeof(Filter));
    Status = pEnableTraceEx(&ProviderGuid, NULL, Session, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE, 0, 0,
                            0, &Filter);
    trace("enable with an empty filter descriptor returned %lu\n", Status);

    Status = pEnableTraceEx(&ProviderGuid, NULL, Session, EVENT_CONTROL_CODE_DISABLE_PROVIDER, 0, 0, 0, 0, NULL);
    ok_long(Status, ERROR_SUCCESS);
}

START_TEST(EnableTraceEx)
{
    PFN_ENABLE_TRACE_EX pEnableTraceEx;
    PEVENT_TRACE_PROPERTIES Properties;
    WCHAR Path[MAX_PATH];
    TRACEHANDLE Session;
    ULONG Size, Status;

    pEnableTraceEx = (PFN_ENABLE_TRACE_EX)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "EnableTraceEx");
    ok(pEnableTraceEx != NULL, "EnableTraceEx is not exported\n");
    if (!pEnableTraceEx)
        return;

    Status = pEnableTraceEx(NULL, NULL, 1, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE, 0, 0, 0, NULL);
    ok_long(Status, ERROR_INVALID_PARAMETER);
    Status = pEnableTraceEx(&ProviderGuid, NULL, 0, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE, 0, 0, 0,
                            NULL);
    ok_long(Status, ERROR_INVALID_PARAMETER);

    if (!GetTempPathW(ARRAYSIZE(Path), Path) || FAILED(StringCchCatW(Path, ARRAYSIZE(Path), L"enabletraceex.etl")))
    {
        skip("No temporary path\n");
        return;
    }
    Size = sizeof(*Properties) + 2 * MAX_PATH * sizeof(WCHAR);
    Properties = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Properties)
    {
        skip("Out of memory\n");
        return;
    }

    ResetProperties(Properties, Size, Path);
    ControlTraceW(0, SESSION_NAME, Properties, EVENT_TRACE_CONTROL_STOP);
    ResetProperties(Properties, Size, Path);
    Status = StartTraceW(&Session, SESSION_NAME, Properties);
    if (Status == ERROR_ACCESS_DENIED)
        skip("Starting a trace session needs administrator rights\n");
    else
        ok_long(Status, ERROR_SUCCESS);
    if (Status == ERROR_SUCCESS)
    {
        TestSession(pEnableTraceEx, Session);
        Status = ControlTraceW(Session, NULL, Properties, EVENT_TRACE_CONTROL_STOP);
        ok_long(Status, ERROR_SUCCESS);
        DeleteFileW(Path);
    }
    HeapFree(GetProcessHeap(), 0, Properties);
}
