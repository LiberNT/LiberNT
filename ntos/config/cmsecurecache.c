/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The hive's own store of who may touch its keys
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE DATA ***************************************************************/

#define CMP_SECURITY_CACHE_GROW 37

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Keys that say the same thing about access share one cell, so a descriptor
 * is looked for by what it says rather than by where it is.
 */
static
ULONG
CmpSecurityHash(
    _In_reads_bytes_(Length) PVOID Descriptor,
    _In_ ULONG Length)
{
    PUCHAR Bytes = Descriptor;
    ULONG ConvKey = Length;
    ULONG i;

    for (i = 0; i < Length; i++)
    {
        ConvKey = (ConvKey * 37) + Bytes[i];
    }

    return ConvKey;
}

/* The array is kept in cell order so that one cell is found by halving */
static
BOOLEAN
CmpFindSecurityCacheIndex(
    _In_ PCMHIVE Hive,
    _In_ HCELL_INDEX Cell,
    _Out_ PULONG Index)
{
    ULONG Low = 0;
    ULONG High;
    ULONG Middle;

    *Index = 0;

    if (Hive->SecurityCount == 0) return FALSE;

    High = Hive->SecurityCount - 1;

    if (Hive->SecurityHitHint >= 0 &&
        (ULONG)Hive->SecurityHitHint < Hive->SecurityCount &&
        Hive->SecurityCache[Hive->SecurityHitHint].Cell == Cell)
    {
        *Index = (ULONG)Hive->SecurityHitHint;
        return TRUE;
    }

    while (Low <= High)
    {
        Middle = Low + (High - Low) / 2;

        if (Hive->SecurityCache[Middle].Cell == Cell)
        {
            *Index = Middle;
            Hive->SecurityHitHint = (LONG)Middle;
            return TRUE;
        }

        if (Hive->SecurityCache[Middle].Cell < Cell)
        {
            Low = Middle + 1;
            continue;
        }

        if (Middle == 0) break;
        High = Middle - 1;
    }

    *Index = Low;
    return FALSE;
}

static
BOOLEAN
CmpGrowSecurityCache(
    _In_ PCMHIVE Hive)
{
    PCM_KEY_SECURITY_CACHE_ENTRY Table;
    ULONG Size;

    if (Hive->SecurityCount < Hive->SecurityCacheSize) return TRUE;

    Size = Hive->SecurityCacheSize + CMP_SECURITY_CACHE_GROW;

    Table = ExAllocatePoolZero(PagedPool,
                               Size * sizeof(CM_KEY_SECURITY_CACHE_ENTRY),
                               TAG_CMSD);
    if (Table == NULL) return FALSE;

    if (Hive->SecurityCache != NULL)
    {
        RtlCopyMemory(Table,
                      Hive->SecurityCache,
                      Hive->SecurityCount *
                          sizeof(CM_KEY_SECURITY_CACHE_ENTRY));
        ExFreePool(Hive->SecurityCache);
    }

    Hive->SecurityCache = Table;
    Hive->SecurityCacheSize = Size;
    return TRUE;
}

static
BOOLEAN
CmpInsertSecurityCacheEntry(
    _In_ PCMHIVE Hive,
    _In_ ULONG Index,
    _In_ HCELL_INDEX Cell,
    _In_ PCM_KEY_SECURITY_CACHE Cached)
{
    ULONG Move;

    if (!CmpGrowSecurityCache(Hive)) return FALSE;

    Move = Hive->SecurityCount - Index;

    if (Move != 0)
    {
        RtlMoveMemory(&Hive->SecurityCache[Index + 1],
                      &Hive->SecurityCache[Index],
                      Move * sizeof(CM_KEY_SECURITY_CACHE_ENTRY));
    }

    Hive->SecurityCache[Index].Cell = Cell;
    Hive->SecurityCache[Index].CachedSecurity = Cached;
    Hive->SecurityCount++;
    Hive->SecurityHitHint = (LONG)Index;

    InsertTailList(&Hive->SecurityHash[Cached->ConvKey %
                                       CMP_SECURITY_HASH_LISTS],
                   &Cached->List);
    return TRUE;
}

static
VOID
CmpRemoveSecurityCacheIndex(
    _In_ PCMHIVE Hive,
    _In_ ULONG Index)
{
    PCM_KEY_SECURITY_CACHE Cached;
    ULONG Move;

    Cached = Hive->SecurityCache[Index].CachedSecurity;

    if (Cached != NULL)
    {
        CmpForgetCachedSecurity(Cached);
        RemoveEntryList(&Cached->List);
        ExFreePool(Cached);
    }

    Move = Hive->SecurityCount - Index - 1;

    if (Move != 0)
    {
        RtlMoveMemory(&Hive->SecurityCache[Index],
                      &Hive->SecurityCache[Index + 1],
                      Move * sizeof(CM_KEY_SECURITY_CACHE_ENTRY));
    }

    Hive->SecurityCount--;
    Hive->SecurityHitHint = -1;
}

/* Takes a copy of what the cell says, which the hive then owns */
static
PCM_KEY_SECURITY_CACHE
CmpBuildSecurityCacheEntry(
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Cell)
{
    PCM_KEY_SECURITY_CACHE Cached;
    PCM_KEY_SECURITY Security;
    SIZE_T Size;
    ULONG Length;

    Security = (PCM_KEY_SECURITY)HvGetCell(Hive, Cell);
    if (Security == NULL) return NULL;

    if (Security->Signature != CM_KEY_SECURITY_SIGNATURE ||
        Security->DescriptorLength == 0)
    {
        HvReleaseCell(Hive, Cell);
        return NULL;
    }

    Length = Security->DescriptorLength;
    Size = FIELD_OFFSET(CM_KEY_SECURITY_CACHE, Descriptor) + Length;

    Cached = ExAllocatePoolZero(PagedPool, Size, TAG_CMSD);
    if (Cached == NULL)
    {
        HvReleaseCell(Hive, Cell);
        return NULL;
    }

    Cached->Cell = Cell;
    Cached->DescriptorLength = Length;
    RtlCopyMemory(&Cached->Descriptor, &Security->Descriptor, Length);
    Cached->ConvKey = CmpSecurityHash(&Cached->Descriptor, Length);

    HvReleaseCell(Hive, Cell);
    return Cached;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
CmpInitSecurityCache(
    _In_ PCMHIVE Hive)
{
    ULONG i;

    Hive->SecurityCount = 0;
    Hive->SecurityCacheSize = 0;
    Hive->SecurityHitHint = -1;
    Hive->SecurityCache = NULL;

    for (i = 0; i < CMP_SECURITY_HASH_LISTS; i++)
    {
        InitializeListHead(&Hive->SecurityHash[i]);
    }

    ExInitializePushLock(&Hive->SecurityLock);
}

VOID
NTAPI
CmpDestroySecurityCache(
    _In_ PCMHIVE Hive)
{
    PCM_KEY_SECURITY_CACHE Cached;
    ULONG i;

    for (i = 0; i < Hive->SecurityCount; i++)
    {
        Cached = Hive->SecurityCache[i].CachedSecurity;
        if (Cached == NULL) continue;

        CmpForgetCachedSecurity(Cached);
        RemoveEntryList(&Cached->List);
        ExFreePool(Cached);
    }

    if (Hive->SecurityCache != NULL) ExFreePool(Hive->SecurityCache);

    Hive->SecurityCache = NULL;
    Hive->SecurityCount = 0;
    Hive->SecurityCacheSize = 0;
    Hive->SecurityHitHint = -1;

    for (i = 0; i < CMP_SECURITY_HASH_LISTS; i++)
    {
        InitializeListHead(&Hive->SecurityHash[i]);
    }
}

/*
 * What a key says about access, as a copy the hive owns. Every caller reads
 * it from here, so nobody depends on a cell staying mapped.
 */
PCM_KEY_SECURITY_CACHE
NTAPI
CmpGetSecurityCacheEntry(
    _In_ PCMHIVE Hive,
    _In_ HCELL_INDEX Cell)
{
    PCM_KEY_SECURITY_CACHE Cached;
    ULONG Index;

    if (Cell == HCELL_NIL) return NULL;

    if (CmpFindSecurityCacheIndex(Hive, Cell, &Index))
    {
        return Hive->SecurityCache[Index].CachedSecurity;
    }

    Cached = CmpBuildSecurityCacheEntry(&Hive->Hive, Cell);
    if (Cached == NULL) return NULL;

    if (!CmpInsertSecurityCacheEntry(Hive, Index, Cell, Cached))
    {
        ExFreePool(Cached);
        return NULL;
    }

    return Cached;
}

VOID
NTAPI
CmpInvalidateSecurityCacheEntry(
    _In_ PCMHIVE Hive,
    _In_ HCELL_INDEX Cell)
{
    ULONG Index;

    if (Cell == HCELL_NIL) return;
    if (!CmpFindSecurityCacheIndex(Hive, Cell, &Index)) return;

    CmpRemoveSecurityCacheIndex(Hive, Index);
}

/*
 * Looks through what the hive already says about access for a descriptor that
 * says exactly the same thing, so one cell can serve many keys.
 */
HCELL_INDEX
NTAPI
CmpFindSecurityCellByDescriptor(
    _In_ PCMHIVE Hive,
    _In_reads_bytes_(Length) PVOID Descriptor,
    _In_ ULONG Length)
{
    PCM_KEY_SECURITY_CACHE Cached;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Head;
    ULONG ConvKey;

    ConvKey = CmpSecurityHash(Descriptor, Length);
    Head = &Hive->SecurityHash[ConvKey % CMP_SECURITY_HASH_LISTS];

    for (Entry = Head->Flink; Entry != Head; Entry = Entry->Flink)
    {
        Cached = CONTAINING_RECORD(Entry, CM_KEY_SECURITY_CACHE, List);

        if (Cached->ConvKey != ConvKey) continue;
        if (Cached->DescriptorLength != Length) continue;
        if (RtlCompareMemory(&Cached->Descriptor,
                             Descriptor,
                             Length) != Length)
        {
            continue;
        }

        return Cached->Cell;
    }

    return HCELL_NIL;
}

/*
 * Reads every security cell the hive holds into the cache, which is what lets
 * a later assignment share a cell with a key loaded long before it.
 */
VOID
NTAPI
CmpPopulateSecurityCache(
    _In_ PCMHIVE Hive,
    _In_ HCELL_INDEX Anchor)
{
    PCM_KEY_SECURITY Security;
    HCELL_INDEX Cell = Anchor;
    HCELL_INDEX Next;
    ULONG Guard = 0;

    if (Anchor == HCELL_NIL) return;

    while (Cell != HCELL_NIL && Guard < Hive->Hive.Storage[Stable].Length)
    {
        if (CmpGetSecurityCacheEntry(Hive, Cell) == NULL) break;

        Security = (PCM_KEY_SECURITY)HvGetCell(&Hive->Hive, Cell);
        if (Security == NULL) break;

        Next = Security->Flink;
        HvReleaseCell(&Hive->Hive, Cell);

        if (Next == Anchor || Next == Cell) break;

        Cell = Next;
        Guard++;
    }
}
