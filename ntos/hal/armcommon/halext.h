/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Services the HAL extensions register, as the ARM64 HAL consumes them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/hal/halext.h>

#if defined(_NTOSKRNL_) || defined(_NTSYSTEM_)
ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID);
#endif

/* NULL when a GIC drives interrupts. */
extern const HAL_ARM64_INTERRUPT_CONTROLLER *HalpArm64InterruptController;

/* NULL when config space is reached through ECAM/MCFG or _CBA. */
extern const HAL_ARM64_PCI_CONFIG_BACKEND *HalpArm64PciConfigBackend;

extern const HAL_ARM64_SYSTEM_RESET *HalpArm64SystemReset;

extern const HAL_ARM64_MSI_DOORBELL *HalpArm64MsiDoorbell;

extern const HAL_ARM64_PROCESSOR_IDLE *HalpArm64ProcessorIdle;

VOID
HalpArm64ProbePlatforms(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock);

VOID
HalpArm64Phase1PlatformInit(VOID);
