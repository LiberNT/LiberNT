/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/sys/syspte.c
 * PURPOSE:     System page-table entry allocation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/misys.h>

static
ULONG
MiSysPteClass(
    _In_ ULONG PageCount)
{
    ULONG Class = 0;

    while ((1u << Class) < PageCount)
        Class++;

    return Class;
}

static
PMI_SYSPTE_CHUNK
MiSysPteChunkOf(
    _In_ PMI_SYSTEM_PTES Ptes,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 Count)
{
    ULONG Index;

    for (Index = 0; Index < Ptes->ChunkCount; Index++)
    {
        PMI_SYSPTE_CHUNK Chunk = &Ptes->Chunk[Index];

        if (VirtualAddress >= Chunk->Base && Count <= Chunk->PageCount &&
            ((VirtualAddress - Chunk->Base) >> PAGE_SHIFT) <= Chunk->PageCount - Count)
        {
            return Chunk;
        }
    }

    return NULL;
}

/* Track allocated extents instead of allocating one bit for every possible
 * kernel VA page. Small runs retain the existing per-CPU caches. */
static
ULONG64
MiSysPteRangeReserve(
    _Inout_ PMI_SYSTEM_PTES Ptes,
    _In_ ULONG64 Count,
    _In_ ULONG64 Alignment,
    _Out_ PULONG SeenChunks,
    _Out_ NTSTATUS *FailureStatus)
{
    PMI_VAD_NODE Node = MI_ALLOCATE(sizeof(*Node));
    ULONG64 Va = 0;
    ULONG64 Index;
    ULONG ChunkIndex;
    KIRQL OldIrql;

    *SeenChunks = 0;
    *FailureStatus = STATUS_INSUFFICIENT_RESOURCES;
    if (Node == NULL)
    {
        *FailureStatus = STATUS_NO_MEMORY;
        return 0;
    }
    RtlZeroMemory(Node, sizeof(*Node));

    MI_SPIN_ACQUIRE(&Ptes->Lock, &OldIrql);
    *SeenChunks = Ptes->ChunkCount;
    for (ChunkIndex = 0; ChunkIndex < Ptes->ChunkCount; ChunkIndex++)
    {
        PMI_SYSPTE_CHUNK Chunk = &Ptes->Chunk[ChunkIndex];

        if (Count > Chunk->PageCount || !MiVadFindEmptyRange(&Chunk->Allocations, Count, Alignment, &Index))
            continue;

        MI_ASSERT(Index <= Chunk->PageCount - Count);
        MI_ASSERT(Alignment != 0 && Index % Alignment == 0);
        Node->StartingVpn = Index;
        Node->EndingVpn = Index + Count - 1;
        if (MiVadInsert(&Chunk->Allocations, Node))
            Va = Chunk->Base + (Index << PAGE_SHIFT);
        break;
    }
    MI_SPIN_RELEASE(&Ptes->Lock, OldIrql);

    if (Va == 0)
        MI_FREE(Node);
    return Va;
}

static
VOID
MiSysPteRangeRelease(
    _Inout_ PMI_SYSTEM_PTES Ptes,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 Count)
{
    PMI_SYSPTE_CHUNK Chunk;
    PMI_VAD_NODE Node;
    ULONG64 Start;
    KIRQL OldIrql;

    MI_SPIN_ACQUIRE(&Ptes->Lock, &OldIrql);
    Chunk = MiSysPteChunkOf(Ptes, VirtualAddress, Count);
    MI_ASSERT(Chunk != NULL);
    Start = (VirtualAddress - Chunk->Base) >> PAGE_SHIFT;
    Node = MiVadFind(&Chunk->Allocations, Start);
    MI_ASSERT(Node != NULL && Node->StartingVpn == Start && Node->EndingVpn == Start + Count - 1);
    MiVadRemove(&Chunk->Allocations, Node);
    MI_SPIN_RELEASE(&Ptes->Lock, OldIrql);
    MI_FREE(Node);
}

static
NTSTATUS
MiSysPteAddChunk(
    _Inout_ PMI_SYSTEM_PTES Ptes,
    _In_ ULONG64 PageCount)
{
    PMI_SYSTEM System = Ptes->System;
    ULONG64 Span = 1ULL << System->Arch->Level[System->Arch->PagingLevels - 1].Shift;
    PMI_SYSPTE_CHUNK Chunk;
    ULONG64 Base, End, Va;
    KIRQL OldIrql;
    NTSTATUS Status;
    PMI_VAD Vad;

    MI_ASSERT(PageCount != 0);
    if (Ptes->ChunkCount == MI_SYSPTE_CHUNKS)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = MiSystemRegionCreate(System, PageCount, FALSE, &Vad, &Base);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Process roots copy the kernel's top-level entries on AMD64. Seed one
     * leaf path per top-level span before processes can be created, so later
     * table population remains visible through those shared entries. This
     * needs only a few table pages, not tables for every page of the arena. */
    End = Base + (PageCount << PAGE_SHIFT);
    for (Va = Base; Va < End; Va = (Va & ~(Span - 1)) + Span)
    {
        Status = MiPtPinSystemRange(&System->SystemSpace, Va, PAGE_SIZE, NULL, NULL);
        if (!NT_SUCCESS(Status))
        {
            MiSystemRegionDelete(System, Vad, TRUE);
            return Status;
        }
    }

    MI_SPIN_ACQUIRE(&Ptes->Lock, &OldIrql);
    MI_ASSERT(Ptes->ChunkCount < MI_SYSPTE_CHUNKS);
    Chunk = &Ptes->Chunk[Ptes->ChunkCount];
    Chunk->Vad = Vad;
    Chunk->Base = Base;
    Chunk->PageCount = PageCount;
    MiVadRootInitialize(&Chunk->Allocations, 0, PageCount - 1);
    Ptes->ChunkCount++;
    Ptes->PageCount += PageCount;
    MI_SPIN_RELEASE(&Ptes->Lock, OldIrql);
    MI_ATOMIC_ADD64(&Ptes->FreePages, (LONG64)PageCount);
    return STATUS_SUCCESS;
}

static
BOOLEAN
MiSysPteGrow(
    _Inout_ PMI_SYSTEM_PTES Ptes,
    _In_ ULONG64 Count,
    _In_ ULONG SeenChunks)
{
    ULONG64 Pages;
    BOOLEAN Grown = FALSE;

    if (!MI_CAN_WAIT() || Ptes->GrowPages == 0)
        return FALSE;

    MI_MUTEX_ACQUIRE(&Ptes->GrowLock);
    if (MI_PEEK(Ptes->ChunkCount) != SeenChunks)
    {
        Grown = TRUE;
    }
    else
    {
        Pages = (Count > Ptes->GrowPages) ? Count : Ptes->GrowPages;
        if (Pages > Ptes->PageLimit - Ptes->PageCount)
            Pages = Ptes->PageLimit - Ptes->PageCount;
        if (Pages >= Count && NT_SUCCESS(MiSysPteAddChunk(Ptes, Pages)))
            Grown = TRUE;
    }
    MI_MUTEX_RELEASE(&Ptes->GrowLock);
    return Grown;
}

NTSTATUS
MiSystemPtesInitialize(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 PageCount,
    _In_ ULONG CpuCount)
{
    PMI_SYSTEM_PTES Ptes;
    NTSTATUS Status;
    ULONG i;

    if (PageCount == 0 || PageCount > (~0ULL >> PAGE_SHIFT))
        return STATUS_INVALID_PARAMETER;

    Ptes = MI_ALLOCATE(sizeof(*Ptes));
    if (Ptes == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Ptes, sizeof(*Ptes));
    Ptes->System = System;
    Ptes->PageLimit = PageCount;
    Ptes->CacheCount = (CpuCount == 0) ? 1 : ((CpuCount > MI_SYSPTE_CPU_CACHES) ? MI_SYSPTE_CPU_CACHES : CpuCount);
    Ptes->DefaultStackPages = 6;
    MI_SPIN_INIT(&Ptes->Lock);
    MI_SPIN_INIT(&Ptes->PopulateLock);
    MI_MUTEX_INIT(&Ptes->GrowLock);

    for (i = 0; i < MI_SYSPTE_CPU_CACHES; i++)
        MI_SPIN_INIT(&Ptes->Cache[i].Lock);

    Status = MiSysPteAddChunk(Ptes, PageCount);
    if (!NT_SUCCESS(Status))
    {
        MI_FREE(Ptes);
        return Status;
    }

    System->SystemPtes = Ptes;
    return STATUS_SUCCESS;
}

VOID
MiSystemPtesAllowGrowth(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 PageLimit,
    _In_ ULONG64 GrowPages)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;

    MI_ASSERT(Ptes != NULL && Ptes->ChunkCount == 1);
    MI_ASSERT(PageLimit >= Ptes->PageCount && GrowPages != 0);
    Ptes->PageLimit = PageLimit;
    Ptes->GrowPages = GrowPages;
}

BOOLEAN
MiSystemPtesContain(
    _In_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 PageCount)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    BOOLEAN Found;
    KIRQL OldIrql;

    if (Ptes == NULL)
        return FALSE;

    MI_SPIN_ACQUIRE(&Ptes->Lock, &OldIrql);
    Found = (BOOLEAN)(MiSysPteChunkOf(Ptes, VirtualAddress, PageCount) != NULL);
    MI_SPIN_RELEASE(&Ptes->Lock, OldIrql);
    return Found;
}

VOID
MiSystemPtesUninitialize(
    _Inout_ PMI_SYSTEM System)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG Cpu, Class;

    if (Ptes == NULL)
        return;

    while (Ptes->SpareTableCount != 0)
        MiPfnFreePage(&System->Pfn, Ptes->SpareTable[--Ptes->SpareTableCount]);

    for (Cpu = 0; Cpu < MI_SYSPTE_CPU_CACHES; Cpu++)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[Cpu];

        while (Cache->StackDepth != 0)
        {
            ULONG64 Top = Cache->Stack[--Cache->StackDepth];
            ULONG64 Base = Top - (ULONG64)Ptes->DefaultStackPages * PAGE_SIZE;

            MiSystemUnmap(System, Base, Ptes->DefaultStackPages, TRUE);
            MiReturnCommit(&System->SystemSpace, Ptes->DefaultStackPages);
            MiReleaseSystemPtes(System, Base - PAGE_SIZE, Ptes->DefaultStackPages + 1);
        }
    }

    for (Cpu = 0; Cpu < MI_SYSPTE_CPU_CACHES; Cpu++)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[Cpu];

        for (Class = 0; Class < MI_SYSPTE_CLASSES; Class++)
        {
            while (Cache->Depth[Class] != 0)
            {
                ULONG64 Va = Cache->Entry[Class][--Cache->Depth[Class]];

                MiSysPteRangeRelease(Ptes, Va, 1ULL << Class);
                MI_ATOMIC_ADD64(&Ptes->FreePages, (LONG64)(1ULL << Class));
            }
        }
    }

    MI_ASSERT(MI_ATOMIC_READ64(&Ptes->FreePages) == (LONG64)Ptes->PageCount);

    System->SystemPtes = NULL;
    while (Ptes->ChunkCount != 0)
    {
        PMI_SYSPTE_CHUNK Chunk = &Ptes->Chunk[--Ptes->ChunkCount];

        MI_ASSERT(Chunk->Allocations.NodeCount == 0);
        MiSystemRegionDelete(System, Chunk->Vad, TRUE);
    }
    MI_FREE(Ptes);
}

ULONG64
MiSystemPteCacheHits(
    _In_ PMI_SYSTEM System)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG64 Total = 0;
    ULONG i;

    for (i = 0; i < Ptes->CacheCount; i++)
    {
        KIRQL OldIrql;

        MI_SPIN_ACQUIRE(&Ptes->Cache[i].Lock, &OldIrql);
        Total += Ptes->Cache[i].CacheHits;
        MI_SPIN_RELEASE(&Ptes->Cache[i].Lock, OldIrql);
    }

    return Total;
}

NTSTATUS
MiReserveSystemPtesEx(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG PageCount,
    _Out_ PULONG64 Base)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG64 Pages = PageCount;
    ULONG64 Va;
    ULONG SeenChunks;
    NTSTATUS FailureStatus;

    *Base = 0;
    if (Ptes == NULL || PageCount == 0)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (PageCount <= MI_SYSPTE_CLASS_MAX_PAGES)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[MI_CURRENT_CPU() % Ptes->CacheCount];
        ULONG Class = MiSysPteClass(PageCount);
        ULONG64 Va = 0;
        KIRQL OldIrql;

        Pages = 1ULL << Class;

        MI_SPIN_ACQUIRE(&Cache->Lock, &OldIrql);
        MI_ASSERT(Cache->Depth[Class] <= MI_SYSPTE_CACHE_DEPTH);
        Cache->Reservations++;
        if (Cache->Depth[Class] != 0)
        {
            Va = Cache->Entry[Class][--Cache->Depth[Class]];
            Cache->CacheHits++;
        }
        MI_SPIN_RELEASE(&Cache->Lock, OldIrql);

        if (Va != 0)
        {
            MI_ASSERT((Va & (PAGE_SIZE - 1)) == 0 && MiSystemPtesContain(System, Va, Pages));
            *Base = Va;
            return STATUS_SUCCESS;
        }
    }

    for (;;)
    {
        Va = MiSysPteRangeReserve(Ptes, Pages, (Pages <= MI_SYSPTE_CLASS_MAX_PAGES) ? Pages : 1, &SeenChunks,
                                  &FailureStatus);
        if (Va != 0)
            break;
        if (FailureStatus != STATUS_INSUFFICIENT_RESOURCES || !MiSysPteGrow(Ptes, Pages, SeenChunks))
            return FailureStatus;
    }

    {
        KIRQL OldIrql;
        NTSTATUS Status;

        /* Prepare only this reservation. Keep touched tables resident across
         * releases so another CPU's mapping in the same table stays valid and
         * cache hits never need a pageable lock or a table allocation. */
        MI_SPIN_ACQUIRE(&Ptes->PopulateLock, &OldIrql);
        MI_ASSERT(Ptes->SpareTableCount <= MI_SYSPTE_SPARE_TABLES);
        Status = MiPtPinSystemRange(&System->SystemSpace,
                                    Va,
                                    Pages << PAGE_SHIFT,
                                    Ptes->SpareTable,
                                    &Ptes->SpareTableCount);
        MI_ASSERT(Ptes->SpareTableCount <= MI_SYSPTE_SPARE_TABLES);
        MI_SPIN_RELEASE(&Ptes->PopulateLock, OldIrql);
        if (!NT_SUCCESS(Status))
        {
            MiSysPteRangeRelease(Ptes, Va, Pages);
            return Status;
        }
    }
    MI_ATOMIC_ADD64(&Ptes->FreePages, -(LONG64)Pages);
    *Base = Va;
    return STATUS_SUCCESS;
}

ULONG
MiSystemPtesRefillTables(
    _Inout_ PMI_SYSTEM System)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG Added = 0;

    if (Ptes == NULL)
        return 0;

    while (MI_PEEK(Ptes->SpareTableCount) < MI_SYSPTE_SPARE_TABLES)
    {
        ULONG Frame = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
        BOOLEAN Stored = FALSE;
        KIRQL OldIrql;

        if (Frame == MI_FRAME_INVALID)
            break;

        MI_SPIN_ACQUIRE(&Ptes->PopulateLock, &OldIrql);
        MI_ASSERT(Ptes->SpareTableCount <= MI_SYSPTE_SPARE_TABLES);
        if (Ptes->SpareTableCount < MI_SYSPTE_SPARE_TABLES)
        {
            Ptes->SpareTable[Ptes->SpareTableCount++] = Frame;
            Stored = TRUE;
        }
        MI_SPIN_RELEASE(&Ptes->PopulateLock, OldIrql);

        if (!Stored)
        {
            MiPfnFreePage(&System->Pfn, Frame);
            break;
        }

        Added++;
    }

    return Added;
}

ULONG64
MiReserveSystemPtes(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG PageCount)
{
    ULONG64 Base;

    MiReserveSystemPtesEx(System, PageCount, &Base);
    return Base;
}

VOID
MiReleaseSystemPtes(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG PageCount)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG64 Pages = PageCount;

    MI_ASSERT((VirtualAddress & (PAGE_SIZE - 1)) == 0);
    MI_ASSERT(MiSystemPtesContain(System, VirtualAddress, PageCount));

    if (PageCount <= MI_SYSPTE_CLASS_MAX_PAGES)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[MI_CURRENT_CPU() % Ptes->CacheCount];
        ULONG Class = MiSysPteClass(PageCount);
        BOOLEAN Cached = FALSE;
        KIRQL OldIrql;

        Pages = 1ULL << Class;

        MI_SPIN_ACQUIRE(&Cache->Lock, &OldIrql);
        MI_ASSERT(Cache->Depth[Class] <= MI_SYSPTE_CACHE_DEPTH);
        if (Cache->Depth[Class] < MI_SYSPTE_CACHE_DEPTH)
        {
            Cache->Entry[Class][Cache->Depth[Class]++] = VirtualAddress;
            Cached = TRUE;
        }
        MI_SPIN_RELEASE(&Cache->Lock, OldIrql);

        if (Cached)
            return;
    }

    MiSysPteRangeRelease(Ptes, VirtualAddress, Pages);
    MI_ATOMIC_ADD64(&Ptes->FreePages, (LONG64)Pages);
}
