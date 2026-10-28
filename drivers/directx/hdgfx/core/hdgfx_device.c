/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device identification and shared display arithmetic
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"

#define HDGFX_VENDOR_INTEL 0x8086

static const HDGFX_GENERATION HdgfxIronlake =
{
    "Ironlake",
    5,
    HdgfxIlkReadDisplayState
};

static const struct
{
    uint16_t DeviceId;
    const HDGFX_GENERATION *Generation;
} HdgfxDevices[] =
{
    { 0x0042, &HdgfxIronlake },
    { 0x0046, &HdgfxIronlake },
};

const HDGFX_GENERATION *
HdgfxFindGeneration(
    uint16_t VendorId,
    uint16_t DeviceId)
{
    size_t Index;

    if (VendorId != HDGFX_VENDOR_INTEL)
        return NULL;
    for (Index = 0; Index < sizeof(HdgfxDevices) / sizeof(HdgfxDevices[0]); Index++)
    {
        if (HdgfxDevices[Index].DeviceId == DeviceId)
            return HdgfxDevices[Index].Generation;
    }
    return NULL;
}

uint32_t
HdgfxRefreshMilliHz(
    const HDGFX_TIMINGS *Timings,
    uint32_t DotKHz)
{
    uint64_t Pixels = (uint64_t)Timings->HTotal * Timings->VTotal;

    return Pixels ? (uint32_t)(((uint64_t)DotKHz * 1000000 + Pixels / 2) / Pixels) : 0;
}
