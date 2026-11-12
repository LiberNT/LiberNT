/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/proc/clone.c
 * PURPOSE:     Process address-space cloning
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/mm.h>

typedef struct _MI_CLONE_WORK
{
    ULONG64 Va;
    PMI_CLONE_REF Existing;
    PMI_CLONE_REF Source;
    PMI_CLONE_RANGE SourceRange;
    PMI_CLONE_REF Target;
    PMI_CLONE_RANGE TargetRange;
    PMI_CLONE_PAGE Page;
    ULONG CopyFrame;
    BOOLEAN Flush;
} MI_CLONE_WORK, *PMI_CLONE_WORK;

#define MI_CLONE_FLUSH_ALL 32

typedef struct _MI_CLONE_TABLES
{
    ULONG64 Va;
    ULONG64 End;
    PMI_PTE Source;
    PMI_PTE Target;
    ULONG SourceTable;
    ULONG TargetTable;
} MI_CLONE_TABLES, *PMI_CLONE_TABLES;

typedef struct _MI_CLONE_PLAN
{
    PMI_CLONE_WORK Work;
    ULONG64 Count;
    ULONG64 Capacity;
    PMI_CLONE_RANGE *Ranges;
    ULONG64 RangeCount;
    ULONG64 RangeCapacity;
    ULONG64 FirstSourceRange;
    ULONG64 Flushes;
    MI_CLONE_TABLES Tables;
} MI_CLONE_PLAN, *PMI_CLONE_PLAN;

static PMI_CLONE_RANGE
MiCloneRangeOf(PMI_ADDRESS_SPACE Space, ULONG64 Va, PMI_CLONE_REF *Ref)
{
    ULONG64 Vpn = Va >> PAGE_SHIFT;
    PMI_CLONE_RANGE Range = MI_ATOMIC_READ_POINTER(&Space->CloneHint);
    PMI_VAD_NODE Node;

    if (Range == NULL || Vpn < Range->Node.StartingVpn || Vpn > Range->Node.EndingVpn)
    {
        Node = MiVadFind(&Space->CloneRoot, Vpn);
        if (Node == NULL)
            return NULL;
        Range = CONTAINING_RECORD(Node, MI_CLONE_RANGE, Node);
        MI_ATOMIC_WRITE_POINTER(&Space->CloneHint, Range);
    }
    *Ref = &Range->Refs[Vpn - Range->Node.StartingVpn];
    return Range;
}

static VOID
MiCloneRemoveRange(PMI_ADDRESS_SPACE Space, PMI_CLONE_RANGE Range)
{
    if (MI_ATOMIC_READ_POINTER(&Space->CloneHint) == Range)
        MI_ATOMIC_WRITE_POINTER(&Space->CloneHint, NULL);
    MiVadRemove(&Space->CloneRoot, &Range->Node);
}

PMI_CLONE_REF
MiCloneLookup(PMI_ADDRESS_SPACE Space, ULONG64 Va)
{
    PMI_CLONE_REF Ref;

    if (Space->CloneRoot.NodeCount == 0 || MiCloneRangeOf(Space, Va, &Ref) == NULL)
        return NULL;
    return Ref->Page != NULL ? Ref : NULL;
}

BOOLEAN
MiCloneIntersects(PMI_ADDRESS_SPACE Space, ULONG64 Start, ULONG64 End)
{
    PMI_VAD_NODE Node = Space->CloneRoot.Root;
    ULONG64 First = Start >> PAGE_SHIFT;
    ULONG64 Last = (End - 1) >> PAGE_SHIFT;

    while (Node != NULL)
    {
        if (Node->EndingVpn < First)
            Node = Node->Right;
        else if (Node->StartingVpn > Last)
            Node = Node->Left;
        else
            return TRUE;
    }
    return FALSE;
}

static BOOLEAN
MiClonePteMayBeClone(PMI_ADDRESS_SPACE Space, MI_PTE Pte)
{
    if (MiArchPteIsValid(Pte))
        return MiPfnTracked(&Space->System->Pfn, MiArchPteFrame(Pte)) &&
               (MI_PFN_FLAGS(&Space->System->Pfn.Pfn[MiArchPteFrame(Pte)]) & MI_PFN_FLAG_PROTOTYPE) != 0;
    return MiSoftKind(Pte) == MiSoftPrototype;
}

#if DBG || defined(MM_HOST_TEST)
static VOID
MiAssertPublishedCloneRange(PMI_ADDRESS_SPACE Space, PMI_CLONE_RANGE Range)
{
    ULONG64 Pages, Live = 0, i;

    MI_ASSERT(Range->Node.StartingVpn <= Range->Node.EndingVpn);
    MI_ASSERT(MiVadFind(&Space->CloneRoot, Range->Node.StartingVpn) == &Range->Node);
    Pages = Range->Node.EndingVpn - Range->Node.StartingVpn + 1;
    for (i = 0; i < Pages; i++)
    {
        PMI_CLONE_PAGE Page = Range->Refs[i].Page;
        PMI_PTE Slot;
        MI_PTE Pte;

        if (Page == NULL)
            continue;
        Live++;
        MI_ASSERT(Page->System == Space->System && MI_ATOMIC_READ32(&Page->References) > 0);
        Slot = MiPtLookup(Space, (Range->Node.StartingVpn + i) << PAGE_SHIFT, NULL);
        MI_ASSERT(Slot != NULL);
        Pte = MiArchPteRead(Slot);
        if (MiArchPteIsValid(Pte))
            MI_ASSERT(MiPfnTracked(&Space->System->Pfn, MiArchPteFrame(Pte)));
        MI_ASSERT(MiClonePteMayBeClone(Space, Pte));
    }
    MI_ASSERT(Live == Range->Live);
}
#else
#define MiAssertPublishedCloneRange(Space, Range) ((void)0)
#endif

static PMI_CLONE_REF
MiCloneLookupPte(PMI_ADDRESS_SPACE Space, ULONG64 Va, MI_PTE Pte)
{
    if (!MiClonePteMayBeClone(Space, Pte))
        return NULL;
    return MiCloneLookup(Space, Va);
}

static VOID
MiCloneForgetRef(PMI_ADDRESS_SPACE Space, PMI_CLONE_RANGE Range, PMI_CLONE_REF Ref)
{
    MI_ASSERT(Range != NULL && Ref->Page != NULL && Range->Live != 0);
    MI_ASSERT(Range->Live <= Range->Node.EndingVpn - Range->Node.StartingVpn + 1);
    Ref->Page = NULL;
    if (--Range->Live == 0)
    {
        MiCloneRemoveRange(Space, Range);
        MI_FREE(Range);
    }
}

static VOID
MiCloneForget(PMI_ADDRESS_SPACE Space, ULONG64 Va)
{
    PMI_CLONE_REF Ref;
    PMI_CLONE_RANGE Range = MiCloneRangeOf(Space, Va, &Ref);

    MiCloneForgetRef(Space, Range, Ref);
}

static ULONG
MiCloneLeafProtection(ULONG Protection)
{
    if (MI_PROT_IS_WRITABLE(Protection))
        return (Protection & ~MI_PROT_ACCESS_MASK) |
               (MI_PROT_IS_EXECUTE(Protection) ? MI_PROT_EXECUTE_WRITECOPY : MI_PROT_WRITECOPY);
    return Protection;
}

static VOID
MiCloneDropShare(PMI_CLONE_PAGE Page, ULONG Frame)
{
    MiPfnShareDecrementEx(&Page->System->Pfn, Frame, FALSE, &Page->Proto,
                          MiSoftMake(MiSoftTransition, MiSoftProtection(MiArchPteRead(&Page->Proto)), Frame));
}

static VOID
MiCloneReference(PMI_CLONE_PAGE Page)
{
    LONG Previous = MI_ATOMIC_ADD32(&Page->References, 1);

    MI_ASSERT(Previous > 0 && Previous < 0x7fffffffL);
    UNREFERENCED_PARAMETER(Previous);
}

static VOID
MiCloneDereference(PMI_CLONE_PAGE Page)
{
    PMI_SYSTEM System = Page->System;
    LONG Previous = MI_ATOMIC_ADD32(&Page->References, -1);

    MI_ASSERT(Previous > 0);
    if (Previous != 1)
        return;

    for (;;)
    {
        MI_PTE Pte = MiArchPteRead(&Page->Proto);
        ULONG Frame = (ULONG)MiSoftValue(Pte);

        MI_ASSERT(MiSoftKind(Pte) != MiSoftResident);
        if (MiSoftKind(Pte) == MiSoftTransition)
        {
            if (!MiPfnReactivate(&System->Pfn, Frame, (ULONG64)(ULONG_PTR)&Page->Proto))
                continue;
            MiReleasePageBacking(System, Frame);
            MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
        }
        else if (MiSoftKind(Pte) == MiSoftPageFile)
        {
            MiPageFileReleaseSlot(MiSystemGetPageFile(System), MiSoftValue(Pte));
        }
        break;
    }
    MI_FREE(Page);
}

static VOID
MiCloneReleaseMapping(PMI_ADDRESS_SPACE Space, PMI_CLONE_REF Ref, ULONG64 Va,
                       PMI_PTE Slot, ULONG TableFrame, MI_PTE NewValue)
{
    MI_PTE Pte = MiArchPteRead(Slot);

    MiPtWrite(Space, Va, Slot, TableFrame, NewValue);
    if (MiArchPteIsValid(Pte))
    {
        ULONG Frame = (ULONG)MiArchPteFrame(Pte);

        MiArchTlbInvalidate(Va, 1, TRUE);
        if (MiArchPteIsDirty(Pte))
            MiPfnSetModified(&Space->System->Pfn, Frame);
        MiCloneDropShare(Ref->Page, Frame);
        MI_ATOMIC_ADD64(&Space->ResidentPages, -1);
    }
}

VOID
MiCloneReleaseBatched(PMI_CLONE_PAGE Page, ULONG Frame)
{
    MiCloneDropShare(Page, Frame);
    MiCloneDereference(Page);
}

BOOLEAN
MiCloneDeletePage(PMI_ADDRESS_SPACE Space, ULONG64 Va, PMI_PTE Slot, ULONG TableFrame, MI_PTE NewValue)
{
    MI_PTE Pte = MiArchPteRead(Slot);
    PMI_CLONE_RANGE Range;
    PMI_CLONE_REF Ref;
    PMI_CLONE_PAGE Page;

    if ((Space->CloneRoot.NodeCount == 0) || !MiClonePteMayBeClone(Space, Pte))
        return FALSE;
    Range = MiCloneRangeOf(Space, Va, &Ref);
    if ((Range == NULL) || (Ref->Page == NULL))
        return FALSE;
    Page = Ref->Page;
    if (MiArchPteIsValid(Pte) && (Space->TlbBatch != NULL))
    {
        ULONG Frame = (ULONG)MiArchPteFrame(Pte);

        MiPtWrite(Space, Va, Slot, TableFrame, NewValue);
        if (MiArchPteIsDirty(Pte))
            MiPfnSetModified(&Space->System->Pfn, Frame);
        MI_ATOMIC_ADD64(&Space->ResidentPages, -1);
        MiCloneForgetRef(Space, Range, Ref);
        MiTlbBatchAddClone(Space, Va, Frame, Page);
        return TRUE;
    }
    MiCloneReleaseMapping(Space, Ref, Va, Slot, TableFrame, NewValue);
    MiCloneForgetRef(Space, Range, Ref);
    MiCloneDereference(Page);
    return TRUE;
}

BOOLEAN
MiCloneProtectPage(PMI_ADDRESS_SPACE Space, ULONG64 Va, PMI_PTE Slot, ULONG TableFrame, ULONG Protection)
{
    PMI_CLONE_REF Ref = MiCloneLookupPte(Space, Va, MiArchPteRead(Slot));

    if (Ref == NULL)
        return FALSE;
    Ref->Protection = Protection;
    MiCloneReleaseMapping(Space, Ref, Va, Slot, TableFrame, MiSoftMake(MiSoftPrototype, Protection, 0));
    return TRUE;
}

BOOLEAN
MiCloneTrimPage(PMI_ADDRESS_SPACE Space, ULONG64 Va, PMI_PTE Slot, ULONG TableFrame)
{
    PMI_CLONE_REF Ref = MiCloneLookupPte(Space, Va, MiArchPteRead(Slot));

    if (Ref == NULL)
        return FALSE;
    MiCloneReleaseMapping(Space, Ref, Va, Slot, TableFrame,
                          MiSoftMake(MiSoftPrototype, Ref->Protection, 0));
    return TRUE;
}

static BOOLEAN
MiCloneAcquirePage(PMI_CLONE_PAGE Page, PULONG Frame)
{
    MI_PTE Pte = MiArchPteRead(&Page->Proto);

    *Frame = (ULONG)MiSoftValue(Pte);
    if (MiSoftKind(Pte) == MiSoftResident)
        return MiPfnShareIncrementIfMapped(&Page->System->Pfn, *Frame, &Page->Proto, Pte);
    if (MiSoftKind(Pte) == MiSoftTransition)
        return MiPfnReactivateEx(&Page->System->Pfn, *Frame, (ULONG64)(ULONG_PTR)&Page->Proto,
                                 &Page->Proto, Pte, MiSoftMake(MiSoftResident, MiSoftProtection(Pte), *Frame));
    return FALSE;
}

static NTSTATUS
MiCloneMakeResident(PMI_CLONE_PAGE Page)
{
    PMI_SYSTEM System = Page->System;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Frame;
    MI_PTE Pte;

    MI_MUTEX_ACQUIRE(&Page->Lock);
    if (MiCloneAcquirePage(Page, &Frame))
    {
        MiCloneDropShare(Page, Frame);
        goto Done;
    }

    Pte = MiArchPteRead(&Page->Proto);
    if (MiSoftKind(Pte) != MiSoftDemandZero && MiSoftKind(Pte) != MiSoftPageFile)
        goto Done;

    Frame = MiPfnAllocatePage(&System->Pfn,
                              MI_ALLOCATE_HIGH | (MiSoftKind(Pte) == MiSoftDemandZero ? MI_ALLOCATE_ZEROED : 0));
    if (Frame == MI_FRAME_INVALID)
    {
        Status = STATUS_NO_MEMORY;
        goto Done;
    }
    if (MiSoftKind(Pte) == MiSoftPageFile)
    {
        PMI_PAGEFILE PageFile = MiSystemGetPageFile(System);
        PVOID Mapping = MiPfnMapFrame(&System->Pfn, Frame);

        Status = PageFile != NULL
            ? PageFile->Ops.Read(PageFile->Context, MiSoftValue(Pte), Mapping)
            : STATUS_IN_PAGE_ERROR;
        MiPfnUnmapFrame(&System->Pfn, Mapping);
        if (!NT_SUCCESS(Status))
        {
            MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
            Status = STATUS_IN_PAGE_ERROR;
            goto Done;
        }
        MI_ATOMIC_ADD64(&PageFile->PagesRead, 1);
    }
    if (MI_PROT_LEAF_CACHE(MiSoftProtection(Pte)) != 0)
    {
        Status = MiPfnSetCache(&System->Pfn, Frame, MI_PROT_LEAF_CACHE(MiSoftProtection(Pte)));
        if (!NT_SUCCESS(Status))
        {
            MiPfnShareDecrement(&System->Pfn, Frame, TRUE);
            goto Done;
        }
    }
    MiPfnInitializePage(&System->Pfn, Frame, (ULONG64)(ULONG_PTR)&Page->Proto, 0, Pte, MI_PFN_FLAG_PROTOTYPE);
    MiArchPteWrite(&Page->Proto, MiSoftMake(MiSoftResident, MiSoftProtection(Pte), Frame));
    MiCloneDropShare(Page, Frame);
Done:
    MI_MUTEX_RELEASE(&Page->Lock);
    return Status;
}

static BOOLEAN
MiCloneTakeOver(PMI_ADDRESS_SPACE Space, PMI_CLONE_REF Ref, ULONG64 Va, PMI_PTE Slot, ULONG TableFrame,
                ULONG Frame, ULONG Protection)
{
    PMI_CLONE_PAGE Page = Ref->Page;
    PMI_SYSTEM System = Space->System;
    PMI_PFN Entry = &System->Pfn.Pfn[Frame];
    KIRQL OldIrql;

    if (MI_ATOMIC_READ32(&Page->References) != 1)
        return FALSE;

    OldIrql = MiPfnLock(&System->Pfn, Frame);
    if (Entry->State != MiPageActive || Entry->ShareCount != 1 || Entry->ReferenceCount != 1 ||
        (MI_PFN_FLAGS(Entry) & (MI_PFN_FLAG_IN_FLIGHT | MI_PFN_FLAG_DELETED | MI_PFN_FLAG_PINNED)) ||
        !(MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_PROTOTYPE) || Entry->PteAddress != (ULONG64)(ULONG_PTR)&Page->Proto)
    {
        MiPfnUnlock(&System->Pfn, Frame, OldIrql);
        return FALSE;
    }
    Entry->PteAddress = MiPtSlotAddress(Slot, TableFrame, Va);
    Entry->PteFrame = TableFrame;
    Entry->OriginalPte = MiSoftWithProtection(Entry->OriginalPte, Protection);
    MI_ATOMIC_AND8(&Entry->Flags, (UCHAR)~MI_PFN_FLAG_PROTOTYPE);
    MiPfnUnlock(&System->Pfn, Frame, OldIrql);

    MiPtWrite(Space, Va, Slot, TableFrame, MiSoftMake(MiSoftPrototype, Protection, 0));
    MiArchTlbInvalidate(Va, 1, TRUE);
    MI_ATOMIC_ADD64(&Space->ResidentPages, -1);
    MiMakePageValid(Space, Va, Slot, TableFrame, Frame, Protection, TRUE);
    MI_ATOMIC_ADD64(&Space->PrivatePages, 1);
    MiCloneForget(Space, Va);
    MI_ASSERT(MI_ATOMIC_READ32(&Page->References) == 1);
    MI_FREE(Page);
    return TRUE;
}

NTSTATUS
MiCloneFault(PMI_ADDRESS_SPACE Space, ULONG64 Va, ULONG Access, BOOLEAN ConsumeGuard)
{
    PMI_SYSTEM System = Space->System;
    ULONG Attempts = 0;
    NTSTATUS Status;

    for (;;)
    {
        PMI_CLONE_REF Ref;
        PMI_CLONE_PAGE Page;
        PMI_PTE Slot;
        ULONG TableFrame, Frame, Protection;
        MI_PTE Pte;

        MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
        Ref = MiCloneLookup(Space, Va);
        if (Ref == NULL)
        {
            Status = STATUS_PENDING_COPY;
            break;
        }
        Page = Ref->Page;
        Slot = MiPtLookup(Space, Va, &TableFrame);
        MI_ASSERT(Slot != NULL);
        Protection = Ref->Protection;
        if ((Protection & MI_PROT_NOACCESS) == MI_PROT_GUARD)
        {
            if (ConsumeGuard)
                MiCloneProtectPage(Space, Va, Slot, TableFrame, Protection & ~MI_PROT_GUARD);
            Status = STATUS_GUARD_PAGE_VIOLATION;
            break;
        }
        if (!MI_PROT_IS_ACCESSIBLE(Protection) ||
            (Access == MiFaultWrite && !MI_PROT_IS_WRITABLE(Protection) && !MI_PROT_IS_COPY(Protection)) ||
            (Access == MiFaultExecute && !MI_PROT_IS_EXECUTE(Protection)))
        {
            Status = STATUS_ACCESS_VIOLATION;
            break;
        }
        Pte = MiArchPteRead(Slot);
        if (!MiArchPteIsValid(Pte))
        {
            if (!MiCloneAcquirePage(Page, &Frame))
            {
                MiCloneReference(Page);
                MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
                Status = MiCloneMakeResident(Page);
                MiCloneDereference(Page);
                if (!NT_SUCCESS(Status))
                    return Status;
                if (++Attempts == 64)
                    return STATUS_NO_MEMORY;
                continue;
            }
            MiMakePageValid(Space, Va, Slot, TableFrame, Frame, MiCloneLeafProtection(Protection), FALSE);
            Pte = MiArchPteRead(Slot);
            MI_ATOMIC_ADD64(&Space->PrototypeFaults, 1);
        }
        Frame = (ULONG)MiArchPteFrame(Pte);
        if ((Access == MiFaultWrite) &&
            MiCloneTakeOver(Space, Ref, Va, Slot, TableFrame, Frame,
                            (Protection & ~MI_PROT_ACCESS_MASK) |
                            (MI_PROT_IS_EXECUTE(Protection) ? MI_PROT_EXECUTE_READWRITE : MI_PROT_READWRITE)))
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (Access == MiFaultWrite)
        {
            ULONG NewFrame = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_HIGH);
            PVOID Source, Target;

            if (NewFrame == MI_FRAME_INVALID)
            {
                Status = STATUS_NO_MEMORY;
                break;
            }
            Source = MiPfnMapFrame(&System->Pfn, Frame);
            Target = MiPfnMapFrame(&System->Pfn, NewFrame);
            RtlCopyMemory(Target, Source, PAGE_SIZE);
            MiPfnUnmapFrame(&System->Pfn, Target);
            MiPfnUnmapFrame(&System->Pfn, Source);
            if (MI_PROT_LEAF_CACHE(Protection) != 0)
            {
                Status = MiPfnSetCache(&System->Pfn, NewFrame, MI_PROT_LEAF_CACHE(Protection));
                if (!NT_SUCCESS(Status))
                {
                    MiPfnShareDecrement(&System->Pfn, NewFrame, TRUE);
                    break;
                }
            }
            Protection = (Protection & ~MI_PROT_ACCESS_MASK) |
                         (MI_PROT_IS_EXECUTE(Protection) ? MI_PROT_EXECUTE_READWRITE : MI_PROT_READWRITE);
            MiPfnInitializePage(&System->Pfn, NewFrame, MiPtSlotAddress(Slot, TableFrame, Va), TableFrame,
                                MiSoftMake(MiSoftDemandZero, Protection, 0), 0);
            MiCloneReleaseMapping(Space, Ref, Va, Slot, TableFrame,
                                  MiSoftMake(MiSoftPrototype, Protection, 0));
            MiMakePageValid(Space, Va, Slot, TableFrame, NewFrame, Protection, TRUE);
            MI_ATOMIC_ADD64(&Space->PrivatePages, 1);
            MI_ATOMIC_ADD64(&Space->CopyOnWriteFaults, 1);
            MiCloneForget(Space, Va);
            MiCloneDereference(Page);
        }
        else
        {
            MiArchPteWrite(Slot, MiArchPteSetAccessed(Pte, TRUE));
            MiArchTlbInvalidate(Va, 1, FALSE);
        }
        Status = STATUS_SUCCESS;
        break;
    }
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}

static BOOLEAN
MiClonePrivatePte(PMI_SYSTEM System, MI_PTE Pte)
{
    if (MiArchPteIsValid(Pte))
        return !MiPfnTracked(&System->Pfn, MiArchPteFrame(Pte)) ||
               !(MI_PFN_FLAGS(&System->Pfn.Pfn[MiArchPteFrame(Pte)]) & MI_PFN_FLAG_PROTOTYPE);
    return MiSoftKind(Pte) == MiSoftTransition || MiSoftKind(Pte) == MiSoftPageFile;
}

static NTSTATUS
MiClonePreparePage(PMI_ADDRESS_SPACE Source, ULONG64 Va, MI_PTE Pte, PMI_CLONE_REF Existing, PMI_CLONE_PLAN Plan)
{
    PMI_CLONE_WORK Work;

    if (Plan->Count == Plan->Capacity)
    {
        ULONG64 Capacity = Plan->Capacity != 0 ? Plan->Capacity * 2 : 256;
        PMI_CLONE_WORK Grown = MI_ALLOCATE((SIZE_T)(Capacity * sizeof(*Grown)));

        if (Grown == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (Plan->Count != 0)
            RtlCopyMemory(Grown, Plan->Work, (SIZE_T)(Plan->Count * sizeof(*Grown)));
        if (Plan->Work != NULL)
            MI_FREE(Plan->Work);
        Plan->Work = Grown;
        Plan->Capacity = Capacity;
    }
    Work = &Plan->Work[Plan->Count++];
    Work->Va = Va;
    Work->Existing = Existing;
    Work->Source = NULL;
    Work->SourceRange = NULL;
    Work->Target = NULL;
    Work->TargetRange = NULL;
    Work->Page = NULL;
    Work->CopyFrame = MI_FRAME_INVALID;
    Work->Flush = FALSE;
    if (Existing != NULL)
        return STATUS_SUCCESS;

    Work->Page = MI_ALLOCATE(sizeof(*Work->Page));
    if (Work->Page == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Work->Page, sizeof(*Work->Page));
    Work->Page->System = Source->System;
    MI_MUTEX_INIT(&Work->Page->Lock);
    if (MiArchPteIsValid(Pte) || MiSoftKind(Pte) == MiSoftTransition)
    {
        ULONG Frame = (ULONG)(MiArchPteIsValid(Pte) ? MiArchPteFrame(Pte) : MiSoftValue(Pte));
        PMI_PFN Entry = &Source->System->Pfn.Pfn[Frame];
        KIRQL OldIrql = MiPfnLock(&Source->System->Pfn, Frame);
        BOOLEAN Locked = Entry->ReferenceCount > (Entry->ShareCount != 0 ? 1 : 0) +
                                                ((MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_IN_FLIGHT) != 0);
        ULONG CacheFlags = MI_ATOMIC_READ32(&Entry->CacheFlags);

        MiPfnUnlock(&Source->System->Pfn, Frame, OldIrql);
        if (Locked)
        {
            Work->CopyFrame = MiPfnAllocatePage(&Source->System->Pfn, MI_ALLOCATE_HIGH);
            if (Work->CopyFrame == MI_FRAME_INVALID)
                return STATUS_NO_MEMORY;
            if (CacheFlags != 0)
                return MiPfnSetCache(&Source->System->Pfn, Work->CopyFrame, CacheFlags);
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
MiCloneAddRange(PMI_CLONE_PLAN Plan, PMI_CLONE_WORK First, ULONG64 Pages, BOOLEAN Source)
{
    PMI_CLONE_RANGE Range;
    SIZE_T Size = FIELD_OFFSET(MI_CLONE_RANGE, Refs) + (SIZE_T)(Pages * sizeof(MI_CLONE_REF));
    ULONG64 i;

    if (Plan->RangeCount == Plan->RangeCapacity)
    {
        ULONG64 Capacity = Plan->RangeCapacity != 0 ? Plan->RangeCapacity * 2 : 16;
        PMI_CLONE_RANGE *Grown = MI_ALLOCATE((SIZE_T)(Capacity * sizeof(*Grown)));

        if (Grown == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        if (Plan->RangeCount != 0)
            RtlCopyMemory(Grown, Plan->Ranges, (SIZE_T)(Plan->RangeCount * sizeof(*Grown)));
        if (Plan->Ranges != NULL)
            MI_FREE(Plan->Ranges);
        Plan->Ranges = Grown;
        Plan->RangeCapacity = Capacity;
    }
    Range = MI_ALLOCATE(Size);
    if (Range == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Range, Size);
    Range->Node.StartingVpn = First->Va >> PAGE_SHIFT;
    Range->Node.EndingVpn = Range->Node.StartingVpn + Pages - 1;
    Plan->Ranges[Plan->RangeCount++] = Range;
    for (i = 0; i < Pages; i++)
    {
        if (Source)
        {
            First[i].Source = &Range->Refs[i];
            First[i].SourceRange = Range;
        }
        else
        {
            First[i].Target = &Range->Refs[i];
            First[i].TargetRange = Range;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
MiClonePlanRanges(PMI_ADDRESS_SPACE Source, PMI_CLONE_PLAN Plan)
{
    ULONG64 i, Run;
    NTSTATUS Status;

    for (i = 0; i < Plan->Count; i = Run)
    {
        for (Run = i + 1; Run < Plan->Count && Plan->Work[Run].Va == Plan->Work[Run - 1].Va + PAGE_SIZE; Run++)
            ;
        Status = MiCloneAddRange(Plan, &Plan->Work[i], Run - i, FALSE);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    Plan->FirstSourceRange = Plan->RangeCount;

    for (i = 0; i < Plan->Count; i = Run)
    {
        PMI_CLONE_WORK Work = &Plan->Work[i];
        PMI_CLONE_RANGE Covering;
        PMI_CLONE_REF Slot;

        Run = i + 1;
        if (Work->Existing != NULL)
            continue;
        Covering = MiCloneRangeOf(Source, Work->Va, &Slot);
        if (Covering != NULL)
        {
            MI_ASSERT(Slot->Page == NULL);
            Work->Source = Slot;
            Work->SourceRange = Covering;
            continue;
        }
        while (Run < Plan->Count && Plan->Work[Run].Existing == NULL &&
               Plan->Work[Run].Va == Plan->Work[Run - 1].Va + PAGE_SIZE &&
               MiCloneRangeOf(Source, Plan->Work[Run].Va, &Slot) == NULL)
        {
            Run++;
        }
        Status = MiCloneAddRange(Plan, Work, Run - i, TRUE);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

static VOID
MiClonePublishPage(PMI_ADDRESS_SPACE Source, PMI_ADDRESS_SPACE Target, PMI_CLONE_PLAN Plan,
                   PMI_CLONE_WORK Work, PMI_VAD *VadCache)
{
    ULONG64 Va = Work->Va;
    PMI_CLONE_TABLES Tables = &Plan->Tables;
    ULONG SourceTable, TargetTable;
    PMI_PTE SourceSlot, TargetSlot;
    PMI_CLONE_PAGE Page = Work->Page;
    MI_PTE Pte;
    ULONG Frame = MI_FRAME_INVALID;
    ULONG Protection, PrototypeProtection, AllocationProtection;
    PMI_VAD Vad;
    BOOLEAN Valid;

    if (Tables->Source == NULL || Va < Tables->Va || Va >= Tables->End)
    {
        Tables->Va = Va;
        Tables->End = MiPtNextTableBoundary(Source, Va);
        Tables->Source = MiPtLookup(Source, Va, &Tables->SourceTable);
        Tables->Target = MiPtLookup(Target, Va, &Tables->TargetTable);
    }
    MI_ASSERT(Tables->Source != NULL && Tables->Target != NULL);
    SourceSlot = Tables->Source + ((Va - Tables->Va) >> PAGE_SHIFT);
    TargetSlot = Tables->Target + ((Va - Tables->Va) >> PAGE_SHIFT);
    SourceTable = Tables->SourceTable;
    TargetTable = Tables->TargetTable;

    MI_ASSERT(Work->Target != NULL && Work->TargetRange != NULL);
    MI_ASSERT(Work->Target->Page == NULL);
    if (Page == NULL)
    {
        MI_ASSERT(Work->Existing != NULL && Work->Existing->Page != NULL);
        *Work->Target = *Work->Existing;
        MiCloneReference(Work->Target->Page);
        goto TargetReady;
    }

    for (;;)
    {
        Pte = MiArchPteRead(SourceSlot);
        Valid = MiArchPteIsValid(Pte);
        if (Valid)
            Frame = (ULONG)MiArchPteFrame(Pte);
        else if (MiSoftKind(Pte) == MiSoftTransition)
        {
            Frame = (ULONG)MiSoftValue(Pte);
            if (!MiPfnReactivate(&Source->System->Pfn, Frame, MiPtSlotAddress(SourceSlot, SourceTable, Va)))
                continue;
        }
        else
            Frame = MI_FRAME_INVALID;
        break;
    }
    if (!MiClonePrivatePte(Source->System, Pte))
    {
        MiPtWrite(Target, Va, TargetSlot, TargetTable, Pte);
        return;
    }

    Vad = *VadCache;
    if (Vad == NULL || (Va >> PAGE_SHIFT) < Vad->Node.StartingVpn || (Va >> PAGE_SHIFT) > Vad->Node.EndingVpn)
        Vad = *VadCache = MiVadLocate(Source, Va);
    AllocationProtection = Vad->Segment != NULL ? Vad->Segment->Protection : Vad->Protection;
    PrototypeProtection = MI_PROT_READWRITE;
    if (MI_PROT_IS_ACCESSIBLE(AllocationProtection))
        PrototypeProtection |= MI_PROT_CACHE(AllocationProtection);
    Protection = Valid ? MiViewPteProtection(Source, Vad, Va, Pte) : MiSoftProtection(Pte);
    Work->Target->Protection = Protection;
    Work->Target->Page = Page;
    Page->References = 1;
    if (Frame != MI_FRAME_INVALID && Work->CopyFrame != MI_FRAME_INVALID)
    {
        PVOID From = MiPfnMapFrame(&Source->System->Pfn, Frame);
        PVOID To = MiPfnMapFrame(&Source->System->Pfn, Work->CopyFrame);

        RtlCopyMemory(To, From, PAGE_SIZE);
        MiPfnUnmapFrame(&Source->System->Pfn, To);
        MiPfnUnmapFrame(&Source->System->Pfn, From);
        if (!Valid)
            MiPfnShareDecrement(&Source->System->Pfn, Frame, FALSE);
        Frame = Work->CopyFrame;
        Work->CopyFrame = MI_FRAME_INVALID;
        MiPfnInitializePage(&Source->System->Pfn, Frame, (ULONG64)(ULONG_PTR)&Page->Proto, 0,
                            MiSoftMake(MiSoftDemandZero, PrototypeProtection, 0), MI_PFN_FLAG_PROTOTYPE);
        MiPfnSetModified(&Source->System->Pfn, Frame);
        MiArchPteWrite(&Page->Proto, MiSoftMake(MiSoftResident, PrototypeProtection, Frame));
        MiCloneDropShare(Page, Frame);
    }
    else
    {
        MI_ASSERT(Work->Source != NULL && Work->SourceRange != NULL && Work->Source->Page == NULL);
        MI_ASSERT(Work->SourceRange->Live <
                  Work->SourceRange->Node.EndingVpn - Work->SourceRange->Node.StartingVpn + 1);
        *Work->Source = *Work->Target;
        Work->SourceRange->Live++;
        Page->References++;
        if (Valid)
        {
            MiPtWrite(Source, Va, SourceSlot, SourceTable,
                       MiArchPteMakeLeaf(Frame, MiCloneLeafProtection(Protection), MI_LEAF_USER));
            Work->Flush = TRUE;
            Plan->Flushes++;
            if (MiArchPteIsDirty(Pte))
                MiPfnSetModified(&Source->System->Pfn, Frame);
        }
        else
        {
            MiPtWrite(Source, Va, SourceSlot, SourceTable, MiSoftMake(MiSoftPrototype, Protection, 0));
            MI_ATOMIC_ADD64(&Source->PageFileGeneration, 1);
        }
        if (Frame != MI_FRAME_INVALID)
        {
            PMI_PFN Entry = &Source->System->Pfn.Pfn[Frame];
            KIRQL OldIrql;

            while (MI_PFN_FLAGS(Entry) & MI_PFN_FLAG_IN_FLIGHT)
                MI_PAUSE();
            OldIrql = MiPfnLock(&Source->System->Pfn, Frame);
            Entry->PteAddress = (ULONG64)(ULONG_PTR)&Page->Proto;
            Entry->PteFrame = 0;
            Entry->OriginalPte = MiSoftWithProtection(Entry->OriginalPte, PrototypeProtection);
            MI_ATOMIC_OR8(&Entry->Flags, MI_PFN_FLAG_PROTOTYPE);
            MiArchPteWrite(&Page->Proto, MiSoftMake(MiSoftResident, PrototypeProtection, Frame));
            MiPfnUnlock(&Source->System->Pfn, Frame, OldIrql);
            if (!Valid)
                MiCloneDropShare(Page, Frame);
        }
        else
            MiArchPteWrite(&Page->Proto, MiSoftWithProtection(Pte, PrototypeProtection));
        MI_ATOMIC_ADD64(&Source->PrivatePages, -1);
    }
    Work->Page = NULL;
TargetReady:
    Protection = Work->Target->Protection;
    if ((Page != NULL) && MI_PROT_IS_ACCESSIBLE(Protection) && ((Protection & MI_PROT_NOACCESS) != MI_PROT_GUARD) &&
        (MiSoftKind(MiArchPteRead(&Work->Target->Page->Proto)) == MiSoftResident) &&
        MiCloneAcquirePage(Work->Target->Page, &Frame))
    {
        MiMakePageValid(Target, Va, TargetSlot, TargetTable, Frame, MiCloneLeafProtection(Protection), FALSE);
    }
    else
    {
        MiPtWrite(Target, Va, TargetSlot, TargetTable, MiSoftMake(MiSoftPrototype, Protection, 0));
    }
    MI_ASSERT(Work->Target->Page != NULL);
    MI_ASSERT(Work->TargetRange->Live <
              Work->TargetRange->Node.EndingVpn - Work->TargetRange->Node.StartingVpn + 1);
    Work->TargetRange->Live++;
}

static VOID
MiCloneFreePlan(PMI_SYSTEM System, PMI_CLONE_PLAN Plan)
{
    ULONG64 i;

    for (i = 0; i < Plan->Count; i++)
    {
        if (Plan->Work[i].CopyFrame != MI_FRAME_INVALID)
            MiPfnShareDecrement(&System->Pfn, Plan->Work[i].CopyFrame, TRUE);
        if (Plan->Work[i].Page != NULL)
            MI_FREE(Plan->Work[i].Page);
    }
    for (i = 0; i < Plan->RangeCount; i++)
    {
        if (Plan->Ranges[i] != NULL)
            MI_FREE(Plan->Ranges[i]);
    }
    if (Plan->Work != NULL)
        MI_FREE(Plan->Work);
    if (Plan->Ranges != NULL)
        MI_FREE(Plan->Ranges);
}

static VOID
MiCloneRollback(PMI_ADDRESS_SPACE Target)
{
    PMI_VAD_NODE Node;

    while ((Node = MiVadFirst(&Target->VadRoot)) != NULL)
    {
        PMI_VAD Vad = CONTAINING_RECORD(Node, MI_VAD, Node);
        ULONG64 Va = Node->StartingVpn << PAGE_SHIFT;
        ULONG64 End = (Node->EndingVpn + 1) << PAGE_SHIFT;

        if (Vad->Type == MiVadLarge)
        {
            PMI_SEGMENT Segment = MiReleaseLargePagesLocked(Target, Vad);

            if (Segment != NULL)
                MiSegmentDereference(Segment);
            continue;
        }
        while (Va < End)
        {
            ULONG64 Next = MiPtNextTableBoundary(Target, Va);

            MiPtPruneEmpty(Target, Va);
            Va = Next;
        }
        MiReturnCommit(Target, Vad->CommitCharge);
        if (Vad->Segment != NULL)
        {
            MiAdjustSegmentCount(&Vad->Segment->MappedViews, -1);
            if (!Vad->CacheView)
                MiAdjustSegmentCount(&Vad->Segment->TruncationViews, -1);
            if (Vad->WritableUser)
                MiAdjustSegmentCount(&Vad->Segment->WritableUserViews, -1);
            MiSegmentDereference(Vad->Segment);
        }
        MiVadRemove(&Target->VadRoot, Node);
        MiWriteWatchRelease(Vad);
        MI_FREE(Vad);
    }
}

NTSTATUS
MiCloneAddressSpace(PMI_ADDRESS_SPACE Source, PMI_ADDRESS_SPACE Target)
{
    PMI_ADDRESS_SPACE First = (ULONG_PTR)Source < (ULONG_PTR)Target ? Source : Target;
    PMI_ADDRESS_SPACE Second = First == Source ? Target : Source;
    PMI_VAD_NODE Node;
    MI_CLONE_PLAN Plan;
    PMI_VAD VadCache = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG64 i;

    RtlZeroMemory(&Plan, sizeof(Plan));
    if (Source == Target || Source->System != Target->System || Source->IsSystem || Target->IsSystem)
        return STATUS_INVALID_PARAMETER;
    MI_RW_ACQUIRE_EXCLUSIVE(&First->Lock);
    MI_RW_ACQUIRE_EXCLUSIVE(&Second->Lock);
    if (Target->VadRoot.NodeCount != 0)
    {
        Status = STATUS_CONFLICTING_ADDRESSES;
        goto Done;
    }

    for (Node = MiVadFirst(&Source->VadRoot); Node != NULL; Node = MiVadNext(Node))
    {
        PMI_VAD Original = CONTAINING_RECORD(Node, MI_VAD, Node);
        PMI_VAD Vad;
        ULONG64 Va = Node->StartingVpn << PAGE_SHIFT;
        ULONG64 End = (Node->EndingVpn + 1) << PAGE_SHIFT;

        if (Original->Type == MiVadAwe || Original->Type == MiVadPhysical || Original->Type == MiVadRotate ||
            (Original->Segment != NULL && !Original->Inherit))
            continue;
        if (MI_VAD_IS_DIRECT(Original) && !(Original->Type == MiVadLarge && Original->Segment != NULL))
        {
            Status = STATUS_NOT_SUPPORTED;
            goto Rollback;
        }
        Vad = MI_ALLOCATE(sizeof(*Vad));
        if (Vad == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Rollback;
        }
        *Vad = *Original;
        if (!NT_SUCCESS(MiWriteWatchDuplicate(Target, Vad)))
        {
            MI_FREE(Vad);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Rollback;
        }
        if (!MiChargeCommit(Target, Vad->CommitCharge))
        {
            MiWriteWatchRelease(Vad);
            MI_FREE(Vad);
            Status = STATUS_COMMITMENT_LIMIT;
            goto Rollback;
        }
        if (!MiVadInsert(&Target->VadRoot, &Vad->Node))
            MI_ASSERT(FALSE);
        if (Vad->Segment != NULL)
        {
            MiSegmentReference(Vad->Segment);
            MiAdjustSegmentCount(&Vad->Segment->MappedViews, 1);
            if (!Vad->CacheView)
                MiAdjustSegmentCount(&Vad->Segment->TruncationViews, 1);
            if (Vad->WritableUser)
                MiAdjustSegmentCount(&Vad->Segment->WritableUserViews, 1);
        }
        if (Vad->Type == MiVadLarge)
        {
            Status = MiCloneLargeViewLocked(Source, Target, Vad);
            if (!NT_SUCCESS(Status))
                goto Rollback;
            continue;
        }
        while (Va < End)
        {
            ULONG64 TableEnd = MiPtNextTableBoundary(Source, Va);
            PMI_PTE Slot = MiPtLookup(Source, Va, NULL);
            BOOLEAN TargetReady = FALSE;
            BOOLEAN Clones;
            ULONG TargetTable;

            if (TableEnd > End)
                TableEnd = End;
            if (Slot == NULL)
            {
                Va = TableEnd;
                continue;
            }
            Clones = MiCloneIntersects(Source, Va, TableEnd);
            for (; Va < TableEnd; Va += PAGE_SIZE, Slot++)
            {
                MI_PTE Pte = MiArchPteRead(Slot);
                PMI_CLONE_REF Existing;
                BOOLEAN Copy;

                if (Pte == 0)
                    continue;
                Existing = Clones ? MiCloneLookupPte(Source, Va, Pte) : NULL;
                Copy = (Existing != NULL) || MiClonePrivatePte(Source->System, Pte);
                if (!Copy && MiViewPteIsDefault(Source, Original, Va, Pte))
                    continue;
                if (!TargetReady)
                {
                    if (MiPtEnsure(Target, Va, &TargetTable) == NULL)
                    {
                        Status = STATUS_NO_MEMORY;
                        goto Rollback;
                    }
                    TargetReady = TRUE;
                }
                if (Copy)
                {
                    Status = MiClonePreparePage(Source, Va, Pte, Existing, &Plan);
                    if (!NT_SUCCESS(Status))
                        goto Rollback;
                }
            }
        }
    }

    Status = MiClonePlanRanges(Source, &Plan);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    for (Node = MiVadFirst(&Target->VadRoot); Node != NULL; Node = MiVadNext(Node))
    {
        PMI_VAD Original = MiVadLocate(Source, Node->StartingVpn << PAGE_SHIFT);
        ULONG64 Va = Node->StartingVpn << PAGE_SHIFT;
        ULONG64 End = (Node->EndingVpn + 1) << PAGE_SHIFT;

        if (Original->Type == MiVadLarge)
            continue;
        while (Va < End)
        {
            ULONG64 TableEnd = MiPtNextTableBoundary(Source, Va);
            ULONG Table;
            PMI_PTE Slot = MiPtLookup(Source, Va, NULL);
            PMI_PTE Destination = MiPtLookup(Target, Va, &Table);
            BOOLEAN Clones;

            if (TableEnd > End)
                TableEnd = End;
            if (Slot == NULL || Destination == NULL)
            {
                Va = TableEnd;
                continue;
            }
            Clones = MiCloneIntersects(Source, Va, TableEnd);
            for (; Va < TableEnd; Va += PAGE_SIZE, Slot++, Destination++)
            {
                MI_PTE Pte = MiArchPteRead(Slot);

                if ((Pte == 0) || ((Original->Type == MiVadPrivate) &&
                                   (MiArchPteIsValid(Pte) || MiClonePrivatePte(Source->System, Pte) ||
                                    (MiSoftKind(Pte) == MiSoftPrototype))))
                {
                    continue;
                }
                if (!(Clones && MiCloneLookupPte(Source, Va, Pte) != NULL) &&
                    !MiClonePrivatePte(Source->System, Pte) && !MiViewPteIsDefault(Source, Original, Va, Pte))
                {
                    if (MiArchPteIsValid(Pte))
                        Pte = MiSoftMake(MiSoftPrototype, MiViewPteProtection(Source, Original, Va, Pte), 0);
                    MiPtWrite(Target, Va, Destination, Table, Pte);
                }
            }
        }
    }
    for (i = 0; i < Plan.RangeCount; i++)
    {
        if (!MiVadInsert(&((i < Plan.FirstSourceRange) ? Target : Source)->CloneRoot, &Plan.Ranges[i]->Node))
            MI_ASSERT(FALSE);
    }
    for (i = 0; i < Plan.Count; i++)
        MiClonePublishPage(Source, Target, &Plan, &Plan.Work[i], &VadCache);
    if (Plan.Flushes > MI_CLONE_FLUSH_ALL)
    {
        MiArchTlbInvalidateAll(TRUE);
    }
    else
    {
        for (i = 0; i < Plan.Count; i++)
        {
            if (Plan.Work[i].Flush)
                MiArchTlbInvalidate(Plan.Work[i].Va, 1, TRUE);
        }
    }
    for (i = 0; i < Plan.RangeCount; i++)
    {
        MiAssertPublishedCloneRange((i < Plan.FirstSourceRange) ? Target : Source, Plan.Ranges[i]);
        if (Plan.Ranges[i]->Live == 0)
            MiCloneRemoveRange((i < Plan.FirstSourceRange) ? Target : Source, Plan.Ranges[i]);
        else
            Plan.Ranges[i] = NULL;
    }
    goto Done;

Rollback:
    MiCloneRollback(Target);
Done:
    MiCloneFreePlan(Source->System, &Plan);
    MI_RW_RELEASE_EXCLUSIVE(&Second->Lock);
    MI_RW_RELEASE_EXCLUSIVE(&First->Lock);
    return Status;
}
