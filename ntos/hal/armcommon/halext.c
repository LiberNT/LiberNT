/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Runs the HAL extensions the loader loaded and holds the services they register
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ndk/ldrtypes.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <libernt/hal/acpi_pci.h>
#include <libernt/drivers/acpi/acpi.h>
#include <halacpi.h>
#include <hal.h>
#include "halext.h"
#include "devtree.h"
#include "gic_internal.h"

#define NDEBUG
#include <debug.h>

extern PHAL_ACPI_PCI_ROUTE_QUERY HalpArm64PciRouteQueryCallback;

static BOOLEAN HalpArm64ExtensionMatched;
static PHAL_EXTENSION_PHASE1_INITIALIZE HalpArm64ExtensionPhase1Initialize;

const HAL_ARM64_INTERRUPT_CONTROLLER *HalpArm64InterruptController = NULL;
const HAL_ARM64_PCI_CONFIG_BACKEND *HalpArm64PciConfigBackend = NULL;
const HAL_ARM64_SYSTEM_RESET *HalpArm64SystemReset = NULL;
const HAL_ARM64_MSI_DOORBELL *HalpArm64MsiDoorbell = NULL;
const HAL_ARM64_PROCESSOR_IDLE *HalpArm64ProcessorIdle = NULL;

NTSTATUS
HalpArm64RegisterInterruptController(
    _In_ const HAL_ARM64_INTERRUPT_CONTROLLER *Controller)
{
    if (!Controller)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64InterruptController)
        return STATUS_TOO_LATE;

    HalpArm64InterruptController = Controller;

    /*
     * The GIC layer must stop trusting its discovery defaults: on GIC-less
     * platforms the firmware MADT often carries compatibility decoys and
     * the GICD/GICC globals default to QEMU-virt physical addresses.
     */
    HalpGicAdoptExternalInterruptController();

    DPRINT1("[arm64][HALEXT] Interrupt controller: %s\n", Controller->Name);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpArm64RegisterPciConfigBackend(
    _In_ const HAL_ARM64_PCI_CONFIG_BACKEND *Backend)
{
    if (!Backend)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64PciConfigBackend)
        return STATUS_TOO_LATE;

    HalpArm64PciConfigBackend = Backend;

    DPRINT1("[arm64][HALEXT] PCI config-space backend: %s\n", Backend->Name);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpArm64RegisterSystemReset(
    _In_ const HAL_ARM64_SYSTEM_RESET *Reset)
{
    if (!Reset || !Reset->Restart)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64SystemReset)
        return STATUS_TOO_LATE;

    HalpArm64SystemReset = Reset;

    DPRINT1("[arm64][HALEXT] System reset: %s\n", Reset->Name);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpArm64RegisterMsiDoorbell(
    _In_ const HAL_ARM64_MSI_DOORBELL *Doorbell)
{
    if (!Doorbell || !Doorbell->VectorCount)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64MsiDoorbell)
        return STATUS_TOO_LATE;

    HalpArm64MsiDoorbell = Doorbell;

    DPRINT1("[arm64][HALEXT] MSI doorbell: %s at 0x%I64x for vectors %lu+%lu\n", Doorbell->Name, Doorbell->Address,
            Doorbell->FirstVector, Doorbell->VectorCount);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpArm64RegisterProcessorIdle(
    _In_ const HAL_ARM64_PROCESSOR_IDLE *Idle)
{
    if (!Idle || !Idle->Idle)
        return STATUS_INVALID_PARAMETER;

    if (HalpArm64ProcessorIdle)
        return STATUS_TOO_LATE;

    HalpArm64ProcessorIdle = Idle;

    DPRINT1("[arm64][HALEXT] Processor idle: %s\n", Idle->Name);
    return STATUS_SUCCESS;
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
    PHAL_ACPI_PCI_ROUTE_QUERY Query = HalpArm64PciRouteQueryCallback;

    *Gsi = 0;
    if (!Query)
        return FALSE;

    return Query(Segment, Bus, Device, Function, Pin, Gsi, Polarity, TriggerMode);
}

ULONG
HalpArm64GetMcfgRangeCount(VOID)
{
    return HalpAcpiMcfgAllocations ? HalpAcpiMcfgAllocationCount : 0;
}

BOOLEAN
HalpArm64GetMcfgRange(
    _In_ ULONG Index,
    _Out_ PHAL_ACPI_MCFG_RANGE Range)
{
    if (Index >= HalpArm64GetMcfgRangeCount())
        return FALSE;

    Range->PciSegment = HalpAcpiMcfgAllocations[Index].PciSegment;
    Range->StartBusNumber = HalpAcpiMcfgAllocations[Index].StartBusNumber;
    Range->EndBusNumber = HalpAcpiMcfgAllocations[Index].EndBusNumber;
    return TRUE;
}

static NTSTATUS NTAPI HalpExtRegisterInterruptController(_In_ const HAL_ARM64_INTERRUPT_CONTROLLER *Controller) { return HalpArm64RegisterInterruptController(Controller); }
static NTSTATUS NTAPI HalpExtRegisterPciConfigBackend(_In_ const HAL_ARM64_PCI_CONFIG_BACKEND *Backend) { return HalpArm64RegisterPciConfigBackend(Backend); }
static NTSTATUS NTAPI HalpExtRegisterSystemReset(_In_ const HAL_ARM64_SYSTEM_RESET *Reset) { return HalpArm64RegisterSystemReset(Reset); }
static NTSTATUS NTAPI HalpExtRegisterMsiDoorbell(_In_ const HAL_ARM64_MSI_DOORBELL *Doorbell) { return HalpArm64RegisterMsiDoorbell(Doorbell); }
static NTSTATUS NTAPI HalpExtRegisterProcessorIdle(_In_ const HAL_ARM64_PROCESSOR_IDLE *Idle) { return HalpArm64RegisterProcessorIdle(Idle); }
static volatile ULONG *NTAPI HalpExtMmio(_In_ ULONG_PTR Base, _In_ ULONG Offset) { return HalpMmio(Base, Offset); }
static PVOID NTAPI HalpExtMapPhysicalMemory(_In_ PHYSICAL_ADDRESS PhysicalAddress, _In_ PFN_COUNT PageCount) { return HalpMapPhysicalMemory64(PhysicalAddress, PageCount); }
static PVOID NTAPI HalpExtGetAcpiTable(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock, _In_ ULONG Signature) { return HalAcpiGetTable(LoaderBlock, Signature); }
static BOOLEAN NTAPI HalpExtOpenDeviceTree(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock, _Out_ PFDT Fdt) { return HalpArm64OpenDeviceTree(LoaderBlock, Fdt); }

static
BOOLEAN
NTAPI
HalpExtFindDeviceTreeNode(
    _In_ PFDT Fdt,
    _In_reads_(Count) const PCSTR *Compatibles,
    _In_ ULONG Count,
    _Out_ PULONG Node,
    _Out_ PULONG Parent,
    _Out_ PULONG Match)
{
    return HalpArm64FindDeviceTreeNode(Fdt, Compatibles, Count, Node, Parent, Match);
}

static BOOLEAN NTAPI HalpExtGetDeviceTree(_Outptr_result_maybenull_ const VOID **Blob, _Out_ PULONG Size) { return HalpArm64GetDeviceTree(Blob, Size); }

static
BOOLEAN
NTAPI
HalpExtQueryPciRoute(
    _In_ ULONG Segment,
    _In_ UCHAR Bus,
    _In_ UCHAR Device,
    _In_ UCHAR Function,
    _In_ UCHAR Pin,
    _Out_ PULONG Gsi,
    _Out_opt_ PUCHAR Polarity,
    _Out_opt_ PUCHAR TriggerMode)
{
    return HalpArm64QueryPciRoute(Segment, Bus, Device, Function, Pin, Gsi, Polarity, TriggerMode);
}

static ULONG NTAPI HalpExtGetMcfgRangeCount(VOID) { return HalpArm64GetMcfgRangeCount(); }
static BOOLEAN NTAPI HalpExtGetMcfgRange(_In_ ULONG Index, _Out_ PHAL_ACPI_MCFG_RANGE Range) { return HalpArm64GetMcfgRange(Index, Range); }
static NTSTATUS NTAPI HalpExtRegisterProcessorStart(_In_ const HAL_ARM64_PROCESSOR_START *Start) { return HalpArm64RegisterProcessorStart(Start); }

static const HAL_EXTENSION_SERVICES HalpExtensionServices =
{
    sizeof(HAL_EXTENSION_SERVICES),
    HAL_EXTENSION_SERVICES_VERSION,
    HalpExtRegisterInterruptController,
    HalpExtRegisterPciConfigBackend,
    HalpExtRegisterSystemReset,
    HalpExtRegisterMsiDoorbell,
    HalpExtRegisterProcessorIdle,
    HalpExtMmio,
    HalpExtMapPhysicalMemory,
    HalpExtGetAcpiTable,
    HalpExtOpenDeviceTree,
    HalpExtFindDeviceTreeNode,
    HalpExtGetDeviceTree,
    HalpExtQueryPciRoute,
    HalpExtGetMcfgRangeCount,
    HalpExtGetMcfgRange,
    HalpExtRegisterProcessorStart
};

static
PLIST_ENTRY
HalpArm64ExtensionModules(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_EXTENSION Extension;

    if (!LoaderBlock || !LoaderBlock->Extension)
        return NULL;

    Extension = LoaderBlock->Extension;
    if (Extension->Size < RTL_SIZEOF_THROUGH_FIELD(LOADER_PARAMETER_EXTENSION, HalExtensionModuleList) ||
        !Extension->HalExtensionModuleList.Flink)
    {
        return NULL;
    }

    return &Extension->HalExtensionModuleList;
}

VOID
HalpArm64ProbePlatforms(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PHAL_EXTENSION_MODULE_ENTRY Module;
    PHAL_EXTENSION_INITIALIZE Initialize;
    PLIST_ENTRY Head, Link;
    NTSTATUS Status;

    if (HalpArm64ExtensionMatched)
        return;

    Head = HalpArm64ExtensionModules(LoaderBlock);
    if (!Head)
        return;

    for (Link = Head->Flink; Link != Head; Link = Link->Flink)
    {
        Module = CONTAINING_RECORD(Link, HAL_EXTENSION_MODULE_ENTRY, Link);
        ASSERT(Module->LdrEntry && Module->InitializeRoutine);

        Initialize = (PHAL_EXTENSION_INITIALIZE)Module->InitializeRoutine;
        Status = Initialize(&HalpExtensionServices, LoaderBlock);
        if (NT_SUCCESS(Status))
        {
            HalpArm64ExtensionMatched = TRUE;
            HalpArm64ExtensionPhase1Initialize = (PHAL_EXTENSION_PHASE1_INITIALIZE)Module->Phase1InitializeRoutine;
            DPRINT1("[arm64][HALEXT] Platform: %wZ\n", &Module->LdrEntry->BaseDllName);
            return;
        }
    }
}

VOID
HalpArm64Phase1PlatformInit(VOID)
{
    if (HalpArm64ExtensionPhase1Initialize)
        HalpArm64ExtensionPhase1Initialize();
}
