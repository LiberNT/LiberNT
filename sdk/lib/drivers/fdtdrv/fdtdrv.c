/*
 * PROJECT:     LiberNT Device Tree Driver Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device-tree access for drivers of devices the HAL enumerates from the firmware tree
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/fdtdrv.h>

#define FDT_DRIVER_TAG 'vDdF'

static
NTSTATUS
FdtDriverLoadBlob(
    _Out_ PFDT_DRIVER_TREE Tree)
{
    static UNICODE_STRING SystemKey = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System");
    static UNICODE_STRING BlobName = RTL_CONSTANT_STRING(L"DeviceTree");
    PKEY_VALUE_PARTIAL_INFORMATION Info = NULL;
    OBJECT_ATTRIBUTES Attributes;
    ULONG Length;
    HANDLE Key;
    NTSTATUS Status;

    RtlZeroMemory(Tree, sizeof(*Tree));
    Tree->Node = FDT_NO_NODE;
    InitializeObjectAttributes(&Attributes, &SystemKey, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryValueKey(Key, &BlobName, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_BUFFER_OVERFLOW)
    {
        Info = ExAllocatePoolWithTag(PagedPool, Length, FDT_DRIVER_TAG);
        Status = Info ? ZwQueryValueKey(Key, &BlobName, KeyValuePartialInformation, Info, Length, &Length) :
                        STATUS_INSUFFICIENT_RESOURCES;
    }
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        goto Done;
    Tree->Blob = ExAllocatePoolWithTag(NonPagedPool, Info->DataLength, FDT_DRIVER_TAG);
    if (!Tree->Blob)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(Tree->Blob, Info->Data, Info->DataLength);
    if (!FdtOpen(Tree->Blob, Info->DataLength, &Tree->Fdt))
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;

Done:
    if (Info)
        ExFreePoolWithTag(Info, FDT_DRIVER_TAG);
    return Status;
}

NTSTATUS
NTAPI
FdtDriverOpen(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PFDT_DRIVER_TREE Tree)
{
    static UNICODE_STRING PathName = RTL_CONSTANT_STRING(L"FdtPath");
    UCHAR PathBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 256 * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION PathInfo = (PKEY_VALUE_PARTIAL_INFORMATION)PathBuffer;
    CHAR Path[256];
    ULONG Length, Index;
    PWCHAR Wide;
    HANDLE Key;
    NTSTATUS Status;

    Status = FdtDriverLoadBlob(Tree);
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = IoOpenDeviceRegistryKey(PhysicalDeviceObject, PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, &Key);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = ZwQueryValueKey(Key, &PathName, KeyValuePartialInformation, PathInfo, sizeof(PathBuffer), &Length);
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (PathInfo->Type != REG_SZ)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }
    Wide = (PWCHAR)PathInfo->Data;
    for (Index = 0; Index < sizeof(Path) - 1 && Index < PathInfo->DataLength / sizeof(WCHAR) && Wide[Index]; ++Index)
        Path[Index] = (CHAR)Wide[Index];
    Path[Index] = '\0';
    if (!FdtFindNode(&Tree->Fdt, Path, &Tree->Node, NULL))
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;

Done:
    if (!NT_SUCCESS(Status))
        FdtDriverClose(Tree);
    return Status;
}

NTSTATUS
NTAPI
FdtDriverOpenByCompatible(
    _In_z_ PCSTR Compatible,
    _Out_ PFDT_DRIVER_TREE Tree)
{
    const VOID *List;
    ULONG Offset, Length;
    NTSTATUS Status;

    Status = FdtDriverLoadBlob(Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Offset = FdtRootNode(&Tree->Fdt); Offset != FDT_NO_NODE; Offset = FdtSkipToken(&Tree->Fdt, Offset))
    {
        if (FdtReadToken(&Tree->Fdt, Offset) != FDT_BEGIN_NODE)
            continue;
        List = FdtGetProperty(&Tree->Fdt, Offset, "compatible", &Length);
        if (List && FdtStringListContains(List, Length, Compatible))
        {
            Tree->Node = Offset;
            return STATUS_SUCCESS;
        }
    }
    FdtDriverClose(Tree);
    return STATUS_NOT_FOUND;
}

NTSTATUS
NTAPI
FdtDriverOpenAlias(
    _In_z_ PCSTR PathOrAlias,
    _Out_ PFDT_DRIVER_TREE Tree)
{
    NTSTATUS Status;

    Status = FdtDriverLoadBlob(Tree);
    if (NT_SUCCESS(Status) &&
        !FdtResolvePath(&Tree->Fdt, PathOrAlias, &Tree->Node, NULL, NULL))
    {
        Status = STATUS_NOT_FOUND;
    }
    if (!NT_SUCCESS(Status))
        FdtDriverClose(Tree);
    return Status;
}

VOID
NTAPI
FdtDriverClose(
    _Inout_ PFDT_DRIVER_TREE Tree)
{
    if (Tree->Blob)
        ExFreePoolWithTag(Tree->Blob, FDT_DRIVER_TAG);
    RtlZeroMemory(Tree, sizeof(*Tree));
    Tree->Node = FDT_NO_NODE;
}

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
    PHYSICAL_ADDRESS Physical;
    ULONG64 Address, Size;
    PUCHAR Va;

    *SizeOut = 0;
    if (!FdtDriverReadReg(Tree, Node, Index, &Address, &Size) || !Size || Size > 0x1000000)
        return NULL;
    Physical.QuadPart = (LONGLONG)Address;
    Va = MmMapIoSpace(Physical, (SIZE_T)Size, MmNonCached);
    if (Va)
        *SizeOut = (ULONG)Size;
    if (AddressOut)
        *AddressOut = Address;
    return Va;
}

NTSTATUS
NTAPI
FdtDriverOpenByAddress(
    _In_z_ PCSTR Compatible,
    _In_ ULONG64 Address,
    _Out_ PFDT_DRIVER_TREE Tree)
{
    static UNICODE_STRING SystemKey = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System");
    static UNICODE_STRING BlobName = RTL_CONSTANT_STRING(L"DeviceTree");
    PKEY_VALUE_PARTIAL_INFORMATION Info = NULL;
    OBJECT_ATTRIBUTES Attributes;
    ULONG Length, Offset;
    HANDLE Key;
    NTSTATUS Status;

    RtlZeroMemory(Tree, sizeof(*Tree));
    Tree->Node = FDT_NO_NODE;
    InitializeObjectAttributes(&Attributes, &SystemKey, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryValueKey(Key, &BlobName, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_BUFFER_OVERFLOW)
    {
        Info = ExAllocatePoolWithTag(PagedPool, Length, FDT_DRIVER_TAG);
        Status = Info ? ZwQueryValueKey(Key, &BlobName, KeyValuePartialInformation, Info, Length, &Length) :
                        STATUS_INSUFFICIENT_RESOURCES;
    }
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        goto Done;
    Tree->Blob = ExAllocatePoolWithTag(NonPagedPool, Info->DataLength, FDT_DRIVER_TAG);
    if (!Tree->Blob)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(Tree->Blob, Info->Data, Info->DataLength);
    if (!FdtOpen(Tree->Blob, Info->DataLength, &Tree->Fdt))
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }
    Status = STATUS_NOT_FOUND;
    for (Offset = FdtRootNode(&Tree->Fdt); Offset != FDT_NO_NODE; Offset = FdtSkipToken(&Tree->Fdt, Offset))
    {
        const VOID *List;
        ULONG64 Base, Size;

        if (FdtReadToken(&Tree->Fdt, Offset) != FDT_BEGIN_NODE)
            continue;
        List = FdtGetProperty(&Tree->Fdt, Offset, "compatible", &Length);
        if (!List || !FdtStringListContains(List, Length, Compatible))
            continue;
        if (FdtDriverReadReg(Tree, Offset, 0, &Base, &Size) && Base == Address)
        {
            Tree->Node = Offset;
            Status = STATUS_SUCCESS;
            break;
        }
    }

Done:
    if (Info)
        ExFreePoolWithTag(Info, FDT_DRIVER_TAG);
    if (!NT_SUCCESS(Status))
        FdtDriverClose(Tree);
    return Status;
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
