/*
 * PROJECT:     LiberNT Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/track/tagtable.c
 * PURPOSE:     Pool allocation tag tracking
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/poolenv.h"
#include "../include/pooltrack.h"

C_ASSERT((POOL_TAG_TABLE_ENTRIES & (POOL_TAG_TABLE_ENTRIES - 1)) == 0);

FORCEINLINE
ULONG
PoolTagHash(ULONG Tag)
{
    ULONG Hash = Tag * 0x9E3779B1UL;

    return (Hash >> 16) & (POOL_TAG_TABLE_ENTRIES - 2);
}

VOID
PoolTagTrackerInitialize(
    _Out_ PPOOL_TAG_TRACKER Tracker)
{
    RtlZeroMemory(Tracker, sizeof(*Tracker));
}

BOOLEAN
PoolTagTrackerAddTable(
    _Inout_ PPOOL_TAG_TRACKER Tracker,
    _In_ PPOOL_TAG_TABLE Table)
{
    ULONG Count = PoolTagTrackerCount(Tracker);
#if DBG || defined(POOL_HOST_TEST)
    ULONG i;

    POOL_ASSERT(Count <= POOL_MAX_SLOTS && Table != NULL);
    for (i = 0; i < Count; i++)
    {
        POOL_ASSERT(Tracker->Table[i] != NULL && Tracker->Table[i] != Table);
        POOL_ASSERT(POOL_ATOMIC_READ32(&Tracker->Table[i]->Entry[POOL_TAG_TABLE_ENTRIES - 1].Tag) ==
                    (LONG)POOL_TAG_OVERFLOW);
    }
#endif
    if (Count >= POOL_MAX_SLOTS)
        return FALSE;

    RtlZeroMemory(Table, sizeof(*Table));
    Table->Entry[POOL_TAG_TABLE_ENTRIES - 1].Tag = (LONG)POOL_TAG_OVERFLOW;
    Tracker->Table[Count] = Table;
    POOL_STORE_RELEASE32(&Tracker->TableCount, Count + 1);
    return TRUE;
}

static
PPOOL_TAG_ENTRY
PoolTagFind(
    _In_ PPOOL_TAG_TABLE Table,
    _In_ ULONG Tag,
    _In_ BOOLEAN Insert)
{
    ULONG Index = PoolTagHash(Tag);
    ULONG Probe;

    POOL_ASSERT(Table != NULL);
    POOL_ASSERT(POOL_ATOMIC_READ32(&Table->Entry[POOL_TAG_TABLE_ENTRIES - 1].Tag) == (LONG)POOL_TAG_OVERFLOW);
    POOL_ASSERT(Index < POOL_TAG_TABLE_ENTRIES - 1);
    if (Tag == 0 || Tag == POOL_TAG_OVERFLOW)
    {
        PPOOL_TAG_ENTRY Overflow = &Table->Entry[POOL_TAG_TABLE_ENTRIES - 1];

        return (Insert || POOL_ATOMIC_READ32(&Overflow->Allocations[0]) != 0 ||
                POOL_ATOMIC_READ32(&Overflow->Allocations[1]) != 0) ? Overflow : NULL;
    }

    for (Probe = 0; Probe < POOL_TAG_TABLE_ENTRIES - 1; Probe++)
    {
        PPOOL_TAG_ENTRY Entry = &Table->Entry[Index];
        LONG Current = POOL_ATOMIC_READ32(&Entry->Tag);

        if (Current == (LONG)Tag)
            return Entry;

        if (Current == 0)
        {
            if (!Insert)
                return NULL;

            Current = POOL_ATOMIC_CAS32(&Entry->Tag, (LONG)Tag, 0);
            if (Current == 0 || Current == (LONG)Tag)
            {
                POOL_ASSERT(POOL_ATOMIC_READ32(&Entry->Tag) == (LONG)Tag);
                return Entry;
            }
        }

        Index++;
        if (Index >= POOL_TAG_TABLE_ENTRIES - 1)
            Index = 0;
    }

    return Insert ? &Table->Entry[POOL_TAG_TABLE_ENTRIES - 1] : NULL;
}

VOID
PoolTagCharge(
    _Inout_ PPOOL_TAG_TRACKER Tracker,
    _In_ ULONG Tag,
    _In_ ULONG Class,
    _In_ SIZE_T Bytes)
{
    PPOOL_TAG_ENTRY Entry;
    PPOOL_TAG_TABLE Table;
    ULONG Count = PoolTagTrackerCount(Tracker);

    POOL_ASSERT(Count <= POOL_MAX_SLOTS);
    if (Count == 0 || Class >= POOL_TAG_CLASSES)
        return;

    Table = Tracker->Table[POOL_CURRENT_PROCESSOR() % Count];
    POOL_ASSERT(Table != NULL);
    Entry = PoolTagFind(Table, Tag, TRUE);
    POOL_ASSERT(Entry != NULL);
    POOL_ASSERT(Entry == &Table->Entry[POOL_TAG_TABLE_ENTRIES - 1] ||
                POOL_ATOMIC_READ32(&Entry->Tag) == (LONG)Tag);
    POOL_ATOMIC_ADD32(&Entry->Allocations[Class], 1);
    POOL_ATOMIC_ADD64(&Entry->Bytes[Class], (LONG64)Bytes);
}

VOID
PoolTagRelease(
    _Inout_ PPOOL_TAG_TRACKER Tracker,
    _In_ ULONG Tag,
    _In_ ULONG Class,
    _In_ SIZE_T Bytes)
{
    PPOOL_TAG_ENTRY Entry;
    PPOOL_TAG_TABLE Table;
    ULONG Count = PoolTagTrackerCount(Tracker);

    POOL_ASSERT(Count <= POOL_MAX_SLOTS);
    if (Count == 0 || Class >= POOL_TAG_CLASSES)
        return;

    Table = Tracker->Table[POOL_CURRENT_PROCESSOR() % Count];
    POOL_ASSERT(Table != NULL);
    Entry = PoolTagFind(Table, Tag, TRUE);
    POOL_ASSERT(Entry != NULL);
    POOL_ASSERT(Entry == &Table->Entry[POOL_TAG_TABLE_ENTRIES - 1] ||
                POOL_ATOMIC_READ32(&Entry->Tag) == (LONG)Tag);
    POOL_ATOMIC_ADD32(&Entry->Frees[Class], 1);
    POOL_ATOMIC_ADD64(&Entry->Bytes[Class], -(LONG64)Bytes);
}

static
VOID
PoolTagAccumulate(
    _Inout_ PPOOL_TAG_USAGE Usage,
    _In_ PPOOL_TAG_ENTRY Entry)
{
    ULONG Class;

    for (Class = 0; Class < POOL_TAG_CLASSES; Class++)
    {
        Usage->Allocations[Class] += (ULONG)POOL_ATOMIC_READ32(&Entry->Allocations[Class]);
        Usage->Frees[Class] += (ULONG)POOL_ATOMIC_READ32(&Entry->Frees[Class]);
        Usage->Bytes[Class] += POOL_ATOMIC_READ64(&Entry->Bytes[Class]);
    }
}

BOOLEAN
PoolTagQuery(
    _In_ PPOOL_TAG_TRACKER Tracker,
    _In_ ULONG Tag,
    _Out_ PPOOL_TAG_USAGE Usage)
{
    BOOLEAN Found = FALSE;
    ULONG Count = PoolTagTrackerCount(Tracker);
    ULONG i;

    RtlZeroMemory(Usage, sizeof(*Usage));
    Usage->Tag = Tag;

    POOL_ASSERT(Count <= POOL_MAX_SLOTS);
    for (i = 0; i < Count; i++)
    {
        PPOOL_TAG_ENTRY Entry = PoolTagFind(Tracker->Table[i], Tag, FALSE);

        if (Entry != NULL)
        {
            PoolTagAccumulate(Usage, Entry);
            Found = TRUE;
        }
    }

    return Found;
}

BOOLEAN
PoolTagEnumerate(
    _In_ PPOOL_TAG_TRACKER Tracker,
    _Inout_ PULONG64 Cursor,
    _Out_ PPOOL_TAG_USAGE Usage)
{
    ULONG Table = (ULONG)(*Cursor >> 32);
    ULONG Index = (ULONG)*Cursor;
    ULONG Count = PoolTagTrackerCount(Tracker);
    ULONG Earlier;

    POOL_ASSERT(Count <= POOL_MAX_SLOTS);
    for (; Table < Count; Table++, Index = 0)
    {
        for (; Index < POOL_TAG_TABLE_ENTRIES; Index++)
        {
            ULONG Tag = (ULONG)POOL_ATOMIC_READ32(&Tracker->Table[Table]->Entry[Index].Tag);

            if (Tag == 0)
                continue;

            for (Earlier = 0; Earlier < Table; Earlier++)
            {
                if (PoolTagFind(Tracker->Table[Earlier], Tag, FALSE) != NULL)
                    break;
            }

            if (Earlier != Table)
                continue;

            if (!PoolTagQuery(Tracker, Tag, Usage))
                continue;

            *Cursor = ((ULONG64)Table << 32) | (Index + 1);
            return TRUE;
        }
    }

    *Cursor = (ULONG64)Table << 32;
    return FALSE;
}

ULONG
PoolTagSnapshot(
    _In_ PPOOL_TAG_TRACKER Tracker,
    _Out_ PPOOL_TAG_USAGE Usage,
    _In_ ULONG MaxEntries)
{
    ULONG64 Cursor = 0;
    ULONG Count = 0;

    while (Count < MaxEntries && PoolTagEnumerate(Tracker, &Cursor, &Usage[Count]))
        Count++;

    return Count;
}
