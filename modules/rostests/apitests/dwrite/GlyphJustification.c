/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the Arabic justification classes IDWriteTextAnalyzer::GetGlyphs returns
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <dwrite.h>
#include <usp10.h>

static const struct
{
    BOOL Rtl;
    const WCHAR *Text;
    UINT16 Justification[8];
} Tests[] =
{
    { TRUE, L"\x628\x628\x628\x628", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_ALEF } },
    { TRUE, L"\x645\x631\x62d\x628\x627", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BARA, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_ALEF } },
    { TRUE, L"\x628\x647\x628", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_ALEF } },
    { TRUE, L"\x628\x649\x628", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_ALEF } },
    { TRUE, L"\x628\x69a\x628", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_SEEN_M, SCRIPT_JUSTIFY_ARABIC_ALEF } },
    { TRUE, L"\x628\x750", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL } },
    { TRUE, L"\x633\x640\x640\x645", { SCRIPT_JUSTIFY_ARABIC_SEEN, SCRIPT_JUSTIFY_ARABIC_KASHIDA, SCRIPT_JUSTIFY_ARABIC_KASHIDA, SCRIPT_JUSTIFY_ARABIC_NORMAL } },
    { TRUE, L"\x628\x633 \x628", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_NORMAL, SCRIPT_JUSTIFY_ARABIC_BLANK, SCRIPT_JUSTIFY_NONE } },
    { TRUE, L"\x628\t\x628", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE } },
    { TRUE, L"\x628\xa0\x628", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE } },
    { TRUE, L"\x628\x200d\x628", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_ARABIC_ALEF } },
    { TRUE, L"\x628\x200c\x628", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE } },
    { FALSE, L"\x628\x628\x628\x628", { SCRIPT_JUSTIFY_ARABIC_ALEF, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_ARABIC_BA, SCRIPT_JUSTIFY_NONE } },
    { FALSE, L"\x628\x633 \x628", { SCRIPT_JUSTIFY_ARABIC_ALEF, SCRIPT_JUSTIFY_ARABIC_SEEN, SCRIPT_JUSTIFY_ARABIC_BLANK, SCRIPT_JUSTIFY_NONE } },
    { FALSE, L"\x631\x628", { SCRIPT_JUSTIFY_ARABIC_BARA, SCRIPT_JUSTIFY_NONE } },
    { FALSE, L"\x628\x631\x631", { SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE, SCRIPT_JUSTIFY_NONE } },
    { FALSE, L"\x647\x628", { SCRIPT_JUSTIFY_ARABIC_HA, SCRIPT_JUSTIFY_NONE } },
};

START_TEST(GlyphJustification)
{
    DWRITE_SHAPING_GLYPH_PROPERTIES GlyphProps[16];
    DWRITE_SHAPING_TEXT_PROPERTIES TextProps[16];
    UINT16 ClusterMap[16], Glyphs[16];
    IDWriteTextAnalyzer *Analyzer = NULL;
    IDWriteGdiInterop *Interop = NULL;
    IDWriteFontFace *Face = NULL;
    IDWriteFactory *Factory = NULL;
    IDWriteFont *Font = NULL;
    DWRITE_SCRIPT_ANALYSIS Analysis;
    LOGFONTW LogFont = { 0 };
    UINT32 Test, Glyph, Count, Length;
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown **)&Factory);
    ok(hr == S_OK, "DWriteCreateFactory failed, hr %#lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IDWriteFactory_CreateTextAnalyzer(Factory, &Analyzer);
    ok(hr == S_OK, "CreateTextAnalyzer failed, hr %#lx\n", hr);
    hr = IDWriteFactory_GetGdiInterop(Factory, &Interop);
    ok(hr == S_OK, "GetGdiInterop failed, hr %#lx\n", hr);
    LogFont.lfHeight = 12;
    LogFont.lfWeight = FW_NORMAL;
    lstrcpyW(LogFont.lfFaceName, L"Tahoma");
    if (Interop)
        hr = IDWriteGdiInterop_CreateFontFromLOGFONT(Interop, &LogFont, &Font);
    ok(hr == S_OK && Font, "CreateFontFromLOGFONT failed, hr %#lx\n", hr);
    if (Font)
        hr = IDWriteFont_CreateFontFace(Font, &Face);
    ok(hr == S_OK && Face, "CreateFontFace failed, hr %#lx\n", hr);

    for (Test = 0; Analyzer && Face && Test < ARRAYSIZE(Tests); Test++)
    {
        Length = lstrlenW(Tests[Test].Text);
        Analysis.script = 3;
        Analysis.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;
        Count = 0;
        hr = IDWriteTextAnalyzer_GetGlyphs(Analyzer, Tests[Test].Text, Length, Face, FALSE, Tests[Test].Rtl, &Analysis,
                                           L"en-US", NULL, NULL, NULL, 0, ARRAYSIZE(Glyphs), ClusterMap, TextProps,
                                           Glyphs, GlyphProps, &Count);
        ok(hr == S_OK, "%u: GetGlyphs failed, hr %#lx\n", Test, hr);
        ok(Count == Length, "%u: got %u glyphs, expected %u\n", Test, Count, Length);
        for (Glyph = 0; Glyph < Count && Glyph < Length; Glyph++)
        {
            ok(GlyphProps[Glyph].justification == Tests[Test].Justification[Glyph],
               "%u: glyph %u: justification %u, expected %u\n", Test, Glyph,
               GlyphProps[Glyph].justification, Tests[Test].Justification[Glyph]);
        }
    }

    if (Face) IDWriteFontFace_Release(Face);
    if (Font) IDWriteFont_Release(Font);
    if (Interop) IDWriteGdiInterop_Release(Interop);
    if (Analyzer) IDWriteTextAnalyzer_Release(Analyzer);
    IDWriteFactory_Release(Factory);
}
