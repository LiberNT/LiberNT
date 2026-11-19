/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Find the part of the desktop that keeps changing, to send it as video
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "videodetect.h"

BOOL VideoTrackerReset(VIDEO_TRACKER *tracker, UINT32 width, UINT32 height)
{
    UINT32 tiles;
    VideoTrackerFree(tracker);
    assert(width && height);
    tracker->Width = width;
    tracker->Height = height;
    tracker->Columns = (width + VIDEO_TILE - 1) / VIDEO_TILE;
    tracker->Rows = (height + VIDEO_TILE - 1) / VIDEO_TILE;
    tiles = tracker->Columns * tracker->Rows;
    tracker->First = calloc(tiles, sizeof(*tracker->First));
    tracker->Last = calloc(tiles, sizeof(*tracker->Last));
    if (tracker->First && tracker->Last) return TRUE;
    VideoTrackerFree(tracker);
    return FALSE;
}

void VideoTrackerFree(VIDEO_TRACKER *tracker)
{
    free(tracker->First);
    free(tracker->Last);
    memset(tracker, 0, sizeof(*tracker));
}

void VideoTrackerChanged(VIDEO_TRACKER *tracker, ULONGLONG now, const RECT *changed)
{
    UINT32 x, y, left, top, right, bottom;
    RECT overlap;
    if (!tracker->First || changed->left >= changed->right || changed->top >= changed->bottom) return;
    assert(changed->left >= 0 && changed->top >= 0 && (UINT32)changed->right <= tracker->Width &&
           (UINT32)changed->bottom <= tracker->Height);
    left = changed->left / VIDEO_TILE;
    top = changed->top / VIDEO_TILE;
    right = (changed->right - 1) / VIDEO_TILE;
    bottom = (changed->bottom - 1) / VIDEO_TILE;
    for (y = top; y <= bottom; ++y)
        for (x = left; x <= right; ++x)
        {
            UINT32 tile = y * tracker->Columns + x;
            if (!tracker->Last[tile] || now - tracker->Last[tile] > VIDEO_GAP_MS) tracker->First[tile] = now;
            tracker->Last[tile] = now;
        }
    if (tracker->Active && IntersectRect(&overlap, &tracker->Region, changed)) tracker->RegionChanged = now;
}

void VideoTrackerStop(VIDEO_TRACKER *tracker, RECT *ended)
{
    SetRectEmpty(ended);
    if (!tracker->Active) return;
    *ended = tracker->Region;
    tracker->Active = FALSE;
    SetRectEmpty(&tracker->Region);
}

BOOL VideoTrackerUpdate(VIDEO_TRACKER *tracker, ULONGLONG now, RECT *ended)
{
    UINT32 x, y, hot = 0;
    RECT bounds;
    SetRectEmpty(ended);
    if (!tracker->First) return FALSE;
    if (tracker->Active && now - tracker->RegionChanged >= VIDEO_IDLE_MS)
    {
        VideoTrackerStop(tracker, ended);
        return FALSE;
    }
    SetRectEmpty(&bounds);
    for (y = 0; y < tracker->Rows; ++y)
        for (x = 0; x < tracker->Columns; ++x)
        {
            UINT32 tile = y * tracker->Columns + x;
            RECT rect;
            if (!tracker->Last[tile] || now - tracker->Last[tile] > VIDEO_GAP_MS ||
                now - tracker->First[tile] < VIDEO_DETECT_MS)
                continue;
            SetRect(&rect, x * VIDEO_TILE, y * VIDEO_TILE, min((x + 1) * VIDEO_TILE, tracker->Width),
                    min((y + 1) * VIDEO_TILE, tracker->Height));
            UnionRect(&bounds, &bounds, &rect);
            ++hot;
        }
    if (hot >= VIDEO_MIN_TILES)
    {
        if (!tracker->Active)
        {
            tracker->Active = TRUE;
            tracker->Region = bounds;
            tracker->RegionChanged = now;
        }
        else
            UnionRect(&tracker->Region, &tracker->Region, &bounds);
    }
    return tracker->Active;
}

BOOL VideoTrackerCovers(const VIDEO_TRACKER *tracker, const RECT *rect)
{
    return tracker->Active && rect->left >= tracker->Region.left && rect->top >= tracker->Region.top &&
           rect->right <= tracker->Region.right && rect->bottom <= tracker->Region.bottom;
}
