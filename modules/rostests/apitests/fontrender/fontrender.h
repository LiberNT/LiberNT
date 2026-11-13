/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared declarations of the font rendering tests
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#pragma once

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>

#define FR_TEXT L"Hamburgefonstiv 0123"

#define ok_gap(Gap, Cond, ...) \
    do { if (!(Cond) && (Gap)) trace("known gap, " __VA_ARGS__); else ok(Cond, __VA_ARGS__); } while (0)

typedef struct
{
    const WCHAR *Family;
    const WCHAR *Resource;
    WCHAR Path[MAX_PATH];
} FR_FONT;

extern FR_FONT FrFonts[2];
extern const INT FrSizes[6];

BOOL FrExtractFont(FR_FONT *Font);
VOID FrDeleteFont(FR_FONT *Font);
BOOL FrEmit(VOID);
VOID FrCoverage(const char *Name, const BYTE *Pixels, INT Width, INT Height, INT Stride, INT Channels, BOOL IsCoverage);
