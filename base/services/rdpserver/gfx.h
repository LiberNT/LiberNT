/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Graphics pipeline (MS-RDPEGFX) output of the remote desktop
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once
#include <windows.h>
#include <freerdp/freerdp.h>
#include "framemove.h"

typedef struct RDP_GFX RDP_GFX;
typedef void (*GFX_LOG)(const char *, ...);

typedef struct GFX_TILE
{
    UINT16 Left, Top, Width, Height;
} GFX_TILE;

RDP_GFX *GfxCreate(HANDLE vcm, rdpContext *rdp, GFX_LOG log);
void GfxFree(RDP_GFX *gfx);
BOOL GfxOpen(RDP_GFX *gfx);
UINT32 GfxChannelId(const RDP_GFX *gfx);
BOOL GfxChannelCreated(RDP_GFX *gfx, INT32 status);
HANDLE GfxEventHandle(const RDP_GFX *gfx);
BOOL GfxHandleMessages(RDP_GFX *gfx);
BOOL GfxActive(const RDP_GFX *gfx);
BOOL GfxSurfaceReady(const RDP_GFX *gfx, UINT32 width, UINT32 height);
BOOL GfxResetSurface(RDP_GFX *gfx, UINT32 width, UINT32 height);
BOOL GfxFrameAllowed(const RDP_GFX *gfx);
BOOL GfxSendFrame(RDP_GFX *gfx, const BYTE *pixels, UINT32 stride, const FRAME_MOVE *moves, UINT32 moveCount,
                  const GFX_TILE *tiles, UINT32 count);
