/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pins GDI text metrics and glyph boxes against Windows 11 and reports the coverage GDI draws
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "fontrender.h"
#include <stdio.h>

#define BITMAP_WIDTH  320
#define BITMAP_HEIGHT 48

#define GAP_EXTENT  0x1
#define GAP_ASCENT  0x2
#define GAP_ADVANCE 0x4
#define GAP_BOX     0x8

typedef struct
{
    INT Cx, Cy, Ascent, Descent;
    UINT BlackBoxX, BlackBoxY;
    INT OriginX, OriginY, CellIncX;
    ULONG Gaps;
} GDI_EXPECT;

static const BYTE Qualities[] = { NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY, CLEARTYPE_QUALITY };
static const char *QualityNames[] = { "mono", "gray", "cleartype" };

static const GDI_EXPECT Expected[] =
{
    { 90, 12, 10, 2, 5, 6, 0, 5, 5 },
    { 90, 12, 10, 2, 5, 6, 0, 5, 5 },
    { 90, 12, 10, 2, 5, 6, 0, 5, 5 },
    { 110, 15, 12, 3, 6, 7, 0, 6, 6 },
    { 110, 15, 12, 3, 6, 7, 0, 6, 6 },
    { 110, 15, 12, 3, 6, 7, 0, 6, 6 },
    { 119, 16, 13, 3, 7, 7, 0, 6, 7 },
    { 119, 16, 13, 3, 7, 7, 0, 6, 7 },
    { 119, 16, 13, 3, 7, 7, 0, 6, 7 },
    { 142, 19, 15, 4, 8, 8, 0, 7, 8 },
    { 142, 19, 15, 4, 8, 8, 0, 7, 8 },
    { 142, 19, 15, 4, 8, 8, 0, 7, 8 },
    { 161, 21, 17, 4, 9, 9, 0, 8, 9 },
    { 161, 21, 17, 4, 9, 9, 0, 8, 9 },
    { 161, 21, 17, 4, 9, 9, 0, 8, 9 },
    { 243, 32, 26, 6, 12, 13, 1, 12, 14 },
    { 243, 32, 26, 6, 12, 13, 1, 12, 14 },
    { 243, 32, 26, 6, 12, 13, 1, 12, 14 },
    { 89, 11, 8, 3, 5, 5, 0, 5, 5 },
    { 89, 11, 8, 3, 5, 5, 0, 5, 5 },
    { 89, 11, 8, 3, 5, 5, 0, 5, 5 },
    { 108, 13, 10, 3, 6, 6, 0, 6, 6 },
    { 108, 13, 10, 3, 6, 6, 0, 6, 6 },
    { 108, 13, 10, 3, 6, 6, 0, 6, 6 },
    { 120, 15, 11, 4, 7, 7, 0, 7, 6 },
    { 120, 15, 11, 4, 7, 7, 0, 7, 6 },
    { 120, 15, 11, 4, 7, 7, 0, 7, 6 },
    { 141, 17, 13, 4, 8, 8, 0, 8, 7 },
    { 141, 17, 13, 4, 8, 8, 0, 8, 7 },
    { 141, 17, 13, 4, 8, 8, 0, 8, 7 },
    { 161, 19, 14, 5, 9, 9, 0, 9, 9 },
    { 161, 19, 14, 5, 9, 9, 0, 9, 9 },
    { 161, 19, 14, 5, 9, 9, 0, 9, 9 },
    { 236, 29, 22, 7, 12, 13, 1, 13, 13, GAP_BOX },
    { 236, 29, 22, 7, 12, 13, 1, 13, 13, GAP_BOX },
    { 236, 29, 22, 7, 12, 13, 1, 13, 13, GAP_BOX },
};

static VOID
TestFont(HDC Dc, BYTE *Bits, FR_FONT *Font, INT Size, INT Quality, const GDI_EXPECT *Expect)
{
    static const MAT2 Identity = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } };
    char Name[64];
    WCHAR Face[LF_FACESIZE];
    TEXTMETRICW Metrics;
    GLYPHMETRICS Glyph;
    GDI_EXPECT Got;
    LOGFONTW LogFont;
    HFONT Handle, Old;
    SIZE Extent;
    RECT Rect = { 0, 0, BITMAP_WIDTH, BITMAP_HEIGHT };

    sprintf(Name, "gdi-%ls-%d-%s", Font->Family, Size, QualityNames[Quality]);

    ZeroMemory(&LogFont, sizeof(LogFont));
    LogFont.lfHeight = -Size;
    LogFont.lfWeight = FW_NORMAL;
    LogFont.lfCharSet = DEFAULT_CHARSET;
    LogFont.lfQuality = Qualities[Quality];
    wcscpy(LogFont.lfFaceName, Font->Family);
    Handle = CreateFontIndirectW(&LogFont);
    ok(Handle != NULL, "%s: CreateFontIndirectW failed\n", Name);
    if (!Handle)
        return;
    Old = SelectObject(Dc, Handle);

    GetTextFaceW(Dc, ARRAYSIZE(Face), Face);
    ok(!wcscmp(Face, Font->Family), "%s: selected %ls\n", Name, Face);

    ZeroMemory(&Got, sizeof(Got));
    ok(GetTextExtentPoint32W(Dc, FR_TEXT, wcslen(FR_TEXT), &Extent), "%s: GetTextExtentPoint32W failed\n", Name);
    ok(GetTextMetricsW(Dc, &Metrics), "%s: GetTextMetricsW failed\n", Name);
    ok(GetGlyphOutlineW(Dc, L'o', GGO_GRAY8_BITMAP, &Glyph, 0, NULL, &Identity) != GDI_ERROR,
       "%s: GetGlyphOutlineW failed\n", Name);
    Got.Cx = Extent.cx;
    Got.Cy = Extent.cy;
    Got.Ascent = Metrics.tmAscent;
    Got.Descent = Metrics.tmDescent;
    Got.BlackBoxX = Glyph.gmBlackBoxX;
    Got.BlackBoxY = Glyph.gmBlackBoxY;
    Got.OriginX = Glyph.gmptGlyphOrigin.x;
    Got.OriginY = Glyph.gmptGlyphOrigin.y;
    Got.CellIncX = Glyph.gmCellIncX;

    if (FrEmit())
    {
        printf("emit { %d, %d, %d, %d, %u, %u, %d, %d, %d },\n", Got.Cx, Got.Cy, Got.Ascent, Got.Descent,
              Got.BlackBoxX, Got.BlackBoxY, Got.OriginX, Got.OriginY, Got.CellIncX);
    }
    else if (Expect)
    {
        ok_gap(Expect->Gaps & GAP_EXTENT, Got.Cx == Expect->Cx && Got.Cy == Expect->Cy,
               "%s: extent %dx%d, expected %dx%d\n", Name, Got.Cx, Got.Cy, Expect->Cx, Expect->Cy);
        ok_gap(Expect->Gaps & GAP_ASCENT, Got.Ascent == Expect->Ascent && Got.Descent == Expect->Descent,
               "%s: ascent %d descent %d, expected %d %d\n", Name, Got.Ascent, Got.Descent, Expect->Ascent,
               Expect->Descent);
        ok_gap(Expect->Gaps & GAP_ADVANCE, Got.CellIncX == Expect->CellIncX, "%s: 'o' advance %d, expected %d\n", Name,
               Got.CellIncX, Expect->CellIncX);
        ok_gap(Expect->Gaps & GAP_BOX, Got.BlackBoxX == Expect->BlackBoxX && Got.BlackBoxY == Expect->BlackBoxY &&
               Got.OriginX == Expect->OriginX && Got.OriginY == Expect->OriginY,
               "%s: 'o' box %ux%u at %d,%d, expected %ux%u at %d,%d\n", Name, Got.BlackBoxX, Got.BlackBoxY,
               Got.OriginX, Got.OriginY, Expect->BlackBoxX, Expect->BlackBoxY, Expect->OriginX, Expect->OriginY);
    }

    FillRect(Dc, &Rect, GetStockObject(WHITE_BRUSH));
    ok(ExtTextOutW(Dc, 4, 4, 0, NULL, FR_TEXT, wcslen(FR_TEXT), NULL), "%s: ExtTextOutW failed\n", Name);
    GdiFlush();
    FrCoverage(Name, Bits, BITMAP_WIDTH, BITMAP_HEIGHT, BITMAP_WIDTH * 4, 4, FALSE);

    SelectObject(Dc, Old);
    DeleteObject(Handle);
}

START_TEST(GdiText)
{
    BITMAPINFO Info;
    HBITMAP Bitmap, OldBitmap;
    BYTE *Bits;
    HDC Dc;
    UINT F, S, Q, Index = 0;
    BOOL Added[ARRAYSIZE(FrFonts)];

    ZeroMemory(&Info, sizeof(Info));
    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = BITMAP_WIDTH;
    Info.bmiHeader.biHeight = -BITMAP_HEIGHT;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;

    Dc = CreateCompatibleDC(NULL);
    Bitmap = CreateDIBSection(Dc, &Info, DIB_RGB_COLORS, (void **)&Bits, NULL, 0);
    ok(Bitmap != NULL, "CreateDIBSection failed\n");
    if (!Bitmap)
    {
        DeleteDC(Dc);
        return;
    }
    OldBitmap = SelectObject(Dc, Bitmap);
    SetBkMode(Dc, TRANSPARENT);
    SetTextColor(Dc, RGB(0, 0, 0));

    for (F = 0; F < ARRAYSIZE(FrFonts); F++)
    {
        Added[F] = FrExtractFont(&FrFonts[F]) && AddFontResourceExW(FrFonts[F].Path, FR_PRIVATE, NULL);
        ok(Added[F], "Adding %ls failed\n", FrFonts[F].Family);
    }

    if (!FrEmit())
    {
        ok(ARRAYSIZE(Expected) == ARRAYSIZE(FrFonts) * ARRAYSIZE(FrSizes) * ARRAYSIZE(Qualities),
           "%u expectations\n", (UINT)ARRAYSIZE(Expected));
    }

    for (F = 0; F < ARRAYSIZE(FrFonts); F++)
    {
        for (S = 0; S < ARRAYSIZE(FrSizes); S++)
        {
            for (Q = 0; Q < ARRAYSIZE(Qualities); Q++, Index++)
            {
                if (Added[F])
                {
                    TestFont(Dc, Bits, &FrFonts[F], FrSizes[S], Q,
                             Index < ARRAYSIZE(Expected) ? &Expected[Index] : NULL);
                }
            }
        }
    }

    for (F = 0; F < ARRAYSIZE(FrFonts); F++)
    {
        if (Added[F])
            RemoveFontResourceExW(FrFonts[F].Path, FR_PRIVATE, NULL);
        FrDeleteFont(&FrFonts[F]);
    }

    SelectObject(Dc, OldBitmap);
    DeleteObject(Bitmap);
    DeleteDC(Dc);
}
