/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Executive initialization hooks this architecture does not need
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * Both phases exist so the executive can ask the architecture to finish work
 * that the common HAL/MM start could not. On LoongArch64 that work is already
 * inside the HAL:
 *
 *   - hal/halloongarch64/halinit.c phase 1 maps the MMIO windows, retires the
 *     loader's aliases and unmasks the delivered line interrupts itself, so
 *     no early device alias is left for the kernel to drop;
 *   - ntos/ke/loongarch64/irql.c raises and lowers IRQL through ECFG.LIE
 *     directly, with no bootstrap mode that has to be turned off until HAL
 *     phase 1 runs (the ARM64 DAIF/PMR split is what phase 0/1 exist for
 *     there).
 *
 * An empty body is therefore the complete implementation, not a placeholder.
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
ExArchPostHalInitSystemPhase0(VOID)
{
}

CODE_SEG("INIT")
VOID
NTAPI
ExArchPostHalInitSystemPhase1(VOID)
{
}

/* EOF */
