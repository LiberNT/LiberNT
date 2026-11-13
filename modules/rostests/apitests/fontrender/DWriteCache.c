/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Checks that DirectWrite glyph run textures do not depend on the rendering modes used before them
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define COBJMACROS
#include "fontrender.h"
#include <dwrite.h>

typedef struct
{
    BYTE *Pixels;
    UINT32 Size;
    RECT Bounds;
} TEXTURE;

static IDWriteFontFace *
CreateFace(IDWriteFactory **Factory, FR_FONT *Font)
{
    IDWriteFontFile *File;
    IDWriteFontFace *Face = NULL;
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown **)Factory);
    ok(hr == S_OK, "DWriteCreateFactory returned 0x%lx\n", hr);
    if (hr != S_OK)
        return NULL;
    hr = IDWriteFactory_CreateFontFileReference(*Factory, Font->Path, NULL, &File);
    ok(hr == S_OK, "CreateFontFileReference returned 0x%lx\n", hr);
    if (hr == S_OK)
    {
        hr = IDWriteFactory_CreateFontFace(*Factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &File, 0,
                                           DWRITE_FONT_SIMULATIONS_NONE, &Face);
        ok(hr == S_OK, "CreateFontFace returned 0x%lx\n", hr);
        IDWriteFontFile_Release(File);
    }
    if (!Face)
        IDWriteFactory_Release(*Factory);
    return Face;
}

static BOOL
RenderRun(IDWriteFactory *Factory, IDWriteFontFace *Face, INT Size, DWRITE_RENDERING_MODE Mode, TEXTURE *Texture)
{
    UINT32 Codepoints[ARRAYSIZE(FR_TEXT) - 1];
    UINT16 Glyphs[ARRAYSIZE(Codepoints)];
    FLOAT Advances[ARRAYSIZE(Codepoints)];
    DWRITE_GLYPH_METRICS Metrics[ARRAYSIZE(Codepoints)];
    DWRITE_FONT_METRICS FontMetrics;
    DWRITE_TEXTURE_TYPE Type = Mode == DWRITE_RENDERING_MODE_ALIASED ? DWRITE_TEXTURE_ALIASED_1x1 :
                               DWRITE_TEXTURE_CLEARTYPE_3x1;
    DWRITE_GLYPH_RUN Run;
    IDWriteGlyphRunAnalysis *Analysis;
    HRESULT hr;
    UINT I;

    ZeroMemory(Texture, sizeof(*Texture));
    for (I = 0; I < ARRAYSIZE(Codepoints); I++)
        Codepoints[I] = FR_TEXT[I];
    hr = IDWriteFontFace_GetGlyphIndices(Face, Codepoints, ARRAYSIZE(Codepoints), Glyphs);
    ok(hr == S_OK, "GetGlyphIndices returned 0x%lx\n", hr);
    hr = IDWriteFontFace_GetDesignGlyphMetrics(Face, Glyphs, ARRAYSIZE(Glyphs), Metrics, FALSE);
    ok(hr == S_OK, "GetDesignGlyphMetrics returned 0x%lx\n", hr);
    if (hr != S_OK)
        return FALSE;
    IDWriteFontFace_GetMetrics(Face, &FontMetrics);
    for (I = 0; I < ARRAYSIZE(Glyphs); I++)
        Advances[I] = (FLOAT)Metrics[I].advanceWidth * Size / FontMetrics.designUnitsPerEm;

    ZeroMemory(&Run, sizeof(Run));
    Run.fontFace = Face;
    Run.fontEmSize = (FLOAT)Size;
    Run.glyphCount = ARRAYSIZE(Glyphs);
    Run.glyphIndices = Glyphs;
    Run.glyphAdvances = Advances;
    hr = IDWriteFactory_CreateGlyphRunAnalysis(Factory, &Run, 1.0f, NULL, Mode, DWRITE_MEASURING_MODE_GDI_CLASSIC,
                                               0.0f, 0.0f, &Analysis);
    ok(hr == S_OK, "CreateGlyphRunAnalysis returned 0x%lx\n", hr);
    if (hr != S_OK)
        return FALSE;

    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(Analysis, Type, &Texture->Bounds);
    ok(hr == S_OK, "GetAlphaTextureBounds returned 0x%lx\n", hr);
    Texture->Size = (Texture->Bounds.right - Texture->Bounds.left) * (Texture->Bounds.bottom - Texture->Bounds.top) *
                    (Type == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3 : 1);
    if (hr == S_OK && Texture->Size)
        Texture->Pixels = HeapAlloc(GetProcessHeap(), 0, Texture->Size);
    if (Texture->Pixels)
    {
        hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(Analysis, Type, &Texture->Bounds, Texture->Pixels,
                                                        Texture->Size);
        ok(hr == S_OK, "CreateAlphaTexture returned 0x%lx\n", hr);
    }
    IDWriteGlyphRunAnalysis_Release(Analysis);
    return Texture->Pixels != NULL && hr == S_OK;
}

static VOID
TestOrder(FR_FONT *Font, INT Size, DWRITE_RENDERING_MODE First, DWRITE_RENDERING_MODE Second, const char *Name)
{
    IDWriteFactory *Factory;
    IDWriteFontFace *Face;
    TEXTURE Fresh, Before, After;
    BOOL HaveFresh;

    Face = CreateFace(&Factory, Font);
    if (!Face)
        return;
    HaveFresh = RenderRun(Factory, Face, Size, Second, &Fresh);
    IDWriteFontFace_Release(Face);
    IDWriteFactory_Release(Factory);

    Face = CreateFace(&Factory, Font);
    if (Face)
    {
        RenderRun(Factory, Face, Size, First, &Before);
        if (RenderRun(Factory, Face, Size, Second, &After) && HaveFresh)
        {
            ok(EqualRect(&After.Bounds, &Fresh.Bounds), "%ls %d %s: bounds %s, fresh face %s\n", Font->Family, Size,
               Name, wine_dbgstr_rect(&After.Bounds), wine_dbgstr_rect(&Fresh.Bounds));
            ok(After.Size == Fresh.Size && !memcmp(After.Pixels, Fresh.Pixels, Fresh.Size),
               "%ls %d %s: texture differs from a fresh face's\n", Font->Family, Size, Name);
        }
        HeapFree(GetProcessHeap(), 0, Before.Pixels);
        HeapFree(GetProcessHeap(), 0, After.Pixels);
        IDWriteFontFace_Release(Face);
        IDWriteFactory_Release(Factory);
    }
    HeapFree(GetProcessHeap(), 0, Fresh.Pixels);
    ok(HeapValidate(GetProcessHeap(), 0, NULL), "%ls %d %s: heap corrupted\n", Font->Family, Size, Name);
}

START_TEST(DWriteCache)
{
    UINT F, S;

    for (F = 0; F < ARRAYSIZE(FrFonts); F++)
    {
        if (!FrExtractFont(&FrFonts[F]))
        {
            ok(0, "Extracting %ls failed\n", FrFonts[F].Family);
            continue;
        }
        for (S = 0; S < ARRAYSIZE(FrSizes); S++)
        {
            TestOrder(&FrFonts[F], FrSizes[S], DWRITE_RENDERING_MODE_ALIASED, DWRITE_RENDERING_MODE_GDI_CLASSIC,
                      "gdiclassic after aliased");
            TestOrder(&FrFonts[F], FrSizes[S], DWRITE_RENDERING_MODE_GDI_CLASSIC, DWRITE_RENDERING_MODE_ALIASED,
                      "aliased after gdiclassic");
        }
        FrDeleteFont(&FrFonts[F]);
    }
}
