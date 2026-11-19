/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Visual experience of a remote session from the client's performance flags
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once
#include <windows.h>

#define EXPERIENCE_EFFECTS 8

typedef struct RDP_EXPERIENCE
{
    BOOL Wallpaper, DragFullWindows, Effects;
} RDP_EXPERIENCE;

typedef struct RDP_EXPERIENCE_STATE
{
    BOOL Saved;
    WCHAR Wallpaper[MAX_PATH + 1];
    BOOL DragFullWindows;
    BOOL Effects[EXPERIENCE_EFFECTS];
} RDP_EXPERIENCE_STATE;

void ExperienceFromClient(UINT32 performanceFlags, UINT32 connectionType, RDP_EXPERIENCE *experience);
BOOL ExperienceApply(RDP_EXPERIENCE_STATE *state, const RDP_EXPERIENCE *experience);
BOOL ExperienceRestore(RDP_EXPERIENCE_STATE *state);
