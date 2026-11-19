/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Moves of desktop areas reported by the remote display
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <assert.h>
#include <string.h>
#include "framemove.h"

BOOL FrameMoveClip(FRAME_MOVE *move, LONG width, LONG height)
{
    LONGLONG dx = (LONGLONG)move->SourceLeft - move->Dest.left, dy = (LONGLONG)move->SourceTop - move->Dest.top;
    LONGLONG left = max((LONGLONG)move->Dest.left, max(0, -dx)), top = max((LONGLONG)move->Dest.top, max(0, -dy));
    LONGLONG right = min((LONGLONG)move->Dest.right, min((LONGLONG)width, width - dx));
    LONGLONG bottom = min((LONGLONG)move->Dest.bottom, min((LONGLONG)height, height - dy));
    if (left >= right || top >= bottom || (dx == 0 && dy == 0)) return FALSE;
    move->Dest.left = (LONG)left; move->Dest.top = (LONG)top;
    move->Dest.right = (LONG)right; move->Dest.bottom = (LONG)bottom;
    move->SourceLeft = (LONG)(left + dx); move->SourceTop = (LONG)(top + dy);
    assert(move->SourceLeft >= 0 && move->SourceLeft + (move->Dest.right - move->Dest.left) <= width);
    assert(move->SourceTop >= 0 && move->SourceTop + (move->Dest.bottom - move->Dest.top) <= height);
    return TRUE;
}

BOOL FrameMoveMatches(const BYTE *previous, const BYTE *current, UINT32 stride, const FRAME_MOVE *move)
{
    LONG row, height = move->Dest.bottom - move->Dest.top, matching = 0;
    size_t bytes = (size_t)(move->Dest.right - move->Dest.left) * 4;
    for (row = 0; row < height; ++row)
        if (!memcmp(previous + (size_t)(move->SourceTop + row) * stride + (size_t)move->SourceLeft * 4,
                    current + (size_t)(move->Dest.top + row) * stride + (size_t)move->Dest.left * 4, bytes))
            ++matching;
    return matching * 2 >= height;
}

void FrameMoveApply(BYTE *pixels, UINT32 stride, const FRAME_MOVE *move)
{
    LONG row, height = move->Dest.bottom - move->Dest.top;
    size_t bytes = (size_t)(move->Dest.right - move->Dest.left) * 4;
    BOOL down = move->Dest.top > move->SourceTop;
    for (row = 0; row < height; ++row)
    {
        LONG index = down ? height - 1 - row : row;
        memmove(pixels + (size_t)(move->Dest.top + index) * stride + (size_t)move->Dest.left * 4,
                pixels + (size_t)(move->SourceTop + index) * stride + (size_t)move->SourceLeft * 4, bytes);
    }
}
