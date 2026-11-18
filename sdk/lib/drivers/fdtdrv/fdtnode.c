/*
 * PROJECT:     LiberNT Device Tree Driver Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device-tree lookups and register mapping through the I/O of the tree's owner
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/fdtdrv.h>

ULONG
NTAPI
FdtDriverFindPhandle(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Phandle)
{
    ULONG Offset = FdtRootNode(&Tree->Fdt), Value;

    if (!Phandle || Phandle == MAXULONG)
        return FDT_NO_NODE;
    while (Offset != FDT_NO_NODE)
    {
        if (FdtReadToken(&Tree->Fdt, Offset) == FDT_BEGIN_NODE &&
            FdtReadU32(&Tree->Fdt, Offset, "phandle", &Value) && Value == Phandle)
        {
            return Offset;
        }
        Offset = FdtSkipToken(&Tree->Fdt, Offset);
    }
    return FDT_NO_NODE;
}

ULONG
NTAPI
FdtDriverFindChild(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Compatible)
{
    ULONG Child, Length;

    for (Child = FdtFirstChild(&Tree->Fdt, Node); Child != FDT_NO_NODE;
         Child = FdtNextSibling(&Tree->Fdt, Child))
    {
        const VOID *List = FdtGetProperty(&Tree->Fdt, Child, "compatible", &Length);

        if (List && FdtStringListContains(List, Length, Compatible))
            return Child;
    }
    return FDT_NO_NODE;
}

static
BOOLEAN
FdtDriverFindParent(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Bus,
    _In_ ULONG Node,
    _In_ ULONG Depth,
    _Out_ PULONG Parent)
{
    ULONG Child, Next;

    for (Child = FdtFirstChild(&Tree->Fdt, Bus); Child != FDT_NO_NODE; Child = Next)
    {
        Next = FdtNextSibling(&Tree->Fdt, Child);
        if (Child == Node)
        {
            *Parent = Bus;
            return TRUE;
        }
        if (Child < Node && (Next == FDT_NO_NODE || Node < Next))
            return Depth < 8 && FdtDriverFindParent(Tree, Child, Node, Depth + 1, Parent);
    }
    return FALSE;
}

static
BOOLEAN
FdtDriverTranslate(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Bus,
    _Inout_ PULONG64 Address)
{
    ULONG Root = FdtRootNode(&Tree->Fdt), Length, ChildCells, ParentCells, SizeCells, Unused, Stride, Entry;
    ULONG GrandParent, Depth;
    ULONG64 ChildBase, ParentBase, Size;
    const VOID *Ranges;

    for (Depth = 0; Bus != Root && Depth < 8; ++Depth)
    {
        Ranges = FdtGetProperty(&Tree->Fdt, Bus, "ranges", &Length);
        if (!Ranges || !FdtDriverFindParent(Tree, Root, Bus, 0, &GrandParent))
            return FALSE;
        if (Length)
        {
            BOOLEAN Found = FALSE;

            FdtGetCellCounts(&Tree->Fdt, Bus, &ChildCells, &SizeCells);
            FdtGetCellCounts(&Tree->Fdt, GrandParent, &ParentCells, &Unused);
            Stride = ChildCells + ParentCells + SizeCells;
            for (Entry = 0; Stride && (Entry + 1) * Stride * sizeof(ULONG) <= Length; ++Entry)
            {
                if (!FdtReadCells(Ranges, Length, Entry * Stride, ChildCells, &ChildBase) ||
                    !FdtReadCells(Ranges, Length, Entry * Stride + ChildCells, ParentCells, &ParentBase) ||
                    !FdtReadCells(Ranges, Length, Entry * Stride + ChildCells + ParentCells, SizeCells, &Size))
                    return FALSE;
                if (*Address >= ChildBase && *Address - ChildBase < Size)
                {
                    *Address = *Address - ChildBase + ParentBase;
                    Found = TRUE;
                    break;
                }
            }
            if (!Found)
                return FALSE;
        }
        Bus = GrandParent;
    }
    return Bus == Root;
}

BOOLEAN
NTAPI
FdtDriverReadReg(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _Out_ PULONG64 Address,
    _Out_ PULONG64 Size)
{
    ULONG Parent;

    if (!FdtDriverFindParent(Tree, FdtRootNode(&Tree->Fdt), Node, 0, &Parent) ||
        !FdtReadReg(&Tree->Fdt, Node, Parent, Index, Address, Size))
        return FALSE;
    return FdtDriverTranslate(Tree, Parent, Address);
}

ULONG
NTAPI
FdtDriverFindRegIndex(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Name)
{
    const VOID *Names;
    const CHAR *Entry;
    ULONG Length, Cursor = 0, EntryLength, Index = 0;
    SIZE_T NameLength = strlen(Name);

    Names = FdtGetProperty(&Tree->Fdt, Node, "reg-names", &Length);
    while ((Entry = FdtNextString(Names, Length, &Cursor, &EntryLength)) != NULL)
    {
        if (EntryLength == NameLength && !memcmp(Entry, Name, EntryLength))
            return Index;
        ++Index;
    }
    return MAXULONG;
}

PUCHAR
NTAPI
FdtDriverMapReg(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG Index,
    _Out_opt_ PULONG64 AddressOut,
    _Out_ PULONG SizeOut)
{
    ULONG64 Address, Size;
    PUCHAR Va;

    *SizeOut = 0;
    if (!Tree->Io || !FdtDriverReadReg(Tree, Node, Index, &Address, &Size) || !Size || Size > 0x1000000)
        return NULL;
    Va = Tree->Io->Map(Address, (ULONG)Size);
    if (Va)
        *SizeOut = (ULONG)Size;
    if (AddressOut)
        *AddressOut = Address;
    return Va;
}

ULONG
NTAPI
FdtDriverFindParentNode(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node)
{
    ULONG Parent;

    return FdtDriverFindParent(Tree, FdtRootNode(&Tree->Fdt), Node, 0, &Parent) ? Parent : FDT_NO_NODE;
}
