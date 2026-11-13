/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     HAL information classes and profile source reporting
 *
 * Only ProfileTime is implemented, and it is sampled from the ordinary clock
 * tick (clock.c). The performance monitor counters of [V1-7.8] are not
 * programmed: PMCFG/PMCNT need a per-event selection table and an overflow
 * line (ESTAT.IS[10], PMCOV) whose rearm path this HAL does not implement, and
 * nothing in the kernel-only profile consumes them. A caller asking for a PMU
 * source is told it is unsupported rather than handed a counter that never
 * overflows.
 */

#include <ntifs.h>
#include "halp.h"

static pHalQuerySystemInformation HalpLa64DefaultQuerySystemInformation;
static pHalSetSystemInformation HalpLa64DefaultSetSystemInformation;

static
NTSTATUS
NTAPI
HalpLa64QuerySystemInformation(
    _In_ HAL_QUERY_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _Inout_ PVOID Buffer,
    _Out_ PULONG ReturnedLength)
{
    PHAL_PROFILE_SOURCE_INFORMATION Information = Buffer;
    PHAL_ACPI_ROOT_POINTER_INFORMATION AcpiInformation = Buffer;

    if (ReturnedLength)
        *ReturnedLength = 0;

    switch (InformationClass)
    {
        case HalProfileSourceInformation:
            if (BufferSize < sizeof(*Information))
                return STATUS_INFO_LENGTH_MISMATCH;
            Information->Supported = (Information->Source == ProfileTime);
            Information->Interval = Information->Supported ? HalpLa64ProfileInterval : 0;
            if (ReturnedLength)
                *ReturnedLength = sizeof(*Information);
            return STATUS_SUCCESS;

        case HalAcpiAuditInformation:
            /* No ACPI locator exists on this port: the LoongArch64 loader
             * block has no ACPI field and the machine this HAL targets
             * publishes no tables. Report the absence instead of inventing an
             * RSDP address. */
            if (BufferSize < sizeof(*AcpiInformation))
                return STATUS_INFO_LENGTH_MISMATCH;
            AcpiInformation->RsdpPhysicalAddress.QuadPart = 0;
            if (ReturnedLength)
                *ReturnedLength = sizeof(*AcpiInformation);
            return STATUS_NOT_FOUND;

        case HalSecondaryInterruptInformation:
            return HalpSecondaryQueryInformation(BufferSize, Buffer, ReturnedLength);

        case HalProcessorBrandString:
            return STATUS_NOT_SUPPORTED;

        case HalFrameBufferCachingInformation:
        case HalDisplayBiosInformation:
        case HalQueryAMLIIllegalIOPortAddresses:
            return STATUS_INVALID_LEVEL;

        default:
            return HalpLa64DefaultQuerySystemInformation(InformationClass, BufferSize,
                                                         Buffer, ReturnedLength);
    }
}

static
NTSTATUS
NTAPI
HalpLa64SetSystemInformation(
    _In_ HAL_SET_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _In_ PVOID Buffer)
{
    PHAL_PROFILE_SOURCE_INTERVAL Interval = Buffer;

    if (InformationClass == HalRegisterSecondaryInterruptInterface)
        return HalpSecondaryRegisterInterface(BufferSize, Buffer);
    if (InformationClass != HalProfileSourceInterval)
        return HalpLa64DefaultSetSystemInformation(InformationClass, BufferSize, Buffer);
    if (BufferSize < sizeof(*Interval))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Interval->Source != ProfileTime)
        return STATUS_NOT_IMPLEMENTED;
    HalSetProfileInterval((ULONG_PTR)Interval->Interval);
    return STATUS_SUCCESS;
}

VOID
HalpLa64RegisterProfileInformation(VOID)
{
    HalpLa64DefaultQuerySystemInformation = HalQuerySystemInformation;
    HalpLa64DefaultSetSystemInformation = HalSetSystemInformation;
    HalQuerySystemInformation = HalpLa64QuerySystemInformation;
    HalSetSystemInformation = HalpLa64SetSystemInformation;
}
