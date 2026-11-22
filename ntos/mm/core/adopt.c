/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/core/adopt.c
 * PURPOSE:     Adoption of boot-time memory mappings
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/include/mm.h>

static
NTSTATUS
MiSpaceAdoptCommon(
    _Inout_ PMI_SYSTEM System,
    _Out_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG RootFrame,
    _In_ BOOLEAN IsSystem)
{
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    PMI_PFN Root;

    if (!MiPfnTracked(&System->Pfn, RootFrame))
        return STATUS_INVALID_PARAMETER;

    Root = &System->Pfn.Pfn[RootFrame];
    RtlZeroMemory(Space, sizeof(*Space));
    Space->System = System;
    Space->IsSystem = IsSystem;
    Space->Granularity = MI_ALLOCATION_GRANULARITY;
    Space->RootFrame = RootFrame;
    Space->LowestVa = IsSystem ? Arch->SystemAddressStart : Arch->UserAddressStart;
    Space->HighestVa = IsSystem ? Arch->SystemAddressEnd : Arch->UserAddressEnd;
    MI_RW_INIT(&Space->Lock);
    MiVadRootInitialize(&Space->VadRoot, Space->LowestVa >> PAGE_SHIFT, Space->HighestVa >> PAGE_SHIFT);
    MiVadRootInitialize(&Space->AweRoot, 0, System->Pfn.FrameCount - 1);
    MiVadRootInitialize(&Space->CloneRoot, Space->LowestVa >> PAGE_SHIFT, Space->HighestVa >> PAGE_SHIFT);

    if (Root->State == MiPageUnusable)
        MiPfnMarkInUse(&System->Pfn, RootFrame, 1);

    MI_ATOMIC_OR8(&Root->Flags, MI_PFN_FLAG_PAGE_TABLE | MI_PFN_FLAG_PINNED);
    Root->PageTableOwner = Space;
    return STATUS_SUCCESS;
}

NTSTATUS
MiAddressSpaceAdopt(
    _Inout_ PMI_SYSTEM System,
    _Out_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG RootFrame,
    _In_ BOOLEAN IsSystem)
{
    return MiSpaceAdoptCommon(System, Space, RootFrame, IsSystem);
}

static
VOID
MiAdoptTable(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG Frame,
    _In_ ULONG ParentFrame,
    _In_ ULONG ParentIndex,
    _In_ LONG Level)
{
    PMI_SYSTEM System = Space->System;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    PMI_PTE Table;
    ULONG Entries;
    PMI_PFN Entry;
    ULONG Index;

    MI_ASSERT(Level >= 0 && (ULONG)Level + 1 < Arch->PagingLevels);
    MI_ASSERT(MiPfnTracked(&System->Pfn, Frame) && MiPfnTracked(&System->Pfn, ParentFrame));
    MI_ASSERT(ParentIndex < Arch->Level[Level + 1].EntryCount);
    MI_ASSERT(System->Pfn.Pfn[ParentFrame].PageTableOwner == Space);
    MI_ASSERT(MI_PFN_FLAGS(&System->Pfn.Pfn[ParentFrame]) & MI_PFN_FLAG_PAGE_TABLE);
    MI_ASSERT(!(MI_PFN_FLAGS(&System->Pfn.Pfn[Frame]) & MI_PFN_FLAG_PAGE_TABLE));
    MI_ASSERT(MiArchPteIsValid(MiArchPteRead((PMI_PTE)MiArchMapFrame(ParentFrame) + ParentIndex)));
    MI_ASSERT(!MiArchPteIsBlock(MiArchPteRead((PMI_PTE)MiArchMapFrame(ParentFrame) + ParentIndex),
                                (ULONG)Level + 1));
    MI_ASSERT(MiArchPteFrame(MiArchPteRead((PMI_PTE)MiArchMapFrame(ParentFrame) + ParentIndex)) == Frame);

    Table = MiArchMapFrame(Frame);
    Entries = (ULONG)Arch->Level[Level].EntryCount;
    Entry = &System->Pfn.Pfn[Frame];
    if (Entry->State == MiPageUnusable)
        MiPfnMarkInUse(&System->Pfn, Frame, 1);

    Entry->PteFrame = ParentFrame;
    Entry->PageTableOwner = Space;
    Entry->PteAddress = ((ULONG64)ParentFrame << PAGE_SHIFT) + (ULONG64)ParentIndex * sizeof(MI_PTE);
    MI_ATOMIC_OR8(&Entry->Flags, MI_PFN_FLAG_PAGE_TABLE | MI_PFN_FLAG_PINNED);
    MI_ATOMIC_ADD64(&Space->PageTablePages, 1);

    if (Level == 0)
        return;

    for (Index = 0; Index < Entries; Index++)
    {
        MI_PTE Pte = MiArchPteRead(&Table[Index]);
        ULONG64 Child = MiArchPteFrame(Pte);

        if (!MiArchPteIsValid(Pte) || MiArchPteIsBlock(Pte, (ULONG)Level) || !MiPfnTracked(&System->Pfn, Child))
            continue;

        if (MI_PFN_FLAGS(&System->Pfn.Pfn[Child]) & MI_PFN_FLAG_PAGE_TABLE)
            continue;

        MiAdoptTable(Space, (ULONG)Child, Frame, Index, Level - 1);
    }
}

typedef struct _MI_BOOT_RUN
{
    ULONG64 Start;
    ULONG64 End;
    BOOLEAN Open;
} MI_BOOT_RUN, *PMI_BOOT_RUN;

static
NTSTATUS
MiBootRunFlush(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_BOOT_RUN Run)
{
    PMI_VAD Vad;

    if (!Run->Open)
        return STATUS_SUCCESS;

    MI_ASSERT(Run->End >= Run->Start);
    Run->Open = FALSE;

    Vad = MI_ALLOCATE(sizeof(*Vad));
    if (Vad == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Vad, sizeof(*Vad));
    Vad->Type = MiVadSystem;
    Vad->Protection = MI_PROT_EXECUTE_READWRITE;
    Vad->Node.StartingVpn = Run->Start >> PAGE_SHIFT;
    Vad->Node.EndingVpn = Run->End >> PAGE_SHIFT;

    if (!MiVadInsert(&Space->VadRoot, &Vad->Node))
    {
        MI_FREE(Vad);
        return STATUS_CONFLICTING_ADDRESSES;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
MiBootRunAdd(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_BOOT_RUN Run,
    _In_ ULONG64 Base,
    _In_ ULONG64 Span)
{
    NTSTATUS Status;

    MI_ASSERT(Span != 0 && Base + (Span - 1) >= Base);
    MI_ASSERT(!Run->Open || Base > Run->End);

    if (Run->Open && Run->End + 1 == Base)
    {
        Run->End = Base + (Span - 1);
        return STATUS_SUCCESS;
    }

    Status = MiBootRunFlush(Space, Run);
    if (!NT_SUCCESS(Status))
        return Status;

    Run->Start = Base;
    Run->End = Base + (Span - 1);
    Run->Open = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
MiReserveBootTable(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_BOOT_RUN Run,
    _In_ ULONG64 Frame,
    _In_ LONG Level,
    _In_ ULONG64 Base)
{
    PMI_SYSTEM System = Space->System;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    ULONG64 Span = 1ULL << Arch->Level[Level].Shift;
    NTSTATUS Status = STATUS_SUCCESS;
    PMI_PTE Table;
    ULONG Index;

    MI_ASSERT(Level >= 1 && (ULONG)Level + 1 < Arch->PagingLevels);
    MI_ASSERT(MiPfnTracked(&System->Pfn, Frame));

    Table = MiArchMapFrame(Frame);
    for (Index = 0; Index < Arch->Level[Level].EntryCount && NT_SUCCESS(Status); Index++)
    {
        MI_PTE Pte = MiArchPteRead(&Table[Index]);
        ULONG64 Va = Base + (ULONG64)Index * Span;

        if (!MiArchPteIsValid(Pte))
            Status = MiBootRunFlush(Space, Run);
        else if (Level == 1 || MiArchPteIsBlock(Pte, (ULONG)Level) || !MiPfnTracked(&System->Pfn, MiArchPteFrame(Pte)))
            Status = MiBootRunAdd(Space, Run, Va, Span);
        else
            Status = MiReserveBootTable(Space, Run, MiArchPteFrame(Pte), Level - 1, Va);
    }
    MiArchUnmapFrame(Table);

    return Status;
}

NTSTATUS
MiSystemAdoptBootMappings(
    _Inout_ PMI_SYSTEM System)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    LONG Top = Arch->PagingLevels - 1;
    ULONG64 Span = 1ULL << Arch->Level[Top].Shift;
    PMI_PTE Root = MiArchMapFrame(Space->RootFrame);
    ULONG First = (ULONG)((Arch->SystemAddressStart >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
    ULONG Last = (ULONG)((Arch->SystemAddressEnd >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
    BOOLEAN Descend = (MiSessionLevel(Arch) != Top) && (Top > 1);
    MI_BOOT_RUN Run = { 0 };
    NTSTATUS Status;
    ULONG Index;

    MI_ASSERT((Arch->SystemAddressStart & (Span - 1)) == 0);

    /* Where system space is carved below the top level, a top-level entry is too coarse to reserve whole. */
    for (Index = First; Index <= Last; Index++)
    {
        MI_PTE Pte = MiArchPteRead(&Root[Index]);
        ULONG64 Base = Arch->SystemAddressStart + (ULONG64)(Index - First) * Span;
        BOOLEAN Table;

        if (!MiArchPteIsValid(Pte))
        {
            Status = MiBootRunFlush(Space, &Run);
            if (!NT_SUCCESS(Status))
                return Status;
            continue;
        }

        Table = !MiArchPteIsBlock(Pte, (ULONG)Top) && MiPfnTracked(&System->Pfn, MiArchPteFrame(Pte));
        if (Table && Descend)
        {
            Status = MiReserveBootTable(Space, &Run, MiArchPteFrame(Pte), Top - 1, Base);
        }
        else
        {
            Status = MiBootRunAdd(Space, &Run, Base, Span);
            if (NT_SUCCESS(Status) && !Descend)
                Status = MiBootRunFlush(Space, &Run);
        }
        if (!NT_SUCCESS(Status))
            return Status;

        if (Table && !(MI_PFN_FLAGS(&System->Pfn.Pfn[MiArchPteFrame(Pte)]) & MI_PFN_FLAG_PAGE_TABLE))
            MiAdoptTable(Space, (ULONG)MiArchPteFrame(Pte), Space->RootFrame, Index, Top - 1);
    }

    return MiBootRunFlush(Space, &Run);
}

NTSTATUS
MiSystemReserveTopLevelHole(
    _Inout_ PMI_SYSTEM System)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    PMI_VAD_NODE Node;
    ULONG64 Cursor, End;

    if (Arch->SystemReservedEnd <= Arch->SystemAddressStart)
        return STATUS_SUCCESS;

    Cursor = Arch->SystemAddressStart >> PAGE_SHIFT;
    End = (Arch->SystemReservedEnd >> PAGE_SHIFT) - 1;
    Node = MiVadFirst(&Space->VadRoot);
    while (Cursor <= End)
    {
        PMI_VAD Vad;
        ULONG64 GapEnd;

        while (Node != NULL && Node->EndingVpn < Cursor)
            Node = MiVadNext(Node);
        if (Node != NULL && Node->StartingVpn <= Cursor)
        {
            if (Node->EndingVpn >= End)
                break;
            Cursor = Node->EndingVpn + 1;
            Node = MiVadNext(Node);
            continue;
        }

        GapEnd = (Node != NULL && Node->StartingVpn <= End) ? Node->StartingVpn - 1 : End;
        Vad = MI_ALLOCATE(sizeof(*Vad));
        if (Vad == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(Vad, sizeof(*Vad));
        Vad->Type = MiVadSystem;
        Vad->Protection = MI_PROT_NOACCESS;
        Vad->Node.StartingVpn = Cursor;
        Vad->Node.EndingVpn = GapEnd;
        if (!MiVadInsert(&Space->VadRoot, &Vad->Node))
        {
            MI_FREE(Vad);
            return STATUS_CONFLICTING_ADDRESSES;
        }
        if (GapEnd == End)
            break;
        Cursor = GapEnd + 1;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
MiSystemReserveSessionRange(
    _Inout_ PMI_SYSTEM System)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    LONG Top = Arch->PagingLevels - 1;
    LONG Level;
    ULONG64 SpanPages;
    ULONG64 RangePages;
    ULONG64 AlignPages;
    ULONG64 Vpn;
    PMI_VAD Vad;

    System->SessionStart = 0;
    System->SessionEnd = 0;

    if (Arch->SessionTopLevelEntries == 0)
        return STATUS_SUCCESS;

    Level = MiSessionLevel(Arch);
    SpanPages = MiSessionSpan(Arch) >> PAGE_SHIFT;
    RangePages = SpanPages * Arch->SessionTopLevelEntries;

    /* A range below the top level must sit in one top-level entry, whose table each session copies. */
    AlignPages = (Level == Top) ? SpanPages : RangePages;
    MI_ASSERT(Level == Top || (((RangePages & (RangePages - 1)) == 0) &&
                               ((RangePages << PAGE_SHIFT) <= (1ULL << Arch->Level[Top].Shift))));

    Vad = MI_ALLOCATE(sizeof(*Vad));
    if (Vad == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Vad, sizeof(*Vad));
    Vad->Type = MiVadSystem;
    Vad->Protection = MI_PROT_NOACCESS;

    if (!MiVadFindEmptyRange(&Space->VadRoot, RangePages, AlignPages, &Vpn))
    {
        MI_FREE(Vad);
        return STATUS_NO_MEMORY;
    }

    Vad->Node.StartingVpn = Vpn;
    Vad->Node.EndingVpn = Vpn + RangePages - 1;
    if (!MiVadInsert(&Space->VadRoot, &Vad->Node))
    {
        MI_FREE(Vad);
        return STATUS_CONFLICTING_ADDRESSES;
    }

    System->SessionStart = Vad->Node.StartingVpn << PAGE_SHIFT;
    System->SessionEnd = (Vad->Node.EndingVpn << PAGE_SHIFT) + PAGE_SIZE - 1;

    MI_ASSERT((Vad->Node.StartingVpn & (AlignPages - 1)) == 0);
    MI_ASSERT(System->SessionStart >= Arch->SystemAddressStart && System->SessionEnd <= Arch->SystemAddressEnd);
    MI_ASSERT(System->SessionEnd - System->SessionStart + 1 == RangePages << PAGE_SHIFT);
    MI_ASSERT(MiVadFind(&Space->VadRoot, Vad->Node.StartingVpn) == &Vad->Node);
    {
        PMI_PTE Root = MiArchMapFrame(Space->RootFrame);
        ULONG FirstTop = (ULONG)((System->SessionStart >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
        ULONG LastTop = (ULONG)((System->SessionEnd >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
        ULONG Index;

        if (Level == Top)
        {
            for (Index = FirstTop; Index <= LastTop; Index++)
                MI_ASSERT(!MiArchPteIsValid(MiArchPteRead(&Root[Index])));
        }
        else
        {
            MI_PTE TopEntry = MiArchPteRead(&Root[FirstTop]);

            MI_ASSERT(FirstTop == LastTop);
            if (MiArchPteIsValid(TopEntry) && !MiArchPteIsBlock(TopEntry, (ULONG)Top))
            {
                PMI_PTE Table = MiArchMapFrame(MiArchPteFrame(TopEntry));

                for (Index = (ULONG)((System->SessionStart >> Arch->Level[Level].Shift) & Arch->Level[Level].IndexMask);
                     Index <= (ULONG)((System->SessionEnd >> Arch->Level[Level].Shift) & Arch->Level[Level].IndexMask);
                     Index++)
                {
                    MI_ASSERT(!MiArchPteIsValid(MiArchPteRead(&Table[Index])));
                }
                MiArchUnmapFrame(Table);
            }
        }
        MiArchUnmapFrame(Root);
    }
    return STATUS_SUCCESS;
}

NTSTATUS
MiSystemPopulateTopLevel(
    _Inout_ PMI_SYSTEM System)
{
    PMI_ADDRESS_SPACE Space = &System->SystemSpace;
    const MI_ARCH_DESCRIPTOR *Arch = System->Arch;
    LONG Top = Arch->PagingLevels - 1;
    PMI_PTE Root = MiArchMapFrame(Space->RootFrame);
    ULONG64 Start = (Arch->SystemReservedEnd > Arch->SystemAddressStart) ? Arch->SystemReservedEnd
                                                                         : Arch->SystemAddressStart;
    ULONG First = (ULONG)((Start >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
    ULONG Last = (ULONG)((Arch->SystemAddressEnd >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask);
    ULONG Index;

    for (Index = First; Index <= Last; Index++)
    {
        ULONG Table;

        if (MiArchPteRead(&Root[Index]) != 0)
            continue;

        if (System->SessionEnd > System->SessionStart && MiSessionLevel(Arch) == Top &&
            Index >= ((System->SessionStart >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask) &&
            Index <= ((System->SessionEnd >> Arch->Level[Top].Shift) & Arch->Level[Top].IndexMask))
        {
            continue;
        }

        Table = MiPfnAllocatePage(&System->Pfn, MI_ALLOCATE_ZEROED);
        if (Table == MI_FRAME_INVALID)
            return STATUS_NO_MEMORY;

        MiPfnInitializePage(&System->Pfn, Table, ((ULONG64)Space->RootFrame << PAGE_SHIFT) +
                                                     (ULONG64)Index * sizeof(MI_PTE),
                            Space->RootFrame, 0, MI_PFN_FLAG_PAGE_TABLE | MI_PFN_FLAG_PINNED);
        System->Pfn.Pfn[Table].PageTableOwner = Space;
        MiArchPteWrite(&Root[Index], MiArchPteMakeTable(Table, 0));
        MI_ATOMIC_ADD64(&Space->PageTablePages, 1);
    }

    if (System->SessionEnd > System->SessionStart && MiSessionLevel(Arch) != Top)
    {
        ULONG64 TopSpan = 1ULL << Arch->Level[Top].Shift;
        ULONG64 Base = System->SessionStart & ~(TopSpan - 1);
        ULONG64 Span = MiSessionSpan(Arch);
        ULONG64 Va;
        NTSTATUS Status;

        /* Sessions copy this top-level entry's table: its other entries must never change after this. */
        for (Va = Base; Va < Base + TopSpan; Va += Span)
        {
            if (Va >= System->SessionStart && Va <= System->SessionEnd)
                continue;

            Status = MiPtPinRange(Space, Va, PAGE_SIZE);
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }

    return STATUS_SUCCESS;
}
