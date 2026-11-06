/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Builds the composited pointer's image and placement
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#define DWM_POINTER_MAX_SIZE 256

static __inline VOID
DwmPointerRect(RECT *Rect, LONG X, LONG Y, LONG HotX, LONG HotY, LONG Width, LONG Height)
{
    Rect->left = X - HotX;
    Rect->top = Y - HotY;
    Rect->right = Rect->left + Width;
    Rect->bottom = Rect->top + Height;
}

/* The black/white difference gives coverage; a pixel darker over white inverts. */
static __inline BOOL
DwmPointerConvert(const ULONG *OnBlack, const ULONG *OnWhite, ULONG Count, ULONG *Image, ULONG *Invert)
{
    BOOL Inverts = FALSE;
    ULONG Index;

    for (Index = 0; Index < Count; ++Index)
    {
        ULONG Black = OnBlack[Index] & 0x00FFFFFF, White = OnWhite[Index] & 0x00FFFFFF;
        LONG Red = (LONG)((White >> 16) & 0xFF) - (LONG)((Black >> 16) & 0xFF);
        LONG Green = (LONG)((White >> 8) & 0xFF) - (LONG)((Black >> 8) & 0xFF);
        LONG Blue = (LONG)(White & 0xFF) - (LONG)(Black & 0xFF);
        LONG Coverage = Red > Green ? Red : Green;

        Coverage = Coverage > Blue ? Coverage : Blue;
        if (Coverage < 0)
        {
            Image[Index] = 0;
            Invert[Index] = 0xFFFFFFFF;
            Inverts = TRUE;
        }
        else
        {
            Image[Index] = ((ULONG)(255 - Coverage) << 24) | Black;
            Invert[Index] = 0;
        }
    }
    return Inverts;
}
