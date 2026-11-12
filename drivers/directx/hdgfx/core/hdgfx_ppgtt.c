/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Sandy Bridge and Ivy Bridge per-process GTT layout and enable sequences
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#define PPGTT_PAGE_SIZE             4096u
#define PPGTT_MASKED_ENABLE(Bits)   (((uint32_t)(Bits) << 16) | (uint32_t)(Bits))
#define PPGTT_MASKED_DISABLE(Bits)  ((uint32_t)(Bits) << 16)
#define PPGTT_GFX_MODE_ENABLE       0x200u
#define PPGTT_DCLV_ALL              0xFFFFFFFFu
#define PPGTT_RINGS                 3u
#define PPGTT_CACHELINE_ENTRIES     16u
#define PPGTT_DIRECTORY_BASE_SHIFT  16u
#define PPGTT_DIRECTORY_BASE_LIMIT  0x8000u
#define PPGTT_PDE_VALID             0x1u
#define PPGTT_PDE_HIGH_SHIFT        4u
#define PPGTT_PHYSICAL_LIMIT        (1ull << 40)
#define SNB_ECOBITS                 0x2000u
#define SNB_ECOBITS_PPGTT_CACHE64B  0x300u
#define SNB_GAB_CONTINUE_AFTER_FAULT 0x100u
#define SNB_ECOCHK                  0x400u
#define SNB_ECOCHK_PPGTT_CACHE64B   0x18u
#define IVB_ECOCHK_PPGTT_LLC        0x8u
#define IVB_ECOCHK_PPGTT_GFDT       0x10u

bool
HdgfxPpgttLayout(
    uint32_t GttEntries,
    uint32_t MappedEntries,
    HDGFX_PPGTT_LAYOUT *Layout)
{
    uint32_t SlotFirst;
    uint32_t SlotCount;

    if (MappedEntries == 0 || MappedEntries > HDGFX_PPGTT_DIRECTORY_ENTRIES * HDGFX_PPGTT_TABLE_ENTRIES ||
        GttEntries > PPGTT_DIRECTORY_BASE_LIMIT * PPGTT_CACHELINE_ENTRIES)
    {
        return false;
    }
    SlotFirst = (MappedEntries + HDGFX_PPGTT_DIRECTORY_ENTRIES - 1) / HDGFX_PPGTT_DIRECTORY_ENTRIES *
                HDGFX_PPGTT_DIRECTORY_ENTRIES;
    if (SlotFirst >= GttEntries)
        return false;
    SlotCount = (GttEntries - SlotFirst) / HDGFX_PPGTT_DIRECTORY_ENTRIES;
    if (SlotCount < 2)
        return false;
    Layout->MappedEntries = MappedEntries;
    Layout->Tables = (MappedEntries + HDGFX_PPGTT_TABLE_ENTRIES - 1) / HDGFX_PPGTT_TABLE_ENTRIES;
    Layout->SlotFirst = SlotFirst;
    Layout->SlotCount = SlotCount;
    return true;
}

uint32_t
HdgfxPpgttDirectoryEntry(
    const HDGFX_PPGTT_LAYOUT *Layout,
    uint32_t Slot)
{
    return Layout->SlotFirst + Slot * HDGFX_PPGTT_DIRECTORY_ENTRIES;
}

uint32_t
HdgfxPpgttDirectoryBase(
    const HDGFX_PPGTT_LAYOUT *Layout,
    uint32_t Slot)
{
    return HdgfxPpgttDirectoryEntry(Layout, Slot) / PPGTT_CACHELINE_ENTRIES << PPGTT_DIRECTORY_BASE_SHIFT;
}

uint32_t
HdgfxPpgttPde(
    uint64_t Table)
{
    if (Table % PPGTT_PAGE_SIZE != 0 || Table >= PPGTT_PHYSICAL_LIMIT)
        return 0;
    return ((uint32_t)Table & ~(uint32_t)(PPGTT_PAGE_SIZE - 1)) |
           ((uint32_t)(Table >> 32) << PPGTT_PDE_HIGH_SHIFT) | PPGTT_PDE_VALID;
}

static void
PpgttLoadDirectory(
    const HDGFX_MMIO *Mmio,
    uint32_t DirectoryBase,
    bool RingMode)
{
    uint32_t Ring;

    for (Ring = 0; Ring < PPGTT_RINGS; Ring++)
    {
        Mmio->Write(Mmio->Context, HDGFX_REG(PP_DCLV, Ring), PPGTT_DCLV_ALL);
        Mmio->Write(Mmio->Context, HDGFX_REG(PP_DIR_BASE, Ring), DirectoryBase);
        if (RingMode)
            Mmio->Write(Mmio->Context, HDGFX_REG(RING_MODE_GEN7, Ring), PPGTT_MASKED_ENABLE(PPGTT_GFX_MODE_ENABLE));
    }
}

void
HdgfxSnbEnablePpgtt(
    const HDGFX_MMIO *Mmio,
    uint32_t DirectoryBase)
{
    Mmio->Write(Mmio->Context, HDGFX_REG(GAC_ECO_BITS, 0),
                Mmio->Read(Mmio->Context, HDGFX_REG(GAC_ECO_BITS, 0)) | SNB_ECOBITS | SNB_ECOBITS_PPGTT_CACHE64B);
    Mmio->Write(Mmio->Context, HDGFX_REG(GAB_CTL, 0),
                Mmio->Read(Mmio->Context, HDGFX_REG(GAB_CTL, 0)) | SNB_GAB_CONTINUE_AFTER_FAULT);
    Mmio->Write(Mmio->Context, HDGFX_REG(GAM_ECOCHK, 0),
                Mmio->Read(Mmio->Context, HDGFX_REG(GAM_ECOCHK, 0)) | SNB_ECOCHK | SNB_ECOCHK_PPGTT_CACHE64B);
    Mmio->Write(Mmio->Context, HDGFX_REG(SNB_GFX_MODE, 0), PPGTT_MASKED_ENABLE(PPGTT_GFX_MODE_ENABLE));
    PpgttLoadDirectory(Mmio, DirectoryBase, false);
}

void
HdgfxSnbDisablePpgtt(
    const HDGFX_MMIO *Mmio)
{
    Mmio->Write(Mmio->Context, HDGFX_REG(SNB_GFX_MODE, 0), PPGTT_MASKED_DISABLE(PPGTT_GFX_MODE_ENABLE));
}

void
HdgfxIvbEnablePpgtt(
    const HDGFX_MMIO *Mmio,
    uint32_t DirectoryBase)
{
    Mmio->Write(Mmio->Context, HDGFX_REG(GAC_ECO_BITS, 0),
                Mmio->Read(Mmio->Context, HDGFX_REG(GAC_ECO_BITS, 0)) | SNB_ECOBITS_PPGTT_CACHE64B);
    Mmio->Write(Mmio->Context, HDGFX_REG(GAM_ECOCHK, 0),
                (Mmio->Read(Mmio->Context, HDGFX_REG(GAM_ECOCHK, 0)) | IVB_ECOCHK_PPGTT_LLC) &
                    ~IVB_ECOCHK_PPGTT_GFDT);
    PpgttLoadDirectory(Mmio, DirectoryBase, true);
}

void
HdgfxIvbDisablePpgtt(
    const HDGFX_MMIO *Mmio)
{
    uint32_t Ring;

    for (Ring = 0; Ring < PPGTT_RINGS; Ring++)
        Mmio->Write(Mmio->Context, HDGFX_REG(RING_MODE_GEN7, Ring), PPGTT_MASKED_DISABLE(PPGTT_GFX_MODE_ENABLE));
}
