/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device tree lookups for platforms booted without ACPI
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "devtree.h"

#define HALP_FDT_MAXIMUM_DEPTH 8

BOOLEAN
HalpArm64OpenDeviceTree(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_ PFDT Fdt)
{
    if (!LoaderBlock || !LoaderBlock->u.Arm64.DeviceTree)
        return FALSE;

    return FdtOpen((const VOID *)(ULONG_PTR)LoaderBlock->u.Arm64.DeviceTree,
                        (SIZE_T)LoaderBlock->u.Arm64.DeviceTreeSize,
                        Fdt);
}

static
BOOLEAN
HalpArm64FindDeviceTreeNodeBelow(
    _In_ PFDT Fdt,
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _In_ ULONG Depth,
    _In_reads_(Count) const PCSTR *Compatibles,
    _In_ ULONG Count,
    _Out_ PULONG Found,
    _Out_ PULONG FoundParent,
    _Out_ PULONG Match)
{
    const VOID *Compatible;
    ULONG Length;
    ULONG Index;
    ULONG Child;

    Compatible = FdtGetProperty(Fdt, Node, "compatible", &Length);
    if (Compatible && Parent != FDT_NO_NODE)
    {
        for (Index = 0; Index < Count; Index++)
        {
            if (FdtStringListContains(Compatible, Length, Compatibles[Index]))
            {
                *Found = Node;
                *FoundParent = Parent;
                *Match = Index;
                return TRUE;
            }
        }
    }

    if (Depth >= HALP_FDT_MAXIMUM_DEPTH)
        return FALSE;

    for (Child = FdtFirstChild(Fdt, Node); Child != FDT_NO_NODE; Child = FdtNextSibling(Fdt, Child))
    {
        if (HalpArm64FindDeviceTreeNodeBelow(Fdt, Child, Node, Depth + 1, Compatibles, Count, Found, FoundParent, Match))
            return TRUE;
    }

    return FALSE;
}

BOOLEAN
HalpArm64FindDeviceTreeNode(
    _In_ PFDT Fdt,
    _In_reads_(Count) const PCSTR *Compatibles,
    _In_ ULONG Count,
    _Out_ PULONG Node,
    _Out_ PULONG Parent,
    _Out_ PULONG Match)
{
    ULONG Root = FdtRootNode(Fdt);

    *Node = FDT_NO_NODE;
    *Parent = FDT_NO_NODE;
    *Match = 0;
    if (Root == FDT_NO_NODE)
        return FALSE;

    return HalpArm64FindDeviceTreeNodeBelow(Fdt, Root, FDT_NO_NODE, 0, Compatibles, Count, Node, Parent, Match);
}

static PVOID HalpArm64DeviceTreeCopy;
static ULONG HalpArm64DeviceTreeCopySize;

VOID
HalpArm64CaptureDeviceTree(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    FDT Fdt;
    PVOID Copy;

    if (HalpArm64DeviceTreeCopy || !HalpArm64OpenDeviceTree(LoaderBlock, &Fdt))
        return;
    Copy = ExAllocatePoolWithTag(NonPagedPool, Fdt.TotalSize, 'tdHA');
    if (!Copy)
        return;
    RtlCopyMemory(Copy, Fdt.Blob, Fdt.TotalSize);
    HalpArm64DeviceTreeCopySize = Fdt.TotalSize;
    HalpArm64DeviceTreeCopy = Copy;
}

BOOLEAN
HalpArm64GetDeviceTree(
    _Outptr_result_maybenull_ const VOID **Blob,
    _Out_ PULONG Size)
{
    *Blob = HalpArm64DeviceTreeCopy;
    *Size = HalpArm64DeviceTreeCopySize;
    return HalpArm64DeviceTreeCopy != NULL;
}

ULONG
HalpArm64FindDeviceTreePhandle(
    _In_ PFDT Fdt,
    _In_ ULONG Phandle)
{
    ULONG Node, Value;

    if (!Phandle || Phandle == MAXULONG)
        return FDT_NO_NODE;
    for (Node = FdtRootNode(Fdt); Node != FDT_NO_NODE; Node = FdtSkipToken(Fdt, Node))
    {
        if (FdtReadToken(Fdt, Node) == FDT_BEGIN_NODE &&
            (FdtReadU32(Fdt, Node, "phandle", &Value) || FdtReadU32(Fdt, Node, "linux,phandle", &Value)) &&
            Value == Phandle)
            return Node;
    }
    return FDT_NO_NODE;
}
