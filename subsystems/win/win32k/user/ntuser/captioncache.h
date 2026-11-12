/*
 * PROJECT:     ReactOS Win32k / Desktop Window Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Preserve caption layout with its retained front surface
 */
#pragma once

typedef struct _DWM_CAPTION_CACHE
{
    RECTL Buttons[DWM_CAPTION_BUTTONS];
    ULONG State;
    ULONG Generation;
    BOOL Valid;
} DWM_CAPTION_CACHE;

static VOID
IntCompositionCaptionCache(DWM_CAPTION_CACHE *Cache, BOOL Held, ULONG Generation, DWM_WIN *Window)
{
    if (Held && Cache->Valid && Cache->Generation == Generation)
    {
        ULONG State = Window->CaptionState;
        ULONG InteractionMask = (1u << (DWM_CAPTION_BUTTONS * 3)) - 1;

        RtlCopyMemory(Window->CaptionButtons, Cache->Buttons, sizeof(Cache->Buttons));
        Window->CaptionState = Cache->State;
        if ((State & DWM_CAPTION_STATE_VALID) && (Cache->State & DWM_CAPTION_STATE_VALID))
            Window->CaptionState = (Cache->State & ~InteractionMask) | (State & InteractionMask);
    }
    else if (!Held)
    {
        RtlCopyMemory(Cache->Buttons, Window->CaptionButtons, sizeof(Cache->Buttons));
        Cache->State = Window->CaptionState;
        Cache->Generation = Generation;
        Cache->Valid = TRUE;
    }
}
