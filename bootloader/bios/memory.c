/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PC/AT BIOS memory map and page allocator
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <bios.h>

#define BIOS_E820_SIGNATURE     0x534D4150
#define BIOS_E820_ENTRY_SIZE    24
#define BIOS_E820_RAM           1
#define BIOS_E820_RESERVED      2
#define BIOS_E820_ACPI          3
#define BIOS_E820_NVS           4
#define BIOS_E820_BAD           5
#define BIOS_E820_ENABLED       0x1
#define BIOS_HEAP_CHUNK         0x100000
#define BIOS_HEAP_SPLIT         64
#define BIOS_MAX_USABLE         (BIOS_MAX_MEMORY_RANGES * 2)
#define BIOS_MAX_DESCRIPTORS    (BIOS_MAX_ALLOCATIONS + 4 * BIOS_MAX_MEMORY_RANGES + 16)

typedef struct _BIOS_PAGE_RUN
{
    PFN_NUMBER BasePage;
    PFN_NUMBER PageCount;
} BIOS_PAGE_RUN, *PBIOS_PAGE_RUN;

static BIOS_PAGE_RUN BiosUsable[BIOS_MAX_USABLE];
static ULONG BiosUsableCount;
static FREELDR_MEMORY_DESCRIPTOR BiosMap[BIOS_MAX_DESCRIPTORS + 1];
static ULONG BiosMapCount;

static
VOID
BiosReadE820(VOID)
{
    PUCHAR Entry = (PUCHAR)(ULONG_PTR)BIOS_TRANSFER_BASE;
    BIOS_REGISTERS Registers;
    PBIOS_MEMORY_RANGE Range;
    ULONG Continuation = 0;

    BiosShared->RangeCount = 0;

    do
    {
        RtlZeroMemory(&Registers, sizeof(Registers));
        RtlZeroMemory(Entry, BIOS_E820_ENTRY_SIZE);
        Entry[20] = BIOS_E820_ENABLED;
        Registers.Eax = 0xE820;
        Registers.Ebx = Continuation;
        Registers.Ecx = BIOS_E820_ENTRY_SIZE;
        Registers.Edx = BIOS_E820_SIGNATURE;
        Registers.Es = BiosSegment(Entry);
        Registers.Edi = BiosOffset(Entry);
        BiosCall(0x15, &Registers);

        if ((Registers.Eflags & BIOS_CARRY_FLAG) || Registers.Eax != BIOS_E820_SIGNATURE)
            break;

        if ((Registers.Ecx < 24 || (Entry[20] & BIOS_E820_ENABLED)) &&
            LeUlonglong(Entry + 8) != 0 &&
            BiosShared->RangeCount < BIOS_MAX_MEMORY_RANGES)
        {
            Range = &BiosShared->Ranges[BiosShared->RangeCount++];
            Range->Base = LeUlonglong(Entry);
            Range->Length = LeUlonglong(Entry + 8);
            Range->Type = LeUlong(Entry + 16);
            Range->Attributes = Registers.Ecx >= 24 ? LeUlong(Entry + 20) : BIOS_E820_ENABLED;
        }

        Continuation = Registers.Ebx;
    } while (Continuation != 0);

    if (BiosShared->RangeCount == 0)
    {
        /* TODO: fall back to INT 15h E801/88h on firmware without the E820 memory map */
        Range = &BiosShared->Ranges[BiosShared->RangeCount++];
        Range->Base = 0;
        Range->Length = 0x9F000;
        Range->Type = BIOS_E820_RAM;
    }
}

static
VOID
BiosPaint(
    _In_ PFN_NUMBER BasePage,
    _In_ PFN_NUMBER PageCount,
    _In_ TYPE_OF_MEMORY Type)
{
    static FREELDR_MEMORY_DESCRIPTOR Pieces[BIOS_MAX_DESCRIPTORS + 1];
    FREELDR_MEMORY_DESCRIPTOR Entry;
    PFN_NUMBER End, Start, Stop;
    ULONG Count = 0, i, j;

    if (PageCount == 0)
        return;

    End = BasePage + PageCount;
    for (i = 0; i < BiosMapCount; i++)
    {
        Start = BiosMap[i].BasePage;
        Stop = BiosMap[i].BasePage + BiosMap[i].PageCount;

        if (Stop <= BasePage || Start >= End || Count + 2 > BIOS_MAX_DESCRIPTORS)
        {
            Pieces[Count++] = BiosMap[i];
            continue;
        }

        if (Start < BasePage)
        {
            Pieces[Count] = BiosMap[i];
            Pieces[Count++].PageCount = BasePage - Start;
        }

        if (Stop > End)
        {
            Pieces[Count] = BiosMap[i];
            Pieces[Count].BasePage = End;
            Pieces[Count++].PageCount = Stop - End;
        }
    }

    if (Count < BIOS_MAX_DESCRIPTORS)
    {
        Pieces[Count].BasePage = BasePage;
        Pieces[Count].PageCount = PageCount;
        Pieces[Count++].MemoryType = Type;
    }

    for (i = 1; i < Count; i++)
    {
        Entry = Pieces[i];
        for (j = i; j > 0 && Pieces[j - 1].BasePage > Entry.BasePage; j--)
            Pieces[j] = Pieces[j - 1];
        Pieces[j] = Entry;
    }

    RtlCopyMemory(BiosMap, Pieces, Count * sizeof(Pieces[0]));
    BiosMapCount = Count;
    RtlZeroMemory(&BiosMap[BiosMapCount], sizeof(BiosMap[0]));
}

static
VOID
BiosPaintBytes(
    _In_ ULONGLONG Base,
    _In_ ULONGLONG Size,
    _In_ TYPE_OF_MEMORY Type,
    _In_ BOOLEAN Inner)
{
    ULONGLONG First, Last;

    if (Size == 0)
        return;

    if (Inner)
    {
        First = (Base + PAGE_SIZE - 1) / PAGE_SIZE;
        Last = (Base + Size) / PAGE_SIZE;
    }
    else
    {
        First = Base / PAGE_SIZE;
        Last = (Base + Size + PAGE_SIZE - 1) / PAGE_SIZE;
    }

    if (Last > First)
        BiosPaint((PFN_NUMBER)First, (PFN_NUMBER)(Last - First), Type);
}

static
TYPE_OF_MEMORY
BiosRangeType(
    _In_ PBIOS_MEMORY_RANGE Range)
{
    switch (Range->Type)
    {
        case BIOS_E820_RAM:
            return LoaderFree;

        case BIOS_E820_ACPI:
        case BIOS_E820_NVS:
            return LoaderSpecialMemory;

        case BIOS_E820_BAD:
            return LoaderBad;
    }

    return Range->Base < BIOS_ROM_LIMIT ? LoaderFirmwarePermanent : LoaderReserve;
}

static
VOID
BiosPaintFirmware(VOID)
{
    ULONG i;

    BiosMapCount = 0;
    RtlZeroMemory(BiosMap, sizeof(BiosMap));

    for (i = 0; i < BiosShared->RangeCount; i++)
    {
        if (BiosShared->Ranges[i].Type == BIOS_E820_RAM)
            BiosPaintBytes(BiosShared->Ranges[i].Base, BiosShared->Ranges[i].Length, LoaderFree, TRUE);
    }

    for (i = 0; i < BiosShared->RangeCount; i++)
    {
        if (BiosShared->Ranges[i].Type != BIOS_E820_RAM)
        {
            BiosPaintBytes(BiosShared->Ranges[i].Base,
                           BiosShared->Ranges[i].Length,
                           BiosRangeType(&BiosShared->Ranges[i]),
                           FALSE);
        }
    }

    BiosPaintBytes(BIOS_CONVENTIONAL_LIMIT, BIOS_ROM_LIMIT - BIOS_CONVENTIONAL_LIMIT, LoaderFirmwarePermanent, FALSE);
}

static
VOID
BiosBuildUsable(VOID)
{
    ULONG i;

    BiosPaintFirmware();

    BiosUsableCount = 0;
    for (i = 0; i < BiosMapCount && BiosUsableCount < BIOS_MAX_USABLE; i++)
    {
        if (BiosMap[i].MemoryType != LoaderFree)
            continue;

        if (BiosUsableCount != 0 &&
            BiosUsable[BiosUsableCount - 1].BasePage + BiosUsable[BiosUsableCount - 1].PageCount == BiosMap[i].BasePage)
        {
            BiosUsable[BiosUsableCount - 1].PageCount += BiosMap[i].PageCount;
            continue;
        }

        BiosUsable[BiosUsableCount].BasePage = BiosMap[i].BasePage;
        BiosUsable[BiosUsableCount].PageCount = BiosMap[i].PageCount;
        BiosUsableCount++;
    }
}

static
BOOLEAN
BiosIsUsable(
    _In_ ULONGLONG Base,
    _In_ ULONGLONG Size)
{
    ULONGLONG First = Base / PAGE_SIZE;
    ULONGLONG Last = (Base + Size) / PAGE_SIZE;
    ULONG i;

    for (i = 0; i < BiosUsableCount; i++)
    {
        if (First >= BiosUsable[i].BasePage && Last <= BiosUsable[i].BasePage + BiosUsable[i].PageCount)
            return TRUE;
    }

    return FALSE;
}

static
ULONG
BiosFindAllocation(
    _In_ ULONGLONG Base)
{
    ULONG Low = 0, High = BiosShared->AllocationCount, Middle;

    while (Low < High)
    {
        Middle = Low + (High - Low) / 2;
        if (BiosShared->Allocations[Middle].Base < Base)
            Low = Middle + 1;
        else
            High = Middle;
    }

    return Low;
}

static
BOOLEAN
BiosOverlapsAllocation(
    _In_ ULONGLONG Base,
    _In_ ULONGLONG Size)
{
    ULONG Index = BiosFindAllocation(Base);

    if (Index < BiosShared->AllocationCount && BiosShared->Allocations[Index].Base < Base + Size)
        return TRUE;

    return Index > 0 &&
           BiosShared->Allocations[Index - 1].Base + BiosShared->Allocations[Index - 1].Size > Base;
}

static
BOOLEAN
BiosRecordAllocation(
    _In_ ULONGLONG Base,
    _In_ ULONGLONG Size,
    _In_ TYPE_OF_MEMORY Type)
{
    ULONG Index;

    ASSERT((Base & (PAGE_SIZE - 1)) == 0);
    ASSERT(Size != 0 && (Size & (PAGE_SIZE - 1)) == 0);

    if (BiosShared->AllocationCount >= BIOS_MAX_ALLOCATIONS || BiosOverlapsAllocation(Base, Size))
        return FALSE;

    Index = BiosFindAllocation(Base);
    RtlMoveMemory(&BiosShared->Allocations[Index + 1],
                  &BiosShared->Allocations[Index],
                  (BiosShared->AllocationCount - Index) * sizeof(BiosShared->Allocations[0]));
    BiosShared->Allocations[Index].Base = Base;
    BiosShared->Allocations[Index].Size = Size;
    BiosShared->Allocations[Index].Type = Type;
    BiosShared->Allocations[Index].Reserved = 0;
    BiosShared->AllocationCount++;

    ASSERT(Index == 0 || BiosShared->Allocations[Index - 1].Base + BiosShared->Allocations[Index - 1].Size <= Base);
    ASSERT(Index + 1 == BiosShared->AllocationCount || Base + Size <= BiosShared->Allocations[Index + 1].Base);
    return TRUE;
}

BOOLEAN
BiosReserveMemory(
    _In_ ULONGLONG Base,
    _In_ ULONGLONG Size,
    _In_ TYPE_OF_MEMORY Type)
{
    ULONGLONG First = ALIGN_DOWN_BY(Base, PAGE_SIZE);
    ULONGLONG Last = ALIGN_UP_BY(Base + Size, PAGE_SIZE);

    return BiosRecordAllocation(First, Last - First, Type);
}

static
BOOLEAN
BiosFindGap(
    _In_ PFN_NUMBER Start,
    _In_ PFN_NUMBER End,
    _In_ PFN_NUMBER Pages,
    _Out_ PPFN_NUMBER Result)
{
    PFN_NUMBER Top = End, BusyStart, BusyEnd;
    LONG i;

    for (i = (LONG)BiosShared->AllocationCount - 1; i >= 0; i--)
    {
        BusyStart = (PFN_NUMBER)(BiosShared->Allocations[i].Base / PAGE_SIZE);
        BusyEnd = BusyStart + (PFN_NUMBER)(BiosShared->Allocations[i].Size / PAGE_SIZE);

        if (BusyStart >= Top)
            continue;
        if (BusyEnd <= Start)
            break;

        if (Top >= BusyEnd && Top - BusyEnd >= Pages)
        {
            *Result = Top - Pages;
            return TRUE;
        }

        Top = BusyStart;
        if (Top <= Start)
            return FALSE;
    }

    if (Top > Start && Top - Start >= Pages)
    {
        *Result = Top - Pages;
        return TRUE;
    }

    return FALSE;
}

BOOLEAN
BiosGetUsableRun(
    _In_ ULONG Index,
    _Out_ PPFN_NUMBER BasePage,
    _Out_ PPFN_NUMBER PageCount)
{
    if (Index >= BiosUsableCount)
        return FALSE;

    *BasePage = BiosUsable[Index].BasePage;
    *PageCount = BiosUsable[Index].PageCount;
    return TRUE;
}

BOOLEAN
BiosGetFreeRun(
    _Inout_ PULONG Cursor,
    _Inout_ PPFN_NUMBER Position,
    _Out_ PPFN_NUMBER BasePage,
    _Out_ PPFN_NUMBER PageCount)
{
    PFN_NUMBER Start, End, BusyStart, BusyEnd;
    ULONG i;

    for (; *Cursor < BiosUsableCount; (*Cursor)++, *Position = 0)
    {
        Start = max(*Position, BiosUsable[*Cursor].BasePage);
        End = BiosUsable[*Cursor].BasePage + BiosUsable[*Cursor].PageCount;

        while (Start < End)
        {
            for (i = 0; i < BiosShared->AllocationCount; i++)
            {
                BusyStart = (PFN_NUMBER)(BiosShared->Allocations[i].Base / PAGE_SIZE);
                BusyEnd = BusyStart + (PFN_NUMBER)(BiosShared->Allocations[i].Size / PAGE_SIZE);
                if (BusyEnd > Start)
                    break;
            }

            if (i == BiosShared->AllocationCount || BusyStart >= End)
            {
                *BasePage = Start;
                *PageCount = End - Start;
                *Position = End;
                return TRUE;
            }

            if (BusyStart > Start)
            {
                *BasePage = Start;
                *PageCount = BusyStart - Start;
                *Position = BusyEnd;
                return TRUE;
            }

            Start = BusyEnd;
        }
    }

    return FALSE;
}

PVOID
FwAllocatePagesBelow(
    _In_ SIZE_T Size,
    _In_ ULONGLONG MaxAddress)
{
    PFN_NUMBER Pages, MaxPage, Start, End, Page;
    LONG i;

    Size = ALIGN_UP_BY(Size, PAGE_SIZE);
    if (Size == 0)
        return NULL;

    /* TODO: map RAM above 4 GB into the boot identity mapping and allocate from it */
    MaxAddress = min(MaxAddress, BIOS_IDENTITY_LIMIT);
    Pages = (PFN_NUMBER)(Size / PAGE_SIZE);
    MaxPage = (PFN_NUMBER)(MaxAddress / PAGE_SIZE);

    for (i = (LONG)BiosUsableCount - 1; i >= 0; i--)
    {
        Start = BiosUsable[i].BasePage;
        End = min(BiosUsable[i].BasePage + BiosUsable[i].PageCount, MaxPage);
        if (End <= Start)
            continue;

        if (BiosFindGap(Start, End, Pages, &Page) &&
            BiosRecordAllocation((ULONGLONG)Page * PAGE_SIZE, Size, LoaderLoadedProgram))
        {
            return (PVOID)(ULONG_PTR)((ULONGLONG)Page * PAGE_SIZE);
        }
    }

    return NULL;
}

PVOID
FwAllocatePages(
    _In_ SIZE_T Size)
{
    return FwAllocatePagesBelow(Size, BIOS_IDENTITY_LIMIT);
}

BOOLEAN
FwAllocatePagesAt(
    _In_ PVOID Address,
    _In_ SIZE_T Size)
{
    ULONGLONG Base = (ULONG_PTR)Address;

    Size = ALIGN_UP_BY(Size, PAGE_SIZE);
    if ((Base & (PAGE_SIZE - 1)) != 0 ||
        Size == 0 ||
        Base + Size > BIOS_IDENTITY_LIMIT ||
        !BiosIsUsable(Base, Size))
    {
        return FALSE;
    }

    return BiosRecordAllocation(Base, Size, LoaderLoadedProgram);
}

VOID
FwFreePages(
    _In_ PVOID Memory,
    _In_ SIZE_T Size)
{
    ULONGLONG Base = (ULONG_PTR)Memory;
    ULONG Index = BiosFindAllocation(Base);

    UNREFERENCED_PARAMETER(Size);

    if (Index >= BiosShared->AllocationCount || BiosShared->Allocations[Index].Base != Base)
        return;

    ASSERT(BiosShared->Allocations[Index].Type == LoaderLoadedProgram);
    RtlMoveMemory(&BiosShared->Allocations[Index],
                  &BiosShared->Allocations[Index + 1],
                  (BiosShared->AllocationCount - Index - 1) * sizeof(BiosShared->Allocations[0]));
    BiosShared->AllocationCount--;
}

PVOID
FwAllocate(
    _In_ SIZE_T Size)
{
    PBIOS_HEAP_BLOCK Block, Last = NULL, Split;
    SIZE_T Chunk;

    Size = ALIGN_UP_BY(max(Size, (SIZE_T)1), sizeof(BIOS_HEAP_BLOCK));

    for (Block = BiosShared->Heap; Block; Last = Block, Block = Block->Next)
    {
        if (!Block->Free || Block->Size < Size)
            continue;

        if (Block->Size >= Size + sizeof(BIOS_HEAP_BLOCK) + BIOS_HEAP_SPLIT)
        {
            Split = (PBIOS_HEAP_BLOCK)((PUCHAR)(Block + 1) + Size);
            Split->Size = Block->Size - Size - sizeof(BIOS_HEAP_BLOCK);
            Split->Free = TRUE;
            Split->Next = Block->Next;
            Block->Size = Size;
            Block->Next = Split;
        }

        Block->Free = FALSE;
        return Block + 1;
    }

    Chunk = max((SIZE_T)BIOS_HEAP_CHUNK, ALIGN_UP_BY(Size + sizeof(BIOS_HEAP_BLOCK), PAGE_SIZE));
    Block = FwAllocatePages(Chunk);
    if (!Block)
        return NULL;

    Block->Size = Chunk - sizeof(BIOS_HEAP_BLOCK);
    Block->Free = TRUE;
    Block->Next = NULL;
    if (Last)
        Last->Next = Block;
    else
        BiosShared->Heap = Block;

    return FwAllocate(Size);
}

VOID
FwFree(
    _In_ PVOID Memory)
{
    PBIOS_HEAP_BLOCK Block, Next;

    if (!Memory)
        return;

    Block = (PBIOS_HEAP_BLOCK)Memory - 1;
    ASSERT(!Block->Free);
    Block->Free = TRUE;

    for (Next = Block->Next;
         Next && Next->Free && (PUCHAR)(Block + 1) + Block->Size == (PUCHAR)Next;
         Next = Block->Next)
    {
        Block->Size += sizeof(BIOS_HEAP_BLOCK) + Next->Size;
        Block->Next = Next->Next;
    }
}

PFREELDR_MEMORY_DESCRIPTOR
FwGetMemoryMap(
    _Out_ PULONG Count)
{
    ULONG i;

    BiosPaintFirmware();

    for (i = 0; i < BiosShared->AllocationCount; i++)
    {
        BiosPaintBytes(BiosShared->Allocations[i].Base,
                       BiosShared->Allocations[i].Size,
                       BiosShared->Allocations[i].Type,
                       FALSE);
    }

    *Count = BiosMapCount;
    return BiosMap;
}

VOID
BiosMemoryInitialize(
    _In_ BOOLEAN Root)
{
    if (!Root)
    {
        BiosBuildUsable();
        return;
    }

    BiosReadE820();
    BiosBuildUsable();

    BiosReserveMemory(0, PAGE_SIZE, LoaderFirmwarePermanent);
    BiosReserveMemory(BIOS_TRANSFER_BASE, BIOS_TRANSFER_SIZE, LoaderFirmwareTemporary);
    BiosReserveMemory(BIOS_STUB_BASE, BIOS_STUB_SIZE, LoaderFirmwareTemporary);
    BiosReserveMemory(BIOS_PAGE_TABLE_BASE, BIOS_PAGE_TABLE_SIZE, LoaderFirmwareTemporary);
    BiosReserveMemory(BIOS_STACK_BASE, BIOS_STACK_SIZE, LoaderFirmwareTemporary);
    BiosReserveMemory(BiosStartup->ImageBase, BiosStartup->ImageSize, LoaderLoadedProgram);
}
