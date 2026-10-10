/*
 * PROJECT:     LiberNT PowerVR Rogue WDDM miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SpacemiT K1 GPU power domain, clock and reset interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define POWERVR_K1_CORE_CLOCK_HZ    614400000ULL
#define POWERVR_K1_COMPATIBLE_COUNT 3

extern const char *const PowerVrK1Compatible[POWERVR_K1_COMPATIBLE_COUNT];

NTSTATUS
PowerVrK1PowerUp(_Inout_ PVOID *PlatformRegisters);

VOID
PowerVrK1PowerDown(_Inout_ PVOID *PlatformRegisters);
