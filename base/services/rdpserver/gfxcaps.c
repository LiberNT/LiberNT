/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Choose the graphics pipeline (MS-RDPEGFX) capability set to confirm
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "gfxcaps.h"

static const UINT32 KnownVersions[] =
{
    RDPGFX_CAPVERSION_107, RDPGFX_CAPVERSION_106, RDPGFX_CAPVERSION_105, RDPGFX_CAPVERSION_104,
    RDPGFX_CAPVERSION_103, RDPGFX_CAPVERSION_102, RDPGFX_CAPVERSION_101, RDPGFX_CAPVERSION_10,
    RDPGFX_CAPVERSION_81, RDPGFX_CAPVERSION_8
};

BOOL GfxChooseCaps(const RDPGFX_CAPSET *offered, UINT32 count, RDPGFX_CAPSET *confirm)
{
    UINT32 i, j;
    for (i = 0; i < ARRAYSIZE(KnownVersions); ++i)
        for (j = 0; j < count; ++j)
            if (offered[j].version == KnownVersions[i])
            {
                *confirm = offered[j];
                if (confirm->version == RDPGFX_CAPVERSION_101)
                {
                    confirm->length = 0x10;
                    confirm->flags = 0;
                }
                else
                {
                    confirm->length = 4;
                }
                return TRUE;
            }
    return FALSE;
}
