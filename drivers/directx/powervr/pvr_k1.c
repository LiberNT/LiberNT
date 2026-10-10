/*
 * PROJECT:     LiberNT PowerVR Rogue WDDM miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SpacemiT K1 platform entry of the render-only miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "powervr.h"
#include "pvr_k1.h"

static NTSTATUS
K1PowerUp(_Inout_ PPOWERVR_ADAPTER Adapter)
{
    return PowerVrK1PowerUp(&Adapter->PlatformRegisters);
}

static VOID
K1PowerDown(_Inout_ PPOWERVR_ADAPTER Adapter)
{
    PowerVrK1PowerDown(&Adapter->PlatformRegisters);
}

const POWERVR_PLATFORM PowerVrK1Platform =
{
    L"FDT\\spacemit_k1-gpu",
    "SpacemiT K1",
    POWERVR_K1_CORE_CLOCK_HZ,
    PowerVrK1Compatible,
    POWERVR_K1_COMPATIBLE_COUNT,
    K1PowerUp,
    K1PowerDown,
};
