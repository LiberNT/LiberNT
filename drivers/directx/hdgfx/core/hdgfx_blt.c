/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Ironlake 2D blitter commands for the render command streamer
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"
#include "hdgfx_regs.h"

#define HDGFX_BLT_CLIENT_2D         2u
#define HDGFX_BLT_XY_COLOR          0x50u
#define HDGFX_BLT_XY_SRC_COPY       0x53u
#define HDGFX_BLT_DEPTH_32          3u
#define HDGFX_BLT_ROP_PATCOPY       0xF0u
#define HDGFX_BLT_ROP_SRCCOPY       0xCCu

static uint32_t
HdgfxBltHeader(
    uint32_t Opcode,
    uint32_t Dwords)
{
    return HDGFX_SET(BLT_CLIENT, HDGFX_BLT_CLIENT_2D) | HDGFX_SET(BLT_OPCODE, Opcode) |
           HDGFX_SET(BLT_WRITE_ALPHA, 1) | HDGFX_SET(BLT_WRITE_RGB, 1) | HDGFX_SET(MI_LENGTH, Dwords - 2);
}

static uint32_t
HdgfxBltPoint(
    uint32_t X,
    uint32_t Y)
{
    return HDGFX_SET(BLT_Y, Y) | HDGFX_SET(BLT_X, X);
}

static bool
HdgfxBltValid(
    const HDGFX_BLT_SURFACE *Surface,
    const HDGFX_BLT_RECT *Rect)
{
    return Surface->Pitch != 0 && Surface->Pitch <= 0x7FFF && Surface->Pitch % 4 == 0 &&
           Rect->Left < Rect->Right && Rect->Top < Rect->Bottom &&
           Rect->Right <= 0x7FFF && Rect->Bottom <= 0x7FFF;
}

uint32_t
HdgfxBltFill(
    uint32_t *Dwords,
    const HDGFX_BLT_SURFACE *Destination,
    const HDGFX_BLT_RECT *Rect,
    uint32_t Color)
{
    if (!HdgfxBltValid(Destination, Rect))
        return 0;

    Dwords[0] = HdgfxBltHeader(HDGFX_BLT_XY_COLOR, 6);
    Dwords[1] = HDGFX_SET(BLT_COLOR_DEPTH, HDGFX_BLT_DEPTH_32) | HDGFX_SET(BLT_ROP, HDGFX_BLT_ROP_PATCOPY) |
                HDGFX_SET(BLT_PITCH, Destination->Pitch);
    Dwords[2] = HdgfxBltPoint(Rect->Left, Rect->Top);
    Dwords[3] = HdgfxBltPoint(Rect->Right, Rect->Bottom);
    Dwords[4] = Destination->Address;
    Dwords[5] = Color;
    return 6;
}

uint32_t
HdgfxBltCopy(
    uint32_t *Dwords,
    const HDGFX_BLT_SURFACE *Destination,
    const HDGFX_BLT_RECT *Rect,
    const HDGFX_BLT_SURFACE *Source,
    uint32_t SourceX,
    uint32_t SourceY)
{
    HDGFX_BLT_RECT SourceRect;

    if (!HdgfxBltValid(Destination, Rect) || SourceX > 0x7FFF || SourceY > 0x7FFF)
        return 0;
    SourceRect.Left = SourceX;
    SourceRect.Top = SourceY;
    SourceRect.Right = SourceX + (Rect->Right - Rect->Left);
    SourceRect.Bottom = SourceY + (Rect->Bottom - Rect->Top);
    if (!HdgfxBltValid(Source, &SourceRect))
        return 0;

    Dwords[0] = HdgfxBltHeader(HDGFX_BLT_XY_SRC_COPY, 8);
    Dwords[1] = HDGFX_SET(BLT_COLOR_DEPTH, HDGFX_BLT_DEPTH_32) | HDGFX_SET(BLT_ROP, HDGFX_BLT_ROP_SRCCOPY) |
                HDGFX_SET(BLT_PITCH, Destination->Pitch);
    Dwords[2] = HdgfxBltPoint(Rect->Left, Rect->Top);
    Dwords[3] = HdgfxBltPoint(Rect->Right, Rect->Bottom);
    Dwords[4] = Destination->Address;
    Dwords[5] = HdgfxBltPoint(SourceX, SourceY);
    Dwords[6] = HDGFX_SET(BLT_PITCH, Source->Pitch);
    Dwords[7] = Source->Address;
    return 8;
}
