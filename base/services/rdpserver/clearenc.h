/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ClearCodec (MS-RDPEGFX 2.2.4.1) encoder
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once
#include <windows.h>

#define CLEAR_MAX_PALETTE 127

typedef enum CLEAR_LAYER
{
    ClearResidual,
    ClearRlex
} CLEAR_LAYER;

UINT32 ClearPaletteSize(const BYTE *pixels, UINT32 stride, UINT32 width, UINT32 height, UINT32 limit);
BOOL ClearEncode(BYTE sequence, CLEAR_LAYER layer, const BYTE *pixels, UINT32 stride, UINT32 width, UINT32 height,
                 BYTE *output, UINT32 capacity, UINT32 *size);
