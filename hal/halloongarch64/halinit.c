/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Bootstrap HAL initialization
 *
 * Phase 0 runs on the loader's boot stack with the boot page tables already
 * active; phase 1 runs from the kernel's Phase1Initialization thread at
 * PASSIVE_LEVEL. The split is the one hal/halriscv64 uses: phase 0 adopts and
 * validates everything the loader handed over, phase 1 touches hardware that
 * needs a mapped MMIO window and then unmasks the line interrupts.
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"
#include "../arch/common/include/halefi.h"

typedef enum _HALP_LA64_INITIALIZATION_FAILURE
{
    La64HalNoFailure = 0,
    La64HalInvalidBootPhase,
    La64HalInvalidLoaderContract,
    La64HalTimerUnavailable,
    La64HalPlatformUnavailable,
    La64HalPciMappingFailed,
    La64HalPchMappingFailed,
    La64HalRtcMappingFailed,
    La64HalUartMappingFailed
} HALP_LA64_INITIALIZATION_FAILURE;

volatile ULONG HalpLa64InitializationPhase;
volatile ULONG HalpLa64InitializationFailure;
ULONG HalpLa64CurrentTimeIncrement = HALP_LA64_MAXIMUM_INCREMENT;

VOID
NTAPI
HalpLa64EnableLineInterrupts(VOID)
{
    ULONG Mask = HALP_LA64_IRQ_BIT(HALP_LA64_IRQ_TIMER) |
                 HALP_LA64_IRQ_BIT(HALP_LA64_IRQ_IPI);
    ULONG Line;

    for (Line = 0; Line < HALP_LA64_HWI_COUNT; ++Line)
        Mask |= HALP_LA64_IRQ_BIT(HALP_LA64_IRQ_HWI(Line));

    /* ECFG.LIE is a per-core mask the kernel owns for IRQL. Set the lines
     * this HAL delivers with one csrxchg, which reads the current value and
     * ORs the mask, so nothing the kernel has already masked is re-enabled.
     * [V1-4.2.1] csrxchg rd, rj, csr: rd is the new value and rj the mask;
     * the kernel's KfLowerIrql restores a saved ECFG value, which preserves
     * these bits as long as its PASSIVE-level image is the enabled set. */
    __asm__ __volatile__("csrxchg %0, %1, %2"
                         :
                         : "r"((ULONG)Mask), "r"((ULONG)Mask),
                           "i"(HALP_LA64_CSR_ECFG)
                         : "memory");
}

BOOLEAN
NTAPI
HalInitSystem(
    _In_ ULONG BootPhase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (BootPhase == 1)
    {
        if (KeGetCurrentProcessorNumber() != 0)
            return HalpLa64InitializationPhase == 2;
        if (HalpLa64InitializationPhase != 1)
        {
            HalpLa64InitializationFailure = La64HalInvalidBootPhase;
            return FALSE;
        }

        if (!HalpLa64MapPciConfig())
        {
            HalpLa64InitializationFailure = La64HalPciMappingFailed;
            return FALSE;
        }
        /* The console is mapped first so a platform that does not match the
         * one this HAL was built for can say so on the wire the loader left
         * running. */
        if (!HalpLa64MapUart())
        {
            HalpLa64InitializationFailure = La64HalUartMappingFailed;
            return FALSE;
        }
        if (!HalpLa64MapPlatform() || !HalpLa64MapInterruptControllers() ||
            !HalpLa64InitializeInterruptControllers())
        {
            HalpLa64InitializationFailure = La64HalPchMappingFailed;
            return FALSE;
        }
        if (!HalpLa64MapRtc())
        {
            HalpLa64InitializationFailure = La64HalRtcMappingFailed;
            return FALSE;
        }

        /* Arm the first deadline and the IPI receiver before any line is
         * unmasked, so the first interrupt taken is one this HAL can clear. */
        HalpLa64StartClock();
        HalpLa64EnableLineInterrupts();

        /* CRMD.IE is deliberately left alone: the kernel owns the interrupt
         * enable bit and sets it when it first lowers the IRQL to PASSIVE.
         * Setting it here would enter the kernel's idle code with interrupts
         * enabled at an IRQL the kernel has not published yet. */
        HalpLa64InitializationPhase = 2;
        return TRUE;
    }

    if ((BootPhase != 0) || (HalpLa64InitializationPhase != 0))
    {
        HalpLa64InitializationFailure = La64HalInvalidBootPhase;
        return FALSE;
    }
    if (LoaderBlock == NULL)
    {
        HalpLa64InitializationFailure = La64HalInvalidLoaderContract;
        return FALSE;
    }

    HalpInitializeEfiRuntime(LoaderBlock);

    if (!HalpLa64AdoptLoaderBlock(LoaderBlock))
    {
        HalpLa64InitializationFailure = La64HalInvalidLoaderContract;
        return FALSE;
    }
    if (!HalpLa64ReadTimerFrequency(&HalpLa64TimerFrequency))
    {
        HalpLa64InitializationFailure = La64HalTimerUnavailable;
        return FALSE;
    }
    if (!HalpLa64SelectPlatform() || !HalpLa64InitializePci())
    {
        HalpLa64InitializationFailure = La64HalPlatformUnavailable;
        return FALSE;
    }
    HalpLa64InitializeCache();
    HalpLa64InitializeRtc();
    HalpLa64InitializeUart();
    HalpLa64InitializeDma(LoaderBlock);
    HalpLa64RegisterProfileInformation();
    HalpSecondaryInitializeDispatch();

    /* The kernel sizes its quantum from the performance counter frequency. */
    KeSetTimeIncrement(HALP_LA64_MAXIMUM_INCREMENT, HALP_LA64_MINIMUM_INCREMENT);
    HalInitPnpDriver = HaliInitPnpDriver;

    HalpLa64InitializationFailure = La64HalNoFailure;
    HalpLa64InitializationPhase = 1;
    return TRUE;
}
