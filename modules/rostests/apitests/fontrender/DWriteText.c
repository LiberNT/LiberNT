/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pins DirectWrite glyph run bounds and blend parameters against Windows 11 and reports coverage
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#define COBJMACROS
#include "fontrender.h"
#include <initguid.h>
#include <dwrite.h>
#include <math.h>
#include <stdio.h>

#define GAP_ADVANCE   0x1
#define GAP_ALIASED   0x2
#define GAP_CLEARTYPE 0x4
#define GAP_BLEND     0x8

typedef struct
{
    INT Advance64;
    RECT Aliased, ClearType;
    FLOAT Gamma, Contrast, Level;
    ULONG Gaps;
} DWRITE_EXPECT;

typedef struct
{
    FLOAT Gamma, Contrast, Level;
} BLEND_PARAMS;

static const struct
{
    DWRITE_RENDERING_MODE Rendering;
    DWRITE_MEASURING_MODE Measuring;
    const char *Name;
} Modes[] =
{
    { DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_GDI_CLASSIC, "aliased" },
    { DWRITE_RENDERING_MODE_GDI_CLASSIC, DWRITE_MEASURING_MODE_GDI_CLASSIC, "gdiclassic" },
    { DWRITE_RENDERING_MODE_GDI_NATURAL, DWRITE_MEASURING_MODE_GDI_NATURAL, "gdinatural" },
    { DWRITE_RENDERING_MODE_NATURAL, DWRITE_MEASURING_MODE_NATURAL, "natural" },
    { DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, DWRITE_MEASURING_MODE_NATURAL, "symmetric" },
};

static const BLEND_PARAMS Params[] =
{
    { 1.0f, 0.0f, 0.0f },
    { 1.8f, 0.5f, 1.0f },
    { 2.2f, 1.0f, 0.5f },
    { 3.0f, 2.0f, 0.25f },
};

static const DWRITE_EXPECT Expected[] =
{
    { 5760, { 1, -7, 89, 2 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f, GAP_ALIASED },
    { 5760, { 0, 0, 0, 0 }, { 0, -7, 90, 2 }, 1.2000f, 0.0000f, 1.0000f, GAP_CLEARTYPE | GAP_BLEND },
    { 5824, { 0, 0, 0, 0 }, { 0, -7, 91, 2 }, 1.2000f, 0.0000f, 1.0000f, GAP_ADVANCE | GAP_CLEARTYPE | GAP_BLEND },
    { 5802, { 0, 0, 0, 0 }, { 0, -7, 91, 2 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 5802, { 0, 0, 0, 0 }, { 0, -7, 91, 3 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 7040, { 1, -8, 109, 3 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f, GAP_ALIASED },
    { 7040, { 0, 0, 0, 0 }, { 0, -8, 110, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_CLEARTYPE | GAP_BLEND },
    { 7232, { 0, 0, 0, 0 }, { 0, -8, 113, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_ADVANCE | GAP_CLEARTYPE | GAP_BLEND },
    { 7091, { 0, 0, 0, 0 }, { 0, -8, 111, 3 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 7091, { 0, 0, 0, 0 }, { 0, -9, 111, 3 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 7616, { 1, -9, 119, 3 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f },
    { 7616, { 0, 0, 0, 0 }, { 0, -9, 119, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_CLEARTYPE | GAP_BLEND },
    { 7872, { 0, 0, 0, 0 }, { 0, -9, 122, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_ADVANCE | GAP_CLEARTYPE | GAP_BLEND },
    { 7736, { 0, 0, 0, 0 }, { 0, -9, 121, 3 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 7736, { 0, 0, 0, 0 }, { 0, -9, 121, 3 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 9088, { 1, -10, 141, 3 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f, GAP_ALIASED },
    { 9088, { 0, 0, 0, 0 }, { 1, -10, 141, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_CLEARTYPE | GAP_BLEND },
    { 9088, { 0, 0, 0, 0 }, { 1, -10, 141, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_CLEARTYPE | GAP_BLEND },
    { 9025, { 0, 0, 0, 0 }, { 1, -10, 141, 3 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 9025, { 0, 0, 0, 0 }, { 1, -11, 141, 4 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 10304, { 1, -12, 159, 4 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f, GAP_ALIASED },
    { 10304, { 0, 0, 0, 0 }, { 1, -12, 160, 4 }, 1.2000f, 0.0000f, 1.0000f, GAP_BLEND },
    { 10304, { 0, 0, 0, 0 }, { 1, -12, 160, 4 }, 1.2000f, 0.0000f, 1.0000f, GAP_BLEND },
    { 10315, { 0, 0, 0, 0 }, { 1, -12, 161, 4 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 10315, { 0, 0, 0, 0 }, { 1, -12, 161, 4 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 15552, { 2, -18, 241, 6 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f, GAP_ALIASED },
    { 15552, { 0, 0, 0, 0 }, { 1, -18, 242, 6 }, 1.2000f, 0.0000f, 1.0000f, GAP_CLEARTYPE | GAP_BLEND },
    { 15616, { 0, 0, 0, 0 }, { 1, -18, 243, 6 }, 1.2000f, 0.0000f, 1.0000f, GAP_CLEARTYPE | GAP_BLEND },
    { 15472, { 0, 0, 0, 0 }, { 1, -18, 241, 6 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 15472, { 0, 0, 0, 0 }, { 1, -18, 241, 6 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 5696, { 1, -7, 89, 2 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f, GAP_ALIASED },
    { 5696, { 0, 0, 0, 0 }, { 0, -7, 89, 2 }, 1.2000f, 0.0000f, 1.0000f, GAP_BLEND },
    { 5888, { 0, 0, 0, 0 }, { 0, -7, 92, 2 }, 1.2000f, 0.0000f, 1.0000f, GAP_ADVANCE | GAP_CLEARTYPE | GAP_BLEND },
    { 5704, { 0, 0, 0, 0 }, { 0, -7, 89, 2 }, 2.2000f, 0.5000f, 1.0000f },
    { 5704, { 0, 0, 0, 0 }, { 0, -7, 89, 2 }, 2.2000f, 0.5000f, 1.0000f },
    { 6912, { 1, -8, 108, 2 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f, GAP_ALIASED },
    { 6912, { 0, 0, 0, 0 }, { 0, -8, 108, 2 }, 1.2000f, 0.0000f, 1.0000f, GAP_BLEND },
    { 6912, { 0, 0, 0, 0 }, { 0, -8, 108, 2 }, 1.2000f, 0.0000f, 1.0000f, GAP_BLEND },
    { 6972, { 0, 0, 0, 0 }, { 0, -8, 109, 2 }, 2.2000f, 0.5000f, 1.0000f },
    { 6972, { 0, 0, 0, 0 }, { 0, -8, 109, 2 }, 2.2000f, 0.5000f, 1.0000f },
    { 7680, { 1, -9, 119, 3 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f, GAP_ALIASED },
    { 7680, { 0, 0, 0, 0 }, { 0, -9, 120, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_BLEND },
    { 7808, { 0, 0, 0, 0 }, { 0, -9, 122, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_ADVANCE | GAP_CLEARTYPE | GAP_BLEND },
    { 7606, { 0, 0, 0, 0 }, { 0, -9, 119, 3 }, 2.2000f, 0.5000f, 1.0000f },
    { 7606, { 0, 0, 0, 0 }, { 0, -9, 119, 3 }, 2.2000f, 0.5000f, 1.0000f },
    { 9024, { 1, -10, 140, 3 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f, GAP_ALIASED },
    { 9024, { 0, 0, 0, 0 }, { 0, -10, 141, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_CLEARTYPE | GAP_BLEND },
    { 9152, { 0, 0, 0, 0 }, { 0, -10, 143, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_ADVANCE | GAP_CLEARTYPE | GAP_BLEND },
    { 8873, { 0, 0, 0, 0 }, { 0, -10, 139, 3 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 8873, { 0, 0, 0, 0 }, { 0, -10, 139, 3 }, 2.2000f, 0.5000f, 1.0000f, GAP_CLEARTYPE },
    { 10304, { 1, -12, 160, 3 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f, GAP_ALIASED },
    { 10304, { 0, 0, 0, 0 }, { 1, -12, 161, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_BLEND },
    { 10368, { 0, 0, 0, 0 }, { 1, -12, 162, 3 }, 1.2000f, 0.0000f, 1.0000f, GAP_ADVANCE | GAP_CLEARTYPE | GAP_BLEND },
    { 10141, { 0, 0, 0, 0 }, { 1, -12, 158, 3 }, 2.2000f, 0.5000f, 1.0000f },
    { 10141, { 0, 0, 0, 0 }, { 1, -12, 158, 3 }, 2.2000f, 0.5000f, 1.0000f },
    { 15104, { 2, -18, 235, 5 }, { 0, 0, 0, 0 }, 2.2000f, 0.5000f, 1.0000f, GAP_ALIASED },
    { 15104, { 0, 0, 0, 0 }, { 1, -18, 236, 5 }, 1.2000f, 0.0000f, 1.0000f, GAP_BLEND },
    { 15232, { 0, 0, 0, 0 }, { 1, -18, 238, 5 }, 1.2000f, 0.0000f, 1.0000f, GAP_ADVANCE | GAP_CLEARTYPE | GAP_BLEND },
    { 15211, { 0, 0, 0, 0 }, { 1, -18, 237, 5 }, 2.2000f, 0.5000f, 1.0000f },
    { 15211, { 0, 0, 0, 0 }, { 1, -18, 237, 5 }, 2.2000f, 0.5000f, 1.0000f },
};

static const BLEND_PARAMS ExpectedBlend[] =
{
    { 1.0000f, 0.0000f, 0.0000f },
    { 1.8000f, 0.5000f, 1.0000f },
    { 2.2000f, 1.0000f, 0.5000f },
    { 3.0000f, 2.0000f, 0.2500f },
};

static BOOL
FloatEqual(FLOAT A, FLOAT B)
{
    return fabsf(A - B) <= 1e-4f;
}

static VOID
CoverTexture(IDWriteGlyphRunAnalysis *Analysis, DWRITE_TEXTURE_TYPE Type, const RECT *Bounds, const char *Name)
{
    INT Channels = Type == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3 : 1;
    INT Width = Bounds->right - Bounds->left, Height = Bounds->bottom - Bounds->top;
    UINT32 Size = Width * Height * Channels;
    BYTE *Texture;
    HRESULT hr;

    if (Width <= 0 || Height <= 0)
        return;
    Texture = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Texture)
        return;
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(Analysis, Type, Bounds, Texture, Size);
    ok(hr == S_OK, "%s: CreateAlphaTexture returned 0x%lx\n", Name, hr);
    if (hr == S_OK)
        FrCoverage(Name, Texture, Width, Height, Width * Channels, Channels, TRUE);
    HeapFree(GetProcessHeap(), 0, Texture);
}

static VOID
TestRun(IDWriteFactory *Factory, IDWriteFontFace *Face, FR_FONT *Font, INT Size, UINT Mode, const DWRITE_EXPECT *Expect)
{
    UINT32 Codepoints[ARRAYSIZE(FR_TEXT) - 1];
    UINT16 Glyphs[ARRAYSIZE(Codepoints)];
    FLOAT Advances[ARRAYSIZE(Codepoints)];
    DWRITE_GLYPH_METRICS Metrics[ARRAYSIZE(Codepoints)];
    DWRITE_FONT_METRICS FontMetrics;
    DWRITE_GLYPH_RUN Run;
    IDWriteGlyphRunAnalysis *Analysis;
    IDWriteRenderingParams *Rendering;
    DWRITE_EXPECT Got;
    FLOAT Sum = 0.0f;
    char Name[64];
    HRESULT hr;
    UINT I;

    sprintf(Name, "dw-%ls-%d-%s", Font->Family, Size, Modes[Mode].Name);

    for (I = 0; I < ARRAYSIZE(Codepoints); I++)
        Codepoints[I] = FR_TEXT[I];
    hr = IDWriteFontFace_GetGlyphIndices(Face, Codepoints, ARRAYSIZE(Codepoints), Glyphs);
    ok(hr == S_OK, "%s: GetGlyphIndices returned 0x%lx\n", Name, hr);
    IDWriteFontFace_GetMetrics(Face, &FontMetrics);

    if (Modes[Mode].Measuring == DWRITE_MEASURING_MODE_NATURAL)
    {
        hr = IDWriteFontFace_GetDesignGlyphMetrics(Face, Glyphs, ARRAYSIZE(Glyphs), Metrics, FALSE);
        ok(hr == S_OK, "%s: GetDesignGlyphMetrics returned 0x%lx\n", Name, hr);
        for (I = 0; I < ARRAYSIZE(Glyphs); I++)
            Advances[I] = (FLOAT)Metrics[I].advanceWidth * Size / FontMetrics.designUnitsPerEm;
    }
    else
    {
        hr = IDWriteFontFace_GetGdiCompatibleGlyphMetrics(Face, (FLOAT)Size, 1.0f, NULL,
                                                          Modes[Mode].Measuring == DWRITE_MEASURING_MODE_GDI_NATURAL,
                                                          Glyphs, ARRAYSIZE(Glyphs), Metrics, FALSE);
        ok(hr == S_OK, "%s: GetGdiCompatibleGlyphMetrics returned 0x%lx\n", Name, hr);
        for (I = 0; I < ARRAYSIZE(Glyphs); I++)
            Advances[I] = floorf((FLOAT)Metrics[I].advanceWidth * Size / FontMetrics.designUnitsPerEm + 0.5f);
    }
    if (hr != S_OK)
        return;

    ZeroMemory(&Got, sizeof(Got));
    for (I = 0; I < ARRAYSIZE(Glyphs); I++)
        Sum += Advances[I];
    Got.Advance64 = (INT)floorf(Sum * 64.0f + 0.5f);

    ZeroMemory(&Run, sizeof(Run));
    Run.fontFace = Face;
    Run.fontEmSize = (FLOAT)Size;
    Run.glyphCount = ARRAYSIZE(Glyphs);
    Run.glyphIndices = Glyphs;
    Run.glyphAdvances = Advances;

    hr = IDWriteFactory_CreateGlyphRunAnalysis(Factory, &Run, 1.0f, NULL, Modes[Mode].Rendering,
                                               Modes[Mode].Measuring, 0.0f, 0.0f, &Analysis);
    ok(hr == S_OK, "%s: CreateGlyphRunAnalysis returned 0x%lx\n", Name, hr);
    if (hr != S_OK)
        return;

    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(Analysis, DWRITE_TEXTURE_ALIASED_1x1, &Got.Aliased);
    ok(hr == S_OK, "%s: GetAlphaTextureBounds(1x1) returned 0x%lx\n", Name, hr);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(Analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &Got.ClearType);
    ok(hr == S_OK, "%s: GetAlphaTextureBounds(3x1) returned 0x%lx\n", Name, hr);

    hr = IDWriteFactory_CreateCustomRenderingParams(Factory, 2.2f, 0.5f, 1.0f, DWRITE_PIXEL_GEOMETRY_RGB,
                                                    Modes[Mode].Rendering, &Rendering);
    ok(hr == S_OK, "%s: CreateCustomRenderingParams returned 0x%lx\n", Name, hr);
    if (hr == S_OK)
    {
        hr = IDWriteGlyphRunAnalysis_GetAlphaBlendParams(Analysis, Rendering, &Got.Gamma, &Got.Contrast, &Got.Level);
        ok(hr == S_OK, "%s: GetAlphaBlendParams returned 0x%lx\n", Name, hr);
        IDWriteRenderingParams_Release(Rendering);
    }

    if (FrEmit())
    {
        printf("emit { %d, { %ld, %ld, %ld, %ld }, { %ld, %ld, %ld, %ld }, %.4ff, %.4ff, %.4ff },\n", Got.Advance64,
              Got.Aliased.left, Got.Aliased.top, Got.Aliased.right, Got.Aliased.bottom, Got.ClearType.left,
              Got.ClearType.top, Got.ClearType.right, Got.ClearType.bottom, Got.Gamma, Got.Contrast, Got.Level);
    }
    else if (Expect)
    {
        ok_gap(Expect->Gaps & GAP_ADVANCE, Got.Advance64 == Expect->Advance64, "%s: advance %d/64, expected %d/64\n",
               Name, Got.Advance64, Expect->Advance64);
        ok_gap(Expect->Gaps & GAP_ALIASED, EqualRect(&Got.Aliased, &Expect->Aliased),
               "%s: 1x1 bounds %s, expected %s\n", Name, wine_dbgstr_rect(&Got.Aliased),
               wine_dbgstr_rect(&Expect->Aliased));
        ok_gap(Expect->Gaps & GAP_CLEARTYPE, EqualRect(&Got.ClearType, &Expect->ClearType),
               "%s: 3x1 bounds %s, expected %s\n", Name, wine_dbgstr_rect(&Got.ClearType),
               wine_dbgstr_rect(&Expect->ClearType));
        ok_gap(Expect->Gaps & GAP_BLEND, FloatEqual(Got.Gamma, Expect->Gamma) &&
               FloatEqual(Got.Contrast, Expect->Contrast) && FloatEqual(Got.Level, Expect->Level),
               "%s: blend %g %g %g, expected %g %g %g\n", Name, Got.Gamma, Got.Contrast, Got.Level, Expect->Gamma,
               Expect->Contrast, Expect->Level);
    }

    strcat(Name, "-1x1");
    CoverTexture(Analysis, DWRITE_TEXTURE_ALIASED_1x1, &Got.Aliased, Name);
    strcpy(Name + strlen(Name) - 3, "3x1");
    CoverTexture(Analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &Got.ClearType, Name);

    if (Mode == 3 && Size == 12 && Font == &FrFonts[0])
    {
        for (I = 0; I < ARRAYSIZE(Params); I++)
        {
            BLEND_PARAMS Blend = { 0 };

            hr = IDWriteFactory_CreateCustomRenderingParams(Factory, Params[I].Gamma, Params[I].Contrast,
                                                            Params[I].Level, DWRITE_PIXEL_GEOMETRY_RGB,
                                                            DWRITE_RENDERING_MODE_NATURAL, &Rendering);
            ok(hr == S_OK, "CreateCustomRenderingParams returned 0x%lx\n", hr);
            if (hr != S_OK)
                continue;
            hr = IDWriteGlyphRunAnalysis_GetAlphaBlendParams(Analysis, Rendering, &Blend.Gamma, &Blend.Contrast,
                                                             &Blend.Level);
            ok(hr == S_OK, "GetAlphaBlendParams returned 0x%lx\n", hr);
            IDWriteRenderingParams_Release(Rendering);

            if (FrEmit())
                printf("emit blend { %.4ff, %.4ff, %.4ff },\n", Blend.Gamma, Blend.Contrast, Blend.Level);
            else if (I < ARRAYSIZE(ExpectedBlend))
            {
                ok(FloatEqual(Blend.Gamma, ExpectedBlend[I].Gamma) &&
                   FloatEqual(Blend.Contrast, ExpectedBlend[I].Contrast) &&
                   FloatEqual(Blend.Level, ExpectedBlend[I].Level),
                   "params %u: blend %g %g %g, expected %g %g %g\n", I, Blend.Gamma, Blend.Contrast, Blend.Level,
                   ExpectedBlend[I].Gamma, ExpectedBlend[I].Contrast, ExpectedBlend[I].Level);
            }
        }
    }

    IDWriteGlyphRunAnalysis_Release(Analysis);
}

START_TEST(DWriteText)
{
    IDWriteFactory *Factory;
    IDWriteFontFile *File;
    IDWriteFontFace *Face;
    UINT F, S, M, Index = 0;
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown **)&Factory);
    ok(hr == S_OK, "DWriteCreateFactory returned 0x%lx\n", hr);
    if (hr != S_OK)
        return;

    if (!FrEmit())
    {
        ok(ARRAYSIZE(Expected) == ARRAYSIZE(FrFonts) * ARRAYSIZE(FrSizes) * ARRAYSIZE(Modes),
           "%u expectations\n", (UINT)ARRAYSIZE(Expected));
        ok(ARRAYSIZE(ExpectedBlend) == ARRAYSIZE(Params), "%u blend expectations\n", (UINT)ARRAYSIZE(ExpectedBlend));
    }

    for (F = 0; F < ARRAYSIZE(FrFonts); F++)
    {
        Face = NULL;
        if (FrExtractFont(&FrFonts[F]))
        {
            hr = IDWriteFactory_CreateFontFileReference(Factory, FrFonts[F].Path, NULL, &File);
            ok(hr == S_OK, "%ls: CreateFontFileReference returned 0x%lx\n", FrFonts[F].Family, hr);
            if (hr == S_OK)
            {
                hr = IDWriteFactory_CreateFontFace(Factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &File, 0,
                                                   DWRITE_FONT_SIMULATIONS_NONE, &Face);
                ok(hr == S_OK, "%ls: CreateFontFace returned 0x%lx\n", FrFonts[F].Family, hr);
                IDWriteFontFile_Release(File);
            }
        }
        else
            ok(0, "Extracting %ls failed\n", FrFonts[F].Family);

        for (S = 0; S < ARRAYSIZE(FrSizes); S++)
        {
            for (M = 0; M < ARRAYSIZE(Modes); M++, Index++)
            {
                if (Face)
                {
                    TestRun(Factory, Face, &FrFonts[F], FrSizes[S], M,
                            Index < ARRAYSIZE(Expected) ? &Expected[Index] : NULL);
                }
            }
        }

        if (Face)
            IDWriteFontFace_Release(Face);
    }

    IDWriteFactory_Release(Factory);

    for (F = 0; F < ARRAYSIZE(FrFonts); F++)
        FrDeleteFont(&FrFonts[F]);
}
