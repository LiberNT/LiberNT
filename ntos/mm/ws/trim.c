/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/ws/trim.c
 * PURPOSE:     Working-set trimming and page reclamation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/mm.h>

static
BOOLEAN
MiTrimPage(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ PMI_VAD Vad,
    _In_ ULONG64 VirtualAddress,
    _Inout_ PMI_PTE Slot,
    _In_ ULONG TableFrame,
    _In_ BOOLEAN Aggressive)
{
    PMI_SYSTEM System = Space->System;
    MI_PTE Pte = MiArchPteRead(Slot);
    PMI_PFN Entry;
    ULONG Frame;

    if (!MiArchPteIsValid(Pte))
        return FALSE;

    MI_ASSERT(!MI_VAD_IS_DIRECT(Vad));
    MI_ASSERT(MiArchPteFrame(Pte) < System->Pfn.FrameCount);
    Frame = (ULONG)MiArchPteFrame(Pte);
    Entry = &System->Pfn.Pfn[Frame];

    if (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PAGE_TABLE)
        return FALSE;

    if (!Aggressive && MiArchPteIsAccessed(Pte))
    {
        MiArchPteWrite(Slot, MiArchPteSetAccessed(Pte, FALSE));
        MiArchTlbInvalidate(VirtualAddress, 1, TRUE);
        return FALSE;
    }

    if (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PROTOTYPE)
        return MiTrimPrototypePage(Space, Vad, VirtualAddress, Slot, TableFrame);

    UNREFERENCED_PARAMETER(TableFrame);

    for (;;)
    {
        MI_PTE Transition = MiSoftMake(MiSoftTransition, MiSoftProtection(Entry->OriginalPte), Frame);

        MI_ASSERT(MiArchPteIsValid(Pte) && MiArchPteFrame(Pte) == Frame);
        if (MiArchPteCompareExchange(Slot, Pte, Transition))
            break;

        Pte = MiArchPteRead(Slot);
    }

    MiArchTlbInvalidate(VirtualAddress, 1, TRUE);

    if (MiArchPteIsDirty(Pte))
    {
        MiPfnSetModified(&System->Pfn, Frame);
        MiWriteWatchNoteWriteLocked(Space, VirtualAddress);
    }

    MiPfnShareDecrement(&System->Pfn, Frame, FALSE);
    MI_ATOMIC_ADD64(&Space->ResidentPages, -1);
    return TRUE;
}

ULONG
MiTrimAddressSpace(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG PageTarget,
    _In_ BOOLEAN Aggressive)
{
    ULONG Trimmed = 0;
    ULONG64 Origin;
    ULONG Pass;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    MiPtFlushEmpty(Space);
    Origin = Space->TrimCursor;

    for (Pass = 0; Pass < 2 && Trimmed < PageTarget; Pass++)
    {
        ULONG64 Limit = (Pass == 0) ? ~0ULL : Origin;
        PMI_VAD_NODE Node;

        for (Node = MiVadFirst(&Space->VadRoot); Node != NULL && Trimmed < PageTarget; Node = MiVadNext(Node))
        {
            PMI_VAD Vad = CONTAINING_RECORD(Node, MI_VAD, Node);
            ULONG64 Va = Node->StartingVpn << PAGE_SHIFT;
            ULONG64 End = (Node->EndingVpn + 1) << PAGE_SHIFT;

            if (End <= Space->TrimCursor || MI_VAD_IS_DIRECT(Vad))
                continue;

            if (Va >= Limit)
                break;

            if (Va < Space->TrimCursor)
                Va = Space->TrimCursor;

            if (End > Limit)
                End = Limit;

            while (Va < End && Trimmed < PageTarget)
            {
                ULONG TableFrame;
                PMI_PTE Slot;
                ULONG64 Next;

                if (Space->LockRangeCount != 0 && MiLockRangeLocked(Space, Va, &Next))
                {
                    Va = (Next < End) ? Next : End;
                    continue;
                }

                Slot = MiPtLookup(Space, Va, &TableFrame);
                if (Slot == NULL)
                {
                    Va = MiPtNextTableBoundary(Space, Va);
                    continue;
                }

                if (MiTrimPage(Space, Vad, Va, Slot, TableFrame, Aggressive))
                    Trimmed++;

                Va += PAGE_SIZE;
            }

            Space->TrimCursor = (Va < End) ? Va : End;
        }

        if (Trimmed < PageTarget)
        {
            if (Pass == 0)
                Space->TrimCursor = 0;
            else
                Space->TrimCursor = Origin;
        }
    }

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Trimmed;
}

ULONG
MiTrimRange(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG Trimmed = 0;
    ULONG64 Va = Start;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    MiPtFlushEmpty(Space);

    while (Va < End)
    {
        PMI_VAD Vad = MiVadLocate(Space, Va);
        ULONG64 VadEnd;

        if (Vad == NULL)
        {
            Va += PAGE_SIZE;
            continue;
        }

        VadEnd = (Vad->Node.EndingVpn + 1) << PAGE_SHIFT;
        if (VadEnd > End)
            VadEnd = End;

        if (MI_VAD_IS_DIRECT(Vad))
        {
            Va = VadEnd;
            continue;
        }

        while (Va < VadEnd)
        {
            ULONG TableFrame;
            PMI_PTE Slot;
            ULONG64 Next;

            if (Space->LockRangeCount != 0 && MiLockRangeLocked(Space, Va, &Next))
            {
                Va = (Next < VadEnd) ? Next : VadEnd;
                continue;
            }

            Slot = MiPtLookup(Space, Va, &TableFrame);
            if (Slot == NULL)
            {
                Va = MiPtNextTableBoundary(Space, Va);
                continue;
            }

            if (MiTrimPage(Space, Vad, Va, Slot, TableFrame, TRUE))
                Trimmed++;

            Va += PAGE_SIZE;
        }
    }

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Trimmed;
}

#if DBG || defined(MM_HOST_TEST)
static
VOID
MiAssertLockRangeState(_In_ PMI_ADDRESS_SPACE Space)
{
    ULONG64 Pages = 0;
    ULONG Index;

    MI_ASSERT(Space->LockRangeCount <= Space->LockRangeCapacity);
    MI_ASSERT((Space->LockRanges == NULL) == (Space->LockRangeCapacity == 0));
    for (Index = 0; Index < Space->LockRangeCount; Index++)
    {
        PMI_LOCK_RANGE Range = &Space->LockRanges[Index];

        MI_ASSERT(Range->Start < Range->End);
        MI_ASSERT(((Range->Start | Range->End) & (PAGE_SIZE - 1)) == 0);
        MI_ASSERT(Index == 0 || Space->LockRanges[Index - 1].End < Range->Start);
        Pages += (Range->End - Range->Start) >> PAGE_SHIFT;
    }
    MI_ASSERT(Pages == Space->LockedPages);
}
#else
#define MiAssertLockRangeState(Space) ((VOID)0)
#endif

static
ULONG
MiLockRangeSearch(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress)
{
    ULONG Low = 0;
    ULONG High = Space->LockRangeCount;

    MI_ASSERT(Space->LockRangeCount <= Space->LockRangeCapacity);
    MI_ASSERT(Space->LockRangeCount == 0 || Space->LockRanges != NULL);
    while (Low < High)
    {
        ULONG Middle = Low + (High - Low) / 2;

        MI_ASSERT(Space->LockRanges[Middle].Start < Space->LockRanges[Middle].End);
        MI_ASSERT(Middle == 0 || Space->LockRanges[Middle - 1].End < Space->LockRanges[Middle].Start);
        MI_ASSERT(Middle + 1 == Space->LockRangeCount ||
                  Space->LockRanges[Middle].End < Space->LockRanges[Middle + 1].Start);
        if (Space->LockRanges[Middle].End <= VirtualAddress)
            Low = Middle + 1;
        else
            High = Middle;
    }

    return Low;
}

BOOLEAN
MiLockRangeLocked(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _Out_ PULONG64 Next)
{
    ULONG Index = MiLockRangeSearch(Space, VirtualAddress);

    if (Index == Space->LockRangeCount)
    {
        *Next = ~0ULL;
        return FALSE;
    }

    if (Space->LockRanges[Index].Start <= VirtualAddress)
    {
        *Next = Space->LockRanges[Index].End;
        return TRUE;
    }

    *Next = Space->LockRanges[Index].Start;
    return FALSE;
}

ULONG64
MiLockRangePages(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG64 Pages = 0;
    ULONG Index;

    for (Index = MiLockRangeSearch(Space, Start); Index < Space->LockRangeCount; Index++)
    {
        PMI_LOCK_RANGE Range = &Space->LockRanges[Index];
        ULONG64 From;
        ULONG64 To;

        if (Range->Start >= End)
            break;

        From = (Range->Start > Start) ? Range->Start : Start;
        To = (Range->End < End) ? Range->End : End;
        Pages += (To - From) >> PAGE_SHIFT;
    }

    return Pages;
}

static
BOOLEAN
MiLockRangeReserve(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG Count)
{
    PMI_LOCK_RANGE Ranges;
    ULONG Capacity;

    if (Count <= Space->LockRangeCapacity)
        return TRUE;

    Capacity = (Space->LockRangeCapacity != 0) ? Space->LockRangeCapacity * 2 : 8;
    if (Capacity < Count)
        Capacity = Count;

    Ranges = MI_ALLOCATE(Capacity * sizeof(*Ranges));
    if (Ranges == NULL)
        return FALSE;

    if (Space->LockRangeCount != 0)
        RtlCopyMemory(Ranges, Space->LockRanges, Space->LockRangeCount * sizeof(*Ranges));
    if (Space->LockRanges != NULL)
        MI_FREE(Space->LockRanges);

    Space->LockRanges = Ranges;
    Space->LockRangeCapacity = Capacity;
    return TRUE;
}

NTSTATUS
MiLockRangeAdd(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG64 Pages;
    ULONG First;
    ULONG Last;

    MI_ASSERT(Start < End && ((Start | End) & (PAGE_SIZE - 1)) == 0);
    MiAssertLockRangeState(Space);
    if (!MiLockRangeReserve(Space, Space->LockRangeCount + 2))
        return STATUS_INSUFFICIENT_RESOURCES;

    Pages = ((End - Start) >> PAGE_SHIFT) - MiLockRangePages(Space, Start, End);

    First = MiLockRangeSearch(Space, Start);
    if (First != 0 && Space->LockRanges[First - 1].End == Start)
        First--;

    for (Last = First; Last < Space->LockRangeCount && Space->LockRanges[Last].Start <= End; Last++)
    {
        if (Space->LockRanges[Last].Start < Start)
            Start = Space->LockRanges[Last].Start;
        if (Space->LockRanges[Last].End > End)
            End = Space->LockRanges[Last].End;
    }

    if (Last == First)
    {
        RtlMoveMemory(&Space->LockRanges[First + 1], &Space->LockRanges[First],
                      (Space->LockRangeCount - First) * sizeof(MI_LOCK_RANGE));
        Space->LockRangeCount++;
    }
    else if (Last > First + 1)
    {
        RtlMoveMemory(&Space->LockRanges[First + 1], &Space->LockRanges[Last],
                      (Space->LockRangeCount - Last) * sizeof(MI_LOCK_RANGE));
        Space->LockRangeCount -= Last - First - 1;
    }

    Space->LockRanges[First].Start = Start;
    Space->LockRanges[First].End = End;
    MI_ASSERT(Start < End);
    MI_ASSERT(First == 0 || Space->LockRanges[First - 1].End < Start);
    MI_ASSERT(First + 1 == Space->LockRangeCount || End < Space->LockRanges[First + 1].Start);
    Space->LockedPages += Pages;
    MiAssertLockRangeState(Space);
    return STATUS_SUCCESS;
}

VOID
MiLockRangeRemove(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG Index;

    MI_ASSERT(Start <= End && ((Start | End) & (PAGE_SIZE - 1)) == 0);
    MiAssertLockRangeState(Space);
    Index = MiLockRangeSearch(Space, Start);
    while (Index < Space->LockRangeCount && Space->LockRanges[Index].Start < End)
    {
        PMI_LOCK_RANGE Range = &Space->LockRanges[Index];
        ULONG64 From = (Range->Start > Start) ? Range->Start : Start;
        ULONG64 To = (Range->End < End) ? Range->End : End;

        MI_ASSERT(From < To);
        MI_ASSERT(Space->LockedPages >= ((To - From) >> PAGE_SHIFT));
        Space->LockedPages -= (To - From) >> PAGE_SHIFT;

        if (Range->Start < Start && Range->End > End)
        {
            if (!MiLockRangeReserve(Space, Space->LockRangeCount + 1))
            {
                MI_ASSERT(Space->LockedPages >= ((Space->LockRanges[Index].End - End) >> PAGE_SHIFT));
                Space->LockedPages -= (Space->LockRanges[Index].End - End) >> PAGE_SHIFT;
                Space->LockRanges[Index].End = Start;
                MiAssertLockRangeState(Space);
                return;
            }

            RtlMoveMemory(&Space->LockRanges[Index + 1], &Space->LockRanges[Index],
                          (Space->LockRangeCount - Index) * sizeof(MI_LOCK_RANGE));
            Space->LockRangeCount++;
            Space->LockRanges[Index].End = Start;
            Space->LockRanges[Index + 1].Start = End;
            MiAssertLockRangeState(Space);
            return;
        }

        if (Range->Start < Start)
        {
            Range->End = Start;
            Index++;
            continue;
        }

        if (Range->End > End)
        {
            Range->Start = End;
            MiAssertLockRangeState(Space);
            return;
        }

        RtlMoveMemory(&Space->LockRanges[Index], &Space->LockRanges[Index + 1],
                      (Space->LockRangeCount - Index - 1) * sizeof(MI_LOCK_RANGE));
        Space->LockRangeCount--;
    }
    MiAssertLockRangeState(Space);
}

VOID
MiLockRangeReset(
    _Inout_ PMI_ADDRESS_SPACE Space)
{
    MiAssertLockRangeState(Space);
    if (Space->LockRanges != NULL)
        MI_FREE(Space->LockRanges);

    Space->LockRanges = NULL;
    Space->LockRangeCount = 0;
    Space->LockRangeCapacity = 0;
    Space->LockedPages = 0;
    MiAssertLockRangeState(Space);
}

static
NTSTATUS
MiWritePageFileCluster(
    _Inout_ PMI_SYSTEM System,
    _In_ const ULONG *Frames,
    _In_ const ULONG64 *Slots,
    _In_ const BOOLEAN *Reused,
    _In_ ULONG Count)
{
    PMI_PAGEFILE PageFile = MiSystemGetPageFile(System);
    NTSTATUS Status;
    PVOID Mapping;
    ULONG i;

    MI_ASSERT(PageFile != NULL);
    MI_ASSERT(Count != 0 && Count <= MI_MAX_FILE_WRITE_PAGES);
#if DBG || defined(MM_HOST_TEST)
    for (i = 0; i < Count; i++)
    {
        MI_ASSERT(Frames[i] < System->Pfn.FrameCount);
        MI_ASSERT(MI_PFN_FLAGS(&System->Pfn.Pfn[Frames[i]]) & MI_PFN_FLAG_IN_FLIGHT);
        MI_ASSERT(Slots[i] != 0);
        MI_ASSERT(i == 0 || Slots[i] == Slots[i - 1] + 1);
    }
#endif
    if (PageFile->Ops.WriteFrames != NULL)
    {
        Status = PageFile->Ops.WriteFrames(PageFile->Context, Slots[0], Frames, Count);
    }
    else
    {
        MI_ASSERT(Count == 1 && PageFile->Ops.Write != NULL);
        Mapping = MiPfnMapFrame(&System->Pfn, Frames[0]);
        Status = PageFile->Ops.Write(PageFile->Context, Slots[0], Mapping);
        MiPfnUnmapFrame(&System->Pfn, Mapping);
    }

    for (i = 0; i < Count; i++)
    {
        PMI_PFN Entry = &System->Pfn.Pfn[Frames[i]];

        if (!NT_SUCCESS(Status))
        {
            if (MiPfnWriteComplete(&System->Pfn, Frames[i], 0, FALSE) || !Reused[i])
                MiPageFileReleaseSlot(PageFile, Slots[i]);
        }
        else if (MiPfnWriteComplete(&System->Pfn, Frames[i],
                                    MiSoftMake(MiSoftPageFile, MiSoftProtection(Entry->OriginalPte), Slots[i]), TRUE))
        {
            MiPageFileReleaseSlot(PageFile, Slots[i]);
        }
    }

    if (NT_SUCCESS(Status))
        MI_ATOMIC_ADD64(&PageFile->PagesWritten, Count);

    return Status;
}

ULONG
MiWriteModifiedPages(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG MaximumPages)
{
    PMI_PAGEFILE PageFile = MiSystemGetPageFile(System);
    ULONG Frames[MI_MAX_FILE_WRITE_PAGES];
    ULONG64 Slots[MI_MAX_FILE_WRITE_PAGES];
    BOOLEAN ClusterReused[MI_MAX_FILE_WRITE_PAGES];
    ULONG Limit = (PageFile != NULL && PageFile->Ops.WriteFrames != NULL) ? MI_MAX_FILE_WRITE_PAGES : 1;
    ULONG Count = 0;
    ULONG Written = 0;
    ULONG Attempts = 0;

    while (Written + Count < MaximumPages && Attempts++ < MaximumPages)
    {
        ULONG Frame = MiPfnTakeModified(&System->Pfn);
        PMI_PFN Entry;
        ULONG64 Slot;
        BOOLEAN Reused;
        NTSTATUS Status;

        if (Frame == MI_FRAME_INVALID)
            break;

        MI_ASSERT(Written <= MaximumPages && Count <= MaximumPages - Written && Count < Limit);
        MI_ASSERT(Frame < System->Pfn.FrameCount);
        Entry = &System->Pfn.Pfn[Frame];
        MI_ASSERT(MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_IN_FLIGHT);

        if ((MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PROTOTYPE) && MiSoftKind(Entry->OriginalPte) == MiSoftSubsection)
        {
            Status = MiWritePrototypePage(System, Frame);

            /* The page of a busy file went back to the modified list. */
            if (Status == STATUS_CANT_WAIT)
                continue;
            if (!NT_SUCCESS(Status))
                break;
            Written++;
            continue;
        }

        if (PageFile == NULL)
        {
            MiPfnWriteComplete(&System->Pfn, Frame, 0, FALSE);
            continue;
        }

        Reused = (BOOLEAN)(MiSoftKind(Entry->OriginalPte) == MiSoftPageFile);
        Slot = Reused ? MiSoftValue(Entry->OriginalPte) : MiPageFileReserveSlot(PageFile);
        MI_ASSERT(!Reused || Slot != 0);
        if (Slot == 0)
        {
            MiPfnWriteComplete(&System->Pfn, Frame, 0, FALSE);
            break;
        }

        if (Count != 0 && Slot != Slots[Count - 1] + 1)
        {
            Status = MiWritePageFileCluster(System, Frames, Slots, ClusterReused, Count);
            if (!NT_SUCCESS(Status))
            {
                if (MiPfnWriteComplete(&System->Pfn, Frame, 0, FALSE) || !Reused)
                    MiPageFileReleaseSlot(PageFile, Slot);

                return Written;
            }

            Written += Count;
            Count = 0;
        }

        MI_ASSERT(Count < Limit);
        MI_ASSERT(Count == 0 || Slot == Slots[Count - 1] + 1);
        Frames[Count] = Frame;
        Slots[Count] = Slot;
        ClusterReused[Count] = Reused;
        Count++;

        if (Count == Limit)
        {
            Status = MiWritePageFileCluster(System, Frames, Slots, ClusterReused, Count);
            if (!NT_SUCCESS(Status))
                return Written;

            Written += Count;
            Count = 0;
        }
    }

    if (Count != 0 && NT_SUCCESS(MiWritePageFileCluster(System, Frames, Slots, ClusterReused, Count)))
        Written += Count;

    return Written;
}
