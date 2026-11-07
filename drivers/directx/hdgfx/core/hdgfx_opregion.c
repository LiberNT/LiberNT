/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OpRegion ACPI and ASLE mailboxes
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"

#include <string.h>

#define OPREGION_SIGNATURE          "IntelGraphicsMem"
#define OPREGION_SIGNATURE_SIZE     16
#define OPREGION_MBOX               0x058
#define OPREGION_MBOX_ACPI          0x01
#define OPREGION_MBOX_ASLE          0x04
#define OPREGION_DRDY               0x100
#define OPREGION_CSTS               0x104
#define OPREGION_CEVT               0x108
#define OPREGION_DIDL               0x120
#define OPREGION_CADL               0x160
#define OPREGION_ARDY               0x300
#define OPREGION_ASLC               0x304
#define OPREGION_TCHE               0x308
#define OPREGION_BCLP               0x310
#define OPREGION_CBLV               0x318
#define OPREGION_READY              0x01
#define OPREGION_VALID              0x80000000u
#define OPREGION_TCHE_BLC           0x02
#define OPREGION_ASLC_REQUESTS      9
#define OPREGION_ASLC_BLC           1
#define OPREGION_ASLC_FAILED(Index) (1u << (10 + 2 * (Index)))
#define OPREGION_CSTS_SUCCESS       0
#define OPREGION_CSTS_FAILURE       1
#define OPREGION_CSTS_PENDING       2
#define OPREGION_CSTS_DISPATCHED    3
#define OPREGION_CEVT_MASK          0x07

static bool
OpRegionSigned(
    const HDGFX_MMIO *OpRegion)
{
    uint32_t Signature[OPREGION_SIGNATURE_SIZE / 4];
    uint32_t Index;

    for (Index = 0; Index < OPREGION_SIGNATURE_SIZE / 4; Index++)
        Signature[Index] = OpRegion->Read(OpRegion->Context, Index * 4);
    return memcmp(Signature, OPREGION_SIGNATURE, OPREGION_SIGNATURE_SIZE) == 0;
}

static uint32_t
LevelToPercent(
    uint32_t Level)
{
    return (Level * 100 + HDGFX_ASLE_LEVEL_MAX / 2) / HDGFX_ASLE_LEVEL_MAX;
}

bool
HdgfxOpRegionDriverReady(
    const HDGFX_MMIO *OpRegion,
    uint32_t DisplayId,
    uint32_t Percent)
{
    uint32_t Mailboxes;

    if (!OpRegionSigned(OpRegion))
        return false;
    Mailboxes = OpRegion->Read(OpRegion->Context, OPREGION_MBOX);
    if ((Mailboxes & (OPREGION_MBOX_ACPI | OPREGION_MBOX_ASLE)) != (OPREGION_MBOX_ACPI | OPREGION_MBOX_ASLE))
        return false;

    OpRegion->Write(OpRegion->Context, OPREGION_DIDL, DisplayId);
    OpRegion->Write(OpRegion->Context, OPREGION_DIDL + 4, 0);
    OpRegion->Write(OpRegion->Context, OPREGION_CADL, DisplayId);
    OpRegion->Write(OpRegion->Context, OPREGION_CADL + 4, 0);
    HdgfxOpRegionSetBrightness(OpRegion, Percent);
    OpRegion->Write(OpRegion->Context, OPREGION_TCHE, OPREGION_TCHE_BLC);
    OpRegion->Write(OpRegion->Context, OPREGION_ARDY, OPREGION_READY);
    OpRegion->Write(OpRegion->Context, OPREGION_DRDY, OPREGION_READY);
    return true;
}

void
HdgfxOpRegionDriverNotReady(
    const HDGFX_MMIO *OpRegion)
{
    OpRegion->Write(OpRegion->Context, OPREGION_DRDY, 0);
    OpRegion->Write(OpRegion->Context, OPREGION_ARDY, 0);
}

void
HdgfxOpRegionSetBrightness(
    const HDGFX_MMIO *OpRegion,
    uint32_t Percent)
{
    OpRegion->Write(OpRegion->Context, OPREGION_CBLV, (Percent > 100 ? 100 : Percent) | OPREGION_VALID);
}

bool
HdgfxOpRegionServiceAsle(
    const HDGFX_MMIO *OpRegion,
    HDGFX_SET_BACKLIGHT SetBacklight,
    void *Context)
{
    uint32_t Requests = OpRegion->Read(OpRegion->Context, OPREGION_ASLC);
    uint32_t Response = 0;
    uint32_t Index;
    uint32_t Level;

    if ((Requests & ((1u << OPREGION_ASLC_REQUESTS) - 1)) == 0)
        return false;

    for (Index = 0; Index < OPREGION_ASLC_REQUESTS; Index++)
    {
        if ((Requests & (1u << Index)) == 0)
            continue;
        if (Index == OPREGION_ASLC_BLC)
        {
            Level = OpRegion->Read(OpRegion->Context, OPREGION_BCLP);
            if ((Level & OPREGION_VALID) != 0 &&
                (Level & ~OPREGION_VALID) <= HDGFX_ASLE_LEVEL_MAX &&
                SetBacklight(Context, Level & ~OPREGION_VALID))
            {
                HdgfxOpRegionSetBrightness(OpRegion, LevelToPercent(Level & ~OPREGION_VALID));
                continue;
            }
        }
        Response |= OPREGION_ASLC_FAILED(Index);
    }
    OpRegion->Write(OpRegion->Context, OPREGION_ASLC, Response);
    return true;
}

bool
HdgfxOpRegionBeginNotification(
    const HDGFX_MMIO *OpRegion,
    uint32_t *Events)
{
    *Events = 0;
    if (OpRegion->Read(OpRegion->Context, OPREGION_CSTS) != OPREGION_CSTS_DISPATCHED)
        return false;

    OpRegion->Write(OpRegion->Context, OPREGION_CSTS, OPREGION_CSTS_PENDING);
    *Events = OpRegion->Read(OpRegion->Context, OPREGION_CEVT) & OPREGION_CEVT_MASK;
    return true;
}

void
HdgfxOpRegionEndNotification(
    const HDGFX_MMIO *OpRegion,
    bool Success)
{
    OpRegion->Write(OpRegion->Context, OPREGION_CSTS, Success ? OPREGION_CSTS_SUCCESS : OPREGION_CSTS_FAILURE);
}
