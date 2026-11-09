/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 Application Processor (AP) spinup setup
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include <smp.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern BOOLEAN HalpOnlyBootProcessor;
extern PPROCESSOR_IDENTITY HalpProcessorIdentity;
extern PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
extern PVOID HalpLowStub;
extern HALP_APIC_INFO_TABLE HalpApicInfoTable;

/* Trampoline symbol (hal/halx86/smp/amd64/apentry.S) */
extern UCHAR HalpAPEntry64[];

ULONG HalpStartedProcessorCount = 1;

/* FUNCTIONS *****************************************************************/

BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    /* Bail out if we only use the boot CPU */
    if (HalpOnlyBootProcessor)
        return FALSE;

    /* Bail out if we have started all available CPUs */
    if (HalpStartedProcessorCount == HalpApicInfoTable.ProcessorCount)
        return FALSE;

    /* No low (<1MB) trampoline stub (e.g. UEFI left no low memory): stay UP */
    if (HalpLowStub == NULL)
    {
        DPRINT1("HalStartNextProcessor: no low-memory AP stub, continuing as UP\n");
        return FALSE;
    }

    /* Copy the trampoline into low memory and point it at this processor */
    if (!HalpPrepareLowStub(ProcessorState, HalpAPEntry64))
        return FALSE;

    /* Fire INIT-SIPI-SIPI */
    ApicStartApplicationProcessor(HalpStartedProcessorCount, HalpLowStubPhysicalAddress);

    HalpStartedProcessorCount++;

    return TRUE;
}

BOOLEAN
HalpRestartProcessor(
    _In_ ULONG NTProcessorNumber,
    _In_ PKPROCESSOR_STATE ProcessorState,
    _In_ PVOID Entry)
{
    if (!HalpPrepareLowStub(ProcessorState, Entry))
        return FALSE;

    ApicStartApplicationProcessor(NTProcessorNumber, HalpLowStubPhysicalAddress);
    return TRUE;
}
