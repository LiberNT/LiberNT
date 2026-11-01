/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for OpenTrace, ProcessTrace and CloseTrace
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <wmistr.h>
#include <evntrace.h>
#include <evntcons.h>

#define TEST_EVENTS 5
#define SESSION_NAME L"LiberNTOpenTraceTest"

static const GUID test_event_guid = { 0x5b3e6c2a, 0x1f4d, 0x4c8e, { 0x9a, 0x17, 0x3d, 0x52, 0x8e, 0x61, 0x0c, 0x94 } };
static const GUID test_session_guid = { 0x7d21a9f0, 0x6b3c, 0x4e5a, { 0x8f, 0x02, 0x44, 0x1c, 0xe7, 0x9b, 0x35, 0xd8 } };
static const GUID event_trace_guid = { 0x68fdd900, 0x4a3e, 0x11d1, { 0x84, 0xf4, 0x00, 0x00, 0xf8, 0x04, 0x64, 0xe3 } };

typedef struct
{
    EVENT_TRACE_HEADER Header;
    ULONG Value;
} TEST_EVENT;

static ULONG record_count, record_header_count, record_other_count, file_header_count;
static USHORT header_flags;
static ULONG record_values[TEST_EVENTS];
static UCHAR record_opcodes[TEST_EVENTS];
static USHORT record_flags;
static USHORT record_length;
static PVOID record_context;
static ULONG record_pid;
static ULONG classic_count, classic_header_count;
static ULONG classic_values[TEST_EVENTS];
static UCHAR classic_types[TEST_EVENTS];
static ULONG classic_length;
static ULONG buffer_calls;
static LONGLONG last_timestamp;
static BOOL timestamps_ordered;

static void WINAPI record_callback(PEVENT_RECORD record)
{
    if (IsEqualGUID(&record->EventHeader.ProviderId, &event_trace_guid))
    {
        trace("header event: opcode %u version %u level %u flags %x property %x length %u\n",
              record->EventHeader.EventDescriptor.Opcode, record->EventHeader.EventDescriptor.Version,
              record->EventHeader.EventDescriptor.Level, record->EventHeader.Flags,
              record->EventHeader.EventProperty, record->UserDataLength);
        header_flags = record->EventHeader.Flags;
        record_header_count++;
        return;
    }

    if (!IsEqualGUID(&record->EventHeader.ProviderId, &test_event_guid))
    {
        record_other_count++;
        return;
    }

    if (record->EventHeader.TimeStamp.QuadPart < last_timestamp)
        timestamps_ordered = FALSE;
    last_timestamp = record->EventHeader.TimeStamp.QuadPart;

    if (record_count < TEST_EVENTS)
    {
        record_opcodes[record_count] = record->EventHeader.EventDescriptor.Opcode;
        record_values[record_count] = (record->UserDataLength >= sizeof(ULONG) && record->UserData) ?
                                      *(ULONG *)record->UserData : 0xdeadbeef;
    }
    record_flags = record->EventHeader.Flags;
    record_length = record->UserDataLength;
    record_context = record->UserContext;
    record_pid = record->EventHeader.ProcessId;
    record_count++;
}

static void WINAPI classic_callback(PEVENT_TRACE event)
{
    if (IsEqualGUID(&event->Header.Guid, &event_trace_guid))
    {
        classic_header_count++;
        return;
    }

    if (!IsEqualGUID(&event->Header.Guid, &test_event_guid))
        return;

    if (classic_count < TEST_EVENTS)
    {
        classic_types[classic_count] = event->Header.Class.Type;
        classic_values[classic_count] = (event->MofLength >= sizeof(ULONG) && event->MofData) ?
                                        *(ULONG *)event->MofData : 0xdeadbeef;
    }
    classic_length = event->MofLength;
    classic_count++;
}

static ULONG WINAPI buffer_callback(PEVENT_TRACE_LOGFILEW logfile)
{
    buffer_calls++;
    trace("buffer callback: BuffersRead %lu Filled %lu EventsLost %lu\n", logfile->BuffersRead, logfile->Filled, logfile->EventsLost);
    return TRUE;
}

static ULONG WINAPI stop_buffer_callback(PEVENT_TRACE_LOGFILEW logfile)
{
    buffer_calls++;
    return FALSE;
}

static void test_invalid(void)
{
    EVENT_TRACE_LOGFILEW logfile;
    TRACEHANDLE handle;
    ULONG ret;

    SetLastError(0xdeadbeef);
    handle = OpenTraceW(NULL);
    ok(handle == INVALID_PROCESSTRACE_HANDLE, "OpenTraceW(NULL) returned %I64x\n", handle);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "OpenTraceW(NULL) error %lu\n", GetLastError());

    ZeroMemory(&logfile, sizeof(logfile));
    logfile.LogFileName = L"C:\\libernt-no-such-dir\\missing.etl";
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = record_callback;
    SetLastError(0xdeadbeef);
    handle = OpenTraceW(&logfile);
    ok(handle == INVALID_PROCESSTRACE_HANDLE, "missing file returned %I64x\n", handle);
    ok(GetLastError() == ERROR_PATH_NOT_FOUND, "missing file error %lu\n", GetLastError());

    ZeroMemory(&logfile, sizeof(logfile));
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = record_callback;
    SetLastError(0xdeadbeef);
    handle = OpenTraceW(&logfile);
    ok(handle == INVALID_PROCESSTRACE_HANDLE, "no names returned %I64x\n", handle);
    ok(GetLastError() == ERROR_PATH_NOT_FOUND, "no names error %lu\n", GetLastError());

    ret = ProcessTrace(NULL, 1, NULL, NULL);
    ok(ret == ERROR_INVALID_PARAMETER, "ProcessTrace(NULL) returned %lu\n", ret);

    handle = INVALID_PROCESSTRACE_HANDLE;
    ret = ProcessTrace(&handle, 0, NULL, NULL);
    ok(ret == ERROR_BAD_LENGTH, "ProcessTrace count 0 returned %lu\n", ret);

    ret = ProcessTrace(&handle, 1, NULL, NULL);
    ok(ret == ERROR_INVALID_HANDLE, "ProcessTrace invalid handle returned %lu\n", ret);

    handle = 0x12345678;
    ret = ProcessTrace(&handle, 1, NULL, NULL);
    ok(ret == ERROR_INVALID_HANDLE, "ProcessTrace bogus handle returned %lu\n", ret);

    ret = CloseTrace(INVALID_PROCESSTRACE_HANDLE);
    ok(ret == ERROR_INVALID_HANDLE, "CloseTrace invalid handle returned %lu\n", ret);

    ret = CloseTrace(0x12345678);
    ok(ret == ERROR_INVALID_HANDLE, "CloseTrace bogus handle returned %lu\n", ret);

    ZeroMemory(&logfile, sizeof(logfile));
    logfile.LoggerName = L"LiberNTNoSuchRealTimeSession";
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = record_callback;
    SetLastError(0xdeadbeef);
    handle = OpenTraceW(&logfile);
    ok(handle != INVALID_PROCESSTRACE_HANDLE, "real-time missing session: OpenTraceW failed: %lu\n", GetLastError());
    if (handle != INVALID_PROCESSTRACE_HANDLE)
    {
        ret = ProcessTrace(&handle, 1, NULL, NULL);
        ok(ret == ERROR_WMI_INSTANCE_NOT_FOUND, "ProcessTrace missing real-time session returned %lu\n", ret);
        ret = CloseTrace(handle);
        ok(ret == ERROR_SUCCESS, "CloseTrace real-time returned %lu\n", ret);
    }
}

static BOOL write_trace(PCWSTR path, ULONG *buffer_size_kb)
{
    EVENT_TRACE_PROPERTIES *props;
    TEST_EVENT event;
    TRACEHANDLE session;
    ULONG size, ret, i;

    size = sizeof(*props) + 2 * MAX_PATH * sizeof(WCHAR);
    props = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    props->Wnode.BufferSize = size;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;
    props->Wnode.Guid = test_session_guid;
    props->BufferSize = 64;
    props->MinimumBuffers = 4;
    props->MaximumBuffers = 8;
    props->LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL;
    props->LoggerNameOffset = sizeof(*props);
    props->LogFileNameOffset = sizeof(*props) + MAX_PATH * sizeof(WCHAR);
    StringCchCopyW((PWSTR)((BYTE *)props + props->LogFileNameOffset), MAX_PATH, path);

    ControlTraceW(0, SESSION_NAME, props, EVENT_TRACE_CONTROL_STOP);
    props->Wnode.BufferSize = size;
    props->LogFileNameOffset = sizeof(*props) + MAX_PATH * sizeof(WCHAR);
    props->LoggerNameOffset = sizeof(*props);
    StringCchCopyW((PWSTR)((BYTE *)props + props->LogFileNameOffset), MAX_PATH, path);

    ret = StartTraceW(&session, SESSION_NAME, props);
    ok(ret == ERROR_SUCCESS, "StartTraceW returned %lu\n", ret);
    if (ret != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, props);
        return FALSE;
    }
    *buffer_size_kb = props->BufferSize;

    for (i = 0; i < TEST_EVENTS; i++)
    {
        ZeroMemory(&event, sizeof(event));
        event.Header.Size = sizeof(event);
        event.Header.Flags = WNODE_FLAG_TRACED_GUID;
        event.Header.Guid = test_event_guid;
        event.Header.Class.Type = (UCHAR)(10 + i);
        event.Header.Class.Level = TRACE_LEVEL_INFORMATION;
        event.Value = 0x1000 + i;
        ret = TraceEvent(session, &event.Header);
        ok(ret == ERROR_SUCCESS, "TraceEvent(%lu) returned %lu\n", i, ret);
    }

    ret = ControlTraceW(session, NULL, props, EVENT_TRACE_CONTROL_STOP);
    ok(ret == ERROR_SUCCESS, "ControlTraceW(STOP) returned %lu\n", ret);
    trace("session stopped: buffers written %lu events lost %lu\n", props->BuffersWritten, props->EventsLost);

    HeapFree(GetProcessHeap(), 0, props);
    return TRUE;
}

static void test_event_record(PCWSTR path, ULONG buffer_size_kb)
{
    EVENT_TRACE_LOGFILEW logfile;
    TRACEHANDLE handle;
    ULONG ret, i;

    ZeroMemory(&logfile, sizeof(logfile));
    logfile.LogFileName = (PWSTR)path;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = record_callback;
    logfile.BufferCallback = buffer_callback;
    logfile.Context = (PVOID)0x1234;

    SetLastError(0xdeadbeef);
    handle = OpenTraceW(&logfile);
    ok(handle != INVALID_PROCESSTRACE_HANDLE, "OpenTraceW failed: %lu\n", GetLastError());
    if (handle == INVALID_PROCESSTRACE_HANDLE)
        return;

    ok(logfile.LogfileHeader.BufferSize == buffer_size_kb * 1024, "BufferSize %lu\n", logfile.LogfileHeader.BufferSize);
    ok(logfile.LogfileHeader.PointerSize == sizeof(void *), "PointerSize %lu\n", logfile.LogfileHeader.PointerSize);
    ok(logfile.LogfileHeader.NumberOfProcessors != 0, "NumberOfProcessors %lu\n", logfile.LogfileHeader.NumberOfProcessors);
    ok(logfile.LogfileHeader.LogFileMode & EVENT_TRACE_FILE_MODE_SEQUENTIAL, "LogFileMode %lx\n", logfile.LogfileHeader.LogFileMode);
    ok(logfile.LogfileHeader.BuffersWritten >= 1, "BuffersWritten %lu\n", logfile.LogfileHeader.BuffersWritten);
    ok(logfile.LogfileHeader.StartTime.QuadPart != 0, "StartTime is 0\n");
    ok(logfile.LogfileHeader.EndTime.QuadPart >= logfile.LogfileHeader.StartTime.QuadPart, "EndTime before StartTime\n");
    ok(logfile.LogfileHeader.PerfFreq.QuadPart != 0, "PerfFreq is 0\n");
    ok(logfile.LogfileHeader.ReservedFlags == 1, "ReservedFlags %lu\n", logfile.LogfileHeader.ReservedFlags);
    ok(logfile.IsKernelTrace == 0, "IsKernelTrace %lu\n", logfile.IsKernelTrace);
    trace("header: version %08lx provider %lu buffers %lu events lost %lu buffers lost %lu timer %lu\n",
          logfile.LogfileHeader.Version, logfile.LogfileHeader.ProviderVersion, logfile.LogfileHeader.BuffersWritten,
          logfile.LogfileHeader.EventsLost, logfile.LogfileHeader.BuffersLost, logfile.LogfileHeader.TimerResolution);
    trace("open: CurrentTime %I64x BuffersRead %lu BufferSize %lu Filled %lu EventsLost %lu\n",
          logfile.CurrentTime, logfile.BuffersRead, logfile.BufferSize, logfile.Filled, logfile.EventsLost);

    record_count = record_header_count = record_other_count = 0;
    buffer_calls = 0;
    last_timestamp = 0;
    timestamps_ordered = TRUE;
    ret = ProcessTrace(&handle, 1, NULL, NULL);
    ok(ret == ERROR_SUCCESS, "ProcessTrace returned %lu\n", ret);
    ok(record_count == TEST_EVENTS, "record events %lu\n", record_count);
    ok(record_header_count == file_header_count, "record header events %lu, file has %lu\n", record_header_count, file_header_count);
    ok(header_flags == (EVENT_HEADER_FLAG_CLASSIC_HEADER | EVENT_HEADER_FLAG_PROCESSOR_INDEX |
                        ((sizeof(void *) == 8) ? EVENT_HEADER_FLAG_64_BIT_HEADER : EVENT_HEADER_FLAG_32_BIT_HEADER)),
       "header flags %x\n", header_flags);
    ok(record_other_count == 0, "record other events %lu\n", record_other_count);
    ok(buffer_calls >= 1, "buffer callbacks %lu\n", buffer_calls);
    ok(timestamps_ordered, "timestamps out of order\n");
    for (i = 0; i < TEST_EVENTS && i < record_count; i++)
    {
        ok(record_opcodes[i] == 10 + i, "record %lu opcode %u\n", i, record_opcodes[i]);
        ok(record_values[i] == 0x1000 + i, "record %lu value %lx\n", i, record_values[i]);
    }
    ok(record_flags & EVENT_HEADER_FLAG_CLASSIC_HEADER, "record flags %x\n", record_flags);
    ok(record_flags & ((sizeof(void *) == 8) ? EVENT_HEADER_FLAG_64_BIT_HEADER : EVENT_HEADER_FLAG_32_BIT_HEADER),
       "record flags %x\n", record_flags);
    ok(record_length == sizeof(ULONG64), "record length %u\n", record_length);
    ok(record_context == (PVOID)0x1234, "record context %p\n", record_context);
    ok(record_pid == GetCurrentProcessId(), "record pid %lu\n", record_pid);

    ret = CloseTrace(handle);
    ok(ret == ERROR_SUCCESS, "CloseTrace returned %lu\n", ret);
    ret = CloseTrace(handle);
    ok(ret == ERROR_INVALID_HANDLE, "second CloseTrace returned %lu\n", ret);
}

static void test_classic(PCWSTR path)
{
    EVENT_TRACE_LOGFILEW logfile;
    TRACEHANDLE handle;
    ULONG ret, i;

    ZeroMemory(&logfile, sizeof(logfile));
    logfile.LogFileName = (PWSTR)path;
    logfile.EventCallback = classic_callback;

    handle = OpenTraceW(&logfile);
    ok(handle != INVALID_PROCESSTRACE_HANDLE, "OpenTraceW classic failed: %lu\n", GetLastError());
    if (handle == INVALID_PROCESSTRACE_HANDLE)
        return;

    classic_count = classic_header_count = 0;
    ret = ProcessTrace(&handle, 1, NULL, NULL);
    ok(ret == ERROR_SUCCESS, "ProcessTrace classic returned %lu\n", ret);
    ok(classic_count == TEST_EVENTS, "classic events %lu\n", classic_count);
    ok(classic_header_count == file_header_count, "classic header events %lu, file has %lu\n", classic_header_count, file_header_count);
    for (i = 0; i < TEST_EVENTS && i < classic_count; i++)
    {
        ok(classic_types[i] == 10 + i, "classic %lu type %u\n", i, classic_types[i]);
        ok(classic_values[i] == 0x1000 + i, "classic %lu value %lx\n", i, classic_values[i]);
    }
    ok(classic_length == sizeof(ULONG64), "classic length %lu\n", classic_length);

    ret = CloseTrace(handle);
    ok(ret == ERROR_SUCCESS, "CloseTrace classic returned %lu\n", ret);
}

static void test_stop_early(PCWSTR path)
{
    EVENT_TRACE_LOGFILEW logfile;
    TRACEHANDLE handle;
    ULONG ret;

    ZeroMemory(&logfile, sizeof(logfile));
    logfile.LogFileName = (PWSTR)path;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = record_callback;
    logfile.BufferCallback = stop_buffer_callback;

    handle = OpenTraceW(&logfile);
    ok(handle != INVALID_PROCESSTRACE_HANDLE, "OpenTraceW failed: %lu\n", GetLastError());
    if (handle == INVALID_PROCESSTRACE_HANDLE)
        return;

    record_count = record_header_count = 0;
    buffer_calls = 0;
    ret = ProcessTrace(&handle, 1, NULL, NULL);
    ok(ret == ERROR_CANCELLED, "ProcessTrace stopped by buffer callback returned %lu\n", ret);
    ok(buffer_calls == 1, "buffer callbacks %lu\n", buffer_calls);
    trace("stop early: records %lu headers %lu\n", record_count, record_header_count);

    ret = CloseTrace(handle);
    ok(ret == ERROR_SUCCESS, "CloseTrace returned %lu\n", ret);
}

static void test_ansi(PCSTR path)
{
    EVENT_TRACE_LOGFILEA logfile;
    TRACEHANDLE handle;
    ULONG ret;

    ZeroMemory(&logfile, sizeof(logfile));
    logfile.LogFileName = (PSTR)path;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = record_callback;

    handle = OpenTraceA(&logfile);
    ok(handle != INVALID_PROCESSTRACE_HANDLE, "OpenTraceA failed: %lu\n", GetLastError());
    if (handle == INVALID_PROCESSTRACE_HANDLE)
        return;

    record_count = 0;
    ret = ProcessTrace(&handle, 1, NULL, NULL);
    ok(ret == ERROR_SUCCESS, "ProcessTrace ansi returned %lu\n", ret);
    ok(record_count == TEST_EVENTS, "ansi events %lu\n", record_count);

    ret = CloseTrace(handle);
    ok(ret == ERROR_SUCCESS, "CloseTrace ansi returned %lu\n", ret);
}

static void dump_file(PCWSTR path, ULONG buffer_size_kb)
{
    BYTE *data;
    HANDLE file;
    DWORD read = 0, size, offset, buffer;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    size = GetFileSize(file, NULL);
    data = HeapAlloc(GetProcessHeap(), 0, size);
    ReadFile(file, data, size, &read, NULL);
    CloseHandle(file);
    trace("file size %lu\n", read);

    for (buffer = 0; (buffer + 1) * buffer_size_kb * 1024 <= read; buffer++)
    {
        BYTE *base = data + buffer * buffer_size_kb * 1024;
        DWORD saved = *(DWORD *)(base + 4);

        trace("buffer %lu: saved %lu offset %lu flag %x type %x\n", buffer, saved, *(DWORD *)(base + 0x30),
              *(USHORT *)(base + 0x34), *(USHORT *)(base + 0x36));
        for (offset = 0x48; offset + 8 <= saved && offset < buffer_size_kb * 1024; )
        {
            DWORD marker = *(DWORD *)(base + offset);
            DWORD length;

            if ((marker >> 16 & 0xff) == 1 || (marker >> 16 & 0xff) == 2)
                length = *(USHORT *)(base + offset + 4);
            else
                length = marker & 0xffff;
            trace("  record at %lx: marker %08lx next %08lx length %lu\n", offset, marker,
                  *(DWORD *)(base + offset + 4), length);
            if (!length)
                break;
            if (buffer == 0 && ((marker >> 16 & 0xff) == 1 || (marker >> 16 & 0xff) == 2) &&
                !(*(DWORD *)(base + offset + 4) & 0xff000000))
                file_header_count++;
            if ((marker >> 16 & 0xff) == 10 || (marker >> 16 & 0xff) == 20)
                ok(length == 56, "classic record length %lu\n", length);
            offset += (length + 7) & ~7;
        }
    }
    HeapFree(GetProcessHeap(), 0, data);
}

START_TEST(OpenTrace)
{
    WCHAR path[MAX_PATH];
    CHAR pathA[MAX_PATH];
    ULONG buffer_size_kb = 0;

    test_invalid();

    GetTempPathW(MAX_PATH, path);
    StringCchCatW(path, MAX_PATH, L"libernt_opentrace.etl");
    DeleteFileW(path);

    if (!write_trace(path, &buffer_size_kb))
    {
        skip("Cannot write a trace file\n");
        return;
    }

    dump_file(path, buffer_size_kb);
    test_event_record(path, buffer_size_kb);
    test_classic(path);
    test_stop_early(path);
    WideCharToMultiByte(CP_ACP, 0, path, -1, pathA, MAX_PATH, NULL, NULL);
    test_ansi(pathA);

    ok(DeleteFileW(path), "DeleteFileW failed: %lu\n", GetLastError());
}
