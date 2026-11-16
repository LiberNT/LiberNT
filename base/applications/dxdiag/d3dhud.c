/*
 * PROJECT:     LiberNT DirectX Diagnostic Tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Statistics overlay drawn over the rotating mark
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <string.h>
#include <windef.h>

#include "d3dmark.h"

#define HUD_GLYPH_WIDTH 5
#define HUD_GLYPH_HEIGHT 7
#define HUD_MARGIN 8
#define HUD_PADDING 2

typedef struct
{
    CHAR Character;
    BYTE Rows[HUD_GLYPH_HEIGHT];
} HUD_GLYPH;

static const HUD_GLYPH HudFont[] =
{
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
    { '/', { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
};

static const BYTE *HudGlyph(CHAR Character)
{
    UINT i;

    for (i = 0; i < ARRAYSIZE(HudFont); ++i)
    {
        if (HudFont[i].Character == Character)
            return HudFont[i].Rows;
    }
    return NULL;
}

static LONG HudScale(UINT Height)
{
    return Height >= 400 ? (LONG)(Height / 200) : 2;
}

UINT D3DTestHudRects(RECT *Rects, UINT Capacity, const PCSTR *Lines, UINT LineCount, UINT Height)
{
    LONG Scale = HudScale(Height);
    LONG Columns = 0;
    UINT Count = 1, Line;

    if (!Capacity || !LineCount)
        return 0;

    for (Line = 0; Line < LineCount; ++Line)
        Columns = max(Columns, (LONG)strlen(Lines[Line]));
    Rects[0].left = HUD_MARGIN;
    Rects[0].top = HUD_MARGIN;
    Rects[0].right = HUD_MARGIN + (2 * HUD_PADDING + Columns * (HUD_GLYPH_WIDTH + 1) - 1) * Scale;
    Rects[0].bottom = HUD_MARGIN + (2 * HUD_PADDING + (LONG)LineCount * (HUD_GLYPH_HEIGHT + 2) - 2) * Scale;

    for (Line = 0; Line < LineCount; ++Line)
    {
        LONG Column;

        for (Column = 0; Lines[Line][Column]; ++Column)
        {
            const BYTE *Glyph = HudGlyph(Lines[Line][Column]);
            LONG Row;

            if (!Glyph)
                continue;
            for (Row = 0; Row < HUD_GLYPH_HEIGHT; ++Row)
            {
                LONG Bit = HUD_GLYPH_WIDTH - 1;

                while (Bit >= 0)
                {
                    LONG First;

                    if (!(Glyph[Row] & (1 << Bit)))
                    {
                        --Bit;
                        continue;
                    }
                    First = Bit;
                    while (Bit >= 0 && (Glyph[Row] & (1 << Bit)))
                        --Bit;
                    if (Count == Capacity)
                        return Count;
                    Rects[Count].left = HUD_MARGIN + HUD_PADDING * Scale + (Column * (HUD_GLYPH_WIDTH + 1) + HUD_GLYPH_WIDTH - 1 - First) * Scale;
                    Rects[Count].right = Rects[Count].left + (First - Bit) * Scale;
                    Rects[Count].top = HUD_MARGIN + HUD_PADDING * Scale + ((LONG)Line * (HUD_GLYPH_HEIGHT + 2) + Row) * Scale;
                    Rects[Count].bottom = Rects[Count].top + Scale;
                    ++Count;
                }
            }
        }
    }
    return Count;
}

static VOID HudQuad(PD3DTEST_VERTEX Vertices, const RECT *Rect, DWORD Color)
{
    static const UINT Corners[6] = { 0, 1, 2, 0, 2, 3 };
    UINT i;

    for (i = 0; i < 6; ++i)
    {
        Vertices[i].x = (FLOAT)((Corners[i] == 1 || Corners[i] == 2) ? Rect->right : Rect->left);
        Vertices[i].y = (FLOAT)(Corners[i] >= 2 ? Rect->bottom : Rect->top);
        Vertices[i].z = 0.0f;
        Vertices[i].rhw = 1.0f;
        Vertices[i].color = Color;
    }
}

UINT D3DTestBuildHud(PD3DTEST_VERTEX Vertices, UINT Capacity, const PCSTR *Lines, UINT LineCount, UINT Height)
{
    RECT Rects[D3DTEST_HUD_VERTEX_COUNT / 6];
    UINT Count, i;

    Count = D3DTestHudRects(Rects, min(Capacity / 6, (UINT)ARRAYSIZE(Rects)), Lines, LineCount, Height);
    for (i = 0; i < Count; ++i)
        HudQuad(Vertices + i * 6, &Rects[i], i ? D3DTEST_HUD_TEXT_COLOR : D3DTEST_HUD_BACK_COLOR);
    return Count * 6;
}
