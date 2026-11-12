/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple AIC2/AIC3 interrupt backend probe
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "devtree.h"

extern BOOLEAN AppleAicDetected;

BOOLEAN
AppleAicInterruptProbe(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
AppleWdtProbe(
    _In_ PFDT Fdt);

VOID
AppleWdtPhase1MapRuntimeMmio(VOID);

VOID
AppleIdleProbe(VOID);
