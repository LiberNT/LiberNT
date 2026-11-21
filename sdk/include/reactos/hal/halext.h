/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Contract between the ARM HAL in the kernel and the HAL extensions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <libernt/fdtlib.h>

/*
 * The backend must honor GIC INTID/priority semantics: INTIDs are the
 * kernel-visible vectors (SGIs 0-15, timer PPIs 26-30, device sources 32+),
 * priorities are GIC-style bytes (lower = more urgent), and
 * SetPriorityMask() implements the software PMR: AcknowledgeInterrupt() may
 * only deliver a source whose priority is numerically below the current
 * mask, and must quiesce (defer) blocked level-triggered sources until the
 * mask rises above them.
 */
#define HAL_ARM64_SGI_IPI    0
#define HAL_ARM64_SGI_APC    1
#define HAL_ARM64_SGI_DPC    2
#define HAL_ARM64_SGI_FREEZE 3

#define HAL_ARM64_SGI_IPI_PRIORITY    0x10
#define HAL_ARM64_SGI_APC_PRIORITY    0xE0
#define HAL_ARM64_SGI_DPC_PRIORITY    0xD0
#define HAL_ARM64_SGI_FREEZE_PRIORITY 0x00

typedef struct _HAL_ARM64_INTERRUPT_CONTROLLER
{
    PCSTR Name;
    ULONG (*AcknowledgeInterrupt)(VOID);
    VOID (*EndInterrupt)(_In_ ULONG IntId);
    VOID (*EnableInterrupt)(_In_ ULONG IntId);
    VOID (*DisableInterrupt)(_In_ ULONG IntId);
    VOID (*SetInterruptPriority)(_In_ ULONG IntId, _In_ ULONG Priority);
    VOID (*SetTriggerMode)(_In_ ULONG IntId, _In_ BOOLEAN EdgeTriggered);
    VOID (*SetPriorityMask)(_In_ KIRQL Irql);
    ULONG (*GetPriorityMask)(VOID);
    VOID (*SendSgi)(_In_ KAFFINITY TargetSet, _In_ ULONG SgiId);
    VOID (*InitializeProcessor)(_In_ ULONG ProcessorNumber);
    BOOLEAN (*Phase0Initialize)(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);
    BOOLEAN (*Phase1MapRuntimeMmio)(VOID);
    BOOLEAN (*TranslateDeviceTreeInterrupt)(
        _In_reads_bytes_(CompatibleLength) const CHAR *Compatible,
        _In_ ULONG CompatibleLength,
        _In_reads_(CellCount) const ULONG *Cells,
        _In_ ULONG CellCount,
        _Out_ PULONG Gsi,
        _Out_ PBOOLEAN EdgeTriggered);
    ULONG IntIdLimit;

    BOOLEAN DeliversToAnyProcessor;
} HAL_ARM64_INTERRUPT_CONTROLLER, *PHAL_ARM64_INTERRUPT_CONTROLLER;

#define HAL_ARM64_CONTROLLER_INTID_MAX 4128

typedef struct _HAL_ARM64_PCI_CONFIG_BACKEND
{
    PCSTR Name;

    /*
     * Returns TRUE when the access was handled (absent devices read as
     * all-ones), FALSE when the backend cannot serve the request and the
     * generic ECAM/_CBA paths should run instead.
     */
    BOOLEAN (*AccessConfigSpace)(
        _In_ BOOLEAN Write,
        _In_ USHORT Segment,
        _In_ ULONG Bus,
        _In_ PCI_SLOT_NUMBER Slot,
        _Inout_updates_bytes_(Length) PVOID Buffer,
        _In_ ULONG Offset,
        _In_ ULONG Length);
} HAL_ARM64_PCI_CONFIG_BACKEND, *PHAL_ARM64_PCI_CONFIG_BACKEND;

/* For SoCs whose firmware offers no PSCI SYSTEM_RESET. Restart runs at HIGH_LEVEL with interrupts off. */
typedef struct _HAL_ARM64_SYSTEM_RESET
{
    PCSTR Name;
    VOID (*Restart)(VOID);
} HAL_ARM64_SYSTEM_RESET, *PHAL_ARM64_SYSTEM_RESET;

/*
 * For SoCs whose MSIs are a write of the message index to one doorbell
 * address that raises a fixed range of interrupt controller lines.
 */
typedef struct _HAL_ARM64_MSI_DOORBELL
{
    PCSTR Name;
    ULONG64 Address;
    ULONG FirstVector;
    ULONG VectorCount;
} HAL_ARM64_MSI_DOORBELL, *PHAL_ARM64_MSI_DOORBELL;

typedef struct _HAL_ARM64_PROCESSOR_IDLE
{
    PCSTR Name;
    VOID (*Idle)(VOID);
} HAL_ARM64_PROCESSOR_IDLE, *PHAL_ARM64_PROCESSOR_IDLE;

/* For SoCs whose firmware starts a processor at an entry point it is handed, with no argument. */
typedef struct _HAL_ARM64_PROCESSOR_START
{
    PCSTR Name;
    BOOLEAN (*Start)(_In_ ULONG64 Mpidr, _In_ ULONG64 EntryPoint);
} HAL_ARM64_PROCESSOR_START, *PHAL_ARM64_PROCESSOR_START;

typedef struct _HAL_ACPI_MCFG_RANGE
{
    USHORT PciSegment;
    UCHAR StartBusNumber;
    UCHAR EndBusNumber;
} HAL_ACPI_MCFG_RANGE, *PHAL_ACPI_MCFG_RANGE;

#define HAL_EXTENSION_SERVICES_VERSION 1

typedef struct _HAL_EXTENSION_SERVICES
{
    ULONG Size;
    ULONG Version;
    NTSTATUS (NTAPI *RegisterInterruptController)(_In_ const HAL_ARM64_INTERRUPT_CONTROLLER *Controller);
    NTSTATUS (NTAPI *RegisterPciConfigBackend)(_In_ const HAL_ARM64_PCI_CONFIG_BACKEND *Backend);
    NTSTATUS (NTAPI *RegisterSystemReset)(_In_ const HAL_ARM64_SYSTEM_RESET *Reset);
    NTSTATUS (NTAPI *RegisterMsiDoorbell)(_In_ const HAL_ARM64_MSI_DOORBELL *Doorbell);
    NTSTATUS (NTAPI *RegisterProcessorIdle)(_In_ const HAL_ARM64_PROCESSOR_IDLE *Idle);
    volatile ULONG *(NTAPI *Mmio)(_In_ ULONG_PTR Base, _In_ ULONG Offset);
    PVOID (NTAPI *MapPhysicalMemory)(_In_ PHYSICAL_ADDRESS PhysicalAddress, _In_ PFN_COUNT PageCount);
    PVOID (NTAPI *GetAcpiTable)(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock, _In_ ULONG Signature);
    BOOLEAN (NTAPI *OpenDeviceTree)(_In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock, _Out_ PFDT Fdt);
    BOOLEAN (NTAPI *FindDeviceTreeNode)(
        _In_ PFDT Fdt,
        _In_reads_(Count) const PCSTR *Compatibles,
        _In_ ULONG Count,
        _Out_ PULONG Node,
        _Out_ PULONG Parent,
        _Out_ PULONG Match);
    BOOLEAN (NTAPI *GetDeviceTree)(_Outptr_result_maybenull_ const VOID **Blob, _Out_ PULONG Size);
    BOOLEAN (NTAPI *QueryPciRoute)(
        _In_ ULONG Segment,
        _In_ UCHAR Bus,
        _In_ UCHAR Device,
        _In_ UCHAR Function,
        _In_ UCHAR Pin,
        _Out_ PULONG Gsi,
        _Out_opt_ PUCHAR Polarity,
        _Out_opt_ PUCHAR TriggerMode);
    ULONG (NTAPI *GetMcfgRangeCount)(VOID);
    BOOLEAN (NTAPI *GetMcfgRange)(_In_ ULONG Index, _Out_ PHAL_ACPI_MCFG_RANGE Range);
    NTSTATUS (NTAPI *RegisterProcessorStart)(_In_ const HAL_ARM64_PROCESSOR_START *Start);
} HAL_EXTENSION_SERVICES, *PHAL_EXTENSION_SERVICES;

typedef
NTSTATUS
NTAPI
HAL_EXTENSION_INITIALIZE(
    _In_ const HAL_EXTENSION_SERVICES *Services,
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);
typedef HAL_EXTENSION_INITIALIZE *PHAL_EXTENSION_INITIALIZE;

typedef
VOID
NTAPI
HAL_EXTENSION_PHASE1_INITIALIZE(VOID);
typedef HAL_EXTENSION_PHASE1_INITIALIZE *PHAL_EXTENSION_PHASE1_INITIALIZE;

volatile ULONG *HalpMmio(ULONG_PTR Base, ULONG Offset);

NTSTATUS
HalpArm64RegisterInterruptController(
    _In_ const HAL_ARM64_INTERRUPT_CONTROLLER *Controller);

NTSTATUS
HalpArm64RegisterPciConfigBackend(
    _In_ const HAL_ARM64_PCI_CONFIG_BACKEND *Backend);

NTSTATUS
HalpArm64RegisterSystemReset(
    _In_ const HAL_ARM64_SYSTEM_RESET *Reset);

NTSTATUS
HalpArm64RegisterMsiDoorbell(
    _In_ const HAL_ARM64_MSI_DOORBELL *Doorbell);

NTSTATUS
HalpArm64RegisterProcessorIdle(
    _In_ const HAL_ARM64_PROCESSOR_IDLE *Idle);

/* STATUS_NOT_SUPPORTED where the HAL starts every processor itself. */
NTSTATUS
HalpArm64RegisterProcessorStart(
    _In_ const HAL_ARM64_PROCESSOR_START *Start);

BOOLEAN
HalpArm64OpenDeviceTree(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _Out_ PFDT Fdt);

BOOLEAN
HalpArm64FindDeviceTreeNode(
    _In_ PFDT Fdt,
    _In_reads_(Count) const PCSTR *Compatibles,
    _In_ ULONG Count,
    _Out_ PULONG Node,
    _Out_ PULONG Parent,
    _Out_ PULONG Match);

BOOLEAN
HalpArm64GetDeviceTree(
    _Outptr_result_maybenull_ const VOID **Blob,
    _Out_ PULONG Size);

BOOLEAN
HalpArm64QueryPciRoute(
    _In_ ULONG Segment,
    _In_ UCHAR Bus,
    _In_ UCHAR Device,
    _In_ UCHAR Function,
    _In_ UCHAR Pin,
    _Out_ PULONG Gsi,
    _Out_opt_ PUCHAR Polarity,
    _Out_opt_ PUCHAR TriggerMode);

ULONG
HalpArm64GetMcfgRangeCount(VOID);

BOOLEAN
HalpArm64GetMcfgRange(
    _In_ ULONG Index,
    _Out_ PHAL_ACPI_MCFG_RANGE Range);
