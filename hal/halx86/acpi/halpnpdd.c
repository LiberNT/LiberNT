/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halx86/acpi/halpnpdd.c
 * PURPOSE:         x86 hooks for the ACPI HAL bus driver
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include <halpnp.h>

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
HalpQueryBusInterface(IN PDEVICE_OBJECT DeviceObject,
                      IN CONST GUID *InterfaceType,
                      IN USHORT Version,
                      IN ULONG InterfaceBufferSize,
                      OUT PINTERFACE Interface,
                      OUT PULONG Length OPTIONAL)
{
    /* FIXME: The ACPI register and port range interfaces are not implemented */
    return STATUS_NOT_SUPPORTED;
}

ULONG
NTAPI
HalpQueryReservedResources(OUT PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptors OPTIONAL)
{
    /* Only the SCI is reported */
    UNREFERENCED_PARAMETER(Descriptors);
    return 0;
}

BOOLEAN
NTAPI
HalpQueryDeviceTree(
    _Outptr_result_maybenull_ const VOID **Blob,
    _Out_ PULONG Size)
{
    *Blob = NULL;
    *Size = 0;
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
    UNREFERENCED_PARAMETER(Compatible);
    UNREFERENCED_PARAMETER(CompatibleLength);
    UNREFERENCED_PARAMETER(Cells);
    UNREFERENCED_PARAMETER(CellCount);
    *Gsi = 0;
    *EdgeTriggered = FALSE;
    return FALSE;
}

/* EOF */
