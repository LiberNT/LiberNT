/*
 * PROJECT:     LiberNT Boot Font Renderer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Proportional boot status face
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windef.h>
#include <bootfont/bootfont.h>
#include "bootfont_ui_data.h"

int
BootFontInitializeUi(
    _Inout_ PBOOT_FONT_RENDERER Renderer,
    _In_ unsigned long PixelHeight)
{
    return BootFontInitializeFace(Renderer, bootfont_ui_data, bootfont_ui_data_SIZE, PixelHeight);
}
