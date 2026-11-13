/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Processor identity, per-core startup and IPI delivery
 *
 * This port boots uniprocessor. A secondary core can only be started by a
 * protocol the firmware publishes: on the machines this HAL targets that
 * protocol is ACPI-described (MADT plus the Loongson boot mailbox), and there
 * is no ACPI table here and no loader field that names the cores. Starting a
 * core by guessing a mailbox address would be an invented write to unknown
 * hardware, so HalStartNextProcessor reports failure and the kernel stays on
 * the boot core. The IPI path below is complete and is what a future SMP port
 * needs; only the "first instruction of a new core" is missing.
 */

#include <ntifs.h>
#include <ndk/halfuncs.h>
#include "halp.h"

static ULONG HalpLa64StartedProcessors = 1;

VOID
NTAPI
HalInitializeProcessor(
    _In_ ULONG Number,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    if (Number >= HalpLa64StartedProcessors)
    {
        /* Unreachable while the port is uniprocessor: the kernel only calls
         * this for a core that HalStartNextProcessor reported as started. */
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x534D50ull, Number,
                     HalpLa64StartedProcessors, 0);
    }

    /* Per-core line state: a fresh timer deadline, a cleared IPI latch and the
     * line mask this HAL delivers with. Idempotent for the boot core, which
     * already ran the same sequence from HalInitSystem phase 1. */
    HalpLa64IpiAcknowledge();
    HalpLa64StartClock();
    HalpLa64EnableLineInterrupts();
}

BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    UNREFERENCED_PARAMETER(ProcessorState);

    /* See the file header: no core can be started without a firmware
     * protocol this platform does not expose. */
    return FALSE;
}

BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    return TRUE;
}

ULONG
NTAPI
HalQueryMaximumProcessorCount(VOID)
{
    /* One core: the kernel must not size per-processor structures for cores it
     * cannot start. The boot core's CPUID is the only one the loader block
     * publishes. */
    return HalpLa64StartedProcessors;
}

BOOLEAN
NTAPI
HalIsHyperThreadingEnabled(VOID)
{
    return FALSE;
}

ULONG
NTAPI
HalpLa64QueryProcessorId(
    _In_ ULONG ProcessorNumber)
{
    if (ProcessorNumber != 0)
        return MAXULONG;
    return (ULONG)HalpLa64BootCpuId;
}
