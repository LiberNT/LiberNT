/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux memory system calls on top of NT virtual memory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

volatile LONG LxMaxMapCount = 65530;

#define LX_STACK_GUARD_GAP  (256 * LX_PAGE_SIZE)

VOID
LxMmLock(
    _In_ PLX_PROCESS Process)
{
    PKTHREAD Self = KeGetCurrentThread();

    Process = Process->Mm;
    if (Process->MmLockOwner == Self)
    {
        Process->MmLockDepth++;
        return;
    }
    KeEnterCriticalRegion();
    KeWaitForSingleObject(&Process->MmLock, Executive, KernelMode, FALSE, NULL);
    Process->MmLockOwner = Self;
    Process->MmLockDepth = 1;
}

VOID
LxMmUnlock(
    _In_ PLX_PROCESS Process)
{
    Process = Process->Mm;
    if (--Process->MmLockDepth != 0)
        return;
    Process->MmLockOwner = NULL;
    KeSetEvent(&Process->MmLock, IO_NO_INCREMENT, FALSE);
    KeLeaveCriticalRegion();
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

static volatile LONG LxRandomizeVaSpaceValue = 2;
static volatile LONG LxMmapRndBitsValue;

#define LX_STACK_GUARD_GAP (256 * LX_PAGE_SIZE)

static
ULONG64
LxMmUserTop(VOID)
{
    return LX_ALIGN_DOWN((ULONG64)(ULONG_PTR)MM_HIGHEST_USER_ADDRESS + 1, LX_ALLOC_GRANULARITY);
}

static
ULONG64
LxMmDefaultTop(VOID)
{
    return LxMmUserTop() - LX_MMAP_GAP;
}

LONG
LxMmRandomizeVaSpace(VOID)
{
    return ReadNoFence(&LxRandomizeVaSpaceValue);
}

VOID
LxMmSetRandomizeVaSpace(
    _In_ LONG Value)
{
    InterlockedExchange(&LxRandomizeVaSpaceValue, Value);
}

ULONG
LxMmRndBits(VOID)
{
    ULONG Minimum, Maximum;
    LONG Bits = ReadNoFence(&LxMmapRndBitsValue);

    LxArchMmapRndBits(&Minimum, &Maximum);
    return (Bits != 0) ? (ULONG)Bits : Minimum;
}

LONG
LxMmSetRndBits(
    _In_ ULONG Bits)
{
    ULONG Minimum, Maximum;

    LxArchMmapRndBits(&Minimum, &Maximum);
    if ((Bits < Minimum) || (Bits > Maximum))
        return -LX_EINVAL;
    InterlockedExchange(&LxMmapRndBitsValue, (LONG)Bits);
    return 0;
}

/* FIXME: NT places views on 64 KB boundaries, so every random offset loses its four low page bits. */
ULONG64
LxMmRandomOffset(VOID)
{
    ULONG64 Value = 0;

    LxFillRandom((PUCHAR)&Value, sizeof(Value));
    return LX_ALIGN_DOWN((Value & ((1ULL << LxMmRndBits()) - 1)) * LX_PAGE_SIZE, LX_ALLOC_GRANULARITY);
}

ULONG64
LxMmRandomizeBrk(
    _In_ ULONG64 Base)
{
    ULONG64 Value = 0;

    LxFillRandom((PUCHAR)&Value, sizeof(Value));
    return Base + LX_ALIGN_DOWN((Value % (1024ULL * 1024 * 1024 / LX_PAGE_SIZE)) * LX_PAGE_SIZE, LX_ALLOC_GRANULARITY);
}

BOOLEAN
LxMmPickLayout(
    _Inout_ PLX_PROCESS Process)
{
    BOOLEAN Randomize = (LxMmRandomizeVaSpace() != 0) && !(Process->Personality & LX_PER_ADDR_NO_RANDOMIZE);
    ULONG64 Top = LxMmUserTop(), Gap = LX_STACK_SIZE + LX_STACK_GUARD_GAP, StackRandom = 0, Value = 0;

    if (Randomize)
    {
        LxFillRandom((PUCHAR)&Value, sizeof(Value));
        StackRandom = LX_ALIGN_DOWN((Value & LxArchStackRndMask()) * LX_PAGE_SIZE, LX_ALLOC_GRANULARITY);
        Gap += LxArchStackRndMask() * LX_PAGE_SIZE;
    }
    if (Gap < LX_MMAP_GAP)
        Gap = LX_MMAP_GAP;
    Process->StackTop = Top - StackRandom;
    Process->MmapTop = LX_ALIGN_DOWN(Top - Gap - (Randomize ? LxMmRandomOffset() : 0), LX_ALLOC_GRANULARITY);
    return Randomize;
}

static
NTSTATUS
LxVmReserveBelow(
    _In_ ULONG64 Top,
    _Out_ PULONG64 Base,
    _In_ ULONG64 Size)
{
    MEM_ADDRESS_REQUIREMENTS Requirements;
    MEM_EXTENDED_PARAMETER Parameter;
    PVOID Address = NULL;
    SIZE_T RegionSize = (SIZE_T)Size;
    NTSTATUS Status;

    RtlZeroMemory(&Requirements, sizeof(Requirements));
    Requirements.LowestStartingAddress = (PVOID)(ULONG_PTR)LX_MMAP_MIN_ADDR;
    Requirements.HighestEndingAddress = (PVOID)(ULONG_PTR)(Top - 1);
    RtlZeroMemory(&Parameter, sizeof(Parameter));
    Parameter.Type = MemExtendedParameterAddressRequirements;
    Parameter.Pointer = &Requirements;
    Status = ZwAllocateVirtualMemoryEx(NtCurrentProcess(), &Address, &RegionSize, MEM_RESERVE | MEM_TOP_DOWN,
                                       PAGE_NOACCESS, &Parameter, 1);
    if (NT_SUCCESS(Status))
        *Base = (ULONG64)(ULONG_PTR)Address;
    return Status;
}

static
NTSTATUS
LxVmReserveUnmapped(
    _In_ PLX_PROCESS Process,
    _Out_ PULONG64 Base,
    _In_ ULONG64 Size)
{
    ULONG64 Top = Process->Mm->MmapTop;

    return LxVmReserveBelow((Top != 0) ? Top : LxMmDefaultTop(), Base, Size);
}

static
NTSTATUS
LxVmReserveStack(
    _In_ PLX_PROCESS Process,
    _Out_ PULONG64 Base,
    _In_ ULONG64 Size)
{
    ULONG64 Top = Process->Mm->StackTop;

    return LxVmReserveBelow((Top != 0) ? Top : LxMmUserTop(), Base, Size);
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
PLIST_ENTRY
LxVmaEntryAbove(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address)
{
    PRTL_BALANCED_NODE Node = Process->VmaTree.Root;
    PLX_VMA Best = NULL;

    while (Node != NULL)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Node, LX_VMA, TreeNode);

        if (Vma->Start <= Address)
        {
            Best = Vma;
            Node = Node->Right;
        }
        else
        {
            Node = Node->Left;
        }
    }
    if (Best == NULL)
        return Process->Vmas.Flink;
    return (Best->End > Address) ? &Best->Link : Best->Link.Flink;
}

static
PLX_VMA
LxVmaFind(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address)
{
    PLIST_ENTRY Entry = LxVmaEntryAbove(Process, Address);
    PLX_VMA Vma;

    if (Entry == &Process->Vmas)
        return NULL;
    Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
    return (Vma->Start <= Address) ? Vma : NULL;
}

static
VOID
LxVmaAccountMemfd(
    _In_ PLX_VMA Vma,
    _In_ LONG Delta)
{
    if ((Vma->File != NULL) && (Vma->File->Memfd != NULL) && (Vma->Flags & LX_MAP_SHARED) &&
        !(Vma->Flags & LX_VMA_NOMAYWRITE))
    {
        InterlockedExchangeAdd(&Vma->File->Memfd->WritableMaps, Delta);
    }
}

static
VOID
LxVmaLink(
    _In_ PLX_PROCESS Process,
    _In_ PLX_VMA Vma,
    _In_ PLIST_ENTRY Before)
{
    PRTL_BALANCED_NODE Node = Process->VmaTree.Root, Parent = NULL;
    BOOLEAN Right = FALSE;

    while (Node != NULL)
    {
        Parent = Node;
        Right = (Vma->Start > CONTAINING_RECORD(Node, LX_VMA, TreeNode)->Start);
        Node = Node->Children[Right];
    }
    RtlRbInsertNodeEx(&Process->VmaTree, Parent, Right, &Vma->TreeNode);
    InsertTailList(Before, &Vma->Link);
    Process->VmaCount++;
    LxVmaAccountMemfd(Vma, 1);
}

static
VOID
LxVmaUnlink(
    _In_ PLX_PROCESS Process,
    _In_ PLX_VMA Vma)
{
    RtlRbRemoveNode(&Process->VmaTree, &Vma->TreeNode);
    RemoveEntryList(&Vma->Link);
    Process->VmaCount--;
    LxVmaAccountMemfd(Vma, -1);
}

static
PVOID
LxVmaShmObject(
    _In_ PLX_VMA Vma)
{
    return (Vma->Section != NULL) ? Vma->Section : (PVOID)Vma->File;
}

static
VOID
LxVmaFree(
    _In_ PLX_VMA Vma)
{
    if (Vma->Flags & LX_VMA_SHM)
        LxIpcShmVmaClose(LxVmaShmObject(Vma));
    if (Vma->File != NULL)
        LxFileDereference(Vma->File);
    if (Vma->Section != NULL)
        ObDereferenceObject(Vma->Section);
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
    if ((Vma->Flags & LX_VMA_HUGETLB) && (Address & (LxHugePageSize() - 1)))
        return -LX_EINVAL;
    if (Process->VmaCount >= LxMaxMapCount)
        return -LX_ENOMEM;

    Tail = LxAlloc(sizeof(*Tail));
    if (Tail == NULL)
        return -LX_ENOMEM;
    Tail->Start = Address;
    Tail->End = Vma->End;
    Tail->Prot = Vma->Prot;
    Tail->Flags = Vma->Flags;
    Tail->File = Vma->File;
    Tail->Section = Vma->Section;
    Tail->Offset = ((Vma->File != NULL) || (Vma->Section != NULL)) ? Vma->Offset + (Address - Vma->Start) : 0;
    Tail->ViewBase = Vma->ViewBase;
    Tail->ViewOffset = Vma->ViewOffset;
    Tail->MapEnd = Vma->MapEnd;
    if (Tail->File != NULL)
        LxFileReference(Tail->File);
    if (Tail->Section != NULL)
        ObReferenceObject(Tail->Section);
    if (Tail->Flags & LX_VMA_SHM)
        LxIpcShmVmaOpen(LxVmaShmObject(Tail));
    Vma->End = Address;
    LxVmaLink(Process, Tail, Vma->Link.Flink);
    return 0;
}

static
VOID
LxVmaMerge(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLIST_ENTRY Entry = LxVmaEntryAbove(Process, Start);

    if (Entry->Blink != &Process->Vmas)
        Entry = Entry->Blink;
    while ((Entry != &Process->Vmas) && (Entry->Flink != &Process->Vmas))
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
        PLX_VMA Next = CONTAINING_RECORD(Entry->Flink, LX_VMA, Link);

        if (Vma->Start > End)
            break;
        if ((Next->End >= Start) &&
            (Vma->End == Next->Start) &&
            (Vma->Prot == Next->Prot) &&
            (((Vma->Flags ^ Next->Flags) & ~LX_VMA_TOUCHED) == 0) &&
            (Vma->File == Next->File) &&
            (Vma->Section == Next->Section) &&
            (Vma->ViewBase == Next->ViewBase) &&
            (((Vma->File == NULL) && (Vma->Section == NULL)) || (Vma->Offset + (Vma->End - Vma->Start) == Next->Offset)))
        {
            Vma->End = Next->End;
            Vma->Flags |= Next->Flags & LX_VMA_TOUCHED;
            LxVmaUnlink(Process, Next);
            LxVmaFree(Next);
            continue;
        }
        Entry = Entry->Flink;
    }
}

typedef struct _LX_GUARD
{
    LIST_ENTRY Link;
    ULONG64 Start;
    ULONG64 End;
} LX_GUARD, *PLX_GUARD;

static
VOID
LxMmGuardRemove(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLIST_ENTRY Entry = Process->Guards.Flink;

    while (Entry != &Process->Guards)
    {
        PLX_GUARD Guard = CONTAINING_RECORD(Entry, LX_GUARD, Link);

        Entry = Entry->Flink;
        if ((Guard->End <= Start) || (Guard->Start >= End))
            continue;
        if ((Guard->Start < Start) && (Guard->End > End))
        {
            PLX_GUARD Tail = LxAlloc(sizeof(*Tail));

            if (Tail != NULL)
            {
                Tail->Start = End;
                Tail->End = Guard->End;
                InsertHeadList(&Guard->Link, &Tail->Link);
            }
            Guard->End = Start;
        }
        else if (Guard->Start < Start)
        {
            Guard->End = Start;
        }
        else if (Guard->End > End)
        {
            Guard->Start = End;
        }
        else
        {
            RemoveEntryList(&Guard->Link);
            LxFree(Guard);
        }
    }
}

static
LONG
LxMmGuardAdd(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLX_GUARD Guard = LxAlloc(sizeof(*Guard));
    PLIST_ENTRY Entry;

    if (Guard == NULL)
        return -LX_ENOMEM;
    LxMmGuardRemove(Process, Start, End);
    Guard->Start = Start;
    Guard->End = End;
    for (Entry = Process->Guards.Flink; Entry != &Process->Guards; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, LX_GUARD, Link)->Start > Start)
            break;
    }
    InsertTailList(Entry, &Guard->Link);
    return 0;
}

static
VOID
LxMmGuardClip(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _Inout_ PULONG64 Start,
    _Inout_ PULONG64 End)
{
    PLIST_ENTRY Entry;

    for (Entry = Process->Guards.Flink; Entry != &Process->Guards; Entry = Entry->Flink)
    {
        PLX_GUARD Guard = CONTAINING_RECORD(Entry, LX_GUARD, Link);

        if ((Guard->End <= Address) && (Guard->End > *Start))
            *Start = Guard->End;
        if ((Guard->Start > Address) && (Guard->Start < *End))
            *End = Guard->Start;
    }
}

static
BOOLEAN
LxMmGuardHit(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address)
{
    PLIST_ENTRY Entry;

    for (Entry = Process->Guards.Flink; Entry != &Process->Guards; Entry = Entry->Flink)
    {
        PLX_GUARD Guard = CONTAINING_RECORD(Entry, LX_GUARD, Link);

        if (Guard->Start > Address)
            break;
        if (Address < Guard->End)
            return TRUE;
    }
    return FALSE;
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
    if (!IsListEmpty(&Process->Guards))
        LxMmGuardRemove(Process, Start, End);

    Entry = LxVmaEntryAbove(Process, Start);
    while (Entry != &Process->Vmas)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        Entry = Entry->Flink;
        if (Vma->Start >= End)
            break;
        if (Vma->Start >= Start)
        {
            LxVmaUnlink(Process, Vma);
            LxVmaFree(Vma);
        }
    }
    return 0;
}

static
LONG
LxVmaInsertView(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Prot,
    _In_ ULONG Flags,
    _In_opt_ PLX_FILE File,
    _In_ ULONG64 Offset,
    _In_opt_ PVOID Section,
    _In_ ULONG64 ViewBase,
    _In_ ULONG64 ViewOffset,
    _In_ ULONG64 MapEnd)
{
    PLX_VMA Vma = LxAlloc(sizeof(*Vma));
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
    Vma->Section = Section;
    Vma->Offset = ((File != NULL) || (Section != NULL)) ? Offset : 0;
    Vma->ViewBase = ViewBase;
    Vma->ViewOffset = ViewOffset;
    Vma->MapEnd = MapEnd;
    if (File != NULL)
        LxFileReference(File);
    if (Section != NULL)
        ObReferenceObject(Section);
    if (Flags & LX_VMA_SHM)
        LxIpcShmVmaOpen((Section != NULL) ? Section : (PVOID)File);

    LxVmaLink(Process, Vma, LxVmaEntryAbove(Process, Start));
    LxVmaMerge(Process, Start, End);
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
    _In_ ULONG64 Offset,
    _In_opt_ PVOID Section)
{
    return LxVmaInsertView(Process, Start, End, Prot, Flags, File, Offset, Section, 0, 0, 0);
}

static
BOOLEAN
LxVmaOverlaps(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLIST_ENTRY Entry = LxVmaEntryAbove(Process, Start);

    return (Entry != &Process->Vmas) && (CONTAINING_RECORD(Entry, LX_VMA, Link)->Start < End);
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

    for (Entry = LxVmaEntryAbove(Process, Start); Entry != &Process->Vmas; Entry = Entry->Flink)
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

    for (Entry = LxVmaEntryAbove(Process, Start); Entry != &Process->Vmas; Entry = Entry->Flink)
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
LONG
LxVmaSetFlags(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Mask,
    _In_ ULONG Bits)
{
    PLIST_ENTRY Entry;
    LONG Error;

    Error = LxVmaSplit(Process, Start);
    if (Error == 0)
        Error = LxVmaSplit(Process, End);
    if (Error != 0)
        return Error;

    for (Entry = LxVmaEntryAbove(Process, Start); Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Vma->Start >= End)
            break;
        if (Vma->Start >= Start)
            Vma->Flags = (Vma->Flags & ~Mask) | Bits;
    }
    LxVmaMerge(Process, Start, End);
    return 0;
}

static
LONG
LxVmaSetLock(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Lock)
{
    return LxVmaSetFlags(Process, Start, End, LX_VMA_LOCK_MASK, Lock);
}

static
ULONG64
LxMmLockedIn(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLIST_ENTRY Entry;
    ULONG64 Bytes = 0;

    for (Entry = LxVmaEntryAbove(Process, Start); Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Vma->Start >= End)
            break;
        if ((Vma->End > Start) && (Vma->Flags & LX_VMA_LOCKED))
            Bytes += min(Vma->End, End) - max(Vma->Start, Start);
    }
    return Bytes;
}

static
BOOLEAN
LxMmCanLock(
    _In_ PLX_PROCESS Caller)
{
    return (Caller->Limits[LX_RLIMIT_MEMLOCK].Cur != 0) || LxCapable(Caller, LX_CAP_IPC_LOCK);
}

static
BOOLEAN
LxMmLockAllowed(
    _In_ PLX_PROCESS Caller,
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Bytes)
{
    ULONG64 Limit = Caller->Limits[LX_RLIMIT_MEMLOCK].Cur;

    if ((Limit == LX_RLIM_INFINITY) || LxCapable(Caller, LX_CAP_IPC_LOCK))
        return TRUE;
    return (LxMmLockedIn(Process, 0, MAXULONG64) + Bytes) / LX_PAGE_SIZE <= Limit / LX_PAGE_SIZE;
}

static
ULONG64
LxVmAllocationEndFrom(
    _In_ ULONG64 AllocationBase,
    _In_ ULONG64 Cursor)
{
    MEMORY_BASIC_INFORMATION Information;

    while (NT_SUCCESS(LxVmQuery(Cursor, &Information)) &&
           ((ULONG64)(ULONG_PTR)Information.AllocationBase == AllocationBase) &&
           (Information.State != MEM_FREE))
    {
        Cursor = (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize;
    }
    return Cursor;
}

static
ULONG64
LxVmAllocationEnd(
    _In_ ULONG64 AllocationBase)
{
    return LxVmAllocationEndFrom(AllocationBase, AllocationBase);
}

static
NTSTATUS
LxVmCommitRange(
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Protect)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Start;
    NTSTATUS Status = STATUS_SUCCESS;

    if (NT_SUCCESS(LxVmAllocate(&Cursor, End - Start, MEM_COMMIT, Protect)))
        return STATUS_SUCCESS;
    Cursor = Start;
    while (Cursor < End)
    {
        ULONG64 Stop = End, Commit = Cursor;

        if (NT_SUCCESS(LxVmQuery(Cursor, &Information)) && (Information.State != MEM_FREE))
            Stop = min(End, (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize);
        Status = LxVmAllocate(&Commit, Stop - Cursor, MEM_COMMIT, Protect);
        if (!NT_SUCCESS(Status))
            break;
        Cursor = Stop;
    }
    return Status;
}

static
NTSTATUS
LxVmProtectRange(
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Protect)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Start;
    NTSTATUS Status = STATUS_SUCCESS;

    while (Cursor < End)
    {
        ULONG64 Stop;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            return STATUS_CONFLICTING_ADDRESSES;
        Stop = min(End, (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize);
        if (Information.State == MEM_COMMIT)
        {
            Status = LxVmProtect(Cursor, Stop - Cursor, Protect);
            if (!NT_SUCCESS(Status))
                break;
        }
        Cursor = Stop;
    }
    return Status;
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
        else if ((Information.State == MEM_COMMIT) && (Information.Type == MEM_MAPPED))
            LxVmProtect(Cursor, RegionEnd - Cursor, PAGE_NOACCESS);
        Cursor = RegionEnd;
    }
}

static
BOOLEAN
LxVmaIsCow(
    _In_ PLX_VMA Vma)
{
    return (Vma->Flags & (LX_VMA_VIEW | LX_MAP_SHARED)) == LX_VMA_VIEW;
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
        if ((Information.State != MEM_FREE) && (AllocationBase != Last) &&
            ((Information.Type == MEM_PRIVATE) || (Information.Type == MEM_MAPPED)))
        {
            Last = AllocationBase;
            if (!LxVmaOverlaps(Process, AllocationBase,
                               LxVmAllocationEndFrom(AllocationBase, (ULONG64)(ULONG_PTR)Information.BaseAddress +
                                                                         Information.RegionSize)))
            {
                if (Information.Type == MEM_MAPPED)
                {
                    PLIST_ENTRY Entry;

                    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
                    {
                        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

                        if (LxVmaIsCow(Vma) && (Vma->ViewBase == AllocationBase))
                            Vma->ViewBase = 0;
                    }
                    ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)AllocationBase);
                }
                else
                {
                    LxVmFree(AllocationBase, 0, MEM_RELEASE);
                }
            }
        }
        Cursor = RegionEnd;
    }
}

#define LX_PLACE_FRESH  0x1
#define LX_PLACE_SPLIT  0x2

typedef struct _LX_MM_COPY
{
    LIST_ENTRY Link;
    ULONG64 Start;
    ULONG64 End;
    ULONG Prot;
    PUCHAR Data;
} LX_MM_COPY, *PLX_MM_COPY;

static
ULONG
LxCowProtToNt(
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
            return PAGE_WRITECOPY;
        case LX_PROT_EXEC:
        case LX_PROT_READ | LX_PROT_EXEC:
            return PAGE_EXECUTE_READ;
        default:
            return PAGE_EXECUTE_WRITECOPY;
    }
}

static
ULONG
LxVmaNtProtect(
    _In_ PLX_VMA Vma)
{
    return LxVmaIsCow(Vma) ? LxCowProtToNt(Vma->Prot) : LxProtToNt(Vma->Prot);
}

static
ULONG
LxViewAccess(
    _In_ ULONG Protect)
{
    switch (Protect & 0xFF)
    {
        case PAGE_NOACCESS:
            return 0;
        case PAGE_READONLY:
            return 1;
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
            return 3;
        case PAGE_EXECUTE:
        case PAGE_EXECUTE_READ:
            return 5;
        default:
            return 7;
    }
}

static
ULONG
LxCowViewMaximum(
    _In_ ULONG Prot)
{
    return (Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC)) ? LxCowProtToNt(Prot) : PAGE_READONLY;
}

static
BOOLEAN
LxVmaBacked(
    _In_ PLX_VMA Vma,
    _In_ PMEMORY_BASIC_INFORMATION Information)
{
    if (Information->State == MEM_FREE)
        return FALSE;
    if (LxVmaIsCow(Vma))
    {
        return (Information->Type == MEM_MAPPED) && (Vma->ViewBase != 0) &&
               ((ULONG64)(ULONG_PTR)Information->AllocationBase == Vma->ViewBase);
    }
    if (Vma->Flags & LX_VMA_VIEW)
        return Information->Type == MEM_MAPPED;
    return Information->Type == MEM_PRIVATE;
}

static
BOOLEAN
LxVmaAccessibleIn(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLIST_ENTRY Entry;

    for (Entry = LxVmaEntryAbove(Process, Start); Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Vma->Start >= End)
            break;
        if ((Vma->End > Start) &&
            ((Vma->Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC)) || (Vma->Flags & LX_VMA_DIRTY)))
        {
            return TRUE;
        }
    }
    return FALSE;
}

static
BOOLEAN
LxVmHasCommit(
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Start;

    while ((Cursor < End) && NT_SUCCESS(LxVmQuery(Cursor, &Information)))
    {
        if (Information.State == MEM_COMMIT)
            return TRUE;
        Cursor = (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize;
    }
    return FALSE;
}

static
LONG
LxMmCopySave(
    _Inout_ PLIST_ENTRY List,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Prot,
    _In_ ULONG Protect)
{
    PLX_MM_COPY Copy;
    LONG Error = 0;

    if (End <= Start)
        return 0;
    Copy = LxAlloc(sizeof(*Copy));
    if (Copy == NULL)
        return -LX_ENOMEM;
    Copy->Data = LxAllocPaged((SIZE_T)(End - Start));
    if (Copy->Data == NULL)
    {
        LxFree(Copy);
        return -LX_ENOMEM;
    }
    Copy->Start = Start;
    Copy->End = End;
    Copy->Prot = Prot;

    LxVmProtectRange(Start, End, PAGE_READONLY);
    _SEH2_TRY
    {
        RtlCopyMemory(Copy->Data, (PVOID)(ULONG_PTR)Start, (SIZE_T)(End - Start));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Error = -LX_EFAULT;
    }
    _SEH2_END;
    LxVmProtectRange(Start, End, Protect);

    if (Error != 0)
    {
        LxFreePaged(Copy->Data);
        LxFree(Copy);
        return Error;
    }
    InsertTailList(List, &Copy->Link);
    return 0;
}

static
VOID
LxMmCopyRestore(
    _Inout_ PLIST_ENTRY List,
    _In_ BOOLEAN Private)
{
    while (!IsListEmpty(List))
    {
        PLX_MM_COPY Copy = CONTAINING_RECORD(RemoveHeadList(List), LX_MM_COPY, Link);
        ULONG64 Size = Copy->End - Copy->Start;
        NTSTATUS Status;

        if (Private)
            Status = LxVmCommitRange(Copy->Start, Copy->End, PAGE_READWRITE);
        else
            Status = LxVmProtectRange(Copy->Start, Copy->End, PAGE_WRITECOPY);
        if (NT_SUCCESS(Status))
        {
            _SEH2_TRY
            {
                RtlCopyMemory((PVOID)(ULONG_PTR)Copy->Start, Copy->Data, (SIZE_T)Size);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            LxVmProtectRange(Copy->Start, Copy->End, Private ? LxProtToNt(Copy->Prot) : LxCowProtToNt(Copy->Prot));
            if (Copy->Prot & LX_PROT_EXEC)
                LxVmFlushCode(Copy->Start, Size);
        }
        if (!NT_SUCCESS(Status))
            DPRINT1("Linux memory at %I64x-%I64x lost its content while moving: 0x%08lx\n", Copy->Start, Copy->End, Status);
        LxFreePaged(Copy->Data);
        LxFree(Copy);
    }
}

static
NTSTATUS
LxMmFileSection(
    _In_ PLX_FILE File,
    _In_ ULONG Protect,
    _Out_ PHANDLE Section)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    if (LxTmpfsFileInode(File) != NULL)
        return LxTmpfsMapSection(LxTmpfsFileInode(File), TRUE, Section, NULL);
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    return ZwCreateSection(Section, SECTION_ALL_ACCESS, &ObjectAttributes, NULL, Protect, SEC_COMMIT, File->Handle);
}

static
BOOLEAN
LxMmFileSize(
    _In_ PLX_FILE File,
    _Out_ PLONG64 Size)
{
    FILE_STANDARD_INFORMATION Standard;
    IO_STATUS_BLOCK Iosb;

    if (LxTmpfsFileInode(File) != NULL)
    {
        *Size = (LONG64)LxTmpfsInodeSize(LxTmpfsFileInode(File));
        return TRUE;
    }
    if ((File->Handle == NULL) ||
        !NT_SUCCESS(ZwQueryInformationFile(File->Handle, &Iosb, &Standard, sizeof(Standard), FileStandardInformation)))
    {
        return FALSE;
    }
    *Size = Standard.EndOfFile.QuadPart;
    return TRUE;
}

static
NTSTATUS
LxMmCreateCowSection(
    _In_ PLX_FILE File,
    _Out_ PVOID *Section)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    if (LxTmpfsFileInode(File) != NULL)
        Status = LxTmpfsMapSection(LxTmpfsFileInode(File), FALSE, &Handle, NULL);
    else
        Status = ZwCreateSection(&Handle, SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_QUERY, &ObjectAttributes,
                                 NULL, PAGE_EXECUTE_WRITECOPY, SEC_COMMIT, File->Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ObReferenceObjectByHandle(Handle, 0, NULL, KernelMode, Section, NULL);
    ZwClose(Handle);
    return Status;
}

static
NTSTATUS
LxMmMapCowView(
    _In_ PVOID Section,
    _In_ ULONG64 Base,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Size,
    _In_ ULONG Maximum,
    _In_ ULONG Protect)
{
    SECTION_BASIC_INFORMATION Basic;
    LARGE_INTEGER SectionOffset;
    SIZE_T ViewSize = (SIZE_T)Size;
    PVOID View = (PVOID)(ULONG_PTR)Base;
    HANDLE Handle;
    NTSTATUS Status;

    Status = ObOpenObjectByPointer(Section, OBJ_KERNEL_HANDLE, NULL,
                                   SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_QUERY, NULL, KernelMode, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQuerySection(Handle, SectionBasicInformation, &Basic, sizeof(Basic), NULL);
    if (NT_SUCCESS(Status) && ((ULONG64)Basic.Size.QuadPart <= Offset))
        Status = STATUS_INVALID_VIEW_SIZE;
    if (!NT_SUCCESS(Status))
    {
        ZwClose(Handle);
        return Status;
    }
    ViewSize = (SIZE_T)min(Size, (ULONG64)Basic.Size.QuadPart - Offset);
    SectionOffset.QuadPart = (LONG64)Offset;
    Status = ZwMapViewOfSection(Handle, NtCurrentProcess(), &View, 0, 0, &SectionOffset, &ViewSize, ViewShare, 0,
                                Maximum);
    ZwClose(Handle);
    if (NT_SUCCESS(Status) && ((ULONG64)(ULONG_PTR)View != Base))
    {
        ZwUnmapViewOfSection(NtCurrentProcess(), View);
        Status = STATUS_CONFLICTING_ADDRESSES;
    }
    if (NT_SUCCESS(Status) && (Protect != Maximum))
    {
        Status = LxVmProtect(Base, ViewSize, Protect);
        if (!NT_SUCCESS(Status))
            ZwUnmapViewOfSection(NtCurrentProcess(), View);
    }
    return Status;
}

static
LONG
LxMmUpgradeView(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Base)
{
    ULONG64 End, ViewOffset = 0;
    PVOID Section = NULL;
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (LxVmaIsCow(Vma) && (Vma->ViewBase == Base))
        {
            Section = Vma->Section;
            ViewOffset = Vma->ViewOffset;
            break;
        }
    }
    if ((Base == 0) || (Section == NULL))
        return 0;

    End = LxVmAllocationEnd(Base);
    ObReferenceObject(Section);
    ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)Base);
    Status = LxMmMapCowView(Section, Base, ViewOffset, End - Base, PAGE_EXECUTE_WRITECOPY, PAGE_NOACCESS);
    ObDereferenceObject(Section);
    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (!LxVmaIsCow(Vma) || (Vma->ViewBase != Base))
            continue;
        if (!NT_SUCCESS(Status))
            Vma->ViewBase = 0;
        else if (Vma->Start < End)
            LxVmProtect(Vma->Start, min(Vma->End, End) - Vma->Start, LxCowProtToNt(Vma->Prot));
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Linux file mapping lost its view at %I64x-%I64x\n", Base, End);
        return -LxStatusToErrno(Status);
    }
    return 0;
}

static
LONG
LxMmViewAllow(
    _In_ PLX_PROCESS Process,
    _In_ PLX_VMA Vma,
    _In_ ULONG Protect)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG Access = LxViewAccess(Protect);

    if (!LxVmaIsCow(Vma) || (Vma->ViewBase == 0) || !NT_SUCCESS(LxVmQuery(Vma->ViewBase, &Information)) ||
        (Information.Type != MEM_MAPPED) || ((LxViewAccess(Information.AllocationProtect) & Access) == Access))
    {
        return 0;
    }
    return LxMmUpgradeView(Process, Vma->ViewBase);
}

static
NTSTATUS
LxMmMapSharedPiece(
    _In_ PLX_VMA Vma,
    _In_ ULONG64 Start,
    _In_ ULONG64 Length,
    _In_ ULONG64 Offset,
    _In_ ULONG Maximum)
{
    SECTION_BASIC_INFORMATION Basic;
    LARGE_INTEGER SectionOffset;
    SIZE_T ViewSize = (SIZE_T)Length;
    PVOID Base = (PVOID)(ULONG_PTR)Start;
    HANDLE Section;
    NTSTATUS Status;

    if (Vma->Section != NULL)
    {
        Status = ObOpenObjectByPointer(Vma->Section, OBJ_KERNEL_HANDLE, NULL,
                                       SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_QUERY, NULL, KernelMode, &Section);
    }
    else
    {
        Status = LxMmFileSection(Vma->File, Maximum, &Section);
    }
    if (!NT_SUCCESS(Status))
        return Status;
    if (NT_SUCCESS(ZwQuerySection(Section, SectionBasicInformation, &Basic, sizeof(Basic), NULL)) &&
        ((ULONG64)Basic.Size.QuadPart > Offset))
    {
        ViewSize = (SIZE_T)min(Length, LX_ALIGN_UP((ULONG64)Basic.Size.QuadPart - Offset, LX_PAGE_SIZE));
    }
    SectionOffset.QuadPart = (LONG64)Offset;
    Status = ZwMapViewOfSection(Section, NtCurrentProcess(), &Base, 0, 0, &SectionOffset, &ViewSize, ViewShare, 0,
                                Maximum);
    ZwClose(Section);
    return Status;
}

static
LONG
LxMmSplitSharedView(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Base,
    _In_ ULONG64 End,
    _In_ ULONG64 ZoneStart,
    _In_ ULONG64 ZoneEnd)
{
    ULONG64 Limit[2][2] = { { Base, min(ZoneStart, End) }, { max(ZoneEnd, Base), End } };
    ULONG64 Low[2] = { 0, 0 }, High[2] = { 0, 0 }, Offset[2] = { 0, 0 }, Cursor;
    PLX_VMA Owner[2] = { NULL, NULL };
    MEMORY_BASIC_INFORMATION Information;
    ULONG Maximum = PAGE_READWRITE;
    PLIST_ENTRY Entry;
    ULONG Index;

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if ((Vma->Start >= End) || (Vma->End <= Base))
            continue;
        if (!(Vma->Flags & LX_MAP_SHARED) || !(Vma->Flags & LX_VMA_VIEW) || (Vma->MapEnd != 0) ||
            ((Vma->Section == NULL) &&
             ((Vma->File == NULL) || ((Vma->File->Handle == NULL) && (LxTmpfsFileInode(Vma->File) == NULL)))))
        {
            return -LX_EBUSY;
        }
        for (Index = 0; Index < 2; Index++)
        {
            ULONG64 From = max(Vma->Start, Limit[Index][0]), To = min(Vma->End, Limit[Index][1]);

            if (From >= To)
                continue;
            if (Owner[Index] == NULL)
            {
                Owner[Index] = Vma;
                Low[Index] = From;
                Offset[Index] = Vma->Offset + (From - Vma->Start);
            }
            else if ((Owner[Index]->Section != Vma->Section) || (Owner[Index]->File != Vma->File) ||
                     (Offset[Index] + (From - Low[Index]) != Vma->Offset + (From - Vma->Start)))
            {
                return -LX_EBUSY;
            }
            High[Index] = To;
        }
    }

    if (NT_SUCCESS(LxVmQuery(Base, &Information)) && (Information.Type == MEM_MAPPED))
        Maximum = Information.AllocationProtect;
    ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)Base);
    for (Index = 0; Index < 2; Index++)
    {
        if (Owner[Index] == NULL)
            continue;
        if (!NT_SUCCESS(LxMmMapSharedPiece(Owner[Index], Low[Index], High[Index] - Low[Index], Offset[Index], Maximum)))
        {
            DPRINT1("Linux shared mapping lost its view at %I64x-%I64x\n", Low[Index], High[Index]);
            continue;
        }
        Cursor = Low[Index];
        for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
        {
            PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
            ULONG64 From = max(Vma->Start, Low[Index]), To = min(Vma->End, High[Index]);

            if (From >= To)
                continue;
            if (From > Cursor)
                LxVmProtect(Cursor, From - Cursor, PAGE_NOACCESS);
            if (LxProtToNt(Vma->Prot) != Maximum)
                LxVmProtect(From, To - From, LxProtToNt(Vma->Prot));
            Cursor = To;
        }
    }
    return 0;
}

static
LONG
LxMmSplitView(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Base,
    _In_ ULONG64 ZoneStart,
    _In_ ULONG64 ZoneEnd,
    _In_ BOOLEAN Convert,
    _Inout_opt_ PLIST_ENTRY Converted)
{
    ULONG64 End = LxVmAllocationEnd(Base), ViewOffset = 0;
    ULONG64 Pieces[2][2] = { { Base, min(ZoneStart, End) }, { ZoneEnd, End } }, Cursor[2];
    BOOLEAN Used[2] = { FALSE, FALSE }, Mapped[2];
    MEMORY_BASIC_INFORMATION Information;
    ULONG Maximum = PAGE_EXECUTE_WRITECOPY;
    PVOID Section = NULL;
    LIST_ENTRY Dirty;
    PLIST_ENTRY Entry;
    ULONG Index;
    LONG Error;

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if ((Vma->Start < End) && (Vma->End > Base) && (Vma->Flags & LX_MAP_SHARED))
            return LxMmSplitSharedView(Process, Base, End, ZoneStart, ZoneEnd);
        if (LxVmaIsCow(Vma) && (Vma->ViewBase == Base))
        {
            Section = Vma->Section;
            ViewOffset = Vma->ViewOffset;
        }
    }
    if (Section == NULL)
    {
        ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)Base);
        return 0;
    }

    Error = LxVmaSplit(Process, ZoneStart);
    if (Error == 0)
        Error = LxVmaSplit(Process, ZoneEnd);
    if (Error != 0)
        return Error;

    InitializeListHead(&Dirty);
    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (!LxVmaIsCow(Vma) || (Vma->ViewBase != Base))
            continue;
        if ((Vma->Start >= ZoneStart) && (Vma->End <= ZoneEnd))
        {
            if (Convert && (Vma->Start < End) &&
                ((Vma->Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC)) || (Vma->Flags & LX_VMA_DIRTY)))
            {
                Error = LxMmCopySave(Converted, Vma->Start, min(Vma->End, End), Vma->Prot, LxCowProtToNt(Vma->Prot));
                if (Error != 0)
                    goto Fail;
            }
            continue;
        }
        Index = (Vma->End <= ZoneStart) ? 0 : 1;
        if (Vma->Start < Pieces[Index][1])
            Used[Index] = TRUE;
        if ((Vma->Flags & LX_VMA_DIRTY) && (Vma->Start < End))
        {
            Error = LxMmCopySave(&Dirty, Vma->Start, min(Vma->End, End), Vma->Prot, LxCowProtToNt(Vma->Prot));
            if (Error != 0)
                goto Fail;
        }
    }

    if (NT_SUCCESS(LxVmQuery(Base, &Information)) && (Information.Type == MEM_MAPPED))
        Maximum = Information.AllocationProtect;
    ObReferenceObject(Section);
    ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)Base);
    for (Index = 0; Index < 2; Index++)
    {
        Mapped[Index] = Used[Index] && (Pieces[Index][0] < Pieces[Index][1]) &&
                        NT_SUCCESS(LxMmMapCowView(Section, Pieces[Index][0], ViewOffset + (Pieces[Index][0] - Base),
                                                  Pieces[Index][1] - Pieces[Index][0], Maximum, Maximum));
        Cursor[Index] = Pieces[Index][0];
        if (Used[Index] && !Mapped[Index])
            DPRINT1("Linux file mapping lost its view at %I64x-%I64x\n", Pieces[Index][0], Pieces[Index][1]);
    }

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (!LxVmaIsCow(Vma) || (Vma->ViewBase != Base))
            continue;
        if ((Vma->Start >= ZoneStart) && (Vma->End <= ZoneEnd))
        {
            Vma->ViewBase = 0;
            if (Convert)
            {
                Vma->Flags &= ~(LX_VMA_VIEW | LX_VMA_DIRTY);
                ObDereferenceObject(Vma->Section);
                Vma->Section = NULL;
                Vma->ViewOffset = 0;
            }
            continue;
        }
        Index = (Vma->End <= ZoneStart) ? 0 : 1;
        if (!Mapped[Index])
        {
            Vma->ViewBase = 0;
            continue;
        }
        Vma->ViewBase = Pieces[Index][0];
        Vma->ViewOffset = ViewOffset + (Pieces[Index][0] - Base);
        if (Vma->Start < Pieces[Index][1])
        {
            if (Vma->Start > Cursor[Index])
                LxVmProtect(Cursor[Index], Vma->Start - Cursor[Index], PAGE_NOACCESS);
            if (LxCowProtToNt(Vma->Prot) != Maximum)
                LxVmProtect(Vma->Start, min(Vma->End, Pieces[Index][1]) - Vma->Start, LxCowProtToNt(Vma->Prot));
            Cursor[Index] = min(Vma->End, Pieces[Index][1]);
        }
    }
    for (Index = 0; Index < 2; Index++)
    {
        if (Mapped[Index] && (Cursor[Index] < Pieces[Index][1]))
            LxVmProtect(Cursor[Index], Pieces[Index][1] - Cursor[Index], PAGE_NOACCESS);
    }
    ObDereferenceObject(Section);
    LxMmCopyRestore(&Dirty, FALSE);
    return 0;

Fail:
    while (!IsListEmpty(&Dirty))
    {
        PLX_MM_COPY Copy = CONTAINING_RECORD(RemoveHeadList(&Dirty), LX_MM_COPY, Link);

        LxFreePaged(Copy->Data);
        LxFree(Copy);
    }
    return Error;
}

static
LONG
LxVmPlace(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG Mode)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 ZoneEnd = LX_ALIGN_UP(End, LX_ALLOC_GRANULARITY), Cursor;
    LIST_ENTRY Converted;
    LONG Error = 0;

    InitializeListHead(&Converted);
    for (Cursor = LX_ALIGN_DOWN(Start, LX_ALLOC_GRANULARITY); Cursor < ZoneEnd;)
    {
        ULONG64 Base;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            return -LX_ENOMEM;
        if ((Information.State == MEM_FREE) || (Information.Type != MEM_MAPPED))
        {
            Cursor = (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize;
            continue;
        }
        if (!(Mode & LX_PLACE_SPLIT))
            return -LX_ENOMEM;
        Base = (ULONG64)(ULONG_PTR)Information.AllocationBase;
        Error = LxMmSplitView(Process, Base, LX_ALIGN_DOWN(Start, LX_ALLOC_GRANULARITY), ZoneEnd, TRUE, &Converted);
        if ((Error == 0) && NT_SUCCESS(LxVmQuery(Cursor, &Information)) && (Information.State != MEM_FREE) &&
            (Information.Type == MEM_MAPPED) && ((ULONG64)(ULONG_PTR)Information.AllocationBase == Base))
        {
            Error = -LX_ENOMEM;
        }
        if (Error != 0)
            break;
    }

    for (Cursor = Start; (Error == 0) && (Cursor < End);)
    {
        ULONG64 RegionEnd;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
        {
            Error = -LX_ENOMEM;
            break;
        }
        RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, End);
        if (Information.State == MEM_FREE)
        {
            ULONG64 Base = LX_ALIGN_DOWN(Cursor, LX_ALLOC_GRANULARITY);
            ULONG64 Limit = LX_ALIGN_UP(RegionEnd, LX_ALLOC_GRANULARITY);

            if (!NT_SUCCESS(LxVmAllocate(&Base, Limit - Base, MEM_RESERVE, PAGE_NOACCESS)))
                Error = -LX_ENOMEM;
        }
        else if (Information.Type != MEM_PRIVATE)
        {
            Error = -LX_ENOMEM;
        }
        else if ((Mode & LX_PLACE_FRESH) && (Information.State == MEM_COMMIT))
        {
            LxVmFree(Cursor, RegionEnd - Cursor, MEM_DECOMMIT);
        }
        Cursor = RegionEnd;
    }

    if (!IsListEmpty(&Converted))
    {
        PLIST_ENTRY Entry;

        for (Entry = Converted.Flink; Entry != &Converted; Entry = Entry->Flink)
        {
            PLX_MM_COPY Copy = CONTAINING_RECORD(Entry, LX_MM_COPY, Link);
            ULONG64 Base = LX_ALIGN_DOWN(Copy->Start, LX_ALLOC_GRANULARITY);

            if (NT_SUCCESS(LxVmQuery(Copy->Start, &Information)) && (Information.State == MEM_FREE))
                LxVmAllocate(&Base, LX_ALIGN_UP(Copy->End, LX_ALLOC_GRANULARITY) - Base, MEM_RESERVE, PAGE_NOACCESS);
        }
        LxMmCopyRestore(&Converted, TRUE);
    }
    return Error;
}

static
LONG
LxMmSavePrivate(
    _In_ PLX_PROCESS Process,
    _Inout_ PLIST_ENTRY Saved,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Start;

    while (Cursor < End)
    {
        ULONG64 RegionEnd;
        PLIST_ENTRY Entry;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            return -LX_ENOMEM;
        RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, End);
        for (Entry = LxVmaEntryAbove(Process, Cursor);
             (Information.State == MEM_COMMIT) && (Entry != &Process->Vmas); Entry = Entry->Flink)
        {
            PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
            LONG Error;

            if (Vma->Start >= RegionEnd)
                break;
            Error = LxMmCopySave(Saved, max(Vma->Start, Cursor), min(Vma->End, RegionEnd), Vma->Prot,
                                 Information.Protect);
            if (Error != 0)
                return Error;
        }
        Cursor = RegionEnd;
    }
    return 0;
}

static
LONG
LxMmClearForView(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 ZoneStart,
    _In_ ULONG64 ZoneEnd)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = ZoneStart;

    if (LxVmaAccessibleIn(Process, ZoneStart, ZoneEnd))
        return 1;
    while (Cursor < ZoneEnd)
    {
        ULONG64 Base, End, Low;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            return -LX_ENOMEM;
        if (Information.State == MEM_FREE)
        {
            Cursor = (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize;
            continue;
        }
        Base = (ULONG64)(ULONG_PTR)Information.AllocationBase;
        End = LxVmAllocationEnd(Base);
        if (Information.Type == MEM_PRIVATE)
        {
            LIST_ENTRY Saved;

            InitializeListHead(&Saved);
            if (LxVmHasCommit(Base, End) &&
                ((LxMmSavePrivate(Process, &Saved, Base, min(ZoneStart, End)) != 0) ||
                 (LxMmSavePrivate(Process, &Saved, max(ZoneEnd, Base), End) != 0)))
            {
                LxMmCopyRestore(&Saved, TRUE);
                return 1;
            }
            if (!NT_SUCCESS(LxVmFree(Base, 0, MEM_RELEASE)))
            {
                LxMmCopyRestore(&Saved, TRUE);
                return 1;
            }
            if (Base < ZoneStart)
            {
                Low = Base;
                LxVmAllocate(&Low, ZoneStart - Base, MEM_RESERVE, PAGE_NOACCESS);
            }
            if (End > ZoneEnd)
            {
                Low = ZoneEnd;
                LxVmAllocate(&Low, LX_ALIGN_UP(End, LX_ALLOC_GRANULARITY) - ZoneEnd, MEM_RESERVE, PAGE_NOACCESS);
            }
            LxMmCopyRestore(&Saved, TRUE);
        }
        else if ((Information.Type != MEM_MAPPED) || (LxMmSplitView(Process, Base, ZoneStart, ZoneEnd, FALSE, NULL) != 0))
        {
            return 1;
        }
        Cursor = max(Cursor + LX_PAGE_SIZE, min(End, ZoneEnd));
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
    LONG Error;

    if (Vma->File == NULL)
        return NT_SUCCESS(LxVmCommitRange(Start, End, NtProtect)) ? 0 : -LX_ENOMEM;

    if (!NT_SUCCESS(LxVmCommitRange(Start, End, PAGE_READWRITE)))
        return -LX_ENOMEM;
    Error = LxMmFill(Start, End - Start, Vma->File, Vma->Offset + (Start - Vma->Start));
    if (Error != 0)
    {
        LxVmDecommitRange(Start, End);
        return Error;
    }
    if (NtProtect != PAGE_READWRITE)
        LxVmProtectRange(Start, End, NtProtect);
    if (Vma->Prot & LX_PROT_EXEC)
        LxVmFlushCode(Start, End - Start);
    return 0;
}

static
ULONG64
LxMmFileEnd(
    _In_ PLX_FILE File)
{
    LONG64 Size;

    if ((File->Type != LX_S_IFREG) || !LxMmFileSize(File, &Size) || (Size < 0))
        return MAXULONG64;
    return LX_ALIGN_UP((ULONG64)Size, LX_PAGE_SIZE);
}

static
LONG
LxMmFillPrivateFile(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG64 Cursor = Start;

    while (Cursor < End)
    {
        PLX_VMA Vma = LxVmaFind(Process, Cursor);
        ULONG64 Stop;
        LONG Error;

        if (Vma == NULL)
            return -LX_EFAULT;
        Stop = min(Vma->End, End);
        if ((Vma->File != NULL) && !(Vma->Flags & LX_VMA_VIEW))
        {
            Error = LxMmPopulateRun(Vma, Cursor, Stop);
            if (Error != 0)
                return Error;
        }
        Cursor = Stop;
    }
    return 0;
}

static
LONG
LxMmBackVma(
    _In_ PLX_PROCESS Process,
    _In_ PLX_VMA Vma)
{
    ULONG64 Start = Vma->Start, End = Vma->End;
    LONG Error;

    Error = LxVmPlace(Process, Start, End, LX_PLACE_SPLIT);
    if (Error != 0)
        return Error;
    Vma = LxVmaFind(Process, Start);
    if ((Vma == NULL) || !LxVmaIsCow(Vma))
        return 0;
    Vma->Flags &= ~(LX_VMA_VIEW | LX_VMA_DIRTY);
    if (Vma->Section != NULL)
        ObDereferenceObject(Vma->Section);
    Vma->Section = NULL;
    Vma->ViewBase = 0;
    Vma->ViewOffset = 0;
    return LxMmFillPrivateFile(Process, Vma->Start, Vma->End);
}

static
VOID
LxVmReleaseReservations(
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Start;

    while (Cursor < End)
    {
        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            break;
        if ((Information.State == MEM_RESERVE) && (Information.Type == MEM_PRIVATE) &&
            ((ULONG64)(ULONG_PTR)Information.AllocationBase >= Start))
        {
            LxVmFree((ULONG64)(ULONG_PTR)Information.AllocationBase, 0, MEM_RELEASE);
        }
        Cursor = (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize;
    }
}

static
LONG
LxMmGrowView(
    _In_ PLX_PROCESS Process,
    _In_ PLX_VMA Vma,
    _In_ ULONG64 Address)
{
    ULONG64 Base = Vma->ViewBase, FileOffset = Vma->ViewOffset, MapEnd = Base, FileEnd, NewSize, OldEnd = Base;
    PLX_FILE File = Vma->File;
    BOOLEAN Writable = ((File->Flags & LX_O_ACCMODE) == LX_O_RDWR);
    MEMORY_BASIC_INFORMATION Information;
    LARGE_INTEGER SectionOffset;
    PLIST_ENTRY Entry;
    SIZE_T ViewSize;
    LONG64 Size;
    PVOID View;
    HANDLE Section;
    NTSTATUS Status;

    if ((File == NULL) || ((File->Handle == NULL) && (LxTmpfsFileInode(File) == NULL)))
        return -LX_EFAULT;
    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Piece = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Piece->ViewBase != Base)
            continue;
        MapEnd = max(MapEnd, min(Piece->End, Piece->MapEnd));
    }
    if (!LxMmFileSize(File, &Size))
        return -LX_EFAULT;
    FileEnd = (ULONG64)Size;
    if (FileEnd <= FileOffset + (LX_ALIGN_DOWN(Address, LX_PAGE_SIZE) - Base))
        return -LX_ENXIO;
    NewSize = min(FileEnd - FileOffset, MapEnd - Base);

    if (NT_SUCCESS(LxVmQuery(Base, &Information)) && (Information.Type == MEM_MAPPED) &&
        ((ULONG64)(ULONG_PTR)Information.AllocationBase == Base))
    {
        OldEnd = LxVmAllocationEnd(Base);
        if (OldEnd >= LX_ALIGN_UP(Base + NewSize, LX_PAGE_SIZE))
            return -LX_EFAULT;
        ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)Base);
    }
    LxVmReleaseReservations(LX_ALIGN_UP(OldEnd, LX_ALLOC_GRANULARITY), LX_ALIGN_UP(MapEnd, LX_ALLOC_GRANULARITY));

    Status = LxMmFileSection(File, Writable ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ, &Section);
    if (NT_SUCCESS(Status))
    {
        View = (PVOID)(ULONG_PTR)Base;
        ViewSize = (SIZE_T)NewSize;
        SectionOffset.QuadPart = (LONG64)FileOffset;
        Status = ZwMapViewOfSection(Section, NtCurrentProcess(), &View, 0, 0, &SectionOffset, &ViewSize, ViewShare, 0,
                                    Writable ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ);
        ZwClose(Section);
    }
    if (LX_ALIGN_UP(Base + NewSize, LX_ALLOC_GRANULARITY) < LX_ALIGN_UP(MapEnd, LX_ALLOC_GRANULARITY))
    {
        ULONG64 Tail = LX_ALIGN_UP(Base + NewSize, LX_ALLOC_GRANULARITY);

        LxVmAllocate(&Tail, LX_ALIGN_UP(MapEnd, LX_ALLOC_GRANULARITY) - Tail, MEM_RESERVE, PAGE_NOACCESS);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Linux shared mapping cannot grow its view at %I64x: 0x%08lx\n", Base, Status);
        return -LX_EFAULT;
    }

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Piece = CONTAINING_RECORD(Entry, LX_VMA, Link);
        ULONG64 Start = max(Piece->Start, Base), End = min(Piece->End, LX_ALIGN_UP(Base + NewSize, LX_PAGE_SIZE));

        if ((Piece->ViewBase == Base) && (Start < End))
            LxVmProtect(Start, End - Start, LxProtToNt(Piece->Prot));
    }
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
    LONG Populated = 0, Healed = 0;
    LONG Error;

    while (Cursor < End)
    {
        PLX_VMA Vma = LxVmaFind(Process, Cursor);
        ULONG64 RegionStart, RegionEnd, FillStart, FillEnd;

        if ((Vma == NULL) || !LxProtAllows(Vma->Prot, Access))
            return -LX_EFAULT;
        if (Vma->Flags & LX_VMA_POISON)
            return -LX_EHWPOISON;
        if (!IsListEmpty(&Process->Guards) && LxMmGuardHit(Process, Cursor))
            return -LX_EFAULT;
        if (Vma->Flags & LX_VMA_HUGETLB)
        {
            Cursor = min(Vma->End, End);
            continue;
        }
        if ((Vma->Flags & LX_VMA_VIEW) && (Vma->MapEnd != 0) &&
            (!NT_SUCCESS(LxVmQuery(Cursor, &Information)) || (Information.State != MEM_COMMIT)))
        {
            Error = LxMmGrowView(Process, Vma, Cursor);
            if (Error != 0)
                return Error;
            Populated++;
            continue;
        }
        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            return -LX_EFAULT;
        if (!(Vma->Flags & LX_MAP_SHARED) && !LxVmaBacked(Vma, &Information))
        {
            if (LxVmaIsCow(Vma) && (Vma->ViewBase != 0))
                return -LX_ENXIO;
            if (++Healed > 8)
                return -LX_EFAULT;
            Error = LxMmBackVma(Process, Vma);
            if (Error != 0)
                return Error;
            Populated++;
            continue;
        }
        if (Information.State == MEM_FREE)
            return -LX_EFAULT;

        RegionStart = (ULONG64)(ULONG_PTR)Information.BaseAddress;
        RegionEnd = min(RegionStart + Information.RegionSize, Vma->End);
        if ((Vma->Flags & LX_VMA_VIEW) && (Information.State != MEM_COMMIT))
            return -LX_EFAULT;
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
            FillStart = max(max(RegionStart, Vma->Start), LX_ALIGN_DOWN(Cursor, LX_FAULT_CLUSTER));
            FillEnd = min(RegionEnd, LX_ALIGN_UP(Cursor + 1, LX_FAULT_CLUSTER));
            if (!IsListEmpty(&Process->Guards))
                LxMmGuardClip(Process, Cursor, &FillStart, &FillEnd);
        }
        if ((Vma->File != NULL) && !(Vma->Flags & LX_MAP_SHARED))
        {
            ULONG64 FileEnd = LxMmFileEnd(Vma->File), Offset = Vma->Offset + (Cursor - Vma->Start);

            if (Offset >= FileEnd)
                return -LX_ENXIO;
            if (FileEnd - Vma->Offset < FillEnd - Vma->Start)
                FillEnd = Vma->Start + (FileEnd - Vma->Offset);
        }
        Error = LxMmPopulateRun(Vma, FillStart, FillEnd);
        if (Error != 0)
            return Error;
        if (!(Vma->Flags & LX_MAP_SHARED))
            Vma->Flags |= LX_VMA_TOUCHED;
        Populated++;
        Cursor = FillEnd;
    }
    return Populated;
}

static
VOID
LxMmPinRegion(
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    QUOTA_LIMITS Quota;
    PVOID Base = (PVOID)(ULONG_PTR)Start;
    SIZE_T Size = (SIZE_T)(End - Start);

    if (ZwLockVirtualMemory(NtCurrentProcess(), &Base, &Size, MAP_PROCESS) != STATUS_WORKING_SET_QUOTA)
        return;
    if (!NT_SUCCESS(ZwQueryInformationProcess(NtCurrentProcess(), ProcessQuotaLimits, &Quota, sizeof(Quota), NULL)))
        return;
    Quota.MinimumWorkingSetSize += (SIZE_T)(End - Start);
    if (Quota.MaximumWorkingSetSize < Quota.MinimumWorkingSetSize)
        Quota.MaximumWorkingSetSize = Quota.MinimumWorkingSetSize;
    if (!NT_SUCCESS(ZwSetInformationProcess(NtCurrentProcess(), ProcessQuotaLimits, &Quota, sizeof(Quota))))
        return;
    Base = (PVOID)(ULONG_PTR)Start;
    Size = (SIZE_T)(End - Start);
    ZwLockVirtualMemory(NtCurrentProcess(), &Base, &Size, MAP_PROCESS);
}

static
VOID
LxMmPin(
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ BOOLEAN Pin)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Start;

    while (Cursor < End)
    {
        ULONG64 RegionEnd;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            break;
        RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, End);
        if (RegionEnd <= Cursor)
            break;
        if (Information.State == MEM_COMMIT)
        {
            if (!Pin)
            {
                PVOID Base = (PVOID)(ULONG_PTR)Cursor;
                SIZE_T Size = (SIZE_T)(RegionEnd - Cursor);

                ZwUnlockVirtualMemory(NtCurrentProcess(), &Base, &Size, MAP_PROCESS);
            }
            else if (!(Information.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            {
                LxMmPinRegion(Cursor, RegionEnd);
            }
        }
        Cursor = RegionEnd;
    }
}

static
VOID
LxMmUnpinLocked(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLIST_ENTRY Entry;

    for (Entry = LxVmaEntryAbove(Process, Start); Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Vma->Start >= End)
            break;
        if (Vma->Flags & LX_VMA_LOCKED)
            LxMmPin(max(Vma->Start, Start), min(Vma->End, End), FALSE);
    }
}

static
VOID
LxMmLockPopulate(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG64 Cursor = Start;

    while (Cursor < End)
    {
        PLIST_ENTRY Entry = LxVmaEntryAbove(Process, Cursor);
        PLX_VMA Vma;
        ULONG64 Stop;
        UCHAR Byte;

        if (Entry == &Process->Vmas)
            break;
        Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
        if (Vma->Start >= End)
            break;

        Cursor = max(Cursor, Vma->Start);
        Stop = min(Vma->End, End);
        if (((Vma->Flags & LX_VMA_LOCK_MASK) == LX_VMA_LOCKED) && (Vma->Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC)))
        {
            ULONG64 First = Cursor;

            LxMmPopulate(Process, Cursor, Stop, LX_PROT_READ, FALSE);
            for (; Cursor < Stop; Cursor += LX_PAGE_SIZE)
            {
                if (LxCopyFromUser(&Byte, Cursor, 1) != 0)
                    break;
            }
            LxMmPin(First, Stop, TRUE);
        }
        Cursor = Stop;
    }
}

static
VOID
LxMmGrowDown(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Page)
{
    PLIST_ENTRY Entry = LxVmaEntryAbove(Process, Page);
    ULONG64 Limit = Process->Limits[LX_RLIMIT_STACK].Cur;
    PLX_VMA Vma, Prev;

    if ((Entry == &Process->Vmas) || (Page < LX_MMAP_MIN_ADDR))
        return;
    Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
    if (!(Vma->Flags & LX_MAP_GROWSDOWN) || (Vma->Start <= Page))
        return;
    if (Entry->Blink != &Process->Vmas)
    {
        Prev = CONTAINING_RECORD(Entry->Blink, LX_VMA, Link);
        if (!(Prev->Flags & LX_MAP_GROWSDOWN) && (Prev->Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC)) &&
            (Page - Prev->End < LX_STACK_GUARD_GAP))
        {
            return;
        }
    }
    if ((Limit != LX_RLIM_INFINITY) && (Vma->End - Page > Limit))
        return;
    if (LxVmPlace(Process, Page, Vma->Start, LX_PLACE_FRESH) == 0)
        Vma->Start = Page;
}

BOOLEAN
LxMmAddressMapped(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address)
{
    BOOLEAN Mapped;

    Process = Process->Mm;
    LxMmLock(Process);
    Mapped = (LxVmaFind(Process, Address) != NULL);
    LxMmUnlock(Process);
    return Mapped;
}

BOOLEAN
LxMmHandleFault(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG Access)
{
    ULONG64 Page = LX_ALIGN_DOWN(Address, LX_PAGE_SIZE);
    PLX_THREAD Thread = LxCurrentThread();
    BOOLEAN OnFault;
    PLX_VMA Vma;
    LONG Result;

    Process = Process->Mm;
    LxMmLock(Process);
    if (LxVmaFind(Process, Page) == NULL)
        LxMmGrowDown(Process, Page);
    Result = LxMmPopulate(Process, Page, Page + LX_PAGE_SIZE, Access, TRUE);
    Vma = LxVmaFind(Process, Page);
    OnFault = (Result >= 0) && (Vma != NULL) && (Vma->Flags & LX_VMA_LOCKONFAULT) &&
              (Vma->Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC));
    LxMmUnlock(Process);
    if (OnFault)
        LxMmPin(Page, Page + LX_PAGE_SIZE, TRUE);

    if (Result < 0)
    {
        if ((Result == -LX_ENXIO) && (Thread != NULL))
            Thread->FaultBus = LX_BUS_ADRERR;
        else if ((Result == -LX_EHWPOISON) && (Thread != NULL))
            Thread->FaultBus = LX_BUS_MCEERR_AR;
        return FALSE;
    }
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

    LxMmLock(Thread->Process->Mm);
    Result = LxMmPopulate(Thread->Process->Mm, Start, End, Write ? LX_PROT_WRITE : LX_PROT_READ, FALSE);
    LxMmUnlock(Thread->Process->Mm);
    if (Result < 0)
        return FALSE;
    if ((Result > 0) || (Thread->FaultAddress != Start))
    {
        Thread->FaultAddress = Start;
        Thread->FaultRetries = 0;
        return TRUE;
    }
    return (++Thread->FaultRetries < 8);
}

BOOLEAN
LxMmSharedKey(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _Out_ PVOID *Object,
    _Out_ PULONG64 Offset)
{
    PFILE_OBJECT FileObject;
    BOOLEAN Found = FALSE;
    PLX_VMA Vma;

    Process = Process->Mm;
    LxMmLock(Process);
    Vma = LxVmaFind(Process, Address);
    if ((Vma != NULL) && (Vma->Flags & LX_VMA_VIEW) &&
        (!LxVmaIsCow(Vma) || !((Vma->Prot & LX_PROT_WRITE) || (Vma->Flags & LX_VMA_DIRTY))))
    {
        if ((Vma->Section != NULL) && !LxVmaIsCow(Vma))
        {
            *Object = Vma->Section;
            *Offset = Vma->Offset + (Address - Vma->Start);
            Found = TRUE;
        }
        else if (LxTmpfsFileInode(Vma->File) != NULL)
        {
            *Object = LxTmpfsFileInode(Vma->File);
            *Offset = Vma->Offset + (Address - Vma->Start);
            Found = TRUE;
        }
        else if ((Vma->File != NULL) && (Vma->File->Handle != NULL) &&
                 NT_SUCCESS(ObReferenceObjectByHandle(Vma->File->Handle, 0, *IoFileObjectType, KernelMode,
                                                      (PVOID *)&FileObject, NULL)))
        {
            *Object = (FileObject->SectionObjectPointer != NULL) ? (PVOID)FileObject->SectionObjectPointer :
                                                                   FileObject->FsContext;
            *Offset = Vma->Offset + (Address - Vma->Start);
            ObDereferenceObject(FileObject);
            Found = TRUE;
        }
    }
    LxMmUnlock(Process);
    return Found;
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

    Process = Process->Mm;
    if (End <= Address)
        return 0;

    LxMmLock(Process);
    Error = LxMmPopulate(Process, Page, End, LX_PROT_READ, FALSE);
    Vma = LxVmaFind(Process, Page);
    if ((Error >= 0) && (Vma != NULL))
    {
        if (LxVmaIsCow(Vma))
        {
            Vma->Flags |= LX_VMA_DIRTY;
            LxMmViewAllow(Process, Vma, PAGE_WRITECOPY);
        }
        LxVmProtectRange(Page, End, LxVmaIsCow(Vma) ? PAGE_WRITECOPY : PAGE_READWRITE);
        Error = (LONG)LxZeroUser(Address, (SIZE_T)(End - Address));
        LxVmProtectRange(Page, End, LxVmaNtProtect(Vma));
    }
    LxMmUnlock(Process);
    return (Error < 0) ? Error : 0;
}

static
LONG
LxMmUnmapRange(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    LONG Error = LxVmaRemove(Process, Start, End);

    if (Error == 0)
    {
        LxVmReleaseRange(Process, Start, End);
        LxVmDecommitRange(Start, End);
    }
    return Error;
}

static
BOOLEAN
LxVmIsFree(
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    MEMORY_BASIC_INFORMATION Information;

    return NT_SUCCESS(LxVmQuery(Start, &Information)) &&
           (Information.State == MEM_FREE) &&
           ((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize >= End);
}

static
LONG
LxMmMapView(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot,
    _In_ BOOLEAN Fixed,
    _In_opt_ PLX_FILE File,
    _In_ ULONG64 Offset,
    _Out_ PULONG64 Result,
    _Out_ PVOID *SectionObject,
    _Out_ PULONG64 GrowBase)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    LARGE_INTEGER MaximumSize, SectionOffset;
    ULONG64 Delta = Offset & (LX_ALLOC_GRANULARITY - 1);
    BOOLEAN Writable = (File == NULL) || ((File->Flags & LX_O_ACCMODE) == LX_O_RDWR);
    ULONG Maximum = Writable ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    ULONG Protect = LxProtToNt(Prot);
    BOOLEAN Grow = FALSE;
    SIZE_T ViewSize = (SIZE_T)(Length + Delta);
    PVOID Base = NULL;
    HANDLE Section;
    NTSTATUS Status;

    *SectionObject = NULL;
    *GrowBase = 0;
    if ((Prot & LX_PROT_WRITE) && !Writable)
        return -LX_EACCES;

    if (File != NULL)
    {
        LONG64 Size;

        if ((File->Handle == NULL) && (LxTmpfsFileInode(File) == NULL))
            return 2;
        if (!LxMmFileSize(File, &Size) || (Size < 0))
        {
            DPRINT1("Linux shared mapping cannot query the size of '%s'\n", File->Path);
            return 3;
        }
        Grow = (Offset + Length > LX_ALIGN_UP((ULONG64)Size, LX_PAGE_SIZE));
        ViewSize = ((ULONG64)Size > Offset - Delta) ? (SIZE_T)min(Length + Delta, (ULONG64)Size - (Offset - Delta)) : 0;
    }

    if (Fixed)
    {
        if ((Address - Delta) & (LX_ALLOC_GRANULARITY - 1))
            return 4;
        if (!LxVmIsFree(Address - Delta, Address + Length))
            return 5;
        Base = (PVOID)(ULONG_PTR)(Address - Delta);
    }
    else if ((Address >= LX_MMAP_MIN_ADDR + Delta) &&
             !((Address - Delta) & (LX_ALLOC_GRANULARITY - 1)) &&
             LxVmIsFree(Address - Delta, Address + Length))
    {
        Base = (PVOID)(ULONG_PTR)(Address - Delta);
    }

    if (Grow)
    {
        ULONG64 Region = LX_ALIGN_UP(Length + Delta, LX_ALLOC_GRANULARITY), Tail;

        if (Base == NULL)
        {
            ULONG64 Probe = 0;

            if (!NT_SUCCESS(LxVmReserveUnmapped(Process, &Probe, Region)))
                return -LX_ENOMEM;
            LxVmFree(Probe, 0, MEM_RELEASE);
            Base = (PVOID)(ULONG_PTR)Probe;
        }
        if (ViewSize != 0)
        {
            Status = LxMmFileSection(File, Maximum, &Section);
            if (!NT_SUCCESS(Status))
                return 6;
            SectionOffset.QuadPart = (LONG64)(Offset - Delta);
            Status = ZwMapViewOfSection(Section, NtCurrentProcess(), &Base, 0, 0, &SectionOffset, &ViewSize, ViewShare,
                                        0, Maximum);
            ZwClose(Section);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Linux shared mapping cannot map a view at %p: 0x%08lx\n", Base, Status);
                return 7;
            }
            if (Protect != Maximum)
                LxVmProtect((ULONG64)(ULONG_PTR)Base, ViewSize, Protect);
        }
        Tail = LX_ALIGN_UP((ULONG64)(ULONG_PTR)Base + ViewSize, LX_ALLOC_GRANULARITY);
        if ((Tail < (ULONG64)(ULONG_PTR)Base + Region) &&
            !NT_SUCCESS(LxVmAllocate(&Tail, (ULONG64)(ULONG_PTR)Base + Region - Tail, MEM_RESERVE, PAGE_NOACCESS)))
        {
            if (ViewSize != 0)
                ZwUnmapViewOfSection(NtCurrentProcess(), Base);
            return -LX_ENOMEM;
        }
        *Result = (ULONG64)(ULONG_PTR)Base + Delta;
        *GrowBase = (ULONG64)(ULONG_PTR)Base;
        return 0;
    }

    MaximumSize.QuadPart = (LONG64)Length;
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    if (File != NULL)
        Status = LxMmFileSection(File, Maximum, &Section);
    else
        Status = ZwCreateSection(&Section, SECTION_ALL_ACCESS, &ObjectAttributes, &MaximumSize, Maximum, SEC_COMMIT,
                                 NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Linux shared mapping cannot create a section: 0x%08lx\n", Status);
        return 6;
    }

    SectionOffset.QuadPart = (LONG64)(Offset - Delta);
    Status = ZwMapViewOfSection(Section, NtCurrentProcess(), &Base, 0, 0, &SectionOffset, &ViewSize, ViewShare, 0,
                                Maximum);
    if (!NT_SUCCESS(Status) && !Fixed && (Base != NULL))
    {
        Base = NULL;
        Status = ZwMapViewOfSection(Section, NtCurrentProcess(), &Base, 0, 0, &SectionOffset, &ViewSize, ViewShare, 0,
                                    Maximum);
    }
    if (NT_SUCCESS(Status) && (File == NULL) &&
        !NT_SUCCESS(ObReferenceObjectByHandle(Section, 0, NULL, KernelMode, SectionObject, NULL)))
    {
        *SectionObject = NULL;
    }
    ZwClose(Section);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Linux shared mapping cannot map a view at %p: 0x%08lx\n", Base, Status);
        return 7;
    }

    if (Protect != Maximum)
        LxVmProtect((ULONG64)(ULONG_PTR)Base, Length + Delta, Protect);
    *Result = (ULONG64)(ULONG_PTR)Base + Delta;
    return 0;
}

LONG64
LxMmMapSection(
    _In_ PLX_PROCESS Process,
    _In_ PVOID Section,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot,
    _In_ BOOLEAN Remap)
{
    LARGE_INTEGER SectionOffset;
    SIZE_T ViewSize = (SIZE_T)Length;
    PVOID Base = (PVOID)(ULONG_PTR)Address;
    HANDLE Handle;
    NTSTATUS Status;
    LONG Error = 0;

    Process = Process->Mm;
    if (Address & (LX_ALLOC_GRANULARITY - 1))
        return -LX_EINVAL;
    Status = ObOpenObjectByPointer(Section, OBJ_KERNEL_HANDLE, NULL,
                                   SECTION_MAP_READ | SECTION_MAP_WRITE | SECTION_MAP_EXECUTE, NULL, KernelMode, &Handle);
    if (!NT_SUCCESS(Status))
        return -LX_ENOMEM;

    LxMmLock(Process);
    if ((Address != 0) && LxVmaOverlaps(Process, Address, Address + Length))
        Error = Remap ? LxMmUnmapRange(Process, Address, Address + Length) : -LX_EINVAL;
    if ((Error == 0) && (Address != 0) && !LxVmIsFree(Address, Address + Length))
        Error = -LX_EINVAL;
    if ((Error == 0) && (Address == 0))
    {
        ULONG64 Probe = 0;

        if (NT_SUCCESS(LxVmReserveUnmapped(Process, &Probe, LX_ALIGN_UP(Length, LX_ALLOC_GRANULARITY))))
        {
            LxVmFree(Probe, 0, MEM_RELEASE);
            Base = (PVOID)(ULONG_PTR)Probe;
        }
    }
    if (Error == 0)
    {
        SectionOffset.QuadPart = 0;
        Status = ZwMapViewOfSection(Handle, NtCurrentProcess(), &Base, 0, 0, &SectionOffset, &ViewSize, ViewShare, 0,
                                    (Prot & LX_PROT_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ);
        if (!NT_SUCCESS(Status))
            Error = -LX_ENOMEM;
        else if (LxProtToNt(Prot) != ((Prot & LX_PROT_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ))
            LxVmProtect((ULONG64)(ULONG_PTR)Base, ViewSize, LxProtToNt(Prot));
    }
    if (Error == 0)
    {
        Error = LxVmaInsert(Process, (ULONG64)(ULONG_PTR)Base, (ULONG64)(ULONG_PTR)Base + Length, Prot,
                            LX_MAP_SHARED | LX_VMA_VIEW | LX_VMA_SHM, NULL, 0, Section);
        if (Error != 0)
            ZwUnmapViewOfSection(NtCurrentProcess(), Base);
    }
    LxMmUnlock(Process);
    ZwClose(Handle);
    return (Error != 0) ? Error : (LONG64)(ULONG_PTR)Base;
}

LONG
LxMmDetachSection(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address)
{
    PLX_VMA Vma;
    PLIST_ENTRY Entry;
    ULONG64 End;
    LONG Error = -LX_EINVAL;

    Process = Process->Mm;
    LxMmLock(Process);
    Vma = LxVmaFind(Process, Address);
    if ((Vma != NULL) && (Vma->Start == Address) && (Vma->Flags & LX_VMA_SHM) && (Vma->Offset == 0))
    {
        End = Vma->End;
        for (Entry = Vma->Link.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
        {
            PLX_VMA Next = CONTAINING_RECORD(Entry, LX_VMA, Link);

            if ((Next->Start != End) || (Next->Section != Vma->Section) || (Next->File != Vma->File) ||
                !(Next->Flags & LX_VMA_SHM) || (Next->Offset != Next->Start - Address))
                break;
            End = Next->End;
        }
        Error = LxMmUnmapRange(Process, Address, End);
    }
    LxMmUnlock(Process);
    return Error;
}

static
LONG
LxMmMapCow(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot,
    _In_ ULONG Flags,
    _In_ PLX_FILE File,
    _In_ ULONG64 Offset,
    _Out_ PULONG64 Result)
{
    BOOLEAN Fixed = (Flags & (LX_MAP_FIXED | LX_MAP_FIXED_NOREPLACE)) != 0;
    ULONG64 Delta = Offset & (LX_ALLOC_GRANULARITY - 1), Base = 0, ViewEnd, Probe;
    LONG64 FileSize;
    PVOID Section;
    NTSTATUS Status;
    LONG Error;

    if (((File->Handle == NULL) && (LxTmpfsFileInode(File) == NULL)) || (File->Type != LX_S_IFREG) ||
        ((File->Flags & LX_O_ACCMODE) == LX_O_WRONLY))
        return 1;
    if (!LxMmFileSize(File, &FileSize) || (FileSize <= 0) || ((ULONG64)FileSize <= Offset))
        return 1;

    if (Fixed)
    {
        if ((Address - Delta) & (LX_ALLOC_GRANULARITY - 1))
            return 1;
        Base = Address - Delta;
    }
    else
    {
        if ((Address != 0) && !((Address - Delta) & (LX_ALLOC_GRANULARITY - 1)) &&
            (Address - Delta >= LX_MMAP_MIN_ADDR) && (Address + Length > Address) &&
            !LxVmaOverlaps(Process, Address - Delta, LX_ALIGN_UP(Address + Length, LX_ALLOC_GRANULARITY)) &&
            LxVmIsFree(Address - Delta, LX_ALIGN_UP(Address + Length, LX_ALLOC_GRANULARITY)))
        {
            Base = Address - Delta;
        }
        else
        {
            Probe = 0;
            if (!NT_SUCCESS(LxVmReserveUnmapped(Process, &Probe, LX_ALIGN_UP(Delta + Length, LX_ALLOC_GRANULARITY))))
                return -LX_ENOMEM;
            LxVmFree(Probe, 0, MEM_RELEASE);
            Base = Probe;
        }
        Address = Base + Delta;
    }
    ViewEnd = Address + min(Length, LX_ALIGN_UP((ULONG64)FileSize, LX_PAGE_SIZE) - Offset);

    Status = LxMmCreateCowSection(File, &Section);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("Linux file mapping of '%s' is a private copy: 0x%08lx\n", File->Path, Status);
        return 1;
    }

    if (Fixed)
    {
        Error = LxVmaRemove(Process, Address, Address + Length);
        if (Error == 0)
            Error = LxMmClearForView(Process, Base, LX_ALIGN_UP(ViewEnd, LX_ALLOC_GRANULARITY));
        if (Error != 0)
        {
            ObDereferenceObject(Section);
            return (Error < 0) ? Error : 1;
        }
    }

    Status = LxMmMapCowView(Section, Base, Offset - Delta, ViewEnd - Base, LxCowViewMaximum(Prot), LxCowProtToNt(Prot));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Linux file mapping of '%s' cannot map a view at %I64x: 0x%08lx\n", File->Path, Base, Status);
        ObDereferenceObject(Section);
        return 1;
    }
    if (Delta != 0)
        LxVmProtect(Base, Delta, PAGE_NOACCESS);
    if (Address + Length > LX_ALIGN_UP(ViewEnd, LX_ALLOC_GRANULARITY))
        LxVmPlace(Process, LX_ALIGN_UP(ViewEnd, LX_ALLOC_GRANULARITY), Address + Length, LX_PLACE_FRESH | LX_PLACE_SPLIT);

    Error = LxVmaInsertView(Process, Address, Address + Length, Prot,
                            LX_MAP_PRIVATE | LX_VMA_VIEW | (Flags & (LX_VMA_HEAP | LX_VMA_STACK)), File, Offset, Section,
                            Base, Offset - Delta, 0);
    ObDereferenceObject(Section);
    if (Error != 0)
    {
        ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)Base);
        return Error;
    }
    *Result = Address;
    return 0;
}

static
LONG
LxMmFindHugeRange(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Hint,
    _In_ ULONG64 Length,
    _In_ ULONG64 PageSize,
    _Out_ PULONG64 Base)
{
    ULONG64 Probe = 0, Aligned;

    Hint = LX_ALIGN_UP(Hint, PageSize);
    if ((Hint >= LX_MMAP_MIN_ADDR) && (Hint + Length > Hint) && LxVmIsFree(Hint, Hint + Length) &&
        !LxVmaOverlaps(Process, Hint, Hint + Length))
    {
        *Base = Hint;
        return 0;
    }
    if ((Length + PageSize < Length) || !NT_SUCCESS(LxVmReserveUnmapped(Process, &Probe, Length + PageSize)))
        return -LX_ENOMEM;
    LxVmFree(Probe, 0, MEM_RELEASE);
    Aligned = LX_ALIGN_UP(Probe, PageSize);
    if (LxVmaOverlaps(Process, Aligned, Aligned + Length))
        return -LX_ENOMEM;
    *Base = Aligned;
    return 0;
}

static
LONG
LxMmMapHuge(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot,
    _In_ ULONG Flags,
    _In_ ULONG VmaFlags,
    _In_opt_ PLX_FILE File,
    _In_ ULONG64 Offset,
    _Out_ PULONG64 Result)
{
    ULONG64 PageSize = LxHugePageSize(), Pages, Index, Base = 0, First = 0;
    BOOLEAN Fixed = (Flags & (LX_MAP_FIXED | LX_MAP_FIXED_NOREPLACE)) != 0;
    ULONG HugeLog = (Flags & LX_MAP_HUGE_MASK) >> LX_MAP_HUGE_SHIFT;
    BOOLEAN Mapped = FALSE;
    PLX_FILE Backing = File;
    PLX_HUGE_STORE Store;
    LONG Error;

    if ((PageSize == 0) || ((File == NULL) && (HugeLog != 0) && ((1ULL << HugeLog) != PageSize)))
        return -LX_EINVAL;
    if ((Offset & (PageSize - 1)) || (Fixed && (Address & (PageSize - 1))))
        return -LX_EINVAL;
    Length = LX_ALIGN_UP(Length, PageSize);
    if ((Length == 0) || (Fixed && (Address + Length < Address)))
        return -LX_ENOMEM;
    Pages = Length / PageSize;

    if ((File == NULL) || !(VmaFlags & LX_MAP_SHARED))
    {
        Backing = LxHugeFileCreate((File != NULL) ? File->Path : "/anon_hugepage (deleted)");
        if (Backing == NULL)
            return -LX_ENOMEM;
    }
    else
    {
        LxFileReference(Backing);
        First = Offset / PageSize;
    }
    Store = Backing->Memfd->Huge;
    Error = LxHugeStoreReserve(Store, First, Pages, FALSE);
    for (Index = 0; (Error == 0) && (Backing != File) && (File != NULL) && (Index < Pages); Index++)
    {
        PVOID Source = LxHugeStorePage(File->Memfd->Huge, Offset / PageSize + Index);
        PVOID Target;

        if (Source == NULL)
            continue;
        Target = LxHugeStorePage(Store, Index);
        if ((Target == NULL) || !NT_SUCCESS(LxHugeCopyPage(Source, Target)))
            Error = -LX_ENOMEM;
        if (Target != NULL)
            ObDereferenceObject(Target);
        ObDereferenceObject(Source);
    }

    if (Error == 0)
    {
        if (!Fixed)
            Error = LxMmFindHugeRange(Process, Address, Length, PageSize, &Base);
        else if ((Flags & LX_MAP_FIXED_NOREPLACE) && LxVmaOverlaps(Process, Address, Address + Length))
            Error = -LX_EEXIST;
        else if ((Error = LxMmUnmapRange(Process, Address, Address + Length)) == 0)
            Base = Address;
    }
    for (Index = 0; (Error == 0) && (Index < Pages); Index++)
    {
        PVOID Section = LxHugeStorePage(Store, First + Index);
        PVOID View = (PVOID)(ULONG_PTR)(Base + Index * PageSize);

        if ((Section == NULL) || !NT_SUCCESS(LxHugeMapPage(Section, NtCurrentProcess(), &View, PAGE_READWRITE)))
        {
            while (Index != 0)
                ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)(Base + --Index * PageSize));
            Error = -LX_ENOMEM;
        }
        else
        {
            LxHugeStoreFault(Store, First + Index);
        }
        if (Section != NULL)
            ObDereferenceObject(Section);
    }
    Mapped = (Error == 0);
    if ((Error == 0) && ((Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC)) != (LX_PROT_READ | LX_PROT_WRITE)))
        LxVmProtect(Base, Length, LxProtToNt(Prot));
    if (Error == 0)
        Error = LxVmaInsertView(Process, Base, Base + Length, Prot, VmaFlags | LX_VMA_HUGETLB, Backing,
                                First * PageSize, NULL, 0, 0, 0);
    if ((Error != 0) && Mapped)
    {
        for (Index = 0; Index < Pages; Index++)
            ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)(Base + Index * PageSize));
    }
    if ((Error == 0) && (Backing == File) && (Prot & LX_PROT_WRITE) && (File->Handle != NULL))
    {
        FILE_STANDARD_INFORMATION Standard;
        FILE_END_OF_FILE_INFORMATION EndOfFile;
        IO_STATUS_BLOCK Iosb;

        if (NT_SUCCESS(ZwQueryInformationFile(File->Handle, &Iosb, &Standard, sizeof(Standard),
                                              FileStandardInformation)) &&
            ((ULONG64)Standard.EndOfFile.QuadPart < Offset + Length))
        {
            EndOfFile.EndOfFile.QuadPart = (LONG64)(Offset + Length);
            ZwSetInformationFile(File->Handle, &Iosb, &EndOfFile, sizeof(EndOfFile), FileEndOfFileInformation);
        }
    }
    LxFileDereference(Backing);
    if (Error == 0)
        *Result = Base;
    return Error;
}

LONG64
LxMmMapHugeSection(
    _In_ PLX_PROCESS Process,
    _In_ PLX_FILE File,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot,
    _In_ BOOLEAN Remap)
{
    ULONG Flags = LX_MAP_SHARED | ((Address == 0) ? 0 : (Remap ? LX_MAP_FIXED : LX_MAP_FIXED_NOREPLACE));
    ULONG64 Base = 0;
    LONG Error;

    Process = Process->Mm;
    LxMmLock(Process);
    Error = LxMmMapHuge(Process, Address, Length, Prot, Flags, LX_MAP_SHARED | LX_VMA_SHM, File, 0, &Base);
    LxMmUnlock(Process);
    if (Error == -LX_EEXIST)
        return -LX_EINVAL;
    return (Error != 0) ? Error : (LONG64)Base;
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
    ULONG VmaFlags = Flags & (LX_MAP_SHARED | LX_MAP_PRIVATE | LX_MAP_GROWSDOWN | LX_VMA_HEAP | LX_VMA_STACK |
                              LX_VMA_NOMAYWRITE);
    PLX_PROCESS Caller = Process;
    BOOLEAN Fresh = FALSE;
    ULONG64 Base = 0, GrowBase = 0;
    PVOID Section = NULL;
    LONG Error = 0;
    ULONG Lock;

    Process = Process->Mm;
    if ((Length == 0) || (Offset & (LX_PAGE_SIZE - 1)))
        return -LX_EINVAL;
    if (Fixed && (Address & (LX_PAGE_SIZE - 1)))
        return -LX_EINVAL;

    Length = LX_ALIGN_UP(Length, LX_PAGE_SIZE);
    if ((Length == 0) || (Fixed && (Address + Length < Address)))
        return -LX_ENOMEM;
    if (Fixed && (Address < LX_MMAP_MIN_ADDR))
        return -LX_EPERM;
    if ((Flags & LX_MAP_LOCKED) && !LxMmCanLock(Caller))
        return -LX_EPERM;
    if ((File != NULL) && (Flags & LX_MAP_HUGETLB) && ((File->Memfd == NULL) || (File->Memfd->Huge == NULL)))
        return -LX_EINVAL;

    LxMmLock(Process);
    if (Process->VmaCount > LxMaxMapCount)
    {
        LxMmUnlock(Process);
        return -LX_ENOMEM;
    }
    Lock = Process->MmLockDefault | ((Flags & LX_MAP_LOCKED) ? LX_VMA_LOCKED : 0);
    if ((Lock != 0) && !LxMmLockAllowed(Caller, Process, Length))
    {
        LxMmUnlock(Process);
        return -LX_EAGAIN;
    }
    VmaFlags |= Lock;

    if ((File != NULL) ? ((File->Memfd != NULL) && (File->Memfd->Huge != NULL)) : ((Flags & LX_MAP_HUGETLB) != 0))
    {
        Error = LxMmMapHuge(Process, Address, Length, Prot, Flags, VmaFlags, File, Offset, &Base);
        if ((Error == 0) && (Lock == LX_VMA_LOCKED))
            LxMmLockPopulate(Process, Base, Base + LX_ALIGN_UP(Length, LxHugePageSize()));
        LxMmUnlock(Process);
        return (Error != 0) ? Error : (LONG64)Base;
    }

    if ((VmaFlags & LX_MAP_SHARED) &&
        !((Flags & LX_MAP_FIXED_NOREPLACE) && LxVmaOverlaps(Process, Address, Address + Length)))
    {
        if (Fixed)
            Error = LxMmUnmapRange(Process, Address, Address + Length);
        if ((Error == 0) && Fixed && !LxVmIsFree(Address, Address + Length))
            Error = LxMmClearForView(Process, Address, Address + Length);
        if (Error == 0)
            Error = LxMmMapView(Process, Address, Length, Prot, Fixed, File, Offset, &Base, &Section, &GrowBase);
        if (Error == 0)
        {
            Error = LxVmaInsertView(Process, Base, Base + Length, Prot, VmaFlags | LX_VMA_VIEW, File, Offset, Section,
                                    GrowBase, (GrowBase != 0) ? (Offset - (Base - GrowBase)) : 0,
                                    (GrowBase != 0) ? (Base + Length) : 0);
            if (Section != NULL)
                ObDereferenceObject(Section);
            if (Error != 0)
                ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)LX_ALIGN_DOWN(Base, LX_ALLOC_GRANULARITY));
            else if (Lock == LX_VMA_LOCKED)
                LxMmLockPopulate(Process, Base, Base + Length);
            LxMmUnlock(Process);
            return (Error != 0) ? Error : (LONG64)Base;
        }
        if (Error < 0)
        {
            LxMmUnlock(Process);
            return Error;
        }
        DPRINT1("Linux shared mapping of '%s' in process %lu is a private copy (reason %ld)\n",
                (File != NULL) ? File->Path : "anonymous memory", Process->Pid, Error);
        Error = 0;
        Base = 0;
    }
    else if ((File != NULL) && !(Prot & LX_PROT_WRITE) &&
             !((Flags & LX_MAP_FIXED_NOREPLACE) && LxVmaOverlaps(Process, Address, Address + Length)))
    {
        Error = LxMmMapCow(Process, Address, Length, Prot, Flags, File, Offset, &Base);
        if ((Error == 0) && (Lock != 0))
        {
            LxVmaSetLock(Process, Base, Base + Length, Lock);
            if (Lock == LX_VMA_LOCKED)
                LxMmLockPopulate(Process, Base, Base + Length);
        }
        if (Error <= 0)
        {
            LxMmUnlock(Process);
            return (Error != 0) ? Error : (LONG64)Base;
        }
        Error = 0;
        Base = 0;
    }

    if (Fixed)
    {
        if ((Flags & LX_MAP_FIXED_NOREPLACE) && LxVmaOverlaps(Process, Address, Address + Length))
            Error = -LX_EEXIST;
        else
            Error = LxVmaRemove(Process, Address, Address + Length);
        if (Error == 0)
            Error = LxVmPlace(Process, Address, Address + Length, LX_PLACE_FRESH | LX_PLACE_SPLIT);
        Base = Address;
    }
    else
    {
        if (Address != 0)
        {
            ULONG64 Hint = LX_ALIGN_UP(Address, LX_PAGE_SIZE);

            if ((Hint >= LX_MMAP_MIN_ADDR) && (Hint + Length > Hint) &&
                !LxVmaOverlaps(Process, Hint, Hint + Length) &&
                (LxVmPlace(Process, Hint, Hint + Length, LX_PLACE_FRESH) == 0))
            {
                Base = Hint;
            }
        }
        if (Base == 0)
        {
            if ((Flags & LX_VMA_STACK) ?
                NT_SUCCESS(LxVmReserveStack(Process, &Base, LX_ALIGN_UP(Length, LX_ALLOC_GRANULARITY))) :
                NT_SUCCESS(LxVmReserveUnmapped(Process, &Base, LX_ALIGN_UP(Length, LX_ALLOC_GRANULARITY))))
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
        Error = LxVmaInsert(Process, Base, Base + Length, Prot, VmaFlags, File, Offset, NULL);
        if ((Error != 0) && Fresh)
            LxVmFree(Base, 0, MEM_RELEASE);
    }
    if ((Error == 0) && (File != NULL))
    {
        ULONG64 FileEnd = LxMmFileEnd(File);

        if (Offset < FileEnd)
            Error = LxMmFillPrivateFile(Process, Base, Base + min(Length, FileEnd - Offset));
        if (Error != 0)
            LxMmUnmapRange(Process, Base, Base + Length);
    }
    if (Error != 0)
    {
        LxMmUnlock(Process);
        return Error;
    }

    if (Lock == LX_VMA_LOCKED)
        LxMmLockPopulate(Process, Base, Base + Length);
    else if ((Flags & LX_MAP_POPULATE) && (Prot != LX_PROT_NONE))
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

    Process = Process->Mm;
    if ((Address & (LX_PAGE_SIZE - 1)) || (Length == 0))
        return -LX_EINVAL;

    Length = LX_ALIGN_UP(Length, LX_PAGE_SIZE);
    End = Address + Length;
    if ((Length == 0) || (End < Address))
        return -LX_EINVAL;

    LxMmLock(Process);
    Error = LxMmUnmapRange(Process, Address, End);
    LxMmUnlock(Process);
    return Error;
}

static
BOOLEAN
LxVmaNoExec(
    _In_ PLX_VMA Vma)
{
    return (Vma->File != NULL) && (Vma->File->Mount != NULL) && ((Vma->File->Mount->Flags & LX_MS_NOEXEC) != 0);
}

LONG64
LxMmProtect(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot)
{
    MEMORY_BASIC_INFORMATION Information;
    PLIST_ENTRY Entry;
    ULONG64 Cursor, End;
    BOOLEAN Upgraded;
    LONG Error;

    Process = Process->Mm;
    if (Address & (LX_PAGE_SIZE - 1))
        return -LX_EINVAL;
    if (Length == 0)
        return 0;

    End = Address + LX_ALIGN_UP(Length, LX_PAGE_SIZE);
    if (End <= Address)
        return -LX_ENOMEM;

    LxMmLock(Process);
    Error = LxVmaCovers(Process, Address, End) ? 0 : -LX_ENOMEM;
    for (Entry = LxVmaEntryAbove(Process, Address);
         (Error == 0) && (Prot & (LX_PROT_WRITE | LX_PROT_EXEC)) && (Entry != &Process->Vmas); Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Vma->Start >= End)
            break;
        if (((Prot & LX_PROT_WRITE) && (Vma->Flags & LX_VMA_NOMAYWRITE)) || ((Prot & LX_PROT_EXEC) && LxVmaNoExec(Vma)))
            Error = -LX_EACCES;
    }
    if (Error == 0)
        Error = LxVmaSetProt(Process, Address, End, Prot);

    for (Entry = LxVmaEntryAbove(Process, Address); (Error == 0) && (Entry != &Process->Vmas); Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
        ULONG NtProtect;

        if (Vma->Start >= End)
            break;
        if (Vma->End <= Address)
            continue;
        if (LxVmaIsCow(Vma) && (Prot & LX_PROT_WRITE))
            Vma->Flags |= LX_VMA_DIRTY;
        NtProtect = LxVmaNtProtect(Vma);
        Upgraded = FALSE;

        for (Cursor = max(Vma->Start, Address); (Error == 0) && (Cursor < min(Vma->End, End));)
        {
            ULONG64 RegionEnd;
            NTSTATUS Status;

            if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
                break;
            RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, min(Vma->End, End));
            if ((Information.State == MEM_COMMIT) && LxVmaBacked(Vma, &Information))
            {
                if (LxVmaIsCow(Vma) && !Upgraded &&
                    ((LxViewAccess(Information.AllocationProtect) & LxViewAccess(NtProtect)) != LxViewAccess(NtProtect)))
                {
                    Upgraded = TRUE;
                    Error = LxMmUpgradeView(Process, Vma->ViewBase);
                    continue;
                }
                Status = LxVmProtect(Cursor, RegionEnd - Cursor, NtProtect);
                if (!NT_SUCCESS(Status))
                    Error = -LxStatusToErrno(Status);
                else if (Prot & LX_PROT_EXEC)
                    LxVmFlushCode(Cursor, RegionEnd - Cursor);
            }
            Cursor = RegionEnd;
        }
    }
    if (Error == 0)
        LxVmaMerge(Process, Address, End);
    LxMmUnlock(Process);
    return Error;
}

VOID
LxMmRelease(
    _In_ PLX_PROCESS Process)
{
    while (!IsListEmpty(&Process->Vmas))
    {
        PLX_VMA Vma = CONTAINING_RECORD(RemoveHeadList(&Process->Vmas), LX_VMA, Link);

        LxVmaAccountMemfd(Vma, -1);
        LxVmaFree(Vma);
    }
    Process->VmaTree.Root = NULL;
    Process->VmaTree.Min = NULL;
    Process->VmaCount = 0;
    while (!IsListEmpty(&Process->Guards))
        LxFree(CONTAINING_RECORD(RemoveHeadList(&Process->Guards), LX_GUARD, Link));
}

static
PLX_FILE
LxMmHugeForkCopy(
    _In_ PLX_VMA Vma)
{
    ULONG64 PageSize = LxHugePageSize(), Pages = (Vma->End - Vma->Start) / PageSize, Index;
    PLX_FILE Copy = LxHugeFileCreate(Vma->File->Path);

    if (Copy == NULL)
        return NULL;
    if (LxHugeStoreReserve(Copy->Memfd->Huge, 0, Pages, TRUE) != 0)
    {
        LxFileDereference(Copy);
        return NULL;
    }
    for (Index = 0; Index < Pages; Index++)
    {
        PVOID Source = LxHugeStorePage(Vma->File->Memfd->Huge, Vma->Offset / PageSize + Index);
        PVOID Target = LxHugeStorePage(Copy->Memfd->Huge, Index);
        NTSTATUS Status = ((Source != NULL) && (Target != NULL)) ? LxHugeCopyPage(Source, Target) : STATUS_NO_MEMORY;

        if (Source != NULL)
            ObDereferenceObject(Source);
        if (Target != NULL)
            ObDereferenceObject(Target);
        if (!NT_SUCCESS(Status))
        {
            LxFileDereference(Copy);
            return NULL;
        }
    }
    return Copy;
}

LONG
LxMmFork(
    _In_ PLX_PROCESS Child,
    _In_ PLX_PROCESS Parent)
{
    PLIST_ENTRY Entry;

    Parent = Parent->Mm;
    for (Entry = Parent->Vmas.Flink; Entry != &Parent->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
        PLX_VMA Copy;

        if (Vma->Flags & LX_VMA_DONTFORK)
            continue;
        Copy = LxAlloc(sizeof(*Copy));
        if (Copy == NULL)
        {
            LxMmRelease(Child);
            return -LX_ENOMEM;
        }
        Copy->Start = Vma->Start;
        Copy->End = Vma->End;
        Copy->Prot = Vma->Prot;
        Copy->Flags = Vma->Flags & ~LX_VMA_LOCK_MASK;
        if (!(Vma->Flags & LX_MAP_SHARED) && (Vma->Flags & (LX_VMA_TOUCHED | LX_VMA_DIRTY)))
            Copy->Flags |= LX_VMA_FORKED;
        Copy->File = Vma->File;
        Copy->Offset = Vma->Offset;
        Copy->Section = Vma->Section;
        Copy->ViewBase = Vma->ViewBase;
        Copy->ViewOffset = Vma->ViewOffset;
        Copy->MapEnd = Vma->MapEnd;
        if ((Vma->Flags & LX_VMA_HUGETLB) && !(Vma->Flags & LX_MAP_SHARED))
        {
            Copy->File = LxMmHugeForkCopy(Vma);
            Copy->Offset = 0;
            if (Copy->File == NULL)
            {
                LxFree(Copy);
                LxMmRelease(Child);
                return -LX_ENOMEM;
            }
        }
        else if (Copy->File != NULL)
        {
            LxFileReference(Copy->File);
        }
        if (Copy->Section != NULL)
            ObReferenceObject(Copy->Section);
        if (Copy->Flags & LX_VMA_SHM)
            LxIpcShmVmaOpen(LxVmaShmObject(Copy));
        LxVmaLink(Child, Copy, &Child->Vmas);
    }
    for (Entry = Parent->Guards.Flink; Entry != &Parent->Guards; Entry = Entry->Flink)
    {
        PLX_GUARD Guard = CONTAINING_RECORD(Entry, LX_GUARD, Link);
        PLX_GUARD Copy = LxAlloc(sizeof(*Copy));

        if (Copy == NULL)
        {
            LxMmRelease(Child);
            return -LX_ENOMEM;
        }
        Copy->Start = Guard->Start;
        Copy->End = Guard->End;
        InsertTailList(&Child->Guards, &Copy->Link);
    }
    return 0;
}

VOID
LxMmForkChild(
    _In_ PLX_PROCESS Parent,
    _In_ PLX_PROCESS Child,
    _In_ HANDLE ProcessHandle)
{
    ULONG64 PageSize = LxHugePageSize();
    PLIST_ENTRY Entry;

    for (Entry = Child->Vmas.Flink; Entry != &Child->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
        ULONG64 Address;

        if (!(Vma->Flags & LX_VMA_HUGETLB) || (Vma->Flags & LX_MAP_SHARED))
            continue;
        for (Address = Vma->Start; Address < Vma->End; Address += PageSize)
        {
            PVOID Section = LxHugeStorePage(Vma->File->Memfd->Huge, (Vma->Offset + (Address - Vma->Start)) / PageSize);
            PVOID View = (PVOID)(ULONG_PTR)Address;

            ZwUnmapViewOfSection(ProcessHandle, View);
            if (Section == NULL)
                continue;
            if (NT_SUCCESS(LxHugeMapPage(Section, ProcessHandle, &View, PAGE_READWRITE)) &&
                ((Vma->Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC)) != (LX_PROT_READ | LX_PROT_WRITE)))
            {
                SIZE_T Size = (SIZE_T)PageSize;
                ULONG OldProtect;

                ZwProtectVirtualMemory(ProcessHandle, &View, &Size, LxProtToNt(Vma->Prot), &OldProtect);
            }
            ObDereferenceObject(Section);
        }
    }

    Parent = Parent->Mm;
    for (Entry = Parent->Vmas.Flink; Entry != &Parent->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
        PVOID Base = (PVOID)(ULONG_PTR)Vma->Start;
        SIZE_T Size = (SIZE_T)(Vma->End - Vma->Start);
        ULONG OldProtect;

        if (!(Vma->Flags & (LX_VMA_DONTFORK | LX_VMA_WIPEONFORK)))
            continue;
        if (Vma->Flags & LX_VMA_VIEW)
            ZwProtectVirtualMemory(ProcessHandle, &Base, &Size, PAGE_NOACCESS, &OldProtect);
        else
            ZwFreeVirtualMemory(ProcessHandle, &Base, &Size, MEM_DECOMMIT);
    }
}

VOID
LxMmReset(
    _In_ PLX_PROCESS Process)
{
    MEMORY_BASIC_INFORMATION Information;
    PLIST_ENTRY Entry;
    ULONG64 Cursor = 0;

    LxMmLock(Process);
    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (LxVmaIsCow(Vma))
        {
            if (Vma->ViewBase != 0)
                ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)Vma->ViewBase);
        }
        else if (!(Vma->Flags & LX_VMA_VIEW) && !(Vma->Start & (LX_ALLOC_GRANULARITY - 1)))
        {
            LxVmFree(Vma->Start, 0, MEM_RELEASE);
        }
    }

    while (NT_SUCCESS(LxVmQuery(Cursor, &Information)))
    {
        ULONG64 Next = (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize;

        if ((Information.State != MEM_FREE) && (Information.Type == MEM_PRIVATE))
            LxVmFree((ULONG64)(ULONG_PTR)Information.AllocationBase, 0, MEM_RELEASE);
        else if ((Information.State != MEM_FREE) && (Information.Type == MEM_MAPPED))
            ZwUnmapViewOfSection(NtCurrentProcess(), Information.AllocationBase);
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
    Process->MmLockDefault = 0;
    LxMmUnlock(Process);
}

LX_SYSCALL(LxSysMmap)
{
    ULONG Flags = (ULONG)Call->Args[3] & ~(LX_VMA_VIEW | LX_VMA_HEAP | LX_VMA_STACK | LX_VMA_NOMAYWRITE);
    ULONG Type = Flags & LX_MAP_TYPE;
    ULONG Prot = (ULONG)Call->Args[2];
    ULONG Access = 0;
    PLX_FILE File = NULL;
    BOOLEAN Zero = FALSE;
    LONG64 Result = 0;

    if (Call->Args[5] & (LX_PAGE_SIZE - 1))
        return -LX_EINVAL;
    if (!(Flags & LX_MAP_ANONYMOUS))
    {
        File = LxFdGet(Call->Process, (LONG)Call->Args[4]);
        if (File == NULL)
            return -LX_EBADF;
        Access = File->Flags & LX_O_ACCMODE;
    }

    if ((Call->Args[1] == 0) ||
        ((Type != LX_MAP_SHARED) && (Type != LX_MAP_PRIVATE) && (Type != LX_MAP_SHARED_VALIDATE)))
    {
        Result = -LX_EINVAL;
    }
    else if ((Type == LX_MAP_SHARED_VALIDATE) &&
             (Flags & ~(LX_MAP_TYPE | LX_MAP_FIXED | LX_MAP_ANONYMOUS | LX_MAP_GROWSDOWN | LX_MAP_DENYWRITE |
                        LX_MAP_EXECUTABLE | LX_MAP_LOCKED | LX_MAP_NORESERVE | LX_MAP_POPULATE | LX_MAP_NONBLOCK |
                        LX_MAP_STACK | LX_MAP_HUGETLB | LX_MAP_FIXED_NOREPLACE | LX_MAP_UNINITIALIZED |
                        LX_MAP_HUGE_MASK)))
    {
        Result = -LX_EOPNOTSUPP;
    }
    else if ((File != NULL) &&
             ((Access == LX_O_WRONLY) || ((Type != LX_MAP_PRIVATE) && (Prot & LX_PROT_WRITE) && (Access != LX_O_RDWR))))
    {
        Result = -LX_EACCES;
    }
    else if ((File != NULL) && (Prot & LX_PROT_EXEC) && (File->Mount != NULL) && (File->Mount->Flags & LX_MS_NOEXEC))
    {
        Result = -LX_EPERM;
    }
    else if ((File != NULL) && (File->Memfd != NULL) && (Type != LX_MAP_PRIVATE) && (Prot & LX_PROT_WRITE) &&
             (File->Memfd->Seals & (LX_F_SEAL_WRITE | LX_F_SEAL_FUTURE_WRITE)))
    {
        Result = -LX_EPERM;
    }
    else if ((File != NULL) && LxDrmIsFile(File))
    {
        Result = LxDrmMmap(Call->Process, File, Call->Args[0], Call->Args[1], Prot, Flags, Call->Args[5]);
        LxFileDereference(File);
        return Result;
    }
    else if ((File != NULL) &&
             (((File->Handle == NULL) && (File->Data == NULL) && (LxTmpfsFileInode(File) == NULL)) || File->Directory ||
              File->NtPipe))
    {
        Zero = (File->Type == LX_S_IFCHR) && (File->Rdev == ((1 << 8) | 5));
        if (!Zero)
            Result = -LX_ENODEV;
    }

    if (Result == 0)
    {
        if (Type == LX_MAP_SHARED_VALIDATE)
            Flags = (Flags & ~LX_MAP_TYPE) | LX_MAP_SHARED;
        if ((File != NULL) && (Type != LX_MAP_PRIVATE) &&
            ((Access != LX_O_RDWR) ||
             ((File->Memfd != NULL) && (File->Memfd->Seals & (LX_F_SEAL_WRITE | LX_F_SEAL_FUTURE_WRITE)))))
        {
            Flags |= LX_VMA_NOMAYWRITE;
        }
        if ((Prot & LX_PROT_READ) && (Call->Process->Personality & LX_PER_READ_IMPLIES_EXEC) &&
            !((File != NULL) && (File->Mount != NULL) && (File->Mount->Flags & LX_MS_NOEXEC)))
        {
            Prot |= LX_PROT_EXEC;
        }
        Result = LxMmMap(Call->Process, Call->Args[0], Call->Args[1], Prot, Flags, Zero ? NULL : File, Call->Args[5]);
    }
    if (File != NULL)
        LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysMunmap)
{
    return LxMmUnmap(Call->Process, Call->Args[0], Call->Args[1]);
}

/* FIXME: READ_IMPLIES_EXEC adds PROT_EXEC to the whole range unless a noexec mapping is in it; Linux decides per mapping. */
static
ULONG
LxReadImpliesExec(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot)
{
    PLX_PROCESS Mm = Process->Mm;
    ULONG64 End = Address + LX_ALIGN_UP(Length, LX_PAGE_SIZE);
    PLIST_ENTRY Entry;

    if (!(Prot & LX_PROT_READ) || !(Process->Personality & LX_PER_READ_IMPLIES_EXEC))
        return Prot;
    LxMmLock(Mm);
    for (Entry = LxVmaEntryAbove(Mm, Address); Entry != &Mm->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Vma->Start >= End)
            break;
        if (LxVmaNoExec(Vma))
        {
            LxMmUnlock(Mm);
            return Prot;
        }
    }
    LxMmUnlock(Mm);
    return Prot | LX_PROT_EXEC;
}

LX_SYSCALL(LxSysMprotect)
{
    return LxMmProtect(Call->Process, Call->Args[0], Call->Args[1],
                       LxReadImpliesExec(Call->Process, Call->Args[0], Call->Args[1], (ULONG)Call->Args[2]));
}

LX_SYSCALL(LxSysPkeyAlloc)
{
    if ((Call->Args[0] != 0) || (Call->Args[1] & ~(ULONG64)LX_PKEY_ACCESS_MASK))
        return -LX_EINVAL;
    return -LX_ENOSPC;
}

LX_SYSCALL(LxSysPkeyFree)
{
    UNREFERENCED_PARAMETER(Call);
    return -LX_EINVAL;
}

LX_SYSCALL(LxSysPkeyMprotect)
{
    if ((LONG)Call->Args[3] != -1)
    {
        if (Call->Args[0] & (LX_PAGE_SIZE - 1))
            return -LX_EINVAL;
        if (Call->Args[1] == 0)
            return 0;
        if (Call->Args[0] + LX_ALIGN_UP(Call->Args[1], LX_PAGE_SIZE) <= Call->Args[0])
            return -LX_ENOMEM;
        return -LX_EINVAL;
    }
    return LxMmProtect(Call->Process, Call->Args[0], Call->Args[1],
                       LxReadImpliesExec(Call->Process, Call->Args[0], Call->Args[1], (ULONG)Call->Args[2]));
}

LONG
LxMmPrepareRemote(
    _In_ PLX_PROCESS Process,
    _In_ PEPROCESS EProcess,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ BOOLEAN Write)
{
    KAPC_STATE ApcState;
    LONG Result;

    Process = Process->Mm;
    KeStackAttachProcess((PKPROCESS)EProcess, &ApcState);
    LxMmLock(Process);
    Result = LxMmPopulate(Process, LX_ALIGN_DOWN(Address, LX_PAGE_SIZE), LX_ALIGN_UP(Address + Length, LX_PAGE_SIZE),
                          Write ? LX_PROT_WRITE : LX_PROT_READ, FALSE);
    LxMmUnlock(Process);
    KeUnstackDetachProcess(&ApcState);
    return (Result == -LX_ENXIO) ? -LX_EFAULT : ((Result < 0) ? Result : 0);
}

LONG
LxMmForceAccess(
    _In_ PLX_PROCESS Process,
    _In_ PEPROCESS EProcess,
    _In_ ULONG64 Address,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN Write)
{
    MEMORY_BASIC_INFORMATION Information;
    KAPC_STATE ApcState;
    PUCHAR Data = Buffer;
    ULONG Done = 0;
    LONG Error = 0;

    Process = Process->Mm;
    KeStackAttachProcess((PKPROCESS)EProcess, &ApcState);
    LxMmLock(Process);
    while ((Error == 0) && (Done < Length))
    {
        ULONG64 Cursor = Address + Done, Page = LX_ALIGN_DOWN(Cursor, LX_PAGE_SIZE);
        ULONG Chunk = (ULONG)min((ULONG64)(Length - Done), Page + LX_PAGE_SIZE - Cursor);
        PLX_VMA Vma = LxVmaFind(Process, Cursor);
        BOOLEAN Cow, Changed = FALSE;

        if ((Vma == NULL) || (Write && (Vma->Flags & (LX_MAP_SHARED | LX_VMA_HUGETLB)) && !(Vma->Prot & LX_PROT_WRITE)) ||
            (LxMmPopulate(Process, Page, Page + LX_PAGE_SIZE, LX_PROT_READ, FALSE) < 0) ||
            !NT_SUCCESS(LxVmQuery(Page, &Information)))
        {
            Error = -LX_EIO;
            break;
        }
        Cow = LxVmaIsCow(Vma);
        if (Write && !LxNtAllows(Information.Protect, LX_PROT_WRITE))
        {
            if (Cow)
            {
                Vma->Flags |= LX_VMA_DIRTY;
                LxMmViewAllow(Process, Vma, PAGE_WRITECOPY);
            }
            Changed = NT_SUCCESS(LxVmProtectRange(Page, Page + LX_PAGE_SIZE, Cow ? PAGE_WRITECOPY : PAGE_READWRITE));
            Error = Changed ? 0 : -LX_EIO;
        }
        else if (!Write && !LxNtAllows(Information.Protect, LX_PROT_READ))
        {
            Changed = NT_SUCCESS(LxVmProtectRange(Page, Page + LX_PAGE_SIZE, PAGE_READONLY));
            Error = Changed ? 0 : -LX_EIO;
        }
        if (Error == 0)
        {
            _SEH2_TRY
            {
                if (Write)
                    RtlCopyMemory((PVOID)(ULONG_PTR)Cursor, Data + Done, Chunk);
                else
                    RtlCopyMemory(Data + Done, (PVOID)(ULONG_PTR)Cursor, Chunk);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Error = -LX_EIO;
            }
            _SEH2_END;
        }
        if (Changed)
            LxVmProtectRange(Page, Page + LX_PAGE_SIZE, LxVmaNtProtect(Vma));
        if (Write && (Error == 0) && (Vma->Prot & LX_PROT_EXEC))
            LxVmFlushCode(Page, LX_PAGE_SIZE);
        Done += Chunk;
    }
    LxMmUnlock(Process);
    KeUnstackDetachProcess(&ApcState);
    return Error;
}

static
BOOLEAN
LxMadviseKnown(
    _In_ ULONG Advice)
{
    return (Advice <= 4) || ((Advice >= 8) && (Advice <= 25)) || (Advice == LX_MADV_HWPOISON) ||
           (Advice == LX_MADV_SOFT_OFFLINE) || (Advice == LX_MADV_GUARD_INSTALL) || (Advice == LX_MADV_GUARD_REMOVE);
}

static
LONG
LxMadviseCheck(
    _In_ PLX_VMA Vma,
    _In_ ULONG64 Address,
    _In_ ULONG Advice)
{
    BOOLEAN Locked = (Vma->Flags & LX_VMA_LOCKED) != 0;
    BOOLEAN PrivateAnon = !(Vma->Flags & LX_MAP_SHARED) && (Vma->File == NULL) && !(Vma->Flags & LX_VMA_SHM);

    if (Vma->Flags & LX_VMA_HUGETLB)
    {
        switch (Advice)
        {
            case LX_MADV_DONTNEED:
            case LX_MADV_DONTNEED_LOCKED:
                return (max(Vma->Start, Address) & (LxHugePageSize() - 1)) ? -LX_EINVAL : 0;
            case LX_MADV_REMOVE:
                if (Locked)
                    return -LX_EINVAL;
                return (!(Vma->Flags & LX_MAP_SHARED) || (Vma->Flags & LX_VMA_NOMAYWRITE)) ? -LX_EACCES : 0;
            case LX_MADV_POPULATE_READ:
            case LX_MADV_POPULATE_WRITE:
            case LX_MADV_NORMAL:
            case LX_MADV_RANDOM:
            case LX_MADV_SEQUENTIAL:
            case LX_MADV_WILLNEED:
            case LX_MADV_DONTDUMP:
            case LX_MADV_DODUMP:
            case LX_MADV_DONTFORK:
            case LX_MADV_DOFORK:
                return 0;
            default:
                return -LX_EINVAL;
        }
    }

    switch (Advice)
    {
        case LX_MADV_DONTNEED:
        case LX_MADV_COLD:
        case LX_MADV_PAGEOUT:
            return Locked ? -LX_EINVAL : 0;
        case LX_MADV_FREE:
            return (Locked || !PrivateAnon) ? -LX_EINVAL : 0;
        case LX_MADV_REMOVE:
            if (Locked || !(Vma->Flags & LX_MAP_SHARED))
                return -LX_EINVAL;
            return (Vma->Flags & LX_VMA_NOMAYWRITE) ? -LX_EACCES : 0;
        case LX_MADV_WIPEONFORK:
            return PrivateAnon ? 0 : -LX_EINVAL;
        case LX_MADV_GUARD_INSTALL:
        case LX_MADV_GUARD_REMOVE:
            return (PrivateAnon && !Locked) ? 0 : -LX_EINVAL;
        default:
            return 0;
    }
}

static
LONG
LxMadviseRemove(
    _In_ PLX_VMA Vma,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    FILE_POSITION_INFORMATION Saved;
    IO_STATUS_BLOCK Iosb;
    LARGE_INTEGER Offset;
    PUCHAR Zero;
    ULONG64 Cursor;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!(Vma->Flags & LX_VMA_HUGETLB) && (LxTmpfsFileInode(Vma->File) != NULL))
    {
        ULONG64 First = Vma->Offset + (Start - Vma->Start);

        return LxTmpfsInodeRemoveRange(LxTmpfsFileInode(Vma->File), First, First + (End - Start));
    }
    if (!(Vma->Flags & LX_VMA_HUGETLB) && (Vma->File != NULL) && (Vma->File->Handle != NULL))
    {
        Zero = LxAllocPaged(LX_IO_CHUNK);
        if (Zero == NULL)
            return -LX_ENOMEM;
        RtlZeroMemory(Zero, LX_IO_CHUNK);
        if (!NT_SUCCESS(ZwQueryInformationFile(Vma->File->Handle, &Iosb, &Saved, sizeof(Saved),
                                               FilePositionInformation)))
        {
            Saved.CurrentByteOffset.QuadPart = -1;
        }
        for (Cursor = Start; NT_SUCCESS(Status) && (Cursor < End); Cursor += LX_IO_CHUNK)
        {
            Offset.QuadPart = (LONG64)(Vma->Offset + (Cursor - Vma->Start));
            Status = ZwWriteFile(Vma->File->Handle, NULL, NULL, NULL, &Iosb, Zero,
                                 (ULONG)min(End - Cursor, LX_IO_CHUNK), &Offset, NULL);
        }
        if (Saved.CurrentByteOffset.QuadPart >= 0)
            ZwSetInformationFile(Vma->File->Handle, &Iosb, &Saved, sizeof(Saved), FilePositionInformation);
        LxFreePaged(Zero);
        return NT_SUCCESS(Status) ? 0 : -LX_EIO;
    }

    if (!NT_SUCCESS(LxVmProtect(Start, End - Start, PAGE_READWRITE)))
        return -LX_EINVAL;
    _SEH2_TRY
    {
        RtlZeroMemory((PVOID)(ULONG_PTR)Start, (SIZE_T)(End - Start));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    LxVmProtect(Start, End - Start, LxVmaNtProtect(Vma));
    return NT_SUCCESS(Status) ? 0 : -LX_EFAULT;
}

LX_SYSCALL(LxSysMadvise)
{
    PLX_PROCESS Process = Call->Process->Mm;
    ULONG64 Address = Call->Args[0];
    ULONG64 End = Address + LX_ALIGN_UP(Call->Args[1], LX_PAGE_SIZE);
    ULONG Advice = (ULONG)Call->Args[2];
    PLIST_ENTRY Entry;
    LONG64 Result = 0;

    if (!LxMadviseKnown(Advice) || (Advice == LX_MADV_MERGEABLE) || (Advice == LX_MADV_UNMERGEABLE))
        return -LX_EINVAL;
    if ((Address & (LX_PAGE_SIZE - 1)) || (End < Address) || ((Call->Args[1] != 0) && (End == Address)))
        return -LX_EINVAL;
    if (((Advice == LX_MADV_HWPOISON) || (Advice == LX_MADV_SOFT_OFFLINE)) &&
        !LxCapable(Call->Process, LX_CAP_SYS_ADMIN))
    {
        return -LX_EPERM;
    }
    if (End == Address)
        return 0;

    LxMmLock(Process);
    for (Entry = Process->Vmas.Flink; (Result == 0) && (Entry != &Process->Vmas); Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Vma->Start >= End)
            break;
        if (Vma->End > Address)
            Result = LxMadviseCheck(Vma, Address, Advice);
    }
    if ((Result == 0) && !LxVmaCovers(Process, Address, End))
        Result = -LX_ENOMEM;
    if (Result != 0)
    {
        LxMmUnlock(Process);
        return Result;
    }

    switch (Advice)
    {
        case LX_MADV_DONTNEED:
        case LX_MADV_DONTNEED_LOCKED:
        case LX_MADV_FREE:
            for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
            {
                PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

                if (Vma->Start >= End)
                    break;
                if ((Vma->End <= Address) || (Vma->Flags & LX_MAP_SHARED))
                    continue;
                if (Vma->Flags & LX_VMA_HUGETLB)
                {
                    ULONG64 From = max(Vma->Start, Address);
                    ULONG64 To = LX_ALIGN_DOWN(min(Vma->End, End), LxHugePageSize());

                    if (To > From)
                        LxMadviseRemove(Vma, From, To);
                    continue;
                }
                if (LxVmaIsCow(Vma))
                {
                    ULONG64 From = max(Vma->Start, Address), To = min(Vma->End, End);

                    if (Vma->ViewBase != 0)
                        To = min(To, LxVmAllocationEnd(Vma->ViewBase));
                    if ((Vma->Flags & LX_VMA_DIRTY) && (Vma->ViewBase != 0) && (From < To))
                    {
                        LxVmProtect(From, To - From, PAGE_WRITECOPY);
                        LxMmFill(From, To - From, Vma->File, Vma->Offset + (From - Vma->Start));
                        LxVmProtect(From, To - From, LxCowProtToNt(Vma->Prot));
                    }
                    continue;
                }
                LxVmDecommitRange(max(Vma->Start, Address), min(Vma->End, End));
                if ((Vma->File != NULL) && !(Vma->Flags & LX_VMA_VIEW))
                    LxMmPopulateRun(Vma, max(Vma->Start, Address), min(Vma->End, End));
            }
            break;

        case LX_MADV_REMOVE:
            for (Entry = Process->Vmas.Flink; (Result == 0) && (Entry != &Process->Vmas); Entry = Entry->Flink)
            {
                PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

                if (Vma->Start >= End)
                    break;
                if (Vma->End > Address)
                    Result = LxMadviseRemove(Vma, max(Vma->Start, Address), min(Vma->End, End));
            }
            break;

        case LX_MADV_WIPEONFORK:
        case LX_MADV_KEEPONFORK:
            Result = LxVmaSetFlags(Process, Address, End, LX_VMA_WIPEONFORK,
                                   (Advice == LX_MADV_WIPEONFORK) ? LX_VMA_WIPEONFORK : 0);
            break;

        case LX_MADV_GUARD_INSTALL:
            LxVmDecommitRange(Address, End);
            Result = LxMmGuardAdd(Process, Address, End);
            break;

        case LX_MADV_GUARD_REMOVE:
            LxMmGuardRemove(Process, Address, End);
            break;

        case LX_MADV_DONTDUMP:
        case LX_MADV_DODUMP:
            Result = LxVmaSetFlags(Process, Address, End, LX_VMA_DONTDUMP,
                                   (Advice == LX_MADV_DONTDUMP) ? LX_VMA_DONTDUMP : 0);
            break;

        case LX_MADV_DONTFORK:
        case LX_MADV_DOFORK:
            Result = LxVmaSetFlags(Process, Address, End, LX_VMA_DONTFORK,
                                   (Advice == LX_MADV_DONTFORK) ? LX_VMA_DONTFORK : 0);
            break;

        case LX_MADV_POPULATE_READ:
        case LX_MADV_POPULATE_WRITE:
            if (LxMmPopulate(Process, Address, End,
                             (Advice == LX_MADV_POPULATE_WRITE) ? LX_PROT_WRITE : LX_PROT_READ, FALSE) < 0)
            {
                Result = -LX_EFAULT;
            }
            break;

        case LX_MADV_HWPOISON:
            Result = LxVmaSetFlags(Process, Address, End, LX_VMA_POISON, LX_VMA_POISON);
            for (Entry = Process->Vmas.Flink; (Result == 0) && (Entry != &Process->Vmas); Entry = Entry->Flink)
            {
                PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

                if (Vma->Start >= End)
                    break;
                if (Vma->End <= Address)
                    continue;
                if (Vma->Flags & LX_VMA_VIEW)
                    LxVmProtect(Vma->Start, Vma->End - Vma->Start, PAGE_NOACCESS);
                else
                    LxVmDecommitRange(Vma->Start, Vma->End);
            }
            break;

        default:
            break;
    }
    LxMmUnlock(Process);
    return Result;
}

LX_SYSCALL(LxSysRemapFilePages)
{
    PLX_PROCESS Process = Call->Process->Mm;
    ULONG64 Start = LX_ALIGN_DOWN(Call->Args[0], LX_PAGE_SIZE);
    ULONG64 Size = LX_ALIGN_DOWN(Call->Args[1], LX_PAGE_SIZE);
    ULONG64 PageOffset = Call->Args[3];
    ULONG Flags = LX_MAP_SHARED | LX_MAP_FIXED | LX_MAP_POPULATE | ((ULONG)Call->Args[4] & LX_MAP_NONBLOCK);
    PLIST_ENTRY Entry;
    PLX_VMA Vma, Last;
    PLX_FILE File;
    ULONG Prot;
    LONG64 Result;

    if ((Call->Args[2] != 0) || (Start + Size <= Start) || (PageOffset + (Size / LX_PAGE_SIZE) < PageOffset) ||
        (PageOffset > (MAXULONG64 / LX_PAGE_SIZE)))
    {
        return -LX_EINVAL;
    }

    LxMmLock(Process);
    Vma = LxVmaFind(Process, Start);
    if ((Vma == NULL) || !(Vma->Flags & LX_MAP_SHARED) || (Vma->File == NULL))
    {
        LxMmUnlock(Process);
        return -LX_EINVAL;
    }
    for (Last = Vma, Entry = Vma->Link.Flink; Start + Size > Last->End; Entry = Entry->Flink)
    {
        PLX_VMA Next;

        if (Entry == &Process->Vmas)
        {
            LxMmUnlock(Process);
            return -LX_EINVAL;
        }
        Next = CONTAINING_RECORD(Entry, LX_VMA, Link);
        if ((Next->Start != Last->End) || (Next->File != Vma->File) || (Next->Flags != Vma->Flags))
        {
            LxMmUnlock(Process);
            return -LX_EINVAL;
        }
        Last = Next;
    }
    Prot = Vma->Prot;
    File = Vma->File;
    if (Vma->Flags & LX_VMA_LOCKED)
        Flags |= LX_MAP_LOCKED;
    LxFileReference(File);
    Result = LxMmMap(Call->Process, Start, Size, Prot, Flags, File, PageOffset * LX_PAGE_SIZE);
    LxFileDereference(File);
    LxMmUnlock(Process);
    return (Result < 0) ? Result : 0;
}

LX_SYSCALL(LxSysMsync)
{
    PLX_PROCESS Process = Call->Process->Mm;
    ULONG64 Address = Call->Args[0];
    ULONG64 End = Address + LX_ALIGN_UP(Call->Args[1], LX_PAGE_SIZE);
    ULONG Flags = (ULONG)Call->Args[2];
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor;
    LONG64 Result = 0;

    if ((Address & (LX_PAGE_SIZE - 1)) || (End < Address) ||
        (Flags & ~(LX_MS_ASYNC | LX_MS_INVALIDATE | LX_MS_SYNC)) ||
        ((Flags & LX_MS_ASYNC) && (Flags & LX_MS_SYNC)))
    {
        return -LX_EINVAL;
    }
    if (End == Address)
        return 0;

    LxMmLock(Process);
    if ((Flags & LX_MS_INVALIDATE) && (LxMmLockedIn(Process, Address, End) != 0))
        Result = -LX_EBUSY;
    else if (!LxVmaCovers(Process, Address, End))
        Result = -LX_ENOMEM;
    for (Cursor = Address; (Result == 0) && (Flags & LX_MS_SYNC) && (Cursor < End);)
    {
        ULONG64 RegionEnd;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            break;
        RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, End);
        if ((Information.State == MEM_COMMIT) && (Information.Type == MEM_MAPPED))
        {
            PVOID Base = (PVOID)(ULONG_PTR)Cursor;
            SIZE_T Size = (SIZE_T)(RegionEnd - Cursor);
            IO_STATUS_BLOCK Iosb;

            if (!NT_SUCCESS(ZwFlushVirtualMemory(NtCurrentProcess(), &Base, &Size, &Iosb)))
                Result = -LX_EIO;
        }
        Cursor = RegionEnd;
    }
    LxMmUnlock(Process);
    return Result;
}

typedef struct _LX_WS_EX_ENTRY
{
    PVOID VirtualAddress;
    ULONG_PTR Attributes;
} LX_WS_EX_ENTRY;

static
VOID
LxMmResidentRun(
    _In_ HANDLE ProcessHandle,
    _In_ PLX_VMA Vma,
    _In_ ULONG64 Start,
    _In_ ULONG Pages,
    _Out_writes_(Pages) PUCHAR Vector)
{
    LX_WS_EX_ENTRY Entries[64];
    MEMORY_BASIC_INFORMATION Information;
    ULONG Index;

    if ((Vma->Flags & LX_MAP_SHARED) && (Vma->File == NULL) && (Pages <= RTL_NUMBER_OF(Entries)))
    {
        for (Index = 0; Index < Pages; Index++)
        {
            Entries[Index].VirtualAddress = (PVOID)(ULONG_PTR)(Start + Index * LX_PAGE_SIZE);
            Entries[Index].Attributes = 0;
        }
        if (NT_SUCCESS(ZwQueryVirtualMemory(ProcessHandle, NULL, MemoryWorkingSetExList, Entries,
                                            Pages * sizeof(Entries[0]), NULL)))
        {
            for (Index = 0; Index < Pages; Index++)
                Vector[Index] = (UCHAR)(Entries[Index].Attributes & 1);
            return;
        }
    }

    for (Index = 0; Index < Pages;)
    {
        ULONG64 Address = Start + Index * LX_PAGE_SIZE, RegionEnd = Address + LX_PAGE_SIZE;
        UCHAR Value = 0;

        if (NT_SUCCESS(ZwQueryVirtualMemory(ProcessHandle, (PVOID)(ULONG_PTR)Address, MemoryBasicInformation,
                                            &Information, sizeof(Information), NULL)))
        {
            Value = (Information.State == MEM_COMMIT) ? 1 : 0;
            RegionEnd = max(RegionEnd, (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize);
        }
        for (; (Index < Pages) && (Start + Index * LX_PAGE_SIZE < RegionEnd); Index++)
            Vector[Index] = Value;
    }
}

static
VOID
LxMmPinResident(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    UCHAR Vector[64];
    ULONG64 Address = Start;

    while (Address < End)
    {
        PLIST_ENTRY Entry = LxVmaEntryAbove(Process, Address);
        PLX_VMA Vma;
        ULONG Pages, Page, Run;

        if (Entry == &Process->Vmas)
            break;
        Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
        if (Vma->Start >= End)
            break;
        Address = max(Address, Vma->Start);
        if (!(Vma->Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC)))
        {
            Address = min(Vma->End, End);
            continue;
        }
        Pages = (ULONG)min((min(Vma->End, End) - Address) / LX_PAGE_SIZE, sizeof(Vector));
        LxMmResidentRun(NtCurrentProcess(), Vma, Address, Pages, Vector);
        for (Page = 0; Page < Pages; Page = Run)
        {
            for (Run = Page; (Run < Pages) && (Vector[Run] & 1); Run++)
                ;
            if (Run > Page)
                LxMmPin(Address + (ULONG64)Page * LX_PAGE_SIZE, Address + (ULONG64)Run * LX_PAGE_SIZE, TRUE);
            else
                Run++;
        }
        Address += (ULONG64)Pages * LX_PAGE_SIZE;
    }
}

static
LONG64
LxMmLockRange(
    _In_ PLX_PROCESS Caller,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Lock)
{
    PLX_PROCESS Process = Caller->Mm;
    ULONG64 Start = LX_ALIGN_DOWN(Address, LX_PAGE_SIZE);
    ULONG64 End = Start + LX_ALIGN_UP(Length + (Address - Start), LX_PAGE_SIZE);
    LONG Error;

    if ((Lock != 0) && !LxMmCanLock(Caller))
        return -LX_EPERM;
    if (End < Start)
        return -LX_EINVAL;
    if (End == Start)
        return 0;

    LxMmLock(Process);
    if (!LxVmaCovers(Process, Start, End))
        Error = -LX_ENOMEM;
    else if ((Lock != 0) && !LxMmLockAllowed(Caller, Process, (End - Start) - LxMmLockedIn(Process, Start, End)))
        Error = -LX_ENOMEM;
    else
    {
        if (Lock == 0)
            LxMmUnpinLocked(Process, Start, End);
        Error = LxVmaSetLock(Process, Start, End, Lock);
    }
    if ((Error == 0) && (Lock == LX_VMA_LOCKED))
        LxMmLockPopulate(Process, Start, End);
    else if ((Error == 0) && (Lock != 0))
        LxMmPinResident(Process, Start, End);
    LxMmUnlock(Process);
    return Error;
}

static
VOID
LxMmSetLockAll(
    _In_ PLX_PROCESS Process,
    _In_ ULONG Lock)
{
    PLIST_ENTRY Entry;

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        Vma->Flags = (Vma->Flags & ~LX_VMA_LOCK_MASK) | Lock;
    }
    LxVmaMerge(Process, 0, MAXULONG64);
}

LX_SYSCALL(LxSysMlock)
{
    return LxMmLockRange(Call->Process, Call->Args[0], Call->Args[1], LX_VMA_LOCKED);
}

LX_SYSCALL(LxSysMlock2)
{
    ULONG Flags = (ULONG)Call->Args[2];

    if (Flags & ~LX_MLOCK_ONFAULT)
        return -LX_EINVAL;
    return LxMmLockRange(Call->Process, Call->Args[0], Call->Args[1],
                         LX_VMA_LOCKED | ((Flags & LX_MLOCK_ONFAULT) ? LX_VMA_LOCKONFAULT : 0));
}

LX_SYSCALL(LxSysMunlock)
{
    return LxMmLockRange(Call->Process, Call->Args[0], Call->Args[1], 0);
}

LX_SYSCALL(LxSysMlockall)
{
    PLX_PROCESS Process = Call->Process->Mm;
    ULONG Flags = (ULONG)Call->Args[0];
    ULONG Lock = LX_VMA_LOCKED | ((Flags & LX_MCL_ONFAULT) ? LX_VMA_LOCKONFAULT : 0);
    PLIST_ENTRY Entry;
    ULONG64 Total = 0;

    if (!(Flags & (LX_MCL_CURRENT | LX_MCL_FUTURE)) || (Flags & ~(LX_MCL_CURRENT | LX_MCL_FUTURE | LX_MCL_ONFAULT)))
        return -LX_EINVAL;
    if (!LxMmCanLock(Call->Process))
        return -LX_EPERM;

    LxMmLock(Process);
    if (Flags & LX_MCL_CURRENT)
    {
        for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
        {
            PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

            Total += Vma->End - Vma->Start;
        }
        if (!LxMmLockAllowed(Call->Process, Process, Total - LxMmLockedIn(Process, 0, MAXULONG64)))
        {
            LxMmUnlock(Process);
            return -LX_ENOMEM;
        }
    }
    Process->MmLockDefault = (Flags & LX_MCL_FUTURE) ? Lock : 0;
    if (Flags & LX_MCL_CURRENT)
    {
        LxMmSetLockAll(Process, Lock);
        if (Lock == LX_VMA_LOCKED)
            LxMmLockPopulate(Process, 0, MAXULONG64);
        else
            LxMmPinResident(Process, 0, MAXULONG64);
    }
    LxMmUnlock(Process);
    return 0;
}

LX_SYSCALL(LxSysMunlockall)
{
    PLX_PROCESS Process = Call->Process->Mm;

    LxMmLock(Process);
    Process->MmLockDefault = 0;
    LxMmUnpinLocked(Process, 0, MAXULONG64);
    LxMmSetLockAll(Process, 0);
    LxMmUnlock(Process);
    return 0;
}

ULONG64
LxMmLockedBytes(
    _In_ PLX_PROCESS Process)
{
    ULONG64 Bytes;

    Process = Process->Mm;
    LxMmLock(Process);
    Bytes = LxMmLockedIn(Process, 0, MAXULONG64);
    LxMmUnlock(Process);
    return Bytes;
}

static
PVOID
LxMmHandleKey(
    _In_opt_ HANDLE Handle)
{
    PFILE_OBJECT FileObject;
    PVOID Key;

    if ((Handle == NULL) ||
        !NT_SUCCESS(ObReferenceObjectByHandle(Handle, 0, *IoFileObjectType, KernelMode, (PVOID *)&FileObject, NULL)))
    {
        return NULL;
    }
    Key = FileObject->SectionObjectPointer;
    ObDereferenceObject(FileObject);
    return Key;
}

static
ULONG
LxMmReleaseFileViews(
    _In_ PLX_PROCESS Process,
    _In_ PVOID Key)
{
    MEMORY_BASIC_INFORMATION Information;
    PLIST_ENTRY Entry, Other;
    ULONG Changed = 0;

    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);
        ULONG64 Base, ViewOffset, MapEnd;

        if (((Vma->Flags & (LX_VMA_VIEW | LX_MAP_SHARED)) != (LX_VMA_VIEW | LX_MAP_SHARED)) || (Vma->File == NULL) ||
            (((LxTmpfsFileInode(Vma->File) != NULL) ? (PVOID)LxTmpfsFileInode(Vma->File) :
                                                      LxMmHandleKey(Vma->File->Handle)) != Key))
        {
            continue;
        }
        if (!NT_SUCCESS(LxVmQuery(Vma->Start, &Information)) || (Information.Type != MEM_MAPPED) ||
            (Information.State == MEM_FREE))
        {
            continue;
        }
        Base = (ULONG64)(ULONG_PTR)Information.AllocationBase;
        ViewOffset = Vma->Offset - (Vma->Start - Base);

        MapEnd = Vma->End;
        for (Other = Process->Vmas.Flink; Other != &Process->Vmas; Other = Other->Flink)
        {
            PLX_VMA Piece = CONTAINING_RECORD(Other, LX_VMA, Link);

            if ((Piece->File == Vma->File) && (Piece->Flags & LX_VMA_VIEW) && (Piece->Start >= Base) &&
                (Piece->Offset - (Piece->Start - Base) == ViewOffset))
            {
                MapEnd = max(MapEnd, Piece->End);
                Piece->ViewBase = Base;
                Piece->ViewOffset = ViewOffset;
                Piece->MapEnd = max(Piece->MapEnd, Piece->End);
            }
        }

        ZwUnmapViewOfSection(NtCurrentProcess(), (PVOID)(ULONG_PTR)Base);
        LxVmAllocate(&Base, LX_ALIGN_UP(MapEnd, LX_ALLOC_GRANULARITY) - Base, MEM_RESERVE, PAGE_NOACCESS);
        Changed++;
    }
    return Changed;
}

ULONG
LxMmTruncateViews(
    _In_ HANDLE Handle)
{
    PVOID Key = LxMmHandleKey(Handle);

    return (Key != NULL) ? LxMmReleaseKeyedViews(Key, NULL, NULL) : 0;
}

ULONG
LxMmReleaseKeyedViews(
    _In_ PVOID Key,
    _In_opt_ VOID (*Visit)(_In_ PVOID Context, _In_ PLX_PROCESS Process),
    _In_opt_ PVOID Context)
{
    PLX_PROCESS *Processes;
    PLIST_ENTRY Entry;
    ULONG Count = 0, Capacity = 0, Index, Changed = 0;

    ExAcquireFastMutex(&LxProcessListLock);
    for (Entry = LxProcessList.Flink; Entry != &LxProcessList; Entry = Entry->Flink)
        Capacity++;
    Processes = (Capacity != 0) ? LxAlloc(Capacity * sizeof(*Processes)) : NULL;
    for (Entry = LxProcessList.Flink; (Processes != NULL) && (Entry != &LxProcessList); Entry = Entry->Flink)
    {
        PLX_PROCESS Process = CONTAINING_RECORD(Entry, LX_PROCESS, GlobalLink);

        if ((Process->Mm == Process) && !Process->Exited && (Process->NtPid != 0))
        {
            LxProcessReference(Process);
            Processes[Count++] = Process;
        }
    }
    ExReleaseFastMutex(&LxProcessListLock);

    for (Index = 0; Index < Count; Index++)
    {
        PLX_PROCESS Process = Processes[Index];
        PEPROCESS EProcess = NULL;
        KAPC_STATE ApcState;
        BOOLEAN Attached = FALSE;

        if (Process->NtPid != HandleToUlong(PsGetCurrentProcessId()))
        {
            if (!NT_SUCCESS(PsLookupProcessByProcessId(ULongToHandle(Process->NtPid), &EProcess)))
            {
                LxProcessDereference(Process);
                continue;
            }
            KeStackAttachProcess((PKPROCESS)EProcess, &ApcState);
            Attached = TRUE;
        }
        LxMmLock(Process);
        Changed += LxMmReleaseFileViews(Process, Key);
        if (Visit != NULL)
            Visit(Context, Process);
        LxMmUnlock(Process);
        if (Attached)
        {
            KeUnstackDetachProcess(&ApcState);
            ObDereferenceObject(EProcess);
        }
        LxProcessDereference(Process);
    }
    LxFree(Processes);
    return Changed;
}

LONG64
LxMmMincore(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG64 End,
    _In_ ULONG64 Output)
{
    UCHAR Vector[64];
    LONG64 Result = 0;

    if (Address == End)
        return 0;

    Process = Process->Mm;
    LxMmLock(Process);
    if (!LxVmaCovers(Process, Address, End))
        Result = -LX_ENOMEM;
    while ((Result == 0) && (Address < End))
    {
        PLX_VMA Vma = LxVmaFind(Process, Address);
        ULONG Pages = (ULONG)min((min(Vma->End, End) - Address) / LX_PAGE_SIZE, sizeof(Vector));

        LxMmResidentRun(NtCurrentProcess(), Vma, Address, Pages, Vector);
        Result = LxCopyToUser(Output, Vector, Pages);
        Output += Pages;
        Address += Pages * LX_PAGE_SIZE;
    }
    LxMmUnlock(Process);
    return Result;
}

VOID
LxMmPagemap(
    _In_ PLX_PROCESS Process,
    _In_ HANDLE ProcessHandle,
    _In_ ULONG64 Address,
    _In_ ULONG Pages,
    _In_ BOOLEAN Frames,
    _Out_writes_(Pages) PULONG64 Entries)
{
    UCHAR Vector[64];
    ULONG Index = 0, Run, Page;

    Process = Process->Mm;
    LxMmLock(Process);
    while (Index < Pages)
    {
        ULONG64 Va = Address + (ULONG64)Index * LX_PAGE_SIZE;
        PLIST_ENTRY Entry = LxVmaEntryAbove(Process, Va);
        PLX_VMA Vma = (Entry != &Process->Vmas) ? CONTAINING_RECORD(Entry, LX_VMA, Link) : NULL;

        if ((Vma == NULL) || (Vma->Start > Va))
        {
            Run = (Vma == NULL) ? (Pages - Index) : (ULONG)min(Pages - Index, (Vma->Start - Va) / LX_PAGE_SIZE);
            RtlZeroMemory(&Entries[Index], (SIZE_T)Run * sizeof(ULONG64));
            Index += Run;
            continue;
        }
        Run = (ULONG)min(min(Pages - Index, (Vma->End - Va) / LX_PAGE_SIZE), sizeof(Vector));
        LxMmResidentRun(ProcessHandle, Vma, Va, Run, Vector);
        for (Page = 0; Page < Run; Page++)
        {
            ULONG64 Value = LX_PM_SOFT_DIRTY;

            if (Vector[Page])
            {
                Value |= LX_PM_PRESENT;
                if ((Vma->File != NULL) || (Vma->Flags & LX_MAP_SHARED))
                    Value |= LX_PM_FILE;
                if (Frames)
                    Value |= (ULONG64)MmGetPhysicalAddress((PVOID)(ULONG_PTR)(Va + (ULONG64)Page * LX_PAGE_SIZE)).QuadPart >>
                             PAGE_SHIFT;
            }
            Entries[Index + Page] = Value;
        }
        Index += Run;
    }
    LxMmUnlock(Process);
}

ULONG64
LxMmVmaResident(
    _In_ HANDLE ProcessHandle,
    _In_ PLX_VMA Vma)
{
    UCHAR Vector[64];
    ULONG64 Address, Bytes = 0;
    ULONG Pages, Index;

    for (Address = Vma->Start; Address < Vma->End; Address += Pages * LX_PAGE_SIZE)
    {
        Pages = (ULONG)min((Vma->End - Address) / LX_PAGE_SIZE, sizeof(Vector));
        LxMmResidentRun(ProcessHandle, Vma, Address, Pages, Vector);
        for (Index = 0; Index < Pages; Index++)
            Bytes += Vector[Index] ? LX_PAGE_SIZE : 0;
    }
    return Bytes;
}

LX_SYSCALL(LxSysMremap)
{
    PLX_PROCESS Process = Call->Process->Mm;
    ULONG64 Old = Call->Args[0];
    ULONG64 OldLength = LX_ALIGN_UP(Call->Args[1], LX_PAGE_SIZE);
    ULONG64 NewLength = LX_ALIGN_UP(Call->Args[2], LX_PAGE_SIZE);
    ULONG Flags = (ULONG)Call->Args[3];
    ULONG64 Fixed = (Flags & LX_MREMAP_FIXED) ? Call->Args[4] : 0;
    BOOLEAN KeepOld = (Flags & LX_MREMAP_DONTUNMAP) != 0;
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 New = 0, Cursor, Offset;
    ULONG Prot, VmaFlags, Lock;
    PLX_FILE File;
    PLX_VMA Vma;
    LONG64 Result;

    if (Flags & ~(LX_MREMAP_MAYMOVE | LX_MREMAP_FIXED | LX_MREMAP_DONTUNMAP))
        return -LX_EINVAL;
    if ((Flags & (LX_MREMAP_FIXED | LX_MREMAP_DONTUNMAP)) && !(Flags & LX_MREMAP_MAYMOVE))
        return -LX_EINVAL;
    if (KeepOld && (OldLength != NewLength))
        return -LX_EINVAL;
    if ((Old & (LX_PAGE_SIZE - 1)) || (OldLength == 0) || (NewLength == 0))
        return -LX_EINVAL;
    if ((Old + OldLength < Old) || (Old + NewLength < Old))
        return -LX_EINVAL;
    if (Flags & LX_MREMAP_FIXED)
    {
        if ((Fixed & (LX_PAGE_SIZE - 1)) || (Fixed + NewLength < Fixed) ||
            ((Fixed < Old + OldLength) && (Old < Fixed + NewLength)))
        {
            return -LX_EINVAL;
        }
    }

    LxMmLock(Process);
    Vma = LxVmaFind(Process, Old);
    if ((Vma == NULL) || (Old + OldLength > Vma->End))
    {
        LxMmUnlock(Process);
        return -LX_EFAULT;
    }
    if (Vma->Flags & LX_VMA_HUGETLB)
    {
        LxMmUnlock(Process);
        return -LX_EINVAL;
    }

    if ((Flags & LX_MREMAP_FIXED) && (NewLength < OldLength))
    {
        LxMmUnmapRange(Process, Old + NewLength, Old + OldLength);
        OldLength = NewLength;
        Vma = LxVmaFind(Process, Old);
    }
    else if ((NewLength <= OldLength) && !(Flags & (LX_MREMAP_FIXED | LX_MREMAP_DONTUNMAP)))
    {
        if (NewLength < OldLength)
            LxMmUnmap(Process, Old + NewLength, OldLength - NewLength);
        LxMmUnlock(Process);
        return (LONG64)Old;
    }

    Lock = Vma->Flags & LX_VMA_LOCK_MASK;
    if ((Lock != 0) && (NewLength > OldLength) && !LxMmLockAllowed(Call->Process, Process, NewLength - OldLength))
    {
        LxMmUnlock(Process);
        return -LX_EAGAIN;
    }
    if (Flags & LX_MREMAP_FIXED)
    {
        Result = LxMmUnmapRange(Process, Fixed, Fixed + NewLength);
        if (Result != 0)
        {
            LxMmUnlock(Process);
            return Result;
        }
        Vma = LxVmaFind(Process, Old);
    }

    if ((Vma->Flags & (LX_VMA_VIEW | LX_MAP_SHARED)) == (LX_VMA_VIEW | LX_MAP_SHARED))
    {
        if (!(Flags & LX_MREMAP_MAYMOVE) || (Vma->File == NULL))
        {
            LxMmUnlock(Process);
            return -LX_ENOMEM;
        }
        Prot = Vma->Prot;
        File = Vma->File;
        Offset = Vma->Offset + (Old - Vma->Start);
        LxFileReference(File);
        Result = LxMmMap(Process, Fixed, NewLength, Prot,
                         LX_MAP_SHARED | (Fixed ? LX_MAP_FIXED : 0) | (Vma->Flags & LX_VMA_NOMAYWRITE), File, Offset);
        if ((Result >= 0) && !KeepOld)
            LxMmUnmap(Process, Old, OldLength);
        if ((Result >= 0) && (Lock != 0))
            LxVmaSetLock(Process, (ULONG64)Result, (ULONG64)Result + NewLength, Lock);
        if ((Result >= 0) && (Lock == LX_VMA_LOCKED))
            LxMmLockPopulate(Process, (ULONG64)Result, (ULONG64)Result + NewLength);
        LxFileDereference(File);
        LxMmUnlock(Process);
        return Result;
    }

    if (LxVmaIsCow(Vma))
    {
        LIST_ENTRY Saved;
        BOOLEAN Dirty = (Vma->Flags & LX_VMA_DIRTY) != 0;

        if (!(Flags & LX_MREMAP_MAYMOVE))
        {
            LxMmUnlock(Process);
            return -LX_ENOMEM;
        }
        Prot = Vma->Prot;
        File = Vma->File;
        Offset = Vma->Offset + (Old - Vma->Start);
        LxFileReference(File);
        InitializeListHead(&Saved);
        Result = Dirty ? LxMmCopySave(&Saved, Old, min(Old + OldLength, Vma->End), Prot, LxCowProtToNt(Prot)) : 0;
        if (Result == 0)
            Result = LxMmMap(Process, Fixed, NewLength, Prot, LX_MAP_PRIVATE | (Fixed ? LX_MAP_FIXED : 0), File, Offset);
        if ((Result >= 0) && !IsListEmpty(&Saved))
        {
            PLX_MM_COPY Copy = CONTAINING_RECORD(Saved.Flink, LX_MM_COPY, Link);
            PLX_VMA Moved = LxVmaFind(Process, (ULONG64)Result);

            Copy->Start = (ULONG64)Result;
            Copy->End = (ULONG64)Result + OldLength;
            if ((Moved != NULL) && LxVmaIsCow(Moved))
            {
                Moved->Flags |= LX_VMA_DIRTY;
                LxMmViewAllow(Process, Moved, PAGE_WRITECOPY);
            }
            LxMmCopyRestore(&Saved, (Moved == NULL) || !LxVmaIsCow(Moved));
        }
        while (!IsListEmpty(&Saved))
        {
            PLX_MM_COPY Copy = CONTAINING_RECORD(RemoveHeadList(&Saved), LX_MM_COPY, Link);

            LxFreePaged(Copy->Data);
            LxFree(Copy);
        }
        if (Result >= 0)
        {
            if (!KeepOld)
                LxMmUnmap(Process, Old, OldLength);
            if (Lock != 0)
                LxVmaSetLock(Process, (ULONG64)Result, (ULONG64)Result + NewLength, Lock);
            if (Lock == LX_VMA_LOCKED)
                LxMmLockPopulate(Process, (ULONG64)Result, (ULONG64)Result + NewLength);
        }
        LxFileDereference(File);
        LxMmUnlock(Process);
        return Result;
    }
    if (Vma->Flags & LX_VMA_VIEW)
    {
        LxMmUnlock(Process);
        return -LX_ENOMEM;
    }

    Prot = Vma->Prot;
    VmaFlags = Vma->Flags;
    File = Vma->File;
    Offset = (File != NULL) ? Vma->Offset + (Old - Vma->Start) : 0;
    if (File != NULL)
        LxFileReference(File);

    if (!(Flags & (LX_MREMAP_FIXED | LX_MREMAP_DONTUNMAP)) &&
        !LxVmaOverlaps(Process, Old + OldLength, Old + NewLength) &&
        (LxVmPlace(Process, Old + OldLength, Old + NewLength, LX_PLACE_FRESH) == 0))
    {
        Result = LxVmaInsert(Process, Old + OldLength, Old + NewLength, Prot, VmaFlags, File,
                             (File != NULL) ? Offset + OldLength : 0, NULL);
        if ((Result == 0) && (File != NULL))
            Result = LxMmFillPrivateFile(Process, Old + OldLength, Old + NewLength);
        if (Result == 0)
            Result = (LONG64)Old;
    }
    else if (!(Flags & LX_MREMAP_MAYMOVE))
    {
        Result = -LX_ENOMEM;
    }
    else if (Fixed ? (LxVmPlace(Process, Fixed, Fixed + NewLength, LX_PLACE_FRESH | LX_PLACE_SPLIT) != 0) :
                     !NT_SUCCESS(LxVmReserveUnmapped(Process, &New, LX_ALIGN_UP(NewLength, LX_ALLOC_GRANULARITY))))
    {
        Result = -LX_ENOMEM;
    }
    else
    {
        if (Fixed)
            New = Fixed;
        Result = LxVmaInsert(Process, New, New + NewLength, Prot, VmaFlags, File, Offset, NULL);
        if ((Result != 0) && !Fixed)
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
                if ((Information.Protect & 0xFF) == PAGE_NOACCESS)
                    LxVmProtect(Cursor, RegionEnd - Cursor, PAGE_NOACCESS);
                if (Prot & LX_PROT_EXEC)
                    LxVmFlushCode(Target, RegionEnd - Cursor);
            }
            Cursor = RegionEnd;
        }

        if ((Result == 0) && (File != NULL))
            Result = LxMmFillPrivateFile(Process, New + OldLength, New + NewLength);
        if (Result == 0)
        {
            if (!KeepOld)
                LxMmUnmap(Process, Old, OldLength);
            else if (File == NULL)
                LxVmDecommitRange(Old, Old + OldLength);
            Result = (LONG64)New;
        }
        else if (New != 0)
        {
            LxMmUnmap(Process, New, NewLength);
        }
    }

    if ((Result >= 0) && (Lock == LX_VMA_LOCKED))
        LxMmLockPopulate(Process, (ULONG64)Result + OldLength, (ULONG64)Result + NewLength);
    if (File != NULL)
        LxFileDereference(File);
    LxMmUnlock(Process);
    return Result;
}

BOOLEAN
LxMmHasVmaAbove(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address)
{
    PLIST_ENTRY Entry;
    BOOLEAN Found = FALSE;

    Process = Process->Mm;
    LxMmLock(Process);
    for (Entry = Process->Vmas.Flink; (Entry != &Process->Vmas) && !Found; Entry = Entry->Flink)
        Found = CONTAINING_RECORD(Entry, LX_VMA, Link)->End > Address;
    LxMmUnlock(Process);
    return Found;
}

BOOLEAN
LxMmMapsPath(
    _In_ PLX_PROCESS Process,
    _In_opt_ PCSTR Path)
{
    PLIST_ENTRY Entry;
    BOOLEAN Found = FALSE;

    Process = Process->Mm;
    if (Path == NULL)
        return FALSE;
    LxMmLock(Process);
    for (Entry = Process->Vmas.Flink; (Entry != &Process->Vmas) && !Found; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        Found = (Vma->File != NULL) && (Vma->File->Path != NULL) && (strcmp(Vma->File->Path, Path) == 0);
    }
    LxMmUnlock(Process);
    return Found;
}

PLX_FILE
LxMmFileAt(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    PLIST_ENTRY Entry;
    PLX_FILE File = NULL;

    Process = Process->Mm;
    LxMmLock(Process);
    for (Entry = Process->Vmas.Flink; Entry != &Process->Vmas; Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if ((Vma->Start == Start) && (Vma->End == End) && (Vma->File != NULL))
        {
            File = Vma->File;
            LxFileReference(File);
            break;
        }
    }
    LxMmUnlock(Process);
    return File;
}

ULONG
LxMmFileRanges(
    _In_ PLX_PROCESS Process,
    _Out_writes_(Max * 2) PULONG64 Ranges,
    _In_ ULONG Max)
{
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    Process = Process->Mm;
    LxMmLock(Process);
    for (Entry = Process->Vmas.Flink; (Entry != &Process->Vmas) && (Count < Max); Entry = Entry->Flink)
    {
        PLX_VMA Vma = CONTAINING_RECORD(Entry, LX_VMA, Link);

        if (Vma->File == NULL)
            continue;
        Ranges[Count * 2] = Vma->Start;
        Ranges[Count * 2 + 1] = Vma->End;
        Count++;
    }
    LxMmUnlock(Process);
    return Count;
}

VOID
LxMmSetBrk(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 StartBrk,
    _In_ ULONG64 Brk)
{
    ULONG64 Base;

    Process = Process->Mm;
    LxMmLock(Process);
    if ((StartBrk != Process->BrkBase) || (Brk != Process->BrkCurrent))
    {
        Process->BrkBase = StartBrk;
        Process->BrkCurrent = Brk;
        Process->BrkCommitted = LX_ALIGN_UP(Brk, LX_PAGE_SIZE);
        Process->BrkLimit = Process->BrkCommitted;
        Process->BrkReserved = TRUE;
        Base = Process->BrkCommitted;
        if (NT_SUCCESS(LxVmAllocate(&Base, LX_BRK_RESERVE, MEM_RESERVE, PAGE_NOACCESS)) && (Base == Process->BrkCommitted))
            Process->BrkLimit = Base + LX_BRK_RESERVE;
    }
    LxMmUnlock(Process);
}

LX_SYSCALL(LxSysBrk)
{
    PLX_PROCESS Process = Call->Process->Mm;
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
            ((Process->MmLockDefault != 0) &&
             !LxMmLockAllowed(Call->Process, Process, NewCommitted - Process->BrkCommitted)) ||
            (LxVmaInsert(Process, Process->BrkCommitted, NewCommitted, LX_PROT_READ | LX_PROT_WRITE,
                         LX_MAP_PRIVATE | LX_VMA_HEAP | Process->MmLockDefault, NULL, 0, NULL) != 0))
        {
            LxMmUnlock(Process);
            return (LONG64)Result;
        }
        if (Process->MmLockDefault == LX_VMA_LOCKED)
            LxMmLockPopulate(Process, Process->BrkCommitted, NewCommitted);
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
