/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The DMA adapter and bus-address work the HAL answers for
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

PDMA_ADAPTER
NTAPI
IoGetDmaAdapter(
    _In_opt_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ PDEVICE_DESCRIPTION DeviceDescription,
    _Inout_ PULONG NumberOfMapRegisters)
{
    PDMA_ADAPTER Adapter;

    PAGED_CODE();

    if (HalGetDmaAdapter == NULL) return NULL;

    Adapter = HalGetDmaAdapter(PhysicalDeviceObject,
                               DeviceDescription,
                               NumberOfMapRegisters);
    if (Adapter == NULL) return NULL;

    return Adapter;
}

NTSTATUS
NTAPI
IopAllocateAdapterChannel(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine,
    _In_ PVOID Context)
{
    PDMA_OPERATIONS Operations = DmaAdapter->DmaOperations;

    return Operations->AllocateAdapterChannel(DmaAdapter,
                                              DeviceObject,
                                              NumberOfMapRegisters,
                                              ExecutionRoutine,
                                              Context);
}

BOOLEAN
NTAPI
IoTranslateBusAddress(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    return HalTranslateBusAddress(InterfaceType,
                                  BusNumber,
                                  BusAddress,
                                  AddressSpace,
                                  TranslatedAddress);
}
