/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for TraceEvent, TraceEventInstance, TraceMessage, TraceMessageVa, CreateTraceInstanceId
 *              and GetTraceLoggerHandle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <wmistr.h>
#include <evntrace.h>

#define SESSION_NAME L"LiberNTTraceEventsTest"
#define MAX_SEEN 32
#define MARKER_SIZE 16

typedef ULONG (WINAPI *PFN_TRACE_EVENT)(TRACEHANDLE, PEVENT_TRACE_HEADER);
typedef ULONG (WINAPI *PFN_TRACE_EVENT_INSTANCE)(TRACEHANDLE, PEVENT_INSTANCE_HEADER, PEVENT_INSTANCE_INFO, PEVENT_INSTANCE_INFO);
typedef ULONG (__cdecl *PFN_TRACE_MESSAGE)(TRACEHANDLE, ULONG, LPCGUID, USHORT, ...);
typedef ULONG (WINAPI *PFN_TRACE_MESSAGE_VA)(TRACEHANDLE, ULONG, LPCGUID, USHORT, va_list);
typedef ULONG (WINAPI *PFN_CREATE_TRACE_INSTANCE_ID)(HANDLE, PEVENT_INSTANCE_INFO);
typedef TRACEHANDLE (WINAPI *PFN_GET_TRACE_LOGGER_HANDLE)(PVOID);

static PFN_TRACE_EVENT pTraceEvent;
static PFN_TRACE_EVENT_INSTANCE pTraceEventInstance;
static PFN_TRACE_MESSAGE pTraceMessage;
static PFN_TRACE_MESSAGE_VA pTraceMessageVa;
static PFN_CREATE_TRACE_INSTANCE_ID pCreateTraceInstanceId;
static PFN_GET_TRACE_LOGGER_HANDLE pGetTraceLoggerHandle;

static const GUID ControlGuid = { 0x1c3a59e4, 0x8d27, 0x4f61, { 0xb0, 0x93, 0x5e, 0x2a, 0x71, 0xc4, 0x0d, 0x86 } };
static const GUID ClassGuid = { 0x2e7f0b13, 0x64a9, 0x4c52, { 0x81, 0xd6, 0x0f, 0x39, 0xa7, 0x5b, 0xe2, 0x14 } };
static const GUID ClassGuid2 = { 0x3a94c6d8, 0x1b70, 0x4e2f, { 0x9c, 0x45, 0x62, 0xd8, 0x0e, 0xb3, 0x71, 0x5a } };
static const GUID MessageGuid = { 0x4b06e2a7, 0xc351, 0x48d9, { 0xa2, 0x7e, 0x13, 0x8f, 0x64, 0x05, 0xdb, 0x9c } };
static const GUID ZeroGuid;

static const UCHAR MarkerA[MARKER_SIZE] = "TraceEvent-in-A";
static const UCHAR MarkerB[MARKER_SIZE] = "TraceEvent-ptrB";
static const UCHAR MarkerC[MARKER_SIZE] = "TraceEvent-mofC";
static const UCHAR MarkerD[MARKER_SIZE] = "Instance-eventD";
static const UCHAR MarkerE[MARKER_SIZE] = "Instance-childE";
static const UCHAR MarkerF[MARKER_SIZE] = "TraceMessage--F";
static const UCHAR MarkerG[MARKER_SIZE] = "TraceMessageVaG";

static UCHAR BigBuffer[0x10000];

typedef struct _TEST_EVENT
{
    EVENT_TRACE_HEADER Header;
    UCHAR Data[MARKER_SIZE];
} TEST_EVENT;

typedef struct _TEST_MOF_EVENT
{
    EVENT_TRACE_HEADER Header;
    MOF_FIELD Fields[MAX_MOF_FIELDS + 1];
} TEST_MOF_EVENT;

typedef struct _TEST_INSTANCE_EVENT
{
    EVENT_INSTANCE_HEADER Header;
    UCHAR Data[MARKER_SIZE];
} TEST_INSTANCE_EVENT;

typedef struct _TEST_SESSION
{
    EVENT_TRACE_PROPERTIES Properties;
    WCHAR LoggerName[64];
    WCHAR LogFileName[MAX_PATH];
} TEST_SESSION;

typedef struct _SEEN_EVENT
{
    GUID Guid;
    UCHAR Type;
    ULONG InstanceId;
    ULONG ParentInstanceId;
    GUID ParentGuid;
    ULONG MofLength;
    UCHAR Data[64];
} SEEN_EVENT;

static SEEN_EVENT Seen[MAX_SEEN];
static ULONG SeenCount;

static ULONG WINAPI
ControlCallback(WMIDPREQUESTCODE RequestCode, PVOID Context, ULONG *BufferSize, PVOID Buffer)
{
    return ERROR_SUCCESS;
}

static VOID WINAPI
ClassicCallback(PEVENT_TRACE Event)
{
    SEEN_EVENT *Entry;

    if (SeenCount >= MAX_SEEN)
        return;
    if (!IsEqualGUID(&Event->Header.Guid, &ClassGuid) && !IsEqualGUID(&Event->Header.Guid, &ClassGuid2) &&
        !IsEqualGUID(&Event->Header.Guid, &MessageGuid))
        return;

    Entry = &Seen[SeenCount++];
    Entry->Guid = Event->Header.Guid;
    Entry->Type = Event->Header.Class.Type;
    Entry->InstanceId = Event->InstanceId;
    Entry->ParentInstanceId = Event->ParentInstanceId;
    Entry->ParentGuid = Event->ParentGuid;
    Entry->MofLength = Event->MofLength;
    if (Event->MofData)
        CopyMemory(Entry->Data, Event->MofData, min(Event->MofLength, sizeof(Entry->Data)));
}

static const SEEN_EVENT *
FindEvent(const GUID *Guid, const UCHAR *Marker, BOOL AtStart)
{
    ULONG i, Offset;

    for (i = 0; i < SeenCount; i++)
    {
        if (!IsEqualGUID(&Seen[i].Guid, Guid))
            continue;
        for (Offset = 0; Offset + MARKER_SIZE <= min(Seen[i].MofLength, sizeof(Seen[i].Data)); Offset++)
        {
            if (!memcmp(Seen[i].Data + Offset, Marker, MARKER_SIZE))
                return &Seen[i];
            if (AtStart)
                break;
        }
    }
    return NULL;
}

static VOID
InitSession(TEST_SESSION *Session, PCWSTR LogFileName)
{
    ZeroMemory(Session, sizeof(*Session));
    StringCchCopyW(Session->LogFileName, ARRAYSIZE(Session->LogFileName), LogFileName);
    Session->Properties.Wnode.BufferSize = sizeof(*Session);
    Session->Properties.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    Session->Properties.Wnode.ClientContext = 1;
    Session->Properties.BufferSize = 4;
    Session->Properties.MinimumBuffers = 4;
    Session->Properties.MaximumBuffers = 8;
    Session->Properties.LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL;
    Session->Properties.LoggerNameOffset = FIELD_OFFSET(TEST_SESSION, LoggerName);
    Session->Properties.LogFileNameOffset = FIELD_OFFSET(TEST_SESSION, LogFileName);
}

static ULONG
StartSession(TEST_SESSION *Session, PCWSTR LogFileName, PTRACEHANDLE Handle)
{
    InitSession(Session, LogFileName);
    ControlTraceW(0, SESSION_NAME, &Session->Properties, EVENT_TRACE_CONTROL_STOP);
    DeleteFileW(LogFileName);
    InitSession(Session, LogFileName);
    return StartTraceW(Handle, SESSION_NAME, &Session->Properties);
}

static ULONG
CallTraceMessageVa(TRACEHANDLE Handle, ULONG Flags, LPCGUID Guid, ULONG Number, ...)
{
    va_list Arguments;
    ULONG Error;

    va_start(Arguments, Number);
    Error = pTraceMessageVa(Handle, Flags, Guid, (USHORT)Number, Arguments);
    va_end(Arguments);
    return Error;
}

static VOID
TestForwarders(HMODULE Advapi32, HMODULE Ntdll, HMODULE KernelBase)
{
    static const struct
    {
        PCSTR Name;
        PCSTR NtdllName;
        BOOL InKernelBase;
    } Exports[] =
    {
        { "TraceEvent", "EtwLogTraceEvent", TRUE },
        { "TraceEventInstance", "EtwTraceEventInstance", FALSE },
        { "TraceMessage", "EtwTraceMessage", TRUE },
        { "TraceMessageVa", "EtwTraceMessageVa", TRUE },
        { "CreateTraceInstanceId", "EtwCreateTraceInstanceId", FALSE },
        { "GetTraceLoggerHandle", "EtwGetTraceLoggerHandle", TRUE },
    };
    FARPROC Target;
    ULONG i;

    for (i = 0; i < ARRAYSIZE(Exports); i++)
    {
        Target = GetProcAddress(Ntdll, Exports[i].NtdllName);
        ok(Target != NULL, "ntdll does not export %s\n", Exports[i].NtdllName);
        if (!Target)
            continue;
        ok(GetProcAddress(Advapi32, Exports[i].Name) == Target,
           "advapi32!%s is not ntdll!%s\n", Exports[i].Name, Exports[i].NtdllName);
        if (Exports[i].InKernelBase && KernelBase)
        {
            ok(GetProcAddress(KernelBase, Exports[i].Name) == Target,
               "kernelbase!%s is not ntdll!%s\n", Exports[i].Name, Exports[i].NtdllName);
        }
    }
}

static VOID
TestCreateTraceInstanceId(HANDLE RegHandle)
{
    EVENT_INSTANCE_INFO First, Second;
    ULONG Error;

    FillMemory(&First, sizeof(First), 0xCC);
    Error = pCreateTraceInstanceId(NULL, &First);
    ok(Error == ERROR_INVALID_PARAMETER, "NULL handle returned %lu\n", Error);

    Error = pCreateTraceInstanceId(RegHandle, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "NULL instance information returned %lu\n", Error);

    FillMemory(&First, sizeof(First), 0xCC);
    Error = pCreateTraceInstanceId(RegHandle, &First);
    ok(Error == ERROR_SUCCESS, "CreateTraceInstanceId returned %lu\n", Error);
    ok(First.RegHandle == RegHandle, "RegHandle %p, expected %p\n", First.RegHandle, RegHandle);
    ok(First.InstanceId != 0, "InstanceId is 0\n");

    FillMemory(&Second, sizeof(Second), 0xCC);
    Error = pCreateTraceInstanceId(RegHandle, &Second);
    ok(Error == ERROR_SUCCESS, "second CreateTraceInstanceId returned %lu\n", Error);
    ok(Second.RegHandle == RegHandle, "second RegHandle %p, expected %p\n", Second.RegHandle, RegHandle);
    ok(Second.InstanceId != 0 && Second.InstanceId != First.InstanceId,
       "second InstanceId %lu, first %lu\n", Second.InstanceId, First.InstanceId);
}

static VOID
TestGetTraceLoggerHandle(TRACEHANDLE Session)
{
    WNODE_HEADER Wnode;
    TRACEHANDLE Handle;

    ZeroMemory(&Wnode, sizeof(Wnode));
    Wnode.BufferSize = sizeof(Wnode);
    Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    Wnode.HistoricalContext = Session;
    SetLastError(0xdeadbeef);
    Handle = pGetTraceLoggerHandle(&Wnode);
    ok(Handle == Session, "GetTraceLoggerHandle returned %I64x, expected %I64x\n", Handle, Session);
    ok(GetLastError() == 0xdeadbeef, "success changed the last error to %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Handle = pGetTraceLoggerHandle(NULL);
    ok(Handle == (TRACEHANDLE)(LONG_PTR)INVALID_HANDLE_VALUE, "NULL buffer returned %I64x\n", Handle);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL buffer error %lu\n", GetLastError());

    Wnode.HistoricalContext = 0;
    SetLastError(0xdeadbeef);
    Handle = pGetTraceLoggerHandle(&Wnode);
    ok(Handle == (TRACEHANDLE)(LONG_PTR)INVALID_HANDLE_VALUE, "zero context returned %I64x\n", Handle);
    ok(GetLastError() == ERROR_INVALID_HANDLE, "zero context error %lu\n", GetLastError());
}

static VOID
InitEvent(TEST_EVENT *Event, const GUID *Guid, UCHAR Type, const UCHAR *Marker)
{
    ZeroMemory(Event, sizeof(*Event));
    Event->Header.Size = sizeof(*Event);
    Event->Header.Flags = WNODE_FLAG_TRACED_GUID;
    Event->Header.Guid = *Guid;
    Event->Header.Class.Type = Type;
    Event->Header.Class.Level = TRACE_LEVEL_INFORMATION;
    CopyMemory(Event->Data, Marker, MARKER_SIZE);
}

static VOID
TestTraceEvent(TRACEHANDLE Session, ULONG BufferBytes)
{
    TEST_EVENT Event;
    TEST_MOF_EVENT MofEvent;
    PEVENT_TRACE_HEADER Big;
    ULONG Error;

    InitEvent(&Event, &ClassGuid, 21, MarkerA);
    Error = pTraceEvent(Session, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "NULL event returned %lu\n", Error);
    Error = pTraceEvent(0, &Event.Header);
    ok(Error == ERROR_INVALID_PARAMETER, "NULL session returned %lu\n", Error);
    Event.Header.Size = sizeof(EVENT_TRACE_HEADER) - 1;
    Error = pTraceEvent(Session, &Event.Header);
    ok(Error == ERROR_INVALID_PARAMETER, "short Size returned %lu\n", Error);
    Event.Header.Size = sizeof(Event);
    Event.Header.Flags = 0;
    Error = pTraceEvent(Session, &Event.Header);
    ok(Error == ERROR_INVALID_FLAG_NUMBER, "missing WNODE_FLAG_TRACED_GUID returned %lu\n", Error);

    ZeroMemory(&MofEvent, sizeof(MofEvent));
    MofEvent.Header.Size = sizeof(MofEvent);
    MofEvent.Header.Flags = WNODE_FLAG_TRACED_GUID | WNODE_FLAG_USE_MOF_PTR;
    MofEvent.Header.Guid = ClassGuid;
    Error = pTraceEvent(Session, &MofEvent.Header);
    ok(Error == ERROR_INVALID_PARAMETER, "%u MOF fields returned %lu\n", MAX_MOF_FIELDS + 1, Error);

    if (BufferBytes <= MAXUSHORT)
    {
        Big = (PEVENT_TRACE_HEADER)BigBuffer;
        ZeroMemory(Big, sizeof(*Big));
        Big->Size = (USHORT)BufferBytes;
        Big->Flags = WNODE_FLAG_TRACED_GUID;
        Big->Guid = ClassGuid;
        Error = pTraceEvent(Session, Big);
        ok(Error == ERROR_MORE_DATA, "event as large as a buffer returned %lu\n", Error);
    }

    InitEvent(&Event, &ClassGuid, 21, MarkerA);
    Error = pTraceEvent(Session, &Event.Header);
    ok(Error == ERROR_SUCCESS, "TraceEvent returned %lu\n", Error);

    InitEvent(&Event, &ZeroGuid, 22, MarkerB);
    Event.Header.Flags |= WNODE_FLAG_USE_GUID_PTR;
    Event.Header.GuidPtr = (ULONGLONG)(ULONG_PTR)&ClassGuid2;
    Error = pTraceEvent(Session, &Event.Header);
    ok(Error == ERROR_SUCCESS, "TraceEvent with a GUID pointer returned %lu\n", Error);

    ZeroMemory(&MofEvent, sizeof(MofEvent));
    MofEvent.Header.Size = sizeof(EVENT_TRACE_HEADER) + 2 * sizeof(MOF_FIELD);
    MofEvent.Header.Flags = WNODE_FLAG_TRACED_GUID | WNODE_FLAG_USE_MOF_PTR;
    MofEvent.Header.Guid = ClassGuid;
    MofEvent.Header.Class.Type = 23;
    MofEvent.Fields[0].DataPtr = (ULONGLONG)(ULONG_PTR)MarkerC;
    MofEvent.Fields[0].Length = MARKER_SIZE / 2;
    MofEvent.Fields[1].DataPtr = (ULONGLONG)(ULONG_PTR)(MarkerC + MARKER_SIZE / 2);
    MofEvent.Fields[1].Length = MARKER_SIZE / 2;
    Error = pTraceEvent(Session, &MofEvent.Header);
    ok(Error == ERROR_SUCCESS, "TraceEvent with MOF pointers returned %lu\n", Error);
}

static VOID
InitInstanceEvent(TEST_INSTANCE_EVENT *Event, HANDLE RegHandle, UCHAR Type, const UCHAR *Marker)
{
    ZeroMemory(Event, sizeof(*Event));
    Event->Header.Size = sizeof(*Event);
    Event->Header.Flags = WNODE_FLAG_TRACED_GUID;
    Event->Header.RegHandle = (ULONGLONG)(ULONG_PTR)RegHandle;
    Event->Header.Class.Type = Type;
    Event->Header.Class.Level = TRACE_LEVEL_INFORMATION;
    CopyMemory(Event->Data, Marker, MARKER_SIZE);
}

static VOID
TestTraceEventInstance(TRACEHANDLE Session, ULONG BufferBytes, HANDLE RegHandle,
                       EVENT_INSTANCE_INFO *Instance, EVENT_INSTANCE_INFO *Child)
{
    TEST_INSTANCE_EVENT Event;
    EVENT_INSTANCE_INFO Empty;
    PEVENT_INSTANCE_HEADER Big;
    ULONG Error;

    ok(pCreateTraceInstanceId(RegHandle, Instance) == ERROR_SUCCESS, "CreateTraceInstanceId failed\n");
    ok(pCreateTraceInstanceId(RegHandle, Child) == ERROR_SUCCESS, "CreateTraceInstanceId failed\n");

    InitInstanceEvent(&Event, RegHandle, 31, MarkerD);
    Error = pTraceEventInstance(Session, NULL, Instance, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "NULL event returned %lu\n", Error);
    Error = pTraceEventInstance(Session, &Event.Header, NULL, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "NULL instance information returned %lu\n", Error);
    ZeroMemory(&Empty, sizeof(Empty));
    Error = pTraceEventInstance(Session, &Event.Header, &Empty, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "empty instance information returned %lu\n", Error);
    Error = pTraceEventInstance(0, &Event.Header, Instance, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "NULL session returned %lu\n", Error);
    Event.Header.Size = sizeof(EVENT_INSTANCE_HEADER) - 1;
    Error = pTraceEventInstance(Session, &Event.Header, Instance, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "short Size returned %lu\n", Error);
    Event.Header.Size = sizeof(Event);
    Event.Header.Flags = 0;
    Error = pTraceEventInstance(Session, &Event.Header, Instance, NULL);
    ok(Error == ERROR_INVALID_FLAGS, "missing WNODE_FLAG_TRACED_GUID returned %lu\n", Error);

    if (BufferBytes <= MAXUSHORT)
    {
        Big = (PEVENT_INSTANCE_HEADER)BigBuffer;
        ZeroMemory(Big, sizeof(*Big));
        Big->Size = (USHORT)BufferBytes;
        Big->Flags = WNODE_FLAG_TRACED_GUID;
        Big->RegHandle = (ULONGLONG)(ULONG_PTR)RegHandle;
        Error = pTraceEventInstance(Session, Big, Instance, NULL);
        ok(Error == ERROR_MORE_DATA, "event as large as a buffer returned %lu\n", Error);
    }

    InitInstanceEvent(&Event, RegHandle, 31, MarkerD);
    Error = pTraceEventInstance(Session, &Event.Header, Instance, NULL);
    ok(Error == ERROR_SUCCESS, "TraceEventInstance returned %lu\n", Error);

    InitInstanceEvent(&Event, RegHandle, 32, MarkerE);
    Event.Header.ParentRegHandle = (ULONGLONG)(ULONG_PTR)RegHandle;
    Error = pTraceEventInstance(Session, &Event.Header, Child, Instance);
    ok(Error == ERROR_SUCCESS, "TraceEventInstance with a parent returned %lu\n", Error);
}

static VOID
TestTraceMessage(TRACEHANDLE Session, ULONG BufferBytes)
{
    ULONG Error;

    Error = pTraceMessage(0, TRACE_MESSAGE_GUID, &MessageGuid, 41, MarkerF, (SIZE_T)MARKER_SIZE, NULL, (SIZE_T)0);
    ok(Error == ERROR_INVALID_HANDLE, "NULL session returned %lu\n", Error);
    Error = pTraceMessage(Session, TRACE_MESSAGE_GUID | TRACE_MESSAGE_COMPONENTID, &MessageGuid, 41,
                          MarkerF, (SIZE_T)MARKER_SIZE, NULL, (SIZE_T)0);
    ok(Error == ERROR_INVALID_PARAMETER, "GUID and component identifier together returned %lu\n", Error);
    Error = CallTraceMessageVa(0, TRACE_MESSAGE_GUID, &MessageGuid, 42, MarkerG, (SIZE_T)MARKER_SIZE, NULL, (SIZE_T)0);
    ok(Error == ERROR_INVALID_HANDLE, "TraceMessageVa with a NULL session returned %lu\n", Error);

    if (BufferBytes <= sizeof(BigBuffer))
    {
        Error = pTraceMessage(Session, TRACE_MESSAGE_GUID, &MessageGuid, 43, BigBuffer, (SIZE_T)BufferBytes,
                              NULL, (SIZE_T)0);
        ok(Error == ERROR_MORE_DATA, "message as large as a buffer returned %lu\n", Error);
    }

    Error = pTraceMessage(Session, TRACE_MESSAGE_GUID, &MessageGuid, 41,
                          MarkerF, (SIZE_T)(MARKER_SIZE / 2), MarkerF + MARKER_SIZE / 2, (SIZE_T)(MARKER_SIZE / 2),
                          NULL, (SIZE_T)0);
    ok(Error == ERROR_SUCCESS, "TraceMessage returned %lu\n", Error);

    Error = CallTraceMessageVa(Session, TRACE_MESSAGE_GUID | TRACE_MESSAGE_SEQUENCE | TRACE_MESSAGE_SYSTEMINFO,
                               &MessageGuid, 42, MarkerG, (SIZE_T)MARKER_SIZE, NULL, (SIZE_T)0);
    ok(Error == ERROR_SUCCESS, "TraceMessageVa returned %lu\n", Error);
}

static VOID
TestStoppedSession(TRACEHANDLE Session, HANDLE RegHandle, EVENT_INSTANCE_INFO *Instance)
{
    TEST_EVENT Event;
    TEST_INSTANCE_EVENT InstanceEvent;
    ULONG Error;

    InitEvent(&Event, &ClassGuid, 21, MarkerA);
    Error = pTraceEvent(Session, &Event.Header);
    ok(Error == ERROR_INVALID_HANDLE, "TraceEvent to a stopped session returned %lu\n", Error);

    InitInstanceEvent(&InstanceEvent, RegHandle, 31, MarkerD);
    Error = pTraceEventInstance(Session, &InstanceEvent.Header, Instance, NULL);
    ok(Error == ERROR_INVALID_HANDLE, "TraceEventInstance to a stopped session returned %lu\n", Error);

    Error = pTraceMessage(Session, TRACE_MESSAGE_GUID, &MessageGuid, 41, MarkerF, (SIZE_T)MARKER_SIZE, NULL, (SIZE_T)0);
    ok(Error == ERROR_INVALID_HANDLE, "TraceMessage to a stopped session returned %lu\n", Error);
}

static VOID
TestLoggedEvents(PCWSTR LogFileName, const EVENT_INSTANCE_INFO *Instance, const EVENT_INSTANCE_INFO *Child)
{
    EVENT_TRACE_LOGFILEW LogFile;
    const SEEN_EVENT *Event;
    TRACEHANDLE Handle;
    ULONG Error;

    ZeroMemory(&LogFile, sizeof(LogFile));
    LogFile.LogFileName = (PWSTR)LogFileName;
    LogFile.EventCallback = ClassicCallback;
    Handle = OpenTraceW(&LogFile);
    ok(Handle != INVALID_PROCESSTRACE_HANDLE, "OpenTraceW failed: %lu\n", GetLastError());
    if (Handle == INVALID_PROCESSTRACE_HANDLE)
        return;

    SeenCount = 0;
    Error = ProcessTrace(&Handle, 1, NULL, NULL);
    ok(Error == ERROR_SUCCESS, "ProcessTrace returned %lu\n", Error);
    CloseTrace(Handle);

    Event = FindEvent(&ClassGuid, MarkerA, TRUE);
    ok(Event != NULL, "TraceEvent record not found\n");
    if (Event)
    {
        ok(Event->Type == 21, "TraceEvent type %u\n", Event->Type);
        ok(Event->MofLength == MARKER_SIZE, "TraceEvent data length %lu\n", Event->MofLength);
    }

    Event = FindEvent(&ClassGuid2, MarkerB, TRUE);
    ok(Event != NULL, "TraceEvent record with a GUID pointer not found\n");
    if (Event)
        ok(Event->Type == 22, "GUID pointer record type %u\n", Event->Type);

    Event = FindEvent(&ClassGuid, MarkerC, TRUE);
    ok(Event != NULL, "TraceEvent record with MOF pointers not found\n");
    if (Event)
    {
        ok(Event->Type == 23, "MOF pointer record type %u\n", Event->Type);
        ok(Event->MofLength == MARKER_SIZE, "MOF pointer record data length %lu\n", Event->MofLength);
    }

    Event = FindEvent(&ClassGuid, MarkerD, TRUE);
    ok(Event != NULL, "TraceEventInstance record not found\n");
    if (Event)
    {
        ok(Event->Type == 31, "instance record type %u\n", Event->Type);
        ok(Event->MofLength == MARKER_SIZE, "instance record data length %lu\n", Event->MofLength);
        if (Event->InstanceId != Instance->InstanceId)
            trace("gap: instance record InstanceId %lu, expected %lu\n", Event->InstanceId, Instance->InstanceId);
    }

    Event = FindEvent(&ClassGuid, MarkerE, TRUE);
    ok(Event != NULL, "TraceEventInstance record with a parent not found\n");
    if (Event)
    {
        ok(Event->Type == 32, "child record type %u\n", Event->Type);
        if (Event->InstanceId != Child->InstanceId)
            trace("gap: child record InstanceId %lu, expected %lu\n", Event->InstanceId, Child->InstanceId);
        if (Event->ParentInstanceId != Instance->InstanceId)
            trace("gap: child record ParentInstanceId %lu, expected %lu\n", Event->ParentInstanceId, Instance->InstanceId);
        if (!IsEqualGUID(&Event->ParentGuid, &ClassGuid))
            trace("gap: child record ParentGuid is not the parent class\n");
    }

    ok(FindEvent(&MessageGuid, MarkerF, FALSE) != NULL, "TraceMessage record not found\n");
    ok(FindEvent(&MessageGuid, MarkerG, FALSE) != NULL, "TraceMessageVa record not found\n");
}

START_TEST(TraceEvents)
{
    HMODULE Advapi32 = GetModuleHandleW(L"advapi32.dll");
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
    TRACE_GUID_REGISTRATION Registrations[2];
    EVENT_INSTANCE_INFO Instance, Child;
    TEST_SESSION Session;
    WCHAR TempPath[MAX_PATH], LogFileName[MAX_PATH];
    TRACEHANDLE RegistrationHandle = 0, SessionHandle = 0;
    ULONG Error, BufferBytes;

    pTraceEvent = (PFN_TRACE_EVENT)GetProcAddress(Advapi32, "TraceEvent");
    pTraceEventInstance = (PFN_TRACE_EVENT_INSTANCE)GetProcAddress(Advapi32, "TraceEventInstance");
    pTraceMessage = (PFN_TRACE_MESSAGE)GetProcAddress(Advapi32, "TraceMessage");
    pTraceMessageVa = (PFN_TRACE_MESSAGE_VA)GetProcAddress(Advapi32, "TraceMessageVa");
    pCreateTraceInstanceId = (PFN_CREATE_TRACE_INSTANCE_ID)GetProcAddress(Advapi32, "CreateTraceInstanceId");
    pGetTraceLoggerHandle = (PFN_GET_TRACE_LOGGER_HANDLE)GetProcAddress(Advapi32, "GetTraceLoggerHandle");
    if (!pTraceEvent || !pTraceEventInstance || !pTraceMessage || !pTraceMessageVa ||
        !pCreateTraceInstanceId || !pGetTraceLoggerHandle)
    {
        skip("advapi32 lacks the classic trace provider functions\n");
        return;
    }

    TestForwarders(Advapi32, Ntdll, KernelBase);

    ZeroMemory(&Instance, sizeof(Instance));
    ZeroMemory(&Child, sizeof(Child));
    Registrations[0].Guid = &ClassGuid;
    Registrations[0].RegHandle = NULL;
    Registrations[1].Guid = &ClassGuid2;
    Registrations[1].RegHandle = NULL;
    Error = RegisterTraceGuidsW(ControlCallback, NULL, &ControlGuid, ARRAYSIZE(Registrations), Registrations,
                                NULL, NULL, &RegistrationHandle);
    ok(Error == ERROR_SUCCESS, "RegisterTraceGuidsW returned %lu\n", Error);
    if (Error != ERROR_SUCCESS)
        return;
    ok(Registrations[0].RegHandle != NULL && Registrations[1].RegHandle != NULL, "class handles not returned\n");

    TestCreateTraceInstanceId(Registrations[0].RegHandle);

    GetTempPathW(ARRAYSIZE(TempPath), TempPath);
    StringCchPrintfW(LogFileName, ARRAYSIZE(LogFileName), L"%sTraceEvents.etl", TempPath);
    Error = StartSession(&Session, LogFileName, &SessionHandle);
    ok(Error == ERROR_SUCCESS, "StartTraceW returned %lu\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        BufferBytes = Session.Properties.BufferSize * 1024;
        TestGetTraceLoggerHandle(SessionHandle);
        TestTraceEvent(SessionHandle, BufferBytes);
        TestTraceEventInstance(SessionHandle, BufferBytes, Registrations[0].RegHandle, &Instance, &Child);
        TestTraceMessage(SessionHandle, BufferBytes);

        Session.Properties.Wnode.BufferSize = sizeof(Session);
        Session.Properties.LoggerNameOffset = FIELD_OFFSET(TEST_SESSION, LoggerName);
        Session.Properties.LogFileNameOffset = FIELD_OFFSET(TEST_SESSION, LogFileName);
        Error = ControlTraceW(SessionHandle, NULL, &Session.Properties, EVENT_TRACE_CONTROL_STOP);
        ok(Error == ERROR_SUCCESS, "ControlTraceW(STOP) returned %lu\n", Error);

        TestStoppedSession(SessionHandle, Registrations[0].RegHandle, &Instance);
        TestLoggedEvents(LogFileName, &Instance, &Child);
        DeleteFileW(LogFileName);
    }

    Error = UnregisterTraceGuids(RegistrationHandle);
    ok(Error == ERROR_SUCCESS, "UnregisterTraceGuids returned %lu\n", Error);
}
