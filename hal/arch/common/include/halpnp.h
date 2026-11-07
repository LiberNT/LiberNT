/*
 * PROJECT:         LiberNT HAL
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * FILE:            hal/arch/common/include/halpnp.h
 * PURPOSE:         ACPI HAL bus driver and its architecture hooks
 */

#pragma once

/* acpi/halpnpdd.c: reference counting for the interfaces the bus exports */
VOID
NTAPI
HalpPnpInterfaceReference(
    _In_opt_ PVOID Context);

VOID
NTAPI
HalpPnpInterfaceDereference(
    _In_opt_ PVOID Context);

/*
 * Architecture hooks
 */

/* Fill an interface the bus exports; STATUS_NOT_SUPPORTED if unknown */
NTSTATUS
NTAPI
HalpQueryBusInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ CONST GUID *InterfaceType,
    _In_ USHORT Version,
    _In_ ULONG InterfaceBufferSize,
    _Out_ PINTERFACE Interface,
    _Out_opt_ PULONG Length);

/*
 * Resources the HAL keeps for itself, reported with the ACPI device.
 * Returns the descriptor count; Descriptors may be NULL to query it.
 */
ULONG
NTAPI
HalpQueryReservedResources(
    _Out_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptors);

/* The firmware device tree; FALSE when the platform was not booted with one */
BOOLEAN
NTAPI
HalpQueryDeviceTree(
    _Outptr_result_maybenull_ const VOID **Blob,
    _Out_ PULONG Size);

/* Global system interrupt of a device-tree interrupt specifier; FALSE when the HAL does not route it */
BOOLEAN
NTAPI
HalpTranslateDeviceTreeInterrupt(
    _In_reads_bytes_(CompatibleLength) const CHAR *Compatible,
    _In_ ULONG CompatibleLength,
    _In_reads_(CellCount) const ULONG *Cells,
    _In_ ULONG CellCount,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered);

/* EOF */
