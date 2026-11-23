/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Process control flow guard bitmap backing
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <mm/include/mm.h>

typedef struct _MI_CFG_BITMAP
{
    MI_VAD Vad;
    MI_VAD_ROOT Pages;
} MI_CFG_BITMAP, *PMI_CFG_BITMAP;

typedef struct _MI_CFG_PAGE
{
    MI_VAD_NODE Node;
    ULONG Frame;
    struct _MI_CFG_PAGE *PendingNext;
} MI_CFG_PAGE, *PMI_CFG_PAGE;

NTSTATUS
MiCfgCreateBitmapLocked(PMI_ADDRESS_SPACE Space, ULONG64 Base, ULONG64 Size)
{
    PMI_CFG_BITMAP Bitmap;
    ULONG64 Vpn = Base >> PAGE_SHIFT;
    ULONG64 Pages = Size >> PAGE_SHIFT;

    if (Space->IsSystem || Space->CfgBitmap != NULL || !Pages ||
        (Size & (PAGE_SIZE - 1)) || (Base & (Space->Granularity - 1)))
        return STATUS_INVALID_PARAMETER;

    if (!Base)
    {
        if (!MiSpaceFindEmptyRange(Space, Pages, Space->Granularity >> PAGE_SHIFT,
                                   0, ~0ULL, TRUE, &Vpn))
            return STATUS_NO_MEMORY;
    }
    else if (Base < Space->LowestVa || Base > Space->HighestVa || Size - 1 > Space->HighestVa - Base)
        return STATUS_INVALID_PARAMETER;

    Bitmap = MI_ALLOCATE(sizeof(*Bitmap));
    if (!Bitmap)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Bitmap, sizeof(*Bitmap));
    Bitmap->Vad.Type = MiVadCfgBitmap;
    Bitmap->Vad.Protection = MI_PROT_NOACCESS;
    Bitmap->Vad.Node.StartingVpn = Vpn;
    Bitmap->Vad.Node.EndingVpn = Vpn + Pages - 1;
    MiVadRootInitialize(&Bitmap->Pages, Vpn, Vpn + Pages - 1);
    if (!MiVadInsert(&Space->VadRoot, &Bitmap->Vad.Node))
    {
        MI_FREE(Bitmap);
        return STATUS_CONFLICTING_ADDRESSES;
    }
    Space->CfgBitmap = &Bitmap->Vad;
    return STATUS_SUCCESS;
}

NTSTATUS
MiCfgGetBitmapPageLocked(PMI_ADDRESS_SPACE Space, ULONG64 Offset, BOOLEAN Create, PULONG Frame)
{
    PMI_VAD Vad = Space->CfgBitmap;
    PMI_CFG_BITMAP Bitmap;
    PMI_CFG_PAGE Page;
    PMI_VAD_NODE Node;
    ULONG64 Vpn, Va;
    PMI_PTE Slot;
    ULONG Table;

    *Frame = MI_FRAME_INVALID;
    if (!Vad || (Offset >> PAGE_SHIFT) > Vad->Node.EndingVpn - Vad->Node.StartingVpn)
        return STATUS_INVALID_PARAMETER;
    Bitmap = CONTAINING_RECORD(Vad, MI_CFG_BITMAP, Vad);
    Vpn = Vad->Node.StartingVpn + (Offset >> PAGE_SHIFT);
    Node = MiVadFind(&Bitmap->Pages, Vpn);
    if (Node)
    {
        *Frame = CONTAINING_RECORD(Node, MI_CFG_PAGE, Node)->Frame;
        return STATUS_SUCCESS;
    }
    if (!Create)
        return STATUS_NOT_COMMITTED;

    Page = MI_ALLOCATE(sizeof(*Page));
    if (!Page)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (!MiChargeCommit(Space, 1))
    {
        MI_FREE(Page);
        return STATUS_COMMITMENT_LIMIT;
    }
    Va = Vpn << PAGE_SHIFT;
    Slot = MiPtEnsure(Space, Va, &Table);
    Page->Frame = Slot ? MiPfnAllocatePage(&Space->System->Pfn, MI_ALLOCATE_ZEROED) : MI_FRAME_INVALID;
    if (Page->Frame == MI_FRAME_INVALID)
    {
        MiPtPruneEmpty(Space, Va);
        MiReturnCommit(Space, 1);
        MI_FREE(Page);
        return STATUS_NO_MEMORY;
    }
    MI_ASSERT(MiArchPteRead(Slot) == 0);
    Page->Node.StartingVpn = Page->Node.EndingVpn = Vpn;
    Page->PendingNext = NULL;
    if (!MiVadInsert(&Bitmap->Pages, &Page->Node))
    {
        MI_ASSERT(FALSE);
        MiPfnShareDecrement(&Space->System->Pfn, Page->Frame, TRUE);
        MiPtPruneEmpty(Space, Va);
        MiReturnCommit(Space, 1);
        MI_FREE(Page);
        return STATUS_CONFLICTING_ADDRESSES;
    }
    MiPfnInitializePage(&Space->System->Pfn, Page->Frame, MiPtSlotAddress(Slot, Table, Va), Table,
                        MiSoftMake(MiSoftDemandZero, MI_PROT_READONLY, 0), 0);
    MiPtWrite(Space, Va, Slot, Table, MiArchPteMakeLeaf(Page->Frame, MI_PROT_READONLY, MI_LEAF_USER));
    Vad->CommitCharge++;
    MI_ASSERT((ULONG64)Vad->CommitCharge == Bitmap->Pages.NodeCount);
    *Frame = Page->Frame;
    return STATUS_SUCCESS;
}

static PMI_VAD_NODE
MiCfgFirstPageAtOrAfter(PMI_CFG_BITMAP Bitmap, ULONG64 Vpn)
{
    PMI_VAD_NODE Node = Bitmap->Pages.Root, Next = NULL;
    while (Node)
    {
        if (Node->StartingVpn == Vpn)
            return Node;
        if (Node->StartingVpn > Vpn)
        {
            Next = Node;
            Node = Node->Left;
        }
        else
            Node = Node->Right;
    }
    return Next;
}

static VOID
MiCfgReleasePageLocked(PMI_ADDRESS_SPACE Space, PMI_CFG_BITMAP Bitmap, PMI_CFG_PAGE Page)
{
    ULONG64 Address = Page->Node.StartingVpn << PAGE_SHIFT;
    ULONG Table;
    PMI_PTE Slot = MiPtLookup(Space, Address, &Table);

    MI_ASSERT(Slot && MiArchPteIsValid(MiArchPteRead(Slot)) && MiArchPteFrame(MiArchPteRead(Slot)) == Page->Frame);
    MiPtWrite(Space, Address, Slot, Table, 0);
    MiTlbInvalidateSpace(Space, Address, 1);
    MiPfnShareDecrement(&Space->System->Pfn, Page->Frame, TRUE);
    MiPtPruneEmpty(Space, Address);
    MiVadRemove(&Bitmap->Pages, &Page->Node);
    --Bitmap->Vad.CommitCharge;
    MiReturnCommit(Space, 1);
    MI_FREE(Page);
}

NTSTATUS
MiCfgPrepareRangeLocked(PMI_ADDRESS_SPACE Space, ULONG64 Start, ULONG64 End)
{
    ULONG64 Offset;
    ULONG Frame;
    PMI_CFG_BITMAP Bitmap;
    PMI_CFG_PAGE Pending = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Space->CfgBitmap)
        return STATUS_SUCCESS;
    Bitmap = CONTAINING_RECORD(Space->CfgBitmap, MI_CFG_BITMAP, Vad);
    MI_ASSERT(Start < End && ((Start | End) & (PAGE_SIZE - 1)) == 0);
    for (Offset = (Start >> 6) & ~(ULONG64)(PAGE_SIZE - 1); Offset < (End >> 6); Offset += PAGE_SIZE)
    {
        ULONG64 Vpn = Bitmap->Vad.Node.StartingVpn + (Offset >> PAGE_SHIFT);
        PMI_CFG_PAGE Added;
        if (MiVadFind(&Bitmap->Pages, Vpn))
            continue;
        Status = MiCfgGetBitmapPageLocked(Space, Offset, TRUE, &Frame);
        if (!NT_SUCCESS(Status))
            break;
        Added = CONTAINING_RECORD(MiVadFind(&Bitmap->Pages, Vpn), MI_CFG_PAGE, Node);
        Added->PendingNext = Pending;
        Pending = Added;
    }
    while (Pending)
    {
        PMI_CFG_PAGE Next = Pending->PendingNext;
        if (NT_SUCCESS(Status))
            Pending->PendingNext = NULL;
        else
            MiCfgReleasePageLocked(Space, Bitmap, Pending);
        Pending = Next;
    }
    return Status;
}

VOID
MiCfgFillRangeLocked(PMI_ADDRESS_SPACE Space, ULONG64 Start, ULONG64 End, UCHAR Value)
{
    PMI_CFG_BITMAP Bitmap;
    PMI_VAD_NODE Node;
    ULONG64 First = Start >> 6, Limit = End >> 6;
    MI_ASSERT(((Start | End) & (PAGE_SIZE - 1)) == 0);
    if (!Space->CfgBitmap || Start >= End)
        return;
    Bitmap = CONTAINING_RECORD(Space->CfgBitmap, MI_CFG_BITMAP, Vad);
    Node = MiCfgFirstPageAtOrAfter(Bitmap, Bitmap->Vad.Node.StartingVpn + (First >> PAGE_SHIFT));
    while (Node)
    {
        ULONG64 Offset = (Node->StartingVpn - Bitmap->Vad.Node.StartingVpn) << PAGE_SHIFT;
        ULONG64 From = First > Offset ? First : Offset;
        ULONG64 To = Limit < Offset + PAGE_SIZE ? Limit : Offset + PAGE_SIZE;
        PUCHAR Mapping;
        if (Offset >= Limit)
            break;
        Mapping = MiPfnMapFrame(&Space->System->Pfn, CONTAINING_RECORD(Node, MI_CFG_PAGE, Node)->Frame);
        RtlFillMemory(Mapping + (From - Offset), (SIZE_T)(To - From), Value);
        MiPfnUnmapFrame(&Space->System->Pfn, Mapping);
        Node = MiVadNext(Node);
    }
}

NTSTATUS
MiCfgSetTargetLocked(PMI_ADDRESS_SPACE Space, ULONG64 Address, UCHAR Value)
{
    ULONG Frame, Shift = (ULONG)((Address >> 3) & 6);
    ULONG64 Offset = Address >> 6;
    NTSTATUS Status = MiCfgGetBitmapPageLocked(Space, Offset, Value != 0, &Frame);
    PUCHAR Mapping, Byte;

    MI_ASSERT(Value <= 3);
    if (!NT_SUCCESS(Status))
        return Status == STATUS_NOT_COMMITTED && !Value ? STATUS_SUCCESS : Status;
    Mapping = MiPfnMapFrame(&Space->System->Pfn, Frame);
    Byte = Mapping + (Offset & (PAGE_SIZE - 1));
    *Byte = (UCHAR)((*Byte & ~(3U << Shift)) | ((ULONG)Value << Shift));
    MiPfnUnmapFrame(&Space->System->Pfn, Mapping);
    return STATUS_SUCCESS;
}

ULONG64
MiCfgBitmapRegionEndLocked(PMI_ADDRESS_SPACE Space, ULONG64 Address, BOOLEAN Committed)
{
    PMI_VAD Vad = Space->CfgBitmap;
    PMI_CFG_BITMAP Bitmap = CONTAINING_RECORD(Vad, MI_CFG_BITMAP, Vad);
    ULONG64 Vpn = Address >> PAGE_SHIFT;
    PMI_VAD_NODE Node = MiCfgFirstPageAtOrAfter(Bitmap, Vpn);

    if (Committed)
    {
        MI_ASSERT(Node != NULL && Node->StartingVpn == Vpn);
        do
        {
            Vpn++;
            Node = MiVadNext(Node);
        } while (Node != NULL && Node->StartingVpn == Vpn);
        return Vpn << PAGE_SHIFT;
    }
    return (Node ? Node->StartingVpn : Vad->Node.EndingVpn + 1) << PAGE_SHIFT;
}

static ULONG
MiCfgReadImageTarget(PMI_SEGMENT Segment, ULONG Index, PUCHAR Flags)
{
    PUCHAR Entry = Segment->CfgEntries + (SIZE_T)Index * Segment->CfgEntrySize;
    ULONG Rva;
    RtlCopyMemory(&Rva, Entry, sizeof(Rva));
    *Flags = Segment->CfgEntrySize > sizeof(Rva) ? Entry[sizeof(Rva)] : 0;
    return Rva;
}

BOOLEAN
MiCfgIsSuppressedExport(PMI_VAD Vad, ULONG64 Address)
{
    PMI_SEGMENT Segment = Vad->Segment;
    ULONG64 Offset;
    ULONG Low = 0, High;
    if (Vad->Type != MiVadImage || !Segment->CfgInstrumented)
        return FALSE;
    Offset = Address - (Vad->Node.StartingVpn << PAGE_SHIFT) + (Vad->SegmentPageOffset << PAGE_SHIFT);
    High = Segment->CfgEntryCount;
    while (Low < High)
    {
        ULONG Mid = Low + (High - Low) / 2;
        UCHAR Flags;
        ULONG Rva = MiCfgReadImageTarget(Segment, Mid, &Flags);
        if (Rva == Offset)
            return (Flags & (MI_CFG_FID_SUPPRESSED | MI_CFG_EXPORT_SUPPRESSED)) == MI_CFG_EXPORT_SUPPRESSED;
        if (Rva < Offset)
            Low = Mid + 1;
        else
            High = Mid;
    }
    return FALSE;
}

NTSTATUS
MiCfgInitializeImageLocked(PMI_ADDRESS_SPACE Space, PMI_VAD Vad)
{
    PMI_SEGMENT Segment = Vad->Segment;
    ULONG64 Base = Vad->Node.StartingVpn << PAGE_SHIFT;
    ULONG64 End = ((Vad->Node.EndingVpn + 1) << PAGE_SHIFT) - (Vad->CfgScp ? PAGE_SIZE : 0);
    ULONG64 First = Vad->SegmentPageOffset << PAGE_SHIFT, Limit = First + (End - Base);
    ULONG Index;
    NTSTATUS Status;

    if (!Space->CfgBitmap)
        return STATUS_SUCCESS;
    MiCfgFillRangeLocked(Space, Base, (Vad->Node.EndingVpn + 1) << PAGE_SHIFT, 0);
    if (Vad->NoExecuteImage)
        return STATUS_SUCCESS;
    for (Index = 0; Index < Segment->LayoutCount; ++Index)
    {
        PMI_SEGMENT_LAYOUT Entry = &Segment->Layout[Index];
        ULONG64 From = Entry->FirstPage << PAGE_SHIFT;
        ULONG64 To = (Entry->FirstPage + Entry->PageCount) << PAGE_SHIFT;
        if (From < First)
            From = First;
        if (To > Limit)
            To = Limit;
        if (From >= To || !MI_PROT_IS_EXECUTE(Entry->Protection))
            continue;
        From = Base + (From - First);
        To = Base + (To - First);
        Status = MiCfgPrepareRangeLocked(Space, From, To);
        if (!NT_SUCCESS(Status))
            return Status;
        if (!Segment->CfgInstrumented)
            MiCfgFillRangeLocked(Space, From, To, 0xff);
    }
    for (Index = 0; Index < Segment->CfgEntryCount; ++Index)
    {
        UCHAR Flags, Value;
        ULONG Rva = MiCfgReadImageTarget(Segment, Index, &Flags);
        ULONG64 Address;
        if (Rva < First || Rva >= Limit || (Flags & MI_CFG_FID_SUPPRESSED))
            continue;
        Address = Base + (Rva - First);
        if (!MI_PROT_IS_EXECUTE(MiViewPteProtection(Space, Vad, Address, 0)))
            continue;
        Value = (Rva & 15) ? 3 : (Flags & MI_CFG_EXPORT_SUPPRESSED) ? 2 : 1;
        Status = MiCfgSetTargetLocked(Space, Address, Value);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}

VOID
MiCfgDeleteBitmapLocked(PMI_ADDRESS_SPACE Space)
{
    PMI_VAD Vad = Space->CfgBitmap;
    PMI_CFG_BITMAP Bitmap = CONTAINING_RECORD(Vad, MI_CFG_BITMAP, Vad);
    PMI_VAD_NODE Node;

    MI_ASSERT(Vad->Type == MiVadCfgBitmap && (ULONG64)Vad->CommitCharge == Bitmap->Pages.NodeCount);
    while ((Node = MiVadFirst(&Bitmap->Pages)) != NULL)
        MiCfgReleasePageLocked(Space, Bitmap, CONTAINING_RECORD(Node, MI_CFG_PAGE, Node));
    Space->CfgBitmap = NULL;
    MiVadRemove(&Space->VadRoot, &Vad->Node);
    MI_FREE(Bitmap);
}

NTSTATUS
MiCfgCloneBitmapLocked(PMI_ADDRESS_SPACE Source, PMI_ADDRESS_SPACE Target)
{
    PMI_VAD Vad = Source->CfgBitmap;
    PMI_CFG_BITMAP Bitmap = CONTAINING_RECORD(Vad, MI_CFG_BITMAP, Vad);
    ULONG64 Base = Vad->Node.StartingVpn << PAGE_SHIFT;
    ULONG64 Size = ((Vad->Node.EndingVpn + 1) << PAGE_SHIFT) - Base;
    PMI_PFN_DATABASE Db = &Source->System->Pfn;
    PMI_VAD_NODE Node;
    NTSTATUS Status;

    Status = MiCfgCreateBitmapLocked(Target, Base, Size);
    if (!NT_SUCCESS(Status))
        return Status;
    Target->CfgExportSuppression = Source->CfgExportSuppression;
    for (Node = MiVadFirst(&Bitmap->Pages); Node; Node = MiVadNext(Node))
    {
        ULONG Frame;
        PVOID From, To;

        Status = MiCfgGetBitmapPageLocked(Target, (Node->StartingVpn << PAGE_SHIFT) - Base, TRUE, &Frame);
        if (!NT_SUCCESS(Status))
            break;
        From = MiPfnMapFrame(Db, CONTAINING_RECORD(Node, MI_CFG_PAGE, Node)->Frame);
        To = MiPfnMapFrame(Db, Frame);
        RtlCopyMemory(To, From, PAGE_SIZE);
        MiPfnUnmapFrame(Db, To);
        MiPfnUnmapFrame(Db, From);
    }
    if (!NT_SUCCESS(Status))
        MiCfgDeleteBitmapLocked(Target);
    return Status;
}
