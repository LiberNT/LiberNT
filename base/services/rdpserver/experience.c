/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Visual experience of a remote session from the client's performance flags
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <assert.h>
#include <freerdp/settings_types.h>
#include "experience.h"

typedef struct EXPERIENCE_EFFECT
{
    UINT Get, Set;
    BOOL ByUiParam;
} EXPERIENCE_EFFECT;

static const EXPERIENCE_EFFECT Effects[EXPERIENCE_EFFECTS] =
{
    { SPI_GETFONTSMOOTHING, SPI_SETFONTSMOOTHING, TRUE },
    { SPI_GETMENUANIMATION, SPI_SETMENUANIMATION, FALSE },
    { SPI_GETCOMBOBOXANIMATION, SPI_SETCOMBOBOXANIMATION, FALSE },
    { SPI_GETLISTBOXSMOOTHSCROLLING, SPI_SETLISTBOXSMOOTHSCROLLING, FALSE },
    { SPI_GETMENUFADE, SPI_SETMENUFADE, FALSE },
    { SPI_GETTOOLTIPANIMATION, SPI_SETTOOLTIPANIMATION, FALSE },
    { SPI_GETUIEFFECTS, SPI_SETUIEFFECTS, FALSE },
    { SPI_GETCLIENTAREAANIMATION, SPI_SETCLIENTAREAANIMATION, FALSE }
};

void ExperienceFromClient(UINT32 performanceFlags, UINT32 connectionType, RDP_EXPERIENCE *experience)
{
    experience->Wallpaper = !(performanceFlags & PERF_DISABLE_WALLPAPER);
    experience->DragFullWindows = !(performanceFlags & PERF_DISABLE_FULLWINDOWDRAG);
    experience->Effects = connectionType < CONNECTION_TYPE_MODEM || connectionType > CONNECTION_TYPE_BROADBAND_HIGH;
}

static BOOL GetFlag(UINT action, BOOL *value)
{
    *value = FALSE;
    return SystemParametersInfoW(action, 0, value, 0);
}

static BOOL SetEffect(const EXPERIENCE_EFFECT *effect, BOOL value)
{
    BOOL current;
    if (GetFlag(effect->Get, &current) && !current == !value) return TRUE;
    return effect->ByUiParam ? SystemParametersInfoW(effect->Set, value, NULL, SPIF_SENDCHANGE)
                             : SystemParametersInfoW(effect->Set, 0, (PVOID)(ULONG_PTR)value, SPIF_SENDCHANGE);
}

static BOOL SetDragFullWindows(BOOL value)
{
    BOOL current;
    if (GetFlag(SPI_GETDRAGFULLWINDOWS, &current) && !current == !value) return TRUE;
    return SystemParametersInfoW(SPI_SETDRAGFULLWINDOWS, value, NULL, SPIF_SENDCHANGE);
}

static BOOL SetWallpaper(const WCHAR *path)
{
    WCHAR current[MAX_PATH + 1];
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, ARRAYSIZE(current), current, 0) && !lstrcmpiW(current, path))
        return TRUE;
    return SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)path, SPIF_SENDCHANGE);
}

BOOL ExperienceApply(RDP_EXPERIENCE_STATE *state, const RDP_EXPERIENCE *experience)
{
    BOOL ok = TRUE;
    UINT i;
    assert(state && experience);
    if (!state->Saved)
    {
        if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, ARRAYSIZE(state->Wallpaper), state->Wallpaper, 0) ||
            !GetFlag(SPI_GETDRAGFULLWINDOWS, &state->DragFullWindows))
            return FALSE;
        for (i = 0; i < EXPERIENCE_EFFECTS; ++i)
            if (!GetFlag(Effects[i].Get, &state->Effects[i])) return FALSE;
        state->Saved = TRUE;
    }
    ok &= SetWallpaper(experience->Wallpaper ? state->Wallpaper : L"");
    ok &= SetDragFullWindows(experience->DragFullWindows && state->DragFullWindows);
    for (i = 0; i < EXPERIENCE_EFFECTS; ++i)
        ok &= SetEffect(&Effects[i], experience->Effects && state->Effects[i]);
    return ok;
}

BOOL ExperienceRestore(RDP_EXPERIENCE_STATE *state)
{
    BOOL ok = TRUE;
    UINT i;
    if (!state->Saved) return TRUE;
    ok &= SetWallpaper(state->Wallpaper);
    ok &= SetDragFullWindows(state->DragFullWindows);
    for (i = 0; i < EXPERIENCE_EFFECTS; ++i)
        ok &= SetEffect(&Effects[i], state->Effects[i]);
    state->Saved = FALSE;
    return ok;
}
