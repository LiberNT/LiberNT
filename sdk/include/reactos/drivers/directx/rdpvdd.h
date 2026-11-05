/*
 * PROJECT:     LiberNT Remote Display Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private escape between the RDP server and the remote display miniport
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#define RDPVDD_ESCAPE_SET_MODE   0x31444456
#define RDPVDD_ESCAPE_OPEN_FRAME 0x32444456
#define RDPVDD_ESCAPE_READ_FRAME 0x33444456

#define RDPVDD_MIN_WIDTH  200
#define RDPVDD_MIN_HEIGHT 200
#define RDPVDD_MAX_WIDTH  8192
#define RDPVDD_MAX_HEIGHT 8192

#define RDPVDD_FRAME_MAX_RECTS 16

typedef struct _RDPVDD_ESCAPE
{
    ULONG Code;
    ULONG Width;
    ULONG Height;
} RDPVDD_ESCAPE, *PRDPVDD_ESCAPE;

typedef struct _RDPVDD_FRAME_ESCAPE
{
    ULONG Code;
    ULONG Reserved;
    ULONG64 Event;
    ULONG64 Buffer;
    ULONG BufferWidth;
    ULONG BufferHeight;
    ULONG Width;
    ULONG Height;
    ULONG RectCount;
    RECT Rects[RDPVDD_FRAME_MAX_RECTS];
} RDPVDD_FRAME_ESCAPE, *PRDPVDD_FRAME_ESCAPE;
