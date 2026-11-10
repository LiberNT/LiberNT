/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Raspberry Pi 5 (BCM2712) PCI configuration-space backend: the
 *              firmware MCFG windows decode only function xx:00.0
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <reactos/hal/acpi_pci.h>
#include <reactos/drivers/acpi/acpi.h>
#include <halacpi.h>
#include <hal.h>
#include "halext.h"
#include "bcm2712_pci.h"

#define NDEBUG
#include <debug.h>

static
BOOLEAN
Bcm2712PciAccessConfigSpace(
    _In_ BOOLEAN Write,
    _In_ USHORT Segment,
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER Slot,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(Offset);

    if (Slot.u.bits.DeviceNumber == 0 && Slot.u.bits.FunctionNumber == 0)
        return FALSE;

    for (Index = 0; Index < HalpAcpiMcfgAllocationCount; Index++)
    {
        const HALP_ACPI_MCFG_ALLOCATION *Allocation = &HalpAcpiMcfgAllocations[Index];

        if (Segment != HALP_ACPI_SEGMENT_ANY && Allocation->PciSegment != Segment)
            continue;

        if (Bus < Allocation->StartBusNumber || Bus > Allocation->EndBusNumber)
            continue;

        if (Allocation->StartBusNumber != Allocation->EndBusNumber)
            return FALSE;

        if (!Write)
            RtlFillMemory(Buffer, Length, 0xFF);

        return TRUE;
    }

    return FALSE;
}

static const HAL_ARM64_PCI_CONFIG_BACKEND Bcm2712PciConfigBackend =
{
    "BCM2712 single-function ECAM",
    Bcm2712PciAccessConfigSpace,
};

BOOLEAN
Bcm2712PciProbe(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PDESCRIPTION_HEADER Fadt;

    Fadt = HalAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
    if (!Fadt)
        return FALSE;

    if (RtlCompareMemory(Fadt->OEMID, BCM2712_ACPI_OEM_ID, 6) != 6)
        return FALSE;

    if (RtlCompareMemory(Fadt->OEMTableID, BCM2712_ACPI_OEM_TABLE_ID, 4) != 4)
        return FALSE;

    return NT_SUCCESS(HalpArm64RegisterPciConfigBackend(&Bcm2712PciConfigBackend));
}
