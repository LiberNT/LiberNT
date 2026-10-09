/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Caption buttons drawn in extended window frames
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

ULONG
DwmFrameKey(const DWM_WIN *Window);

const ULONG *
DwmFrameButtonImage(const DWM_WIN *Window, ULONG Button, ULONG Background);

VOID
DwmFrameInterior(const DWM_WIN *Window, RECTL *Interior);

ULONG
DwmFrameButtonSignature(const DWM_WIN *Window, ULONG Background);

BOOL
DwmFrameButtonBounds(const DWM_WIN *Window, RECTL *Bounds);

ULONG *
DwmFrameButtonAtlas(const DWM_WIN *Window, ULONG Background, RECTL *Bounds);

#ifdef __cplusplus
}
#endif
