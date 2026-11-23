/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/core/mmsystem.c
 * PURPOSE:     Shared memory manager system initialization
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/mm.h>

BOOLEAN
MiChargeSystemCommit(
    _Inout_ PMI_SYSTEM System,
    _In_ LONG64 Pages,
    _In_ BOOLEAN Wait)
{
    LONG64 Committed;
    LONG64 Limit;
    PMI_EXPAND_COMMIT ExpandCommit;

    MI_ASSERT(Pages >= 0);
    for (;;)
    {
        Limit = MI_ATOMIC_READ64(&System->CommitLimit);
        Committed = MI_ATOMIC_ADD64(&System->CommittedPages, Pages) + Pages;
        if (Committed <= Limit)
            break;

        MI_ATOMIC_ADD64(&System->CommittedPages, -Pages);
        ExpandCommit = MiSystemGetExpandCommit(System);
        if (Pages <= 0 || ExpandCommit == NULL || !ExpandCommit(System, Pages, Limit, Wait))
            return FALSE;
    }

    ExpandCommit = MiSystemGetExpandCommit(System);
    if (Pages > 0 && ExpandCommit != NULL && Committed > Limit - Limit / 10)
        ExpandCommit(System, 0, Limit, FALSE);

    return TRUE;
}

BOOLEAN
MiChargeCommit(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ LONG64 Pages)
{
    PMI_SYSTEM System = Space->System;

    MI_ASSERT(Pages >= 0);
    if (!MiChargeSystemCommit(System, Pages, (BOOLEAN)!Space->IsSystem))
        return FALSE;

    if (Pages > 0 && Space->CommitOwner != NULL && System->ChargeOwnerCommit != NULL &&
        !System->ChargeOwnerCommit(Space->CommitOwner, Pages))
    {
        MI_ATOMIC_ADD64(&System->CommittedPages, -Pages);
        return FALSE;
    }

    MI_ATOMIC_ADD64(&Space->CommittedPages, Pages);
    return TRUE;
}

VOID
MiReturnCommit(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ LONG64 Pages)
{
    LONG64 Previous;

    MI_ASSERT(Pages >= 0);
    if (Pages == 0)
        return;

    Previous = MI_ATOMIC_ADD64(&Space->System->CommittedPages, -Pages);
    MI_ASSERT(Previous >= Pages);
    Previous = MI_ATOMIC_ADD64(&Space->CommittedPages, -Pages);
    MI_ASSERT(Previous >= Pages);
    UNREFERENCED_PARAMETER(Previous);

    if (Space->CommitOwner != NULL && Space->System->ReturnOwnerCommit != NULL)
        Space->System->ReturnOwnerCommit(Space->CommitOwner, Pages);
}

VOID
MiTlbInvalidateSpace(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 PageCount)
{
    PMI_SYSTEM System = Space->System;

    if (Space->IsSystem || Space->CommitOwner == NULL || System->OwnerProcessors == NULL || PageCount == 0 ||
        VirtualAddress < Space->LowestVa || VirtualAddress > Space->HighestVa ||
        PageCount - 1 > ((Space->HighestVa - VirtualAddress) >> PAGE_SHIFT))
    {
        MiArchTlbInvalidate(VirtualAddress, PageCount, TRUE);
        return;
    }
    MiArchTlbInvalidateProcessors(VirtualAddress, PageCount, System->OwnerProcessors(Space->CommitOwner));
}

static
NTSTATUS
MiSpaceInitialize(
    _Inout_ PMI_SYSTEM System,
    _Out_ PMI_ADDRESS_SPACE Space,
    _In_ BOOLEAN IsSystem)
{
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    ULONG Root;

    RtlZeroMemory(Space, sizeof(*Space));
    Space->System = System;
    Space->IsSystem = IsSystem;
    Space->Granularity = MI_ALLOCATION_GRANULARITY;
    Space->LowestVa = IsSystem ? Arch->SystemAddressStart : Arch->UserAddressStart;
    Space->HighestVa = IsSystem ? Arch->SystemAddressEnd : Arch->UserAddressEnd;
    MI_RW_INIT(&Space->Lock);
    MiVadRootInitialize(&Space->VadRoot, Space->LowestVa >> PAGE_SHIFT, Space->HighestVa >> PAGE_SHIFT);
    MiVadRootInitialize(&Space->AweRoot, 0, System->Pfn.FrameCount - 1);
    MiVadRootInitialize(&Space->CloneRoot, Space->LowestVa >> PAGE_SHIFT, Space->HighestVa >> PAGE_SHIFT);

    Root = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
    if (Root == MI_FRAME_INVALID)
        return STATUS_NO_MEMORY;

    MiPfnInitializePage(&System->Pfn, Root, 0, 0, 0, MI_PFN_FLAG_PAGE_TABLE);
    System->Pfn.Pfn[Root].PageTableOwner = Space;
    Space->RootFrame = Root;

    if (!IsSystem)
        MiArchInitializeProcessRoot(System->SystemSpace.RootFrame, Root);

    return STATUS_SUCCESS;
}

NTSTATUS
MiSystemInitialize(
    _Out_ PMI_SYSTEM System,
    _In_ PMI_PFN PfnArray,
    _In_ ULONG FirstFrame,
    _In_ ULONG FrameCount,
    _In_ ULONG CpuCount,
    _In_ LONG64 CommitLimit)
{
    ULONG Bucket;

    RtlZeroMemory(System, sizeof(*System));
    System->Arch = MiArchDescribe();
    System->CommitLimit = CommitLimit;
    MiPfnDbInitialize(&System->Pfn, PfnArray, FirstFrame, FrameCount, CpuCount);
    MiPfnDbSetDirectFrames(&System->Pfn, System->Arch->DirectFrameCount);
    MiPfnDbSetRegionRotation(&System->Pfn, System->Arch->PrefersRegionRotation);
    System->Pfn.Owner = System;
    MI_SPIN_INIT(&System->SegmentListLock);
    MI_MUTEX_INIT(&System->CfgPageLock);
    InitializeListHead(&System->CfgPages);
    InitializeListHead(&System->SegmentList);
    InitializeListHead(&System->UnusedSegmentList);
    for (Bucket = 0; Bucket < RTL_NUMBER_OF(System->SegmentChunkHash); Bucket++)
        InitializeListHead(&System->SegmentChunkHash[Bucket]);
    System->Pfn.Repurpose = MiRepurposeStandbyPage;
    return STATUS_SUCCESS;
}

NTSTATUS
MiSystemAttachFrameWindow(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 Base)
{
    PMI_PFN_DATABASE Db = &System->Pfn;
    ULONG SlotCount = PAGE_SIZE / sizeof(PMI_PTE);
    ULONG SlotFrame, StateFrame, Index;
    PMI_PTE *Slots;
    NTSTATUS Status;

    if (Db->DirectFrames >= Db->FrameCount || Db->Window.SlotCount != 0)
        return STATUS_SUCCESS;

    Status = MiPtPinSystemRange(&System->SystemSpace, Base, (ULONG64)SlotCount << PAGE_SHIFT, NULL, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    SlotFrame = MiPfnAllocatePage(Db, MI_ALLOCATE_ZEROED);
    StateFrame = MiPfnAllocatePage(Db, MI_ALLOCATE_ZEROED);
    if (SlotFrame == MI_FRAME_INVALID || StateFrame == MI_FRAME_INVALID)
        goto Fail;

    Slots = MiArchMapFrame(SlotFrame);
    for (Index = 0; Index < SlotCount; Index++)
    {
        Slots[Index] = MiPtLookup(&System->SystemSpace, Base + ((ULONG64)Index << PAGE_SHIFT), NULL);
        if (Slots[Index] == NULL)
            goto Fail;
    }

    MiPfnWindowAttach(Db, Base, Slots, MiArchMapFrame(StateFrame), SlotCount);
    return STATUS_SUCCESS;

Fail:
    if (SlotFrame != MI_FRAME_INVALID)
        MiPfnShareDecrement(Db, SlotFrame, TRUE);
    if (StateFrame != MI_FRAME_INVALID)
        MiPfnShareDecrement(Db, StateFrame, TRUE);
    return STATUS_NO_MEMORY;
}

NTSTATUS
MiAddressSpaceCreate(
    _Inout_ PMI_SYSTEM System,
    _Out_ PMI_ADDRESS_SPACE Space)
{
    return MiSpaceInitialize(System, Space, (BOOLEAN)(Space == &System->SystemSpace));
}

VOID
MiAddressSpaceDestroy(
    _Inout_ PMI_ADDRESS_SPACE Space)
{
    MiVadFlushCache(Space);
    MiPtFlushEmpty(Space);
    MI_ASSERT(Space->VadRoot.NodeCount == 0);
    MI_ASSERT(Space->AweRoot.NodeCount == 0 && Space->AwePages == 0);
    MI_ASSERT(Space->CloneRoot.NodeCount == 0);
    MI_ASSERT(MI_ATOMIC_READ64(&Space->PageTablePages) == 0);

    MiLockRangeReset(Space);
    MiPfnShareDecrement(&Space->System->Pfn, Space->RootFrame, TRUE);
    Space->RootFrame = 0;
}

static
VOID
MiSessionSpaceUnpinTops(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Limit)
{
    PMI_SYSTEM System = Space->System;
    ULONG64 Span = MiSessionSpan(System->Arch);
    ULONG64 Va;

    for (Va = System->SessionStart; Va < Limit && Va <= System->SessionEnd; Va += Span)
    {
        MiPtUnpinRange(Space, Va, PAGE_SIZE);
        if (Va + Span < Va)
            break;
    }
}

/* Index in the root of the top-level entry that holds a session range below the top level. */
static
ULONG
MiSessionTopIndex(
    _In_ PMI_SYSTEM System)
{
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    LONG Top = Arch->PagingLevels - 1;

    MI_ASSERT(MiSessionLevel(Arch) != Top);
    MI_ASSERT((System->SessionStart >> Arch->Level[Top].Shift) == (System->SessionEnd >> Arch->Level[Top].Shift));
    return (ULONG)((System->SessionStart >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
}

/*
 * A session range below the top level shares its top-level entry with system
 * space. The session gets its own copy of that entry's table: the system
 * entries are copied (they are pinned and never change), the session ones
 * start empty and are filled in the copy only.
 */
static
NTSTATUS
MiSessionCopyTopTable(
    _Inout_ PMI_ADDRESS_SPACE Space)
{
    PMI_SYSTEM System = Space->System;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    LONG Level = MiSessionLevel(Arch);
    ULONG TopIndex = MiSessionTopIndex(System);
    ULONG First = (ULONG)((System->SessionStart >> Arch->Level[Level].Shift) & Arch->Level[Level].IndexMask);
    ULONG Last = (ULONG)((System->SessionEnd >> Arch->Level[Level].Shift) & Arch->Level[Level].IndexMask);
    PMI_PTE Root;
    PMI_PTE From;
    PMI_PTE To;
    MI_PTE TopEntry;
    ULONG Copy;
    ULONG Index;
    LONG Used = 0;

    MI_ASSERT(Level == (LONG)Arch->PagingLevels - 2);

    Root = MiArchMapFrame(System->SystemSpace.RootFrame);
    TopEntry = MiArchPteRead(&Root[TopIndex]);
    MiArchUnmapFrame(Root);
    MI_ASSERT(MiArchPteIsValid(TopEntry) && !MiArchPteIsBlock(TopEntry, (ULONG)Level + 1));

    Copy = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
    if (Copy == MI_FRAME_INVALID)
        return STATUS_NO_MEMORY;

    From = MiArchMapFrame(MiArchPteFrame(TopEntry));
    To = MiArchMapFrame(Copy);
    for (Index = 0; Index < Arch->Level[Level].EntryCount; Index++)
    {
        MI_PTE Entry = MiArchPteRead(&From[Index]);

        if (Index >= First && Index <= Last)
        {
            MI_ASSERT(!MiArchPteIsValid(Entry));
            continue;
        }

        MI_ASSERT(MiArchPteIsValid(Entry));
        MiArchPteWrite(&To[Index], Entry);
        Used++;
    }
    MiArchUnmapFrame(To);
    MiArchUnmapFrame(From);

    MiPfnInitializePage(&System->Pfn, Copy, ((ULONG64)Space->RootFrame << PAGE_SHIFT) +
                                            (ULONG64)TopIndex * sizeof(MI_PTE),
                        Space->RootFrame, 0, MI_PFN_FLAG_PAGE_TABLE | MI_PFN_FLAG_PINNED);
    System->Pfn.Pfn[Copy].PageTableOwner = Space;
    System->Pfn.Pfn[Copy].UsedEntries = Used;

    Root = MiArchMapFrame(Space->RootFrame);
    MI_ASSERT(!MiArchPteIsValid(MiArchPteRead(&Root[TopIndex])));
    MiArchPteWrite(&Root[TopIndex], MiArchPteMakeTable(Copy, 0));
    MiArchUnmapFrame(Root);
    MI_ATOMIC_ADD32(&System->Pfn.Pfn[Space->RootFrame].UsedEntries, 1);
    MI_ATOMIC_ADD64(&Space->PageTablePages, 1);
    return STATUS_SUCCESS;
}

static
VOID
MiSessionFreeTopTable(
    _Inout_ PMI_ADDRESS_SPACE Space)
{
    PMI_SYSTEM System = Space->System;
    ULONG TopIndex = MiSessionTopIndex(System);
    PMI_PTE Root = MiArchMapFrame(Space->RootFrame);
    MI_PTE TopEntry = MiArchPteRead(&Root[TopIndex]);
    ULONG Copy;

    if (MiArchPteIsValid(TopEntry))
    {
        Copy = (ULONG)MiArchPteFrame(TopEntry);
        MI_ASSERT(System->Pfn.Pfn[Copy].PageTableOwner == Space);
        MiArchPteWrite(&Root[TopIndex], 0);
        MI_ATOMIC_ADD32(&System->Pfn.Pfn[Space->RootFrame].UsedEntries, -1);
        MI_ATOMIC_ADD64(&Space->PageTablePages, -1);
        MI_ATOMIC_AND8(&System->Pfn.Pfn[Copy].Flags, (UCHAR)~MI_PFN_FLAG_PINNED);
        System->Pfn.Pfn[Copy].UsedEntries = 0;
        MiArchTlbInvalidateAll(TRUE);
        MiPfnShareDecrement(&System->Pfn, Copy, TRUE);
    }

    MiArchUnmapFrame(Root);
}

NTSTATUS
MiSessionSpaceCreate(
    _Inout_ PMI_SYSTEM System,
    _Out_ PMI_ADDRESS_SPACE Space)
{
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    ULONG64 Span = MiSessionSpan(Arch);
    NTSTATUS Status;
    ULONG64 Va;

    if (System->SessionEnd <= System->SessionStart)
        return STATUS_NOT_SUPPORTED;

    MI_ASSERT((System->SessionStart & (Span - 1)) == 0);
    MI_ASSERT(((System->SessionEnd + 1) & (Span - 1)) == 0);
    MI_ASSERT(System->SessionStart >= Arch->SystemAddressStart && System->SessionEnd <= Arch->SystemAddressEnd);

    Status = MiSpaceInitialize(System, Space, TRUE);
    if (!NT_SUCCESS(Status))
        return Status;

    Space->IsSession = TRUE;
    Space->LowestVa = System->SessionStart;
    Space->HighestVa = System->SessionEnd;
    MiVadRootInitialize(&Space->VadRoot, Space->LowestVa >> PAGE_SHIFT, Space->HighestVa >> PAGE_SHIFT);
    MiVadRootInitialize(&Space->CloneRoot, Space->LowestVa >> PAGE_SHIFT, Space->HighestVa >> PAGE_SHIFT);

    if (MiSessionLevel(Arch) != (LONG)Arch->PagingLevels - 1)
    {
        Status = MiSessionCopyTopTable(Space);
        if (!NT_SUCCESS(Status))
        {
            MiAddressSpaceDestroy(Space);
            return Status;
        }
    }

    for (Va = System->SessionStart; Va <= System->SessionEnd; Va += Span)
    {
        Status = MiPtPinRange(Space, Va, PAGE_SIZE);
        if (!NT_SUCCESS(Status))
        {
            MiSessionSpaceUnpinTops(Space, Va + PAGE_SIZE);
            if (MiSessionLevel(Arch) != (LONG)Arch->PagingLevels - 1)
                MiSessionFreeTopTable(Space);
            MiAddressSpaceDestroy(Space);
            return Status;
        }
        if (Va + Span < Va)
            break;
    }

    MI_ASSERT(Space->IsSystem && Space->IsSession && Space->RootFrame != 0);
    MI_ASSERT(Space->VadRoot.NodeCount == 0 && Space->LowestVa == System->SessionStart &&
              Space->HighestVa == System->SessionEnd);
    return STATUS_SUCCESS;
}

VOID
MiSessionSpaceDestroy(
    _Inout_ PMI_ADDRESS_SPACE Space)
{
    const MI_ARCH_DESCRIPTOR *Arch = Space->System->Arch;

    MI_ASSERT(Space->IsSystem && Space->IsSession && Space->RootFrame != 0);
    MI_ASSERT(Space != &Space->System->SystemSpace);
    MiCleanAddressSpace(Space);
    MiSessionSpaceUnpinTops(Space, Space->HighestVa);
    if (MiSessionLevel(Arch) != (LONG)Arch->PagingLevels - 1)
        MiSessionFreeTopTable(Space);
    MiAddressSpaceDestroy(Space);
}

VOID
MiSessionSpaceLink(
    _In_ PMI_ADDRESS_SPACE Session,
    _Inout_ PMI_ADDRESS_SPACE Process,
    _In_ BOOLEAN Link)
{
    PMI_SYSTEM System = Session->System;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    LONG Top = Arch->PagingLevels - 1;
    ULONG First = (ULONG)((System->SessionStart >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
    ULONG Last = (ULONG)((System->SessionEnd >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
    PMI_PTE From;
    PMI_PTE To;
    ULONG Index;

    MI_ASSERT(Session->IsSystem && Session->IsSession && Session->RootFrame != 0);
    MI_ASSERT(!Process->IsSystem && !Process->IsSession && Process->RootFrame != 0);
    MI_ASSERT(Session->System == Process->System && Session->RootFrame != Process->RootFrame);
    MI_ASSERT(System->SessionEnd > System->SessionStart && First <= Last);

    From = MiArchMapFrame(Session->RootFrame);
    To = MiArchMapFrame(Process->RootFrame);

    if (MiSessionLevel(Arch) == Top)
    {
        for (Index = First; Index <= Last; Index++)
        {
            MI_ASSERT(MiArchPteIsValid(MiArchPteRead(&From[Index])));
            MI_ASSERT(Link ? (MiArchPteRead(&To[Index]) == 0) :
                             (MiArchPteRead(&To[Index]) == MiArchPteRead(&From[Index])));
            MiArchPteWrite(&To[Index], Link ? MiArchPteRead(&From[Index]) : 0);
        }
    }
    else
    {
        /* The process swaps the shared top-level entry for the session's copy of it. */
        PMI_PTE SystemRoot = MiArchMapFrame(System->SystemSpace.RootFrame);
        MI_PTE Shared = MiArchPteRead(&SystemRoot[First]);

        MI_ASSERT(First == Last && MiArchPteIsValid(MiArchPteRead(&From[First])));
        MI_ASSERT(Link ? (MiArchPteRead(&To[First]) == Shared) :
                         (MiArchPteRead(&To[First]) == MiArchPteRead(&From[First])));
        MiArchPteReplaceTable(&To[First], Link ? MiArchPteRead(&From[First]) : Shared);
        MiArchUnmapFrame(SystemRoot);
    }

    MiArchUnmapFrame(To);
    MiArchUnmapFrame(From);

    if (!Link || MiSessionLevel(Arch) != Top)
        MiArchTlbInvalidateAll(TRUE);
}

#if DBG || defined(MM_HOST_TEST)
static
VOID
MiValidateVadCache(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_opt_ PMI_VAD Exclude)
{
    PMI_VAD Vad = Space->FreeVads;
    ULONG Count = 0;

    MI_ASSERT(Space->FreeVadCount <= MI_VAD_CACHE_SIZE);
    while (Vad != NULL && Count < MI_VAD_CACHE_SIZE)
    {
        MI_ASSERT(Vad != Exclude);
        MI_ASSERT(MiVadFind(&Space->VadRoot, Vad->Node.StartingVpn) != &Vad->Node);
        Vad = (PMI_VAD)Vad->Node.Left;
        Count++;
    }
    MI_ASSERT(Vad == NULL);
    MI_ASSERT(Count == Space->FreeVadCount);
}
#else
#define MiValidateVadCache(Space, Exclude) ((VOID)0)
#endif

PMI_VAD
MiVadPopFree(PMI_ADDRESS_SPACE Space)
{
    PMI_VAD Vad = Space->FreeVads;

    MiValidateVadCache(Space, NULL);
    if (Vad != NULL)
    {
        Space->FreeVads = (PMI_VAD)Vad->Node.Left;
        Space->FreeVadCount--;
        RtlZeroMemory(Vad, sizeof(*Vad));
    }
    MiValidateVadCache(Space, Vad);
    return Vad;
}

PMI_VAD
MiVadAllocateAndLock(PMI_ADDRESS_SPACE Space)
{
    PMI_VAD Vad;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    Vad = MiVadPopFree(Space);
    if (Vad != NULL)
        return Vad;
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    Vad = MI_ALLOCATE(sizeof(*Vad));
    if (Vad != NULL)
        RtlZeroMemory(Vad, sizeof(*Vad));
    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    return Vad;
}

BOOLEAN
MiVadCacheFree(PMI_ADDRESS_SPACE Space, PMI_VAD Vad)
{
    MiValidateVadCache(Space, Vad);
    MI_ASSERT(MiVadFind(&Space->VadRoot, Vad->Node.StartingVpn) != &Vad->Node);
    if (Space->FreeVadCount == MI_VAD_CACHE_SIZE)
        return FALSE;
    Vad->Node.Left = (PMI_VAD_NODE)Space->FreeVads;
    Space->FreeVads = Vad;
    Space->FreeVadCount++;
    MiValidateVadCache(Space, NULL);
    return TRUE;
}

VOID
MiVadFlushCache(PMI_ADDRESS_SPACE Space)
{
    MiValidateVadCache(Space, NULL);
    while (Space->FreeVads != NULL)
    {
        PMI_VAD Vad = Space->FreeVads;

        Space->FreeVads = (PMI_VAD)Vad->Node.Left;
        Space->FreeVadCount--;
        MI_FREE(Vad);
    }
    MI_ASSERT(Space->FreeVadCount == 0);
    MiValidateVadCache(Space, NULL);
}
