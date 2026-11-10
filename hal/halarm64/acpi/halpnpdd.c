/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halarm64/acpi/halpnpdd.c
 * PURPOSE:         ARM64 hooks for the ACPI HAL bus driver
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include <halpnp.h>

#include <wdmguid.h>

#include "../halext.h"
#include "../devtree.h"

#ifndef NonPagedPoolNx
#define NonPagedPoolNx NonPagedPool
#endif

#define NDEBUG
#include <debug.h>

typedef struct _HALP_PORT_RANGE_ENTRY
{
    LIST_ENTRY ListEntry;
    USHORT RangeId;
    BOOLEAN IsSparse;
    BOOLEAN PrimaryIsMmio;
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Length;
} HALP_PORT_RANGE_ENTRY, *PHALP_PORT_RANGE_ENTRY;

/* GLOBALS ********************************************************************/

static LIST_ENTRY HalpPortRangeList = { &HalpPortRangeList, &HalpPortRangeList };
static KSPIN_LOCK HalpPortRangeLock;
static USHORT HalpPortRangeNextId = 1;

/*
 * The HAL claims these vectors in the ACPI device's boot configuration so
 * the arbiter never hands them to a device.
 */
#define HALP_RESERVED_VECTOR_COUNT 1280

/* FUNCTIONS ******************************************************************/

static
BOOLEAN
HalpAcpiResolveInterfaceGas(
    _In_ ACPI_REG_TYPE RegisterType,
    _In_ ULONG RegisterIndex,
    _Out_ GEN_ADDR *Gas)
{
    const GEN_ADDR *Source = NULL;
    GEN_ADDR TempGas;
    ULONG Offset = 0;
    ULONG SegmentLength = 0;
    ULONG Stride = 0;

    switch (RegisterType)
    {
        case PM1a_ENABLE:
            if (!HalpPm1EventBlockValid[0] || HalpFixedAcpiDescTable.pm1_evt_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1EventBlocks[0];
            SegmentLength = HalpFixedAcpiDescTable.pm1_evt_len / 2;
            Offset = SegmentLength;
            Stride = sizeof(USHORT);
            break;

        case PM1b_ENABLE:
            if (!HalpPm1EventBlockValid[1] || HalpFixedAcpiDescTable.pm1_evt_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1EventBlocks[1];
            SegmentLength = HalpFixedAcpiDescTable.pm1_evt_len / 2;
            Offset = SegmentLength;
            Stride = sizeof(USHORT);
            break;

        case PM1a_STATUS:
            if (!HalpPm1EventBlockValid[0] || HalpFixedAcpiDescTable.pm1_evt_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1EventBlocks[0];
            SegmentLength = HalpFixedAcpiDescTable.pm1_evt_len / 2;
            Stride = sizeof(USHORT);
            break;

        case PM1b_STATUS:
            if (!HalpPm1EventBlockValid[1] || HalpFixedAcpiDescTable.pm1_evt_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1EventBlocks[1];
            SegmentLength = HalpFixedAcpiDescTable.pm1_evt_len / 2;
            Stride = sizeof(USHORT);
            break;

        case PM1a_CONTROL:
            if (!HalpPm1ControlBlockValid[0] || HalpFixedAcpiDescTable.pm1_ctrl_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1ControlBlocks[0];
            SegmentLength = HalpFixedAcpiDescTable.pm1_ctrl_len;
            Stride = sizeof(USHORT);
            break;

        case PM1b_CONTROL:
            if (!HalpPm1ControlBlockValid[1] || HalpFixedAcpiDescTable.pm1_ctrl_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1ControlBlocks[1];
            SegmentLength = HalpFixedAcpiDescTable.pm1_ctrl_len;
            Stride = sizeof(USHORT);
            break;

        case GP_STATUS:
            if (!HalpGeneralPurposeBlockValid[0] || HalpFixedAcpiDescTable.gp0_blk_len < 2)
                return FALSE;
            Source = &HalpGeneralPurposeBlocks[0];
            SegmentLength = HalpFixedAcpiDescTable.gp0_blk_len / 2;
            Stride = sizeof(UCHAR);
            break;

        case GP_ENABLE:
            if (!HalpGeneralPurposeBlockValid[0] || HalpFixedAcpiDescTable.gp0_blk_len < 2)
                return FALSE;
            Source = &HalpGeneralPurposeBlocks[0];
            SegmentLength = HalpFixedAcpiDescTable.gp0_blk_len / 2;
            Offset = SegmentLength;
            Stride = sizeof(UCHAR);
            break;

        case SMI_CMD:
            if (!HalpFixedAcpiDescTable.smi_cmd_io_port)
                return FALSE;

            RtlZeroMemory(&TempGas, sizeof(TempGas));
            TempGas.AddressSpaceID = ACPI_GAS_SYSTEM_IO;
            TempGas.BitWidth = 8;
            TempGas.Address.QuadPart = HalpFixedAcpiDescTable.smi_cmd_io_port;
            Source = &TempGas;
            Offset = 0;
            SegmentLength = sizeof(UCHAR);
            Stride = sizeof(UCHAR);
            break;

        default:
            return FALSE;
    }

    if (!Source || !Gas || !Stride)
    {
        return FALSE;
    }

    if ((RegisterIndex + 1) * Stride > SegmentLength)
    {
        return FALSE;
    }

    *Gas = *Source;
    Gas->Address.QuadPart += Offset + (RegisterIndex * Stride);
    return TRUE;
}

static
USHORT
HalpAcpiInterfaceReadRegister(
    _In_ ACPI_REG_TYPE RegisterType,
    _In_ ULONG RegisterIndex)
{
    GEN_ADDR Gas;
    ULONG Value;

    if (!HalpAcpiResolveInterfaceGas(RegisterType, RegisterIndex, &Gas))
    {
        return 0;
    }

    if (!HalpAcpiReadRegister(&Gas, &Value))
    {
        return 0;
    }

    return (USHORT)Value;
}

static
VOID
HalpAcpiInterfaceWriteRegister(
    _In_ ACPI_REG_TYPE RegisterType,
    _In_ ULONG RegisterIndex,
    _In_ USHORT Value)
{
    GEN_ADDR Gas;

    if (!HalpAcpiResolveInterfaceGas(RegisterType, RegisterIndex, &Gas))
    {
        return;
    }

    HalpAcpiWriteRegister(&Gas, Value);
}

static
NTSTATUS
HalpPortRangeQueryAllocate(
    _In_ BOOLEAN IsSparse,
    _In_ BOOLEAN PrimaryIsMmio,
    _In_opt_ PVOID VirtBaseAddr,
    _In_ PHYSICAL_ADDRESS PhysBaseAddr,
    _In_ ULONG Length,
    _Out_ PUSHORT NewRangeId)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    USHORT StartId;
    USHORT CandidateId;
    USHORT Attempt;
    BOOLEAN IdAssigned = FALSE;
    PHALP_PORT_RANGE_ENTRY RangeEntry;

    if (!NewRangeId || (Length == 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RangeEntry = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*RangeEntry), TAG_HAL);
    if (!RangeEntry)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RangeEntry->IsSparse = IsSparse;
    RangeEntry->PrimaryIsMmio = PrimaryIsMmio;
    RangeEntry->VirtualAddress = VirtBaseAddr;
    RangeEntry->PhysicalAddress = PhysBaseAddr;
    RangeEntry->Length = Length;

    KeAcquireSpinLock(&HalpPortRangeLock, &OldIrql);

    StartId = HalpPortRangeNextId ? HalpPortRangeNextId : 1;

    for (Attempt = 0; Attempt < 0xFFFF; Attempt++)
    {
        PHALP_PORT_RANGE_ENTRY ExistingEntry;

        CandidateId = (USHORT)(((StartId - 1 + Attempt) % 0xFFFF) + 1);

        Entry = HalpPortRangeList.Flink;
        while (Entry != &HalpPortRangeList)
        {
            ExistingEntry = CONTAINING_RECORD(Entry, HALP_PORT_RANGE_ENTRY, ListEntry);
            if (ExistingEntry->RangeId == CandidateId)
            {
                break;
            }

            Entry = Entry->Flink;
        }

        if (Entry == &HalpPortRangeList)
        {
            RangeEntry->RangeId = CandidateId;
            HalpPortRangeNextId = CandidateId + 1;
            if (HalpPortRangeNextId == 0)
            {
                HalpPortRangeNextId = 1;
            }

            InsertTailList(&HalpPortRangeList, &RangeEntry->ListEntry);
            IdAssigned = TRUE;
            break;
        }
    }

    KeReleaseSpinLock(&HalpPortRangeLock, OldIrql);

    if (!IdAssigned)
    {
        ExFreePoolWithTag(RangeEntry, TAG_HAL);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *NewRangeId = RangeEntry->RangeId;
    return STATUS_SUCCESS;
}

static
VOID
HalpPortRangeFreeRange(
    _In_ USHORT RangeId)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PHALP_PORT_RANGE_ENTRY RangeEntry;

    KeAcquireSpinLock(&HalpPortRangeLock, &OldIrql);
    Entry = HalpPortRangeList.Flink;
    while (Entry != &HalpPortRangeList)
    {
        RangeEntry = CONTAINING_RECORD(Entry, HALP_PORT_RANGE_ENTRY, ListEntry);
        if (RangeEntry->RangeId == RangeId)
        {
            RemoveEntryList(Entry);
            KeReleaseSpinLock(&HalpPortRangeLock, OldIrql);
            ExFreePoolWithTag(RangeEntry, TAG_HAL);
            return;
        }

        Entry = Entry->Flink;
    }

    KeReleaseSpinLock(&HalpPortRangeLock, OldIrql);
}

NTSTATUS
NTAPI
HalpQueryBusInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ CONST GUID *InterfaceType,
    _In_ USHORT Version,
    _In_ ULONG InterfaceBufferSize,
    _Out_ PINTERFACE Interface,
    _Out_opt_ PULONG Length)
{
    PACPI_REGS_INTERFACE_STANDARD RegInterface;
    PHAL_PORT_RANGE_INTERFACE PortRangeInterface;
    const USHORT ProvidedVersion = 1;

    if (IsEqualIID(InterfaceType, &GUID_ACPI_REGS_INTERFACE_STANDARD))
    {
        if (InterfaceBufferSize < sizeof(*RegInterface))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        if ((Version != 0) && (Version != ProvidedVersion))
        {
            return STATUS_NOT_SUPPORTED;
        }

        RegInterface = (PACPI_REGS_INTERFACE_STANDARD)Interface;
        RtlZeroMemory(RegInterface, sizeof(*RegInterface));
        RegInterface->Size = sizeof(*RegInterface);
        RegInterface->Version = ProvidedVersion;
        RegInterface->Context = DeviceObject;
        RegInterface->InterfaceReference = HalpPnpInterfaceReference;
        RegInterface->InterfaceDereference = HalpPnpInterfaceDereference;
        RegInterface->ReadAcpiRegister = HalpAcpiInterfaceReadRegister;
        RegInterface->WriteAcpiRegister = HalpAcpiInterfaceWriteRegister;
        RegInterface->InterfaceReference(RegInterface->Context);
        if (Length)
        {
            *Length = sizeof(*RegInterface);
        }

        return STATUS_SUCCESS;
    }

    if (IsEqualIID(InterfaceType, &GUID_ACPI_PORT_RANGES_INTERFACE_STANDARD))
    {
        if (InterfaceBufferSize < sizeof(*PortRangeInterface))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        if ((Version != 0) && (Version != ProvidedVersion))
        {
            return STATUS_NOT_SUPPORTED;
        }

        PortRangeInterface = (PHAL_PORT_RANGE_INTERFACE)Interface;
        RtlZeroMemory(PortRangeInterface, sizeof(*PortRangeInterface));
        PortRangeInterface->Size = sizeof(*PortRangeInterface);
        PortRangeInterface->Version = ProvidedVersion;
        PortRangeInterface->Context = DeviceObject;
        PortRangeInterface->InterfaceReference = HalpPnpInterfaceReference;
        PortRangeInterface->InterfaceDereference = HalpPnpInterfaceDereference;
        PortRangeInterface->QueryAllocateRange = HalpPortRangeQueryAllocate;
        PortRangeInterface->FreeRange = HalpPortRangeFreeRange;
        PortRangeInterface->InterfaceReference(PortRangeInterface->Context);
        if (Length)
        {
            *Length = sizeof(*PortRangeInterface);
        }

        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

ULONG
NTAPI
HalpQueryReservedResources(
    _Out_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptors)
{
    ULONG i;

    if (Descriptors)
    {
        for (i = 0; i < HALP_RESERVED_VECTOR_COUNT; i++, Descriptors++)
        {
            ULONG Vector = (i < 0x400) ? (0x800 + i) : (0x1000 + (i - 0x400));
            Descriptors->Type = CmResourceTypeInterrupt;
            Descriptors->ShareDisposition = CmResourceShareDeviceExclusive;
            Descriptors->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
            Descriptors->u.Interrupt.Level = Vector;
            Descriptors->u.Interrupt.Vector = Vector;
            Descriptors->u.Interrupt.Affinity = 0xFFFFFFFF;
        }
    }

    return HALP_RESERVED_VECTOR_COUNT;
}

BOOLEAN
NTAPI
HalpQueryDeviceTree(
    _Outptr_result_maybenull_ const VOID **Blob,
    _Out_ PULONG Size)
{
    return HalpArm64GetDeviceTree(Blob, Size);
}

static const CHAR *const HalpAppleAicCompatibles[] =
{
    "apple,aic",
    "apple,aic2",
    "apple,t8122-aic3",
};

static const CHAR *const HalpGicCompatibles[] =
{
    "arm,gic-v3",
    "arm,gic-400",
    "arm,cortex-a15-gic",
    "arm,cortex-a9-gic",
    "arm,cortex-a7-gic",
    "arm,pl390",
};

static
BOOLEAN
HalpCompatibleListContains(
    _In_reads_bytes_(Length) const CHAR *List,
    _In_ ULONG Length,
    _In_reads_(Count) const CHAR *const *Names,
    _In_ ULONG Count)
{
    ULONG Cursor = 0, EntryLength, Index;
    const CHAR *Entry;

    while ((Entry = FdtNextString(List, Length, &Cursor, &EntryLength)) != NULL)
    {
        for (Index = 0; Index < Count; ++Index)
        {
            if (strlen(Names[Index]) == EntryLength && !memcmp(Entry, Names[Index], EntryLength))
                return TRUE;
        }
    }
    return FALSE;
}

BOOLEAN
NTAPI
HalpTranslateDeviceTreeInterrupt(
    _In_reads_bytes_(CompatibleLength) const CHAR *Compatible,
    _In_ ULONG CompatibleLength,
    _In_reads_(CellCount) const ULONG *Cells,
    _In_ ULONG CellCount,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered)
{
    ULONG Die = 0, Number, Flags, PerDie;

    *Gsi = 0;
    *EdgeTriggered = FALSE;

    if (HalpCompatibleListContains(Compatible, CompatibleLength, HalpAppleAicCompatibles,
                                   RTL_NUMBER_OF(HalpAppleAicCompatibles)))
    {
        if (CellCount == 3)
        {
            Number = Cells[1];
            Flags = Cells[2];
        }
        else if (CellCount == 4)
        {
            Die = Cells[1];
            Number = Cells[2];
            Flags = Cells[3];
        }
        else
        {
            return FALSE;
        }
        if (Cells[0] != 0)
            return FALSE;
        PerDie = HalpAppleAicIrqsPerDie();
        if (Die && !PerDie)
            return FALSE;
        *Gsi = 32 + Die * PerDie + Number;
        *EdgeTriggered = (Flags & 0x3) != 0;
        return TRUE;
    }

    if (HalpCompatibleListContains(Compatible, CompatibleLength, HalpGicCompatibles,
                                   RTL_NUMBER_OF(HalpGicCompatibles)))
    {
        if (CellCount < 3)
            return FALSE;
        if (Cells[0] == 0)
            *Gsi = 32 + Cells[1];
        else if (Cells[0] == 1)
            *Gsi = 16 + Cells[1];
        else
            return FALSE;
        *EdgeTriggered = (Cells[2] & 0x3) != 0;
        return TRUE;
    }

    return FALSE;
}

BOOLEAN
NTAPI
HalArm64TranslateDeviceTreeInterrupt(
    _In_reads_bytes_(CompatibleLength) const CHAR *Compatible,
    _In_ ULONG CompatibleLength,
    _In_reads_(CellCount) const ULONG *Cells,
    _In_ ULONG CellCount,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered)
{
    return HalpTranslateDeviceTreeInterrupt(Compatible, CompatibleLength, Cells, CellCount, Gsi, EdgeTriggered);
}

/* EOF */

BOOLEAN
NTAPI
HalQueryPciRoutedInterrupt(
    _In_ ULONG Segment,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ ULONG Pin,
    _Out_ PULONG Gsi)
{
    static const PCSTR Compatibles[] = { "pci-host-ecam-generic" };
    ULONG Node, Parent, Match, Length, MaskLength, Domain = 0, Offset, Index;
    ULONG Address[4], Mask[4], Controller, ParentAddressCells, ParentInterruptCells, CompatibleLength;
    ULONG Cells[4];
    const UCHAR *Map, *MaskProperty;
    const CHAR *Compatible;
    const VOID *Blob;
    BOOLEAN Edge;
    FDT Fdt;
    ULONG BlobSize;

    *Gsi = 0;
    if (HalAcpiGetTable(NULL, 'PCAF') || !HalpArm64GetDeviceTree(&Blob, &BlobSize) || !Blob ||
        !FdtOpen(Blob, BlobSize, &Fdt) ||
        !HalpArm64FindDeviceTreeNode(&Fdt, Compatibles, RTL_NUMBER_OF(Compatibles), &Node, &Parent, &Match) ||
        Pin < 1 || Pin > 4 || Device > 31 || Function > 7 || Bus > 255)
        return FALSE;
    if (FdtReadU32(&Fdt, Node, "linux,pci-domain", &Domain) && Domain != Segment)
        return FALSE;
    Map = FdtGetProperty(&Fdt, Node, "interrupt-map", &Length);
    MaskProperty = FdtGetProperty(&Fdt, Node, "interrupt-map-mask", &MaskLength);
    if (!Map || !MaskProperty || MaskLength != 4 * sizeof(ULONG))
        return FALSE;
    Address[0] = (Bus << 16) | (Device << 11) | (Function << 8);
    Address[1] = Address[2] = 0;
    Address[3] = Pin;
    for (Index = 0; Index < 4; ++Index)
        Mask[Index] = FdtReadBigEndian32(MaskProperty + Index * sizeof(ULONG));
    for (Offset = 0; Offset + 5 * sizeof(ULONG) <= Length;)
    {
        BOOLEAN Hit = TRUE;

        for (Index = 0; Index < 4; ++Index)
        {
            if ((Address[Index] ^ FdtReadBigEndian32(Map + Offset + Index * sizeof(ULONG))) & Mask[Index])
                Hit = FALSE;
        }
        Controller = HalpArm64FindDeviceTreePhandle(&Fdt, FdtReadBigEndian32(Map + Offset + 4 * sizeof(ULONG)));
        if (Controller == FDT_NO_NODE)
            return FALSE;
        if (!FdtReadU32(&Fdt, Controller, "#address-cells", &ParentAddressCells))
            ParentAddressCells = 0;
        if (!FdtReadU32(&Fdt, Controller, "#interrupt-cells", &ParentInterruptCells) ||
            ParentInterruptCells == 0 || ParentInterruptCells > RTL_NUMBER_OF(Cells))
            return FALSE;
        Offset += (5 + ParentAddressCells) * sizeof(ULONG);
        if (Offset + ParentInterruptCells * sizeof(ULONG) > Length)
            return FALSE;
        if (Hit)
        {
            for (Index = 0; Index < ParentInterruptCells; ++Index)
                Cells[Index] = FdtReadBigEndian32(Map + Offset + Index * sizeof(ULONG));
            Compatible = FdtGetProperty(&Fdt, Controller, "compatible", &CompatibleLength);
            return Compatible &&
                   HalpTranslateDeviceTreeInterrupt(Compatible, CompatibleLength, Cells, ParentInterruptCells, Gsi, &Edge);
        }
        Offset += ParentInterruptCells * sizeof(ULONG);
    }
    return FALSE;
}
