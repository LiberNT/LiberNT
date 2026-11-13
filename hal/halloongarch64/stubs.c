/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Platform services this machine does not provide
 *
 * Every entry here either reports an unsupported service through its own
 * result (BOOLEAN/FALSE, STATUS_NOT_SUPPORTED) or implements a service that
 * needs no device. Nothing fakes success: a caller that needs the service sees
 * the failure.
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/halfuncs.h>
#include <ndk/iofuncs.h>
#include <reactos/hal/acpi_pci.h>
#include <reactos/hal/msi.h>
#include "halp.h"

/* Entered with interrupts disabled, like the x86 sti;hlt sequence: enable,
 * wait for any interrupt, return with interrupts enabled. IDLE is defined to
 * end on an interrupt request [V1-4.2.6.3]; with CRMD.IE set the pending
 * request is taken immediately after the instruction retires. */
VOID
NTAPI
HalProcessorIdle(VOID)
{
    __asm__ __volatile__("csrxchg %0, %1, %2"
                         :
                         : "r"((ULONG)HALP_LA64_CRMD_IE),
                           "r"((ULONG)HALP_LA64_CRMD_IE),
                           "i"(HALP_LA64_CSR_CRMD)
                         : "memory");
    __asm__ __volatile__("idle 0" ::: "memory");
}

BOOLEAN
NTAPI
HalMakeBeep(
    _In_ ULONG Frequency)
{
    UNREFERENCED_PARAMETER(Frequency);
    return FALSE;
}

ARC_STATUS
NTAPI
HalGetEnvironmentVariable(
    _In_ PCH Variable,
    _In_ USHORT Length,
    _Out_ PCH Buffer)
{
    UNREFERENCED_PARAMETER(Variable);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Buffer);
    return ENOENT;
}

ARC_STATUS
NTAPI
HalSetEnvironmentVariable(
    _In_ PCH Name,
    _In_ PCH Value)
{
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Value);
    return EACCES;
}

/* The HAL supplies PCI configuration access and translation to the PnP PCI bus
 * driver. It has no legacy, non-PnP slot resource allocator. */
NTSTATUS
NTAPI
HalAssignSlotResources(
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PUNICODE_STRING DriverClassName,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ INTERFACE_TYPE BusType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_ PCM_RESOURCE_LIST *AllocatedResources)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(DriverClassName);
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(BusType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    if (!AllocatedResources)
        return STATUS_INVALID_PARAMETER;
    *AllocatedResources = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalGetInterruptTargetInformation(
    _Out_ PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation)
{
    UNREFERENCED_PARAMETER(TargetInformation);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalGetMessageRoutingInfo(
    _Out_ PHAL_MESSAGE_ROUTING_INFO RoutingInfo)
{
    UNREFERENCED_PARAMETER(RoutingInfo);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalpGetInterruptTargetInformation(
    _Out_ PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation)
{
    return HalGetInterruptTargetInformation(TargetInformation);
}

NTSTATUS
NTAPI
HalpGetMessageRoutingInfo(
    _Out_ PHAL_MESSAGE_ROUTING_INFO RoutingInfo)
{
    return HalGetMessageRoutingInfo(RoutingInfo);
}

VOID
NTAPI
HalReturnToFirmware(
    _In_ FIRMWARE_REENTRY Action)
{
    UNREFERENCED_PARAMETER(Action);

    /* No reset protocol is reachable: LoongArch has no reset instruction, the
     * 7A1000 power-management block needs programming this HAL cannot derive
     * without ACPI, and there is no firmware call interface on this port.
     * TODO: program the 7A1000 chapter 12 power-management reset register once
     * its values are sourced; until then a reset request halts this core. */
    _disable();
    for (;;)
        __asm__ __volatile__("idle 0");
}

/* The stable counter is one platform counter, consistent on every core and not
 * writable: take part in the rendezvous without adjusting it. */
VOID
NTAPI
HalCalibratePerformanceCounter(
    _In_ volatile PLONG Count,
    _In_ ULONGLONG NewCount)
{
    UNREFERENCED_PARAMETER(NewCount);
    InterlockedDecrement(Count);
    while (*Count)
        YieldProcessor();
}

KIRQL
NTAPI
HalConvertDeviceIdtToIrql(
    _In_ ULONG Vector)
{
    UNREFERENCED_PARAMETER(Vector);
    return 0;
}

NTSTATUS
NTAPI
HalGetProcessorIdByNtNumber(
    _In_ ULONG ProcessorNumber,
    _Out_ PULONG ProcessorId)
{
    ULONG Id;

    if (!ProcessorId)
        return STATUS_INVALID_PARAMETER;
    Id = HalpLa64QueryProcessorId(ProcessorNumber);
    if (Id == MAXULONG)
        return STATUS_INVALID_PARAMETER;
    *ProcessorId = Id;
    return STATUS_SUCCESS;
}

/* ACPI PCI routing hooks. This platform has no ACPI namespace and routes PCI
 * INTx by fixed board wiring, so there is nothing for the ACPI driver to tell
 * the HAL and nothing for the HAL to publish. */
VOID
NTAPI
HalpConfigurePciRootBridge(
    _In_ const HAL_ACPI_PCI_ROOT_INFO *Info)
{
    UNREFERENCED_PARAMETER(Info);
}

VOID
NTAPI
HalpRegisterPciRouteQuery(
    _In_opt_ PHAL_ACPI_PCI_ROUTE_QUERY Provider)
{
    UNREFERENCED_PARAMETER(Provider);
}

VOID
NTAPI
HalpSetPciRoutingMap(
    _In_reads_opt_(EntryCount) const HAL_ACPI_PCI_ROUTE_ENTRY *Entries,
    _In_ ULONG EntryCount)
{
    UNREFERENCED_PARAMETER(Entries);
    UNREFERENCED_PARAMETER(EntryCount);
}

VOID
NTAPI
HalpRecordPciMaxGsi(
    _In_ const HAL_ACPI_PCI_ROUTE_ENTRY *Entry)
{
    UNREFERENCED_PARAMETER(Entry);
}

VOID
NTAPI
KeFlushWriteBuffer(VOID)
{
    /* Order memory and I/O accesses; does not claim a posted write reached a
     * device. */
    __asm__ __volatile__("dbar 0" ::: "memory");
}

/* Report the HAL-owned windows to Io. Device BAR windows are not HAL
 * allocations; the timer is a CSR and the interrupt controllers are described
 * by the platform bus. */
VOID
NTAPI
HalReportResourceUsage(VOID)
{
    static WCHAR Name[] = L"LoongArch64 HAL";
    UNICODE_STRING HalName;
    CM_RESOURCE_LIST List;
    ULONG ListSize;
    ULONG Count = 1;

    HalName.Buffer = Name;
    HalName.Length = sizeof(Name) - sizeof(WCHAR);
    HalName.MaximumLength = sizeof(Name);
    RtlZeroMemory(&List, sizeof(List));
    List.Count = 1;
    List.List[0].InterfaceType = Internal;
    List.List[0].PartialResourceList.Version = 1;
    List.List[0].PartialResourceList.Revision = 1;
    List.List[0].PartialResourceList.Count = 0;
    if (HalpLa64GetPciResource(&List.List[0].PartialResourceList.PartialDescriptors[0]))
        List.List[0].PartialResourceList.Count = 1;
    else
        Count = 0;
    ListSize = FIELD_OFFSET(CM_RESOURCE_LIST, List[0].PartialResourceList.PartialDescriptors[Count]);
    IoReportHalResourceUsage(&HalName, &List, &List, ListSize);
}
