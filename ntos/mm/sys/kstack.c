/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/sys/kstack.c
 * PURPOSE:     Kernel stack allocation and management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/misys.h>

#if DBG || defined(MM_HOST_TEST)
static
VOID
MiAssertKernelStack(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 StackTop,
    _In_ ULONG ReservePages,
    _In_ ULONG CommittedPages)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG64 TopIndex;
    ULONG i;

    MI_ASSERT(Ptes != NULL && Ptes->CacheCount != 0 && Ptes->CacheCount <= MI_SYSPTE_CPU_CACHES);
    MI_ASSERT((StackTop & (PAGE_SIZE - 1)) == 0 && StackTop >= Ptes->Base);
    TopIndex = (StackTop - Ptes->Base) >> PAGE_SHIFT;
    MI_ASSERT(ReservePages != 0 && TopIndex >= (ULONG64)ReservePages + 1 && TopIndex <= Ptes->PageCount);
    MI_ASSERT(CommittedPages != 0 && CommittedPages <= ReservePages);

    for (i = 0; i <= ReservePages; i++)
    {
        PMI_PTE Slot = MiPtLookup(&System->SystemSpace, StackTop - ((ULONG64)i + 1) * PAGE_SIZE, NULL);
        MI_PTE Pte;

        MI_ASSERT(Slot != NULL);
        Pte = MiArchPteRead(Slot);
        if (i < CommittedPages)
            MI_ASSERT(MiArchPteIsValid(Pte) && MiArchPteIsWritable(Pte));
        else
            MI_ASSERT(Pte == 0);
    }
}
#else
#define MiAssertKernelStack(System, StackTop, ReservePages, CommittedPages) ((void)0)
#endif

static
NTSTATUS
MiCommitStackPages(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG Count)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;

    if (!MiChargeCommit(Space, Count))
        return STATUS_COMMITMENT_LIMIT;

    for (i = 0; i < Count && NT_SUCCESS(Status); i++)
    {
        ULONG Frame = MiPfnAllocatePage(&System->Pfn, 0);
        MI_FRAME_NUMBER Number = Frame;

        if (Frame == MI_FRAME_INVALID)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }

        Status = MiSystemMapFrames(System, VirtualAddress + (ULONG64)i * PAGE_SIZE, &Number, 1, MI_PROT_READWRITE, 0,
                                   TRUE);
        if (!NT_SUCCESS(Status))
            MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
    }

    if (!NT_SUCCESS(Status))
    {
        MiSystemUnmap(System, VirtualAddress, i, TRUE);
        MiReturnCommit(Space, Count);
    }

    return Status;
}

NTSTATUS
MiCreateKernelStack(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG ReservePages,
    _In_ ULONG CommitPages,
    _Out_ PULONG64 StackTop)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    NTSTATUS Status;
    ULONG64 Base;
    ULONG64 Top;

    *StackTop = 0;

    if (Ptes == NULL || ReservePages == 0 || CommitPages == 0 || CommitPages > ReservePages)
        return STATUS_INVALID_PARAMETER;

    if (ReservePages == Ptes->DefaultStackPages && CommitPages == ReservePages)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[MI_CURRENT_CPU() % Ptes->CacheCount];
        KIRQL OldIrql;

        if (MI_PEEK(Cache->StackDepth) != 0)
        {
            MI_SPIN_ACQUIRE(&Cache->Lock, &OldIrql);
            MI_ASSERT(Cache->StackDepth <= MI_KSTACK_CACHE_DEPTH);
            if (Cache->StackDepth != 0)
                *StackTop = Cache->Stack[--Cache->StackDepth];
            MI_SPIN_RELEASE(&Cache->Lock, OldIrql);

            if (*StackTop != 0)
            {
                MI_ASSERT((*StackTop & (PAGE_SIZE - 1)) == 0 && *StackTop > Ptes->Base);
                MI_ASSERT(((*StackTop - Ptes->Base) >> PAGE_SHIFT) <= Ptes->PageCount);
                MiAssertKernelStack(System, *StackTop, ReservePages, ReservePages);
                return STATUS_SUCCESS;
            }
        }
    }

    Status = MiReserveSystemPtesEx(System, ReservePages + 1, &Base);
    if (!NT_SUCCESS(Status))
        return Status;

    Top = Base + ((ULONG64)ReservePages + 1) * PAGE_SIZE;

    Status = MiCommitStackPages(System, Top - (ULONG64)CommitPages * PAGE_SIZE, CommitPages);
    if (!NT_SUCCESS(Status))
    {
        MiReleaseSystemPtes(System, Base, ReservePages + 1);
        return Status;
    }

    MiAssertKernelStack(System, Top, ReservePages, CommitPages);
    *StackTop = Top;
    return STATUS_SUCCESS;
}

static
ULONG
MiCommittedStackPages(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 StackTop,
    _In_ ULONG ReservePages)
{
    ULONG Pages = 0;

    while (Pages < ReservePages && MiGetPhysicalAddress(&System->SystemSpace,
                                                        StackTop - ((ULONG64)Pages + 1) * PAGE_SIZE) != 0)
    {
        Pages++;
    }

    return Pages;
}

VOID
MiDeleteKernelStack(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 StackTop,
    _In_ ULONG ReservePages)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG Committed = MiCommittedStackPages(System, StackTop, ReservePages);

    MiAssertKernelStack(System, StackTop, ReservePages, Committed);
    if (ReservePages == Ptes->DefaultStackPages && Committed == ReservePages)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[MI_CURRENT_CPU() % Ptes->CacheCount];
        BOOLEAN Cached = FALSE;
        KIRQL OldIrql;

        MI_SPIN_ACQUIRE(&Cache->Lock, &OldIrql);
        MI_ASSERT(Cache->StackDepth <= MI_KSTACK_CACHE_DEPTH);
        if (Cache->StackDepth < MI_KSTACK_CACHE_DEPTH)
        {
            Cache->Stack[Cache->StackDepth++] = StackTop;
            Cached = TRUE;
        }
        MI_SPIN_RELEASE(&Cache->Lock, OldIrql);

        if (Cached)
            return;
    }

    MiSystemUnmap(System, StackTop - (ULONG64)Committed * PAGE_SIZE, Committed, TRUE);
    MiReturnCommit(&System->SystemSpace, Committed);
    MiReleaseSystemPtes(System, StackTop - ((ULONG64)ReservePages + 1) * PAGE_SIZE, ReservePages + 1);
}

NTSTATUS
MiGrowKernelStack(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 StackTop,
    _In_ ULONG ReservePages,
    _In_ ULONG64 NewLimit)
{
    ULONG Committed = MiCommittedStackPages(System, StackTop, ReservePages);
    ULONG64 Limit = StackTop - (ULONG64)Committed * PAGE_SIZE;
    ULONG64 Lowest = StackTop - (ULONG64)ReservePages * PAGE_SIZE;
    ULONG Needed;
    NTSTATUS Status;

    MiAssertKernelStack(System, StackTop, ReservePages, Committed);
    NewLimit &= ~((ULONG64)PAGE_SIZE - 1);

    if (NewLimit >= Limit)
        return STATUS_SUCCESS;

    if (NewLimit < Lowest)
        return STATUS_STACK_OVERFLOW;

    Needed = (ULONG)((Limit - NewLimit) >> PAGE_SHIFT);
    MI_ASSERT(Needed != 0 && Needed <= ReservePages - Committed);
    Status = MiCommitStackPages(System, NewLimit, Needed);
    MiAssertKernelStack(System, StackTop, ReservePages, NT_SUCCESS(Status) ? Committed + Needed : Committed);
    return Status;
}
