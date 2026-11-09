/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Lookaside lists, which keep freed blocks of one size to hand back
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY ExSystemLookasideListHead;
LIST_ENTRY ExPoolLookasideListHead;
LIST_ENTRY ExpNonPagedLookasideListHead;
LIST_ENTRY ExpPagedLookasideListHead;
KSPIN_LOCK ExpNonPagedLookasideListLock;
KSPIN_LOCK ExpPagedLookasideListLock;

GENERAL_LOOKASIDE ExpSmallNPagedPoolLookasideLists[NUMBER_POOL_LOOKASIDE_LISTS];
GENERAL_LOOKASIDE ExpSmallPagedPoolLookasideLists[NUMBER_POOL_LOOKASIDE_LISTS];

/* PRIVATE DATA ***************************************************************/

#define EXP_LOOKASIDE_DEFAULT_DEPTH 4
#define EXP_LOOKASIDE_MINIMUM_DEPTH 4
#define EXP_LOOKASIDE_POOL_TAG 'looP'

/*
 * How the depth of a list is tuned. A list that is missing often is given room
 * to hold more; a list that is being asked for rarely gives room back. The
 * rates are counted against the allocations made since the last pass, so a list
 * that has gone quiet is not judged on what it did long ago.
 */

/* Allocations in one pass below which a list is left alone */
#define EXP_LOOKASIDE_MINIMUM_ALLOCATIONS 25

/* One miss in this many allocations or worse: the list grows */
#define EXP_LOOKASIDE_GROW_RATIO 25

/* One miss in this many allocations or better: the list shrinks */
#define EXP_LOOKASIDE_SHRINK_RATIO 100

/* How much a list grows or shrinks in one pass */
#define EXP_LOOKASIDE_DEPTH_STEP 2

/* PRIVATE FUNCTIONS **********************************************************/

static
PVOID
NTAPI
ExpAllocateNonPaged(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag)
{
    return ExAllocatePoolWithTag(PoolType, NumberOfBytes, Tag);
}

static
PVOID
NTAPI
ExpLookasideAllocateEx(
    _In_ POOL_TYPE PoolType,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag,
    _Inout_ PLOOKASIDE_LIST_EX Lookaside)
{
    UNREFERENCED_PARAMETER(Lookaside);

    return ExAllocatePoolWithTag(PoolType, NumberOfBytes, Tag);
}

static
VOID
NTAPI
ExpLookasideFreeEx(
    _In_ PVOID Buffer,
    _Inout_ PLOOKASIDE_LIST_EX Lookaside)
{
    UNREFERENCED_PARAMETER(Lookaside);

    ExFreePool(Buffer);
}

static
VOID
NTAPI
ExpLookasideFree(
    _In_ PVOID Buffer)
{
    ExFreePool(Buffer);
}

static
VOID
ExpInitializeLookasideCommon(
    _Out_ PGENERAL_LOOKASIDE_POOL Lookaside,
    _In_ POOL_TYPE PoolType,
    _In_ ULONG Flags,
    _In_ SIZE_T Size,
    _In_ ULONG Tag,
    _In_ USHORT Depth)
{
    RtlZeroMemory(Lookaside, sizeof(*Lookaside));

    InitializeSListHead(&Lookaside->ListHead);

    Lookaside->Depth = EXP_LOOKASIDE_MINIMUM_DEPTH;
    Lookaside->MaximumDepth = (Depth != 0) ? Depth : 256;
    Lookaside->TotalAllocates = 0;
    Lookaside->AllocateMisses = 0;
    Lookaside->TotalFrees = 0;
    Lookaside->FreeMisses = 0;
    Lookaside->Type = PoolType | (POOL_TYPE)(Flags & POOL_RAISE_IF_ALLOCATION_FAILURE);
    Lookaside->Tag = Tag;
    Lookaside->Size = (ULONG)Size;
    Lookaside->LastTotalAllocates = 0;
    Lookaside->LastAllocateMisses = 0;
}

/*
 * Empties a list down to a given depth, handing every block it was holding back
 * to the pool through the list's own free routine.
 */
static
VOID
ExpDrainLookasideListEx(
    _Inout_ PLOOKASIDE_LIST_EX Lookaside,
    _In_ USHORT Depth)
{
    PSLIST_ENTRY Entry;

    while (ExQueryDepthSList(&Lookaside->L.ListHead) > Depth)
    {
        Entry = InterlockedPopEntrySList(&Lookaside->L.ListHead);
        if (Entry == NULL)
        {
            return;
        }

        Lookaside->L.FreeEx(Entry, Lookaside);
    }
}

static
VOID
ExpDrainLookasideList(
    _Inout_ PGENERAL_LOOKASIDE_POOL Lookaside)
{
    PSLIST_ENTRY Entry;

    for (;;)
    {
        Entry = InterlockedPopEntrySList(&Lookaside->ListHead);
        if (Entry == NULL)
        {
            return;
        }

        Lookaside->Free(Entry);
    }
}

/*
 * Judges one list on what it did since the last pass and moves its depth. The
 * blocks a shrunken list is still holding above its new depth are given back on
 * the next free, so nothing has to be handed back here.
 */
static
VOID
ExpTuneLookasideDepth(
    _Inout_ PGENERAL_LOOKASIDE_POOL Lookaside)
{
    ULONG Allocates;
    ULONG Misses;
    USHORT Depth;

    Allocates = Lookaside->TotalAllocates - Lookaside->LastTotalAllocates;
    Misses = Lookaside->AllocateMisses - Lookaside->LastAllocateMisses;

    Lookaside->LastTotalAllocates = Lookaside->TotalAllocates;
    Lookaside->LastAllocateMisses = Lookaside->AllocateMisses;

    if (Allocates < EXP_LOOKASIDE_MINIMUM_ALLOCATIONS)
    {
        return;
    }

    Depth = Lookaside->Depth;

    if ((Misses != 0) && ((Allocates / Misses) <= EXP_LOOKASIDE_GROW_RATIO))
    {
        Depth = (USHORT)(Depth + EXP_LOOKASIDE_DEPTH_STEP);

        if (Depth > Lookaside->MaximumDepth)
        {
            Depth = Lookaside->MaximumDepth;
        }
    }
    else if ((Misses == 0) ||
             ((Allocates / Misses) >= EXP_LOOKASIDE_SHRINK_RATIO))
    {
        if (Depth > EXP_LOOKASIDE_MINIMUM_DEPTH + EXP_LOOKASIDE_DEPTH_STEP)
        {
            Depth = (USHORT)(Depth - EXP_LOOKASIDE_DEPTH_STEP);
        }
        else
        {
            Depth = EXP_LOOKASIDE_MINIMUM_DEPTH;
        }
    }

    Lookaside->Depth = Depth;
}

static
VOID
ExpTuneLookasideList(
    _In_ PLIST_ENTRY ListHead,
    _Inout_opt_ PKSPIN_LOCK Lock)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql = PASSIVE_LEVEL;

    if (Lock != NULL)
    {
        KeAcquireSpinLock(Lock, &OldIrql);
    }

    for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
    {
        ExpTuneLookasideDepth(CONTAINING_RECORD(Entry,
                                                GENERAL_LOOKASIDE_POOL,
                                                ListEntry));
    }

    if (Lock != NULL)
    {
        KeReleaseSpinLock(Lock, OldIrql);
    }
}

static
VOID
ExpUnlinkLookaside(
    _Inout_ PLIST_ENTRY Entry,
    _Inout_ PKSPIN_LOCK Lock)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(Lock, &OldIrql);
    RemoveEntryList(Entry);
    KeReleaseSpinLock(Lock, OldIrql);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
ExInitializeSystemLookasideList(
    _Out_ PGENERAL_LOOKASIDE List,
    _In_ POOL_TYPE Type,
    _In_ ULONG Size,
    _In_ ULONG Tag,
    _In_ USHORT MaximumDepth,
    _In_ PLIST_ENTRY ListHead)
{
    RtlZeroMemory(List, sizeof(GENERAL_LOOKASIDE));

    InitializeSListHead(&List->ListHead);

    List->Depth = EXP_LOOKASIDE_MINIMUM_DEPTH;
    List->MaximumDepth = MaximumDepth;
    List->Type = Type;
    List->Tag = Tag;
    List->Size = Size;
    List->Allocate = ExpAllocateNonPaged;
    List->Free = ExpLookasideFree;

    InsertHeadList(ListHead, &List->ListEntry);
}

CODE_SEG("INIT")
VOID
NTAPI
ExpInitLookasideLists(VOID)
{
    ULONG Index;

    InitializeListHead(&ExSystemLookasideListHead);
    InitializeListHead(&ExPoolLookasideListHead);
    InitializeListHead(&ExpNonPagedLookasideListHead);
    InitializeListHead(&ExpPagedLookasideListHead);

    KeInitializeSpinLock(&ExpNonPagedLookasideListLock);
    KeInitializeSpinLock(&ExpPagedLookasideListLock);

    for (Index = 0; Index < NUMBER_POOL_LOOKASIDE_LISTS; Index++)
    {
        ExInitializeSystemLookasideList(&ExpSmallNPagedPoolLookasideLists[Index],
                                        NonPagedPool,
                                        (Index + 1) * MEMORY_ALLOCATION_ALIGNMENT,
                                        EXP_LOOKASIDE_POOL_TAG,
                                        256,
                                        &ExPoolLookasideListHead);

        ExInitializeSystemLookasideList(&ExpSmallPagedPoolLookasideLists[Index],
                                        PagedPool,
                                        (Index + 1) * MEMORY_ALLOCATION_ALIGNMENT,
                                        EXP_LOOKASIDE_POOL_TAG,
                                        256,
                                        &ExPoolLookasideListHead);
    }
}

/*
 * Gives every processor the small-block lists the pool reaches through its own
 * control block, so a small allocation never leaves the processor it is made on.
 * Called once on each processor as it starts.
 */
CODE_SEG("INIT")
VOID
NTAPI
ExInitPoolLookasidePointers(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG Index;

    for (Index = 0; Index < NUMBER_POOL_LOOKASIDE_LISTS; Index++)
    {
        PGENERAL_LOOKASIDE_POOL Paged = &Prcb->PPPagedLookasideList[Index];
        PGENERAL_LOOKASIDE_POOL NonPaged = &Prcb->PPNPagedLookasideList[Index];

        ExpInitializeLookasideCommon(Paged,
                                     PagedPool,
                                     0,
                                     (Index + 1) * MEMORY_ALLOCATION_ALIGNMENT,
                                     EXP_LOOKASIDE_POOL_TAG,
                                     256);
        Paged->Allocate = ExpAllocateNonPaged;
        Paged->Free = ExpLookasideFree;

        ExpInitializeLookasideCommon(NonPaged,
                                     NonPagedPool,
                                     0,
                                     (Index + 1) * MEMORY_ALLOCATION_ALIGNMENT,
                                     EXP_LOOKASIDE_POOL_TAG,
                                     256);
        NonPaged->Allocate = ExpAllocateNonPaged;
        NonPaged->Free = ExpLookasideFree;
    }
}

NTSTATUS
NTAPI
ExInitializeLookasideListEx(
    _Out_ PLOOKASIDE_LIST_EX Lookaside,
    _In_opt_ PALLOCATE_FUNCTION_EX Allocate,
    _In_opt_ PFREE_FUNCTION_EX Free,
    _In_ POOL_TYPE PoolType,
    _In_ ULONG Flags,
    _In_ SIZE_T Size,
    _In_ ULONG Tag,
    _In_ USHORT Depth)
{
    PLIST_ENTRY ListHead;
    PKSPIN_LOCK Lock;
    KIRQL OldIrql;

    if ((Flags & ~POOL_RAISE_IF_ALLOCATION_FAILURE) != 0)
    {
        return STATUS_INVALID_PARAMETER_5;
    }

    if ((PoolType != NonPagedPool) && (PoolType != PagedPool))
    {
        return STATUS_INVALID_PARAMETER_4;
    }

    ExpInitializeLookasideCommon(&Lookaside->L,
                                 PoolType,
                                 Flags,
                                 Size,
                                 Tag,
                                 Depth);

    Lookaside->L.AllocateEx = (Allocate != NULL) ? Allocate :
                                                   ExpLookasideAllocateEx;
    Lookaside->L.FreeEx = (Free != NULL) ? Free : ExpLookasideFreeEx;

    if (PoolType == PagedPool)
    {
        ListHead = &ExpPagedLookasideListHead;
        Lock = &ExpPagedLookasideListLock;
    }
    else
    {
        ListHead = &ExpNonPagedLookasideListHead;
        Lock = &ExpNonPagedLookasideListLock;
    }

    KeAcquireSpinLock(Lock, &OldIrql);
    InsertTailList(ListHead, &Lookaside->L.ListEntry);
    KeReleaseSpinLock(Lock, OldIrql);

    return STATUS_SUCCESS;
}

VOID
NTAPI
ExDeleteLookasideListEx(
    _Inout_ PLOOKASIDE_LIST_EX Lookaside)
{
    PKSPIN_LOCK Lock;

    ExpDrainLookasideListEx(Lookaside, 0);

    Lock = (Lookaside->L.Type == PagedPool) ? &ExpPagedLookasideListLock :
                                              &ExpNonPagedLookasideListLock;

    ExpUnlinkLookaside(&Lookaside->L.ListEntry, Lock);
}

VOID
NTAPI
ExFlushLookasideListEx(
    _Inout_ PLOOKASIDE_LIST_EX Lookaside)
{
    ExpDrainLookasideListEx(Lookaside, 0);
}

/*
 * Goes over every list the system keeps and moves each one's depth to suit how
 * it is being used. Called from the worker thread that balances the system, so
 * it runs seldom and may take the list locks.
 */
VOID
NTAPI
ExAdjustLookasideDepth(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG Index;

    ExpTuneLookasideList(&ExpNonPagedLookasideListHead,
                         &ExpNonPagedLookasideListLock);
    ExpTuneLookasideList(&ExpPagedLookasideListHead,
                         &ExpPagedLookasideListLock);
    ExpTuneLookasideList(&ExSystemLookasideListHead, NULL);
    ExpTuneLookasideList(&ExPoolLookasideListHead, NULL);

    /*
     * The per-processor pool lists are reached through the processor's own
     * control block and are only ever touched by that processor, so the one
     * this pass is running on is the one it tunes.
     */
    for (Index = 0; Index < NUMBER_POOL_LOOKASIDE_LISTS; Index++)
    {
        ExpTuneLookasideDepth(&Prcb->PPPagedLookasideList[Index]);
        ExpTuneLookasideDepth(&Prcb->PPNPagedLookasideList[Index]);
    }
}

VOID
NTAPI
ExInitializePagedLookasideList(
    _Out_ PPAGED_LOOKASIDE_LIST Lookaside,
    _In_opt_ PALLOCATE_FUNCTION Allocate,
    _In_opt_ PFREE_FUNCTION Free,
    _In_ ULONG Flags,
    _In_ SIZE_T Size,
    _In_ ULONG Tag,
    _In_ USHORT Depth)
{
    KIRQL OldIrql;

    ExpInitializeLookasideCommon((PGENERAL_LOOKASIDE_POOL)&Lookaside->L,
                                 PagedPool,
                                 Flags,
                                 Size,
                                 Tag,
                                 Depth);

    Lookaside->L.Allocate = (Allocate != NULL) ? Allocate : ExpAllocateNonPaged;
    Lookaside->L.Free = (Free != NULL) ? Free : ExpLookasideFree;

    KeAcquireSpinLock(&ExpPagedLookasideListLock, &OldIrql);
    InsertTailList(&ExpPagedLookasideListHead, &Lookaside->L.ListEntry);
    KeReleaseSpinLock(&ExpPagedLookasideListLock, OldIrql);
}

VOID
NTAPI
ExDeletePagedLookasideList(
    _Inout_ PPAGED_LOOKASIDE_LIST Lookaside)
{
    ExpDrainLookasideList((PGENERAL_LOOKASIDE_POOL)&Lookaside->L);

    ExpUnlinkLookaside(&Lookaside->L.ListEntry, &ExpPagedLookasideListLock);
}

VOID
NTAPI
ExInitializeNPagedLookasideList(
    _Out_ PNPAGED_LOOKASIDE_LIST Lookaside,
    _In_opt_ PALLOCATE_FUNCTION Allocate,
    _In_opt_ PFREE_FUNCTION Free,
    _In_ ULONG Flags,
    _In_ SIZE_T Size,
    _In_ ULONG Tag,
    _In_ USHORT Depth)
{
    KIRQL OldIrql;

    ExpInitializeLookasideCommon((PGENERAL_LOOKASIDE_POOL)&Lookaside->L,
                                 NonPagedPool,
                                 Flags,
                                 Size,
                                 Tag,
                                 Depth);

    Lookaside->L.Allocate = (Allocate != NULL) ? Allocate : ExpAllocateNonPaged;
    Lookaside->L.Free = (Free != NULL) ? Free : ExpLookasideFree;

    KeAcquireSpinLock(&ExpNonPagedLookasideListLock, &OldIrql);
    InsertTailList(&ExpNonPagedLookasideListHead, &Lookaside->L.ListEntry);
    KeReleaseSpinLock(&ExpNonPagedLookasideListLock, OldIrql);
}

VOID
NTAPI
ExDeleteNPagedLookasideList(
    _Inout_ PNPAGED_LOOKASIDE_LIST Lookaside)
{
    ExpDrainLookasideList((PGENERAL_LOOKASIDE_POOL)&Lookaside->L);

    ExpUnlinkLookaside(&Lookaside->L.ListEntry, &ExpNonPagedLookasideListLock);
}

/*
 * The 64-bit builds export the allocate and free paths as real calls rather than
 * the inline forms, because a driver built for either word size has to find them
 * in the image.
 */
PVOID
NTAPI
ExiAllocateFromLookasideListEx(
    _Inout_ PLOOKASIDE_LIST_EX Lookaside)
{
    PVOID Entry;

    Lookaside->L.TotalAllocates++;

    Entry = InterlockedPopEntrySList(&Lookaside->L.ListHead);
    if (Entry == NULL)
    {
        Lookaside->L.AllocateMisses++;
        Entry = Lookaside->L.AllocateEx(Lookaside->L.Type,
                                        Lookaside->L.Size,
                                        Lookaside->L.Tag,
                                        Lookaside);
    }

    return Entry;
}

VOID
NTAPI
ExpFreeToLookasideListExExport(
    _Inout_ PLOOKASIDE_LIST_EX Lookaside,
    _In_ PVOID Entry)
{
    Lookaside->L.TotalFrees++;

    if (ExQueryDepthSList(&Lookaside->L.ListHead) >= Lookaside->L.Depth)
    {
        Lookaside->L.FreeMisses++;
        Lookaside->L.FreeEx(Entry, Lookaside);
        return;
    }

    InterlockedPushEntrySList(&Lookaside->L.ListHead, (PSLIST_ENTRY)Entry);
}

PVOID
NTAPI
ExiAllocateFromNPagedLookasideList(
    _Inout_ PNPAGED_LOOKASIDE_LIST Lookaside)
{
    PVOID Entry;

    Lookaside->L.TotalAllocates++;

    Entry = InterlockedPopEntrySList(&Lookaside->L.ListHead);
    if (Entry == NULL)
    {
        Lookaside->L.AllocateMisses++;
        Entry = Lookaside->L.Allocate(Lookaside->L.Type,
                                      Lookaside->L.Size,
                                      Lookaside->L.Tag);
    }

    return Entry;
}

VOID
NTAPI
ExpFreeToNPagedLookasideListExport(
    _Inout_ PNPAGED_LOOKASIDE_LIST Lookaside,
    _In_ PVOID Entry)
{
    Lookaside->L.TotalFrees++;

    if (ExQueryDepthSList(&Lookaside->L.ListHead) >= Lookaside->L.Depth)
    {
        Lookaside->L.FreeMisses++;
        Lookaside->L.Free(Entry);
        return;
    }

    InterlockedPushEntrySList(&Lookaside->L.ListHead, (PSLIST_ENTRY)Entry);
}

PVOID
NTAPI
ExiAllocateFromPagedLookasideList(
    _Inout_ PPAGED_LOOKASIDE_LIST Lookaside)
{
    PVOID Entry;

    Lookaside->L.TotalAllocates++;

    Entry = InterlockedPopEntrySList(&Lookaside->L.ListHead);
    if (Entry == NULL)
    {
        Lookaside->L.AllocateMisses++;
        Entry = Lookaside->L.Allocate(Lookaside->L.Type,
                                      Lookaside->L.Size,
                                      Lookaside->L.Tag);
    }

    return Entry;
}

VOID
NTAPI
ExiFreeToPagedLookasideList(
    _Inout_ PPAGED_LOOKASIDE_LIST Lookaside,
    _In_ PVOID Entry)
{
    Lookaside->L.TotalFrees++;

    if (ExQueryDepthSList(&Lookaside->L.ListHead) >= Lookaside->L.Depth)
    {
        Lookaside->L.FreeMisses++;
        Lookaside->L.Free(Entry);
        return;
    }

    InterlockedPushEntrySList(&Lookaside->L.ListHead, (PSLIST_ENTRY)Entry);
}

/* EOF */
