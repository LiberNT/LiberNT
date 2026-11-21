/*
 * PROJECT:     LiberNT HAL extensions
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The HAL routines platform code calls, served through the HAL's services table
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <hal.h>
#include <halacpi.h>
#include "halextlib.h"

static const HAL_EXTENSION_SERVICES *HalExtServices;

NTSTATUS
HalExtBindServices(
    _In_ const HAL_EXTENSION_SERVICES *Services)
{
    if (!Services ||
        Services->Size < sizeof(*Services) ||
        Services->Version != HAL_EXTENSION_SERVICES_VERSION)
    {
        return STATUS_REVISION_MISMATCH;
    }

    ASSERT(!HalExtServices || HalExtServices == Services);
    HalExtServices = Services;
    return STATUS_SUCCESS;
}

volatile ULONG *
HalpMmio(
    ULONG_PTR Base,
    ULONG Offset)
{
    ASSERT(HalExtServices);
    return HalExtServices->Mmio(Base, Offset);
}

PVOID
NTAPI
HalpMapPhysicalMemory64(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ PFN_COUNT PageCount)
{
    ASSERT(HalExtServices);
    return HalExtServices->MapPhysicalMemory(PhysicalAddress, PageCount);
}

PVOID
NTAPI
HalAcpiGetTable(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Signature)
{
    ASSERT(HalExtServices);
    return HalExtServices->GetAcpiTable(LoaderBlock, Signature);
}

NTSTATUS
HalpArm64RegisterInterruptController(
    _In_ const HAL_ARM64_INTERRUPT_CONTROLLER *Controller)
{
    ASSERT(HalExtServices);
    return HalExtServices->RegisterInterruptController(Controller);
}

NTSTATUS
HalpArm64RegisterPciConfigBackend(
    _In_ const HAL_ARM64_PCI_CONFIG_BACKEND *Backend)
{
    ASSERT(HalExtServices);
    return HalExtServices->RegisterPciConfigBackend(Backend);
}

NTSTATUS
HalpArm64RegisterSystemReset(
    _In_ const HAL_ARM64_SYSTEM_RESET *Reset)
{
    ASSERT(HalExtServices);
    return HalExtServices->RegisterSystemReset(Reset);
}

NTSTATUS
HalpArm64RegisterMsiDoorbell(
    _In_ const HAL_ARM64_MSI_DOORBELL *Doorbell)
{
    ASSERT(HalExtServices);
    return HalExtServices->RegisterMsiDoorbell(Doorbell);
}

NTSTATUS
HalpArm64RegisterProcessorIdle(
    _In_ const HAL_ARM64_PROCESSOR_IDLE *Idle)
{
    ASSERT(HalExtServices);
    return HalExtServices->RegisterProcessorIdle(Idle);
}

BOOLEAN
HalpArm64OpenDeviceTree(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_ PFDT Fdt)
{
    ASSERT(HalExtServices);
    return HalExtServices->OpenDeviceTree(LoaderBlock, Fdt);
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
    ASSERT(HalExtServices);
    return HalExtServices->FindDeviceTreeNode(Fdt, Compatibles, Count, Node, Parent, Match);
}

BOOLEAN
HalpArm64GetDeviceTree(
    _Outptr_result_maybenull_ const VOID **Blob,
    _Out_ PULONG Size)
{
    ASSERT(HalExtServices);
    return HalExtServices->GetDeviceTree(Blob, Size);
}

BOOLEAN
HalpArm64QueryPciRoute(
    _In_ ULONG Segment,
    _In_ UCHAR Bus,
    _In_ UCHAR Device,
    _In_ UCHAR Function,
    _In_ UCHAR Pin,
    _Out_ PULONG Gsi,
    _Out_opt_ PUCHAR Polarity,
    _Out_opt_ PUCHAR TriggerMode)
{
    ASSERT(HalExtServices);
    return HalExtServices->QueryPciRoute(Segment, Bus, Device, Function, Pin, Gsi, Polarity, TriggerMode);
}

ULONG
HalpArm64GetMcfgRangeCount(VOID)
{
    ASSERT(HalExtServices);
    return HalExtServices->GetMcfgRangeCount();
}

BOOLEAN
HalpArm64GetMcfgRange(
    _In_ ULONG Index,
    _Out_ PHAL_ACPI_MCFG_RANGE Range)
{
    ASSERT(HalExtServices);
    return HalExtServices->GetMcfgRange(Index, Range);
}
