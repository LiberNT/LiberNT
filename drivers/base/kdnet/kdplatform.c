/*
 * PROJECT:     LiberNT Network Kernel Debugger Transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Debug network devices described by the firmware: ACPI DBG2 and the device tree
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "kdnet.h"
#include <reactos/riscv64/fdtlib.h>
#include <kdfdt.h>

#define KDNET_DBG2_SIGNATURE        0x32474244
#define KDNET_DBG2_PORT_NET         0x8003
#define KDNET_ACPI_HEADER_SIZE      36
#define KDNET_DBG2_TABLE_SIZE       (KDNET_ACPI_HEADER_SIZE + 8)
#define KDNET_DBG2_DEVICE_SIZE      22
#define KDNET_GAS_SIZE              12
#define KDNET_GAS_MEMORY            0
#define KDNET_TREE_DEPTH            16

static KD_FDT_DEVICE KdpTreeDevice;

static
BOOLEAN
KdpEnumerateDebugPortTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKDNET_PLATFORM_CALLBACK Callback)
{
    KDNET_PLATFORM_DEVICE Device;
    const UCHAR *Table, *Entry, *Address;
    ULONG TableLength, Offset, Count, Length, PathLength, PathOffset, AddressOffset, SizeOffset, i;

    if (!HalGetCachedAcpiTable)
        return FALSE;

    Table = HalGetCachedAcpiTable(KDNET_DBG2_SIGNATURE, NULL, NULL);
    if (!Table)
        return FALSE;

    TableLength = KdNetGetLe32(Table + 4);
    if (TableLength < KDNET_DBG2_TABLE_SIZE)
        return FALSE;

    Offset = KdNetGetLe32(Table + KDNET_ACPI_HEADER_SIZE);
    for (Count = KdNetGetLe32(Table + KDNET_ACPI_HEADER_SIZE + 4); Count; Count--)
    {
        if (Offset > TableLength || TableLength - Offset < KDNET_DBG2_DEVICE_SIZE)
            break;

        Entry = Table + Offset;
        Length = KdNetGetLe16(Entry + 1);
        if (Length < KDNET_DBG2_DEVICE_SIZE || Length > TableLength - Offset)
            break;

        Offset += Length;
        PathLength = KdNetGetLe16(Entry + 4);
        PathOffset = KdNetGetLe16(Entry + 6);
        AddressOffset = KdNetGetLe16(Entry + 18);
        SizeOffset = KdNetGetLe16(Entry + 20);
        if (KdNetGetLe16(Entry + 12) != KDNET_DBG2_PORT_NET || !Entry[3] ||
            AddressOffset > Length || Length - AddressOffset < KDNET_GAS_SIZE ||
            SizeOffset > Length || Length - SizeOffset < sizeof(ULONG))
        {
            continue;
        }

        Address = Entry + AddressOffset;
        if (Address[0] != KDNET_GAS_MEMORY)
            continue;

        RtlZeroMemory(&Device, sizeof(Device));
        Device.NameSpace = KdNameSpaceACPI;
        Device.PortType = KDNET_DBG2_PORT_NET;
        Device.PortSubtype = KdNetGetLe16(Entry + 14);
        Device.Base.QuadPart = KdNetGetLe64(Address + 4);
        Device.Length = KdNetGetLe32(Entry + SizeOffset);
        _snwprintf(Device.ModuleName, RTL_NUMBER_OF(Device.ModuleName), L"kd_%04x_%04x.dll",
                   Device.PortType, Device.PortSubtype);
        Device.ModuleName[RTL_NUMBER_OF(Device.ModuleName) - 1] = UNICODE_NULL;
        if (PathOffset <= Length && PathLength <= Length - PathOffset)
        {
            for (i = 0; i < PathLength && i < sizeof(Device.Path) - 1 && Entry[PathOffset + i]; i++)
                Device.Path[i] = Entry[PathOffset + i];
        }

        if (Device.Base.QuadPart && Device.Length && Callback(LoaderBlock, &Device))
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
KdpTreeNodeEnabled(
    _In_ const RISCV_FDT *Tree,
    _In_ ULONG Node)
{
    const CHAR *Status;
    ULONG Length;

    Status = RiscvFdtGetProperty(Tree, Node, "status", &Length);
    if (!Status)
        return TRUE;

    return (Length == sizeof("okay") && RtlCompareMemory(Status, "okay", Length) == Length) ||
           (Length == sizeof("ok") && RtlCompareMemory(Status, "ok", Length) == Length);
}

static
BOOLEAN
KdpTreeTranslate(
    _In_ const RISCV_FDT *Tree,
    _In_reads_(Depth) const ULONG *Ancestors,
    _In_ ULONG Depth,
    _Inout_ PULONGLONG Address)
{
    const VOID *Ranges;
    ULONG Length, ChildCells, SizeCells, ParentCells, Unused, EntryCells, Index;
    ULONGLONG Child, Parent, Size;

    while (Depth > 1)
    {
        Ranges = RiscvFdtGetProperty(Tree, Ancestors[Depth - 1], "ranges", &Length);
        if (!Ranges)
            return FALSE;

        RiscvFdtGetCellCounts(Tree, Ancestors[Depth - 1], &ChildCells, &SizeCells);
        RiscvFdtGetCellCounts(Tree, Ancestors[Depth - 2], &ParentCells, &Unused);
        EntryCells = ChildCells + ParentCells + SizeCells;
        if (Length)
        {
            if (!EntryCells || ChildCells > 2 || ParentCells > 2 || SizeCells > 2)
                return FALSE;

            for (Index = 0; (Index + EntryCells) * sizeof(ULONG) <= Length; Index += EntryCells)
            {
                if (!RiscvFdtReadCells(Ranges, Length, Index, ChildCells, &Child) ||
                    !RiscvFdtReadCells(Ranges, Length, Index + ChildCells, ParentCells, &Parent) ||
                    !RiscvFdtReadCells(Ranges, Length, Index + ChildCells + ParentCells, SizeCells, &Size))
                {
                    return FALSE;
                }

                if (*Address >= Child && *Address - Child < Size)
                {
                    *Address = *Address - Child + Parent;
                    break;
                }
            }

            if ((Index + EntryCells) * sizeof(ULONG) > Length)
                return FALSE;
        }

        Depth--;
    }

    return TRUE;
}

static
BOOLEAN
KdpTreeNode(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKDNET_PLATFORM_CALLBACK Callback,
    _In_ const RISCV_FDT *Tree,
    _In_ ULONG Node,
    _In_reads_(Depth) const ULONG *Ancestors,
    _In_ ULONG Depth)
{
    KDNET_PLATFORM_DEVICE Device;
    const VOID *Compatible;
    const CHAR *Name;
    ULONG Length, Cursor = 0, NameLength, i;
    ULONGLONG Address, Size;
    CHAR Character;

    Compatible = RiscvFdtGetProperty(Tree, Node, "compatible", &Length);
    if (!Compatible || !KdpTreeNodeEnabled(Tree, Node) ||
        !RiscvFdtReadReg(Tree, Node, Ancestors[Depth - 1], 0, &Address, &Size) ||
        !Size || Size > MAXULONG || !KdpTreeTranslate(Tree, Ancestors, Depth, &Address))
    {
        return FALSE;
    }

    while ((Name = RiscvFdtNextString(Compatible, Length, &Cursor, &NameLength)) != NULL)
    {
        if (!NameLength || NameLength + sizeof("kd_fdt_.dll") > RTL_NUMBER_OF(Device.ModuleName))
            continue;

        RtlZeroMemory(&Device, sizeof(Device));
        _snwprintf(Device.ModuleName, RTL_NUMBER_OF(Device.ModuleName), L"kd_fdt_");
        for (i = 0; i < NameLength; i++)
        {
            Character = Name[i];
            if (!((Character >= 'a' && Character <= 'z') || (Character >= '0' && Character <= '9') ||
                  Character == '-'))
            {
                Character = '_';
            }

            Device.ModuleName[7 + i] = Character;
        }

        RtlCopyMemory(&Device.ModuleName[7 + NameLength], L".dll", sizeof(L".dll"));
        Device.NameSpace = KdNameSpaceNone;
        Device.Base.QuadPart = Address;
        Device.Length = (ULONG)Size;
        KdpTreeDevice.Blob = Tree->Blob;
        KdpTreeDevice.Size = Tree->TotalSize;
        KdpTreeDevice.Node = Node;
        KdpTreeDevice.Parent = Ancestors[Depth - 1];
        Device.OemData = &KdpTreeDevice;
        Device.OemDataLength = sizeof(KdpTreeDevice);
        if (Callback(LoaderBlock, &Device))
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
KdpEnumerateDeviceTree(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKDNET_PLATFORM_CALLBACK Callback)
{
    ULONG Path[KDNET_TREE_DEPTH], Depth = 0, Node, Next, Size;
    const VOID *Blob;
    RISCV_FDT Tree;

    if (!KdNetArchGetDeviceTree(LoaderBlock, &Blob, &Size) || !RiscvFdtOpen(Blob, Size, &Tree))
        return FALSE;

    Node = RiscvFdtRootNode(&Tree);
    if (Node == RISCV_FDT_NO_NODE)
        return FALSE;

    for (;;)
    {
        if (Depth && KdpTreeNode(LoaderBlock, Callback, &Tree, Node, Path, Depth))
            return TRUE;

        Next = Depth + 1 < KDNET_TREE_DEPTH ? RiscvFdtFirstChild(&Tree, Node) : RISCV_FDT_NO_NODE;
        if (Next != RISCV_FDT_NO_NODE)
        {
            Path[Depth++] = Node;
            Node = Next;
            continue;
        }

        for (;;)
        {
            if (!Depth)
                return FALSE;

            Next = RiscvFdtNextSibling(&Tree, Node);
            if (Next != RISCV_FDT_NO_NODE)
            {
                Node = Next;
                break;
            }

            Node = Path[--Depth];
        }
    }
}

BOOLEAN
KdPlatformEnumerate(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKDNET_PLATFORM_CALLBACK Callback)
{
    return KdpEnumerateDebugPortTable(LoaderBlock, Callback) ||
           KdpEnumerateDeviceTree(LoaderBlock, Callback);
}
