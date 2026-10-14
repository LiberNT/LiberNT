/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process debug information buffers for module and heap queries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl.h>
#define NDEBUG
#include <debug.h>

#define RTL_DEBUG_BUFFER_DEFAULT_SIZE (4 * 1024 * 1024)
#define RTL_DEBUG_BUFFER_MAX_MODULES 4096

PVOID
NTAPI
RtlpDebugBufferCommit(
    _Inout_ PRTL_DEBUG_INFORMATION Buffer,
    _In_ SIZE_T Size)
{
    ULONG_PTR Start = ALIGN_UP_BY(Buffer->OffsetFree, sizeof(PVOID));
    ULONG_PTR End;
    PVOID Base;
    SIZE_T Commit;
    NTSTATUS Status;

    if (Size > Buffer->ViewSize || Start > Buffer->ViewSize - Size)
        return NULL;

    End = Start + Size;
    if (End > Buffer->CommitSize)
    {
        Base = (PUCHAR)Buffer->ViewBaseClient + Buffer->CommitSize;
        Commit = ALIGN_UP_BY(End - Buffer->CommitSize, PAGE_SIZE);
        Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Commit, MEM_COMMIT, PAGE_READWRITE);
        if (!NT_SUCCESS(Status))
            return NULL;
        Buffer->CommitSize += Commit;
    }

    Buffer->OffsetFree = End;
    return (PUCHAR)Buffer->ViewBaseClient + Start;
}

PRTL_DEBUG_INFORMATION
NTAPI
RtlCreateQueryDebugBuffer(
    _In_ ULONG Size,
    _In_ BOOLEAN EventPair)
{
    PRTL_DEBUG_INFORMATION Buffer = NULL;
    SIZE_T ViewSize;
    SIZE_T CommitSize = PAGE_SIZE;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(EventPair);

    ViewSize = Size ? ALIGN_UP_BY(Size, PAGE_SIZE) : RTL_DEBUG_BUFFER_DEFAULT_SIZE;
    if (ViewSize < PAGE_SIZE)
        ViewSize = PAGE_SIZE;

    Status = NtAllocateVirtualMemory(NtCurrentProcess(), (PVOID *)&Buffer, 0, &ViewSize, MEM_RESERVE, PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
        return NULL;

    Status = NtAllocateVirtualMemory(NtCurrentProcess(), (PVOID *)&Buffer, 0, &CommitSize, MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        CommitSize = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), (PVOID *)&Buffer, &CommitSize, MEM_RELEASE);
        return NULL;
    }

    RtlZeroMemory(Buffer, sizeof(*Buffer));
    Buffer->ViewBaseClient = Buffer;
    Buffer->ViewSize = ViewSize;
    Buffer->CommitSize = CommitSize;
    Buffer->OffsetFree = sizeof(*Buffer);
    return Buffer;
}

NTSTATUS
NTAPI
RtlDestroyQueryDebugBuffer(
    _In_ PRTL_DEBUG_INFORMATION Buffer)
{
    PVOID Base = Buffer;
    SIZE_T Size = 0;

    return NtFreeVirtualMemory(NtCurrentProcess(), &Base, &Size, MEM_RELEASE);
}

static
NTSTATUS
RtlpQueryLocalModules(
    _Inout_ PRTL_DEBUG_INFORMATION Buffer)
{
    PRTL_PROCESS_MODULES Modules;
    ULONG Size = sizeof(RTL_PROCESS_MODULES);
    ULONG Needed;
    NTSTATUS Status;

    for (;;)
    {
        Modules = RtlpDebugBufferCommit(Buffer, Size);
        if (Modules == NULL)
            return STATUS_NO_MEMORY;

        Needed = 0;
        Status = LdrQueryProcessModuleInformation(Modules, Size, &Needed);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
            break;
        if (Needed <= Size)
            return STATUS_INFO_LENGTH_MISMATCH;
        Size = Needed;
    }

    if (NT_SUCCESS(Status))
        Buffer->Modules = Modules;
    return Status;
}

static
NTSTATUS
RtlpReadTarget(
    _In_ HANDLE Process,
    _In_ PVOID Address,
    _Out_writes_bytes_(Size) PVOID Data,
    _In_ SIZE_T Size)
{
    SIZE_T Read = 0;
    NTSTATUS Status;

    Status = NtReadVirtualMemory(Process, Address, Data, Size, &Read);
    if (NT_SUCCESS(Status) && Read != Size)
        Status = STATUS_PARTIAL_COPY;
    return Status;
}

static
NTSTATUS
RtlpQueryTargetPeb(
    _In_ HANDLE Process,
    _Out_ PPEB *Peb)
{
    PROCESS_BASIC_INFORMATION Information;
    NTSTATUS Status;

    Status = NtQueryInformationProcess(Process, ProcessBasicInformation, &Information, sizeof(Information), NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information.PebBaseAddress == NULL)
        return STATUS_UNSUCCESSFUL;

    *Peb = Information.PebBaseAddress;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtlpQueryRemoteModules(
    _In_ HANDLE Process,
    _Inout_ PRTL_DEBUG_INFORMATION Buffer)
{
    PRTL_PROCESS_MODULES Modules;
    PRTL_PROCESS_MODULE_INFORMATION Module;
    LDR_DATA_TABLE_ENTRY Entry;
    PPEB_LDR_DATA Ldr;
    PLIST_ENTRY Head;
    PLIST_ENTRY Link;
    PPEB Peb;
    WCHAR Path[MAX_PATH];
    UNICODE_STRING UnicodePath;
    ANSI_STRING AnsiPath;
    PCHAR Name;
    ULONG Count = 0;
    NTSTATUS Status;

    Status = RtlpQueryTargetPeb(Process, &Peb);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtlpReadTarget(Process, &Peb->Ldr, &Ldr, sizeof(Ldr));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Ldr == NULL)
        return STATUS_UNSUCCESSFUL;

    Head = &Ldr->InLoadOrderModuleList;
    Status = RtlpReadTarget(Process, &Head->Flink, &Link, sizeof(Link));
    if (!NT_SUCCESS(Status))
        return Status;

    Modules = RtlpDebugBufferCommit(Buffer, FIELD_OFFSET(RTL_PROCESS_MODULES, Modules));
    if (Modules == NULL)
        return STATUS_NO_MEMORY;
    Modules->NumberOfModules = 0;

    while (Link != Head && Count < RTL_DEBUG_BUFFER_MAX_MODULES)
    {
        Status = RtlpReadTarget(Process,
                                CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks),
                                &Entry,
                                sizeof(Entry));
        if (!NT_SUCCESS(Status))
            return Status;

        Module = RtlpDebugBufferCommit(Buffer, sizeof(*Module));
        if (Module == NULL)
            return STATUS_NO_MEMORY;

        RtlZeroMemory(Module, sizeof(*Module));
        Module->ImageBase = Entry.DllBase;
        Module->ImageSize = Entry.SizeOfImage;
        Module->Flags = Entry.Flags;
        Module->LoadCount = (USHORT)Entry.LoadCount;
        Module->LoadOrderIndex = (USHORT)Count;

        UnicodePath.Length = min(Entry.FullDllName.Length, (USHORT)(sizeof(Path) - sizeof(WCHAR)));
        UnicodePath.MaximumLength = sizeof(Path);
        UnicodePath.Buffer = Path;
        if (UnicodePath.Length != 0 &&
            NT_SUCCESS(RtlpReadTarget(Process, Entry.FullDllName.Buffer, Path, UnicodePath.Length)))
        {
            AnsiPath.Length = 0;
            AnsiPath.MaximumLength = sizeof(Module->FullPathName);
            AnsiPath.Buffer = (PCHAR)Module->FullPathName;
            if (NT_SUCCESS(RtlUnicodeStringToAnsiString(&AnsiPath, &UnicodePath, FALSE)))
            {
                Module->FullPathName[min(AnsiPath.Length, sizeof(Module->FullPathName) - 1)] = ANSI_NULL;
                Name = strrchr((PCHAR)Module->FullPathName, '\\');
                Module->OffsetToFileName = Name ? (USHORT)(Name - (PCHAR)Module->FullPathName + 1) : 0;
            }
        }

        Count++;
        Modules->NumberOfModules = Count;
        Link = Entry.InLoadOrderLinks.Flink;
    }

    Buffer->Modules = Modules;
    return STATUS_SUCCESS;
}

static
VOID
RtlpFillHeapEntry(
    _Out_ PRTL_HEAP_ENTRY HeapEntry,
    _In_ const RTL_HEAP_WALK_ENTRY *WalkEntry)
{
    RtlZeroMemory(HeapEntry, sizeof(*HeapEntry));
    HeapEntry->Size = WalkEntry->DataSize;
    HeapEntry->Flags = WalkEntry->Flags;

    if (WalkEntry->Flags & RTL_HEAP_ENTRY_REGION)
    {
        HeapEntry->u.s2.CommittedSize = WalkEntry->Segment.CommittedSize;
        HeapEntry->u.s2.FirstBlock = WalkEntry->Segment.FirstEntry;
    }
    else
    {
        HeapEntry->AllocatorBackTraceIndex = WalkEntry->Block.AllocatorBackTraceIndex;
        HeapEntry->u.s1.Settable = WalkEntry->Block.Settable;
        HeapEntry->u.s1.Tag = WalkEntry->Block.TagIndex;
    }
}

static
NTSTATUS
RtlpQueryLocalHeap(
    _Inout_ PRTL_DEBUG_INFORMATION Buffer,
    _Inout_ PRTL_HEAP_INFORMATION Information,
    _In_ BOOLEAN Blocks)
{
    RTL_HEAP_WALK_ENTRY WalkEntry;
    PRTL_HEAP_ENTRY HeapEntry;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!RtlLockHeap(Information->BaseAddress))
        return STATUS_UNSUCCESSFUL;

    RtlZeroMemory(&WalkEntry, sizeof(WalkEntry));
    while (NT_SUCCESS(RtlWalkHeap(Information->BaseAddress, &WalkEntry)))
    {
        if (WalkEntry.Flags & RTL_HEAP_ENTRY_REGION)
            Information->BytesCommitted += WalkEntry.Segment.CommittedSize;
        else if (WalkEntry.Flags & RTL_HEAP_ENTRY_BUSY)
            Information->BytesAllocated += WalkEntry.DataSize;

        if (!Blocks)
            continue;

        HeapEntry = RtlpDebugBufferCommit(Buffer, sizeof(*HeapEntry));
        if (HeapEntry == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        if (Information->Entries == NULL)
            Information->Entries = HeapEntry;
        RtlpFillHeapEntry(HeapEntry, &WalkEntry);
        Information->NumberOfEntries++;
    }

    RtlUnlockHeap(Information->BaseAddress);
    return Status;
}

static
PRTL_PROCESS_HEAPS
RtlpCommitHeapList(
    _Inout_ PRTL_DEBUG_INFORMATION Buffer,
    _In_ ULONG Count)
{
    PRTL_PROCESS_HEAPS Heaps;
    SIZE_T Size = FIELD_OFFSET(RTL_PROCESS_HEAPS, Heaps) + (SIZE_T)Count * sizeof(RTL_HEAP_INFORMATION);

    Heaps = RtlpDebugBufferCommit(Buffer, Size);
    if (Heaps != NULL)
    {
        RtlZeroMemory(Heaps, Size);
        Heaps->NumberOfHeaps = Count;
    }
    return Heaps;
}

static
NTSTATUS
RtlpQueryLocalHeaps(
    _Inout_ PRTL_DEBUG_INFORMATION Buffer,
    _In_ BOOLEAN Blocks)
{
    PRTL_PROCESS_HEAPS Heaps;
    PHANDLE Handles;
    ULONG Count;
    ULONG Index;
    NTSTATUS Status;

    Count = RtlGetProcessHeaps(0, NULL);
    Handles = RtlpDebugBufferCommit(Buffer, (SIZE_T)max(Count, 1) * sizeof(HANDLE));
    if (Handles == NULL)
        return STATUS_NO_MEMORY;
    Count = min(RtlGetProcessHeaps(max(Count, 1), Handles), max(Count, 1));

    Heaps = RtlpCommitHeapList(Buffer, Count);
    if (Heaps == NULL)
        return STATUS_NO_MEMORY;

    for (Index = 0; Index < Count; Index++)
    {
        Heaps->Heaps[Index].BaseAddress = Handles[Index];
        Status = RtlpQueryLocalHeap(Buffer, &Heaps->Heaps[Index], Blocks);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Buffer->Heaps = Heaps;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtlpQueryRemoteHeaps(
    _In_ HANDLE Process,
    _Inout_ PRTL_DEBUG_INFORMATION Buffer)
{
    PRTL_PROCESS_HEAPS Heaps;
    PVOID *TargetHeaps;
    PPEB Peb;
    ULONG Count;
    ULONG Index;
    PVOID Heap;
    NTSTATUS Status;

    Status = RtlpQueryTargetPeb(Process, &Peb);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtlpReadTarget(Process, &Peb->NumberOfHeaps, &Count, sizeof(Count));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = RtlpReadTarget(Process, &Peb->ProcessHeaps, &TargetHeaps, sizeof(TargetHeaps));
    if (!NT_SUCCESS(Status))
        return Status;
    if (TargetHeaps == NULL)
        Count = 0;

    Heaps = RtlpCommitHeapList(Buffer, Count);
    if (Heaps == NULL)
        return STATUS_NO_MEMORY;

    for (Index = 0; Index < Count; Index++)
    {
        Status = RtlpReadTarget(Process, &TargetHeaps[Index], &Heap, sizeof(Heap));
        if (!NT_SUCCESS(Status))
            return Status;
        Heaps->Heaps[Index].BaseAddress = Heap;
    }

    Buffer->Heaps = Heaps;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlQueryProcessDebugInformation(
    _In_ ULONG ProcessId,
    _In_ ULONG DebugInfoClassMask,
    _Inout_ PRTL_DEBUG_INFORMATION Buffer)
{
    OBJECT_ATTRIBUTES Attributes;
    CLIENT_ID ClientId;
    HANDLE Process = NULL;
    BOOLEAN Local;
    NTSTATUS Status = STATUS_SUCCESS;

    Buffer->Flags = DebugInfoClassMask;
    Buffer->TargetProcessId = ULongToHandle(ProcessId);
    Buffer->OffsetFree = sizeof(*Buffer);
    Buffer->Modules = NULL;
    Buffer->BackTraces = NULL;
    Buffer->Heaps = NULL;
    Buffer->Locks = NULL;

    Local = ULongToHandle(ProcessId) == NtCurrentTeb()->ClientId.UniqueProcess;
    if (!Local)
    {
        ClientId.UniqueProcess = ULongToHandle(ProcessId);
        ClientId.UniqueThread = NULL;
        InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
        Status = NtOpenProcess(&Process, PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, &Attributes, &ClientId);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (DebugInfoClassMask & RTL_DEBUG_QUERY_MODULES)
        Status = Local ? RtlpQueryLocalModules(Buffer) : RtlpQueryRemoteModules(Process, Buffer);

    if (NT_SUCCESS(Status) && (DebugInfoClassMask & RTL_DEBUG_QUERY_HEAPS))
    {
        Status = Local ? RtlpQueryLocalHeaps(Buffer, (DebugInfoClassMask & RTL_DEBUG_QUERY_HEAP_BLOCKS) != 0)
                       : RtlpQueryRemoteHeaps(Process, Buffer);
    }

    if (Process != NULL)
        NtClose(Process);

    return Status;
}
