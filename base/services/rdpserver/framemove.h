/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Moves of desktop areas reported by the remote display
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once
#include <windows.h>

typedef struct FRAME_MOVE
{
    LONG SourceLeft, SourceTop;
    RECT Dest;
} FRAME_MOVE;

BOOL FrameMoveClip(FRAME_MOVE *move, LONG width, LONG height);
BOOL FrameMoveMatches(const BYTE *previous, const BYTE *current, UINT32 stride, const FRAME_MOVE *move);
void FrameMoveApply(BYTE *pixels, UINT32 stride, const FRAME_MOVE *move);
