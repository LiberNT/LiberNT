/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     advapi32.dll event tracing consumer functions
 * COPYRIGHT:   Copyright 2017 Mark Jansen (mark.jansen@reactos.org)
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <advapi32.h>
#include <wmistr.h>
#include <evntrace.h>
#include <evntcons.h>
#include <ntwmi.h>

WINE_DEFAULT_DEBUG_CHANNEL(advapi);

#define ETWP_MAX_PROCESS_HANDLES 64

C_ASSERT(FIELD_OFFSET(EVENT_TRACE_LOGFILEA, CurrentTime) == FIELD_OFFSET(EVENT_TRACE_LOGFILEW, CurrentTime));
C_ASSERT(FIELD_OFFSET(EVENT_TRACE_LOGFILEA, LogfileHeader) == FIELD_OFFSET(EVENT_TRACE_LOGFILEW, LogfileHeader));
C_ASSERT(FIELD_OFFSET(EVENT_TRACE_LOGFILEA, BufferCallback) == FIELD_OFFSET(EVENT_TRACE_LOGFILEW, BufferCallback));
C_ASSERT(FIELD_OFFSET(EVENT_TRACE_LOGFILEA, EventCallback) == FIELD_OFFSET(EVENT_TRACE_LOGFILEW, EventCallback));
C_ASSERT(FIELD_OFFSET(EVENT_TRACE_LOGFILEA, Context) == FIELD_OFFSET(EVENT_TRACE_LOGFILEW, Context));
C_ASSERT(sizeof(EVENT_TRACE_LOGFILEA) == sizeof(EVENT_TRACE_LOGFILEW));

typedef struct _ETWP_TRACE
{
    LIST_ENTRY Entry;
    TRACEHANDLE Handle;
    PEVENT_TRACE_LOGFILEW Logfile;
    BOOL RealTime;
    BOOL Closing;
    LONG Busy;
    PWSTR LoggerName;
    HANDLE File;
    ULONG BufferSize;
    ULONG BufferCount;
    ULONG BufferIndex;
    PUCHAR Buffer;
    ULONG Used;
    ULONG Offset;
    ULONG ClockType;
    ULONG CpuSpeedInMHz;
    LONGLONG BaseRaw;
    LONGLONG BaseTime;
    LONGLONG Frequency;
    ULONG EventsLost;
    BOOL Pending;
    PUCHAR Record;
    ULONG RecordSize;
    UCHAR HeaderType;
    LONGLONG RawTime;
} ETWP_TRACE, *PETWP_TRACE;

static RTL_SRWLOCK EtwpTraceLock = RTL_SRWLOCK_INIT;
static LIST_ENTRY EtwpTraces = { &EtwpTraces, &EtwpTraces };
static TRACEHANDLE EtwpNextHandle = 0x100;

static const GUID EtwpEventTraceGuid =
    { 0x68fdd900, 0x4a3e, 0x11d1, { 0x84, 0xf4, 0x00, 0x00, 0xf8, 0x04, 0x64, 0xe3 } };

static PETWP_TRACE
EtwpFindTrace(TRACEHANDLE Handle)
{
    PLIST_ENTRY Entry;
    PETWP_TRACE Trace;

    for (Entry = EtwpTraces.Flink; Entry != &EtwpTraces; Entry = Entry->Flink)
    {
        Trace = CONTAINING_RECORD(Entry, ETWP_TRACE, Entry);
        if (Trace->Handle == Handle && !Trace->Closing)
            return Trace;
    }
    return NULL;
}

static VOID
EtwpFreeTrace(PETWP_TRACE Trace)
{
    if (Trace->File && Trace->File != INVALID_HANDLE_VALUE)
        CloseHandle(Trace->File);
    HeapFree(GetProcessHeap(), 0, Trace->Buffer);
    HeapFree(GetProcessHeap(), 0, Trace->LoggerName);
    HeapFree(GetProcessHeap(), 0, Trace);
}

static BOOL
EtwpReadBuffer(PETWP_TRACE Trace, ULONG Index)
{
    PWMI_BUFFER_HEADER Header;
    LARGE_INTEGER Offset;
    DWORD Read;

    Offset.QuadPart = (LONGLONG)Index * Trace->BufferSize;
    if (!SetFilePointerEx(Trace->File, Offset, NULL, FILE_BEGIN) ||
        !ReadFile(Trace->File, Trace->Buffer, Trace->BufferSize, &Read, NULL) ||
        Read != Trace->BufferSize)
    {
        return FALSE;
    }

    Header = (PWMI_BUFFER_HEADER)Trace->Buffer;
    if (Header->SavedOffset < sizeof(WMI_BUFFER_HEADER) || Header->SavedOffset > Trace->BufferSize)
        return FALSE;

    Trace->Used = Header->SavedOffset;
    Trace->Offset = sizeof(WMI_BUFFER_HEADER);
    return TRUE;
}

static BOOL
EtwpParseRecord(PETWP_TRACE Trace)
{
    ULONG Marker, Size;
    UCHAR HeaderType;

    while (Trace->Offset + sizeof(ULONG) <= Trace->Used)
    {
        Marker = *(PULONG)(Trace->Buffer + Trace->Offset);
        if (Marker == MAXULONG)
            break;
        if ((Marker & (TRACE_HEADER_FLAG | TRACE_HEADER_EVENT_TRACE)) != (TRACE_HEADER_FLAG | TRACE_HEADER_EVENT_TRACE))
            break;

        HeaderType = (UCHAR)((Marker & TRACE_HEADER_ENUM_MASK) >> 16);
        if (HeaderType == TRACE_HEADER_TYPE_SYSTEM32 || HeaderType == TRACE_HEADER_TYPE_SYSTEM64)
        {
            PSYSTEM_TRACE_HEADER System = (PSYSTEM_TRACE_HEADER)(Trace->Buffer + Trace->Offset);

            if (Trace->Offset + sizeof(SYSTEM_TRACE_HEADER) > Trace->Used)
                break;
            Size = System->Packet.Size;
            if (Size < sizeof(SYSTEM_TRACE_HEADER) || Trace->Offset + Size > Trace->Used)
                break;
            Trace->RawTime = System->SystemTime.QuadPart;
        }
        else if (HeaderType == TRACE_HEADER_TYPE_FULL_HEADER32 || HeaderType == TRACE_HEADER_TYPE_FULL_HEADER64)
        {
            PEVENT_TRACE_HEADER Event = (PEVENT_TRACE_HEADER)(Trace->Buffer + Trace->Offset);

            if (Trace->Offset + sizeof(EVENT_TRACE_HEADER) > Trace->Used)
                break;
            Size = Event->Size;
            if (Size < sizeof(EVENT_TRACE_HEADER) || Trace->Offset + Size > Trace->Used)
                break;
            Trace->RawTime = Event->TimeStamp.QuadPart;
        }
        else
        {
            FIXME("Unsupported trace header type %u\n", HeaderType);
            break;
        }

        Trace->Record = Trace->Buffer + Trace->Offset;
        Trace->RecordSize = Size;
        Trace->HeaderType = HeaderType;
        Trace->Offset += ALIGN_UP_BY(Size, sizeof(ULONG64));
        Trace->Pending = TRUE;
        return TRUE;
    }

    Trace->Offset = Trace->Used;
    Trace->Pending = FALSE;
    return FALSE;
}

static LONGLONG
EtwpConvertTime(PETWP_TRACE Trace, LONGLONG Raw)
{
    LONGLONG Delta;

    if (Trace->Logfile->ProcessTraceMode & PROCESS_TRACE_MODE_RAW_TIMESTAMP)
        return Raw;

    switch (Trace->ClockType)
    {
        case EVENT_TRACE_CLOCK_SYSTEMTIME:
            return Raw;

        case EVENT_TRACE_CLOCK_CPUCYCLE:
            if (!Trace->CpuSpeedInMHz)
                return Raw;
            Delta = Raw - Trace->BaseRaw;
            return Trace->BaseTime + (Delta / Trace->CpuSpeedInMHz) * 10 + ((Delta % Trace->CpuSpeedInMHz) * 10) / Trace->CpuSpeedInMHz;

        default:
            if (!Trace->Frequency)
                return Raw;
            Delta = Raw - Trace->BaseRaw;
            return Trace->BaseTime + (Delta / Trace->Frequency) * 10000000 +
                   ((Delta % Trace->Frequency) * 10000000) / Trace->Frequency;
    }
}

static VOID
EtwpDeliverRecord(PETWP_TRACE Trace)
{
    PEVENT_TRACE_LOGFILEW Logfile = Trace->Logfile;
    PWMI_BUFFER_HEADER Buffer = (PWMI_BUFFER_HEADER)Trace->Buffer;
    EVENT_RECORD Record;
    EVENT_TRACE Event;
    GUID Guid;
    UCHAR Type, Level;
    USHORT Version;
    ULONG ThreadId, ProcessId, KernelTime, UserTime, HeaderSize;
    LONGLONG Time;

    if (Trace->HeaderType == TRACE_HEADER_TYPE_SYSTEM32 || Trace->HeaderType == TRACE_HEADER_TYPE_SYSTEM64)
    {
        PSYSTEM_TRACE_HEADER System = (PSYSTEM_TRACE_HEADER)Trace->Record;

        if (System->Packet.Group != EVENT_TRACE_GROUP_HEADER)
            return;
        Guid = EtwpEventTraceGuid;
        Type = System->Packet.Type;
        Level = 0;
        Version = System->Version;
        ThreadId = System->ThreadId;
        ProcessId = System->ProcessId;
        KernelTime = System->KernelTime;
        UserTime = System->UserTime;
        HeaderSize = sizeof(SYSTEM_TRACE_HEADER);
    }
    else
    {
        PEVENT_TRACE_HEADER Header = (PEVENT_TRACE_HEADER)Trace->Record;

        Guid = Header->Guid;
        Type = Header->Class.Type;
        Level = Header->Class.Level;
        Version = Header->Class.Version;
        ThreadId = Header->ThreadId;
        ProcessId = Header->ProcessId;
        KernelTime = Header->KernelTime;
        UserTime = Header->UserTime;
        HeaderSize = sizeof(EVENT_TRACE_HEADER);
    }

    Time = EtwpConvertTime(Trace, Trace->RawTime);
    Logfile->CurrentTime = Time;

    if (Logfile->ProcessTraceMode & PROCESS_TRACE_MODE_EVENT_RECORD)
    {
        if (!Logfile->EventRecordCallback)
            return;

        RtlZeroMemory(&Record, sizeof(Record));
        Record.EventHeader.Size = sizeof(EVENT_HEADER);
        Record.EventHeader.HeaderType = Trace->HeaderType;
        Record.EventHeader.Flags = EVENT_HEADER_FLAG_CLASSIC_HEADER |
            ((Trace->HeaderType == TRACE_HEADER_TYPE_SYSTEM32 || Trace->HeaderType == TRACE_HEADER_TYPE_FULL_HEADER32) ?
             EVENT_HEADER_FLAG_32_BIT_HEADER : EVENT_HEADER_FLAG_64_BIT_HEADER);
        if (Buffer->BufferFlag & ETW_BUFFER_FLAG_PROC_INDEX)
            Record.EventHeader.Flags |= EVENT_HEADER_FLAG_PROCESSOR_INDEX;
        Record.EventHeader.ThreadId = ThreadId;
        Record.EventHeader.ProcessId = ProcessId;
        Record.EventHeader.TimeStamp.QuadPart = Time;
        Record.EventHeader.ProviderId = Guid;
        Record.EventHeader.EventDescriptor.Opcode = Type;
        Record.EventHeader.EventDescriptor.Level = Level;
        Record.EventHeader.EventDescriptor.Version = (UCHAR)Version;
        Record.EventHeader.KernelTime = KernelTime;
        Record.EventHeader.UserTime = UserTime;
        Record.BufferContext = Buffer->ClientContext;
        Record.UserDataLength = (USHORT)(Trace->RecordSize - HeaderSize);
        Record.UserData = Record.UserDataLength ? Trace->Record + HeaderSize : NULL;
        Record.UserContext = Logfile->Context;
        Logfile->EventRecordCallback(&Record);
        return;
    }

    RtlZeroMemory(&Event, sizeof(Event));
    Event.Header.Size = (USHORT)Trace->RecordSize;
    Event.Header.HeaderType = Trace->HeaderType;
    Event.Header.MarkerFlags = (UCHAR)((TRACE_HEADER_FLAG | TRACE_HEADER_EVENT_TRACE) >> 24);
    Event.Header.Class.Type = Type;
    Event.Header.Class.Level = Level;
    Event.Header.Class.Version = Version;
    Event.Header.ThreadId = ThreadId;
    Event.Header.ProcessId = ProcessId;
    Event.Header.TimeStamp.QuadPart = Time;
    Event.Header.Guid = Guid;
    Event.Header.KernelTime = KernelTime;
    Event.Header.UserTime = UserTime;
    Event.MofLength = Trace->RecordSize - HeaderSize;
    Event.MofData = Event.MofLength ? Trace->Record + HeaderSize : NULL;
    Event.BufferContext = Buffer->ClientContext;
    Logfile->CurrentEvent = Event;
    if (Logfile->EventCallback)
        Logfile->EventCallback(&Event);
}

static BOOL
EtwpAdvance(PETWP_TRACE Trace, PULONG Error)
{
    PEVENT_TRACE_LOGFILEW Logfile = Trace->Logfile;

    while (!EtwpParseRecord(Trace))
    {
        Logfile->BuffersRead++;
        Logfile->Filled = Trace->Used;
        Logfile->EventsLost = Trace->EventsLost;
        if (Logfile->BufferCallback && !Logfile->BufferCallback(Logfile))
        {
            *Error = ERROR_CANCELLED;
            return FALSE;
        }

        if (++Trace->BufferIndex >= Trace->BufferCount || !EtwpReadBuffer(Trace, Trace->BufferIndex))
            return TRUE;
    }
    return TRUE;
}

static ULONG
EtwpOpenFile(PETWP_TRACE Trace, LPCWSTR FileName)
{
    PEVENT_TRACE_LOGFILEW Logfile = Trace->Logfile;
    PSYSTEM_TRACE_HEADER System;
    PUCHAR Data;
    WMI_BUFFER_HEADER Header;
    LARGE_INTEGER FileSize;
    ULONG PointerSize, Available, Zone, Boot;
    DWORD Read;

    Trace->File = CreateFileW(FileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (Trace->File == INVALID_HANDLE_VALUE)
        return GetLastError();

    if (!ReadFile(Trace->File, &Header, sizeof(Header), &Read, NULL) || Read != sizeof(Header) ||
        Header.BufferSize < sizeof(WMI_BUFFER_HEADER) + sizeof(SYSTEM_TRACE_HEADER) ||
        !GetFileSizeEx(Trace->File, &FileSize) || FileSize.QuadPart < Header.BufferSize)
    {
        return ERROR_BAD_FORMAT;
    }

    Trace->BufferSize = Header.BufferSize;
    Trace->BufferCount = (ULONG)(FileSize.QuadPart / Header.BufferSize);
    Trace->Buffer = HeapAlloc(GetProcessHeap(), 0, Trace->BufferSize);
    if (!Trace->Buffer)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!EtwpReadBuffer(Trace, 0))
        return ERROR_BAD_FORMAT;

    System = (PSYSTEM_TRACE_HEADER)(Trace->Buffer + sizeof(WMI_BUFFER_HEADER));
    if ((System->Marker & (TRACE_HEADER_FLAG | TRACE_HEADER_EVENT_TRACE)) != (TRACE_HEADER_FLAG | TRACE_HEADER_EVENT_TRACE) ||
        System->Packet.Group != EVENT_TRACE_GROUP_HEADER ||
        System->Packet.Size < sizeof(SYSTEM_TRACE_HEADER) + FIELD_OFFSET(TRACE_LOGFILE_HEADER, LoggerName) ||
        sizeof(WMI_BUFFER_HEADER) + System->Packet.Size > Trace->Used)
    {
        return ERROR_BAD_FORMAT;
    }

    Data = (PUCHAR)(System + 1);
    Available = System->Packet.Size - sizeof(SYSTEM_TRACE_HEADER);
    RtlZeroMemory(&Logfile->LogfileHeader, sizeof(Logfile->LogfileHeader));
    RtlCopyMemory(&Logfile->LogfileHeader, Data, FIELD_OFFSET(TRACE_LOGFILE_HEADER, LoggerName));

    PointerSize = Logfile->LogfileHeader.PointerSize;
    if (PointerSize != sizeof(ULONG) && PointerSize != sizeof(ULONG64))
        return ERROR_BAD_FORMAT;
    Zone = FIELD_OFFSET(TRACE_LOGFILE_HEADER, LoggerName) + 2 * PointerSize;
    Boot = ALIGN_UP_BY(Zone + sizeof(TIME_ZONE_INFORMATION), sizeof(LARGE_INTEGER));
    if (Boot + 4 * sizeof(LARGE_INTEGER) > Available)
        return ERROR_BAD_FORMAT;
    RtlCopyMemory(&Logfile->LogfileHeader.TimeZone, Data + Zone, sizeof(TIME_ZONE_INFORMATION));
    RtlCopyMemory(&Logfile->LogfileHeader.BootTime, Data + Boot,
                  sizeof(TRACE_LOGFILE_HEADER) - FIELD_OFFSET(TRACE_LOGFILE_HEADER, BootTime));

    Trace->ClockType = Logfile->LogfileHeader.ReservedFlags;
    Trace->CpuSpeedInMHz = Logfile->LogfileHeader.CpuSpeedInMHz;
    Trace->Frequency = Logfile->LogfileHeader.PerfFreq.QuadPart;
    Trace->BaseTime = Logfile->LogfileHeader.StartTime.QuadPart;
    Trace->BaseRaw = System->SystemTime.QuadPart;
    Trace->EventsLost = Logfile->LogfileHeader.EventsLost;
    return ERROR_SUCCESS;
}

static TRACEHANDLE
EtwpOpenTrace(PEVENT_TRACE_LOGFILEW Logfile, LPCWSTR LogFileName, LPCWSTR LoggerName)
{
    PETWP_TRACE Trace;
    ULONG Error = ERROR_SUCCESS;
    SIZE_T Length;

    Trace = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Trace));
    if (!Trace)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_PROCESSTRACE_HANDLE;
    }
    Trace->Logfile = Logfile;
    Trace->File = INVALID_HANDLE_VALUE;

    Logfile->CurrentTime = 0;
    Logfile->BuffersRead = 0;
    Logfile->BufferSize = 0;
    Logfile->Filled = 0;
    Logfile->EventsLost = 0;

    if (Logfile->ProcessTraceMode & PROCESS_TRACE_MODE_REAL_TIME)
    {
        if (!LoggerName || !*LoggerName)
        {
            Error = ERROR_INVALID_PARAMETER;
        }
        else
        {
            Length = (wcslen(LoggerName) + 1) * sizeof(WCHAR);
            Trace->LoggerName = HeapAlloc(GetProcessHeap(), 0, Length);
            if (Trace->LoggerName)
                RtlCopyMemory(Trace->LoggerName, LoggerName, Length);
            else
                Error = ERROR_NOT_ENOUGH_MEMORY;
            Trace->RealTime = TRUE;
        }
    }
    else if (!LogFileName)
    {
        Error = ERROR_PATH_NOT_FOUND;
    }
    else
    {
        Error = EtwpOpenFile(Trace, LogFileName);
    }

    if (Error != ERROR_SUCCESS)
    {
        EtwpFreeTrace(Trace);
        SetLastError(Error);
        return INVALID_PROCESSTRACE_HANDLE;
    }

    RtlAcquireSRWLockExclusive(&EtwpTraceLock);
    Trace->Handle = EtwpNextHandle++;
    InsertTailList(&EtwpTraces, &Trace->Entry);
    RtlReleaseSRWLockExclusive(&EtwpTraceLock);

    SetLastError(ERROR_SUCCESS);
    return Trace->Handle;
}

TRACEHANDLE
WINAPI
OpenTraceA(IN PEVENT_TRACE_LOGFILEA Logfile)
{
    UNICODE_STRING LogFileName, LoggerName;
    TRACEHANDLE Handle;

    if (!Logfile)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_PROCESSTRACE_HANDLE;
    }

    RtlInitUnicodeString(&LogFileName, NULL);
    RtlInitUnicodeString(&LoggerName, NULL);
    if ((Logfile->LogFileName && !RtlCreateUnicodeStringFromAsciiz(&LogFileName, Logfile->LogFileName)) ||
        (Logfile->LoggerName && !RtlCreateUnicodeStringFromAsciiz(&LoggerName, Logfile->LoggerName)))
    {
        RtlFreeUnicodeString(&LogFileName);
        RtlFreeUnicodeString(&LoggerName);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return INVALID_PROCESSTRACE_HANDLE;
    }

    Handle = EtwpOpenTrace((PEVENT_TRACE_LOGFILEW)Logfile, LogFileName.Buffer, LoggerName.Buffer);

    RtlFreeUnicodeString(&LogFileName);
    RtlFreeUnicodeString(&LoggerName);
    return Handle;
}

TRACEHANDLE
WINAPI
OpenTraceW(IN PEVENT_TRACE_LOGFILEW Logfile)
{
    if (!Logfile)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_PROCESSTRACE_HANDLE;
    }

    return EtwpOpenTrace(Logfile, Logfile->LogFileName, Logfile->LoggerName);
}

static ULONG
EtwpProcessRealTime(PETWP_TRACE Trace)
{
    PEVENT_TRACE_PROPERTIES Properties;
    ULONG Size, Error;
    BOOL Seen = FALSE;

    Size = sizeof(*Properties) + 2 * 1024 * sizeof(WCHAR);
    Properties = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (!Properties)
        return ERROR_NOT_ENOUGH_MEMORY;

    for (;;)
    {
        RtlZeroMemory(Properties, Size);
        Properties->Wnode.BufferSize = Size;
        Properties->LoggerNameOffset = sizeof(*Properties);
        Properties->LogFileNameOffset = sizeof(*Properties) + 1024 * sizeof(WCHAR);
        Error = ControlTraceW(0, Trace->LoggerName, Properties, EVENT_TRACE_CONTROL_QUERY);
        if (Error != ERROR_SUCCESS)
            break;
        Seen = TRUE;
        if (Trace->Closing)
        {
            Error = ERROR_SUCCESS;
            break;
        }
        /* FIXME: the kernel has no real-time consumer delivery; wait for the session to stop without events */
        Sleep(100);
    }

    HeapFree(GetProcessHeap(), 0, Properties);
    if (Error == ERROR_WMI_INSTANCE_NOT_FOUND && Seen)
        Error = ERROR_SUCCESS;
    return Error;
}

ULONG
WINAPI
ProcessTrace(IN PTRACEHANDLE HandleArray,
             IN ULONG HandleCount,
             IN LPFILETIME StartTime,
             IN LPFILETIME EndTime)
{
    PETWP_TRACE Traces[ETWP_MAX_PROCESS_HANDLES];
    PETWP_TRACE Best;
    LONGLONG Start = 0, End = MAXLONGLONG, Time, BestTime = 0;
    ULONG Error = ERROR_SUCCESS, i, Active;

    if (!HandleArray)
        return ERROR_INVALID_PARAMETER;
    if (!HandleCount || HandleCount > ETWP_MAX_PROCESS_HANDLES)
        return ERROR_BAD_LENGTH;

    if (StartTime)
        Start = ((LONGLONG)StartTime->dwHighDateTime << 32) | StartTime->dwLowDateTime;
    if (EndTime)
        End = ((LONGLONG)EndTime->dwHighDateTime << 32) | EndTime->dwLowDateTime;

    RtlAcquireSRWLockExclusive(&EtwpTraceLock);
    for (i = 0; i < HandleCount; i++)
    {
        Traces[i] = EtwpFindTrace(HandleArray[i]);
        if (!Traces[i] || Traces[i]->Busy)
        {
            Error = ERROR_INVALID_HANDLE;
            break;
        }
    }
    if (Error == ERROR_SUCCESS)
    {
        for (i = 0; i < HandleCount; i++)
            Traces[i]->Busy = TRUE;
    }
    RtlReleaseSRWLockExclusive(&EtwpTraceLock);
    if (Error != ERROR_SUCCESS)
        return Error;

    for (i = 0; i < HandleCount && Error == ERROR_SUCCESS; i++)
    {
        if (Traces[i]->RealTime)
            continue;
        Traces[i]->BufferIndex = 0;
        Traces[i]->Logfile->BuffersRead = 0;
        if (!EtwpReadBuffer(Traces[i], 0))
            Error = ERROR_BAD_FORMAT;
        else if (!EtwpAdvance(Traces[i], &Error))
            break;
    }

    while (Error == ERROR_SUCCESS)
    {
        Best = NULL;
        for (i = 0, Active = 0; i < HandleCount; i++)
        {
            if (Traces[i]->RealTime || !Traces[i]->Pending)
                continue;
            Active++;
            Time = EtwpConvertTime(Traces[i], Traces[i]->RawTime);
            if (!Best || Time < BestTime)
            {
                Best = Traces[i];
                BestTime = Time;
            }
        }
        if (!Best)
            break;

        if (BestTime >= Start && BestTime <= End)
            EtwpDeliverRecord(Best);

        Best->Pending = FALSE;
        if (!EtwpAdvance(Best, &Error))
            break;
    }

    for (i = 0; i < HandleCount && Error == ERROR_SUCCESS; i++)
    {
        if (Traces[i]->RealTime)
            Error = EtwpProcessRealTime(Traces[i]);
    }

    RtlAcquireSRWLockExclusive(&EtwpTraceLock);
    for (i = 0; i < HandleCount; i++)
    {
        Traces[i]->Busy = FALSE;
        if (Traces[i]->Closing)
        {
            RemoveEntryList(&Traces[i]->Entry);
            EtwpFreeTrace(Traces[i]);
        }
    }
    RtlReleaseSRWLockExclusive(&EtwpTraceLock);

    return Error;
}

ULONG
WINAPI
CloseTrace(IN TRACEHANDLE TraceHandle)
{
    PETWP_TRACE Trace;

    RtlAcquireSRWLockExclusive(&EtwpTraceLock);
    Trace = EtwpFindTrace(TraceHandle);
    if (!Trace)
    {
        RtlReleaseSRWLockExclusive(&EtwpTraceLock);
        return ERROR_INVALID_HANDLE;
    }

    if (Trace->Busy)
    {
        Trace->Closing = TRUE;
        RtlReleaseSRWLockExclusive(&EtwpTraceLock);
        return ERROR_CTX_CLOSE_PENDING;
    }

    RemoveEntryList(&Trace->Entry);
    RtlReleaseSRWLockExclusive(&EtwpTraceLock);
    EtwpFreeTrace(Trace);
    return ERROR_SUCCESS;
}

ULONG
WINAPI
EnableTraceEx2(IN TRACEHANDLE TraceHandle,
               IN LPCGUID ProviderId,
               IN ULONG ControlCode,
               IN UCHAR Level,
               IN ULONGLONG MatchAnyKeyword,
               IN ULONGLONG MatchAllKeyword,
               IN ULONG Timeout,
               IN PENABLE_TRACE_PARAMETERS EnableParameters OPTIONAL)
{
    if (!TraceHandle || !ProviderId)
        return ERROR_INVALID_PARAMETER;

    if (ControlCode != EVENT_CONTROL_CODE_DISABLE_PROVIDER &&
        ControlCode != EVENT_CONTROL_CODE_ENABLE_PROVIDER &&
        ControlCode != EVENT_CONTROL_CODE_CAPTURE_STATE)
    {
        return ERROR_INVALID_PARAMETER;
    }

    if (EnableParameters && EnableParameters->Version != ENABLE_TRACE_PARAMETERS_VERSION &&
        EnableParameters->Version != ENABLE_TRACE_PARAMETERS_VERSION_2)
    {
        return ERROR_INVALID_PARAMETER;
    }

    return ERROR_SUCCESS;
}

ULONG
WINAPI
EnableTraceEx(IN LPCGUID ProviderId,
              IN LPCGUID SourceId OPTIONAL,
              IN TRACEHANDLE TraceHandle,
              IN ULONG IsEnabled,
              IN UCHAR Level,
              IN ULONGLONG MatchAnyKeyword,
              IN ULONGLONG MatchAllKeyword,
              IN ULONG EnableProperty,
              IN PEVENT_FILTER_DESCRIPTOR EnableFilterDesc OPTIONAL)
{
    ENABLE_TRACE_PARAMETERS Parameters;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.Version = ENABLE_TRACE_PARAMETERS_VERSION;
    Parameters.EnableProperty = EnableProperty;
    if (SourceId)
        Parameters.SourceId = *SourceId;
    Parameters.EnableFilterDesc = EnableFilterDesc;

    return EnableTraceEx2(TraceHandle, ProviderId, IsEnabled, Level, MatchAnyKeyword, MatchAllKeyword, 0, &Parameters);
}
