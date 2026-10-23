/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux memory system calls on top of NT virtual memory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

VOID
LxMmLock(
    _In_ PLX_PROCESS Process)
{
    PKTHREAD Self = KeGetCurrentThread();

    if (Process->MmLockOwner == Self)
    {
        Process->MmLockDepth++;
        return;
    }
    KeWaitForSingleObject(&Process->MmLock, Executive, KernelMode, FALSE, NULL);
    Process->MmLockOwner = Self;
    Process->MmLockDepth = 1;
}

VOID
LxMmUnlock(
    _In_ PLX_PROCESS Process)
{
    if (--Process->MmLockDepth != 0)
        return;
    Process->MmLockOwner = NULL;
    KeSetEvent(&Process->MmLock, IO_NO_INCREMENT, FALSE);
}

ULONG
LxProtToNt(
    _In_ ULONG Prot)
{
    switch (Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC))
    {
        case LX_PROT_NONE:
            return PAGE_NOACCESS;
        case LX_PROT_READ:
            return PAGE_READONLY;
        case LX_PROT_WRITE:
        case LX_PROT_READ | LX_PROT_WRITE:
            return PAGE_READWRITE;
        case LX_PROT_EXEC:
        case LX_PROT_READ | LX_PROT_EXEC:
            return PAGE_EXECUTE_READ;
        default:
            return PAGE_EXECUTE_READWRITE;
    }
}

static
BOOLEAN
LxProtAllows(
    _In_ ULONG Prot,
    _In_ ULONG Access)
{
    if ((Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC)) == LX_PROT_NONE)
        return FALSE;
    return (Access == LX_PROT_READ) || ((Prot & Access) != 0);
}

static
BOOLEAN
LxNtAllows(
    _In_ ULONG Protect,
    _In_ ULONG Access)
{
    switch (Protect & 0xFF)
    {
        case PAGE_READONLY:
            return (Access == LX_PROT_READ);
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
            return (Access != LX_PROT_EXEC);
        case PAGE_EXECUTE_READ:
            return (Access != LX_PROT_WRITE);
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return TRUE;
        default:
            return FALSE;
    }
}

static
NTSTATUS
LxVmAllocate(
    _Inout_ PULONG64 Base,
    _In_ ULONG64 Size,
    _In_ ULONG Type,
    _In_ ULONG Protect)
{
    PVOID Address = (PVOID)(ULONG_PTR)*Base;
    SIZE_T RegionSize = (SIZE_T)Size;
    NTSTATUS Status;

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &Address, 0, &RegionSize, Type, Protect);
    if (NT_SUCCESS(Status))
        *Base = (ULONG64)(ULONG_PTR)Address;
    return Status;
}

static
NTSTATUS
LxVmFree(
    _In_ ULONG64 Base,
    _In_ ULONG64 Size,
    _In_ ULONG Type)
{
    PVOID Address = (PVOID)(ULONG_PTR)Base;
    SIZE_T RegionSize = (SIZE_T)Size;

    return ZwFreeVirtualMemory(NtCurrentProcess(), &Address, &RegionSize, Type);
}

static
NTSTATUS
LxVmProtect(
    _In_ ULONG64 Base,
    _In_ ULONG64 Size,
    _In_ ULONG Protect)
{
    PVOID Address = (PVOID)(ULONG_PTR)Base;
    SIZE_T RegionSize = (SIZE_T)Size;
    ULONG OldProtect;

    return ZwProtectVirtualMemory(NtCurrentProcess(), &Address, &RegionSize, Protect, &OldProtect);
}

static
NTSTATUS
LxVmQuery(
    _In_ ULONG64 Address,
    _Out_ PMEMORY_BASIC_INFORMATION Information)
{
    return ZwQueryVirtualMemory(NtCurrentProcess(),
                                (PVOID)(ULONG_PTR)Address,
                                MemoryBasicInformation,
                                Information,
                                sizeof(*Information),
                                NULL);
}

static
VOID
LxVmFlushCode(
    _In_ ULONG64 Base,
    _In_ ULONG64 Size)
{
    ZwFlushInstructionCache(NtCurrentProcess(), (PVOID)(ULONG_PTR)Base, (SIZE_T)Size);
}

static
PLX_VMA
LxVmaFind(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address)
{
    PLIST_ENTRY Entry;

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Address < Vma->Start)
            return NULL;
        if (Address < Vma->End)
            return Vma;
    }
    return NULL;
}

static
VOID
LxVmaFree(
    _In_ PLX_VMA Vma)
{
    if (Vma->File != NULL)
        LxFileDereference(Vma->File);
    LxFree(Vma);
}

static
LONG
LxVmaSplit(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address)
{
    PLX_VMA Vma = LxVmaFind(Process, Address);
    PLX_VMA Tail;

    if ((Vma == NULL) || (Vma->Start == Address))
        return 0;

    Tail = LxAlloc(sizeof(*Tail));
    if (Tail == NULL)
        return -LX_ENOMEM;
    Tail->Start = Address;
    Tail->End = Vma->End;
    Tail->Prot = Vma->Prot;
    Tail->Flags = Vma->Flags;
    Tail->File = Vma->File;
    Tail->Offset = (Vma->File != NULL) ? Vma->Offset + (Address - Vma->Start) : 0;
    if (Tail->File != NULL)
        LxFileReference(Tail->File);
    Vma->End = Address;
    InsertHeadList(&Vma->Link, &Tail->Link);
    return 0;
}

static
VOID
LxVmaMerge(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLIST_ENTRY Entry = Process->Vmas.Flink;

    while ((Entry != &Process->Vmas) && (Entry->Flink != &Process->Vmas))
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
        PLX_VMA Next = CONTAINING_RECORD(Entry->Flink, LX_VMA, Link);

        if (Vma->Start > End)
            break;
        if ((Next->End >= Start) &&
            (Vma->End == Next->Start) &&
            (Vma->Prot == Next->Prot) &&
            (Vma->Flags == Next->Flags) &&
            (Vma->File == Next->File) &&
            ((Vma->File == NULL) || (Vma->Offset + (Vma->End - Vma->Start) == Next->Offset)))
        {
            Vma->End = Next->End;
            RemoveEntryList(&Next->Link);
            LxVmaFree(Next);
            continue;
        }
        Entry = Entry->Flink;
    }
}

static
LONG
LxVmaRemove(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLIST_ENTRY Entry;
    LONG Error;

    Error = LxVmaSplit(Process, Start);
    if (Error == 0)
        Error = LxVmaSplit(Process, End);
    if (Error != 0)
        return Error;

    Entry = Process->Vmas.Flink;
    while (Entry != &Process->Vmas)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        Entry = Entry->Flink;
        if (Vma->Start >= End)
            break;
        if (Vma->Start >= Start)
        {
            RemoveEntryList(&Vma->Link);
            LxVmaFree(Vma);
        }
    }
    return 0;
}

static
LONG
LxVmaInsert(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Prot,
    _In_ ULONG Flags,
    _In_opt_ PLX_FILE File,
    _In_ ULONG64 Offset)
{
    PLX_VMA Vma = LxAlloc(sizeof(*Vma));
    PLIST_ENTRY Entry;
    LONG Error;

    if (Vma == NULL)
        return -LX_ENOMEM;
    Error = LxVmaRemove(Process, Start, End);
    if (Error != 0)
    {
        LxFree(Vma);
        return Error;
    }

    Vma->Start = Start;
    Vma->End = End;
    Vma->Prot = Prot;
    Vma->Flags = Flags;
    Vma->File = File;
    Vma->Offset = (File != NULL) ? Offset : 0;
    if (File != NULL)
        LxFileReference(File);

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, LX_VMA, Link)->Start >= End)
            break;
    }
    InsertTailList(Entry, &Vma->Link);
    LxVmaMerge(Process, Start, End);
    return 0;
}

static
BOOLEAN
LxVmaOverlaps(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLIST_ENTRY Entry;

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Vma->Start >= End)
            return FALSE;
        if (Vma->End > Start)
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
LxVmaCovers(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLIST_ENTRY Entry;
    ULONG64 Cursor = Start;

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Vma->End <= Cursor)
            continue;
        if (Vma->Start > Cursor)
            return FALSE;
        Cursor = Vma->End;
        if (Cursor >= End)
            return TRUE;
    }
    return FALSE;
}

static
LONG
LxVmaSetProt(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Prot)
{
    PLIST_ENTRY Entry;
    LONG Error;

    Error = LxVmaSplit(Process, Start);
    if (Error == 0)
        Error = LxVmaSplit(Process, End);
    if (Error != 0)
        return Error;

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Vma->Start >= End)
            break;
        if (Vma->Start >= Start)
            Vma->Prot = Prot;
    }
    LxVmaMerge(Process, Start, End);
    return 0;
}

static
ULONG64
LxVmAllocationEnd(
    _In_ ULONG64 AllocationBase)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = AllocationBase;

    while (NT_SUCCESS(LxVmQuery(Cursor, &Information)) &&
           ((ULONG64)(ULONG_PTR)Information.AllocationBase == AllocationBase) &&
           (Information.State != MEM_FREE))
    {
        Cursor = (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize;
    }
    return Cursor;
}

static
VOID
LxVmDecommitRange(
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Start;

    while (Cursor < End)
    {
        ULONG64 RegionEnd;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            break;
        RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, End);
        if ((Information.State == MEM_COMMIT) && (Information.Type == MEM_PRIVATE))
            LxVmFree(Cursor, RegionEnd - Cursor, MEM_DECOMMIT);
        Cursor = RegionEnd;
    }
}

static
VOID
LxVmReleaseRange(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Start, Last = 0;

    while (Cursor < End)
    {
        ULONG64 RegionEnd, AllocationBase;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            break;
        RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, End);
        AllocationBase = (ULONG64)(ULONG_PTR)Information.AllocationBase;
        if ((Information.State != MEM_FREE) && (Information.Type == MEM_PRIVATE) && (AllocationBase != Last))
        {
            Last = AllocationBase;
            if (!LxVmaOverlaps(Process, AllocationBase, LxVmAllocationEnd(AllocationBase)))
                LxVmFree(AllocationBase, 0, MEM_RELEASE);
        }
        Cursor = RegionEnd;
    }
}

static
LONG
LxVmPlace(
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Start;

    while (Cursor < End)
    {
        ULONG64 RegionEnd;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            return -LX_ENOMEM;
        RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, End);
        if (Information.State == MEM_FREE)
        {
            ULONG64 Base = LX_ALIGN_DOWN(Cursor, LX_ALLOC_GRANULARITY);
            ULONG64 Limit = LX_ALIGN_UP(RegionEnd, LX_ALLOC_GRANULARITY);

            if (!NT_SUCCESS(LxVmAllocate(&Base, Limit - Base, MEM_RESERVE, PAGE_NOACCESS)))
                return -LX_ENOMEM;
        }
        else if (Information.Type != MEM_PRIVATE)
        {
            return -LX_ENOMEM;
        }
        else if (Information.State == MEM_COMMIT)
        {
            LxVmFree(Cursor, RegionEnd - Cursor, MEM_DECOMMIT);
        }
        Cursor = RegionEnd;
    }
    return 0;
}

static
LONG
LxMmFill(
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ PLX_FILE File,
    _In_ ULONG64 Offset)
{
    PUCHAR Bounce = LxAllocPaged(LX_IO_CHUNK);
    ULONG64 Done = 0;
    LONG64 Read;
    LONG64 Error;

    if (Bounce == NULL)
        return -LX_ENOMEM;

    while (Done < Length)
    {
        ULONG Chunk = (ULONG)min(Length - Done, LX_IO_CHUNK);

        Read = LxFileReadKernel(File, Bounce, Chunk, Offset + Done);
        if (Read < 0)
        {
            LxFreePaged(Bounce);
            return (LONG)Read;
        }
        if (Read == 0)
            break;
        Error = LxCopyToUser(Address + Done, Bounce, (SIZE_T)Read);
        if (Error != 0)
        {
            LxFreePaged(Bounce);
            return (LONG)Error;
        }
        Done += (ULONG64)Read;
        if ((ULONG64)Read < Chunk)
            break;
    }

    LxFreePaged(Bounce);
    return 0;
}

static
LONG
LxMmPopulateRun(
    _In_ PLX_VMA Vma,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG NtProtect = LxProtToNt(Vma->Prot);
    ULONG64 Commit = Start;
    LONG Error;

    if (Vma->File == NULL)
        return NT_SUCCESS(LxVmAllocate(&Commit, End - Start, MEM_COMMIT, NtProtect)) ? 0 : -LX_ENOMEM;

    if (!NT_SUCCESS(LxVmAllocate(&Commit, End - Start, MEM_COMMIT, PAGE_READWRITE)))
        return -LX_ENOMEM;
    Error = LxMmFill(Start, End - Start, Vma->File, Vma->Offset + (Start - Vma->Start));
    if (Error != 0)
    {
        LxVmFree(Start, End - Start, MEM_DECOMMIT);
        return Error;
    }
    if (NtProtect != PAGE_READWRITE)
        LxVmProtect(Start, End - Start, NtProtect);
    if (Vma->Prot & LX_PROT_EXEC)
        LxVmFlushCode(Start, End - Start);
    return 0;
}

static
LONG
LxMmPopulate(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Access,
    _In_ BOOLEAN Cluster)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Start;
    LONG Populated = 0;
    LONG Error;

    while (Cursor < End)
    {
        PLX_VMA Vma = LxVmaFind(Process, Cursor);
        ULONG64 RegionStart, RegionEnd, FillStart, FillEnd;

        if ((Vma == NULL) || !LxProtAllows(Vma->Prot, Access))
            return -LX_EFAULT;
        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)) || (Information.State == MEM_FREE))
            return -LX_EFAULT;

        RegionStart = (ULONG64)(ULONG_PTR)Information.BaseAddress;
        RegionEnd = min(RegionStart + Information.RegionSize, Vma->End);
        if (Information.State == MEM_COMMIT)
        {
            if (!LxNtAllows(Information.Protect, Access))
                return -LX_EFAULT;
            Cursor = min(RegionEnd, End);
            continue;
        }

        FillStart = Cursor;
        FillEnd = min(RegionEnd, End);
        if (Cluster)
        {
            FillStart = max(max(RegionStart, Vma->Start), LX_ALIGN_DOWN(Cursor, LX_ALLOC_GRANULARITY));
            FillEnd = min(RegionEnd, LX_ALIGN_UP(Cursor + 1, LX_ALLOC_GRANULARITY));
        }
        Error = LxMmPopulateRun(Vma, FillStart, FillEnd);
        if (Error != 0)
            return Error;
        Populated++;
        Cursor = FillEnd;
    }
    return Populated;
}

BOOLEAN
LxMmHandleFault(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG Access)
{
    ULONG64 Page = LX_ALIGN_DOWN(Address, LX_PAGE_SIZE);
    PLX_THREAD Thread = LxCurrentThread();
    LONG Result;

    LxMmLock(Process);
    Result = LxMmPopulate(Process, Page, Page + LX_PAGE_SIZE, Access, TRUE);
    LxMmUnlock(Process);

    if (Result < 0)
        return FALSE;
    if ((Result > 0) || (Thread == NULL) || (Thread->FaultAddress != Page))
    {
        if (Thread != NULL)
        {
            Thread->FaultAddress = Page;
            Thread->FaultRetries = 0;
        }
        return TRUE;
    }
    return (++Thread->FaultRetries < 8);
}

BOOLEAN
LxMmFaultIn(
    _In_ ULONG64 Address,
    _In_ SIZE_T Length,
    _In_ BOOLEAN Write)
{
    PLX_THREAD Thread = LxCurrentThread();
    ULONG64 Start, End;
    LONG Result;

    if ((Thread == NULL) || (Length == 0) ||
        (KeGetCurrentIrql() != PASSIVE_LEVEL) || KeAreAllApcsDisabled())
    {
        return FALSE;
    }

    Start = LX_ALIGN_DOWN(Address, LX_PAGE_SIZE);
    End = LX_ALIGN_UP(Address + Length, LX_PAGE_SIZE);
    if (End <= Start)
        return FALSE;

    LxMmLock(Thread->Process);
    Result = LxMmPopulate(Thread->Process, Start, End, Write ? LX_PROT_WRITE : LX_PROT_READ, FALSE);
    LxMmUnlock(Thread->Process);
    return (Result > 0);
}

LONG
LxMmZero(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG64 End)
{
    ULONG64 Page = LX_ALIGN_DOWN(Address, LX_PAGE_SIZE);
    PLX_VMA Vma;
    LONG Error;

    if (End <= Address)
        return 0;

    LxMmLock(Process);
    Error = LxMmPopulate(Process, Page, End, LX_PROT_READ, FALSE);
    Vma = LxVmaFind(Process, Page);
    if ((Error >= 0) && (Vma != NULL))
    {
        LxVmProtect(Page, End - Page, PAGE_READWRITE);
        Error = (LONG)LxZeroUser(Address, (SIZE_T)(End - Address));
        LxVmProtect(Page, End - Page, LxProtToNt(Vma->Prot));
    }
    LxMmUnlock(Process);
    return (Error < 0) ? Error : 0;
}

BOOLEAN
LxMmIsMapped(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    BOOLEAN Mapped;

    LxMmLock(Process);
    Mapped = LxVmaCovers(Process, Start, End);
    LxMmUnlock(Process);
    return Mapped;
}

LONG64
LxMmMap(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot,
    _In_ ULONG Flags,
    _In_opt_ PLX_FILE File,
    _In_ ULONG64 Offset)
{
    BOOLEAN Fixed = (Flags & (LX_MAP_FIXED | LX_MAP_FIXED_NOREPLACE)) != 0;
    ULONG VmaFlags = Flags & (LX_MAP_SHARED | LX_MAP_PRIVATE | LX_VMA_HEAP | LX_VMA_STACK);
    BOOLEAN Fresh = FALSE;
    ULONG64 Base = 0;
    LONG Error = 0;

    if ((Length == 0) || (Offset & (LX_PAGE_SIZE - 1)))
        return -LX_EINVAL;
    if (Fixed && (Address & (LX_PAGE_SIZE - 1)))
        return -LX_EINVAL;

    Length = LX_ALIGN_UP(Length, LX_PAGE_SIZE);
    if ((Length == 0) || (Fixed && (Address + Length < Address)))
        return -LX_ENOMEM;
    if (Fixed && (Address < LX_MMAP_MIN_ADDR))
        return -LX_EPERM;

    LxMmLock(Process);

    if (Fixed)
    {
        if ((Flags & LX_MAP_FIXED_NOREPLACE) && LxVmaOverlaps(Process, Address, Address + Length))
            Error = -LX_EEXIST;
        else
            Error = LxVmPlace(Address, Address + Length);
        Base = Address;
    }
    else
    {
        if (Address != 0)
        {
            ULONG64 Hint = LX_ALIGN_UP(Address, LX_PAGE_SIZE);

            if ((Hint >= LX_MMAP_MIN_ADDR) && (Hint + Length > Hint) &&
                !LxVmaOverlaps(Process, Hint, Hint + Length) &&
                (LxVmPlace(Hint, Hint + Length) == 0))
            {
                Base = Hint;
            }
        }
        if (Base == 0)
        {
            if (NT_SUCCESS(LxVmAllocate(&Base,
                                        LX_ALIGN_UP(Length, LX_ALLOC_GRANULARITY),
                                        MEM_RESERVE | ((Flags & LX_VMA_STACK) ? MEM_TOP_DOWN : 0),
                                        PAGE_NOACCESS)))
            {
                Fresh = TRUE;
            }
            else
            {
                Error = -LX_ENOMEM;
            }
        }
    }

    if (Error == 0)
    {
        Error = LxVmaInsert(Process, Base, Base + Length, Prot, VmaFlags, File, Offset);
        if ((Error != 0) && Fresh)
            LxVmFree(Base, 0, MEM_RELEASE);
    }
    if (Error != 0)
    {
        LxMmUnlock(Process);
        return Error;
    }

    if ((Flags & (LX_MAP_POPULATE | LX_MAP_LOCKED)) && (Prot != LX_PROT_NONE))
        LxMmPopulate(Process, Base, Base + Length, LX_PROT_READ, FALSE);

    LxMmUnlock(Process);
    return (LONG64)Base;
}

LONG64
LxMmUnmap(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length)
{
    ULONG64 End;
    LONG Error;

    if ((Address & (LX_PAGE_SIZE - 1)) || (Length == 0))
        return -LX_EINVAL;

    Length = LX_ALIGN_UP(Length, LX_PAGE_SIZE);
    End = Address + Length;
    if ((Length == 0) || (End < Address))
        return -LX_EINVAL;

    LxMmLock(Process);
    Error = LxVmaRemove(Process, Address, End);
    if (Error == 0)
    {
        LxVmDecommitRange(Address, End);
        LxVmReleaseRange(Process, Address, End);
    }
    LxMmUnlock(Process);
    return Error;
}

LONG64
LxMmProtect(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG NtProtect = LxProtToNt(Prot);
    ULONG64 Cursor, End;
    LONG Error;

    if (Address & (LX_PAGE_SIZE - 1))
        return -LX_EINVAL;
    if (Length == 0)
        return 0;

    End = Address + LX_ALIGN_UP(Length, LX_PAGE_SIZE);
    if (End <= Address)
        return -LX_ENOMEM;

    LxMmLock(Process);
    if (!LxVmaCovers(Process, Address, End))
        Error = -LX_ENOMEM;
    else
        Error = LxVmaSetProt(Process, Address, End, Prot);

    for (Cursor = Address; (Error == 0) && (Cursor < End);)
    {
        ULONG64 RegionEnd;
        NTSTATUS Status;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            break;
        RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, End);
        if (Information.State == MEM_COMMIT)
        {
            Status = LxVmProtect(Cursor, RegionEnd - Cursor, NtProtect);
            if (!NT_SUCCESS(Status))
                Error = -LxStatusToErrno(Status);
            else if (Prot & LX_PROT_EXEC)
                LxVmFlushCode(Cursor, RegionEnd - Cursor);
        }
        Cursor = RegionEnd;
    }
    LxMmUnlock(Process);
    return Error;
}

VOID
LxMmRelease(
    _In_ PLX_PROCESS Process)
{
    while (!IsListEmpty(&Process->Vmas))
        LxVmaFree(CONTAINING_RECORD(RemoveHeadList(&Process->Vmas), LX_VMA, Link));
}

LONG
LxMmFork(
    _In_ PLX_PROCESS Child,
    _In_ PLX_PROCESS Parent)
{
    PLIST_ENTRY Entry;

    for (Entry = Parent->Vmas.Flink; Entry != &Parent->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
        PLX_VMA Copy = LxAlloc(sizeof(*Copy));

        if (Copy == NULL)
        {
            LxMmRelease(Child);
            return -LX_ENOMEM;
        }
        Copy->Start = Vma->Start;
        Copy->End = Vma->End;
        Copy->Prot = Vma->Prot;
        Copy->Flags = Vma->Flags;
        Copy->File = Vma->File;
        Copy->Offset = Vma->Offset;
        if (Copy->File != NULL)
            LxFileReference(Copy->File);
        InsertTailList(&Child->Vmas, &Copy->Link);
    }
    return 0;
}

VOID
LxMmReset(
    _In_ PLX_PROCESS Process)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = 0;

    LxMmLock(Process);
    while (NT_SUCCESS(LxVmQuery(Cursor, &Information)))
    {
        ULONG64 Next = (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize;

        if ((Information.State != MEM_FREE) && (Information.Type == MEM_PRIVATE))
            LxVmFree((ULONG64)(ULONG_PTR)Information.AllocationBase, 0, MEM_RELEASE);
        if (Next <= Cursor)
            break;
        Cursor = Next;
    }

    LxMmRelease(Process);
    Process->BrkBase = 0;
    Process->BrkCurrent = 0;
    Process->BrkCommitted = 0;
    Process->BrkLimit = 0;
    Process->BrkReserved = FALSE;
    LxMmUnlock(Process);
}

LX_SYSCALL(LxSysMmap)
{
    ULONG Flags = (ULONG)Call->Args[3] & ~(LX_VMA_HEAP | LX_VMA_STACK);
    PLX_FILE File = NULL;
    LONG64 Result;

    if (!(Flags & LX_MAP_ANONYMOUS))
    {
        File = LxFdGet(Call->Process, (LONG)Call->Args[4]);
        if (File == NULL)
            return -LX_EBADF;
        if (((File->Handle == NULL) && (File->Data == NULL)) || File->Directory || File->NtPipe)
        {
            BOOLEAN Zero = (File->Type == LX_S_IFCHR) && (File->Rdev == ((1 << 8) | 5));

            LxFileDereference(File);
            if (!Zero)
                return -LX_ENODEV;
            File = NULL;
        }
    }

    Result = LxMmMap(Call->Process, Call->Args[0], Call->Args[1], (ULONG)Call->Args[2], Flags, File, Call->Args[5]);
    if (File != NULL)
        LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysMunmap)
{
    return LxMmUnmap(Call->Process, Call->Args[0], Call->Args[1]);
}

LX_SYSCALL(LxSysMprotect)
{
    return LxMmProtect(Call->Process, Call->Args[0], Call->Args[1], (ULONG)Call->Args[2]);
}

LX_SYSCALL(LxSysMadvise)
{
    PLX_PROCESS Process = Call->Process;
    ULONG64 Address = Call->Args[0];
    ULONG64 End = Address + LX_ALIGN_UP(Call->Args[1], LX_PAGE_SIZE);
    ULONG Advice = (ULONG)Call->Args[2];
    PLIST_ENTRY Entry;
    LONG64 Result = 0;

    if ((Address & (LX_PAGE_SIZE - 1)) || (End < Address))
        return -LX_EINVAL;
    if (End == Address)
        return 0;

    LxMmLock(Process);
    if (!LxVmaCovers(Process, Address, End))
    {
        Result = -LX_ENOMEM;
    }
    else if ((Advice == LX_MADV_DONTNEED) || (Advice == LX_MADV_FREE))
    {
        for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
        {
            PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

            if (Vma->Start >= End)
                break;
            if ((Vma->End <= Address) || (Vma->Flags & LX_MAP_SHARED))
                continue;
            if ((Advice == LX_MADV_FREE) && (Vma->File != NULL))
                continue;
            LxVmDecommitRange(max(Vma->Start, Address), min(Vma->End, End));
        }
    }
    LxMmUnlock(Process);
    return Result;
}

LX_SYSCALL(LxSysMremap)
{
    PLX_PROCESS Process = Call->Process;
    ULONG64 Old = Call->Args[0];
    ULONG64 OldLength = LX_ALIGN_UP(Call->Args[1], LX_PAGE_SIZE);
    ULONG64 NewLength = LX_ALIGN_UP(Call->Args[2], LX_PAGE_SIZE);
    ULONG Flags = (ULONG)Call->Args[3];
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 New = 0, Cursor, Offset;
    ULONG Prot, VmaFlags;
    PLX_FILE File;
    PLX_VMA Vma;
    LONG64 Result;

    if ((Old & (LX_PAGE_SIZE - 1)) || (OldLength == 0) || (NewLength == 0) || (Flags & ~LX_MREMAP_MAYMOVE))
        return -LX_EINVAL;
    if ((Old + OldLength < Old) || (Old + NewLength < Old))
        return -LX_EINVAL;

    LxMmLock(Process);
    Vma = LxVmaFind(Process, Old);
    if ((Vma == NULL) || (Old + OldLength > Vma->End))
    {
        LxMmUnlock(Process);
        return -LX_EFAULT;
    }

    if (NewLength <= OldLength)
    {
        if (NewLength < OldLength)
            LxMmUnmap(Process, Old + NewLength, OldLength - NewLength);
        LxMmUnlock(Process);
        return (LONG64)Old;
    }

    Prot = Vma->Prot;
    VmaFlags = Vma->Flags;
    File = Vma->File;
    Offset = (File != NULL) ? Vma->Offset + (Old - Vma->Start) : 0;
    if (File != NULL)
        LxFileReference(File);

    if (!LxVmaOverlaps(Process, Old + OldLength, Old + NewLength) &&
        (LxVmPlace(Old + OldLength, Old + NewLength) == 0))
    {
        Result = LxVmaInsert(Process, Old + OldLength, Old + NewLength, Prot, VmaFlags, File,
                             (File != NULL) ? Offset + OldLength : 0);
        if (Result == 0)
            Result = (LONG64)Old;
    }
    else if (!(Flags & LX_MREMAP_MAYMOVE))
    {
        Result = -LX_ENOMEM;
    }
    else if (!NT_SUCCESS(LxVmAllocate(&New, LX_ALIGN_UP(NewLength, LX_ALLOC_GRANULARITY), MEM_RESERVE, PAGE_NOACCESS)))
    {
        Result = -LX_ENOMEM;
    }
    else
    {
        Result = LxVmaInsert(Process, New, New + NewLength, Prot, VmaFlags, File, Offset);
        if (Result != 0)
            LxVmFree(New, 0, MEM_RELEASE);

        for (Cursor = Old; (Result == 0) && (Cursor < Old + OldLength);)
        {
            ULONG64 RegionEnd, Target, Commit;

            if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            {
                Result = -LX_EFAULT;
                break;
            }
            RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, Old + OldLength);
            if (Information.State == MEM_COMMIT)
            {
                Target = New + (Cursor - Old);
                Commit = Target;
                if (!NT_SUCCESS(LxVmAllocate(&Commit, RegionEnd - Cursor, MEM_COMMIT, PAGE_READWRITE)))
                {
                    Result = -LX_ENOMEM;
                    break;
                }
                if ((Information.Protect & 0xFF) == PAGE_NOACCESS)
                    LxVmProtect(Cursor, RegionEnd - Cursor, PAGE_READONLY);
                _SEH2_TRY
                {
                    RtlCopyMemory((PVOID)(ULONG_PTR)Target, (PVOID)(ULONG_PTR)Cursor, (SIZE_T)(RegionEnd - Cursor));
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Result = -LX_EFAULT;
                }
                _SEH2_END;
                if (Result != 0)
                    break;
                if (LxProtToNt(Prot) != PAGE_READWRITE)
                    LxVmProtect(Target, RegionEnd - Cursor, LxProtToNt(Prot));
                if (Prot & LX_PROT_EXEC)
                    LxVmFlushCode(Target, RegionEnd - Cursor);
            }
            Cursor = RegionEnd;
        }

        if (Result == 0)
        {
            LxMmUnmap(Process, Old, OldLength);
            Result = (LONG64)New;
        }
        else if (New != 0)
        {
            LxMmUnmap(Process, New, NewLength);
        }
    }

    if (File != NULL)
        LxFileDereference(File);
    LxMmUnlock(Process);
    return Result;
}

LX_SYSCALL(LxSysBrk)
{
    PLX_PROCESS Process = Call->Process;
    ULONG64 Requested = Call->Args[0];
    ULONG64 NewCommitted, Result;
    NTSTATUS Status;

    LxMmLock(Process);

    if (!Process->BrkReserved && (Process->BrkBase != 0))
    {
        ULONG64 Base = Process->BrkBase;

        Process->BrkReserved = TRUE;
        Status = LxVmAllocate(&Base, LX_BRK_RESERVE, MEM_RESERVE, PAGE_NOACCESS);
        if (NT_SUCCESS(Status) && (Base == Process->BrkBase))
            Process->BrkLimit = Process->BrkBase + LX_BRK_RESERVE;
        else
            Process->BrkLimit = Process->BrkBase;
    }

    Result = Process->BrkCurrent;
    if ((Requested < Process->BrkBase) || (Requested > Process->BrkLimit))
    {
        LxMmUnlock(Process);
        return (LONG64)Result;
    }

    NewCommitted = LX_ALIGN_UP(Requested, LX_PAGE_SIZE);
    if (NewCommitted > Process->BrkCommitted)
    {
        if (LxVmaOverlaps(Process, Process->BrkCommitted, NewCommitted) ||
            (LxVmaInsert(Process, Process->BrkCommitted, NewCommitted, LX_PROT_READ | LX_PROT_WRITE,
                         LX_MAP_PRIVATE | LX_VMA_HEAP, NULL, 0) != 0))
        {
            LxMmUnlock(Process);
            return (LONG64)Result;
        }
    }
    else if (NewCommitted < Process->BrkCommitted)
    {
        LxVmaRemove(Process, NewCommitted, Process->BrkCommitted);
        LxVmDecommitRange(NewCommitted, Process->BrkCommitted);
    }

    Process->BrkCommitted = NewCommitted;
    Process->BrkCurrent = Requested;
    LxMmUnlock(Process);
    return (LONG64)Requested;
}
