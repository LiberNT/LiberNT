/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The interrupt translator for a bus that changes nothing on the way through
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "fstubp.h"
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FstubTranslateResource(
    _Inout_opt_ PVOID Context,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Source,
    _In_ RESOURCE_TRANSLATION_DIRECTION Direction,
    _In_opt_ ULONG AlternativesCount,
    _In_reads_opt_(AlternativesCount) IO_RESOURCE_DESCRIPTOR Alternatives[],
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Target)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Direction);
    UNREFERENCED_PARAMETER(AlternativesCount);
    UNREFERENCED_PARAMETER(Alternatives);
    UNREFERENCED_PARAMETER(PhysicalDeviceObject);

    if (Source == NULL || Target == NULL) return STATUS_INVALID_PARAMETER;

    *Target = *Source;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FstubTranslateRequirement(
    _Inout_opt_ PVOID Context,
    _In_ PIO_RESOURCE_DESCRIPTOR Source,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PULONG TargetCount,
    _Out_writes_(*TargetCount) PIO_RESOURCE_DESCRIPTOR *Target)
{
    PIO_RESOURCE_DESCRIPTOR Result;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PhysicalDeviceObject);

    if (Source == NULL || TargetCount == NULL || Target == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *TargetCount = 0;
    *Target = NULL;

    Result = ExAllocatePoolZero(PagedPool,
                                sizeof(IO_RESOURCE_DESCRIPTOR),
                                TAG_FSTUB);
    if (Result == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    *Result = *Source;

    *TargetCount = 1;
    *Target = Result;

    return STATUS_SUCCESS;
}

VOID
NTAPI
FstubTranslatorNull(
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
}

NTSTATUS
NTAPI
xHalGetInterruptTranslator(
    _In_ INTERFACE_TYPE ParentInterfaceType,
    _In_ ULONG ParentBusNumber,
    _In_ INTERFACE_TYPE BridgeInterfaceType,
    _In_ USHORT Size,
    _In_ USHORT Version,
    _Out_ PTRANSLATOR_INTERFACE Translator,
    _Out_ PULONG BridgeBusNumber)
{
    UNREFERENCED_PARAMETER(ParentInterfaceType);
    UNREFERENCED_PARAMETER(BridgeInterfaceType);

    if (Translator == NULL || BridgeBusNumber == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Version != HAL_IRQ_TRANSLATOR_VERSION)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Size < sizeof(TRANSLATOR_INTERFACE)) return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(Translator, sizeof(TRANSLATOR_INTERFACE));

    Translator->Size = sizeof(TRANSLATOR_INTERFACE);
    Translator->Version = HAL_IRQ_TRANSLATOR_VERSION;
    Translator->Context = NULL;
    Translator->InterfaceReference = (PINTERFACE_REFERENCE)FstubTranslatorNull;
    Translator->InterfaceDereference = (PINTERFACE_DEREFERENCE)FstubTranslatorNull;
    Translator->TranslateResources = FstubTranslateResource;
    Translator->TranslateResourceRequirements = FstubTranslateRequirement;

    *BridgeBusNumber = ParentBusNumber;

    return STATUS_SUCCESS;
}

/* EOF */
