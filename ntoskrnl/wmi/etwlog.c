/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Event tracing logger sessions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define INITGUID
#define _WMIKM_
#include <ntwmi.h>

#define NDEBUG
#include <debug.h>

#define ETWP_FIRST_LOGGER_ID        2
#define ETWP_MAX_NAME_LENGTH        (1024 * sizeof(WCHAR))
#define ETWP_DEFAULT_BUFFER_SIZE    64
#define ETWP_MAX_BUFFER_SIZE        16384
#define ETWP_EXTRA_BUFFERS          22
#define ETWP_LOGGER_MODE_MASK       0xBFFFFFFF
#define ETWP_UNSUPPORTED_MODES      (EVENT_TRACE_FILE_MODE_APPEND | EVENT_TRACE_FILE_MODE_NEWFILE | \
                                     EVENT_TRACE_DELAY_OPEN_FILE_MODE | EVENT_TRACE_BUFFERING_MODE | \
                                     EVENT_TRACE_PRIVATE_LOGGER_MODE | EVENT_TRACE_ADD_HEADER_MODE | \
                                     EVENT_TRACE_RELOG_MODE | EVENT_TRACE_PRIVATE_IN_PROC | \
                                     EVENT_TRACE_SYSTEM_LOGGER_MODE | EVENT_TRACE_COMPRESSED_MODE)
#define ETWP_TAG                    'LwtE'

typedef struct _ETWP_LOGGER
{
    LONG ReferenceCount;
    USHORT LoggerId;
    BOOLEAN StopRequested;
    BOOLEAN LimitReached;
    POOL_TYPE PoolType;
    GUID InstanceGuid;
    UNICODE_STRING LoggerName;
    UNICODE_STRING LogFileName;
    HANDLE LogFileHandle;
    ULONGLONG FileOffset;
    ULONGLONG FileLimit;
    ULONG BufferSizeKb;
    ULONG BufferSize;
    ULONG MinimumBuffers;
    ULONG MaximumBuffers;
    ULONG MaximumFileSize;
    ULONG LogFileMode;
    ULONG FlushTimer;
    ULONG ClockType;
    ULONG NumberOfBuffers;
    ULONG FreeBuffers;
    ULONG EventsLost;
    ULONG BuffersWritten;
    ULONG LogBuffersLost;
    ULONG RealTimeBuffersLost;
    LONGLONG SequenceNumber;
    LARGE_INTEGER StartTime;
    HANDLE LoggerThreadId;
    PETHREAD LoggerThread;
    KEVENT WakeEvent;
    KMUTEX FileLock;
    KGUARDED_MUTEX BufferLock;
    PWMI_BUFFER_HEADER HeaderBuffer;
    PWMI_BUFFER_HEADER CurrentBuffer;
    PWMI_BUFFER_HEADER FreeList;
    PWMI_BUFFER_HEADER FlushHead;
    PWMI_BUFFER_HEADER FlushTail;
} ETWP_LOGGER, *PETWP_LOGGER;

static EX_PUSH_LOCK EtwpLoggerLock;
static PETWP_LOGGER EtwpLoggers[MAXLOGGERS];

static const struct
{
    SID Sid;
    ULONG SubAuthority1;
} EtwpLoggingUsersSid =
{
    { SID_REVISION, 2, { SECURITY_NT_AUTHORITY }, { SECURITY_BUILTIN_DOMAIN_RID } },
    DOMAIN_ALIAS_RID_LOGGING_USERS
};

static
NTSTATUS
EtwpCheckControlAccess(
    _In_ KPROCESSOR_MODE PreviousMode)
{
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    PACCESS_TOKEN Token;
    BOOLEAN Allowed;

    if (PreviousMode == KernelMode)
        return STATUS_SUCCESS;

    SeCaptureSubjectContext(&SubjectContext);
    SeLockSubjectContext(&SubjectContext);
    Token = SeQuerySubjectContextToken(&SubjectContext);
    Allowed = SepSidInToken(Token, SeLocalSystemSid) ||
              SepSidInToken(Token, SeAliasAdminsSid) ||
              SepSidInToken(Token, SeLocalServiceSid) ||
              SepSidInToken(Token, SeNetworkServiceSid) ||
              SepSidInToken(Token, (PSID)&EtwpLoggingUsersSid);
    SeUnlockSubjectContext(&SubjectContext);
    SeReleaseSubjectContext(&SubjectContext);

    return Allowed ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
}

static
LONGLONG
EtwpQueryClock(
    _In_ PETWP_LOGGER Logger)
{
    LARGE_INTEGER Time;

    if (Logger->ClockType == EVENT_TRACE_CLOCK_SYSTEMTIME)
        KeQuerySystemTime(&Time);
    else
        Time = KeQueryPerformanceCounter(NULL);

    return Time.QuadPart;
}

static
VOID
EtwpInitializeBuffer(
    _In_ PETWP_LOGGER Logger,
    _Out_ PWMI_BUFFER_HEADER Buffer,
    _In_ USHORT BufferType)
{
    RtlZeroMemory(Buffer, sizeof(*Buffer));
    Buffer->BufferSize = Logger->BufferSize;
    Buffer->CurrentOffset = sizeof(*Buffer);
    Buffer->ClientContext.ProcessorNumber = (UCHAR)KeGetCurrentProcessorNumber();
    Buffer->ClientContext.LoggerId = Logger->LoggerId;
    Buffer->State = EtwBufferStateGeneralLogging;
    Buffer->BufferType = BufferType;
}

static
VOID
EtwpFreeBufferList(
    _In_opt_ PWMI_BUFFER_HEADER Buffer)
{
    PWMI_BUFFER_HEADER Next;

    while (Buffer)
    {
        Next = Buffer->NextBuffer;
        ExFreePoolWithTag(Buffer, ETWP_TAG);
        Buffer = Next;
    }
}

static
VOID
EtwpDereferenceLogger(
    _In_ PETWP_LOGGER Logger)
{
    if (InterlockedDecrement(&Logger->ReferenceCount) != 0)
        return;

    if (Logger->LoggerThread)
        ObDereferenceObject(Logger->LoggerThread);
    if (Logger->LogFileHandle)
        ZwClose(Logger->LogFileHandle);
    if (Logger->HeaderBuffer)
        ExFreePoolWithTag(Logger->HeaderBuffer, ETWP_TAG);
    if (Logger->CurrentBuffer)
        ExFreePoolWithTag(Logger->CurrentBuffer, ETWP_TAG);
    EtwpFreeBufferList(Logger->FreeList);
    EtwpFreeBufferList(Logger->FlushHead);
    if (Logger->LoggerName.Buffer)
        ExFreePoolWithTag(Logger->LoggerName.Buffer, ETWP_TAG);
    if (Logger->LogFileName.Buffer)
        ExFreePoolWithTag(Logger->LogFileName.Buffer, ETWP_TAG);
    ExFreePoolWithTag(Logger, ETWP_TAG);
}

static
PETWP_LOGGER
EtwpReferenceLoggerById(
    _In_ USHORT LoggerId)
{
    PETWP_LOGGER Logger = NULL;

    if (LoggerId >= MAXLOGGERS)
        return NULL;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&EtwpLoggerLock);
    Logger = EtwpLoggers[LoggerId];
    if (Logger)
        InterlockedIncrement(&Logger->ReferenceCount);
    ExReleasePushLockShared(&EtwpLoggerLock);
    KeLeaveCriticalRegion();

    return Logger;
}

static
PETWP_LOGGER
EtwpReferenceLoggerByName(
    _In_ PCUNICODE_STRING LoggerName)
{
    PETWP_LOGGER Logger = NULL;
    ULONG i;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&EtwpLoggerLock);
    for (i = ETWP_FIRST_LOGGER_ID; i < MAXLOGGERS; i++)
    {
        if (EtwpLoggers[i] &&
            RtlEqualUnicodeString(&EtwpLoggers[i]->LoggerName, LoggerName, TRUE))
        {
            Logger = EtwpLoggers[i];
            InterlockedIncrement(&Logger->ReferenceCount);
            break;
        }
    }
    ExReleasePushLockShared(&EtwpLoggerLock);
    KeLeaveCriticalRegion();

    return Logger;
}

static
PWMI_BUFFER_HEADER
EtwpTakeBuffer(
    _In_ PETWP_LOGGER Logger)
{
    PWMI_BUFFER_HEADER Buffer = Logger->FreeList;

    if (Buffer)
    {
        Logger->FreeList = Buffer->NextBuffer;
        Logger->FreeBuffers--;
    }
    else if (Logger->NumberOfBuffers < Logger->MaximumBuffers)
    {
        Buffer = ExAllocatePoolWithTag(Logger->PoolType, Logger->BufferSize, ETWP_TAG);
        if (Buffer)
            Logger->NumberOfBuffers++;
    }

    if (Buffer)
        EtwpInitializeBuffer(Logger, Buffer, ETW_BUFFER_TYPE_GENERIC);

    return Buffer;
}

static
VOID
EtwpQueueCurrentBuffer(
    _In_ PETWP_LOGGER Logger)
{
    PWMI_BUFFER_HEADER Buffer = Logger->CurrentBuffer;

    Buffer->NextBuffer = NULL;
    if (Logger->FlushTail)
        Logger->FlushTail->NextBuffer = Buffer;
    else
        Logger->FlushHead = Buffer;
    Logger->FlushTail = Buffer;
    Logger->CurrentBuffer = NULL;
}

static
VOID
EtwpSealBuffer(
    _In_ PETWP_LOGGER Logger,
    _Inout_ PWMI_BUFFER_HEADER Buffer)
{
    ULONG Used = Buffer->CurrentOffset;

    Buffer->NextBuffer = NULL;
    Buffer->SavedOffset = Used;
    Buffer->Offset = Used;
    Buffer->State = EtwBufferStateFlush;
    Buffer->BufferFlag = ETW_BUFFER_FLAG_FLUSH_MARKER | ETW_BUFFER_FLAG_PROC_INDEX;
    RtlFillMemory((PUCHAR)Buffer + Used, Logger->BufferSize - Used, 0xFF);
}

static
VOID
EtwpWriteBuffer(
    _In_ PETWP_LOGGER Logger,
    _Inout_ PWMI_BUFFER_HEADER Buffer)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Offset;
    NTSTATUS Status;

    Buffer->TimeStamp.QuadPart = EtwpQueryClock(Logger);
    Buffer->SequenceNumber = ++Logger->SequenceNumber;
    EtwpSealBuffer(Logger, Buffer);

    if (!Logger->LogFileHandle)
    {
        if (Logger->LogFileName.Length)
            Logger->LogBuffersLost++;
        else if (Logger->LogFileMode & EVENT_TRACE_REAL_TIME_MODE)
            Logger->RealTimeBuffersLost++;
        return;
    }

    if (Logger->LimitReached)
        return;

    if (Logger->FileLimit && Logger->FileOffset + Logger->BufferSize > Logger->FileLimit)
    {
        if (!(Logger->LogFileMode & EVENT_TRACE_FILE_MODE_CIRCULAR))
        {
            Logger->LimitReached = TRUE;
            KeSetEvent(&Logger->WakeEvent, IO_NO_INCREMENT, FALSE);
            return;
        }
        Logger->FileOffset = Logger->BufferSize;
    }

    Offset.QuadPart = Logger->FileOffset;
    Status = ZwWriteFile(Logger->LogFileHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         Buffer,
                         Logger->BufferSize,
                         &Offset,
                         NULL);
    if (!NT_SUCCESS(Status))
    {
        Logger->LogBuffersLost++;
        return;
    }

    Logger->FileOffset += Logger->BufferSize;
    Logger->BuffersWritten++;
}

static
NTSTATUS
EtwpWriteHeaderBuffer(
    _In_ PETWP_LOGGER Logger)
{
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Offset;

    EtwpSealBuffer(Logger, Logger->HeaderBuffer);
    Offset.QuadPart = 0;
    return ZwWriteFile(Logger->LogFileHandle,
                       NULL,
                       NULL,
                       NULL,
                       &IoStatusBlock,
                       Logger->HeaderBuffer,
                       Logger->BufferSize,
                       &Offset,
                       NULL);
}

static
NTSTATUS
EtwpBuildHeaderBuffer(
    _In_ PETWP_LOGGER Logger)
{
    PWMI_BUFFER_HEADER Buffer;
    PSYSTEM_TRACE_HEADER SystemHeader;
    PTRACE_LOGFILE_HEADER LogHeader;
    LARGE_INTEGER Frequency;
    PUCHAR Strings;
    ULONG Size;

    Size = sizeof(SYSTEM_TRACE_HEADER) + sizeof(TRACE_LOGFILE_HEADER) +
           Logger->LoggerName.Length + sizeof(WCHAR) +
           Logger->LogFileName.Length + sizeof(WCHAR);
    if (Size > MAXUSHORT ||
        sizeof(WMI_BUFFER_HEADER) + ALIGN_UP_BY(Size, sizeof(ULONG64)) > Logger->BufferSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Buffer = Logger->HeaderBuffer;
    if (!Buffer)
    {
        Buffer = ExAllocatePoolWithTag(Logger->PoolType, Logger->BufferSize, ETWP_TAG);
        if (!Buffer)
            return STATUS_NO_MEMORY;
        Logger->HeaderBuffer = Buffer;
    }

    EtwpInitializeBuffer(Logger, Buffer, ETW_BUFFER_TYPE_HEADER);
    Buffer->ClientContext.ProcessorNumber = 0;

    SystemHeader = (PSYSTEM_TRACE_HEADER)(Buffer + 1);
    RtlZeroMemory(SystemHeader, Size);
    SystemHeader->Marker = TRACE_HEADER_FLAG | TRACE_HEADER_EVENT_TRACE | SYSTEM_TRACE_VERSION |
                           ((sizeof(PVOID) == sizeof(ULONG64) ? TRACE_HEADER_TYPE_SYSTEM64 :
                                                               TRACE_HEADER_TYPE_SYSTEM32) << 16);
    SystemHeader->Packet.Size = (USHORT)Size;
    SystemHeader->Packet.HookId = WMI_LOG_TYPE_HEADER;
    SystemHeader->ThreadId = HandleToUlong(PsGetCurrentThreadId());
    SystemHeader->ProcessId = HandleToUlong(PsGetCurrentProcessId());
    SystemHeader->SystemTime.QuadPart = EtwpQueryClock(Logger);

    LogHeader = (PTRACE_LOGFILE_HEADER)(SystemHeader + 1);
    LogHeader->BufferSize = Logger->BufferSize;
    LogHeader->VersionDetail.MajorVersion = (UCHAR)NtMajorVersion;
    LogHeader->VersionDetail.MinorVersion = (UCHAR)NtMinorVersion;
    LogHeader->VersionDetail.SubVersion = 1;
    LogHeader->VersionDetail.SubMinorVersion = 5;
    LogHeader->ProviderVersion = NtBuildNumber & 0xFFFF;
    LogHeader->NumberOfProcessors = KeNumberProcessors;
    LogHeader->TimerResolution = KeMaximumIncrement;
    LogHeader->MaximumFileSize = Logger->MaximumFileSize;
    LogHeader->LogFileMode = Logger->LogFileMode & ~EVENT_TRACE_PERSIST_ON_HYBRID_SHUTDOWN;
    LogHeader->BuffersWritten = 1;
    LogHeader->StartBuffers = 1;
    LogHeader->PointerSize = sizeof(PVOID);
    LogHeader->CpuSpeedInMHz = KeGetCurrentPrcb()->MHz;
    LogHeader->TimeZone = ExpTimeZoneInfo;
    LogHeader->BootTime = KeBootTime;
    KeQueryPerformanceCounter(&Frequency);
    LogHeader->PerfFreq = Frequency;
    LogHeader->StartTime = Logger->StartTime;
    LogHeader->ReservedFlags = Logger->ClockType;

    Strings = (PUCHAR)(LogHeader + 1);
    RtlCopyMemory(Strings, Logger->LoggerName.Buffer, Logger->LoggerName.Length);
    Strings += Logger->LoggerName.Length + sizeof(WCHAR);
    RtlCopyMemory(Strings, Logger->LogFileName.Buffer, Logger->LogFileName.Length);

    Buffer->CurrentOffset = sizeof(*Buffer) + ALIGN_UP_BY(Size, sizeof(ULONG64));
    return STATUS_SUCCESS;
}

static
VOID
EtwpFinishLogFile(
    _In_ PETWP_LOGGER Logger)
{
    PTRACE_LOGFILE_HEADER LogHeader;

    if (!Logger->LogFileHandle)
        return;

    LogHeader = (PTRACE_LOGFILE_HEADER)((PUCHAR)(Logger->HeaderBuffer + 1) + sizeof(SYSTEM_TRACE_HEADER));
    KeQuerySystemTime(&LogHeader->EndTime);
    LogHeader->BuffersWritten = Logger->BuffersWritten;
    LogHeader->EventsLost = Logger->EventsLost;
    LogHeader->BuffersLost = Logger->LogBuffersLost;
    EtwpWriteHeaderBuffer(Logger);

    ZwClose(Logger->LogFileHandle);
    Logger->LogFileHandle = NULL;
}

static
VOID
EtwpFlushBuffers(
    _In_ PETWP_LOGGER Logger,
    _In_ BOOLEAN IncludeCurrent)
{
    PWMI_BUFFER_HEADER Buffer, List;

    KeWaitForSingleObject(&Logger->FileLock, Executive, KernelMode, FALSE, NULL);

    KeAcquireGuardedMutex(&Logger->BufferLock);
    if (IncludeCurrent &&
        Logger->CurrentBuffer &&
        Logger->CurrentBuffer->CurrentOffset > sizeof(WMI_BUFFER_HEADER))
    {
        EtwpQueueCurrentBuffer(Logger);
    }
    List = Logger->FlushHead;
    Logger->FlushHead = NULL;
    Logger->FlushTail = NULL;
    KeReleaseGuardedMutex(&Logger->BufferLock);

    while (List)
    {
        Buffer = List;
        List = Buffer->NextBuffer;
        EtwpWriteBuffer(Logger, Buffer);

        KeAcquireGuardedMutex(&Logger->BufferLock);
        Buffer->NextBuffer = Logger->FreeList;
        Logger->FreeList = Buffer;
        Logger->FreeBuffers++;
        KeReleaseGuardedMutex(&Logger->BufferLock);
    }

    KeReleaseMutex(&Logger->FileLock, FALSE);
}

static
BOOLEAN
EtwpUnlinkLogger(
    _In_ PETWP_LOGGER Logger)
{
    BOOLEAN Removed = FALSE;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&EtwpLoggerLock);
    if (EtwpLoggers[Logger->LoggerId] == Logger)
    {
        EtwpLoggers[Logger->LoggerId] = NULL;
        Removed = TRUE;
    }
    ExReleasePushLockExclusive(&EtwpLoggerLock);
    KeLeaveCriticalRegion();

    return Removed;
}

static
VOID
NTAPI
EtwpLoggerThread(
    _In_ PVOID Context)
{
    PETWP_LOGGER Logger = Context;
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER TimeoutPointer;
    BOOLEAN SelfStop = FALSE;
    ULONG FlushTimer;
    NTSTATUS Status;

    for (;;)
    {
        FlushTimer = ReadULongNoFence(&Logger->FlushTimer);
        TimeoutPointer = NULL;
        if (FlushTimer)
        {
            Timeout.QuadPart = -(LONGLONG)FlushTimer * 10000000;
            TimeoutPointer = &Timeout;
        }

        Status = KeWaitForSingleObject(&Logger->WakeEvent, Executive, KernelMode, FALSE, TimeoutPointer);
        if (ReadBooleanNoFence(&Logger->StopRequested))
            break;

        EtwpFlushBuffers(Logger, Status == STATUS_TIMEOUT);

        if (ReadBooleanNoFence(&Logger->LimitReached) && EtwpUnlinkLogger(Logger))
        {
            KeAcquireGuardedMutex(&Logger->BufferLock);
            Logger->StopRequested = TRUE;
            KeReleaseGuardedMutex(&Logger->BufferLock);
            SelfStop = TRUE;
            break;
        }
    }

    EtwpFlushBuffers(Logger, TRUE);

    KeWaitForSingleObject(&Logger->FileLock, Executive, KernelMode, FALSE, NULL);
    EtwpFinishLogFile(Logger);
    KeReleaseMutex(&Logger->FileLock, FALSE);

    if (SelfStop)
        EtwpDereferenceLogger(Logger);

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
ULONGLONG
EtwpFileLimit(
    _In_ ULONG LogFileMode,
    _In_ ULONG MaximumFileSize)
{
    if (LogFileMode & EVENT_TRACE_USE_KBYTES_FOR_SIZE)
        return (ULONGLONG)MaximumFileSize << 10;

    return (ULONGLONG)MaximumFileSize << 20;
}

static
NTSTATUS
EtwpOpenLogFile(
    _In_ PCUNICODE_STRING DosName,
    _In_ ULONG LogFileMode,
    _In_ ULONG MaximumFileSize,
    _Out_ PHANDLE FileHandle)
{
    static const UNICODE_STRING DosPrefix = RTL_CONSTANT_STRING(L"\\??\\");
    static const UNICODE_STRING UncPrefix = RTL_CONSTANT_STRING(L"\\??\\UNC");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER AllocationSize;
    UNICODE_STRING NtName, Remainder;
    PCUNICODE_STRING Prefix = &DosPrefix;
    NTSTATUS Status;

    Remainder = *DosName;
    if (Remainder.Length >= 2 * sizeof(WCHAR) &&
        Remainder.Buffer[0] == L'\\' &&
        Remainder.Buffer[1] == L'\\')
    {
        Prefix = &UncPrefix;
        Remainder.Buffer++;
        Remainder.Length -= sizeof(WCHAR);
    }

    NtName.Length = 0;
    NtName.MaximumLength = Prefix->Length + Remainder.Length + sizeof(WCHAR);
    NtName.Buffer = ExAllocatePoolWithTag(PagedPool, NtName.MaximumLength, ETWP_TAG);
    if (!NtName.Buffer)
        return STATUS_NO_MEMORY;
    RtlAppendUnicodeStringToString(&NtName, Prefix);
    RtlAppendUnicodeStringToString(&NtName, &Remainder);

    InitializeObjectAttributes(&ObjectAttributes,
                               &NtName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE | OBJ_FORCE_ACCESS_CHECK,
                               NULL,
                               NULL);
    AllocationSize.QuadPart = EtwpFileLimit(LogFileMode, MaximumFileSize);
    Status = IoCreateFile(FileHandle,
                          FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          (LogFileMode & EVENT_TRACE_FILE_MODE_PREALLOCATE) ? &AllocationSize : NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ,
                          FILE_OVERWRITE_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL,
                          0,
                          CreateFileTypeNone,
                          NULL,
                          IO_NO_PARAMETER_CHECKING | IO_FORCE_ACCESS_CHECK);
    ExFreePoolWithTag(NtName.Buffer, ETWP_TAG);

    return Status;
}

static
NTSTATUS
EtwpDuplicateString(
    _In_ PCUNICODE_STRING Source,
    _Out_ PUNICODE_STRING Destination)
{
    Destination->Length = Source->Length;
    Destination->MaximumLength = Source->Length + sizeof(WCHAR);
    Destination->Buffer = ExAllocatePoolWithTag(PagedPool, Destination->MaximumLength, ETWP_TAG);
    if (!Destination->Buffer)
        return STATUS_NO_MEMORY;

    RtlCopyMemory(Destination->Buffer, Source->Buffer, Source->Length);
    Destination->Buffer[Source->Length / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
NTSTATUS
EtwpStartLogger(
    _In_ PWMI_LOGGER_INFORMATION Info,
    _In_ PCUNICODE_STRING LoggerName,
    _In_ PCUNICODE_STRING LogFileName,
    _Out_ PETWP_LOGGER *OutLogger)
{
    static const UNICODE_STRING KernelLoggerName = RTL_CONSTANT_STRING(KERNEL_LOGGER_NAMEW);
    static const GUID NullGuid = { 0 };
    OBJECT_ATTRIBUTES ObjectAttributes;
    PWMI_BUFFER_HEADER Buffer;
    PETWP_LOGGER Logger;
    HANDLE ThreadHandle;
    CLIENT_ID ClientId;
    ULONG Mode = Info->LogFileMode;
    ULONG MinimumBuffers, i, Slot;
    NTSTATUS Status;

    if (!LoggerName->Length)
        return STATUS_OBJECT_NAME_INVALID;
    if ((Mode & EVENT_TRACE_FILE_MODE_SEQUENTIAL) && (Mode & EVENT_TRACE_FILE_MODE_CIRCULAR))
        return STATUS_INVALID_PARAMETER;
    if ((Mode & (EVENT_TRACE_FILE_MODE_CIRCULAR | EVENT_TRACE_FILE_MODE_NEWFILE | EVENT_TRACE_FILE_MODE_PREALLOCATE)) &&
        !Info->MaximumFileSize)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if ((Mode & EVENT_TRACE_BUFFERING_MODE) && LogFileName->Length)
        return STATUS_INVALID_PARAMETER;
    if (!(Mode & (EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_BUFFERING_MODE)) && !LogFileName->Length)
        return STATUS_OBJECT_PATH_INVALID;
    if ((Mode & ETWP_UNSUPPORTED_MODES) ||
        IsEqualGUID(&Info->Wnode.Guid, &SystemTraceControlGuid) ||
        RtlEqualUnicodeString(LoggerName, &KernelLoggerName, TRUE))
    {
        return STATUS_NOT_SUPPORTED;
    }

    Logger = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Logger), ETWP_TAG);
    if (!Logger)
        return STATUS_NO_MEMORY;
    RtlZeroMemory(Logger, sizeof(*Logger));
    Logger->ReferenceCount = 2;
    KeInitializeEvent(&Logger->WakeEvent, SynchronizationEvent, FALSE);
    KeInitializeMutex(&Logger->FileLock, 0);
    KeInitializeGuardedMutex(&Logger->BufferLock);

    Mode &= ETWP_LOGGER_MODE_MASK;
    if (!(Mode & EVENT_TRACE_STOP_ON_HYBRID_SHUTDOWN))
        Mode |= EVENT_TRACE_PERSIST_ON_HYBRID_SHUTDOWN;
    Logger->LogFileMode = Mode;
    Logger->PoolType = (Mode & EVENT_TRACE_USE_PAGED_MEMORY) ? PagedPool : NonPagedPoolNx;

    Logger->BufferSizeKb = Info->BufferSize ? Info->BufferSize : ETWP_DEFAULT_BUFFER_SIZE;
    if (Logger->BufferSizeKb > ETWP_MAX_BUFFER_SIZE)
        Logger->BufferSizeKb = ETWP_MAX_BUFFER_SIZE;
    Logger->BufferSize = Logger->BufferSizeKb << 10;

    MinimumBuffers = 2 * (ULONG)KeNumberProcessors;
    Logger->MinimumBuffers = max(Info->MinimumBuffers, MinimumBuffers);
    Logger->MaximumBuffers = Info->MaximumBuffers ? Info->MaximumBuffers : MinimumBuffers + ETWP_EXTRA_BUFFERS;
    if (Logger->MaximumBuffers < Logger->MinimumBuffers)
        Logger->MaximumBuffers = Logger->MinimumBuffers;

    Logger->MaximumFileSize = Info->MaximumFileSize;
    if (Mode & (EVENT_TRACE_FILE_MODE_SEQUENTIAL | EVENT_TRACE_FILE_MODE_CIRCULAR))
        Logger->FileLimit = EtwpFileLimit(Mode, Info->MaximumFileSize);
    Logger->FlushTimer = Info->FlushTimer;
    if (!Logger->FlushTimer && (Mode & EVENT_TRACE_REAL_TIME_MODE))
        Logger->FlushTimer = 1;
    Logger->ClockType = (Info->Wnode.ClientContext == EVENT_TRACE_CLOCK_SYSTEMTIME) ? EVENT_TRACE_CLOCK_SYSTEMTIME :
                                                                                      EVENT_TRACE_CLOCK_PERFCOUNTER;
    Logger->InstanceGuid = Info->Wnode.Guid;
    KeQuerySystemTime(&Logger->StartTime);

    Status = STATUS_SUCCESS;
    if (IsEqualGUID(&Logger->InstanceGuid, &NullGuid))
        Status = ExUuidCreate(&Logger->InstanceGuid);
    if (NT_SUCCESS(Status))
        Status = EtwpDuplicateString(LoggerName, &Logger->LoggerName);
    if (NT_SUCCESS(Status))
        Status = EtwpDuplicateString(LogFileName, &Logger->LogFileName);

    if (NT_SUCCESS(Status) &&
        (ULONGLONG)Logger->MinimumBuffers * Logger->BufferSize > (ULONGLONG)MmNumberOfPhysicalPages * PAGE_SIZE)
    {
        Status = STATUS_NO_MEMORY;
    }

    for (i = 0; NT_SUCCESS(Status) && i < Logger->MinimumBuffers; i++)
    {
        Buffer = ExAllocatePoolWithTag(Logger->PoolType, Logger->BufferSize, ETWP_TAG);
        if (!Buffer)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        Buffer->NextBuffer = Logger->FreeList;
        Logger->FreeList = Buffer;
        Logger->NumberOfBuffers++;
        Logger->FreeBuffers++;
    }

    if (!NT_SUCCESS(Status))
    {
        Logger->ReferenceCount = 1;
        EtwpDereferenceLogger(Logger);
        return Status;
    }

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&EtwpLoggerLock);

    Slot = 0;
    for (i = ETWP_FIRST_LOGGER_ID; i < MAXLOGGERS; i++)
    {
        if (!EtwpLoggers[i])
        {
            if (!Slot)
                Slot = i;
            continue;
        }
        if (RtlEqualUnicodeString(&EtwpLoggers[i]->LoggerName, LoggerName, TRUE) ||
            IsEqualGUID(&EtwpLoggers[i]->InstanceGuid, &Logger->InstanceGuid))
        {
            Status = STATUS_OBJECT_NAME_COLLISION;
            break;
        }
    }
    if (NT_SUCCESS(Status) && !Slot)
        Status = STATUS_INSUFFICIENT_RESOURCES;

    if (NT_SUCCESS(Status))
    {
        Logger->LoggerId = (USHORT)Slot;
        Status = EtwpBuildHeaderBuffer(Logger);
    }

    if (NT_SUCCESS(Status) && LogFileName->Length)
    {
        Status = EtwpOpenLogFile(LogFileName, Mode, Logger->MaximumFileSize, &Logger->LogFileHandle);
        if (NT_SUCCESS(Status))
            Status = EtwpWriteHeaderBuffer(Logger);
        if (NT_SUCCESS(Status))
        {
            Logger->FileOffset = Logger->BufferSize;
            Logger->BuffersWritten = 1;
        }
    }

    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
        Status = PsCreateSystemThread(&ThreadHandle,
                                      THREAD_ALL_ACCESS,
                                      &ObjectAttributes,
                                      NULL,
                                      &ClientId,
                                      EtwpLoggerThread,
                                      Logger);
        if (NT_SUCCESS(Status))
        {
            ObReferenceObjectByHandle(ThreadHandle,
                                      THREAD_ALL_ACCESS,
                                      PsThreadType,
                                      KernelMode,
                                      (PVOID*)&Logger->LoggerThread,
                                      NULL);
            ZwClose(ThreadHandle);
            Logger->LoggerThreadId = ClientId.UniqueThread;
            EtwpLoggers[Slot] = Logger;
        }
    }

    ExReleasePushLockExclusive(&EtwpLoggerLock);
    KeLeaveCriticalRegion();

    if (!NT_SUCCESS(Status))
    {
        Logger->ReferenceCount = 1;
        EtwpDereferenceLogger(Logger);
        return Status;
    }

    *OutLogger = Logger;
    return STATUS_SUCCESS;
}

static
NTSTATUS
EtwpStopLogger(
    _In_ PETWP_LOGGER Logger)
{
    if (!EtwpUnlinkLogger(Logger))
        return STATUS_WMI_INSTANCE_NOT_FOUND;

    KeAcquireGuardedMutex(&Logger->BufferLock);
    Logger->StopRequested = TRUE;
    KeReleaseGuardedMutex(&Logger->BufferLock);

    KeSetEvent(&Logger->WakeEvent, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(Logger->LoggerThread, Executive, KernelMode, FALSE, NULL);

    EtwpDereferenceLogger(Logger);
    return STATUS_SUCCESS;
}

VOID
NTAPI
EtwpShutdownLoggers(VOID)
{
    PETWP_LOGGER Logger;
    USHORT LoggerId;

    for (LoggerId = ETWP_FIRST_LOGGER_ID; LoggerId < MAXLOGGERS; LoggerId++)
    {
        Logger = EtwpReferenceLoggerById(LoggerId);
        if (!Logger)
            continue;

        EtwpStopLogger(Logger);
        EtwpDereferenceLogger(Logger);
    }
}

static
NTSTATUS
EtwpUpdateLogger(
    _In_ PETWP_LOGGER Logger,
    _In_ PWMI_LOGGER_INFORMATION Info,
    _In_ PCUNICODE_STRING LogFileName)
{
    UNICODE_STRING NewName, OldName;
    HANDLE FileHandle;
    NTSTATUS Status = STATUS_SUCCESS;

    if (LogFileName->Length)
    {
        Status = EtwpDuplicateString(LogFileName, &NewName);
        if (!NT_SUCCESS(Status))
            return Status;

        Status = EtwpOpenLogFile(LogFileName, Logger->LogFileMode, Logger->MaximumFileSize, &FileHandle);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(NewName.Buffer, ETWP_TAG);
            return Status;
        }

        EtwpFlushBuffers(Logger, TRUE);

        KeWaitForSingleObject(&Logger->FileLock, Executive, KernelMode, FALSE, NULL);
        if (Logger->StopRequested)
        {
            KeReleaseMutex(&Logger->FileLock, FALSE);
            ZwClose(FileHandle);
            ExFreePoolWithTag(NewName.Buffer, ETWP_TAG);
            return STATUS_WMI_INSTANCE_NOT_FOUND;
        }
        EtwpFinishLogFile(Logger);
        OldName = Logger->LogFileName;
        Logger->LogFileName = NewName;
        Status = EtwpBuildHeaderBuffer(Logger);
        if (NT_SUCCESS(Status))
        {
            Logger->LogFileHandle = FileHandle;
            Status = EtwpWriteHeaderBuffer(Logger);
        }
        if (NT_SUCCESS(Status))
        {
            Logger->FileOffset = Logger->BufferSize;
            Logger->BuffersWritten = 1;
            ExFreePoolWithTag(OldName.Buffer, ETWP_TAG);
        }
        else
        {
            Logger->LogFileHandle = NULL;
            Logger->LogFileName = OldName;
            ZwClose(FileHandle);
            ExFreePoolWithTag(NewName.Buffer, ETWP_TAG);
        }
        KeReleaseMutex(&Logger->FileLock, FALSE);

        if (!NT_SUCCESS(Status))
            return Status;
    }

    KeAcquireGuardedMutex(&Logger->BufferLock);
    if (Info->MaximumBuffers >= Logger->MinimumBuffers)
        Logger->MaximumBuffers = Info->MaximumBuffers;
    Logger->LogFileMode = (Logger->LogFileMode & ~EVENT_TRACE_REAL_TIME_MODE) |
                          (Info->LogFileMode & EVENT_TRACE_REAL_TIME_MODE);
    if (Info->FlushTimer)
        Logger->FlushTimer = Info->FlushTimer;
    KeReleaseGuardedMutex(&Logger->BufferLock);

    KeSetEvent(&Logger->WakeEvent, IO_NO_INCREMENT, FALSE);
    return STATUS_SUCCESS;
}

static
VOID
EtwpQueryLogger(
    _In_ PETWP_LOGGER Logger,
    _Inout_ PWMI_LOGGER_INFORMATION Info)
{
    KeWaitForSingleObject(&Logger->FileLock, Executive, KernelMode, FALSE, NULL);
    KeAcquireGuardedMutex(&Logger->BufferLock);

    Info->Wnode.ProviderId = 0;
    Info->Wnode.HistoricalContext = Logger->LoggerId;
    Info->Wnode.TimeStamp.QuadPart = 0;
    Info->Wnode.Guid = Logger->InstanceGuid;
    Info->Wnode.ClientContext = Logger->ClockType;
    Info->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    Info->BufferSize = Logger->BufferSizeKb;
    Info->MinimumBuffers = Logger->MinimumBuffers;
    Info->MaximumBuffers = Logger->MaximumBuffers;
    Info->MaximumFileSize = Logger->MaximumFileSize;
    Info->LogFileMode = Logger->LogFileMode;
    Info->FlushTimer = Logger->FlushTimer;
    Info->EnableFlags = 0;
    Info->AgeLimit = 0;
    Info->LogFileHandle64 = 0;
    Info->NumberOfBuffers = Logger->NumberOfBuffers;
    Info->FreeBuffers = Logger->FreeBuffers;
    Info->EventsLost = Logger->EventsLost;
    Info->BuffersWritten = Logger->BuffersWritten;
    Info->LogBuffersLost = Logger->LogBuffersLost;
    Info->RealTimeBuffersLost = Logger->RealTimeBuffersLost;
    Info->LoggerThreadId64 = (ULONG_PTR)Logger->LoggerThreadId;
    Info->RealTimeConsumerCount = 0;

    KeReleaseGuardedMutex(&Logger->BufferLock);
    KeReleaseMutex(&Logger->FileLock, FALSE);
}

static
NTSTATUS
EtwpCaptureString(
    _In_ PUNICODE_STRING64 Source,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PUNICODE_STRING Destination)
{
    PVOID UserBuffer = (PVOID)(ULONG_PTR)Source->Buffer;
    NTSTATUS Status = STATUS_SUCCESS;

    RtlInitEmptyUnicodeString(Destination, NULL, 0);
    if (!Source->Length || !UserBuffer)
        return STATUS_SUCCESS;
    if (Source->Length > ETWP_MAX_NAME_LENGTH || (Source->Length & 1))
        return STATUS_INVALID_PARAMETER;

    Destination->MaximumLength = Source->Length + sizeof(WCHAR);
    Destination->Buffer = ExAllocatePoolWithTag(PagedPool, Destination->MaximumLength, ETWP_TAG);
    if (!Destination->Buffer)
        return STATUS_NO_MEMORY;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForRead(UserBuffer, Source->Length, sizeof(WCHAR));
        RtlCopyMemory(Destination->Buffer, UserBuffer, Source->Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Destination->Buffer, ETWP_TAG);
        RtlInitEmptyUnicodeString(Destination, NULL, 0);
        return Status;
    }

    Destination->Length = Source->Length;
    Destination->Buffer[Source->Length / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
VOID
EtwpReturnString(
    _In_ PCUNICODE_STRING Source,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Inout_ PUNICODE_STRING64 Destination)
{
    PVOID UserBuffer = (PVOID)(ULONG_PTR)Destination->Buffer;

    Destination->Length = Source->Length;
    if (!UserBuffer || Destination->MaximumLength < Source->Length + sizeof(WCHAR))
        return;

    if (PreviousMode != KernelMode)
        ProbeForWrite(UserBuffer, Source->Length + sizeof(WCHAR), sizeof(WCHAR));
    RtlCopyMemory(UserBuffer, Source->Buffer, Source->Length);
    *(PWCHAR)((PUCHAR)UserBuffer + Source->Length) = UNICODE_NULL;
}

static
NTSTATUS
EtwpTraceClassicEvent(
    _In_opt_ HANDLE TraceHandle,
    _In_ ULONG FieldSize,
    _In_ PVOID Fields,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    MOF_FIELD MofFields[MAX_MOF_FIELDS];
    EVENT_TRACE_HEADER Header;
    PEVENT_TRACE_HEADER Record;
    PWMI_BUFFER_HEADER Buffer;
    PETWP_LOGGER Logger;
    PETHREAD Thread = PsGetCurrentThread();
    ULONG FieldCount = 0, Total = FieldSize, Reserved, i;
    BOOLEAN Narrow;
    PUCHAR Data;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Fields || FieldSize < sizeof(EVENT_TRACE_HEADER) || FieldSize > MAXUSHORT)
        return STATUS_INVALID_PARAMETER;

    Logger = EtwpReferenceLoggerById((USHORT)(ULONG_PTR)TraceHandle);
    if (!Logger)
        return STATUS_INVALID_HANDLE;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForRead(Fields, FieldSize, sizeof(UCHAR));
        RtlCopyMemory(&Header, Fields, sizeof(Header));

        if (Header.Flags & WNODE_FLAG_USE_GUID_PTR)
        {
            if (PreviousMode != KernelMode)
                ProbeForRead((PVOID)(ULONG_PTR)Header.GuidPtr, sizeof(GUID), sizeof(UCHAR));
            RtlCopyMemory(&Header.Guid, (PVOID)(ULONG_PTR)Header.GuidPtr, sizeof(GUID));
        }

        if (Header.Flags & WNODE_FLAG_USE_MOF_PTR)
        {
            FieldCount = (FieldSize - sizeof(Header)) / sizeof(MOF_FIELD);
            if (FieldCount > MAX_MOF_FIELDS)
            {
                Status = STATUS_ARRAY_BOUNDS_EXCEEDED;
            }
            else
            {
                RtlCopyMemory(MofFields, (PUCHAR)Fields + sizeof(Header), FieldCount * sizeof(MOF_FIELD));
                Total = sizeof(Header);
                for (i = 0; i < FieldCount; i++)
                {
                    if (MofFields[i].Length > MAXUSHORT)
                    {
                        Status = STATUS_BUFFER_OVERFLOW;
                        break;
                    }
                    Total += MofFields[i].Length;
                }
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    Reserved = ALIGN_UP_BY(Total, sizeof(ULONG64));
    if (NT_SUCCESS(Status) &&
        (Total > MAXUSHORT || Reserved > Logger->BufferSize - sizeof(WMI_BUFFER_HEADER)))
    {
        Status = STATUS_BUFFER_OVERFLOW;
    }
    if (!NT_SUCCESS(Status))
    {
        EtwpDereferenceLogger(Logger);
        return Status;
    }

    Narrow = (sizeof(PVOID) == sizeof(ULONG)) ||
             (PreviousMode != KernelMode && PsGetCurrentProcessWow64Process() != NULL);

    KeAcquireGuardedMutex(&Logger->BufferLock);

    if (Logger->StopRequested)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    else
    {
        Buffer = Logger->CurrentBuffer;
        if (Buffer && Buffer->CurrentOffset + Reserved > Logger->BufferSize)
        {
            EtwpQueueCurrentBuffer(Logger);
            KeSetEvent(&Logger->WakeEvent, IO_NO_INCREMENT, FALSE);
            Buffer = NULL;
        }
        if (!Buffer)
        {
            Buffer = EtwpTakeBuffer(Logger);
            Logger->CurrentBuffer = Buffer;
        }
        if (!Buffer)
        {
            Logger->EventsLost++;
            Status = STATUS_NO_MEMORY;
        }
    }

    if (NT_SUCCESS(Status))
    {
        Record = (PEVENT_TRACE_HEADER)((PUCHAR)Buffer + Buffer->CurrentOffset);
        Data = (PUCHAR)(Record + 1);

        _SEH2_TRY
        {
            if (Header.Flags & WNODE_FLAG_USE_MOF_PTR)
            {
                for (i = 0; i < FieldCount; i++)
                {
                    if (!MofFields[i].Length)
                        continue;
                    if (PreviousMode != KernelMode)
                        ProbeForRead((PVOID)(ULONG_PTR)MofFields[i].DataPtr, MofFields[i].Length, sizeof(UCHAR));
                    RtlCopyMemory(Data, (PVOID)(ULONG_PTR)MofFields[i].DataPtr, MofFields[i].Length);
                    Data += MofFields[i].Length;
                }
            }
            else
            {
                RtlCopyMemory(Data, (PUCHAR)Fields + sizeof(Header), FieldSize - sizeof(Header));
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (NT_SUCCESS(Status))
        {
            RtlZeroMemory(Record, sizeof(*Record));
            Record->Size = (USHORT)Total;
            Record->HeaderType = Narrow ? TRACE_HEADER_TYPE_FULL_HEADER32 : TRACE_HEADER_TYPE_FULL_HEADER64;
            Record->MarkerFlags = (TRACE_HEADER_FLAG | TRACE_HEADER_EVENT_TRACE) >> 24;
            Record->Class = Header.Class;
            Record->ThreadId = HandleToUlong(PsGetCurrentThreadId());
            Record->ProcessId = HandleToUlong(PsGetCurrentProcessId());
            Record->TimeStamp.QuadPart = EtwpQueryClock(Logger);
            Record->Guid = Header.Guid;
            Record->KernelTime = Thread->Tcb.KernelTime;
            Record->UserTime = Thread->Tcb.UserTime;
            RtlZeroMemory((PUCHAR)Record + Total, Reserved - Total);
            Buffer->CurrentOffset += Reserved;
        }
    }

    KeReleaseGuardedMutex(&Logger->BufferLock);
    EtwpDereferenceLogger(Logger);
    return Status;
}

NTSTATUS
NTAPI
NtTraceEvent(
    _In_opt_ HANDLE TraceHandle,
    _In_ ULONG Flags,
    _In_ ULONG TraceHeaderLength,
    _In_ PEVENT_TRACE_HEADER TraceHeader)
{
    if ((Flags & ETW_NT_TRACE_TYPE_MASK) == ETW_NT_FLAGS_TRACE_HEADER)
        return EtwpTraceClassicEvent(TraceHandle, TraceHeaderLength, TraceHeader, ExGetPreviousMode());

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtTraceControl(
    _In_ ULONG FunctionCode,
    _In_reads_bytes_opt_(InBufferLen) PVOID InBuffer,
    _In_ ULONG InBufferLen,
    _Out_writes_bytes_opt_(OutBufferLen) PVOID OutBuffer,
    _In_ ULONG OutBufferLen,
    _Out_ PULONG ReturnSize)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    UNICODE_STRING LoggerName, LogFileName;
    WMI_LOGGER_INFORMATION Info;
    PETWP_LOGGER Logger = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (FunctionCode < EtwStartLoggerCode || FunctionCode > EtwFlushLoggerCode)
    {
        UNIMPLEMENTED;
        return STATUS_NOT_IMPLEMENTED;
    }

    if (!InBuffer || InBufferLen < sizeof(Info) || !OutBuffer || OutBufferLen < sizeof(Info))
        return STATUS_INVALID_PARAMETER;

    Status = EtwpCheckControlAccess(PreviousMode);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(InBuffer, sizeof(Info), sizeof(ULONG));
            ProbeForWrite(OutBuffer, sizeof(Info), sizeof(ULONG));
            if (ReturnSize)
                ProbeForWriteUlong(ReturnSize);
        }
        RtlCopyMemory(&Info, InBuffer, sizeof(Info));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = EtwpCaptureString(&Info.LoggerName64, PreviousMode, &LoggerName);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = EtwpCaptureString(&Info.LogFileName64, PreviousMode, &LogFileName);
    if (!NT_SUCCESS(Status))
    {
        if (LoggerName.Buffer)
            ExFreePoolWithTag(LoggerName.Buffer, ETWP_TAG);
        return Status;
    }

    if (FunctionCode == EtwStartLoggerCode)
    {
        Status = EtwpStartLogger(&Info, &LoggerName, &LogFileName, &Logger);
    }
    else
    {
        if (LoggerName.Length)
            Logger = EtwpReferenceLoggerByName(&LoggerName);
        else
            Logger = EtwpReferenceLoggerById((USHORT)Info.Wnode.HistoricalContext);

        if (!Logger)
            Status = STATUS_WMI_INSTANCE_NOT_FOUND;
        else if (FunctionCode == EtwStopLoggerCode)
            Status = EtwpStopLogger(Logger);
        else if (FunctionCode == EtwUpdateLoggerCode)
            Status = EtwpUpdateLogger(Logger, &Info, &LogFileName);
        else if (FunctionCode == EtwFlushLoggerCode)
            EtwpFlushBuffers(Logger, TRUE);
    }

    if (NT_SUCCESS(Status))
    {
        EtwpQueryLogger(Logger, &Info);

        KeWaitForSingleObject(&Logger->FileLock, Executive, KernelMode, FALSE, NULL);
        _SEH2_TRY
        {
            EtwpReturnString(&Logger->LoggerName, PreviousMode, &Info.LoggerName64);
            EtwpReturnString(&Logger->LogFileName, PreviousMode, &Info.LogFileName64);
            RtlCopyMemory(OutBuffer, &Info, sizeof(Info));
            if (ReturnSize)
                *ReturnSize = sizeof(Info);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        KeReleaseMutex(&Logger->FileLock, FALSE);
    }

    if (Logger)
        EtwpDereferenceLogger(Logger);
    if (LoggerName.Buffer)
        ExFreePoolWithTag(LoggerName.Buffer, ETWP_TAG);
    if (LogFileName.Buffer)
        ExFreePoolWithTag(LogFileName.Buffer, ETWP_TAG);

    return Status;
}
