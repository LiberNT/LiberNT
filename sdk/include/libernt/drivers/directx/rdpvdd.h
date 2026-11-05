/*
 * PROJECT:     LiberNT Remote Display Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private escape between the RDP server and the remote display miniport
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#define RDPVDD_ESCAPE_SET_MODE 0x31444456

#define RDPVDD_MIN_WIDTH  200
#define RDPVDD_MIN_HEIGHT 200
#define RDPVDD_MAX_WIDTH  8192
#define RDPVDD_MAX_HEIGHT 8192

typedef struct _RDPVDD_ESCAPE
{
    ULONG Code;
    ULONG Width;
    ULONG Height;
} RDPVDD_ESCAPE, *PRDPVDD_ESCAPE;
