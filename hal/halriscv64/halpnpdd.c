/*
 * PROJECT:     LiberNT RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V firmware PCI root bus and device-tree PnP bridge
 *
 * The HAL has already validated the QEMU virt ECAM and bus range in its FDT.
 * Report the host as a PnP child so the ordinary pci.sys driver can enumerate
 * devices. This is a platform bus, not a replacement PCI enumerator.
 */

#include <ntifs.h>
#include <ndk/iofuncs.h>
#include <initguid.h>
#include <wdmguid.h>
#include <reactos/riscv64/fdtlib.h>
#include "halp.h"

#define RISCV_PLATFORM_FDO 0x52504644UL
#define RISCV_PLATFORM_PDO 0x52505044UL
#define RISCV_PLATFORM_FDT 0x52504654UL
#define RISCV_PLATFORM_TAG 'bPvR'
#define RISCV_PLATFORM_MAX_CHILDREN 64
#define RISCV_PLATFORM_MAX_MEMORY 4
#define RISCV_PLATFORM_MAX_INTERRUPTS 4
#define RISCV_PLATFORM_MAX_NAME 64
#define RISCV_PLATFORM_MAX_DEPTH 6
#define RISCV_PLATFORM_KEY_DEPTH 2

typedef struct _RISCV_PLATFORM_EXTENSION
{
    ULONG Type;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT ChildDevice;
    ULONG FirstBus;
    ULONG LastBus;
    ULONG Node;
    ULONG Parent;
    RISCV_DMA_TOPOLOGY Dma;
    ULONG ChildCount;
    PDEVICE_OBJECT Children[RISCV_PLATFORM_MAX_CHILDREN];
} RISCV_PLATFORM_EXTENSION;

typedef struct _RISCV_FDT_DEVICE
{
    ULONG MemoryCount;
    ULONG64 MemoryBase[RISCV_PLATFORM_MAX_MEMORY];
    ULONG64 MemorySize[RISCV_PLATFORM_MAX_MEMORY];
    ULONG InterruptCount;
    ULONG Interrupt[RISCV_PLATFORM_MAX_INTERRUPTS];
} RISCV_FDT_DEVICE;

static RISCV_FDT HalpRiscvPlatformFdt;
static ULONG HalpRiscvPlatformSoc = RISCV_FDT_NO_NODE;
static BOOLEAN HalpRiscvPlatformFdtOpen;
static PDRIVER_OBJECT HalpRiscvPlatformDriver;
static ULONG64 HalpRiscvDebuggerBase;

static NTSTATUS
HalpRiscvCopyId(_In_reads_bytes_(Size) const WCHAR *Source,
               _In_ SIZE_T Size,
               _Out_ PULONG_PTR Information)
{
    PWCHAR Copy = ExAllocatePoolWithTag(PagedPool, Size, RISCV_PLATFORM_TAG);

    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Copy, Source, Size);
    *Information = (ULONG_PTR)Copy;
    return STATUS_SUCCESS;
}

static BOOLEAN
HalpRiscvFdtHas(_In_ ULONG Node, _In_z_ const CHAR *Name)
{
    ULONG Length;

    return RiscvFdtGetProperty(&HalpRiscvPlatformFdt, Node, Name, &Length) != NULL;
}

static BOOLEAN
HalpRiscvFdtEnabled(_In_ ULONG Node)
{
    ULONG Length;
    const VOID *Property = RiscvFdtGetProperty(&HalpRiscvPlatformFdt, Node, "status", &Length);

    return !Property || RiscvFdtStringListContains(Property, Length, "okay") ||
           RiscvFdtStringListContains(Property, Length, "ok");
}

static BOOLEAN
HalpRiscvFdtIsSimpleBus(_In_ ULONG Node)
{
    ULONG Length;
    const VOID *Property = RiscvFdtGetProperty(&HalpRiscvPlatformFdt, Node, "compatible", &Length);

    if (!Property || !RiscvFdtStringListContains(Property, Length, "simple-bus") ||
        !HalpRiscvFdtEnabled(Node))
        return FALSE;
    Property = RiscvFdtGetProperty(&HalpRiscvPlatformFdt, Node, "ranges", &Length);
    return Property && !Length;
}

static VOID
HalpRiscvFdtDmaTopology(_In_reads_(Depth) const ULONG *Path, _In_ ULONG Depth,
                        _Out_ RISCV_DMA_TOPOLOGY *Topology)
{
    const RISCV_FDT *Fdt = &HalpRiscvPlatformFdt;
    ULONG Index;

    RtlZeroMemory(Topology, sizeof(*Topology));
    Topology->Coherent = TRUE;
    for (Index = Depth; Index-- > 0;)
    {
        if (HalpRiscvFdtHas(Path[Index], "dma-noncoherent"))
        {
            Topology->Coherent = FALSE;
            break;
        }
        if (HalpRiscvFdtHas(Path[Index], "dma-coherent"))
            break;
    }

    for (Index = Depth - 1; Index-- > 1;)
    {
        ULONG Length, ChildCells, SizeCells, ParentCells, Unused, Stride, Entry;
        const VOID *Property = RiscvFdtGetProperty(Fdt, Path[Index], "dma-ranges", &Length);

        if (!Property || !Length)
            continue;
        RiscvFdtGetCellCounts(Fdt, Path[Index], &ChildCells, &SizeCells);
        RiscvFdtGetCellCounts(Fdt, Path[Index - 1], &ParentCells, &Unused);
        Stride = ChildCells + ParentCells + SizeCells;
        for (Entry = 0; Entry < Length / (Stride * sizeof(ULONG)) &&
                        Topology->WindowCount < RISCV_DMA_MAX_WINDOWS; ++Entry)
        {
            ULONG64 Bus, Cpu, Size;

            if (!RiscvFdtReadCells(Property, Length, Entry * Stride, ChildCells, &Bus) ||
                !RiscvFdtReadCells(Property, Length, Entry * Stride + ChildCells, ParentCells, &Cpu) ||
                !RiscvFdtReadCells(Property, Length, Entry * Stride + ChildCells + ParentCells,
                                   SizeCells, &Size) || !Size)
                continue;
            Topology->BusBase[Topology->WindowCount] = Bus;
            Topology->CpuBase[Topology->WindowCount] = Cpu;
            Topology->Size[Topology->WindowCount++] = Size;
        }
        break;
    }
}

static BOOLEAN
HalpRiscvFdtDescribe(_In_ ULONG Node, _In_ ULONG Bus, _Out_ RISCV_FDT_DEVICE *Device)
{
    const RISCV_FDT *Fdt = &HalpRiscvPlatformFdt;
    const VOID *Property;
    ULONG Length, Index, Parent;
    ULONG64 Address, Size;

    RtlZeroMemory(Device, sizeof(*Device));
    Property = RiscvFdtGetProperty(Fdt, Node, "status", &Length);
    if (Property && !RiscvFdtStringListContains(Property, Length, "okay") &&
        !RiscvFdtStringListContains(Property, Length, "ok"))
        return FALSE;
    Property = RiscvFdtGetProperty(Fdt, Node, "compatible", &Length);
    if (!Property || Length < 2 || ((const CHAR *)Property)[Length - 1] != '\0' ||
        RiscvFdtStringListContains(Property, Length, "pci-host-ecam-generic") ||
        RiscvFdtStringListContains(Property, Length, "google,goldfish-rtc") ||
        RiscvFdtGetProperty(Fdt, Node, "interrupt-controller", &Length))
        return FALSE;

    for (Index = 0; Index < RISCV_PLATFORM_MAX_MEMORY; ++Index)
    {
        if (!RiscvFdtReadReg(Fdt, Node, Bus, Index, &Address, &Size))
            break;
        if (!Size || Size > MAXULONG || Address + Size < Address)
            continue;
        Device->MemoryBase[Device->MemoryCount] = Address;
        Device->MemorySize[Device->MemoryCount++] = Size;
    }

    Property = RiscvFdtGetProperty(Fdt, Node, "interrupts-extended", &Length);
    if (Property)
    {
        if (Length % (2 * sizeof(ULONG)))
            return FALSE;
        for (Index = 0; Index < Length / (2 * sizeof(ULONG)); ++Index)
        {
            ULONG Phandle = RiscvFdtReadBigEndian32((const UCHAR *)Property + Index * 2 * sizeof(ULONG));
            ULONG Source = RiscvFdtReadBigEndian32((const UCHAR *)Property + (Index * 2 + 1) * sizeof(ULONG));

            if (!HalpRiscvPlicHasSource(Phandle, Source))
                return FALSE;
            if (Device->InterruptCount < RISCV_PLATFORM_MAX_INTERRUPTS)
                Device->Interrupt[Device->InterruptCount++] = Source;
        }
        return Device->MemoryCount || Device->InterruptCount ||
               RiscvFdtFirstChild(Fdt, Node) != RISCV_FDT_NO_NODE;
    }

    Property = RiscvFdtGetProperty(Fdt, Node, "interrupts", &Length);
    if (Property && !(Length % sizeof(ULONG)) &&
        (RiscvFdtReadU32(Fdt, Node, "interrupt-parent", &Parent) ||
         RiscvFdtReadU32(Fdt, Bus, "interrupt-parent", &Parent) ||
         RiscvFdtReadU32(Fdt, HalpRiscvPlatformSoc, "interrupt-parent", &Parent) ||
         RiscvFdtReadU32(Fdt, RiscvFdtRootNode(Fdt), "interrupt-parent", &Parent)))
    {
        for (Index = 0; Index < Length / sizeof(ULONG); ++Index)
        {
            ULONG Source = RiscvFdtReadBigEndian32((const UCHAR *)Property + Index * sizeof(ULONG));

            if (HalpRiscvPlicHasSource(Parent, Source) &&
                Device->InterruptCount < RISCV_PLATFORM_MAX_INTERRUPTS)
                Device->Interrupt[Device->InterruptCount++] = Source;
        }
    }
    return Device->MemoryCount || Device->InterruptCount ||
           RiscvFdtFirstChild(Fdt, Node) != RISCV_FDT_NO_NODE;
}

static NTSTATUS
HalpRiscvFdtIds(_In_ ULONG Node, _In_ BOOLEAN All, _Out_ PULONG_PTR Information)
{
    static const WCHAR Prefix[] = L"FDT\\";
    const CHAR *List, *Entry;
    ULONG Length, Cursor = 0, EntryLength, Characters = 1, Index;
    PWCHAR Ids, Out;

    List = RiscvFdtGetProperty(&HalpRiscvPlatformFdt, Node, "compatible", &Length);
    while ((Entry = RiscvFdtNextString(List, Length, &Cursor, &EntryLength)) != NULL)
    {
        Characters += RTL_NUMBER_OF(Prefix) + EntryLength;
        if (!All)
            break;
    }
    if (Characters == 1)
        return STATUS_NOT_SUPPORTED;

    Ids = ExAllocatePoolWithTag(PagedPool, Characters * sizeof(WCHAR), RISCV_PLATFORM_TAG);
    if (!Ids)
        return STATUS_INSUFFICIENT_RESOURCES;
    Out = Ids;
    Cursor = 0;
    while ((Entry = RiscvFdtNextString(List, Length, &Cursor, &EntryLength)) != NULL)
    {
        RtlCopyMemory(Out, Prefix, sizeof(Prefix) - sizeof(WCHAR));
        Out += RTL_NUMBER_OF(Prefix) - 1;
        for (Index = 0; Index < EntryLength; ++Index)
        {
            CHAR Character = Entry[Index];

            *Out++ = (Character <= ' ' || Character > '~' || Character == ',' || Character == '\\')
                         ? L'_' : (WCHAR)Character;
        }
        *Out++ = UNICODE_NULL;
        if (!All)
            break;
    }
    *Out = UNICODE_NULL;
    *Information = (ULONG_PTR)Ids;
    return STATUS_SUCCESS;
}

static NTSTATUS
HalpRiscvFdtInstanceId(_In_ ULONG Node, _In_ ULONG Bus, _Out_ PULONG_PTR Information)
{
    static const WCHAR Digits[] = L"0123456789abcdef";
    RISCV_FDT_DEVICE Device;
    PWCHAR Id;
    ULONG Index;

    if (!HalpRiscvFdtDescribe(Node, Bus, &Device))
        return STATUS_NO_SUCH_DEVICE;
    if (!Device.MemoryCount)
    {
        ULONG NameLength;
        const CHAR *Name = RiscvFdtNodeName(&HalpRiscvPlatformFdt, Node, &NameLength);

        if (!Name || !NameLength || NameLength >= RISCV_PLATFORM_MAX_NAME)
            return STATUS_NO_SUCH_DEVICE;
        Id = ExAllocatePoolWithTag(PagedPool, (NameLength + 1) * sizeof(WCHAR), RISCV_PLATFORM_TAG);
        if (!Id)
            return STATUS_INSUFFICIENT_RESOURCES;
        for (Index = 0; Index < NameLength; ++Index)
            Id[Index] = (Name[Index] <= ' ' || Name[Index] > '~' || Name[Index] == ',' || Name[Index] == '\\')
                            ? L'_' : (WCHAR)Name[Index];
        Id[NameLength] = UNICODE_NULL;
        *Information = (ULONG_PTR)Id;
        return STATUS_SUCCESS;
    }
    Id = ExAllocatePoolWithTag(PagedPool, 17 * sizeof(WCHAR), RISCV_PLATFORM_TAG);
    if (!Id)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Index = 0; Index < 16; ++Index)
        Id[Index] = Digits[(Device.MemoryBase[0] >> (60 - 4 * Index)) & 15];
    Id[16] = UNICODE_NULL;
    *Information = (ULONG_PTR)Id;
    return STATUS_SUCCESS;
}

static NTSTATUS
HalpRiscvFdtResources(_In_ ULONG Node, _In_ ULONG Bus, _Out_ PULONG_PTR Information)
{
    RISCV_FDT_DEVICE Device;
    PCM_RESOURCE_LIST List;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;
    ULONG Count, Index;
    SIZE_T Size;

    if (!HalpRiscvFdtDescribe(Node, Bus, &Device))
        return STATUS_NO_SUCH_DEVICE;
    Count = Device.MemoryCount + Device.InterruptCount;
    Size = FIELD_OFFSET(CM_RESOURCE_LIST, List[0].PartialResourceList.PartialDescriptors) +
           Count * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    List = ExAllocatePoolWithTag(PagedPool, Size, RISCV_PLATFORM_TAG);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, Size);
    List->Count = 1;
    List->List[0].InterfaceType = Internal;
    List->List[0].BusNumber = 0;
    List->List[0].PartialResourceList.Version = 1;
    List->List[0].PartialResourceList.Revision = 1;
    List->List[0].PartialResourceList.Count = Count;
    Resource = List->List[0].PartialResourceList.PartialDescriptors;
    for (Index = 0; Index < Device.MemoryCount; ++Index, ++Resource)
    {
        Resource->Type = CmResourceTypeMemory;
        Resource->ShareDisposition = CmResourceShareShared;
        Resource->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
        Resource->u.Memory.Start.QuadPart = Device.MemoryBase[Index];
        Resource->u.Memory.Length = (ULONG)Device.MemorySize[Index];
    }
    for (Index = 0; Index < Device.InterruptCount; ++Index, ++Resource)
    {
        Resource->Type = CmResourceTypeInterrupt;
        Resource->ShareDisposition = CmResourceShareShared;
        Resource->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        Resource->u.Interrupt.Level = Device.Interrupt[Index];
        Resource->u.Interrupt.Vector = Device.Interrupt[Index];
        Resource->u.Interrupt.Affinity = 1;
    }
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static NTSTATUS
HalpRiscvFdtRequirements(_In_ ULONG Node, _In_ ULONG Bus, _Out_ PULONG_PTR Information)
{
    RISCV_FDT_DEVICE Device;
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PIO_RESOURCE_DESCRIPTOR Resource;
    ULONG Count, Index;
    SIZE_T Size;

    if (!HalpRiscvFdtDescribe(Node, Bus, &Device))
        return STATUS_NO_SUCH_DEVICE;
    Count = Device.MemoryCount + Device.InterruptCount;
    Size = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) +
           Count * sizeof(IO_RESOURCE_DESCRIPTOR);
    List = ExAllocatePoolWithTag(PagedPool, Size, RISCV_PLATFORM_TAG);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, Size);
    List->ListSize = (ULONG)Size;
    List->InterfaceType = Internal;
    List->BusNumber = 0;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = Count;
    Resource = List->List[0].Descriptors;
    for (Index = 0; Index < Device.MemoryCount; ++Index, ++Resource)
    {
        Resource->Type = CmResourceTypeMemory;
        Resource->ShareDisposition = CmResourceShareShared;
        Resource->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
        Resource->u.Memory.Length = (ULONG)Device.MemorySize[Index];
        Resource->u.Memory.Alignment = 1;
        Resource->u.Memory.MinimumAddress.QuadPart = Device.MemoryBase[Index];
        Resource->u.Memory.MaximumAddress.QuadPart = Device.MemoryBase[Index] + Device.MemorySize[Index] - 1;
    }
    for (Index = 0; Index < Device.InterruptCount; ++Index, ++Resource)
    {
        Resource->Type = CmResourceTypeInterrupt;
        Resource->ShareDisposition = CmResourceShareShared;
        Resource->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        Resource->u.Interrupt.MinimumVector = Device.Interrupt[Index];
        Resource->u.Interrupt.MaximumVector = Device.Interrupt[Index];
    }
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static ULONG
HalpRiscvFdtFindPhandle(_In_ ULONG Phandle)
{
    const RISCV_FDT *Fdt = &HalpRiscvPlatformFdt;
    ULONG Offset = RiscvFdtRootNode(Fdt), Value;

    if (!Phandle || Phandle == MAXULONG)
        return RISCV_FDT_NO_NODE;
    while (Offset != RISCV_FDT_NO_NODE)
    {
        if (RiscvFdtToken(Fdt, Offset) == RISCV_FDT_BEGIN_NODE &&
            RiscvFdtReadU32(Fdt, Offset, "phandle", &Value) && Value == Phandle)
        {
            return Offset;
        }
        Offset = RiscvFdtSkipToken(Fdt, Offset);
    }
    return RISCV_FDT_NO_NODE;
}

static VOID
HalpRiscvFdtWriteNode(_In_ HANDLE Key, _In_ ULONG Node, _In_ ULONG Depth);

static VOID
HalpRiscvFdtWriteSupply(_In_ HANDLE Key, _In_ PUNICODE_STRING Name, _In_ ULONG Phandle)
{
    ULONG Node = HalpRiscvFdtFindPhandle(Phandle);
    OBJECT_ATTRIBUTES Attributes;
    HANDLE SupplyKey;

    if (Node == RISCV_FDT_NO_NODE)
        return;
    InitializeObjectAttributes(&Attributes, Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Key, NULL);
    if (!NT_SUCCESS(ZwCreateKey(&SupplyKey, KEY_SET_VALUE | KEY_CREATE_SUB_KEY, &Attributes, 0, NULL,
                                REG_OPTION_VOLATILE, NULL)))
        return;
    HalpRiscvFdtWriteNode(SupplyKey, Node, 0);
    ZwClose(SupplyKey);
}

static VOID
HalpRiscvFdtWriteNode(_In_ HANDLE Key, _In_ ULONG Node, _In_ ULONG Depth)
{
    static const CHAR Supply[] = "-supply";
    const RISCV_FDT *Fdt = &HalpRiscvPlatformFdt;
    ULONG Offset = RiscvFdtSkipToken(Fdt, Node), Child;

    while (Offset != RISCV_FDT_NO_NODE)
    {
        ULONG Token = RiscvFdtToken(Fdt, Offset);

        if (Token == RISCV_FDT_PROPERTY)
        {
            const UCHAR *Header = Fdt->Blob + Fdt->StructureOffset + Offset + sizeof(ULONG);
            WCHAR Name[RISCV_PLATFORM_MAX_NAME];
            UNICODE_STRING ValueName;
            ULONG ValueLength, NameOffset, Index;
            const CHAR *Source;

            if (Offset > Fdt->StructureSize - 3 * sizeof(ULONG))
                break;
            ValueLength = RiscvFdtReadBigEndian32(Header);
            NameOffset = RiscvFdtReadBigEndian32(Header + sizeof(ULONG));
            if (NameOffset >= Fdt->StringsSize ||
                ValueLength > Fdt->StructureSize - (Offset + 3 * sizeof(ULONG)))
                break;
            Source = (const CHAR *)(Fdt->Blob + Fdt->StringsOffset + NameOffset);
            for (Index = 0; Index < RISCV_PLATFORM_MAX_NAME - 1 &&
                            Index < Fdt->StringsSize - NameOffset && Source[Index]; ++Index)
                Name[Index] = (WCHAR)(UCHAR)Source[Index];
            Name[Index] = UNICODE_NULL;
            if (Index && Index < Fdt->StringsSize - NameOffset && !Source[Index])
            {
                RtlInitUnicodeString(&ValueName, Name);
                ZwSetValueKey(Key, &ValueName, 0, REG_BINARY, (PVOID)(Header + 2 * sizeof(ULONG)), ValueLength);
                if (Depth && ValueLength == sizeof(ULONG) && Index > sizeof(Supply) - 1 &&
                    RtlEqualMemory(Source + Index - (sizeof(Supply) - 1), Supply, sizeof(Supply) - 1))
                {
                    HalpRiscvFdtWriteSupply(Key, &ValueName, RiscvFdtReadBigEndian32(Header + 2 * sizeof(ULONG)));
                }
            }
        }
        else if (Token != RISCV_FDT_NOP)
        {
            break;
        }
        Offset = RiscvFdtSkipToken(Fdt, Offset);
    }

    if (!Depth)
        return;
    for (Child = RiscvFdtFirstChild(Fdt, Node); Child != RISCV_FDT_NO_NODE;
         Child = RiscvFdtNextSibling(Fdt, Child))
    {
        WCHAR Name[RISCV_PLATFORM_MAX_NAME];
        OBJECT_ATTRIBUTES Attributes;
        UNICODE_STRING KeyName;
        ULONG NameLength, Index;
        const CHAR *Source = RiscvFdtNodeName(Fdt, Child, &NameLength);
        HANDLE ChildKey;

        if (!Source || !NameLength || NameLength >= RISCV_PLATFORM_MAX_NAME)
            continue;
        for (Index = 0; Index < NameLength; ++Index)
            Name[Index] = (Source[Index] == '\\' || Source[Index] <= ' ') ? L'_' : (WCHAR)(UCHAR)Source[Index];
        Name[NameLength] = UNICODE_NULL;
        RtlInitUnicodeString(&KeyName, Name);
        InitializeObjectAttributes(&Attributes, &KeyName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Key, NULL);
        if (!NT_SUCCESS(ZwCreateKey(&ChildKey, KEY_SET_VALUE | KEY_CREATE_SUB_KEY, &Attributes, 0, NULL,
                                    REG_OPTION_VOLATILE, NULL)))
            continue;
        HalpRiscvFdtWriteNode(ChildKey, Child, Depth - 1);
        ZwClose(ChildKey);
    }
}

static VOID
HalpRiscvFdtPublishProperties(_In_ PDEVICE_OBJECT DeviceObject, _In_ ULONG Node)
{
    HANDLE Key;

    if (!NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceObject, PLUGPLAY_REGKEY_DEVICE,
                                            KEY_SET_VALUE | KEY_CREATE_SUB_KEY, &Key)))
        return;
    HalpRiscvFdtWriteNode(Key, Node, RISCV_PLATFORM_KEY_DEPTH);
    ZwClose(Key);
}

static VOID NTAPI
HalpRiscvFdtInterfaceReference(_In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
}

static BOOLEAN NTAPI
HalpRiscvFdtTranslateBusAddress(_In_ PVOID Context, _In_ PHYSICAL_ADDRESS BusAddress,
                                _In_ ULONG Length, _Inout_ PULONG AddressSpace,
                                _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(AddressSpace);
    *TranslatedAddress = BusAddress;
    return TRUE;
}

static PDMA_ADAPTER NTAPI
HalpRiscvFdtGetDmaAdapter(_In_ PVOID Context, _In_ PDEVICE_DESCRIPTION Description,
                          _Out_ PULONG NumberOfMapRegisters)
{
    PDEVICE_OBJECT DeviceObject = Context;
    RISCV_PLATFORM_EXTENSION *Extension = DeviceObject->DeviceExtension;

    return HalpRiscvCreateDmaAdapter(Description, &Extension->Dma, NumberOfMapRegisters);
}

static ULONG NTAPI
HalpRiscvFdtBusData(_In_ PVOID Context, _In_ ULONG DataType, _In_ PVOID Buffer,
                    _In_ ULONG Offset, _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(DataType);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Length);
    return 0;
}

BOOLEAN
HalpRiscvGetPlatformDmaTopology(_In_opt_ PDEVICE_OBJECT PhysicalDeviceObject,
                                _Out_ RISCV_DMA_TOPOLOGY *Topology)
{
    ULONG Path[3], Parent;

    if (PhysicalDeviceObject && PhysicalDeviceObject->DriverObject == HalpRiscvPlatformDriver &&
        ((RISCV_PLATFORM_EXTENSION *)PhysicalDeviceObject->DeviceExtension)->Type == RISCV_PLATFORM_FDT)
    {
        *Topology = ((RISCV_PLATFORM_EXTENSION *)PhysicalDeviceObject->DeviceExtension)->Dma;
        return TRUE;
    }
    if (!HalpRiscvPlatformFdtOpen)
    {
        if (!HalpRiscvDeviceTree ||
            !RiscvFdtOpen(HalpRiscvDeviceTree, HalpRiscvDeviceTreeSize, &HalpRiscvPlatformFdt))
            return FALSE;
        if (!RiscvFdtFindNode(&HalpRiscvPlatformFdt, "/soc", &HalpRiscvPlatformSoc, &Parent) ||
            Parent != RiscvFdtRootNode(&HalpRiscvPlatformFdt))
            HalpRiscvPlatformSoc = RISCV_FDT_NO_NODE;
        HalpRiscvPlatformFdtOpen = TRUE;
    }
    Path[0] = RiscvFdtRootNode(&HalpRiscvPlatformFdt);
    if (HalpRiscvPlatformSoc == RISCV_FDT_NO_NODE)
    {
        Path[1] = Path[0];
        HalpRiscvFdtDmaTopology(Path, 2, Topology);
    }
    else
    {
        Path[1] = Path[2] = HalpRiscvPlatformSoc;
        HalpRiscvFdtDmaTopology(Path, 3, Topology);
    }
    return TRUE;
}

static NTSTATUS
HalpRiscvFdtPnp(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIO_STACK_LOCATION Stack, _Inout_ PIRP Irp)
{
    RISCV_PLATFORM_EXTENSION *Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status = Irp->IoStatus.Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                    Status = HalpRiscvFdtIds(Extension->Node, FALSE, &Irp->IoStatus.Information);
                    break;
                case BusQueryHardwareIDs:
                    Status = HalpRiscvFdtIds(Extension->Node, TRUE, &Irp->IoStatus.Information);
                    break;
                case BusQueryInstanceID:
                    Status = HalpRiscvFdtInstanceId(Extension->Node, Extension->Parent, &Irp->IoStatus.Information);
                    break;
                default:
                    break;
            }
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = HalpRiscvFdtResources(Extension->Node, Extension->Parent, &Irp->IoStatus.Information);
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = HalpRiscvFdtRequirements(Extension->Node, Extension->Parent, &Irp->IoStatus.Information);
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
        {
            PPNP_BUS_INFORMATION BusInformation;

            BusInformation = ExAllocatePoolWithTag(PagedPool, sizeof(*BusInformation), RISCV_PLATFORM_TAG);
            if (!BusInformation)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            BusInformation->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
            BusInformation->LegacyBusType = Internal;
            BusInformation->BusNumber = 0;
            Irp->IoStatus.Information = (ULONG_PTR)BusInformation;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;

            if (!Capabilities || Capabilities->Version != 1 ||
                Capabilities->Size < sizeof(*Capabilities))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Capabilities->UniqueID = TRUE;
            Capabilities->SilentInstall = TRUE;
            Capabilities->SurpriseRemovalOK = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_INTERFACE:
            if (IsEqualGUID(Stack->Parameters.QueryInterface.InterfaceType, &GUID_BUS_INTERFACE_STANDARD) &&
                Stack->Parameters.QueryInterface.Interface &&
                Stack->Parameters.QueryInterface.Size >= sizeof(BUS_INTERFACE_STANDARD))
            {
                PBUS_INTERFACE_STANDARD Interface =
                    (PBUS_INTERFACE_STANDARD)Stack->Parameters.QueryInterface.Interface;

                Interface->Size = sizeof(*Interface);
                Interface->Version = 1;
                Interface->Context = DeviceObject;
                Interface->InterfaceReference = HalpRiscvFdtInterfaceReference;
                Interface->InterfaceDereference = HalpRiscvFdtInterfaceReference;
                Interface->TranslateBusAddress = HalpRiscvFdtTranslateBusAddress;
                Interface->GetDmaAdapter = HalpRiscvFdtGetDmaAdapter;
                Interface->SetBusData = HalpRiscvFdtBusData;
                Interface->GetBusData = HalpRiscvFdtBusData;
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_START_DEVICE:
            HalpRiscvFdtPublishProperties(DeviceObject, Extension->Node);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }
    return Status;
}

static NTSTATUS
HalpRiscvPciResources(_In_ RISCV_PLATFORM_EXTENSION *Extension,
                     _Out_ PULONG_PTR Information)
{
    PCM_RESOURCE_LIST List = ExAllocatePoolWithTag(PagedPool, sizeof(*List), RISCV_PLATFORM_TAG);
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;

    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, sizeof(*List));
    List->Count = 1;
    List->List[0].InterfaceType = PCIBus;
    List->List[0].BusNumber = Extension->FirstBus;
    List->List[0].PartialResourceList.Version = 1;
    List->List[0].PartialResourceList.Revision = 1;
    List->List[0].PartialResourceList.Count = 1;
    Resource = &List->List[0].PartialResourceList.PartialDescriptors[0];
    Resource->Type = CmResourceTypeBusNumber;
    Resource->ShareDisposition = CmResourceShareDeviceExclusive;
    Resource->u.BusNumber.Start = Extension->FirstBus;
    Resource->u.BusNumber.Length = Extension->LastBus - Extension->FirstBus + 1;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static NTSTATUS
HalpRiscvPciRequirements(_In_ RISCV_PLATFORM_EXTENSION *Extension,
                        _Out_ PULONG_PTR Information)
{
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PIO_RESOURCE_DESCRIPTOR Resource;

    List = ExAllocatePoolWithTag(PagedPool, sizeof(*List), RISCV_PLATFORM_TAG);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, sizeof(*List));
    List->ListSize = sizeof(*List);
    List->InterfaceType = PCIBus;
    List->BusNumber = Extension->FirstBus;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = 1;
    Resource = &List->List[0].Descriptors[0];
    Resource->Type = CmResourceTypeBusNumber;
    Resource->ShareDisposition = CmResourceShareDeviceExclusive;
    Resource->u.BusNumber.Length = Extension->LastBus - Extension->FirstBus + 1;
    Resource->u.BusNumber.MinBusNumber = Extension->FirstBus;
    Resource->u.BusNumber.MaxBusNumber = Extension->LastBus;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
HalpRiscvPlatformPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    RISCV_PLATFORM_EXTENSION *Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;

    if (Extension->Type == RISCV_PLATFORM_FDO)
    {
        if (Stack->MinorFunction == IRP_MN_QUERY_DEVICE_RELATIONS &&
            Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
        {
            PDEVICE_RELATIONS Relations;

            ULONG Index;

            Relations = ExAllocatePoolWithTag(PagedPool,
                                              FIELD_OFFSET(DEVICE_RELATIONS, Objects) +
                                                  max(Extension->ChildCount, 1) * sizeof(PDEVICE_OBJECT),
                                              RISCV_PLATFORM_TAG);
            if (!Relations)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Complete;
            }
            Relations->Count = Extension->ChildCount;
            for (Index = 0; Index < Extension->ChildCount; ++Index)
            {
                Relations->Objects[Index] = Extension->Children[Index];
                ObReferenceObject(Extension->Children[Index]);
            }
            Irp->IoStatus.Information = (ULONG_PTR)Relations;
            Status = STATUS_SUCCESS;
            goto Complete;
        }
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Extension->LowerDevice, Irp);
    }

    if (Extension->Type == RISCV_PLATFORM_FDT)
    {
        Status = HalpRiscvFdtPnp(DeviceObject, Stack, Irp);
        goto Complete;
    }

    if (Extension->Type != RISCV_PLATFORM_PDO)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Complete;
    }

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                {
                    static const WCHAR DeviceId[] = L"ACPI\\PNP0A03";
                    Status = HalpRiscvCopyId(DeviceId, sizeof(DeviceId), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryHardwareIDs:
                case BusQueryCompatibleIDs:
                {
                    static const WCHAR HardwareIds[] = L"*PNP0A03\0";
                    Status = HalpRiscvCopyId(HardwareIds, sizeof(HardwareIds), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryInstanceID:
                {
                    static const WCHAR InstanceId[] = L"0";
                    Status = HalpRiscvCopyId(InstanceId, sizeof(InstanceId), &Irp->IoStatus.Information);
                    break;
                }
                default:
                    break;
            }
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = HalpRiscvPciResources(Extension, &Irp->IoStatus.Information);
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = HalpRiscvPciRequirements(Extension, &Irp->IoStatus.Information);
            break;

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;

            if (!Capabilities || Capabilities->Version != 1 ||
                Capabilities->Size < sizeof(*Capabilities))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Capabilities->UniqueID = TRUE;
            Capabilities->Address = 0;
            Capabilities->UINumber = 0;
            Capabilities->SilentInstall = TRUE;
            Capabilities->SurpriseRemovalOK = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_START_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }

Complete:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS NTAPI
HalpRiscvPlatformPower(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    RISCV_PLATFORM_EXTENSION *Extension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    if (Extension->Type == RISCV_PLATFORM_FDO)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(Extension->LowerDevice, Irp);
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static VOID
HalpRiscvQueryDebuggerDevice(VOID)
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[3];
    ULONG Low = 0, High = 0;

    RtlZeroMemory(QueryTable, sizeof(QueryTable));
    QueryTable[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED;
    QueryTable[0].Name = L"BaseLow";
    QueryTable[0].EntryContext = &Low;
    QueryTable[1].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_REQUIRED;
    QueryTable[1].Name = L"BaseHigh";
    QueryTable[1].EntryContext = &High;
    if (NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_SERVICES, L"FDT\\Debug\\0",
                                          QueryTable, NULL, NULL)))
        HalpRiscvDebuggerBase = ((ULONG64)High << 32) | Low;
}

static VOID
HalpRiscvFdtEnumerate(_In_ PDRIVER_OBJECT DriverObject, _Inout_ RISCV_PLATFORM_EXTENSION *Extension,
                      _Inout_updates_(RISCV_PLATFORM_MAX_DEPTH) PULONG Path, _In_ ULONG Depth)
{
    RISCV_PLATFORM_EXTENSION *ChildExtension;
    RISCV_FDT_DEVICE Device;
    PDEVICE_OBJECT Child;
    ULONG Bus = Path[Depth - 1], Node;

    for (Node = RiscvFdtFirstChild(&HalpRiscvPlatformFdt, Bus);
         Node != RISCV_FDT_NO_NODE && Extension->ChildCount < RISCV_PLATFORM_MAX_CHILDREN;
         Node = RiscvFdtNextSibling(&HalpRiscvPlatformFdt, Node))
    {
        Path[Depth] = Node;
        if (HalpRiscvFdtIsSimpleBus(Node))
        {
            if (Depth + 1 < RISCV_PLATFORM_MAX_DEPTH)
                HalpRiscvFdtEnumerate(DriverObject, Extension, Path, Depth + 1);
            continue;
        }
        if (!HalpRiscvFdtDescribe(Node, Bus, &Device))
            continue;
        if (HalpRiscvDebuggerBase && Device.MemoryCount &&
            Device.MemoryBase[0] == HalpRiscvDebuggerBase)
            continue;
        if (!NT_SUCCESS(IoCreateDevice(DriverObject, sizeof(*Extension), NULL,
                                       FILE_DEVICE_CONTROLLER, FILE_AUTOGENERATED_DEVICE_NAME,
                                       FALSE, &Child)))
            break;
        ChildExtension = Child->DeviceExtension;
        RtlZeroMemory(ChildExtension, sizeof(*ChildExtension));
        ChildExtension->Type = RISCV_PLATFORM_FDT;
        ChildExtension->Node = Node;
        ChildExtension->Parent = Bus;
        HalpRiscvFdtDmaTopology(Path, Depth + 1, &ChildExtension->Dma);
        Extension->Children[Extension->ChildCount++] = Child;
        Child->Flags &= ~DO_DEVICE_INITIALIZING;
    }
}

static NTSTATUS NTAPI
HalpRiscvPlatformAddDevice(_In_ PDRIVER_OBJECT DriverObject,
                          _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    RISCV_PLATFORM_EXTENSION *Extension, *ChildExtension;
    PDEVICE_OBJECT Fdo, Child;
    NTSTATUS Status;
    ULONG FirstBus = 0, LastBus = 0;
    BOOLEAN HasPci = HalpRiscvGetPciBusRange(&FirstBus, &LastBus);

    if (!HasPci && HalpRiscvPlatformSoc == RISCV_FDT_NO_NODE)
        return STATUS_NO_SUCH_DEVICE;

    Status = IoCreateDevice(DriverObject, sizeof(*Extension), NULL,
                            FILE_DEVICE_BUS_EXTENDER, 0, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Type = RISCV_PLATFORM_FDO;
    Extension->FirstBus = FirstBus;
    Extension->LastBus = LastBus;
    Extension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    if (HasPci)
    {
        Status = IoCreateDevice(DriverObject, sizeof(*Extension), NULL,
                                FILE_DEVICE_BUS_EXTENDER, FILE_AUTOGENERATED_DEVICE_NAME,
                                FALSE, &Child);
        if (!NT_SUCCESS(Status))
        {
            IoDetachDevice(Extension->LowerDevice);
            IoDeleteDevice(Fdo);
            return Status;
        }
        Extension->ChildDevice = Child;
        Extension->Children[Extension->ChildCount++] = Child;
        ChildExtension = Child->DeviceExtension;
        RtlZeroMemory(ChildExtension, sizeof(*ChildExtension));
        ChildExtension->Type = RISCV_PLATFORM_PDO;
        ChildExtension->FirstBus = FirstBus;
        ChildExtension->LastBus = LastBus;
        Child->Flags &= ~DO_DEVICE_INITIALIZING;
    }

    if (HalpRiscvPlatformSoc != RISCV_FDT_NO_NODE)
    {
        ULONG Path[RISCV_PLATFORM_MAX_DEPTH];

        Path[0] = RiscvFdtRootNode(&HalpRiscvPlatformFdt);
        Path[1] = HalpRiscvPlatformSoc;
        HalpRiscvQueryDebuggerDevice();
        HalpRiscvFdtEnumerate(DriverObject, Extension, Path, 2);
    }
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
HalpRiscvPlatformDriverEntry(_In_ PDRIVER_OBJECT DriverObject,
                            _In_ PUNICODE_STRING RegistryPath)
{
    PDEVICE_OBJECT RootDevice = NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    HalpRiscvPlatformDriver = DriverObject;
    DriverObject->DriverExtension->AddDevice = HalpRiscvPlatformAddDevice;
    DriverObject->MajorFunction[IRP_MJ_PNP] = HalpRiscvPlatformPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = HalpRiscvPlatformPower;

    Status = IoReportDetectedDevice(DriverObject, InterfaceTypeUndefined,
                                    MAXULONG, MAXULONG, NULL, NULL,
                                    FALSE, &RootDevice);
    if (!NT_SUCCESS(Status))
        return Status;
    RootDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    return HalpRiscvPlatformAddDevice(DriverObject, RootDevice);
}

NTSTATUS
NTAPI
HaliInitPnpDriver(VOID)
{
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\RISCV_PLATFORM");
    ULONG FirstBus, LastBus, Parent;

    if (HalpRiscvDeviceTree &&
        RiscvFdtOpen(HalpRiscvDeviceTree, HalpRiscvDeviceTreeSize, &HalpRiscvPlatformFdt))
    {
        if (!RiscvFdtFindNode(&HalpRiscvPlatformFdt, "/soc", &HalpRiscvPlatformSoc, &Parent) ||
            Parent != RiscvFdtRootNode(&HalpRiscvPlatformFdt))
            HalpRiscvPlatformSoc = RISCV_FDT_NO_NODE;
        HalpRiscvPlatformFdtOpen = TRUE;
    }

    if (!HalpRiscvGetPciBusRange(&FirstBus, &LastBus) && HalpRiscvPlatformSoc == RISCV_FDT_NO_NODE)
        return STATUS_SUCCESS;
    return IoCreateDriver(&DriverName, HalpRiscvPlatformDriverEntry);
}
