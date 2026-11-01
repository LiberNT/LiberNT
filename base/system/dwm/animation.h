/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Opacity of the content a maximize animation replaces
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

static inline ULONG
DwmAnimationSourceAlpha(const DWM_WIN *Source, const DWM_WIN *Window)
{
    LONGLONG Width = (LONGLONG)Window->cx - Source->cx;
    LONGLONG Height = (LONGLONG)Window->cy - Source->cy;
    LONGLONG Total = Width, Remaining = (LONGLONG)Window->cx - Window->AnimCx, Alpha;

    if ((Height < 0 ? -Height : Height) > (Width < 0 ? -Width : Width))
    {
        Total = Height;
        Remaining = (LONGLONG)Window->cy - Window->AnimCy;
    }
    if (Total == 0)
        return 0;
    Alpha = Remaining * 255 / Total;
    if (Alpha <= 0)
        return 0;
    return Alpha >= 255 ? 255 : (ULONG)Alpha;
}
