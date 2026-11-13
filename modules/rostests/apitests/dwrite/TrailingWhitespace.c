/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests how IDWriteTextLayout lays out, draws and hit tests the trailing whitespace of each line
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <dwrite.h>
#include <float.h>
#include <math.h>

#define T_RTL       0x01
#define T_TRAILING  0x02
#define T_CENTER    0x04
#define T_UNDERLINE 0x08
#define T_STRIKE    0x10
#define T_TRIM      0x20
#define T_FALLBACK  0x40

typedef struct
{
    UINT32 Position;
    UINT32 Length;
    UINT32 Glyphs;
    UINT32 Level;
    FLOAT X;
    UINT32 Line;
} RUN;

static const struct
{
    const WCHAR *Text;
    FLOAT MaxWidth;
    ULONG Flags;
    UINT32 Lines;
    FLOAT Width;
    FLOAT WidthTrailing;
    FLOAT Left;
    UINT32 Decorations;
    UINT32 InlineObjects;
    UINT32 RunCount;
    RUN Runs[5];
    UINT32 IdeoSpaces;
    UINT32 Spaces;
} Tests[] =
{
    { L" ", FLT_MAX, 0, 1, 0, 1, 0, 0, 0, 1, { { 0, 1, 1, 0, 0, 0 } } },
    { L"  ", FLT_MAX, 0, 1, 0, 2, 0, 0, 0, 1, { { 0, 2, 2, 0, 0, 0 } } },
    { L"a  ", FLT_MAX, 0, 1, 1, 3, 0, 0, 0, 2, { { 0, 1, 1, 0, 0, 0 }, { 1, 2, 2, 0, 1, 0 } } },
    { L" a", FLT_MAX, 0, 1, 2, 2, 0, 0, 0, 1, { { 0, 2, 2, 0, 0, 0 } } },
    { L"a b", FLT_MAX, 0, 1, 3, 3, 0, 0, 0, 1, { { 0, 3, 3, 0, 0, 0 } } },
    { L"a  ", 10, T_TRAILING, 1, 1, 3, 9, 0, 0, 2, { { 0, 1, 1, 0, 9, 0 }, { 1, 2, 2, 0, 10, 0 } } },
    { L"a  ", 10, T_CENTER, 1, 1, 3, 4.5, 0, 0, 2, { { 0, 1, 1, 0, 4.5, 0 }, { 1, 2, 2, 0, 5.5, 0 } } },
    { L" ", 10, T_TRAILING, 1, 0, 1, 10, 0, 0, 1, { { 0, 1, 1, 0, 10, 0 } } },
    { L"a  ", 10, T_RTL, 1, 1, 3, 7, 0, 0, 2, { { 0, 1, 1, 2, 9, 0 }, { 1, 2, 2, 1, 9, 0 } } },
    { L"a  ", 10, T_RTL | T_TRAILING, 1, 1, 3, -2, 0, 0, 2, { { 0, 1, 1, 2, 0, 0 }, { 1, 2, 2, 1, 0, 0 } } },
    { L"a  ", 10, T_RTL | T_CENTER, 1, 1, 3, 2.5, 0, 0, 2, { { 0, 1, 1, 2, 4.5, 0 }, { 1, 2, 2, 1, 4.5, 0 } } },
    { L" ", 10, T_RTL, 1, 0, 1, 9, 0, 0, 1, { { 0, 1, 1, 1, 10, 0 } } },
    { L"\x627\x628  ", FLT_MAX, 0, 1, 2, 4, 0, 0, 0, 2, { { 0, 2, 2, 1, 2, 0 }, { 2, 2, 2, 0, 2, 0 } } },
    { L"ab  cd  ", 2.5, T_RTL, 2, 2, 4, -1.5, 0, 0, 4,
      { { 0, 2, 2, 2, 0.5, 0 }, { 2, 2, 2, 1, 0.5, 0 }, { 4, 2, 2, 2, 0.5, 1 }, { 6, 2, 2, 1, 0.5, 1 } } },
    { L"a  ", FLT_MAX, T_UNDERLINE, 1, 1, 3, 0, 1, 0, 2, { { 0, 1, 1, 0, 0, 0 }, { 1, 2, 2, 0, 1, 0 } } },
    { L"a  ", FLT_MAX, T_STRIKE, 1, 1, 3, 0, 1, 0, 2, { { 0, 1, 1, 0, 0, 0 }, { 1, 2, 2, 0, 1, 0 } } },
    { L" ", FLT_MAX, T_UNDERLINE, 1, 0, 1, 0, 0, 0, 1, { { 0, 1, 1, 0, 0, 0 } } },
    { L"a  \r\n", FLT_MAX, 0, 2, 1, 3, 0, 0, 0, 3, { { 0, 1, 1, 0, 0, 0 }, { 1, 2, 2, 0, 1, 0 }, { 3, 2, 0, 0, 3, 0 } } },
    { L" \r\n", FLT_MAX, 0, 2, 0, 1, 0, 0, 0, 2, { { 0, 1, 1, 0, 0, 0 }, { 1, 2, 0, 0, 1, 0 } } },
    { L"a  \nb", FLT_MAX, 0, 2, 1, 3, 0, 0, 0, 4,
      { { 0, 1, 1, 0, 0, 0 }, { 1, 2, 2, 0, 1, 0 }, { 3, 1, 0, 0, 3, 0 }, { 4, 1, 1, 0, 0, 1 } } },
    { L"ab cd", 4.5, 0, 2, 2, 3, 0, 0, 0, 3, { { 0, 2, 2, 0, 0, 0 }, { 2, 1, 1, 0, 2, 0 }, { 3, 2, 2, 0, 0, 1 } } },
    { L"ab  cd", 2.25, 0, 2, 2, 4, 0, 0, 0, 3, { { 0, 2, 2, 0, 0, 0 }, { 2, 2, 2, 0, 2, 0 }, { 4, 2, 2, 0, 0, 1 } } },
    { L"ab  cd", 4.5, T_TRAILING, 2, 2, 4, 2.5, 0, 0, 3, { { 0, 2, 2, 0, 2.5, 0 }, { 2, 2, 2, 0, 4.5, 0 }, { 4, 2, 2, 0, 2.5, 1 } } },
    { L"  ", 0, 0, 1, 0, 2, 0, 0, 0, 1, { { 0, 2, 2, 0, 0, 0 } } },
    { L"a  b", 0, 0, 2, 1, 3, 0, 0, 0, 3, { { 0, 1, 1, 0, 0, 0 }, { 1, 2, 2, 0, 1, 0 }, { 3, 1, 1, 0, 0, 1 } } },
    { L" a", 0, 0, 2, 1, 1, 0, 0, 0, 2, { { 0, 1, 1, 0, 0, 0 }, { 1, 1, 1, 0, 0, 1 } } },
    { L"ab  ", 0.5, 0, 2, 1, 3, 0, 0, 0, 3, { { 0, 1, 1, 0, 0, 0 }, { 1, 1, 1, 0, 0, 1 }, { 2, 2, 2, 0, 1, 1 } } },
    { L"ab \nc", 0.5, 0, 3, 1, 2, 0, 0, 0, 5,
      { { 0, 1, 1, 0, 0, 0 }, { 1, 1, 1, 0, 0, 1 }, { 2, 1, 1, 0, 1, 1 }, { 3, 1, 0, 0, 2, 1 }, { 4, 1, 1, 0, 0, 2 } } },
    { L"abcd  ", 10.0f / 3.0f, T_TRIM, 1, 3, 3, 0, 0, 1, 1, { { 0, 2, 2, 0, 0, 0 } } },
    { L"ab  ", 25.0f / 9.0f, T_TRIM, 1, 2, 4, 0, 0, 0, 2, { { 0, 2, 2, 0, 0, 0 }, { 2, 2, 2, 0, 2, 0 } } },
    { L"\x65e5\x3000", FLT_MAX, T_FALLBACK, 1, 1, 1, 0, 0, 0, 2, { { 0, 1, 1, 0, 0, 0 }, { 1, 1, 1, 0, 1, 0 } }, 1, 0 },
    { L"\x65e5\x672c ", FLT_MAX, T_FALLBACK, 1, 2, 2, 0, 0, 0, 2, { { 0, 2, 2, 0, 0, 0 }, { 2, 1, 1, 0, 2, 0 } }, 0, 1 },
    { L"\x65e5\x672c\x8a9e", 1.5, T_FALLBACK, 3, 1, 1, 0, 0, 0, 3,
      { { 0, 1, 1, 0, 0, 0 }, { 1, 1, 1, 0, 0, 1 }, { 2, 1, 1, 0, 0, 2 } } },
    { L"\x65e5\x3000\x672c", 1.5, T_FALLBACK, 2, 1, 1, 0, 0, 0, 3,
      { { 0, 1, 1, 0, 0, 0 }, { 1, 1, 1, 0, 1, 0 }, { 2, 1, 1, 0, 0, 1 } }, 1, 0 },
    { L"\x65e5 \x672c", 1.5, T_FALLBACK, 2, 1, 1, 0, 0, 0, 3,
      { { 0, 1, 1, 0, 0, 0 }, { 1, 1, 1, 0, 1, 0 }, { 2, 1, 1, 0, 0, 1 } }, 0, 1 },
    { L"\xd55c\xae00 ", FLT_MAX, T_FALLBACK, 1, 2, 2, 0, 0, 0, 2, { { 0, 2, 2, 0, 0, 0 }, { 2, 1, 1, 0, 2, 0 } }, 0, 1 },
    { L"\xd55c \xae00", 1.5, T_FALLBACK, 2, 1, 1, 0, 0, 0, 3,
      { { 0, 1, 1, 0, 0, 0 }, { 1, 1, 1, 0, 1, 0 }, { 2, 1, 1, 0, 0, 1 } }, 0, 1 },
};

static RUN Drawn[8];
static UINT32 DrawnCount, Underlines, Strikes, InlineObjects;
static FLOAT LineY[4];
static UINT32 LineCount;

static HRESULT WINAPI
Renderer_QueryInterface(IDWriteTextRenderer *Iface, REFIID Riid, void **Object)
{
    if (IsEqualIID(Riid, &IID_IDWriteTextRenderer) || IsEqualIID(Riid, &IID_IDWritePixelSnapping) ||
        IsEqualIID(Riid, &IID_IUnknown))
    {
        *Object = Iface;
        return S_OK;
    }
    *Object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
Renderer_AddRef(IDWriteTextRenderer *Iface)
{
    return 2;
}

static ULONG WINAPI
Renderer_Release(IDWriteTextRenderer *Iface)
{
    return 1;
}

static HRESULT WINAPI
Renderer_IsPixelSnappingDisabled(IDWriteTextRenderer *Iface, void *Context, BOOL *Disabled)
{
    *Disabled = TRUE;
    return S_OK;
}

static HRESULT WINAPI
Renderer_GetCurrentTransform(IDWriteTextRenderer *Iface, void *Context, DWRITE_MATRIX *Matrix)
{
    Matrix->m11 = Matrix->m22 = 1.0f;
    Matrix->m12 = Matrix->m21 = Matrix->dx = Matrix->dy = 0.0f;
    return S_OK;
}

static HRESULT WINAPI
Renderer_GetPixelsPerDip(IDWriteTextRenderer *Iface, void *Context, FLOAT *PixelsPerDip)
{
    *PixelsPerDip = 1.0f;
    return S_OK;
}

static HRESULT WINAPI
Renderer_DrawGlyphRun(IDWriteTextRenderer *Iface, void *Context, FLOAT X, FLOAT Y, DWRITE_MEASURING_MODE Mode,
                      const DWRITE_GLYPH_RUN *GlyphRun, const DWRITE_GLYPH_RUN_DESCRIPTION *Description, IUnknown *Effect)
{
    UINT32 Line;

    for (Line = 0; Line < LineCount && LineY[Line] != Y; Line++);
    if (Line == LineCount && LineCount < ARRAYSIZE(LineY))
        LineY[LineCount++] = Y;

    if (DrawnCount < ARRAYSIZE(Drawn))
    {
        Drawn[DrawnCount].Position = Description->textPosition;
        Drawn[DrawnCount].Length = Description->stringLength;
        Drawn[DrawnCount].Glyphs = GlyphRun->glyphCount;
        Drawn[DrawnCount].Level = GlyphRun->bidiLevel;
        Drawn[DrawnCount].X = X;
        Drawn[DrawnCount].Line = Line;
    }
    DrawnCount++;
    return S_OK;
}

static HRESULT WINAPI
Renderer_DrawUnderline(IDWriteTextRenderer *Iface, void *Context, FLOAT X, FLOAT Y, const DWRITE_UNDERLINE *Underline,
                       IUnknown *Effect)
{
    Underlines++;
    return S_OK;
}

static HRESULT WINAPI
Renderer_DrawStrikethrough(IDWriteTextRenderer *Iface, void *Context, FLOAT X, FLOAT Y,
                           const DWRITE_STRIKETHROUGH *Strikethrough, IUnknown *Effect)
{
    Strikes++;
    return S_OK;
}

static HRESULT WINAPI
Renderer_DrawInlineObject(IDWriteTextRenderer *Iface, void *Context, FLOAT X, FLOAT Y, IDWriteInlineObject *Object,
                          BOOL Sideways, BOOL RightToLeft, IUnknown *Effect)
{
    InlineObjects++;
    return S_OK;
}

static IDWriteTextRendererVtbl RendererVtbl =
{
    Renderer_QueryInterface, Renderer_AddRef, Renderer_Release, Renderer_IsPixelSnappingDisabled,
    Renderer_GetCurrentTransform, Renderer_GetPixelsPerDip, Renderer_DrawGlyphRun, Renderer_DrawUnderline,
    Renderer_DrawStrikethrough, Renderer_DrawInlineObject
};

static IDWriteTextRenderer Renderer = { &RendererVtbl };

static BOOL
CellsEqual(FLOAT Value, FLOAT Cells, FLOAT Cell)
{
    return fabsf(Value - Cells * Cell) < 0.05f;
}

static FLOAT
MaxWidthDips(FLOAT MaxWidth, FLOAT Cell)
{
    return MaxWidth == FLT_MAX ? FLT_MAX : MaxWidth * Cell;
}

static FLOAT
MeasureWidth(IDWriteFactory *Factory, IDWriteTextFormat *Format, const WCHAR *Text, BOOL Trailing)
{
    DWRITE_TEXT_METRICS Metrics;
    IDWriteTextLayout *Layout;
    FLOAT Width = 0.0f;
    HRESULT hr;

    hr = IDWriteFactory_CreateTextLayout(Factory, Text, lstrlenW(Text), Format, FLT_MAX, 100.0f, &Layout);
    ok(hr == S_OK, "CreateTextLayout failed, hr %#lx\n", hr);
    if (FAILED(hr))
        return 0.0f;
    hr = IDWriteTextLayout_GetMetrics(Layout, &Metrics);
    ok(hr == S_OK, "GetMetrics failed, hr %#lx\n", hr);
    if (hr == S_OK)
        Width = Trailing ? Metrics.widthIncludingTrailingWhitespace : Metrics.width;
    ok(Width > 0.0f, "Width %.2f\n", Width);
    IDWriteTextLayout_Release(Layout);
    return Width;
}

static IDWriteTextLayout *
CreateLayout(IDWriteFactory *Factory, IDWriteTextFormat *Format, const WCHAR *Text, FLOAT MaxWidth, ULONG Flags,
             IDWriteInlineObject *Sign)
{
    DWRITE_TEXT_RANGE All = { 0, lstrlenW(Text) };
    DWRITE_TRIMMING Trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    IDWriteTextLayout *Layout;
    HRESULT hr;

    hr = IDWriteFactory_CreateTextLayout(Factory, Text, All.length, Format, MaxWidth, 100.0f, &Layout);
    ok(hr == S_OK, "CreateTextLayout failed, hr %#lx\n", hr);
    if (FAILED(hr))
        return NULL;
    if (Flags & T_RTL)
        IDWriteTextLayout_SetReadingDirection(Layout, DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
    if (Flags & T_TRAILING)
        IDWriteTextLayout_SetTextAlignment(Layout, DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (Flags & T_CENTER)
        IDWriteTextLayout_SetTextAlignment(Layout, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (Flags & T_UNDERLINE)
        IDWriteTextLayout_SetUnderline(Layout, TRUE, All);
    if (Flags & T_STRIKE)
        IDWriteTextLayout_SetStrikethrough(Layout, TRUE, All);
    if (Flags & T_TRIM)
        IDWriteTextLayout_SetTrimming(Layout, &Trimming, Sign);
    return Layout;
}

static void
TestHitTest(IDWriteFactory *Factory, IDWriteTextFormat *Format, FLOAT Cell)
{
    static const struct
    {
        const WCHAR *Text;
        FLOAT MaxWidth;
        ULONG Flags;
        UINT32 Position;
        FLOAT Left;
        FLOAT Width;
        FLOAT PointX;
        UINT32 Length;
        UINT32 Level;
    } HitTests[] =
    {
        { L" ", 10, 0, 0, 0, 1, 0, 1, 0 },
        { L" ", 10, 0, 1, 1, 0, 1, 0, 0 },
        { L"a  ", 10, 0, 1, 1, 1, 1, 1, 0 },
        { L"a  ", 10, 0, 2, 2, 1, 2, 1, 0 },
        { L"a  ", 10, 0, 3, 3, 0, 3, 0, 0 },
        { L"a  ", 10, T_RTL, 0, 9, 1, 9, 1, 2 },
        { L"a  ", 10, T_RTL, 1, 8, 1, 9, 1, 1 },
        { L"a  ", 10, T_RTL, 2, 7, 1, 8, 1, 1 },
        { L"a  ", 10, T_RTL, 3, 7, 0, 7, 0, 1 },
        { L"ab  cd", 4.5, 0, 3, 3, 1, 3, 1, 0 },
        { L"ab  cd", 4.5, 0, 4, 0, 1, 0, 1, 0 },
    };
    DWRITE_HIT_TEST_METRICS Metrics;
    IDWriteTextLayout *Layout;
    FLOAT PointX, PointY;
    UINT32 Test;
    HRESULT hr;

    for (Test = 0; Test < ARRAYSIZE(HitTests); Test++)
    {
        Layout = CreateLayout(Factory, Format, HitTests[Test].Text, HitTests[Test].MaxWidth * Cell, HitTests[Test].Flags,
                              NULL);
        if (!Layout)
            continue;
        memset(&Metrics, 0xcc, sizeof(Metrics));
        hr = IDWriteTextLayout_HitTestTextPosition(Layout, HitTests[Test].Position, FALSE, &PointX, &PointY, &Metrics);
        ok(hr == S_OK, "hit %u: HitTestTextPosition failed, hr %#lx\n", Test, hr);
        if (hr == S_OK)
        {
            ok(CellsEqual(Metrics.left, HitTests[Test].Left, Cell), "hit %u: left %.2f\n", Test, Metrics.left);
            ok(CellsEqual(Metrics.width, HitTests[Test].Width, Cell), "hit %u: width %.2f\n", Test, Metrics.width);
            ok(CellsEqual(PointX, HitTests[Test].PointX, Cell), "hit %u: point %.2f\n", Test, PointX);
            ok(Metrics.textPosition == HitTests[Test].Position, "hit %u: position %u\n", Test, Metrics.textPosition);
            ok(Metrics.length == HitTests[Test].Length, "hit %u: length %u\n", Test, Metrics.length);
            ok(Metrics.bidiLevel == HitTests[Test].Level, "hit %u: level %u\n", Test, Metrics.bidiLevel);
        }
        IDWriteTextLayout_Release(Layout);
    }
}

START_TEST(TrailingWhitespace)
{
    IDWriteTextFormat *Format = NULL;
    IDWriteInlineObject *Sign = NULL;
    IDWriteFactory *Factory = NULL;
    IDWriteTextLayout *Layout;
    DWRITE_TEXT_METRICS Metrics;
    UINT32 Test, Index;
    FLOAT Cell, SpaceCell, IdeoSpaceCell, Extra;
    WCHAR First[2] = { 0 };
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown **)&Factory);
    ok(hr == S_OK, "DWriteCreateFactory failed, hr %#lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IDWriteFactory_CreateTextFormat(Factory, L"Courier New", NULL, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-us", &Format);
    ok(hr == S_OK, "CreateTextFormat failed, hr %#lx\n", hr);
    if (FAILED(hr))
        goto done;
    hr = IDWriteFactory_CreateEllipsisTrimmingSign(Factory, Format, &Sign);
    ok(hr == S_OK, "CreateEllipsisTrimmingSign failed, hr %#lx\n", hr);

    SpaceCell = MeasureWidth(Factory, Format, L"a", FALSE);
    if (SpaceCell <= 0.0f)
        goto done;
    IdeoSpaceCell = MeasureWidth(Factory, Format, L"\x3000", TRUE);

    for (Test = 0; Test < ARRAYSIZE(Tests); Test++)
    {
        Cell = SpaceCell;
        if (Tests[Test].Flags & T_FALLBACK)
        {
            First[0] = Tests[Test].Text[0];
            Cell = MeasureWidth(Factory, Format, First, FALSE);
            if (Cell <= 0.0f)
                continue;
        }
        Extra = Tests[Test].IdeoSpaces * IdeoSpaceCell + Tests[Test].Spaces * SpaceCell;

        Layout = CreateLayout(Factory, Format, Tests[Test].Text, MaxWidthDips(Tests[Test].MaxWidth, Cell),
                              Tests[Test].Flags, Sign);
        if (!Layout)
            continue;

        hr = IDWriteTextLayout_GetMetrics(Layout, &Metrics);
        ok(hr == S_OK, "%u: GetMetrics failed, hr %#lx\n", Test, hr);
        ok(Metrics.lineCount == Tests[Test].Lines, "%u: %u lines, expected %u\n", Test, Metrics.lineCount,
           Tests[Test].Lines);
        ok(CellsEqual(Metrics.width, Tests[Test].Width, Cell), "%u: width %.2f\n", Test, Metrics.width);
        ok(CellsEqual(Metrics.widthIncludingTrailingWhitespace - Extra, Tests[Test].WidthTrailing, Cell),
           "%u: width with trailing whitespace %.2f\n", Test, Metrics.widthIncludingTrailingWhitespace);
        ok(CellsEqual(Metrics.left, Tests[Test].Left, Cell), "%u: left %.2f\n", Test, Metrics.left);

        DrawnCount = Underlines = Strikes = InlineObjects = LineCount = 0;
        hr = IDWriteTextLayout_Draw(Layout, NULL, &Renderer, 0.0f, 0.0f);
        ok(hr == S_OK, "%u: Draw failed, hr %#lx\n", Test, hr);
        ok(DrawnCount == Tests[Test].RunCount, "%u: %u glyph runs, expected %u\n", Test, DrawnCount,
           Tests[Test].RunCount);
        ok(Underlines + Strikes == Tests[Test].Decorations, "%u: %u underlines, %u strikethroughs\n", Test,
           Underlines, Strikes);
        ok(InlineObjects == Tests[Test].InlineObjects, "%u: %u inline objects\n", Test, InlineObjects);
        for (Index = 0; Index < min(DrawnCount, Tests[Test].RunCount); Index++)
        {
            const RUN *Expected = &Tests[Test].Runs[Index], *Actual = &Drawn[Index];
            ok(Actual->Position == Expected->Position && Actual->Length == Expected->Length,
               "%u: run %u at %u length %u, expected %u length %u\n", Test, Index, Actual->Position, Actual->Length,
               Expected->Position, Expected->Length);
            ok(Actual->Glyphs == Expected->Glyphs, "%u: run %u has %u glyphs, expected %u\n", Test, Index,
               Actual->Glyphs, Expected->Glyphs);
            ok(Actual->Level == Expected->Level, "%u: run %u level %u, expected %u\n", Test, Index, Actual->Level,
               Expected->Level);
            ok(CellsEqual(Actual->X, Expected->X, Cell), "%u: run %u x %.2f, expected %.2f\n", Test, Index, Actual->X,
               Expected->X * Cell);
            ok(Actual->Line == Expected->Line, "%u: run %u on line %u, expected %u\n", Test, Index, Actual->Line,
               Expected->Line);
        }
        IDWriteTextLayout_Release(Layout);
    }

    TestHitTest(Factory, Format, SpaceCell);

done:
    if (Sign) IDWriteInlineObject_Release(Sign);
    if (Format) IDWriteTextFormat_Release(Format);
    IDWriteFactory_Release(Factory);
}
