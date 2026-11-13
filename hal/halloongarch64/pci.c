/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     PCIe ECAM host bridge, address translation, INTx and MSI
 *
 * The board has one PCIe host bridge with a 128 MiB ECAM window at
 * 0x2000_0000 (one 1 MiB configuration region per bus, buses 0..127), a
 * 1 GiB memory window at 0x4000_0000 and a 48 KiB I/O window whose bus
 * addresses 0x4000..0xFFFF live at 0x1800_4000 (QEMU hw/loongarch/virt.c
 * VIRT_PCI_CFG_*, VIRT_PCI_MEM_*, VIRT_PCI_IO_BASE/OFFSET/SIZE).
 *
 * There is no ACPI MCFG or _PRT to read: the root bridge is described by the
 * platform table and the INTA..INTD routing is the board's fixed wiring
 * (virt.c virt_devices_init: gpex IRQ n -> PCH-PIC input 16+n). PCIe MSI is
 * delivered through the PCH-MSI window, which raises EIOINTC sources 32..255.
 */

#include <ntifs.h>
#include <ioaccess.h>
#include <ndk/halfuncs.h>
#include "halp.h"

typedef struct _HALP_LA64_PCI_HOST
{
    ULONG64 EcamBase;
    ULONG64 EcamSize;
    PUCHAR EcamMapping;
    ULONG FirstBus;
    ULONG LastBus;
    ULONG Segment;
    ULONG64 MemoryBase;
    ULONG64 MemorySize;
    ULONG64 IoBase;      /* bus I/O address range start */
    ULONG64 IoSize;
    ULONG64 IoMapping;   /* CPU address of IoBase */
    ULONG IntxBase;      /* first EIOINTC source of INTA */
    KSPIN_LOCK Lock;
    BOOLEAN Present;
} HALP_LA64_PCI_HOST;

static HALP_LA64_PCI_HOST HalpLa64PciHost;

BOOLEAN
HalpLa64InitializePci(VOID)
{
    HALP_LA64_PCI_HOST *Host = &HalpLa64PciHost;
    ULONG64 Buses;

    if (!HalpLa64Platform.PciEcamBase || !HalpLa64Platform.PciMemoryBase)
        return FALSE;

    Host->EcamBase = HalpLa64Platform.PciEcamBase;
    Host->FirstBus = HalpLa64Platform.PciFirstBus;
    Host->LastBus = HalpLa64Platform.PciLastBus;
    Host->Segment = HalpLa64Platform.PciSegment;
    Host->MemoryBase = HalpLa64Platform.PciMemoryBase;
    Host->MemorySize = HalpLa64Platform.PciMemoryLength;
    Host->IoBase = HalpLa64Platform.PciIoBase;
    Host->IoSize = HalpLa64Platform.PciIoLength;
    Host->IntxBase = HalpLa64Platform.PciIntxBase;

    if (Host->FirstBus > Host->LastBus || Host->LastBus > 255)
        return FALSE;
    Buses = (ULONG64)(Host->LastBus - Host->FirstBus + 1);
    /* The ECAM window must cover every bus the bridge reports. */
    if (Host->EcamBase + Buses * HALP_LA64_PCI_BUS_SIZE < Host->EcamBase)
        return FALSE;
    if (HalpLa64Platform.PciEcamLength < Buses * HALP_LA64_PCI_BUS_SIZE)
        return FALSE;
    Host->EcamSize = Buses * HALP_LA64_PCI_BUS_SIZE;
    if (!Host->EcamBase || (Host->EcamBase & (HALP_LA64_PCI_BUS_SIZE - 1)))
        return FALSE;
    if (Host->MemoryBase + Host->MemorySize < Host->MemoryBase ||
        Host->IoBase + Host->IoSize < Host->IoBase)
        return FALSE;

    KeInitializeSpinLock(&Host->Lock);
    Host->Present = TRUE;
    return TRUE;
}

BOOLEAN
HalpLa64MapPciConfig(VOID)
{
    HALP_LA64_PCI_HOST *Host = &HalpLa64PciHost;
    PHYSICAL_ADDRESS Address;

    if (!Host->Present)
        return TRUE;
    Address.QuadPart = (LONGLONG)Host->EcamBase;
    Host->EcamMapping = MmMapIoSpace(Address, Host->EcamSize, MmNonCached);
    if (!Host->EcamMapping)
        return FALSE;

    /* Devices reach the I/O window through their BARs; the CPU reaches the
     * same cycles as memory at IoMapping. VIRT_PCI_IO_OFFSET makes the window
     * start at bus port 0x4000, so the CPU address of bus port p is
     * VIRT_PCI_IO_BASE + (p - 0x4000). */
    Host->IoMapping = 0x18004000ULL;
    return TRUE;
}

BOOLEAN
NTAPI
HalpLa64GetPciBusRange(
    _Out_ PULONG FirstBus,
    _Out_ PULONG LastBus)
{
    if (!HalpLa64PciHost.Present || !FirstBus || !LastBus)
        return FALSE;
    *FirstBus = HalpLa64PciHost.FirstBus;
    *LastBus = HalpLa64PciHost.LastBus;
    return TRUE;
}

BOOLEAN
HalpLa64PciDmaCoherent(VOID)
{
    return HalpLa64PciHost.Present && HalpLa64Platform.DmaCoherent;
}

BOOLEAN
HalpLa64PciHostPresent(VOID)
{
    return HalpLa64PciHost.Present;
}

BOOLEAN
NTAPI
HalpLa64GetPciResource(
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource)
{
    if (!HalpLa64PciHost.EcamMapping || !Resource)
        return FALSE;
    Resource->Type = CmResourceTypeMemory;
    Resource->ShareDisposition = CmResourceShareDeviceExclusive;
    Resource->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Resource->u.Memory.Start.QuadPart = (LONGLONG)HalpLa64PciHost.EcamBase;
    Resource->u.Memory.Length = (ULONG)HalpLa64PciHost.EcamSize;
    return TRUE;
}

/* INTA..INTD of any device behind the bridge land on consecutive PCH-PIC
 * inputs starting at IntxBase (16 on this board), which is also their EIOINTC
 * source number. The pin rotates with the device number in real hardware;
 * this bridge does not rotate, so the pin alone selects the input. */
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
    HALP_LA64_PCI_HOST *Host = &HalpLa64PciHost;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Function);
    if (!Gsi || !Host->Present || Segment != Host->Segment ||
        Bus < Host->FirstBus || Bus > Host->LastBus ||
        Pin < 1 || Pin > 4 || !Host->IntxBase)
        return FALSE;
    *Gsi = Host->IntxBase + (Pin - 1);
    return TRUE;
}

/* PCIe MSI is supported and delivered by the PCH-MSI window. Shutdown is not
 * negotiated in this HAL, so the OSC controls are reported as granted. */
BOOLEAN
NTAPI
HalQueryPciMsiSupport(
    _In_ ULONG Segment,
    _In_ UCHAR Bus,
    _Out_ PBOOLEAN Supported,
    _Out_opt_ PULONG OscStatusFlags,
    _Out_opt_ PULONG OscControlGranted,
    _Out_opt_ PUSHORT EffectiveSegment,
    _Out_opt_ PULONG OscMaskedControls)
{
    UNREFERENCED_PARAMETER(Bus);
    if (Supported)
        *Supported = (HalpLa64Platform.PchMsiBase != 0);
    if (OscStatusFlags)
        *OscStatusFlags = 0;
    if (OscControlGranted)
        *OscControlGranted = 0;
    if (EffectiveSegment)
        *EffectiveSegment = (USHORT)Segment;
    if (OscMaskedControls)
        *OscMaskedControls = 0;
    return TRUE;
}

BOOLEAN
NTAPI
HalTranslateBusAddress(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    HALP_LA64_PCI_HOST *Host = &HalpLa64PciHost;
    ULONG64 Address;

    if (!AddressSpace || !TranslatedAddress || *AddressSpace > 1 ||
        BusAddress.QuadPart < 0)
        return FALSE;

    Address = (ULONG64)BusAddress.QuadPart;
    if (InterfaceType == Internal && BusNumber == 0 && *AddressSpace == 0 &&
        Address < HalpLa64PhysicalLimit)
    {
        *TranslatedAddress = BusAddress;
        return TRUE;
    }
    if (InterfaceType != PCIBus || !Host->Present ||
        BusNumber != Host->Segment)
        return FALSE;

    if (*AddressSpace == 1)
    {
        /* Bus I/O ports are memory-mapped on this machine. */
        if (Address < Host->IoBase || Address - Host->IoBase >= Host->IoSize)
            return FALSE;
        TranslatedAddress->QuadPart = (LONGLONG)(Host->IoMapping +
                                                 (Address - Host->IoBase));
        *AddressSpace = 0;
        return TRUE;
    }
    if (Address < Host->MemoryBase ||
        Address - Host->MemoryBase >= Host->MemorySize)
        return FALSE;
    /* The host bridge applies no offset to the memory window. */
    TranslatedAddress->QuadPart = (LONGLONG)Address;
    return TRUE;
}

/* Clamp the driver's requested resources to what the bridge decodes. A driver
 * that asks for a window outside the host bridge's apertures would otherwise be
 * handed an address that no device can reach. */
static
VOID
HalpLa64ClampToWindow(
    _Inout_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ ULONG AddressSpace,
    _In_ ULONG64 Base,
    _In_ ULONG64 Size)
{
    ULONG64 Minimum, Maximum;

    if (Descriptor->Type != CmResourceTypeMemory &&
        Descriptor->Type != CmResourceTypeMemoryLarge &&
        Descriptor->Type != CmResourceTypePort)
        return;
    if (Descriptor->Type == CmResourceTypePort && AddressSpace != 1)
        return;
    if (Descriptor->Type != CmResourceTypePort && AddressSpace != 0)
        return;

    Minimum = (ULONG64)Descriptor->u.Memory.MinimumAddress.QuadPart;
    Maximum = (ULONG64)Descriptor->u.Memory.MaximumAddress.QuadPart;
    if (Minimum < Base)
    {
        Minimum = Base;
        Descriptor->u.Memory.MinimumAddress.QuadPart = (LONGLONG)Minimum;
    }
    if (Maximum > Base + Size - 1 || Maximum < Base)
    {
        Maximum = Base + Size - 1;
        Descriptor->u.Memory.MaximumAddress.QuadPart = (LONGLONG)Maximum;
    }
    if (Minimum > Maximum)
    {
        /* The request cannot be satisfied inside the bridge's window. */
        Descriptor->u.Memory.MinimumAddress.QuadPart = 0;
        Descriptor->u.Memory.MaximumAddress.QuadPart = 0;
    }
}

NTSTATUS
NTAPI
HalAdjustResourceList(
    _Inout_ PIO_RESOURCE_REQUIREMENTS_LIST *ResourceList)
{
    HALP_LA64_PCI_HOST *Host = &HalpLa64PciHost;
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Index, Count;

    if (!ResourceList || !*ResourceList)
        return STATUS_INVALID_PARAMETER;
    List = *ResourceList;
    if (List->ListSize < FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0]))
        return STATUS_INVALID_PARAMETER;
    if (!Host->Present)
        return STATUS_SUCCESS;

    /* List[0] is the first descriptor: the array starts at List[1]. */
    if (List->ListSize < FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[1]))
        return STATUS_INVALID_PARAMETER;
    Descriptor = &List->List[0].Descriptors[0];
    Count = (List->ListSize - FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0])) /
            sizeof(IO_RESOURCE_DESCRIPTOR);
    for (Index = 0; Index < Count; ++Index)
    {
        HalpLa64ClampToWindow(Descriptor, List->InterfaceType == PCIBus ? 1 : 0,
                              Host->IoBase, Host->IoSize);
        HalpLa64ClampToWindow(Descriptor, 0, Host->MemoryBase, Host->MemorySize);
        ++Descriptor;
    }
    return STATUS_SUCCESS;
}

static
ULONG
HalpLa64AccessPciConfig(
    _In_ BOOLEAN Write,
    _In_ BOOLEAN Lock,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Inout_ PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    HALP_LA64_PCI_HOST *Host = &HalpLa64PciHost;
    PCI_SLOT_NUMBER Slot;
    PUCHAR Bytes = Buffer;
    volatile UCHAR *Config;
    KIRQL OldIrql = PASSIVE_LEVEL;
    ULONG Done = 0;

    Slot.u.AsULONG = SlotNumber;
    if (!Buffer || !Length || !Host->EcamMapping || Slot.u.bits.Reserved ||
        Offset >= HALP_LA64_PCI_CONFIG_SIZE || BusNumber != Host->Segment)
        return 0;

    /* The PCI bus driver passes a flat bus number that packs the segment in
     * the upper bits on some ports; on this platform the segment is always 0
     * and BusNumber is the bus. Keep the check simple and explicit. */
    if (Slot.u.bits.DeviceNumber > 31 || Slot.u.bits.FunctionNumber > 7)
        return 0;

    if (BusNumber < Host->FirstBus || BusNumber > Host->LastBus)
        return 0;
    Config = Host->EcamMapping +
             ((BusNumber - Host->FirstBus) << 20) +
             (Slot.u.bits.DeviceNumber << 15) +
             (Slot.u.bits.FunctionNumber << 12) + Offset;
    Length = min(Length, HALP_LA64_PCI_CONFIG_SIZE - Offset);

    if (Lock)
    {
        KeRaiseIrql(HIGH_LEVEL, &OldIrql);
        KeAcquireSpinLockAtDpcLevel(&Host->Lock);
    }
    __asm__ __volatile__("dbar 0" ::: "memory");

    while (Done < Length)
    {
        ULONG Width = 1, Value = 0, Index;

        if (!((Offset + Done) & 3) && Length - Done >= 4)
            Width = 4;
        else if (!((Offset + Done) & 1) && Length - Done >= 2)
            Width = 2;
        if (Write)
        {
            for (Index = 0; Index < Width; ++Index)
                Value |= (ULONG)Bytes[Done + Index] << (Index * 8);
            if (Width == 4)
                *(volatile ULONG *)(Config + Done) = Value;
            else if (Width == 2)
                *(volatile USHORT *)(Config + Done) = (USHORT)Value;
            else
                Config[Done] = (UCHAR)Value;
        }
        else
        {
            if (Width == 4)
                Value = *(volatile ULONG *)(Config + Done);
            else if (Width == 2)
                Value = *(volatile USHORT *)(Config + Done);
            else
                Value = Config[Done];
            for (Index = 0; Index < Width; ++Index)
                Bytes[Done + Index] = (UCHAR)(Value >> (Index * 8));
        }
        Done += Width;
    }
    __asm__ __volatile__("dbar 0" ::: "memory");
    if (Lock)
    {
        KeReleaseSpinLockFromDpcLevel(&Host->Lock);
        KeLowerIrql(OldIrql);
    }
    return Done;
}

ULONG
NTAPI
HalpKdReadPciConfig(
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_ PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    /* The debugger reads with the other processors frozen at an IRQL at which
     * the lock cannot be taken. A frozen processor may hold it: never wait. */
    if (!HalpLa64PciHost.Present || !KeTestSpinLock(&HalpLa64PciHost.Lock))
        return MAXULONG;
    return HalpLa64AccessPciConfig(FALSE, FALSE, BusNumber, SlotNumber, Buffer,
                                   Offset, Length);
}

ULONG
NTAPI
HalGetBusDataByOffset(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_ PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    if (BusDataType != PCIConfiguration)
        return 0;
    return HalpLa64AccessPciConfig(FALSE, TRUE, BusNumber, SlotNumber, Buffer,
                                   Offset, Length);
}

ULONG
NTAPI
HalGetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_ PVOID Buffer,
    _In_ ULONG Length)
{
    return HalGetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, 0,
                                 Length);
}

ULONG
NTAPI
HalSetBusDataByOffset(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    if (BusDataType != PCIConfiguration)
        return 0;
    return HalpLa64AccessPciConfig(TRUE, TRUE, BusNumber, SlotNumber, Buffer,
                                   Offset, Length);
}

ULONG
NTAPI
HalSetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_ PVOID Buffer,
    _In_ ULONG Length)
{
    return HalSetBusDataByOffset(BusDataType, BusNumber, SlotNumber, Buffer, 0,
                                 Length);
}

/* ---------------------------------------------------------------------------
 * Port I/O
 *
 * There is no x86 port space: a port address is a bus I/O address decoded by
 * the host bridge and reached as MMIO. Ports outside the window decode to
 * nothing, so reads return all ones, the value an absent x86 device returns.
 * ------------------------------------------------------------------------ */

static
BOOLEAN
HalpLa64PortAddress(
    _In_ ULONG_PTR Port,
    _Out_ PVOID *Address)
{
    HALP_LA64_PCI_HOST *Host = &HalpLa64PciHost;

    if (!Host->Present || Port < Host->IoBase || Port - Host->IoBase >= Host->IoSize)
        return FALSE;
    *Address = (PVOID)(ULONG_PTR)(Host->IoMapping + (Port - Host->IoBase));
    return TRUE;
}

#undef READ_PORT_UCHAR
#undef READ_PORT_USHORT
#undef READ_PORT_ULONG
#undef WRITE_PORT_UCHAR
#undef WRITE_PORT_USHORT
#undef WRITE_PORT_ULONG

UCHAR
NTAPI
READ_PORT_UCHAR(
    _In_ PUCHAR Port)
{
    PVOID Address;

    if (!HalpLa64PortAddress((ULONG_PTR)Port, &Address))
        return 0xFF;
    return READ_REGISTER_UCHAR((volatile UCHAR *)Address);
}

USHORT
NTAPI
READ_PORT_USHORT(
    _In_ PUSHORT Port)
{
    PVOID Address;

    if (!HalpLa64PortAddress((ULONG_PTR)Port, &Address))
        return 0xFFFF;
    return READ_REGISTER_USHORT((volatile USHORT *)Address);
}

ULONG
NTAPI
READ_PORT_ULONG(
    _In_ PULONG Port)
{
    PVOID Address;

    if (!HalpLa64PortAddress((ULONG_PTR)Port, &Address))
        return 0xFFFFFFFF;
    return READ_REGISTER_ULONG((volatile ULONG *)Address);
}

VOID
NTAPI
WRITE_PORT_UCHAR(
    _In_ PUCHAR Port,
    _In_ UCHAR Value)
{
    PVOID Address;

    if (HalpLa64PortAddress((ULONG_PTR)Port, &Address))
        WRITE_REGISTER_UCHAR((volatile UCHAR *)Address, Value);
}

VOID
NTAPI
WRITE_PORT_USHORT(
    _In_ PUSHORT Port,
    _In_ USHORT Value)
{
    PVOID Address;

    if (HalpLa64PortAddress((ULONG_PTR)Port, &Address))
        WRITE_REGISTER_USHORT((volatile USHORT *)Address, Value);
}

VOID
NTAPI
WRITE_PORT_ULONG(
    _In_ PULONG Port,
    _In_ ULONG Value)
{
    PVOID Address;

    if (HalpLa64PortAddress((ULONG_PTR)Port, &Address))
        WRITE_REGISTER_ULONG((volatile ULONG *)Address, Value);
}

VOID
NTAPI
READ_PORT_BUFFER_UCHAR(
    _In_ PUCHAR Port,
    _Out_writes_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        *Buffer++ = READ_PORT_UCHAR(Port);
}

VOID
NTAPI
READ_PORT_BUFFER_USHORT(
    _In_ PUSHORT Port,
    _Out_writes_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        *Buffer++ = READ_PORT_USHORT(Port);
}

VOID
NTAPI
READ_PORT_BUFFER_ULONG(
    _In_ PULONG Port,
    _Out_writes_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        *Buffer++ = READ_PORT_ULONG(Port);
}

VOID
NTAPI
WRITE_PORT_BUFFER_UCHAR(
    _In_ PUCHAR Port,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        WRITE_PORT_UCHAR(Port, *Buffer++);
}

VOID
NTAPI
WRITE_PORT_BUFFER_USHORT(
    _In_ PUSHORT Port,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        WRITE_PORT_USHORT(Port, *Buffer++);
}

VOID
NTAPI
WRITE_PORT_BUFFER_ULONG(
    _In_ PULONG Port,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    while (Count--)
        WRITE_PORT_ULONG(Port, *Buffer++);
}
