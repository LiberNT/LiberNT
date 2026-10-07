/*
 * ntdll.dll Event Tracing Functions
 */

#include <ntdll.h>

#include <wmistr.h>
#include <evntrace.h>
#include <ntwmi.h>

#define NDEBUG
#include <debug.h>

#define FIXME DPRINT1

typedef struct _ETW_CLASSIC_PROVIDER
{
    LIST_ENTRY Entry;
    TRACEHANDLE Handle;
    GUID ControlGuid;
    WMIDPREQUEST Callback;
    PVOID Context;
    GUID ClassGuids[ANYSIZE_ARRAY];
} ETW_CLASSIC_PROVIDER;

static RTL_SRWLOCK EtwpClassicLock = RTL_SRWLOCK_INIT;
static LIST_ENTRY EtwpClassicProviders = { &EtwpClassicProviders, &EtwpClassicProviders };
static TRACEHANDLE EtwpNextClassicHandle;

static ULONG
EtwpRegisterClassicProvider(WMIDPREQUEST Callback, PVOID Context, LPCGUID ControlGuid,
                            ULONG GuidCount, PTRACE_GUID_REGISTRATION TraceGuidReg,
                            PTRACEHANDLE RegistrationHandle)
{
    ETW_CLASSIC_PROVIDER *Provider;
    ULONG i;

    if (!Callback || !ControlGuid || !RegistrationHandle || (GuidCount && !TraceGuidReg))
        return ERROR_INVALID_PARAMETER;
    for (i = 0; i < GuidCount; ++i)
        if (!TraceGuidReg[i].Guid) return ERROR_INVALID_PARAMETER;

    if ((ULONG_PTR)GuidCount > (MAXULONG_PTR - FIELD_OFFSET(ETW_CLASSIC_PROVIDER, ClassGuids)) / sizeof(GUID))
        return ERROR_NOT_ENOUGH_MEMORY;
    Provider = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY,
                              FIELD_OFFSET(ETW_CLASSIC_PROVIDER, ClassGuids) + (SIZE_T)GuidCount * sizeof(GUID));
    if (!Provider) return ERROR_NOT_ENOUGH_MEMORY;
    Provider->ControlGuid = *ControlGuid;
    Provider->Callback = Callback;
    Provider->Context = Context;
    for (i = 0; i < GuidCount; ++i)
        Provider->ClassGuids[i] = *TraceGuidReg[i].Guid;

    RtlAcquireSRWLockExclusive(&EtwpClassicLock);
    Provider->Handle = ++EtwpNextClassicHandle;
    InsertTailList(&EtwpClassicProviders, &Provider->Entry);
    RtlReleaseSRWLockExclusive(&EtwpClassicLock);

    for (i = 0; i < GuidCount; ++i)
        TraceGuidReg[i].RegHandle = &Provider->ClassGuids[i];
    *RegistrationHandle = Provider->Handle;
    /* No trace controller is active, so no enable callback is delivered. */
    return ERROR_SUCCESS;
}

/*
 * @unimplemented
 */
ULONG CDECL
EtwTraceMessage(
    TRACEHANDLE  SessionHandle,
    ULONG        MessageFlags,
    LPCGUID      MessageGuid,
    USHORT       MessageNumber,
    ...)
{
    FIXME("TraceMessage()\n");
    return ERROR_SUCCESS;
}

TRACEHANDLE
NTAPI
EtwGetTraceLoggerHandle(
    PVOID Buffer
)
{
    FIXME("EtwGetTraceLoggerHandle stub()\n");
    return (TRACEHANDLE)-1;
}


ULONG
NTAPI
EtwTraceEvent(
    TRACEHANDLE SessionHandle,
    PEVENT_TRACE_HEADER EventTrace
)
{
    if (!EventTrace || EventTrace->Size < sizeof(EVENT_TRACE_HEADER))
        return ERROR_INVALID_PARAMETER;

    return RtlNtStatusToDosError(NtTraceEvent((HANDLE)(ULONG_PTR)SessionHandle,
                                              ETW_NT_FLAGS_TRACE_HEADER,
                                              EventTrace->Size,
                                              EventTrace));
}

ULONG
NTAPI
EtwGetTraceEnableFlags(
    TRACEHANDLE TraceHandle
)
{
    /* There are currently no enabled trace sessions. */
    return 0;
}

UCHAR
NTAPI
EtwGetTraceEnableLevel(
    TRACEHANDLE TraceHandle
)
{
    return 0;
}

ULONG
NTAPI
EtwUnregisterTraceGuids(
    TRACEHANDLE RegistrationHandle
)
{
    PLIST_ENTRY Entry;
    ETW_CLASSIC_PROVIDER *Provider;

    RtlAcquireSRWLockExclusive(&EtwpClassicLock);
    for (Entry = EtwpClassicProviders.Flink; Entry != &EtwpClassicProviders; Entry = Entry->Flink)
    {
        Provider = CONTAINING_RECORD(Entry, ETW_CLASSIC_PROVIDER, Entry);
        if (Provider->Handle == RegistrationHandle)
        {
            RemoveEntryList(Entry);
            RtlReleaseSRWLockExclusive(&EtwpClassicLock);
            RtlFreeHeap(RtlGetProcessHeap(), 0, Provider);
            return ERROR_SUCCESS;
        }
    }
    RtlReleaseSRWLockExclusive(&EtwpClassicLock);
    return ERROR_INVALID_HANDLE;
}

ULONG
NTAPI
EtwRegisterTraceGuidsA(
    WMIDPREQUEST RequestAddress,
    PVOID RequestContext,
    LPCGUID ControlGuid,
    ULONG GuidCount,
    PTRACE_GUID_REGISTRATION TraceGuidReg,
    LPCSTR MofImagePath,
    LPCSTR MofResourceName,
    PTRACEHANDLE RegistrationHandle
)
{
    return EtwpRegisterClassicProvider(RequestAddress, RequestContext, ControlGuid,
                                       GuidCount, TraceGuidReg, RegistrationHandle);
}

ULONG
NTAPI
EtwRegisterTraceGuidsW(
    WMIDPREQUEST RequestAddress,
    PVOID RequestContext,
    LPCGUID ControlGuid,
    ULONG GuidCount,
    PTRACE_GUID_REGISTRATION TraceGuidReg,
    LPCWSTR MofImagePath,
    LPCWSTR MofResourceName,
    PTRACEHANDLE RegistrationHandle
)
{
    return EtwpRegisterClassicProvider(RequestAddress, RequestContext, ControlGuid,
                                       GuidCount, TraceGuidReg, RegistrationHandle);
}

#define ETWP_MAX_NAME_CHARS 1024

typedef struct _ETWP_CONTROL
{
    WMI_LOGGER_INFORMATION Info;
    WCHAR LoggerName[ETWP_MAX_NAME_CHARS + 1];
    WCHAR LogFileName[ETWP_MAX_NAME_CHARS + 1];
} ETWP_CONTROL, *PETWP_CONTROL;

static const GUID EtwpSystemTraceControlGuid =
    { 0x9e814aad, 0x3204, 0x11d2, { 0x9a, 0x82, 0x00, 0x60, 0x08, 0xa8, 0x69, 0x39 } };

static BOOLEAN
EtwpValidOffset(ULONG Offset, ULONG Size)
{
    return !Offset || (Offset >= sizeof(EVENT_TRACE_PROPERTIES) && Offset <= Size);
}

static PETWP_CONTROL
EtwpAllocateControl(const EVENT_TRACE_PROPERTIES *Properties)
{
    PETWP_CONTROL Control;

    Control = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Control));
    if (!Control) return NULL;

    Control->Info.Wnode = Properties->Wnode;
    Control->Info.Wnode.BufferSize = sizeof(Control->Info);
    Control->Info.BufferSize = Properties->BufferSize;
    Control->Info.MinimumBuffers = Properties->MinimumBuffers;
    Control->Info.MaximumBuffers = Properties->MaximumBuffers;
    Control->Info.MaximumFileSize = Properties->MaximumFileSize;
    Control->Info.LogFileMode = Properties->LogFileMode;
    Control->Info.FlushTimer = Properties->FlushTimer;
    Control->Info.EnableFlags = Properties->EnableFlags;
    Control->Info.AgeLimit = Properties->AgeLimit;
    Control->Info.LoggerName64.Buffer = (ULONG_PTR)Control->LoggerName;
    Control->Info.LoggerName64.MaximumLength = sizeof(Control->LoggerName);
    Control->Info.LogFileName64.Buffer = (ULONG_PTR)Control->LogFileName;
    Control->Info.LogFileName64.MaximumLength = sizeof(Control->LogFileName);
    return Control;
}

static BOOLEAN
EtwpSetLoggerName(PETWP_CONTROL Control, LPCWSTR Name)
{
    SIZE_T Length = wcslen(Name);

    if (Length > ETWP_MAX_NAME_CHARS) return FALSE;
    RtlCopyMemory(Control->LoggerName, Name, (Length + 1) * sizeof(WCHAR));
    Control->Info.LoggerName64.Length = (USHORT)(Length * sizeof(WCHAR));
    return TRUE;
}

static BOOLEAN
EtwpSetLogFileName(PETWP_CONTROL Control, LPCWSTR File)
{
    ULONG Length;

    Control->LogFileName[0] = UNICODE_NULL;
    Control->Info.LogFileName64.Length = 0;
    if (!File || !*File) return TRUE;

    Length = RtlGetFullPathName_U(File, sizeof(Control->LogFileName), Control->LogFileName, NULL);
    if (!Length || Length >= sizeof(Control->LogFileName))
    {
        Control->LogFileName[0] = UNICODE_NULL;
        return FALSE;
    }
    Control->Info.LogFileName64.Length = (USHORT)Length;
    return TRUE;
}

static VOID
EtwpReturnProperties(const WMI_LOGGER_INFORMATION *Info, PEVENT_TRACE_PROPERTIES Properties)
{
    Properties->Wnode.ProviderId = Info->Wnode.ProviderId;
    Properties->Wnode.HistoricalContext = Info->Wnode.HistoricalContext;
    Properties->Wnode.TimeStamp = Info->Wnode.TimeStamp;
    Properties->Wnode.Guid = Info->Wnode.Guid;
    Properties->Wnode.ClientContext = Info->Wnode.ClientContext;
    Properties->Wnode.Flags = Info->Wnode.Flags;
    Properties->BufferSize = Info->BufferSize;
    Properties->MinimumBuffers = Info->MinimumBuffers;
    Properties->MaximumBuffers = Info->MaximumBuffers;
    Properties->MaximumFileSize = Info->MaximumFileSize;
    Properties->LogFileMode = Info->LogFileMode;
    Properties->FlushTimer = Info->FlushTimer;
    Properties->EnableFlags = Info->EnableFlags;
    Properties->AgeLimit = Info->AgeLimit;
    Properties->NumberOfBuffers = Info->NumberOfBuffers;
    Properties->FreeBuffers = Info->FreeBuffers;
    Properties->EventsLost = Info->EventsLost;
    Properties->BuffersWritten = Info->BuffersWritten;
    Properties->LogBuffersLost = Info->LogBuffersLost;
    Properties->RealTimeBuffersLost = Info->RealTimeBuffersLost;
    Properties->LoggerThreadId = (HANDLE)(ULONG_PTR)Info->LoggerThreadId64;
}

static VOID
EtwpReturnStrings(PEVENT_TRACE_PROPERTIES Properties, ULONG NameOffset, const VOID *Name, ULONG NameBytes,
                  ULONG FileOffset, const VOID *File, ULONG FileBytes)
{
    ULONG Size = Properties->Wnode.BufferSize;
    ULONG NameLimit = (FileOffset > NameOffset) ? FileOffset : Size;
    ULONG FileLimit = (NameOffset > FileOffset) ? NameOffset : Size;

    if (NameOffset && NameOffset <= NameLimit && NameBytes <= NameLimit - NameOffset)
        RtlCopyMemory((PUCHAR)Properties + NameOffset, Name, NameBytes);
    if (FileOffset && FileOffset <= FileLimit && FileBytes <= FileLimit - FileOffset)
        RtlCopyMemory((PUCHAR)Properties + FileOffset, File, FileBytes);
}

static ULONG
EtwpReturnControl(PEVENT_TRACE_PROPERTIES Properties, const WMI_LOGGER_INFORMATION *Info, const VOID *Name,
                  ULONG NameBytes, const VOID *File, ULONG FileBytes, ULONG TooSmallError)
{
    ULONG Size = Properties->Wnode.BufferSize;
    ULONG NameOffset = Properties->LoggerNameOffset;
    ULONG FileOffset = Properties->LogFileNameOffset;

    EtwpReturnProperties(Info, Properties);
    if (Size - sizeof(*Properties) < NameBytes + FileBytes)
    {
        Properties->Wnode.Flags |= WNODE_FLAG_TOO_SMALL;
        Properties->BufferSize = sizeof(*Properties) + NameBytes + FileBytes;
        return TooSmallError;
    }

    if (!NameOffset) NameOffset = sizeof(*Properties);
    if (!FileOffset && Size - NameOffset >= NameBytes + FileBytes) FileOffset = NameOffset + NameBytes;
    EtwpReturnStrings(Properties, NameOffset, Name, NameBytes, FileOffset, File, FileBytes);
    Properties->LoggerNameOffset = NameOffset;
    Properties->LogFileNameOffset = FileOffset;
    return ERROR_SUCCESS;
}

static ULONG
EtwpStartLogger(PTRACEHANDLE SessionHandle, PETWP_CONTROL Control, PEVENT_TRACE_PROPERTIES Properties, LPCWSTR File)
{
    ULONG Mode = Properties->LogFileMode;
    ULONG ReturnSize;
    NTSTATUS Status;

    if ((Mode & EVENT_TRACE_FILE_MODE_SEQUENTIAL) && (Mode & EVENT_TRACE_FILE_MODE_CIRCULAR))
        return ERROR_INVALID_PARAMETER;
    if ((Mode & (EVENT_TRACE_FILE_MODE_CIRCULAR | EVENT_TRACE_FILE_MODE_NEWFILE | EVENT_TRACE_FILE_MODE_PREALLOCATE)) &&
        !Properties->MaximumFileSize)
        return ERROR_INVALID_PARAMETER;
    if (IsEqualGUID(&Properties->Wnode.Guid, &EtwpSystemTraceControlGuid) &&
        _wcsicmp(Control->LoggerName, KERNEL_LOGGER_NAMEW))
        return ERROR_INVALID_PARAMETER;
    if (!(Mode & (EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_BUFFERING_MODE)) && (!File || !*File))
        return ERROR_BAD_PATHNAME;
    if (!EtwpSetLogFileName(Control, File))
        return ERROR_BAD_PATHNAME;

    *SessionHandle = 0;
    Status = NtTraceControl(EtwStartLoggerCode, &Control->Info, sizeof(Control->Info),
                            &Control->Info, sizeof(Control->Info), &ReturnSize);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);

    *SessionHandle = Control->Info.Wnode.HistoricalContext;
    EtwpReturnProperties(&Control->Info, Properties);
    return ERROR_SUCCESS;
}

static ULONG
EtwpControlLogger(TRACEHANDLE SessionHandle, PETWP_CONTROL Control, LPCWSTR SessionName, LPCWSTR File, ULONG ControlCode)
{
    static const ULONG Codes[] = { EtwQueryLoggerCode, EtwStopLoggerCode, EtwUpdateLoggerCode, EtwFlushLoggerCode };
    ULONG ReturnSize;

    Control->Info.Wnode.HistoricalContext = SessionHandle;
    if (SessionName && !EtwpSetLoggerName(Control, SessionName))
        return ERROR_WMI_INSTANCE_NOT_FOUND;
    if (ControlCode == EVENT_TRACE_CONTROL_UPDATE && !EtwpSetLogFileName(Control, File))
        return ERROR_BAD_PATHNAME;

    return RtlNtStatusToDosError(NtTraceControl(Codes[ControlCode], &Control->Info, sizeof(Control->Info),
                                                &Control->Info, sizeof(Control->Info), &ReturnSize));
}

static ULONG
EtwpCheckControl(TRACEHANDLE SessionHandle, BOOLEAN HasName, const EVENT_TRACE_PROPERTIES *Properties, ULONG ControlCode)
{
    if (!Properties || ControlCode > EVENT_TRACE_CONTROL_FLUSH) return ERROR_INVALID_PARAMETER;
    if (!HasName && !SessionHandle) return ERROR_INVALID_PARAMETER;
    if (Properties->Wnode.BufferSize < sizeof(*Properties)) return ERROR_BAD_LENGTH;
    if (!EtwpValidOffset(Properties->LoggerNameOffset, Properties->Wnode.BufferSize) ||
        !EtwpValidOffset(Properties->LogFileNameOffset, Properties->Wnode.BufferSize))
        return ERROR_INVALID_PARAMETER;
    return ERROR_SUCCESS;
}

static LPWSTR
EtwpDuplicateAnsi(LPCSTR String, SIZE_T Length)
{
    ULONG Bytes = 0;
    LPWSTR Wide;

    Wide = RtlAllocateHeap(RtlGetProcessHeap(), 0, (Length + 1) * sizeof(WCHAR));
    if (!Wide) return NULL;
    RtlMultiByteToUnicodeN(Wide, (ULONG)(Length * sizeof(WCHAR)), &Bytes, String, (ULONG)Length);
    Wide[Bytes / sizeof(WCHAR)] = UNICODE_NULL;
    return Wide;
}

static ULONG
EtwpAnsiBytes(LPCWSTR String, LPSTR *Ansi)
{
    ULONG WideBytes = (ULONG)(wcslen(String) + 1) * sizeof(WCHAR);
    ULONG Bytes = 0;

    RtlUnicodeToMultiByteSize(&Bytes, String, WideBytes);
    *Ansi = RtlAllocateHeap(RtlGetProcessHeap(), 0, Bytes);
    if (!*Ansi) return 0;
    RtlUnicodeToMultiByteN(*Ansi, Bytes, &Bytes, String, WideBytes);
    return Bytes;
}

ULONG WINAPI EtwStartTraceW( PTRACEHANDLE pSessionHandle, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    PETWP_CONTROL Control;
    LPCWSTR File = NULL;
    SIZE_T NameBytes, FileBytes = 0, Limit;
    ULONG Size, Error;

    if (!pSessionHandle || !Properties) return ERROR_INVALID_PARAMETER;
    if (!SessionName || !*SessionName) return ERROR_INVALID_NAME;
    Size = Properties->Wnode.BufferSize;
    if (Size < sizeof(*Properties)) return ERROR_BAD_LENGTH;
    if (!EtwpValidOffset(Properties->LoggerNameOffset, Size) || !EtwpValidOffset(Properties->LogFileNameOffset, Size))
        return ERROR_INVALID_PARAMETER;

    NameBytes = (wcslen(SessionName) + 1) * sizeof(WCHAR);
    if (Properties->LogFileNameOffset)
    {
        File = (LPCWSTR)((PUCHAR)Properties + Properties->LogFileNameOffset);
        Limit = (Size - Properties->LogFileNameOffset) / sizeof(WCHAR);
        while (FileBytes < Limit && File[FileBytes]) FileBytes++;
        if (FileBytes == Limit) return ERROR_BAD_LENGTH;
        FileBytes = (FileBytes + 1) * sizeof(WCHAR);
    }
    if (Size - sizeof(*Properties) < NameBytes + FileBytes) return ERROR_BAD_LENGTH;

    Control = EtwpAllocateControl(Properties);
    if (!Control) return ERROR_NOT_ENOUGH_MEMORY;
    if (!EtwpSetLoggerName(Control, SessionName))
        Error = ERROR_INVALID_PARAMETER;
    else
        Error = EtwpStartLogger(pSessionHandle, Control, Properties, File);
    if (!Error)
    {
        EtwpReturnStrings(Properties, Properties->LoggerNameOffset, Control->LoggerName,
                          Control->Info.LoggerName64.Length + sizeof(WCHAR),
                          Properties->LogFileNameOffset, Control->LogFileName,
                          Control->Info.LogFileName64.Length + sizeof(WCHAR));
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Control);
    return Error;
}

ULONG WINAPI EtwStartTraceA( PTRACEHANDLE pSessionHandle, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    PETWP_CONTROL Control;
    LPWSTR NameW, FileW = NULL;
    LPSTR NameA = NULL, FileA = NULL;
    LPCSTR File = NULL;
    SIZE_T NameBytes, FileBytes = 0, Limit;
    ULONG Size, Error, NameSize, FileSize;

    if (!pSessionHandle || !Properties) return ERROR_INVALID_PARAMETER;
    if (!SessionName || !*SessionName) return ERROR_INVALID_NAME;
    Size = Properties->Wnode.BufferSize;
    if (Size < sizeof(*Properties)) return ERROR_BAD_LENGTH;
    if (!EtwpValidOffset(Properties->LoggerNameOffset, Size) || !EtwpValidOffset(Properties->LogFileNameOffset, Size))
        return ERROR_INVALID_PARAMETER;

    NameBytes = strlen(SessionName) + 1;
    if (Properties->LogFileNameOffset)
    {
        File = (LPCSTR)Properties + Properties->LogFileNameOffset;
        Limit = Size - Properties->LogFileNameOffset;
        while (FileBytes < Limit && File[FileBytes]) FileBytes++;
        if (FileBytes == Limit) return ERROR_BAD_LENGTH;
        FileBytes++;
    }
    if (Size - sizeof(*Properties) < NameBytes + FileBytes) return ERROR_BAD_LENGTH;

    Control = EtwpAllocateControl(Properties);
    NameW = EtwpDuplicateAnsi(SessionName, NameBytes - 1);
    if (File) FileW = EtwpDuplicateAnsi(File, FileBytes - 1);
    if (!Control || !NameW || (File && !FileW))
        Error = ERROR_NOT_ENOUGH_MEMORY;
    else if (!EtwpSetLoggerName(Control, NameW))
        Error = ERROR_INVALID_PARAMETER;
    else
        Error = EtwpStartLogger(pSessionHandle, Control, Properties, FileW);
    if (!Error)
    {
        NameSize = EtwpAnsiBytes(Control->LoggerName, &NameA);
        FileSize = EtwpAnsiBytes(Control->LogFileName, &FileA);
        if (NameA && FileA)
        {
            EtwpReturnStrings(Properties, Properties->LoggerNameOffset, NameA, NameSize,
                              Properties->LogFileNameOffset, FileA, FileSize);
        }
    }
    if (NameA) RtlFreeHeap(RtlGetProcessHeap(), 0, NameA);
    if (FileA) RtlFreeHeap(RtlGetProcessHeap(), 0, FileA);
    if (NameW) RtlFreeHeap(RtlGetProcessHeap(), 0, NameW);
    if (FileW) RtlFreeHeap(RtlGetProcessHeap(), 0, FileW);
    if (Control) RtlFreeHeap(RtlGetProcessHeap(), 0, Control);
    return Error;
}

/******************************************************************************
 * EtwControlTraceW [NTDLL.@]
 *
 * Control a givel event trace session
 *
 */
ULONG WINAPI EtwControlTraceW( TRACEHANDLE hSession, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties, ULONG control )
{
    PETWP_CONTROL Control;
    LPCWSTR File = NULL;
    SIZE_T Chars = 0, Limit;
    ULONG Error;

    if (SessionName && !*SessionName) SessionName = NULL;
    Error = EtwpCheckControl(hSession, SessionName != NULL, Properties, control);
    if (Error) return Error;

    if (control == EVENT_TRACE_CONTROL_UPDATE && Properties->LogFileNameOffset)
    {
        File = (LPCWSTR)((PUCHAR)Properties + Properties->LogFileNameOffset);
        Limit = (Properties->Wnode.BufferSize - Properties->LogFileNameOffset) / sizeof(WCHAR);
        while (Chars < Limit && File[Chars]) Chars++;
        if (Chars == Limit) File = NULL;
    }

    Control = EtwpAllocateControl(Properties);
    if (!Control) return ERROR_NOT_ENOUGH_MEMORY;
    Error = EtwpControlLogger(hSession, Control, SessionName, File, control);
    if (!Error)
    {
        Error = EtwpReturnControl(Properties, &Control->Info, Control->LoggerName,
                                  Control->Info.LoggerName64.Length + sizeof(WCHAR), Control->LogFileName,
                                  Control->Info.LogFileName64.Length + sizeof(WCHAR), ERROR_MORE_DATA);
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Control);
    return Error;
}

/******************************************************************************
 * EtwControlTraceA [NTDLL.@]
 *
 * See ControlTraceW.
 *
 */
ULONG WINAPI EtwControlTraceA( TRACEHANDLE hSession, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties, ULONG control )
{
    PETWP_CONTROL Control;
    LPWSTR NameW = NULL, FileW = NULL;
    LPSTR NameA = NULL, FileA = NULL;
    LPCSTR File;
    SIZE_T Chars = 0, Limit;
    ULONG Error, NameSize, FileSize;

    if (SessionName && !*SessionName) SessionName = NULL;
    Error = EtwpCheckControl(hSession, SessionName != NULL, Properties, control);
    if (Error) return Error;

    if (control == EVENT_TRACE_CONTROL_UPDATE && Properties->LogFileNameOffset)
    {
        File = (LPCSTR)Properties + Properties->LogFileNameOffset;
        Limit = Properties->Wnode.BufferSize - Properties->LogFileNameOffset;
        while (Chars < Limit && File[Chars]) Chars++;
        if (Chars < Limit) FileW = EtwpDuplicateAnsi(File, Chars);
    }
    if (SessionName) NameW = EtwpDuplicateAnsi(SessionName, strlen(SessionName));

    Control = EtwpAllocateControl(Properties);
    if (!Control || (SessionName && !NameW))
        Error = ERROR_NOT_ENOUGH_MEMORY;
    else
        Error = EtwpControlLogger(hSession, Control, NameW, FileW, control);
    if (!Error)
    {
        NameSize = EtwpAnsiBytes(Control->LoggerName, &NameA);
        FileSize = EtwpAnsiBytes(Control->LogFileName, &FileA);
        if (NameA && FileA)
            EtwpReturnControl(Properties, &Control->Info, NameA, NameSize, FileA, FileSize, ERROR_SUCCESS);
        else
            Error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (NameA) RtlFreeHeap(RtlGetProcessHeap(), 0, NameA);
    if (FileA) RtlFreeHeap(RtlGetProcessHeap(), 0, FileA);
    if (NameW) RtlFreeHeap(RtlGetProcessHeap(), 0, NameW);
    if (FileW) RtlFreeHeap(RtlGetProcessHeap(), 0, FileW);
    if (Control) RtlFreeHeap(RtlGetProcessHeap(), 0, Control);
    return Error;
}

/******************************************************************************
 * EtwEnableTrace [NTDLL.@]
 */
ULONG WINAPI EtwEnableTrace( ULONG enable, ULONG flag, ULONG level, LPCGUID guid, TRACEHANDLE hSession )
{
    FIXME("(%d, 0x%x, %d, %p, %I64x): stub\n", enable, flag, level,
            guid, hSession);

    return ERROR_SUCCESS;
}

/******************************************************************************
 * EtwQueryAllTracesW [NTDLL.@]
 *
 * Query information for started event trace sessions
 *
 */
static ULONG
EtwpQueryAllTraces(PEVENT_TRACE_PROPERTIES *Array, ULONG ArrayCount, PULONG SessionCount, BOOLEAN Ansi)
{
    PETWP_CONTROL Control;
    EVENT_TRACE_PROPERTIES Empty;
    LPSTR NameA, FileA;
    ULONG Id, Count = 0, ReturnSize, NameSize, FileSize;
    NTSTATUS Status;

    if (!SessionCount || (ArrayCount && !Array)) return ERROR_INVALID_PARAMETER;

    RtlZeroMemory(&Empty, sizeof(Empty));
    Control = EtwpAllocateControl(&Empty);
    if (!Control) return ERROR_NOT_ENOUGH_MEMORY;

    for (Id = 1; Id < MAXLOGGERS; Id++)
    {
        Control->Info.Wnode.HistoricalContext = Id;
        Control->Info.LoggerName64.Length = 0;
        Control->Info.LogFileName64.Length = 0;
        Status = NtTraceControl(EtwQueryLoggerCode, &Control->Info, sizeof(Control->Info),
                                &Control->Info, sizeof(Control->Info), &ReturnSize);
        if (Status == STATUS_ACCESS_DENIED)
        {
            RtlFreeHeap(RtlGetProcessHeap(), 0, Control);
            return ERROR_ACCESS_DENIED;
        }
        if (!NT_SUCCESS(Status)) continue;

        if (Count < ArrayCount && Array[Count] && Array[Count]->Wnode.BufferSize >= sizeof(EVENT_TRACE_PROPERTIES) &&
            EtwpValidOffset(Array[Count]->LoggerNameOffset, Array[Count]->Wnode.BufferSize) &&
            EtwpValidOffset(Array[Count]->LogFileNameOffset, Array[Count]->Wnode.BufferSize))
        {
            if (!Ansi)
            {
                EtwpReturnControl(Array[Count], &Control->Info, Control->LoggerName,
                                  Control->Info.LoggerName64.Length + sizeof(WCHAR), Control->LogFileName,
                                  Control->Info.LogFileName64.Length + sizeof(WCHAR), ERROR_MORE_DATA);
            }
            else
            {
                NameSize = EtwpAnsiBytes(Control->LoggerName, &NameA);
                FileSize = EtwpAnsiBytes(Control->LogFileName, &FileA);
                if (NameA && FileA)
                    EtwpReturnControl(Array[Count], &Control->Info, NameA, NameSize, FileA, FileSize, ERROR_SUCCESS);
                if (NameA) RtlFreeHeap(RtlGetProcessHeap(), 0, NameA);
                if (FileA) RtlFreeHeap(RtlGetProcessHeap(), 0, FileA);
            }
        }
        Count++;
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, Control);
    *SessionCount = Count;
    return Count > ArrayCount ? ERROR_MORE_DATA : ERROR_SUCCESS;
}

ULONG WINAPI EtwQueryAllTracesW( PEVENT_TRACE_PROPERTIES * parray, ULONG arraycount, PULONG psessioncount )
{
    return EtwpQueryAllTraces(parray, arraycount, psessioncount, FALSE);
}

/******************************************************************************
 * QueryAllTracesA [NTDLL.@]
 *
 * See EtwQueryAllTracesA.
 */
ULONG WINAPI EtwQueryAllTracesA( PEVENT_TRACE_PROPERTIES * parray, ULONG arraycount, PULONG psessioncount )
{
    return EtwpQueryAllTraces(parray, arraycount, psessioncount, TRUE);
}

/******************************************************************************
 * EtwFlushTraceA [NTDLL.@]
 *
 */
ULONG WINAPI EtwFlushTraceA( TRACEHANDLE hSession, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceA( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_FLUSH );
}

/******************************************************************************
 * EtwFlushTraceW [NTDLL.@]
 *
 */
ULONG WINAPI EtwFlushTraceW( TRACEHANDLE hSession, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceW( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_FLUSH );
}

/******************************************************************************
 * EtwQueryTraceA [NTDLL.@]
 *
 */
ULONG WINAPI EtwQueryTraceA( TRACEHANDLE hSession, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceA( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_QUERY );
}

/******************************************************************************
 * EtwQueryTraceW [NTDLL.@]
 *
 */
ULONG WINAPI EtwQueryTraceW( TRACEHANDLE hSession, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceW( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_QUERY );
}

/******************************************************************************
 * EtwStopTraceA [NTDLL.@]
 *
 */
ULONG WINAPI EtwStopTraceA( TRACEHANDLE hSession, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceA( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_STOP );
}

/******************************************************************************
 * EtwStopTraceW [NTDLL.@]
 *
 */
ULONG WINAPI EtwStopTraceW( TRACEHANDLE hSession, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceW( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_STOP );
}

/******************************************************************************
 * EtwUpdateTraceA [NTDLL.@]
 *
 */
ULONG WINAPI EtwUpdateTraceA( TRACEHANDLE hSession, LPCSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceA( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_UPDATE );
}

/******************************************************************************
 * EtwUpdateTraceW [NTDLL.@]
 *
 */
ULONG WINAPI EtwUpdateTraceW( TRACEHANDLE hSession, LPCWSTR SessionName, PEVENT_TRACE_PROPERTIES Properties )
{
    return EtwControlTraceW( hSession, SessionName, Properties, EVENT_TRACE_CONTROL_UPDATE );
}

/* EOF */
