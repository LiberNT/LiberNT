/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Choose the graphics pipeline (MS-RDPEGFX) capability set to confirm
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once

#include <windows.h>
#include <freerdp/channels/rdpgfx.h>

BOOL GfxChooseCaps(const RDPGFX_CAPSET *offered, UINT32 count, RDPGFX_CAPSET *confirm);
