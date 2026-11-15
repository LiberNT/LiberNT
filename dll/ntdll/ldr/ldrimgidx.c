/*
 * PROJECT:     LiberNT NT User-Mode Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Index of the loaded images by address, kept by the loader instead of read from the PEB lists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>
#define NDEBUG
#include <debug.h>

#define LDRP_IMAGE_RANGES_INITIAL 32

typedef struct _LDRP_IMAGE_RANGE
{
    ULONG_PTR Base;
    ULONG_PTR End;
    PLDR_DATA_TABLE_ENTRY Entry;
} LDRP_IMAGE_RANGE, *PLDRP_IMAGE_RANGE;

static PLDRP_IMAGE_RANGE LdrpImageRanges;
static ULONG LdrpImageRangeCount;
static ULONG LdrpImageRangeCapacity;
static BOOLEAN LdrpImageRangesIncomplete;

/* Number of ranges that start at or below Address */
static
ULONG
LdrpCountRangesAtOrBelow(
    _In_ ULONG_PTR Address)
{
    ULONG Low = 0;
    ULONG High = LdrpImageRangeCount;

    while (Low < High)
    {
        ULONG Middle = Low + (High - Low) / 2;

        if (LdrpImageRanges[Middle].Base <= Address)
            Low = Middle + 1;
        else
            High = Middle;
    }

    return Low;
}

static
BOOLEAN
LdrpGrowImageRanges(VOID)
{
    ULONG Capacity = (LdrpImageRangeCapacity != 0) ? (LdrpImageRangeCapacity * 2) : LDRP_IMAGE_RANGES_INITIAL;
    PLDRP_IMAGE_RANGE Ranges;

    if (LdrpImageRanges == NULL)
        Ranges = RtlAllocateHeap(LdrpHeap, 0, Capacity * sizeof(*Ranges));
    else
        Ranges = RtlReAllocateHeap(LdrpHeap, 0, LdrpImageRanges, Capacity * sizeof(*Ranges));

    if (Ranges == NULL)
        return FALSE;

    LdrpImageRanges = Ranges;
    LdrpImageRangeCapacity = Capacity;
    return TRUE;
}

_Requires_exclusive_lock_held_(LdrpModuleDatatableLock)
VOID
LdrpInsertImageRange(
    _In_ PLDR_DATA_TABLE_ENTRY Entry)
{
    ULONG_PTR Base = (ULONG_PTR)Entry->DllBase;
    ULONG_PTR End = Base + Entry->SizeOfImage;
    ULONG Index;

    ASSERT(End > Base);

    if (LdrpImageRangesIncomplete)
        return;

    if ((LdrpImageRangeCount == LdrpImageRangeCapacity) && !LdrpGrowImageRanges())
    {
        /* Lookups go back to the loader lists for the rest of the process */
        LdrpImageRangesIncomplete = TRUE;
        return;
    }

    Index = LdrpCountRangesAtOrBelow(Base);

    /* Two mapped images never share an address */
    ASSERT((Index == 0) || (LdrpImageRanges[Index - 1].End <= Base));
    ASSERT((Index == LdrpImageRangeCount) || (End <= LdrpImageRanges[Index].Base));

    RtlMoveMemory(&LdrpImageRanges[Index + 1],
                  &LdrpImageRanges[Index],
                  (LdrpImageRangeCount - Index) * sizeof(LDRP_IMAGE_RANGE));

    LdrpImageRanges[Index].Base = Base;
    LdrpImageRanges[Index].End = End;
    LdrpImageRanges[Index].Entry = Entry;
    LdrpImageRangeCount++;
}

_Requires_exclusive_lock_held_(LdrpModuleDatatableLock)
VOID
LdrpRemoveImageRange(
    _In_ PLDR_DATA_TABLE_ENTRY Entry)
{
    ULONG Index = LdrpCountRangesAtOrBelow((ULONG_PTR)Entry->DllBase);

    if ((Index == 0) || (LdrpImageRanges[Index - 1].Entry != Entry))
    {
        /* Only an index that stopped growing can miss an entry */
        ASSERT(LdrpImageRangesIncomplete);
        return;
    }

    Index--;
    LdrpImageRangeCount--;
    RtlMoveMemory(&LdrpImageRanges[Index],
                  &LdrpImageRanges[Index + 1],
                  (LdrpImageRangeCount - Index) * sizeof(LDRP_IMAGE_RANGE));
}

/* FALSE when the index cannot answer and the caller has to walk the loader lists */
_Requires_lock_held_(LdrpModuleDatatableLock)
BOOLEAN
LdrpFindImageByAddress(
    _In_ PVOID Address,
    _Out_ PLDR_DATA_TABLE_ENTRY *Entry)
{
    ULONG Index;

    *Entry = NULL;

    if (LdrpImageRangesIncomplete)
        return FALSE;

    Index = LdrpCountRangesAtOrBelow((ULONG_PTR)Address);
    if ((Index != 0) && ((ULONG_PTR)Address < LdrpImageRanges[Index - 1].End))
        *Entry = LdrpImageRanges[Index - 1].Entry;

    return TRUE;
}
