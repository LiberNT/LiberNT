/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 processor start from the kernel's startup blocks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <halacpi_arm64.h>
#include <halarch.h>
#include "devtree.h"

#define NDEBUG
#include <debug.h>

static const HAL_ARM64_PROCESSOR_START *HalpArmProcessorStart;

NTSTATUS
HalpArm64RegisterProcessorStart(
    _In_ const HAL_ARM64_PROCESSOR_START *Start)
{
    if (!Start || !Start->Start)
        return STATUS_INVALID_PARAMETER;

    if (HalpArmProcessorStart)
        return STATUS_TOO_LATE;

    HalpArmProcessorStart = Start;
    DPRINT1("Processor start: %s\n", Start->Name);
    return STATUS_SUCCESS;
}

BOOLEAN
HalpArchPrepareProcessorStart(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    return TRUE;
}

/*
 * ContextFrame.Pc is the physical entry that takes the startup block in ContextFrame.R0, for firmware that passes
 * an argument. ContextFrame.R1 is the physical entry that finds the block itself, for firmware that does not.
 */
BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    ULONGLONG Mpidr;
    ULONG Number;
    LONG Result;

    UNREFERENCED_PARAMETER(LoaderBlock);

    while (HalpStartedProcessorCount < HalpArm64GicInfo.GiccEntryCount)
    {
        Number = HalpStartedProcessorCount++;
        Mpidr = HalpArm64GicInfo.GiccEntries[Number].Mpidr;

        if (HalpArm64PsciInfo.Present)
        {
            Result = HalpPsciCall(PSCI_FN_CPU_ON_32, Mpidr, ProcessorState->ContextFrame.Pc, ProcessorState->ContextFrame.R0);
            if (Result == PSCI_SUCCESS)
                return TRUE;
            DPRINT1("PSCI CPU_ON of MPIDR 0x%I64x failed: %ld\n", Mpidr, Result);
        }
        else if (HalpArmProcessorStart && ProcessorState->ContextFrame.R1 &&
                 HalpArmProcessorStart->Start(Mpidr, ProcessorState->ContextFrame.R1))
        {
            return TRUE;
        }
    }
    return FALSE;
}
