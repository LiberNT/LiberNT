/*
 * PROJECT:     LiberNT Cache Manager
 * FILE:        ntoskrnl/cc/api/ccclose.c
 * PURPOSE:     Cache map teardown and cached-view reclamation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccnt.h"

CC_NT_MAP_BUCKET CcNtMapBuckets[CC_NT_MAP_BUCKETS];
static volatile LONG CcNtClosedMapCount;

VOID
CcNtInitializeMapTable(VOID)
{
    ULONG Index;

    for (Index = 0; Index < CC_NT_MAP_BUCKETS; Index++)
    {
        CC_LOCK_INIT(&CcNtMapBuckets[Index].Lock);
        InitializeListHead(&CcNtMapBuckets[Index].List);
    }
    CcNtClosedMapCount = 0;
}

PCC_NT_MAP_BUCKET
CcNtMapBucket(
    _In_ PSECTION_OBJECT_POINTERS Pointers)
{
    ULONG_PTR Value = (ULONG_PTR)Pointers >> 4;

    return &CcNtMapBuckets[(Value ^ (Value >> 6) ^ (Value >> 12)) & (CC_NT_MAP_BUCKETS - 1)];
}

static
VOID
CcNtReviveMap(
    _Inout_ PCC_NT_MAP NtMap)
{
    if (NtMap->Closing)
    {
        NtMap->Closing = FALSE;
        CC_ATOMIC_ADD32(&CcNtClosedMapCount, -1);
    }
    CC_ATOMIC_ADD32(&NtMap->ReferenceCount, 1);
}

PCC_NT_MAP
CcNtInsertMap(
    _Inout_ PCC_NT_MAP Fresh)
{
    PCC_NT_MAP_BUCKET Bucket = CcNtMapBucket(Fresh->Pointers);
    PCC_NT_MAP NtMap;
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Bucket->Lock, &OldIrql);
    NtMap = Fresh->Pointers->SharedCacheMap;
    if (NtMap == NULL)
    {
        Fresh->Pointers->SharedCacheMap = Fresh;
        InsertTailList(&Bucket->List, &Fresh->Link);
        NtMap = Fresh;
    }
    else
    {
        CcNtReviveMap(NtMap);
    }
    CC_LOCK_RELEASE(&Bucket->Lock, OldIrql);

    return NtMap;
}

VOID
CcNtLinkMap(
    _Inout_ PCC_NT_MAP NtMap)
{
    PCC_NT_MAP_BUCKET Bucket = CcNtMapBucket(NtMap->Pointers);
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Bucket->Lock, &OldIrql);
    InsertTailList(&Bucket->List, &NtMap->Link);
    CC_LOCK_RELEASE(&Bucket->Lock, OldIrql);
}

BOOLEAN
CcNtIsMapTableEmpty(VOID)
{
    ULONG Index;

    for (Index = 0; Index < CC_NT_MAP_BUCKETS; Index++)
    {
        if (!IsListEmpty(&CcNtMapBuckets[Index].List))
            return FALSE;
    }

    return TRUE;
}

PCC_NT_MAP
CcNtReferenceMap(
    _In_ PSECTION_OBJECT_POINTERS Pointers)
{
    PCC_NT_MAP_BUCKET Bucket;
    PCC_NT_MAP NtMap;
    KIRQL OldIrql;

    if (Pointers == NULL)
        return NULL;

    Bucket = CcNtMapBucket(Pointers);
    CC_LOCK_ACQUIRE(&Bucket->Lock, &OldIrql);
    NtMap = Pointers->SharedCacheMap;
    if (NtMap != NULL)
        CcNtReviveMap(NtMap);
    CC_LOCK_RELEASE(&Bucket->Lock, OldIrql);

    return NtMap;
}

PCC_NT_MAP
CcNtReferenceFileMap(
    _In_ PFILE_OBJECT FileObject)
{
    PSECTION_OBJECT_POINTERS Pointers = FileObject->SectionObjectPointer;
    PCC_NT_MAP NtMap;

    if (Pointers == NULL || FileObject->PrivateCacheMap == NULL)
        return CcNtReferenceMap(Pointers);

    NtMap = Pointers->SharedCacheMap;
    if (NtMap == NULL)
        return CcNtReferenceMap(Pointers);

    ASSERT(!NtMap->Closing);
    CC_ATOMIC_ADD32(&NtMap->ReferenceCount, 1);
    return NtMap;
}

static
VOID
CcNtUnlinkMap(
    _Inout_ PCC_NT_MAP NtMap)
{
    if (NtMap->Pointers->SharedCacheMap == NtMap)
        NtMap->Pointers->SharedCacheMap = NULL;
    if (NtMap->Closing)
    {
        NtMap->Closing = FALSE;
        CC_ATOMIC_ADD32(&CcNtClosedMapCount, -1);
    }
    RemoveEntryList(&NtMap->Link);
    InitializeListHead(&NtMap->Link);
}

VOID
CcNtDereferenceMap(
    _Inout_ PCC_NT_MAP NtMap)
{
    PCC_NT_MAP_BUCKET Bucket;
    BOOLEAN Destroy = FALSE;
    KIRQL OldIrql;
    LONG Count = CC_ATOMIC_READ32(&NtMap->ReferenceCount);

    while (Count > 1)
    {
        LONG Previous = CC_ATOMIC_CAS32(&NtMap->ReferenceCount, Count - 1, Count);

        if (Previous == Count)
            return;
        Count = Previous;
    }

    Bucket = CcNtMapBucket(NtMap->Pointers);
    CC_LOCK_ACQUIRE(&Bucket->Lock, &OldIrql);
    if (CC_ATOMIC_ADD32(&NtMap->ReferenceCount, -1) == 1 && NtMap->Map.OpenCount == 0 &&
        CC_ATOMIC_READ64(&NtMap->Map.DirtyPages) == 0)
    {
        if (NtMap->UninitializeEvent == NULL && !NtMap->PurgeOnClose && NtMap->Map.FileSize != 0 &&
            (NtMap->Closing || CC_ATOMIC_READ32(&CcNtClosedMapCount) < 128))
        {
            if (!NtMap->Closing)
            {
                NtMap->Closing = TRUE;
                CC_ATOMIC_ADD32(&CcNtClosedMapCount, 1);
            }
        }
        else
        {
            CcNtUnlinkMap(NtMap);
            Destroy = TRUE;
        }
    }
    CC_LOCK_RELEASE(&Bucket->Lock, OldIrql);

    if (Destroy)
        CcNtDestroyMap(NtMap);
}

VOID
CcNtExpireClosedMaps(VOID)
{
    ULONG Remaining = 128;
    ULONG Index;

    for (Index = 0; (Index < CC_NT_MAP_BUCKETS) && (Remaining != 0); Index++)
    {
        PCC_NT_MAP_BUCKET Bucket = &CcNtMapBuckets[Index];
        PCC_NT_MAP Expired[16];
        ULONG Batch = min(Remaining, RTL_NUMBER_OF(Expired));
        ULONG Found = 0;
        ULONG Destroyed;
        PLIST_ENTRY Entry, Next;
        KIRQL OldIrql;

        if (CC_ATOMIC_READ32(&CcNtClosedMapCount) == 0)
            break;

        CC_LOCK_ACQUIRE(&Bucket->Lock, &OldIrql);
        for (Entry = Bucket->List.Flink; (Entry != &Bucket->List) && (Found < Batch); Entry = Next)
        {
            PCC_NT_MAP NtMap = CONTAINING_RECORD(Entry, CC_NT_MAP, Link);

            Next = Entry->Flink;
            if (NtMap->Closing && NtMap->ReferenceCount == 0 && NtMap->Map.OpenCount == 0 &&
                CC_ATOMIC_READ64(&NtMap->Map.DirtyPages) == 0)
            {
                CcNtUnlinkMap(NtMap);
                Expired[Found++] = NtMap;
            }
        }
        CC_LOCK_RELEASE(&Bucket->Lock, OldIrql);

        for (Destroyed = 0; Destroyed < Found; Destroyed++)
            CcNtDestroyMap(Expired[Destroyed]);

        Remaining -= Found;
        if (Found == Batch)
            Index--;
    }
}

VOID
CcNtMapBarrier(
    _In_ PCC_NT_MAP NtMap)
{
    PCC_NT_MAP_BUCKET Bucket = CcNtMapBucket(NtMap->Pointers);
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Bucket->Lock, &OldIrql);
    CC_LOCK_RELEASE(&Bucket->Lock, OldIrql);
}

static
VOID
CcNtCompleteDestroyMap(
    _In_opt_ PVOID Context)
{
    PCC_NT_MAP NtMap = Context;
    PMI_SEGMENT Segment = NtMap->Control->Segment;
    ULONG64 FileSize = NtMap->Map.FileSize;
    ULONG64 FlushSize = FileSize;
    KEVENT *Event = NtMap->UninitializeEvent;
    BOOLEAN Uninitialized;
    PLIST_ENTRY Entry;

    for (Entry = NtMap->Map.BcbList.Flink; Entry != &NtMap->Map.BcbList; Entry = Entry->Flink)
    {
        PCC_BCB Bcb = CONTAINING_RECORD(Entry, CC_BCB, Link);

        if (Bcb->Dirty)
        {
            CcDirtyMark(&NtMap->Map, Bcb->FileOffset, Bcb->Length);
            if (Bcb->FileOffset + Bcb->Length > FlushSize)
                FlushSize = Bcb->FileOffset + Bcb->Length;
        }
    }

    if (FlushSize != 0)
        CcNtFlushMap(NtMap, 0, FlushSize, ~0u, NULL);

    Uninitialized = CcMapUninitialize(&NtMap->Map);
    ASSERT(Uninitialized);
    MiSegmentPurge(Segment, ROUND_UP(FileSize, (ULONG64)PAGE_SIZE), 0);
    MiSegmentFlush(Segment, 0, FileSize);
    if (Event != NULL || FileSize == 0 || NtMap->PurgeOnClose)
        MiSegmentDereferenceAndClose(Segment);
    else
        MiSegmentDereference(Segment);

    ObDereferenceObject(NtMap->FileObject);
    CcNtMapBarrier(NtMap);
    ExFreePoolWithTag(NtMap, CC_NT_MAP_TAG);

    if (Event != NULL)
        KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
}

static
VOID
CcNtDrainMapReads(
    _In_opt_ PVOID Context)
{
    PCC_NT_MAP NtMap = Context;

    NtMap->ReadDrain.Complete = CcNtCompleteDestroyMap;
    NtMap->ReadDrain.Context = NtMap;
    MiSegmentDrainReads(NtMap->Control->Segment, &NtMap->ReadDrain);
}

VOID
CcNtDestroyMap(
    _Inout_ PCC_NT_MAP NtMap)
{
    CcMapDrainReclaims(&NtMap->Map, CcNtDrainMapReads, NtMap);
}
