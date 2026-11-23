/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Keyed hash that names a tile in the graphics cache
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#include <windows.h>

typedef struct TILE_KEY_SECRET
{
    UINT64 K0, K1;
} TILE_KEY_SECRET;

BOOL TileKeySecret(TILE_KEY_SECRET *secret);
UINT64 TileKeyOf(const TILE_KEY_SECRET *secret, const BYTE *pixels, UINT32 stride, UINT32 left, UINT32 top,
                 UINT32 width, UINT32 height);
