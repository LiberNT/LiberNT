/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Find the part of the desktop that keeps changing, to send it as video
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once
#include <windows.h>

#define VIDEO_TILE 64
#define VIDEO_GAP_MS 250
#define VIDEO_DETECT_MS 2000
#define VIDEO_IDLE_MS 1000
#define VIDEO_MIN_TILES 4

typedef struct VIDEO_TRACKER
{
    UINT32 Columns, Rows, Width, Height;
    ULONGLONG *First, *Last;
    BOOL Active;
    RECT Region;
    ULONGLONG RegionChanged;
} VIDEO_TRACKER;

BOOL VideoTrackerReset(VIDEO_TRACKER *tracker, UINT32 width, UINT32 height);
void VideoTrackerFree(VIDEO_TRACKER *tracker);
void VideoTrackerChanged(VIDEO_TRACKER *tracker, ULONGLONG now, const RECT *changed);
BOOL VideoTrackerUpdate(VIDEO_TRACKER *tracker, ULONGLONG now, RECT *ended);
void VideoTrackerStop(VIDEO_TRACKER *tracker, RECT *ended);
BOOL VideoTrackerCovers(const VIDEO_TRACKER *tracker, const RECT *rect);
