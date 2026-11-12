/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Plug and Play devices described by the firmware device tree
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntifs.h>
#endif
#include <libernt/fdtlib.h>
#include "../include/halpnp.h"
#include "../include/halfdt.h"

#define TAG_HAL_FDT 'tdFH'
#define HALP_FDT_MAX_MEMORY 8
#define HALP_FDT_MAX_INTERRUPTS 8
#define HALP_FDT_MAX_CELLS 4
#define HALP_FDT_MAX_DEPTH 4
#define HALP_FDT_KEY_DEPTH 2
#define HALP_FDT_MAX_NAME 64
#define HALP_FDT_MAX_PATH 256

typedef struct _HALP_FDT_RESOURCES
{
    ULONG MemoryCount;
    ULONG64 MemoryBase[HALP_FDT_MAX_MEMORY];
    ULONG64 MemorySize[HALP_FDT_MAX_MEMORY];
    ULONG InterruptCount;
    ULONG Interrupt[HALP_FDT_MAX_INTERRUPTS];
    BOOLEAN InterruptEdge[HALP_FDT_MAX_INTERRUPTS];
} HALP_FDT_RESOURCES, *PHALP_FDT_RESOURCES;

static FDT HalpFdt;
static BOOLEAN HalpFdtOpened;

static const CHAR *const HalpFdtIgnoredCompatibles[] =
{
    "simple-bus",
    "fixed-clock",
    "operating-points-v2",
    "arm,armv8-timer",
    "arm,armv7-timer",
    "arm,psci",
    "arm,psci-0.2",
    "arm,psci-1.0",
};

static const CHAR *const HalpFdtIgnoredRootNodes[] =
{
    "cpus",
    "memory",
    "reserved-memory",
    "aliases",
    "chosen",
};

static
BOOLEAN
HalpFdtEnabled(
    _In_ ULONG Node)
{
    ULONG Length;
    const VOID *Status = FdtGetProperty(&HalpFdt, Node, "status", &Length);

    return !Status || FdtStringListContains(Status, Length, "okay") ||
           FdtStringListContains(Status, Length, "ok");
}

static
BOOLEAN
HalpFdtIsCompatible(
    _In_ ULONG Node,
    _In_z_ const CHAR *Name)
{
    ULONG Length;
    const VOID *Compatible = FdtGetProperty(&HalpFdt, Node, "compatible", &Length);

    return Compatible && FdtStringListContains(Compatible, Length, Name);
}

static
BOOLEAN
HalpFdtHasProperty(
    _In_ ULONG Node,
    _In_z_ const CHAR *Name)
{
    ULONG Length;

    return FdtGetProperty(&HalpFdt, Node, Name, &Length) != NULL;
}

static
ULONG
HalpFdtFindPhandle(
    _In_ ULONG Phandle)
{
    ULONG Offset = FdtRootNode(&HalpFdt), Value;

    if (!Phandle || Phandle == MAXULONG)
        return FDT_NO_NODE;
    while (Offset != FDT_NO_NODE)
    {
        if (FdtReadToken(&HalpFdt, Offset) == FDT_BEGIN_NODE &&
            FdtReadU32(&HalpFdt, Offset, "phandle", &Value) && Value == Phandle)
        {
            return Offset;
        }
        Offset = FdtSkipToken(&HalpFdt, Offset);
    }
    return FDT_NO_NODE;
}

static
BOOLEAN
HalpFdtFindParent(
    _In_ ULONG Bus,
    _In_ ULONG Node,
    _In_ ULONG Depth,
    _Out_ PULONG Parent)
{
    ULONG Child, Next;

    for (Child = FdtFirstChild(&HalpFdt, Bus); Child != FDT_NO_NODE; Child = Next)
    {
        Next = FdtNextSibling(&HalpFdt, Child);
        if (Child == Node)
        {
            *Parent = Bus;
            return TRUE;
        }
        if (Child < Node && (Next == FDT_NO_NODE || Node < Next))
            return Depth < 8 && HalpFdtFindParent(Child, Node, Depth + 1, Parent);
    }
    return FALSE;
}

static
BOOLEAN
HalpFdtIsDevice(
    _In_ ULONG Node)
{
    ULONG Length, Index;
    const CHAR *Compatible = FdtGetProperty(&HalpFdt, Node, "compatible", &Length);

    if (!Compatible || Length < 2 || Compatible[Length - 1] != '\0' || !HalpFdtEnabled(Node))
        return FALSE;
    for (Index = 0; Index < RTL_NUMBER_OF(HalpFdtIgnoredCompatibles); ++Index)
    {
        if (FdtStringListContains(Compatible, Length, HalpFdtIgnoredCompatibles[Index]))
            return FALSE;
    }
    return !HalpFdtHasProperty(Node, "interrupt-controller") || HalpFdtHasProperty(Node, "gpio-controller") ||
           HalpFdtHasProperty(Node, "interrupts") || HalpFdtHasProperty(Node, "interrupts-extended");
}

static
BOOLEAN
HalpFdtIsIgnoredRootNode(
    _In_ ULONG Node)
{
    ULONG NameLength, Index, EntryLength;
    const CHAR *Name = FdtNodeName(&HalpFdt, Node, &NameLength);

    if (!Name)
        return TRUE;
    for (Index = 0; Index < RTL_NUMBER_OF(HalpFdtIgnoredRootNodes); ++Index)
    {
        EntryLength = (ULONG)strlen(HalpFdtIgnoredRootNodes[Index]);
        if (FdtNameMatches(Name, NameLength, HalpFdtIgnoredRootNodes[Index], EntryLength))
            return TRUE;
    }
    return FALSE;
}

static
VOID
HalpFdtWalk(
    _In_ ULONG Bus,
    _In_ ULONG Depth,
    _Out_writes_(MaximumCount) PHALP_FDT_DEVICE_NODE Devices,
    _In_ ULONG MaximumCount,
    _Inout_ PULONG Count)
{
    ULONG Node;

    for (Node = FdtFirstChild(&HalpFdt, Bus); Node != FDT_NO_NODE && *Count < MaximumCount;
         Node = FdtNextSibling(&HalpFdt, Node))
    {
        if (Depth == 0 && HalpFdtIsIgnoredRootNode(Node))
            continue;
        if (HalpFdtIsCompatible(Node, "simple-bus"))
        {
            if (HalpFdtEnabled(Node) && Depth + 1 < HALP_FDT_MAX_DEPTH)
                HalpFdtWalk(Node, Depth + 1, Devices, MaximumCount, Count);
            continue;
        }
        if (!HalpFdtIsDevice(Node))
            continue;
        Devices[*Count].Node = Node;
        Devices[*Count].Parent = Bus;
        ++*Count;
    }
}

static
BOOLEAN
HalpFdtTranslateAddress(
    _In_ ULONG Parent,
    _Inout_ PULONG64 Address)
{
    ULONG Length, ChildCells, ParentCells, SizeCells, Unused, Stride, Entry, GrandParent;
    ULONG64 ChildBase, ParentBase, Size;
    const VOID *Ranges;

    if (Parent == FdtRootNode(&HalpFdt))
        return TRUE;
    Ranges = FdtGetProperty(&HalpFdt, Parent, "ranges", &Length);
    if (!Ranges)
        return FALSE;
    if (!Length)
        return TRUE;
    if (!HalpFdtFindParent(FdtRootNode(&HalpFdt), Parent, 0, &GrandParent))
        return FALSE;
    FdtGetCellCounts(&HalpFdt, Parent, &ChildCells, &SizeCells);
    FdtGetCellCounts(&HalpFdt, GrandParent, &ParentCells, &Unused);
    Stride = ChildCells + ParentCells + SizeCells;
    if (!Stride)
        return FALSE;
    for (Entry = 0; (Entry + 1) * Stride * sizeof(ULONG) <= Length; ++Entry)
    {
        if (!FdtReadCells(Ranges, Length, Entry * Stride, ChildCells, &ChildBase) ||
            !FdtReadCells(Ranges, Length, Entry * Stride + ChildCells, ParentCells, &ParentBase) ||
            !FdtReadCells(Ranges, Length, Entry * Stride + ChildCells + ParentCells, SizeCells, &Size))
        {
            return FALSE;
        }
        if (*Address >= ChildBase && *Address - ChildBase < Size)
        {
            *Address = *Address - ChildBase + ParentBase;
            return TRUE;
        }
    }
    return FALSE;
}

static
ULONG
HalpFdtInterruptParent(
    _In_ ULONG Node,
    _In_ ULONG Parent)
{
    ULONG Phandle, Current = Node, Up;

    if (FdtReadU32(&HalpFdt, Node, "interrupt-parent", &Phandle))
        return HalpFdtFindPhandle(Phandle);
    for (Current = Parent; Current != FDT_NO_NODE; Current = Up)
    {
        if (FdtReadU32(&HalpFdt, Current, "interrupt-parent", &Phandle))
            return HalpFdtFindPhandle(Phandle);
        if (Current == FdtRootNode(&HalpFdt) ||
            !HalpFdtFindParent(FdtRootNode(&HalpFdt), Current, 0, &Up))
        {
            break;
        }
    }
    return FDT_NO_NODE;
}

static
VOID
HalpFdtAddInterrupt(
    _In_ ULONG Controller,
    _In_reads_bytes_(CellCount * sizeof(ULONG)) const UCHAR *Specifier,
    _In_ ULONG CellCount,
    _Inout_ PHALP_FDT_RESOURCES Resources)
{
    ULONG Cells[HALP_FDT_MAX_CELLS], Index, Length, Gsi;
    const CHAR *Compatible;
    BOOLEAN Edge;

    if (Controller == FDT_NO_NODE || CellCount == 0 || CellCount > HALP_FDT_MAX_CELLS ||
        Resources->InterruptCount >= HALP_FDT_MAX_INTERRUPTS)
    {
        return;
    }
    Compatible = FdtGetProperty(&HalpFdt, Controller, "compatible", &Length);
    if (!Compatible || !Length)
        return;
    for (Index = 0; Index < CellCount; ++Index)
        Cells[Index] = FdtReadBigEndian32(Specifier + Index * sizeof(ULONG));
    if (!HalpTranslateDeviceTreeInterrupt(Compatible, Length, Cells, CellCount, &Gsi, &Edge))
        return;
    Resources->Interrupt[Resources->InterruptCount] = Gsi;
    Resources->InterruptEdge[Resources->InterruptCount++] = Edge;
}

static
BOOLEAN
HalpFdtDescribe(
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _Out_ PHALP_FDT_RESOURCES Resources)
{
    ULONG Index, Length, Controller, CellCount, Offset;
    ULONG64 Address, Size;
    const UCHAR *Property;

    RtlZeroMemory(Resources, sizeof(*Resources));
    if (!HalpFdtOpened || !HalpFdtIsDevice(Node))
        return FALSE;

    for (Index = 0; Index < HALP_FDT_MAX_MEMORY; ++Index)
    {
        if (!FdtReadReg(&HalpFdt, Node, Parent, Index, &Address, &Size))
            break;
        if (!Size || Size > MAXULONG || Address + Size < Address || !HalpFdtTranslateAddress(Parent, &Address))
            continue;
        Resources->MemoryBase[Resources->MemoryCount] = Address;
        Resources->MemorySize[Resources->MemoryCount++] = Size;
    }

    Property = FdtGetProperty(&HalpFdt, Node, "interrupts-extended", &Length);
    if (Property)
    {
        for (Offset = 0; Offset + sizeof(ULONG) <= Length;)
        {
            Controller = HalpFdtFindPhandle(FdtReadBigEndian32(Property + Offset));
            if (Controller == FDT_NO_NODE)
                break;
            if (!FdtReadU32(&HalpFdt, Controller, "#interrupt-cells", &CellCount))
                CellCount = 1;
            Offset += sizeof(ULONG);
            if (CellCount == 0 || Offset + CellCount * sizeof(ULONG) > Length)
                break;
            HalpFdtAddInterrupt(Controller, Property + Offset, CellCount, Resources);
            Offset += CellCount * sizeof(ULONG);
        }
        return TRUE;
    }

    Property = FdtGetProperty(&HalpFdt, Node, "interrupts", &Length);
    if (Property)
    {
        Controller = HalpFdtInterruptParent(Node, Parent);
        if (Controller == FDT_NO_NODE)
            return TRUE;
        if (!FdtReadU32(&HalpFdt, Controller, "#interrupt-cells", &CellCount))
            CellCount = 1;
        if (CellCount == 0)
            return TRUE;
        for (Offset = 0; Offset + CellCount * sizeof(ULONG) <= Length; Offset += CellCount * sizeof(ULONG))
            HalpFdtAddInterrupt(Controller, Property + Offset, CellCount, Resources);
    }
    return TRUE;
}

static
PWCHAR
HalpFdtAllocateString(
    _In_ SIZE_T Characters)
{
    return ExAllocatePoolZero(PagedPool, Characters * sizeof(WCHAR), TAG_HAL_FDT);
}

static
WCHAR
HalpFdtIdCharacter(
    _In_ CHAR Character)
{
    return (Character <= ' ' || Character > '~' || Character == ',' || Character == '\\') ?
           L'_' : (WCHAR)Character;
}

#define HALP_FDT_MAX_PCI_WINDOWS 4

typedef struct _HALP_FDT_PCI_HOST
{
    ULONG FirstBus;
    ULONG LastBus;
    ULONG WindowCount;
    ULONG64 WindowBase[HALP_FDT_MAX_PCI_WINDOWS];
    ULONG WindowSize[HALP_FDT_MAX_PCI_WINDOWS];
    BOOLEAN WindowPrefetchable[HALP_FDT_MAX_PCI_WINDOWS];
} HALP_FDT_PCI_HOST, *PHALP_FDT_PCI_HOST;

static
BOOLEAN
HalpFdtDescribePciHost(
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _Out_opt_ PHALP_FDT_PCI_HOST Host)
{
    HALP_FDT_PCI_HOST Local;
    ULONG Length, ParentCells, Unused, Entry, Flags;
    ULONG64 BusAddress, CpuAddress, Size;
    const VOID *Property;

    if (!HalpFdtIsCompatible(Node, "pci-host-ecam-generic"))
        return FALSE;
    if (!Host)
        Host = &Local;
    RtlZeroMemory(Host, sizeof(*Host));
    Host->LastBus = 255;
    Property = FdtGetProperty(&HalpFdt, Node, "bus-range", &Length);
    if (Property && Length == 2 * sizeof(ULONG))
    {
        Host->FirstBus = FdtReadBigEndian32(Property);
        Host->LastBus = FdtReadBigEndian32((const UCHAR *)Property + sizeof(ULONG));
    }
    if (Host->FirstBus > Host->LastBus || Host->LastBus > 255)
        return FALSE;
    FdtGetCellCounts(&HalpFdt, Parent, &ParentCells, &Unused);
    Property = FdtGetProperty(&HalpFdt, Node, "ranges", &Length);
    for (Entry = 0; Property && ParentCells <= 2 && (Entry + 1) * (5 + ParentCells) * sizeof(ULONG) <= Length &&
         Host->WindowCount < HALP_FDT_MAX_PCI_WINDOWS; ++Entry)
    {
        ULONG Base = Entry * (5 + ParentCells);

        Flags = FdtReadBigEndian32((const UCHAR *)Property + Base * sizeof(ULONG));
        if (!FdtReadCells(Property, Length, Base + 1, 2, &BusAddress) ||
            !FdtReadCells(Property, Length, Base + 3, ParentCells, &CpuAddress) ||
            !FdtReadCells(Property, Length, Base + 3 + ParentCells, 2, &Size))
            break;
        if (((Flags >> 24) & 3) < 2 || !Size || Size > MAXULONG || BusAddress != CpuAddress ||
            !HalpFdtTranslateAddress(Parent, &CpuAddress))
            continue;
        Host->WindowBase[Host->WindowCount] = CpuAddress;
        Host->WindowSize[Host->WindowCount] = (ULONG)Size;
        Host->WindowPrefetchable[Host->WindowCount] = (Flags & 0x40000000) != 0;
        Host->WindowCount++;
    }
    return TRUE;
}

BOOLEAN
NTAPI
HalpFdtInitialize(VOID)
{
    static UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System");
    static UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"DeviceTree");
    OBJECT_ATTRIBUTES Attributes;
    const VOID *Blob;
    ULONG Size;
    HANDLE Key;

    if (HalpFdtOpened)
        return TRUE;
    if (!HalpQueryDeviceTree(&Blob, &Size) || !Blob || !FdtOpen(Blob, Size, &HalpFdt))
        return FALSE;
    HalpFdtOpened = TRUE;

    InitializeObjectAttributes(&Attributes, &KeyName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (NT_SUCCESS(ZwOpenKey(&Key, KEY_SET_VALUE, &Attributes)))
    {
        ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, (PVOID)HalpFdt.Blob, HalpFdt.TotalSize);
        ZwClose(Key);
    }
    return TRUE;
}

ULONG
NTAPI
HalpFdtEnumerateDevices(
    _Out_writes_(MaximumCount) PHALP_FDT_DEVICE_NODE Devices,
    _In_ ULONG MaximumCount)
{
    ULONG Count = 0;

    if (!HalpFdtOpened)
        return 0;
    HalpFdtWalk(FdtRootNode(&HalpFdt), 0, Devices, MaximumCount, &Count);
    return Count;
}

NTSTATUS
NTAPI
HalpFdtQueryId(
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PWCHAR *Id)
{
    static const WCHAR Prefix[] = L"FDT\\";
    static const WCHAR Digits[] = L"0123456789abcdef";
    const CHAR *List, *Entry, *Name;
    ULONG Length, Cursor = 0, EntryLength, Index, NameLength;
    SIZE_T Characters = 1;
    HALP_FDT_RESOURCES Resources;
    BOOLEAN All = (IdType == BusQueryHardwareIDs);
    PWCHAR Ids, Out;

    *Id = NULL;
    if (IdType == BusQueryInstanceID)
    {
        if (HalpFdtDescribe(Node, Parent, &Resources) && Resources.MemoryCount)
        {
            Ids = HalpFdtAllocateString(17);
            if (!Ids)
                return STATUS_INSUFFICIENT_RESOURCES;
            for (Index = 0; Index < 16; ++Index)
                Ids[Index] = Digits[(Resources.MemoryBase[0] >> (60 - 4 * Index)) & 15];
            *Id = Ids;
            return STATUS_SUCCESS;
        }
        Name = FdtNodeName(&HalpFdt, Node, &NameLength);
        if (!Name || !NameLength || NameLength >= HALP_FDT_MAX_NAME)
            return STATUS_NOT_SUPPORTED;
        Ids = HalpFdtAllocateString(NameLength + 1);
        if (!Ids)
            return STATUS_INSUFFICIENT_RESOURCES;
        for (Index = 0; Index < NameLength; ++Index)
            Ids[Index] = HalpFdtIdCharacter(Name[Index]);
        *Id = Ids;
        return STATUS_SUCCESS;
    }
    if (IdType != BusQueryDeviceID && IdType != BusQueryHardwareIDs)
        return STATUS_NOT_SUPPORTED;
    if (HalpFdtDescribePciHost(Node, Parent, NULL))
    {
        static const WCHAR PciDeviceId[] = L"ACPI\\PNP0A03";
        static const WCHAR PciHardwareIds[] = L"*PNP0A03\0";
        const WCHAR *Source = (IdType == BusQueryDeviceID) ? PciDeviceId : PciHardwareIds;
        SIZE_T Bytes = (IdType == BusQueryDeviceID) ? sizeof(PciDeviceId) : sizeof(PciHardwareIds);

        Ids = HalpFdtAllocateString(Bytes / sizeof(WCHAR));
        if (!Ids)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(Ids, Source, Bytes);
        *Id = Ids;
        return STATUS_SUCCESS;
    }

    List = FdtGetProperty(&HalpFdt, Node, "compatible", &Length);
    while ((Entry = FdtNextString(List, Length, &Cursor, &EntryLength)) != NULL)
    {
        Characters += RTL_NUMBER_OF(Prefix) + EntryLength;
        if (!All)
            break;
    }
    if (Characters == 1)
        return STATUS_NOT_SUPPORTED;
    Ids = HalpFdtAllocateString(Characters);
    if (!Ids)
        return STATUS_INSUFFICIENT_RESOURCES;
    Out = Ids;
    Cursor = 0;
    while ((Entry = FdtNextString(List, Length, &Cursor, &EntryLength)) != NULL)
    {
        RtlCopyMemory(Out, Prefix, sizeof(Prefix) - sizeof(WCHAR));
        Out += RTL_NUMBER_OF(Prefix) - 1;
        for (Index = 0; Index < EntryLength; ++Index)
            *Out++ = HalpFdtIdCharacter(Entry[Index]);
        *Out++ = UNICODE_NULL;
        if (!All)
            break;
    }
    *Id = Ids;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpFdtQueryDeviceText(
    _In_ ULONG Node,
    _Out_ PWCHAR *Text)
{
    ULONG NameLength, Index;
    const CHAR *Name = FdtNodeName(&HalpFdt, Node, &NameLength);
    PWCHAR Buffer;

    *Text = NULL;
    if (!HalpFdtOpened || !Name || !NameLength || NameLength >= HALP_FDT_MAX_NAME)
        return STATUS_NOT_SUPPORTED;
    Buffer = HalpFdtAllocateString(NameLength + 1);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Index = 0; Index < NameLength; ++Index)
        Buffer[Index] = (WCHAR)(UCHAR)Name[Index];
    *Text = Buffer;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpFdtQueryResources(
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _Out_ PCM_RESOURCE_LIST *Resources)
{
    HALP_FDT_RESOURCES Device;
    HALP_FDT_PCI_HOST PciHost;
    PCM_RESOURCE_LIST List;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Count, Index;
    SIZE_T Size;

    *Resources = NULL;
    if (HalpFdtOpened && HalpFdtDescribePciHost(Node, Parent, &PciHost))
    {
        Size = FIELD_OFFSET(CM_RESOURCE_LIST, List[0].PartialResourceList.PartialDescriptors) +
               (1 + PciHost.WindowCount) * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
        List = ExAllocatePoolZero(PagedPool, Size, TAG_HAL_FDT);
        if (!List)
            return STATUS_INSUFFICIENT_RESOURCES;
        List->Count = 1;
        List->List[0].InterfaceType = PCIBus;
        List->List[0].BusNumber = PciHost.FirstBus;
        List->List[0].PartialResourceList.Version = 1;
        List->List[0].PartialResourceList.Revision = 1;
        List->List[0].PartialResourceList.Count = 1 + PciHost.WindowCount;
        Descriptor = List->List[0].PartialResourceList.PartialDescriptors;
        Descriptor->Type = CmResourceTypeBusNumber;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor->u.BusNumber.Start = PciHost.FirstBus;
        Descriptor->u.BusNumber.Length = PciHost.LastBus - PciHost.FirstBus + 1;
        for (Index = 0; Index < PciHost.WindowCount; ++Index)
        {
            ++Descriptor;
            Descriptor->Type = CmResourceTypeMemory;
            Descriptor->ShareDisposition = CmResourceShareShared;
            Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE | CM_RESOURCE_MEMORY_WINDOW_DECODE |
                                (PciHost.WindowPrefetchable[Index] ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);
            Descriptor->u.Memory.Start.QuadPart = PciHost.WindowBase[Index];
            Descriptor->u.Memory.Length = PciHost.WindowSize[Index];
        }
        *Resources = List;
        return STATUS_SUCCESS;
    }
    if (!HalpFdtDescribe(Node, Parent, &Device))
        return STATUS_NO_SUCH_DEVICE;
    Count = Device.MemoryCount + Device.InterruptCount;
    if (!Count)
        return STATUS_NOT_SUPPORTED;
    Size = FIELD_OFFSET(CM_RESOURCE_LIST, List[0].PartialResourceList.PartialDescriptors) +
           Count * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    List = ExAllocatePoolZero(PagedPool, Size, TAG_HAL_FDT);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    List->Count = 1;
    List->List[0].InterfaceType = Internal;
    List->List[0].PartialResourceList.Version = 1;
    List->List[0].PartialResourceList.Revision = 1;
    List->List[0].PartialResourceList.Count = Count;
    Descriptor = List->List[0].PartialResourceList.PartialDescriptors;
    for (Index = 0; Index < Device.MemoryCount; ++Index, ++Descriptor)
    {
        Descriptor->Type = CmResourceTypeMemory;
        Descriptor->ShareDisposition = CmResourceShareShared;
        Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
        Descriptor->u.Memory.Start.QuadPart = Device.MemoryBase[Index];
        Descriptor->u.Memory.Length = (ULONG)Device.MemorySize[Index];
    }
    for (Index = 0; Index < Device.InterruptCount; ++Index, ++Descriptor)
    {
        Descriptor->Type = CmResourceTypeInterrupt;
        Descriptor->ShareDisposition = CmResourceShareShared;
        Descriptor->Flags = Device.InterruptEdge[Index] ? CM_RESOURCE_INTERRUPT_LATCHED :
                                                          CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        Descriptor->u.Interrupt.Level = Device.Interrupt[Index];
        Descriptor->u.Interrupt.Vector = Device.Interrupt[Index];
        Descriptor->u.Interrupt.Affinity = (KAFFINITY)-1;
    }
    *Resources = List;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpFdtQueryRequirements(
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _Out_ PIO_RESOURCE_REQUIREMENTS_LIST *Requirements)
{
    HALP_FDT_RESOURCES Device;
    HALP_FDT_PCI_HOST PciHost;
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Count, Index;
    SIZE_T Size;

    *Requirements = NULL;
    if (HalpFdtOpened && HalpFdtDescribePciHost(Node, Parent, &PciHost))
    {
        Size = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) +
               (1 + PciHost.WindowCount) * sizeof(IO_RESOURCE_DESCRIPTOR);
        List = ExAllocatePoolZero(PagedPool, Size, TAG_HAL_FDT);
        if (!List)
            return STATUS_INSUFFICIENT_RESOURCES;
        List->ListSize = (ULONG)Size;
        List->InterfaceType = PCIBus;
        List->BusNumber = PciHost.FirstBus;
        List->AlternativeLists = 1;
        List->List[0].Version = 1;
        List->List[0].Revision = 1;
        List->List[0].Count = 1 + PciHost.WindowCount;
        Descriptor = List->List[0].Descriptors;
        Descriptor->Type = CmResourceTypeBusNumber;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor->u.BusNumber.MinBusNumber = PciHost.FirstBus;
        Descriptor->u.BusNumber.MaxBusNumber = PciHost.LastBus;
        Descriptor->u.BusNumber.Length = PciHost.LastBus - PciHost.FirstBus + 1;
        for (Index = 0; Index < PciHost.WindowCount; ++Index)
        {
            ++Descriptor;
            Descriptor->Type = CmResourceTypeMemory;
            Descriptor->ShareDisposition = CmResourceShareShared;
            Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE | CM_RESOURCE_MEMORY_WINDOW_DECODE |
                                (PciHost.WindowPrefetchable[Index] ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);
            Descriptor->u.Memory.Length = PciHost.WindowSize[Index];
            Descriptor->u.Memory.Alignment = 1;
            Descriptor->u.Memory.MinimumAddress.QuadPart = PciHost.WindowBase[Index];
            Descriptor->u.Memory.MaximumAddress.QuadPart = PciHost.WindowBase[Index] + PciHost.WindowSize[Index] - 1;
        }
        *Requirements = List;
        return STATUS_SUCCESS;
    }
    if (!HalpFdtDescribe(Node, Parent, &Device))
        return STATUS_NO_SUCH_DEVICE;
    Count = Device.MemoryCount + Device.InterruptCount;
    if (!Count)
        return STATUS_NOT_SUPPORTED;
    Size = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) +
           Count * sizeof(IO_RESOURCE_DESCRIPTOR);
    List = ExAllocatePoolZero(PagedPool, Size, TAG_HAL_FDT);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    List->ListSize = (ULONG)Size;
    List->InterfaceType = Internal;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = Count;
    Descriptor = List->List[0].Descriptors;
    for (Index = 0; Index < Device.MemoryCount; ++Index, ++Descriptor)
    {
        Descriptor->Type = CmResourceTypeMemory;
        Descriptor->ShareDisposition = CmResourceShareShared;
        Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
        Descriptor->u.Memory.Length = (ULONG)Device.MemorySize[Index];
        Descriptor->u.Memory.Alignment = 1;
        Descriptor->u.Memory.MinimumAddress.QuadPart = Device.MemoryBase[Index];
        Descriptor->u.Memory.MaximumAddress.QuadPart = Device.MemoryBase[Index] + Device.MemorySize[Index] - 1;
    }
    for (Index = 0; Index < Device.InterruptCount; ++Index, ++Descriptor)
    {
        Descriptor->Type = CmResourceTypeInterrupt;
        Descriptor->ShareDisposition = CmResourceShareShared;
        Descriptor->Flags = Device.InterruptEdge[Index] ? CM_RESOURCE_INTERRUPT_LATCHED :
                                                          CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        Descriptor->u.Interrupt.MinimumVector = Device.Interrupt[Index];
        Descriptor->u.Interrupt.MaximumVector = Device.Interrupt[Index];
    }
    *Requirements = List;
    return STATUS_SUCCESS;
}

static
VOID
HalpFdtWriteNode(
    _In_ HANDLE Key,
    _In_ ULONG Node,
    _In_ ULONG Depth)
{
    ULONG Offset = FdtSkipToken(&HalpFdt, Node), Child;

    while (Offset != FDT_NO_NODE)
    {
        ULONG Token = FdtReadToken(&HalpFdt, Offset);

        if (Token == FDT_PROPERTY)
        {
            const UCHAR *Header = HalpFdt.Blob + HalpFdt.StructureOffset + Offset + sizeof(ULONG);
            WCHAR Name[HALP_FDT_MAX_NAME];
            UNICODE_STRING ValueName;
            ULONG ValueLength, NameOffset, Index;
            const CHAR *Source;

            if (Offset > HalpFdt.StructureSize - 3 * sizeof(ULONG))
                break;
            ValueLength = FdtReadBigEndian32(Header);
            NameOffset = FdtReadBigEndian32(Header + sizeof(ULONG));
            if (NameOffset >= HalpFdt.StringsSize ||
                ValueLength > HalpFdt.StructureSize - (Offset + 3 * sizeof(ULONG)))
                break;
            Source = (const CHAR *)(HalpFdt.Blob + HalpFdt.StringsOffset + NameOffset);
            for (Index = 0; Index < HALP_FDT_MAX_NAME - 1 && Index < HalpFdt.StringsSize - NameOffset && Source[Index];
                 ++Index)
                Name[Index] = (WCHAR)(UCHAR)Source[Index];
            Name[Index] = UNICODE_NULL;
            if (Index && Index < HalpFdt.StringsSize - NameOffset && !Source[Index])
            {
                RtlInitUnicodeString(&ValueName, Name);
                ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, (PVOID)(Header + 2 * sizeof(ULONG)), ValueLength);
            }
        }
        else if (Token != FDT_NOP)
        {
            break;
        }
        Offset = FdtSkipToken(&HalpFdt, Offset);
    }

    if (!Depth)
        return;
    for (Child = FdtFirstChild(&HalpFdt, Node); Child != FDT_NO_NODE;
         Child = FdtNextSibling(&HalpFdt, Child))
    {
        WCHAR Name[HALP_FDT_MAX_NAME];
        OBJECT_ATTRIBUTES Attributes;
        UNICODE_STRING KeyName;
        ULONG NameLength, Index;
        const CHAR *Source = FdtNodeName(&HalpFdt, Child, &NameLength);
        HANDLE ChildKey;

        if (!Source || !NameLength || NameLength >= HALP_FDT_MAX_NAME)
            continue;
        for (Index = 0; Index < NameLength; ++Index)
            Name[Index] = (Source[Index] == '\\' || Source[Index] <= ' ') ? L'_' : (WCHAR)(UCHAR)Source[Index];
        Name[NameLength] = UNICODE_NULL;
        RtlInitUnicodeString(&KeyName, Name);
        InitializeObjectAttributes(&Attributes, &KeyName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Key, NULL);
        if (!NT_SUCCESS(ZwCreateKey(&ChildKey, KEY_SET_VALUE | KEY_CREATE_SUB_KEY, &Attributes, 0, NULL,
                                    REG_OPTION_VOLATILE, NULL)))
            continue;
        HalpFdtWriteNode(ChildKey, Child, Depth - 1);
        ZwClose(ChildKey);
    }
}

static
BOOLEAN
HalpFdtBuildPath(
    _In_ ULONG Node,
    _Out_writes_(Capacity) PWCHAR Path,
    _In_ ULONG Capacity)
{
    ULONG Chain[HALP_FDT_MAX_DEPTH + 4], Depth = 0, Current = Node, Parent, Used = 0, Index, Level, NameLength;
    const CHAR *Name;

    while (Current != FdtRootNode(&HalpFdt))
    {
        if (Depth >= RTL_NUMBER_OF(Chain) ||
            !HalpFdtFindParent(FdtRootNode(&HalpFdt), Current, 0, &Parent))
            return FALSE;
        Chain[Depth++] = Current;
        Current = Parent;
    }
    for (Level = Depth; Level-- > 0;)
    {
        Name = FdtNodeName(&HalpFdt, Chain[Level], &NameLength);
        if (!Name || Used + NameLength + 2 > Capacity)
            return FALSE;
        Path[Used++] = L'/';
        for (Index = 0; Index < NameLength; ++Index)
            Path[Used++] = (WCHAR)(UCHAR)Name[Index];
    }
    if (!Used)
        Path[Used++] = L'/';
    Path[Used] = UNICODE_NULL;
    return TRUE;
}

VOID
NTAPI
HalpFdtPublishProperties(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG Node,
    _In_ ULONG Parent)
{
    static UNICODE_STRING PathName = RTL_CONSTANT_STRING(L"FdtPath");
    static UNICODE_STRING NodeName = RTL_CONSTANT_STRING(L"FdtNode");
    WCHAR Path[HALP_FDT_MAX_PATH];
    HANDLE Key;

    UNREFERENCED_PARAMETER(Parent);

    if (!HalpFdtOpened ||
        !NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                            KEY_SET_VALUE | KEY_CREATE_SUB_KEY, &Key)))
    {
        return;
    }
    HalpFdtWriteNode(Key, Node, HALP_FDT_KEY_DEPTH);
    if (HalpFdtBuildPath(Node, Path, RTL_NUMBER_OF(Path)))
        ZwSetValueKey(Key, &PathName, 0, REG_SZ, Path, (ULONG)((wcslen(Path) + 1) * sizeof(WCHAR)));
    ZwSetValueKey(Key, &NodeName, 0, REG_DWORD, &Node, sizeof(Node));
    ZwClose(Key);
}
