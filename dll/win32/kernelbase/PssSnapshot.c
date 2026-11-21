/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process Snapshotting process-information support
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <string.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winternl.h>
#include <processsnapshot.h>

#include "wine/kernelbase.h"

#if DBG
NTSYSAPI VOID NTAPI RtlAssert(PVOID FailedAssertion, PVOID FileName, ULONG LineNumber, PCHAR Message);
#define ASSERT(x) if (!(x)) { RtlAssert((PVOID)#x, (PVOID)__FILE__, __LINE__, (PCHAR)""); }
#else
#define ASSERT(x) ((VOID)0)
#endif

NTSYSAPI NTSTATUS WINAPI NtCreateProcessEx(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, HANDLE, ULONG, HANDLE, HANDLE, HANDLE, ULONG);

C_ASSERT(sizeof(PSS_PROCESS_INFORMATION) == (sizeof(void *) == 8 ? 704 : 636));
C_ASSERT(sizeof(void *) != 8 || FIELD_OFFSET(PSS_PROCESS_INFORMATION, ParentProcessId) == 32);
C_ASSERT(sizeof(void *) != 8 || FIELD_OFFSET(PSS_PROCESS_INFORMATION, ImageFileName) == 180);
C_ASSERT(sizeof(PSS_THREAD_INFORMATION) == 8);
C_ASSERT(sizeof(void *) != 8 || sizeof(PSS_THREAD_ENTRY) == 120);
C_ASSERT(sizeof(void *) != 8 || sizeof(PSS_HANDLE_ENTRY) == 136);
C_ASSERT(sizeof(void *) != 8 || sizeof(PSS_VA_SPACE_ENTRY) == 80);
C_ASSERT(sizeof(void *) != 8 || sizeof(PSS_AUXILIARY_PAGE_ENTRY) == 80);

/* A captured class: its entries, then a pool their pointer fields address as pool offset plus one; name lengths count the terminating NUL as Windows does */
typedef struct _PSS_BLOB
{
    PBYTE Data;
    SIZE_T Size;
    SIZE_T EntriesSize;
    DWORD Count;
} PSS_BLOB;

typedef struct _PSS_BUILDER
{
    PBYTE Entries;
    SIZE_T EntrySize;
    SIZE_T EntriesCapacity;
    DWORD Count;
    PBYTE Pool;
    SIZE_T PoolSize;
    SIZE_T PoolCapacity;
    BOOL Failed;
} PSS_BUILDER;

typedef struct _PSS_SNAPSHOT_ROS
{
    struct _PSS_SNAPSHOT_ROS *Next;
    PSS_PROCESS_INFORMATION ProcessInfo;
    HANDLE VaCloneHandle;
    BOOL HasThreads;
    BOOL HasHandles;
    BOOL HasVaSpace;
    DWORD ContextLength;
    PSS_BLOB Threads;
    PSS_BLOB Handles;
    PSS_BLOB VaSpace;
    PSS_PERFORMANCE_COUNTERS Performance;
} PSS_SNAPSHOT_ROS;

typedef struct _PSS_WALK_MARKER_ROS
{
    struct _PSS_WALK_MARKER_ROS *Next;
    PSS_ALLOCATOR Allocator;
    BOOL HasAllocator;
    ULONG_PTR Position;
    PSS_SNAPSHOT_ROS *BoundSnapshot;
    PSS_WALK_INFORMATION_CLASS BoundClass;
    PBYTE Data;
    SIZE_T EntriesSize;
    DWORD Count;
} PSS_WALK_MARKER_ROS;

static RTL_SRWLOCK PssSnapshotLock = RTL_SRWLOCK_INIT;
static PSS_SNAPSHOT_ROS *PssSnapshots;
static PSS_WALK_MARKER_ROS *PssWalkMarkers;

static PVOID
PssHeapAlloc(
    _In_ SIZE_T Size)
{
    return RtlAllocateHeap(NtCurrentTeb()->Peb->ProcessHeap, HEAP_ZERO_MEMORY, Size);
}

static VOID
PssHeapFree(
    _In_opt_ PVOID Address)
{
    if (Address) RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Address);
}

static PSS_SNAPSHOT_ROS *
PssFindSnapshot(
    _In_ HANDLE Handle)
{
    PSS_SNAPSHOT_ROS *Snapshot;

    for (Snapshot = PssSnapshots; Snapshot; Snapshot = Snapshot->Next)
        if ((HANDLE)Snapshot == Handle) return Snapshot;
    return NULL;
}

static PSS_WALK_MARKER_ROS *
PssFindWalkMarker(
    _In_ HPSSWALK Handle)
{
    PSS_WALK_MARKER_ROS *Marker;

    for (Marker = PssWalkMarkers; Marker; Marker = Marker->Next)
        if ((HPSSWALK)Marker == Handle) return Marker;
    return NULL;
}

static BOOL
PssReserve(
    _Inout_ PBYTE *Buffer,
    _Inout_ SIZE_T *Capacity,
    _In_ SIZE_T Needed)
{
    SIZE_T NewCapacity = *Capacity ? *Capacity : 4096;
    PBYTE NewBuffer;

    if (Needed <= *Capacity) return TRUE;
    while (NewCapacity < Needed)
    {
        if (NewCapacity > ((SIZE_T)-1) / 2) return FALSE;
        NewCapacity *= 2;
    }
    NewBuffer = *Buffer ? RtlReAllocateHeap(NtCurrentTeb()->Peb->ProcessHeap, HEAP_ZERO_MEMORY, *Buffer, NewCapacity)
                        : PssHeapAlloc(NewCapacity);
    if (!NewBuffer) return FALSE;
    *Buffer = NewBuffer;
    *Capacity = NewCapacity;
    return TRUE;
}

/* The entry stays at the returned address only until the next entry is added */
static PVOID
PssAddEntry(
    _Inout_ PSS_BUILDER *Builder)
{
    PVOID Entry;

    if (Builder->Failed ||
        !PssReserve(&Builder->Entries, &Builder->EntriesCapacity, (Builder->Count + 1) * Builder->EntrySize))
    {
        Builder->Failed = TRUE;
        return NULL;
    }
    Entry = Builder->Entries + Builder->Count * Builder->EntrySize;
    RtlZeroMemory(Entry, Builder->EntrySize);
    Builder->Count++;
    return Entry;
}

static ULONG_PTR
PssAddData(
    _Inout_ PSS_BUILDER *Builder,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ SIZE_T Size)
{
    SIZE_T Offset = (Builder->PoolSize + sizeof(ULONG_PTR) - 1) & ~(sizeof(ULONG_PTR) - 1);

    if (Builder->Failed || !PssReserve(&Builder->Pool, &Builder->PoolCapacity, Offset + Size + sizeof(WCHAR)))
    {
        Builder->Failed = TRUE;
        return 0;
    }
    RtlCopyMemory(Builder->Pool + Offset, Data, Size);
    RtlZeroMemory(Builder->Pool + Offset + Size, sizeof(WCHAR));
    Builder->PoolSize = Offset + Size + sizeof(WCHAR);
    return Offset + 1;
}

static DWORD
PssFinishBlob(
    _Inout_ PSS_BUILDER *Builder,
    _Out_ PSS_BLOB *Blob)
{
    SIZE_T EntriesSize = Builder->Count * Builder->EntrySize;
    DWORD Error = ERROR_SUCCESS;

    RtlZeroMemory(Blob, sizeof(*Blob));
    if (Builder->Failed)
        Error = ERROR_NOT_ENOUGH_MEMORY;
    else if ((Blob->Data = PssHeapAlloc(EntriesSize + Builder->PoolSize + 1)) == NULL)
        Error = ERROR_NOT_ENOUGH_MEMORY;
    else
    {
        if (EntriesSize) RtlCopyMemory(Blob->Data, Builder->Entries, EntriesSize);
        if (Builder->PoolSize) RtlCopyMemory(Blob->Data + EntriesSize, Builder->Pool, Builder->PoolSize);
        Blob->Size = EntriesSize + Builder->PoolSize;
        Blob->EntriesSize = EntriesSize;
        Blob->Count = Builder->Count;
    }
    PssHeapFree(Builder->Entries);
    PssHeapFree(Builder->Pool);
    RtlZeroMemory(Builder, sizeof(*Builder));
    return Error;
}

static VOID
PssFreeSnapshotData(
    _In_ PSS_SNAPSHOT_ROS *Snapshot)
{
    if (Snapshot->VaCloneHandle)
        NtClose(Snapshot->VaCloneHandle);
    PssHeapFree(Snapshot->Threads.Data);
    PssHeapFree(Snapshot->Handles.Data);
    PssHeapFree(Snapshot->VaSpace.Data);
    PssHeapFree(Snapshot);
}

static VOID
PssFileTime(
    _In_ LARGE_INTEGER Time,
    _Out_ FILETIME *FileTime)
{
    FileTime->dwLowDateTime = Time.u.LowPart;
    FileTime->dwHighDateTime = Time.u.HighPart;
}

static VOID
PssCaptureTime(
    _Out_ FILETIME *FileTime)
{
    LARGE_INTEGER Now;

    NtQuerySystemTime(&Now);
    PssFileTime(Now, FileTime);
}

typedef struct _PSS_TIMER
{
    ULONG64 Cycles;
    LARGE_INTEGER Counter;
} PSS_TIMER;

static ULONG64
PssThreadCycles(VOID)
{
    /* THREAD_CYCLE_TIME_INFORMATION: AccumulatedCycles, CurrentCycleCount */
    ULONGLONG CycleTime[2] = { 0, 0 };

    NtQueryInformationThread(NtCurrentThread(), ThreadCycleTime, CycleTime, sizeof(CycleTime), NULL);
    return CycleTime[0];
}

static VOID
PssStartTimer(
    _Out_ PSS_TIMER *Timer)
{
    Timer->Cycles = PssThreadCycles();
    QueryPerformanceCounter(&Timer->Counter);
}

static VOID
PssStopTimer(
    _In_ const PSS_TIMER *Timer,
    _Out_ UINT64 *Cycles,
    _Out_ UINT64 *WallClock)
{
    LARGE_INTEGER Counter, Frequency;

    QueryPerformanceCounter(&Counter);
    QueryPerformanceFrequency(&Frequency);
    *Cycles = PssThreadCycles() - Timer->Cycles;
    *WallClock = Frequency.QuadPart ? (UINT64)(Counter.QuadPart - Timer->Counter.QuadPart) * PSS_PERF_RESOLUTION / (UINT64)Frequency.QuadPart : 0;
}

static PVOID
PssQuerySystemInformation(
    _In_ SYSTEM_INFORMATION_CLASS InformationClass)
{
    ULONG Size = 0x10000;
    PVOID Buffer = NULL;
    NTSTATUS Status = STATUS_INFO_LENGTH_MISMATCH;

    while (Status == STATUS_INFO_LENGTH_MISMATCH && Size < 0x10000000)
    {
        PssHeapFree(Buffer);
        Buffer = PssHeapAlloc(Size);
        if (!Buffer) return NULL;
        Status = NtQuerySystemInformation(InformationClass, Buffer, Size, NULL);
        Size *= 2;
    }
    if (NT_SUCCESS(Status)) return Buffer;
    PssHeapFree(Buffer);
    return NULL;
}

static VOID
PssCaptureThreadContext(
    _In_ HANDLE Thread,
    _In_ DWORD ThreadId,
    _In_ BOOL OwnProcess,
    _In_ DWORD ContextFlags,
    _Inout_ PSS_BUILDER *Builder,
    _Inout_ PSS_THREAD_ENTRY *Entry)
{
    CONTEXT Context;
    ULONG PreviousCount = 0;
    BOOL Captured = FALSE;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = ContextFlags;
    if (OwnProcess && ThreadId == GetCurrentThreadId())
    {
        /* FIXME: the calling thread is captured where it stands, inside this function */
        RtlCaptureContext(&Context);
        Captured = TRUE;
    }
    else if (Thread && NT_SUCCESS(NtSuspendThread(Thread, &PreviousCount)))
    {
        Captured = NT_SUCCESS(NtGetContextThread(Thread, &Context));
        NtResumeThread(Thread, NULL);
        Entry->SuspendCount = (WORD)PreviousCount;
    }
    if (Captured)
    {
        Entry->ContextRecord = (PCONTEXT)PssAddData(Builder, &Context, sizeof(Context));
        if (Entry->ContextRecord) Entry->SizeOfContextRecord = sizeof(Context);
    }
}

static DWORD
PssCaptureThreads(
    _In_ DWORD ProcessId,
    _In_ BOOL Context,
    _In_ DWORD ContextFlags,
    _Inout_ PSS_SNAPSHOT_ROS *Snapshot)
{
    SYSTEM_PROCESS_INFORMATION *Process, *Buffer;
    PSS_BUILDER Builder;
    BOOL OwnProcess = ProcessId == GetCurrentProcessId();
    DWORD Index;

    RtlZeroMemory(&Builder, sizeof(Builder));
    Builder.EntrySize = sizeof(PSS_THREAD_ENTRY);
    Buffer = PssQuerySystemInformation(SystemProcessInformation);
    if (!Buffer) return ERROR_NOT_ENOUGH_MEMORY;
    for (Process = Buffer;; Process = (SYSTEM_PROCESS_INFORMATION *)((PBYTE)Process + Process->NextEntryOffset))
    {
        if (HandleToUlong(Process->UniqueProcessId) == ProcessId)
        {
            for (Index = 0; Index < Process->dwThreadCount; Index++)
            {
                const SYSTEM_THREAD_INFORMATION *Info = &Process->ti[Index];
                ACCESS_MASK Access = THREAD_QUERY_INFORMATION | (Context ? (THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME) : 0);
                OBJECT_ATTRIBUTES Attributes;
                THREAD_BASIC_INFORMATION Basic;
                KERNEL_USER_TIMES Times;
                PSS_THREAD_ENTRY Entry;
                PVOID Start = NULL;
                ULONG SuspendCount = 0;
                HANDLE Thread = NULL;
                PVOID Slot;

                RtlZeroMemory(&Entry, sizeof(Entry));
                Entry.ProcessId = ProcessId;
                Entry.ThreadId = HandleToUlong(Info->ClientId.UniqueThread);
                Entry.Priority = Info->dwCurrentPriority;
                Entry.BasePriority = Info->dwBasePriority;
                Entry.Win32StartAddress = Info->StartAddress;
                Entry.ExitStatus = STILL_ACTIVE;
                PssFileTime(Info->CreateTime, &Entry.CreateTime);
                PssFileTime(Info->KernelTime, &Entry.KernelTime);
                PssFileTime(Info->UserTime, &Entry.UserTime);
                InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
                if (!NT_SUCCESS(NtOpenThread(&Thread, Access, &Attributes, (CLIENT_ID *)&Info->ClientId)))
                    Thread = NULL;
                if (Thread)
                {
                    if (NT_SUCCESS(NtQueryInformationThread(Thread, ThreadBasicInformation, &Basic, sizeof(Basic), NULL)))
                    {
                        Entry.ExitStatus = Basic.ExitStatus;
                        Entry.TebBaseAddress = Basic.TebBaseAddress;
                        Entry.AffinityMask = Basic.AffinityMask;
                        Entry.Priority = Basic.Priority;
                        Entry.BasePriority = Basic.BasePriority;
                    }
                    if (NT_SUCCESS(NtQueryInformationThread(Thread, ThreadTimes, &Times, sizeof(Times), NULL)))
                    {
                        PssFileTime(Times.CreateTime, &Entry.CreateTime);
                        PssFileTime(Times.ExitTime, &Entry.ExitTime);
                        PssFileTime(Times.KernelTime, &Entry.KernelTime);
                        PssFileTime(Times.UserTime, &Entry.UserTime);
                    }
                    if (NT_SUCCESS(NtQueryInformationThread(Thread, ThreadQuerySetWin32StartAddress, &Start, sizeof(Start), NULL)))
                        Entry.Win32StartAddress = Start;
                    if (NT_SUCCESS(NtQueryInformationThread(Thread, ThreadSuspendCount, &SuspendCount, sizeof(SuspendCount), NULL)))
                        Entry.SuspendCount = (WORD)SuspendCount;
                }
                /* TODO: LastSyscallFirstArgument and LastSyscallNumber are not captured */
                if (Entry.ExitStatus != STILL_ACTIVE)
                    Entry.Flags |= PSS_THREAD_FLAGS_TERMINATED;
                PssCaptureTime(&Entry.CaptureTime);
                if (Context && !(Entry.Flags & PSS_THREAD_FLAGS_TERMINATED))
                    PssCaptureThreadContext(Thread, Entry.ThreadId, OwnProcess, ContextFlags, &Builder, &Entry);
                if (Thread) NtClose(Thread);
                Slot = PssAddEntry(&Builder);
                if (Slot) RtlCopyMemory(Slot, &Entry, sizeof(Entry));
            }
            break;
        }
        if (!Process->NextEntryOffset) break;
    }
    PssHeapFree(Buffer);
    Snapshot->ContextLength = Context ? sizeof(CONTEXT) : 0;
    Snapshot->HasThreads = TRUE;
    return PssFinishBlob(&Builder, &Snapshot->Threads);
}

static PSS_OBJECT_TYPE
PssObjectType(
    _In_ const UNICODE_STRING *TypeName)
{
    static const struct
    {
        PCWSTR Name;
        PSS_OBJECT_TYPE Type;
    } Types[] =
    {
        { L"Process", PSS_OBJECT_TYPE_PROCESS },
        { L"Thread", PSS_OBJECT_TYPE_THREAD },
        { L"Mutant", PSS_OBJECT_TYPE_MUTANT },
        { L"Event", PSS_OBJECT_TYPE_EVENT },
        { L"Section", PSS_OBJECT_TYPE_SECTION },
        { L"Semaphore", PSS_OBJECT_TYPE_SEMAPHORE },
    };
    UNICODE_STRING Name;
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Types); Index++)
    {
        RtlInitUnicodeString(&Name, Types[Index].Name);
        if (RtlEqualUnicodeString(&Name, TypeName, FALSE)) return Types[Index].Type;
    }
    return PSS_OBJECT_TYPE_UNKNOWN;
}

static BOOL
PssCaptureTypeSpecific(
    _In_ HANDLE Object,
    _In_ PSS_OBJECT_TYPE Type,
    _Inout_ PSS_HANDLE_ENTRY *Entry)
{
    switch (Type)
    {
        case PSS_OBJECT_TYPE_PROCESS:
        {
            PROCESS_BASIC_INFORMATION Basic;
            if (!NT_SUCCESS(NtQueryInformationProcess(Object, ProcessBasicInformation, &Basic, sizeof(Basic), NULL)))
                return FALSE;
            Entry->TypeSpecificInformation.Process.ExitStatus = Basic.ExitStatus;
            Entry->TypeSpecificInformation.Process.PebBaseAddress = Basic.PebBaseAddress;
            Entry->TypeSpecificInformation.Process.AffinityMask = Basic.AffinityMask;
            Entry->TypeSpecificInformation.Process.BasePriority = Basic.BasePriority;
            Entry->TypeSpecificInformation.Process.ProcessId = (DWORD)Basic.UniqueProcessId;
            Entry->TypeSpecificInformation.Process.ParentProcessId = (DWORD)Basic.InheritedFromUniqueProcessId;
            return TRUE;
        }
        case PSS_OBJECT_TYPE_THREAD:
        {
            THREAD_BASIC_INFORMATION Basic;
            PVOID Start = NULL;
            if (!NT_SUCCESS(NtQueryInformationThread(Object, ThreadBasicInformation, &Basic, sizeof(Basic), NULL)))
                return FALSE;
            NtQueryInformationThread(Object, ThreadQuerySetWin32StartAddress, &Start, sizeof(Start), NULL);
            Entry->TypeSpecificInformation.Thread.ExitStatus = Basic.ExitStatus;
            Entry->TypeSpecificInformation.Thread.TebBaseAddress = Basic.TebBaseAddress;
            Entry->TypeSpecificInformation.Thread.ProcessId = HandleToUlong(Basic.ClientId.UniqueProcess);
            Entry->TypeSpecificInformation.Thread.ThreadId = HandleToUlong(Basic.ClientId.UniqueThread);
            Entry->TypeSpecificInformation.Thread.AffinityMask = Basic.AffinityMask;
            Entry->TypeSpecificInformation.Thread.Priority = Basic.Priority;
            Entry->TypeSpecificInformation.Thread.BasePriority = Basic.BasePriority;
            Entry->TypeSpecificInformation.Thread.Win32StartAddress = Start;
            return TRUE;
        }
        case PSS_OBJECT_TYPE_MUTANT:
        {
            MUTANT_BASIC_INFORMATION Basic;
            if (!NT_SUCCESS(NtQueryMutant(Object, MutantBasicInformation, &Basic, sizeof(Basic), NULL)))
                return FALSE;
            Entry->TypeSpecificInformation.Mutant.CurrentCount = Basic.CurrentCount;
            Entry->TypeSpecificInformation.Mutant.Abandoned = Basic.AbandonedState;
            /* TODO: the owner process and thread of a mutant are not captured */
            return TRUE;
        }
        case PSS_OBJECT_TYPE_EVENT:
        {
            EVENT_BASIC_INFORMATION Basic;
            if (!NT_SUCCESS(NtQueryEvent(Object, EventBasicInformation, &Basic, sizeof(Basic), NULL)))
                return FALSE;
            Entry->TypeSpecificInformation.Event.ManualReset = Basic.EventType == NotificationEvent;
            Entry->TypeSpecificInformation.Event.Signaled = Basic.EventState != 0;
            return TRUE;
        }
        case PSS_OBJECT_TYPE_SECTION:
        {
            SECTION_BASIC_INFORMATION Basic;
            if (!NT_SUCCESS(NtQuerySection(Object, SectionBasicInformation, &Basic, sizeof(Basic), NULL)))
                return FALSE;
            Entry->TypeSpecificInformation.Section.BaseAddress = Basic.BaseAddress;
            Entry->TypeSpecificInformation.Section.AllocationAttributes = Basic.Attributes;
            Entry->TypeSpecificInformation.Section.MaximumSize = Basic.Size;
            return TRUE;
        }
        case PSS_OBJECT_TYPE_SEMAPHORE:
        {
            SEMAPHORE_BASIC_INFORMATION Basic;
            if (!NT_SUCCESS(NtQuerySemaphore(Object, SemaphoreBasicInformation, &Basic, sizeof(Basic), NULL)))
                return FALSE;
            Entry->TypeSpecificInformation.Semaphore.CurrentCount = Basic.CurrentCount;
            Entry->TypeSpecificInformation.Semaphore.MaximumCount = Basic.MaximumCount;
            return TRUE;
        }
        default:
            return FALSE;
    }
}

static DWORD
PssCaptureHandles(
    _In_ HANDLE ProcessHandle,
    _In_ DWORD ProcessId,
    _In_ PSS_CAPTURE_FLAGS CaptureFlags,
    _Inout_ PSS_SNAPSHOT_ROS *Snapshot)
{
    SYSTEM_HANDLE_INFORMATION_EX *Handles;
    PSS_BUILDER Builder;
    union
    {
        OBJECT_TYPE_INFORMATION Type;
        OBJECT_NAME_INFORMATION Name;
        BYTE Bytes[0x1000];
    } *Info;
    ULONG_PTR Index;

    RtlZeroMemory(&Builder, sizeof(Builder));
    Builder.EntrySize = sizeof(PSS_HANDLE_ENTRY);
    Handles = PssQuerySystemInformation(SystemExtendedHandleInformation);
    Info = PssHeapAlloc(sizeof(*Info));
    if (!Handles || !Info)
    {
        PssHeapFree(Handles);
        PssHeapFree(Info);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    for (Index = 0; Index < Handles->NumberOfHandles; Index++)
    {
        const SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX *Handle = &Handles->Handles[Index];
        PSS_OBJECT_TYPE Type = PSS_OBJECT_TYPE_UNKNOWN;
        PSS_HANDLE_ENTRY Entry;
        HANDLE Object = NULL;
        BOOL IsFile = FALSE;
        PVOID Slot;

        if (Handle->UniqueProcessId != ProcessId)
            continue;
        RtlZeroMemory(&Entry, sizeof(Entry));
        Entry.Handle = (HANDLE)Handle->HandleValue;
        PssCaptureTime(&Entry.CaptureTime);
        if (!NT_SUCCESS(NtDuplicateObject(ProcessHandle, Entry.Handle, NtCurrentProcess(), &Object, 0, 0, DUPLICATE_SAME_ACCESS)))
            Object = NULL;
        /* TODO: a handle this process cannot duplicate keeps no type, name or object information */
        if (Object && NT_SUCCESS(NtQueryObject(Object, ObjectTypeInformation, Info, sizeof(*Info), NULL)))
        {
            Entry.Flags |= PSS_HANDLE_HAVE_TYPE;
            Entry.TypeName = (wchar_t const *)PssAddData(&Builder, Info->Type.TypeName.Buffer, Info->Type.TypeName.Length);
            Entry.TypeNameLength = Info->Type.TypeName.Length + sizeof(WCHAR);
            Type = PssObjectType(&Info->Type.TypeName);
            IsFile = Info->Type.TypeName.Length == 4 * sizeof(WCHAR) && !_wcsnicmp(Info->Type.TypeName.Buffer, L"File", 4);
        }
        /* FIXME: file object names are not captured; a query can block on a synchronous pipe */
        if (Object && (CaptureFlags & PSS_CAPTURE_HANDLE_NAME_INFORMATION) && !IsFile &&
            NT_SUCCESS(NtQueryObject(Object, ObjectNameInformation, Info, sizeof(*Info), NULL)))
        {
            Entry.Flags |= PSS_HANDLE_HAVE_NAME;
            if (Info->Name.Name.Length)
            {
                Entry.ObjectName = (wchar_t const *)PssAddData(&Builder, Info->Name.Name.Buffer, Info->Name.Name.Length);
                Entry.ObjectNameLength = Info->Name.Name.Length + sizeof(WCHAR);
            }
        }
        if (Object && (CaptureFlags & PSS_CAPTURE_HANDLE_BASIC_INFORMATION))
        {
            OBJECT_BASIC_INFORMATION Basic;

            if (NT_SUCCESS(NtQueryObject(Object, ObjectBasicInformation, &Basic, sizeof(Basic), NULL)))
            {
                Entry.Flags |= PSS_HANDLE_HAVE_BASIC_INFORMATION;
                Entry.Attributes = Handle->HandleAttributes;
                Entry.GrantedAccess = Handle->GrantedAccess;
                Entry.HandleCount = Basic.HandleCount;
                Entry.PointerCount = Basic.PointerCount;
                Entry.PagedPoolCharge = Basic.PagedPoolUsage;
                Entry.NonPagedPoolCharge = Basic.NonPagedPoolUsage;
                PssFileTime(Basic.CreateTime, &Entry.CreationTime);
            }
        }
        if (Object && (CaptureFlags & PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION) &&
            PssCaptureTypeSpecific(Object, Type, &Entry))
        {
            Entry.Flags |= PSS_HANDLE_HAVE_TYPE_SPECIFIC_INFORMATION;
            Entry.ObjectType = Type;
        }
        if (Object) NtClose(Object);
        Slot = PssAddEntry(&Builder);
        if (Slot) RtlCopyMemory(Slot, &Entry, sizeof(Entry));
    }
    PssHeapFree(Handles);
    PssHeapFree(Info);
    Snapshot->HasHandles = TRUE;
    return PssFinishBlob(&Builder, &Snapshot->Handles);
}

static VOID
PssCaptureImageHeader(
    _In_ HANDLE Process,
    _In_ PVOID ImageBase,
    _Inout_ PSS_VA_SPACE_ENTRY *Entry)
{
    IMAGE_DOS_HEADER Dos;
    union
    {
        IMAGE_NT_HEADERS32 Nt32;
        IMAGE_NT_HEADERS64 Nt64;
    } Nt;
    SIZE_T Read;

    if (!ReadProcessMemory(Process, ImageBase, &Dos, sizeof(Dos), &Read) || Dos.e_magic != IMAGE_DOS_SIGNATURE ||
        Dos.e_lfanew <= 0 || Dos.e_lfanew > 0x10000000 ||
        !ReadProcessMemory(Process, (PBYTE)ImageBase + Dos.e_lfanew, &Nt, sizeof(Nt), &Read) ||
        Nt.Nt32.Signature != IMAGE_NT_SIGNATURE)
    {
        return;
    }
    Entry->TimeDateStamp = Nt.Nt32.FileHeader.TimeDateStamp;
    if (Nt.Nt32.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        Entry->SizeOfImage = Nt.Nt64.OptionalHeader.SizeOfImage;
        Entry->ImageBase = (PVOID)(ULONG_PTR)Nt.Nt64.OptionalHeader.ImageBase;
        Entry->CheckSum = Nt.Nt64.OptionalHeader.CheckSum;
    }
    else
    {
        Entry->SizeOfImage = Nt.Nt32.OptionalHeader.SizeOfImage;
        Entry->ImageBase = (PVOID)(ULONG_PTR)Nt.Nt32.OptionalHeader.ImageBase;
        Entry->CheckSum = Nt.Nt32.OptionalHeader.CheckSum;
    }
}

static DWORD
PssCaptureVaSpace(
    _In_ HANDLE Process,
    _In_ BOOL Sections,
    _Inout_ PSS_SNAPSHOT_ROS *Snapshot)
{
    union
    {
        UNICODE_STRING Name;
        BYTE Bytes[sizeof(UNICODE_STRING) + MAX_PATH * 4 * sizeof(WCHAR)];
    } *Name;
    MEMORY_BASIC_INFORMATION Basic;
    PSS_BUILDER Builder;
    ULONG_PTR Address = 0;

    RtlZeroMemory(&Builder, sizeof(Builder));
    Builder.EntrySize = sizeof(PSS_VA_SPACE_ENTRY);
    Name = PssHeapAlloc(sizeof(*Name));
    if (!Name) return ERROR_NOT_ENOUGH_MEMORY;
    while (VirtualQueryEx(Process, (PVOID)Address, &Basic, sizeof(Basic)) == sizeof(Basic) && Basic.RegionSize)
    {
        PSS_VA_SPACE_ENTRY Entry;
        ULONG_PTR Next = (ULONG_PTR)Basic.BaseAddress + Basic.RegionSize;
        PVOID Slot;

        RtlZeroMemory(&Entry, sizeof(Entry));
        Entry.BaseAddress = Basic.BaseAddress;
        Entry.AllocationBase = Basic.AllocationBase;
        Entry.AllocationProtect = Basic.AllocationProtect;
        Entry.RegionSize = Basic.RegionSize;
        Entry.State = Basic.State;
        Entry.Protect = Basic.Protect;
        Entry.Type = Basic.Type;
        if (Sections && (Basic.Type == MEM_IMAGE || Basic.Type == MEM_MAPPED))
        {
            if (Basic.Type == MEM_IMAGE)
                PssCaptureImageHeader(Process, Basic.AllocationBase, &Entry);
            if (NT_SUCCESS(NtQueryVirtualMemory(Process, Basic.BaseAddress, MemoryMappedFilenameInformation,
                                                Name, sizeof(*Name), NULL)) && Name->Name.Length)
            {
                Entry.MappedFileName = (wchar_t const *)PssAddData(&Builder, Name->Name.Buffer, Name->Name.Length);
                Entry.MappedFileNameLength = Name->Name.Length + sizeof(WCHAR);
            }
        }
        Slot = PssAddEntry(&Builder);
        if (Slot) RtlCopyMemory(Slot, &Entry, sizeof(Entry));
        if (Next <= Address) break;
        Address = Next;
    }
    PssHeapFree(Name);
    Snapshot->HasVaSpace = TRUE;
    return PssFinishBlob(&Builder, &Snapshot->VaSpace);
}

static DWORD
PssCopySnapshot(
    _In_ const PSS_SNAPSHOT_ROS *Source,
    _Out_ PSS_SNAPSHOT_ROS **Copy)
{
    PSS_SNAPSHOT_ROS *Snapshot = PssHeapAlloc(sizeof(*Snapshot));
    const PSS_BLOB *Blobs[] = { &Source->Threads, &Source->Handles, &Source->VaSpace };
    PSS_BLOB *Targets[3];
    ULONG Index;

    *Copy = NULL;
    if (!Snapshot) return ERROR_NOT_ENOUGH_MEMORY;
    RtlCopyMemory(Snapshot, Source, sizeof(*Snapshot));
    Snapshot->Next = NULL;
    Snapshot->VaCloneHandle = NULL;
    Targets[0] = &Snapshot->Threads;
    Targets[1] = &Snapshot->Handles;
    Targets[2] = &Snapshot->VaSpace;
    for (Index = 0; Index < ARRAYSIZE(Blobs); Index++)
        Targets[Index]->Data = NULL;
    for (Index = 0; Index < ARRAYSIZE(Blobs); Index++)
    {
        if (!Blobs[Index]->Data) continue;
        Targets[Index]->Data = PssHeapAlloc(Blobs[Index]->Size + 1);
        if (!Targets[Index]->Data)
        {
            PssFreeSnapshotData(Snapshot);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        RtlCopyMemory(Targets[Index]->Data, Blobs[Index]->Data, Blobs[Index]->Size);
    }
    if (Source->VaCloneHandle &&
        !NT_SUCCESS(NtDuplicateObject(NtCurrentProcess(), Source->VaCloneHandle, NtCurrentProcess(),
                                      &Snapshot->VaCloneHandle, 0, 0, DUPLICATE_SAME_ACCESS)))
    {
        Snapshot->VaCloneHandle = NULL;
        PssFreeSnapshotData(Snapshot);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    *Copy = Snapshot;
    return ERROR_SUCCESS;
}

static DWORD
PssQueryLength(
    _In_ PSS_QUERY_INFORMATION_CLASS InformationClass)
{
    switch (InformationClass)
    {
        case PSS_QUERY_PROCESS_INFORMATION: return sizeof(PSS_PROCESS_INFORMATION);
        case PSS_QUERY_VA_CLONE_INFORMATION: return sizeof(PSS_VA_CLONE_INFORMATION);
        case PSS_QUERY_AUXILIARY_PAGES_INFORMATION: return sizeof(PSS_AUXILIARY_PAGES_INFORMATION);
        case PSS_QUERY_VA_SPACE_INFORMATION: return sizeof(PSS_VA_SPACE_INFORMATION);
        case PSS_QUERY_HANDLE_INFORMATION: return sizeof(PSS_HANDLE_INFORMATION);
        case PSS_QUERY_THREAD_INFORMATION: return sizeof(PSS_THREAD_INFORMATION);
        case PSS_QUERY_HANDLE_TRACE_INFORMATION: return sizeof(PSS_HANDLE_TRACE_INFORMATION);
        case PSS_QUERY_PERFORMANCE_COUNTERS: return sizeof(PSS_PERFORMANCE_COUNTERS);
        default: return 0;
    }
}

static DWORD
PssQueryLocked(
    _In_ PSS_SNAPSHOT_ROS *Snapshot,
    _In_ PSS_QUERY_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_(BufferLength) PVOID Buffer,
    _In_ DWORD BufferLength)
{
    DWORD RequiredLength = PssQueryLength(InformationClass);
    DWORD Count;

    if (!RequiredLength) return ERROR_INVALID_PARAMETER;
    if (BufferLength != RequiredLength) return ERROR_BAD_LENGTH;
    switch (InformationClass)
    {
        case PSS_QUERY_PROCESS_INFORMATION:
            RtlCopyMemory(Buffer, &Snapshot->ProcessInfo, sizeof(Snapshot->ProcessInfo));
            return ERROR_SUCCESS;
        case PSS_QUERY_VA_CLONE_INFORMATION:
            if (!Snapshot->VaCloneHandle) return ERROR_NOT_FOUND;
            RtlCopyMemory(Buffer, &Snapshot->VaCloneHandle, sizeof(Snapshot->VaCloneHandle));
            return ERROR_SUCCESS;
        case PSS_QUERY_VA_SPACE_INFORMATION:
            if (!Snapshot->HasVaSpace) return ERROR_NOT_FOUND;
            Count = Snapshot->VaSpace.Count;
            RtlCopyMemory(Buffer, &Count, sizeof(Count));
            return ERROR_SUCCESS;
        case PSS_QUERY_HANDLE_INFORMATION:
            if (!Snapshot->HasHandles) return ERROR_NOT_FOUND;
            Count = Snapshot->Handles.Count;
            RtlCopyMemory(Buffer, &Count, sizeof(Count));
            return ERROR_SUCCESS;
        case PSS_QUERY_THREAD_INFORMATION:
        {
            PSS_THREAD_INFORMATION Info;
            if (!Snapshot->HasThreads) return ERROR_NOT_FOUND;
            Info.ThreadsCaptured = Snapshot->Threads.Count;
            Info.ContextLength = Snapshot->ContextLength;
            RtlCopyMemory(Buffer, &Info, sizeof(Info));
            return ERROR_SUCCESS;
        }
        case PSS_QUERY_PERFORMANCE_COUNTERS:
            RtlCopyMemory(Buffer, &Snapshot->Performance, sizeof(Snapshot->Performance));
            return ERROR_SUCCESS;
        default:
            /* TODO: auxiliary pages and handle traces are never captured */
            return ERROR_NOT_FOUND;
    }
}

DWORD
WINAPI
PssQuerySnapshot(
    _In_ HPSS SnapshotHandle,
    _In_ PSS_QUERY_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_(BufferLength) PVOID Buffer,
    _In_ DWORD BufferLength)
{
    PSS_SNAPSHOT_ROS *Snapshot;
    DWORD Error;

    RtlAcquireSRWLockShared(&PssSnapshotLock);
    Snapshot = PssFindSnapshot(SnapshotHandle);
    Error = Snapshot ? PssQueryLocked(Snapshot, InformationClass, Buffer, BufferLength) : ERROR_INVALID_HANDLE;
    RtlReleaseSRWLockShared(&PssSnapshotLock);
    return Error;
}

DWORD
WINAPI
PssFreeSnapshot(
    _In_ HANDLE ProcessHandle,
    _In_ HPSS SnapshotHandle)
{
    PSS_SNAPSHOT_ROS **Link;
    PSS_SNAPSHOT_ROS *Snapshot;
    PSS_WALK_MARKER_ROS *Marker;
    NTSTATUS Status;

    Status = NtCompareObjects(ProcessHandle, NtCurrentProcess());
    if (Status == STATUS_NOT_SAME_OBJECT)
        return ERROR_INVALID_HANDLE;
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    RtlAcquireSRWLockExclusive(&PssSnapshotLock);
    for (Link = &PssSnapshots; (Snapshot = *Link); Link = &Snapshot->Next)
    {
        if ((HANDLE)Snapshot != SnapshotHandle)
            continue;
        *Link = Snapshot->Next;
        /* A marker keeps its own copy of what it walks; only its binding goes */
        for (Marker = PssWalkMarkers; Marker; Marker = Marker->Next)
            if (Marker->BoundSnapshot == Snapshot) Marker->BoundSnapshot = NULL;
        RtlReleaseSRWLockExclusive(&PssSnapshotLock);
        PssFreeSnapshotData(Snapshot);
        return ERROR_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&PssSnapshotLock);
    return ERROR_INVALID_HANDLE;
}

static DWORD
PssCaptureProcessInformation(
    _In_ HANDLE ProcessHandle,
    _Inout_ PSS_SNAPSHOT_ROS *Snapshot)
{
    PSS_PROCESS_INFORMATION *Info = &Snapshot->ProcessInfo;
    PROCESS_BASIC_INFORMATION BasicInfo;
    KERNEL_USER_TIMES Times;
    VM_COUNTERS_EX Counters;
    ULONG_PTR Wow64Info = 0;
    ULONG ExecuteFlags = 0;
    DWORD ImageLength = MAX_PATH;
    NTSTATUS Status;

    Status = NtQueryInformationProcess(ProcessHandle, ProcessBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);
    Status = NtQueryInformationProcess(ProcessHandle, ProcessTimes, &Times, sizeof(Times), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);
    Status = NtQueryInformationProcess(ProcessHandle, ProcessVmCounters, &Counters, sizeof(Counters), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    Info->ExitStatus = BasicInfo.ExitStatus;
    Info->PebBaseAddress = BasicInfo.PebBaseAddress;
    Info->AffinityMask = BasicInfo.AffinityMask;
    Info->BasePriority = BasicInfo.BasePriority;
    Info->ProcessId = (DWORD)BasicInfo.UniqueProcessId;
    Info->ParentProcessId = (DWORD)BasicInfo.InheritedFromUniqueProcessId;
    PssFileTime(Times.CreateTime, &Info->CreateTime);
    PssFileTime(Times.ExitTime, &Info->ExitTime);
    PssFileTime(Times.KernelTime, &Info->KernelTime);
    PssFileTime(Times.UserTime, &Info->UserTime);
    Info->PriorityClass = GetPriorityClass(ProcessHandle);
    Info->PeakVirtualSize = Counters.PeakVirtualSize;
    Info->VirtualSize = Counters.VirtualSize;
    Info->PageFaultCount = Counters.PageFaultCount;
    Info->PeakWorkingSetSize = Counters.PeakWorkingSetSize;
    Info->WorkingSetSize = Counters.WorkingSetSize;
    Info->QuotaPeakPagedPoolUsage = Counters.QuotaPeakPagedPoolUsage;
    Info->QuotaPagedPoolUsage = Counters.QuotaPagedPoolUsage;
    Info->QuotaPeakNonPagedPoolUsage = Counters.QuotaPeakNonPagedPoolUsage;
    Info->QuotaNonPagedPoolUsage = Counters.QuotaNonPagedPoolUsage;
    Info->PagefileUsage = Counters.PagefileUsage;
    Info->PeakPagefileUsage = Counters.PeakPagefileUsage;
    Info->PrivateUsage = Counters.PrivateUsage;
    Status = NtQueryInformationProcess(ProcessHandle, ProcessExecuteFlags, &ExecuteFlags, sizeof(ExecuteFlags), NULL);
    if (NT_SUCCESS(Status))
        Info->ExecuteFlags = ExecuteFlags;
    Status = NtQueryInformationProcess(ProcessHandle, ProcessWow64Information, &Wow64Info, sizeof(Wow64Info), NULL);
    if (NT_SUCCESS(Status) && Wow64Info)
        Info->Flags |= PSS_PROCESS_FLAGS_WOW64;
    if (!QueryFullProcessImageNameW(ProcessHandle, 0, Info->ImageFileName, &ImageLength))
        return GetLastError();
    return ERROR_SUCCESS;
}

/* Every part a capture flag names is taken in turn; the first failure undoes the snapshot */
static DWORD
PssCaptureParts(
    _In_ HANDLE ProcessHandle,
    _In_ PSS_CAPTURE_FLAGS CaptureFlags,
    _In_ DWORD ThreadContextFlags,
    _Inout_ PSS_SNAPSHOT_ROS *Snapshot)
{
    PSS_PERFORMANCE_COUNTERS *Performance = &Snapshot->Performance;
    BOOL Measure = !!(CaptureFlags & PSS_CREATE_MEASURE_PERFORMANCE);
    DWORD ProcessId;
    PSS_TIMER Total, Part;
    UINT64 Cycles, WallClock;
    NTSTATUS Status;
    DWORD Error;

    PssStartTimer(&Total);
    Error = PssCaptureProcessInformation(ProcessHandle, Snapshot);
    if (Error != ERROR_SUCCESS) return Error;
    ProcessId = Snapshot->ProcessInfo.ProcessId;
    if (CaptureFlags & PSS_CAPTURE_VA_CLONE)
    {
        PssStartTimer(&Part);
        Status = NtCreateProcessEx(&Snapshot->VaCloneHandle, MAXIMUM_ALLOWED, NULL,
                                   ProcessHandle, 0, NULL, NULL, NULL, 0);
        if (!NT_SUCCESS(Status))
        {
            Snapshot->VaCloneHandle = NULL;
            return RtlNtStatusToDosError(Status);
        }
        PssStopTimer(&Part, &Cycles, &WallClock);
        if (Measure) { Performance->VaCloneCycleCount = Cycles; Performance->VaCloneWallClockPeriod = WallClock; }
    }
    if (CaptureFlags & PSS_CAPTURE_VA_SPACE)
    {
        PssStartTimer(&Part);
        Error = PssCaptureVaSpace(ProcessHandle, !!(CaptureFlags & PSS_CAPTURE_VA_SPACE_SECTION_INFORMATION), Snapshot);
        if (Error != ERROR_SUCCESS) return Error;
        PssStopTimer(&Part, &Cycles, &WallClock);
        if (Measure) { Performance->VaSpaceCycleCount = Cycles; Performance->VaSpaceWallClockPeriod = WallClock; }
    }
    if (CaptureFlags & PSS_CAPTURE_HANDLES)
    {
        PssStartTimer(&Part);
        Error = PssCaptureHandles(ProcessHandle, ProcessId, CaptureFlags, Snapshot);
        if (Error != ERROR_SUCCESS) return Error;
        PssStopTimer(&Part, &Cycles, &WallClock);
        if (Measure) { Performance->HandlesCycleCount = Cycles; Performance->HandlesWallClockPeriod = WallClock; }
    }
    if (CaptureFlags & PSS_CAPTURE_THREADS)
    {
        PssStartTimer(&Part);
        /* TODO: PSS_CAPTURE_THREAD_CONTEXT_EXTENDED captures no extended state beyond CONTEXT */
        Error = PssCaptureThreads(ProcessId,
                                  !!(CaptureFlags & (PSS_CAPTURE_THREAD_CONTEXT | PSS_CAPTURE_THREAD_CONTEXT_EXTENDED)),
                                  ThreadContextFlags, Snapshot);
        if (Error != ERROR_SUCCESS) return Error;
        PssStopTimer(&Part, &Cycles, &WallClock);
        if (Measure) { Performance->ThreadsCycleCount = Cycles; Performance->ThreadsWallClockPeriod = WallClock; }
    }
    /* TODO: handle traces, IPT traces and auxiliary pages are accepted but never captured */
    PssStopTimer(&Total, &Cycles, &WallClock);
    if (Measure) { Performance->TotalCycleCount = Cycles; Performance->TotalWallClockPeriod = WallClock; }
    return ERROR_SUCCESS;
}

DWORD
WINAPI
PssCaptureSnapshot(
    _In_ HANDLE ProcessHandle,
    _In_ PSS_CAPTURE_FLAGS CaptureFlags,
    _In_opt_ DWORD ThreadContextFlags,
    _Out_ HPSS *SnapshotHandle)
{
    PSS_SNAPSHOT_ROS *Snapshot;
    DWORD Error;

    if (SnapshotHandle)
        *SnapshotHandle = NULL;
    if (!SnapshotHandle)
        return ERROR_INVALID_PARAMETER;

    Snapshot = PssHeapAlloc(sizeof(*Snapshot));
    if (!Snapshot)
        return ERROR_NOT_ENOUGH_MEMORY;
    Error = PssCaptureParts(ProcessHandle, CaptureFlags, ThreadContextFlags, Snapshot);
    if (Error != ERROR_SUCCESS)
    {
        PssFreeSnapshotData(Snapshot);
        return Error;
    }

    RtlAcquireSRWLockExclusive(&PssSnapshotLock);
    Snapshot->Next = PssSnapshots;
    PssSnapshots = Snapshot;
    RtlReleaseSRWLockExclusive(&PssSnapshotLock);
    *SnapshotHandle = (HPSS)Snapshot;
    return ERROR_SUCCESS;
}

DWORD
WINAPI
PssDuplicateSnapshot(
    _In_ HANDLE SourceProcessHandle,
    _In_ HPSS SnapshotHandle,
    _In_ HANDLE TargetProcessHandle,
    _Out_ HPSS *TargetSnapshotHandle,
    _In_opt_ PSS_DUPLICATE_FLAGS Flags)
{
    PSS_SNAPSHOT_ROS *Source, *Copy = NULL;
    NTSTATUS Status;
    DWORD Error;

    if (!TargetSnapshotHandle)
        return ERROR_INVALID_PARAMETER;
    *TargetSnapshotHandle = NULL;
    Status = NtCompareObjects(SourceProcessHandle, NtCurrentProcess());
    if (NT_SUCCESS(Status))
        Status = NtCompareObjects(TargetProcessHandle, NtCurrentProcess());
    /* TODO: a snapshot is duplicated only within the calling process */
    if (Status == STATUS_NOT_SAME_OBJECT)
        return ERROR_NOT_SUPPORTED;
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    RtlAcquireSRWLockShared(&PssSnapshotLock);
    Source = PssFindSnapshot(SnapshotHandle);
    Error = Source ? PssCopySnapshot(Source, &Copy) : ERROR_INVALID_HANDLE;
    RtlReleaseSRWLockShared(&PssSnapshotLock);
    if (Error != ERROR_SUCCESS)
        return Error;

    RtlAcquireSRWLockExclusive(&PssSnapshotLock);
    Copy->Next = PssSnapshots;
    PssSnapshots = Copy;
    RtlReleaseSRWLockExclusive(&PssSnapshotLock);
    *TargetSnapshotHandle = (HPSS)Copy;
    if (Flags & PSS_DUPLICATE_CLOSE_SOURCE)
        PssFreeSnapshot(SourceProcessHandle, SnapshotHandle);
    return ERROR_SUCCESS;
}

DWORD
WINAPI
PssWalkMarkerCreate(
    _In_opt_ PSS_ALLOCATOR const *Allocator,
    _Out_ HPSSWALK *WalkMarkerHandle)
{
    PSS_WALK_MARKER_ROS *Marker;

    if (!WalkMarkerHandle)
        return ERROR_INVALID_PARAMETER;
    *WalkMarkerHandle = NULL;
    /* The caller's allocator provides the marker itself, as on Windows; what a walk copies comes from the heap */
    Marker = Allocator ? Allocator->AllocRoutine(Allocator->Context, sizeof(*Marker)) : PssHeapAlloc(sizeof(*Marker));
    if (!Marker)
        return ERROR_NOT_ENOUGH_MEMORY;
    RtlZeroMemory(Marker, sizeof(*Marker));
    if (Allocator)
    {
        Marker->Allocator = *Allocator;
        Marker->HasAllocator = TRUE;
    }
    RtlAcquireSRWLockExclusive(&PssSnapshotLock);
    Marker->Next = PssWalkMarkers;
    PssWalkMarkers = Marker;
    RtlReleaseSRWLockExclusive(&PssSnapshotLock);
    *WalkMarkerHandle = (HPSSWALK)Marker;
    return ERROR_SUCCESS;
}

static VOID
PssReleaseMarkerData(
    _Inout_ PSS_WALK_MARKER_ROS *Marker)
{
    PssHeapFree(Marker->Data);
    Marker->Data = NULL;
    Marker->Count = 0;
    Marker->EntriesSize = 0;
}

DWORD
WINAPI
PssWalkMarkerFree(
    _In_ HPSSWALK WalkMarkerHandle)
{
    PSS_WALK_MARKER_ROS **Link;
    PSS_WALK_MARKER_ROS *Marker;

    RtlAcquireSRWLockExclusive(&PssSnapshotLock);
    for (Link = &PssWalkMarkers; (Marker = *Link); Link = &Marker->Next)
    {
        if ((HPSSWALK)Marker != WalkMarkerHandle)
            continue;
        *Link = Marker->Next;
        RtlReleaseSRWLockExclusive(&PssSnapshotLock);
        PssReleaseMarkerData(Marker);
        if (!Marker->HasAllocator)
            PssHeapFree(Marker);
        else if (Marker->Allocator.FreeRoutine)
            Marker->Allocator.FreeRoutine(Marker->Allocator.Context, Marker);
        return ERROR_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&PssSnapshotLock);
    return ERROR_INVALID_HANDLE;
}

DWORD
WINAPI
PssWalkMarkerGetPosition(
    _In_ HPSSWALK WalkMarkerHandle,
    _Out_ ULONG_PTR *Position)
{
    PSS_WALK_MARKER_ROS *Marker;
    DWORD Error = ERROR_INVALID_HANDLE;

    RtlAcquireSRWLockShared(&PssSnapshotLock);
    Marker = PssFindWalkMarker(WalkMarkerHandle);
    if (Marker)
    {
        *Position = Marker->Position;
        Error = ERROR_SUCCESS;
    }
    RtlReleaseSRWLockShared(&PssSnapshotLock);
    return Error;
}

DWORD
WINAPI
PssWalkMarkerSetPosition(
    _In_ HPSSWALK WalkMarkerHandle,
    _In_ ULONG_PTR Position)
{
    PSS_WALK_MARKER_ROS *Marker;
    DWORD Error = ERROR_INVALID_HANDLE;

    RtlAcquireSRWLockExclusive(&PssSnapshotLock);
    Marker = PssFindWalkMarker(WalkMarkerHandle);
    if (Marker)
    {
        Marker->Position = Position;
        Error = ERROR_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&PssSnapshotLock);
    return Error;
}

DWORD
WINAPI
PssWalkMarkerSeekToBeginning(
    _In_ HPSSWALK WalkMarkerHandle)
{
    return PssWalkMarkerSetPosition(WalkMarkerHandle, 0);
}

static const PSS_BLOB *
PssWalkBlob(
    _In_ const PSS_SNAPSHOT_ROS *Snapshot,
    _In_ PSS_WALK_INFORMATION_CLASS InformationClass)
{
    switch (InformationClass)
    {
        case PSS_WALK_VA_SPACE: return Snapshot->HasVaSpace ? &Snapshot->VaSpace : NULL;
        case PSS_WALK_HANDLES: return Snapshot->HasHandles ? &Snapshot->Handles : NULL;
        case PSS_WALK_THREADS: return Snapshot->HasThreads ? &Snapshot->Threads : NULL;
        default: return NULL;
    }
}

static DWORD
PssWalkEntrySize(
    _In_ PSS_WALK_INFORMATION_CLASS InformationClass)
{
    switch (InformationClass)
    {
        case PSS_WALK_AUXILIARY_PAGES: return sizeof(PSS_AUXILIARY_PAGE_ENTRY);
        case PSS_WALK_VA_SPACE: return sizeof(PSS_VA_SPACE_ENTRY);
        case PSS_WALK_HANDLES: return sizeof(PSS_HANDLE_ENTRY);
        case PSS_WALK_THREADS: return sizeof(PSS_THREAD_ENTRY);
        default: return 0;
    }
}

static PVOID
PssMarkerPointer(
    _In_ const PSS_WALK_MARKER_ROS *Marker,
    _In_ const VOID *Encoded)
{
    ULONG_PTR Offset = (ULONG_PTR)Encoded;

    return Offset ? Marker->Data + Marker->EntriesSize + Offset - 1 : NULL;
}

/* The first walk copies the class into the marker, which owns what the returned pointers address */
static DWORD
PssBindMarker(
    _Inout_ PSS_WALK_MARKER_ROS *Marker,
    _In_ PSS_SNAPSHOT_ROS *Snapshot,
    _In_ PSS_WALK_INFORMATION_CLASS InformationClass,
    _In_ const PSS_BLOB *Blob)
{
    PBYTE Data;

    if (Marker->Data && Marker->BoundSnapshot == Snapshot && Marker->BoundClass == InformationClass)
        return ERROR_SUCCESS;
    PssReleaseMarkerData(Marker);
    Data = PssHeapAlloc(Blob->Size + 1);
    if (!Data)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (Blob->Size) RtlCopyMemory(Data, Blob->Data, Blob->Size);
    Marker->Data = Data;
    Marker->EntriesSize = Blob->EntriesSize;
    Marker->Count = Blob->Count;
    Marker->BoundSnapshot = Snapshot;
    Marker->BoundClass = InformationClass;
    return ERROR_SUCCESS;
}

static VOID
PssDecodeEntry(
    _In_ const PSS_WALK_MARKER_ROS *Marker,
    _In_ PSS_WALK_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_(EntrySize) PVOID Buffer,
    _In_ DWORD EntrySize)
{
    RtlCopyMemory(Buffer, Marker->Data + Marker->Position * EntrySize, EntrySize);
    if (InformationClass == PSS_WALK_THREADS)
    {
        PSS_THREAD_ENTRY *Entry = Buffer;
        Entry->ContextRecord = PssMarkerPointer(Marker, Entry->ContextRecord);
    }
    else if (InformationClass == PSS_WALK_HANDLES)
    {
        PSS_HANDLE_ENTRY *Entry = Buffer;
        Entry->TypeName = PssMarkerPointer(Marker, Entry->TypeName);
        Entry->ObjectName = PssMarkerPointer(Marker, Entry->ObjectName);
    }
    else if (InformationClass == PSS_WALK_VA_SPACE)
    {
        PSS_VA_SPACE_ENTRY *Entry = Buffer;
        Entry->MappedFileName = PssMarkerPointer(Marker, Entry->MappedFileName);
    }
}

DWORD
WINAPI
PssWalkSnapshot(
    _In_ HPSS SnapshotHandle,
    _In_ PSS_WALK_INFORMATION_CLASS InformationClass,
    _In_ HPSSWALK WalkMarkerHandle,
    _Out_writes_opt_(BufferLength) PVOID Buffer,
    _In_ DWORD BufferLength)
{
    PSS_SNAPSHOT_ROS *Snapshot;
    PSS_WALK_MARKER_ROS *Marker;
    const PSS_BLOB *Blob;
    DWORD EntrySize = PssWalkEntrySize(InformationClass);
    DWORD Error = ERROR_SUCCESS;

    RtlAcquireSRWLockExclusive(&PssSnapshotLock);
    Snapshot = PssFindSnapshot(SnapshotHandle);
    Marker = PssFindWalkMarker(WalkMarkerHandle);
    Blob = Snapshot ? PssWalkBlob(Snapshot, InformationClass) : NULL;
    if (!Snapshot)
        Error = ERROR_INVALID_HANDLE;
    else if (!EntrySize)
        Error = ERROR_INVALID_PARAMETER;
    else if (BufferLength != EntrySize)
        Error = ERROR_BAD_LENGTH;
    else if (!Blob)
        Error = ERROR_NOT_FOUND;
    else if (!Marker)
        Error = ERROR_INVALID_HANDLE;
    else if (!Buffer)
        Error = ERROR_INVALID_PARAMETER;
    else
        Error = PssBindMarker(Marker, Snapshot, InformationClass, Blob);
    if (Error == ERROR_SUCCESS && Marker->Position >= Marker->Count)
        Error = ERROR_NO_MORE_ITEMS;
    if (Error == ERROR_SUCCESS)
    {
        ASSERT(Marker->Data != NULL);
        ASSERT((Marker->Position + 1) * EntrySize <= Marker->EntriesSize);
        PssDecodeEntry(Marker, InformationClass, Buffer, EntrySize);
        Marker->Position++;
    }
    RtlReleaseSRWLockExclusive(&PssSnapshotLock);
    return Error;
}
