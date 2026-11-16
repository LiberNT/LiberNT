/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that glyph run analyses of one font face keep each em size's bounds and pixels when sizes alternate
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <dwrite_2.h>

typedef struct _RUN_IMAGE
{
    RECT Bounds;
    BYTE *Pixels;
    UINT32 Size;
} RUN_IMAGE;

static IDWriteFontFace *
CreateTextFace(IDWriteFactory2 *Factory)
{
    static const WCHAR *const Names[] = { L"Tahoma", L"Arial", L"Segoe UI" };
    IDWriteFontCollection *Collection = NULL;
    IDWriteFontFamily *Family = NULL;
    IDWriteFontFace *Face = NULL;
    IDWriteFont *Font = NULL;
    UINT32 Index = 0;
    UINT Name;
    BOOL Exists = FALSE;
    HRESULT hr;

    hr = IDWriteFactory2_GetSystemFontCollection(Factory, &Collection, FALSE);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return NULL;
    for (Name = 0; Name < ARRAYSIZE(Names) && !Exists; ++Name)
    {
        hr = IDWriteFontCollection_FindFamilyName(Collection, Names[Name], &Index, &Exists);
        ok_hr(hr, S_OK);
    }
    if (SUCCEEDED(hr) && Exists)
        hr = IDWriteFontCollection_GetFontFamily(Collection, Index, &Family);
    if (SUCCEEDED(hr) && Family != NULL)
        hr = IDWriteFontFamily_GetFirstMatchingFont(Family, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                    DWRITE_FONT_STYLE_NORMAL, &Font);
    if (SUCCEEDED(hr) && Font != NULL)
        hr = IDWriteFont_CreateFontFace(Font, &Face);
    ok(Face != NULL, "text face: hr 0x%lx, exists %d\n", hr, Exists);
    if (Font != NULL)
        IDWriteFont_Release(Font);
    if (Family != NULL)
        IDWriteFontFamily_Release(Family);
    IDWriteFontCollection_Release(Collection);
    return Face;
}

static BOOL
RenderRun(IDWriteFactory2 *Factory, IDWriteFontFace *Face, const UINT16 *Glyphs, UINT32 GlyphCount,
          FLOAT EmSize, RUN_IMAGE *Image)
{
    IDWriteGlyphRunAnalysis *Analysis = NULL;
    DWRITE_GLYPH_RUN Run;
    HRESULT hr;

    ZeroMemory(Image, sizeof(*Image));
    ZeroMemory(&Run, sizeof(Run));
    Run.fontFace = Face;
    Run.fontEmSize = EmSize;
    Run.glyphCount = GlyphCount;
    Run.glyphIndices = Glyphs;

    /* A fractional origin with grid fitting renders hinted glyphs outside the glyph cache, at this em size. */
    hr = IDWriteFactory2_CreateGlyphRunAnalysis(Factory, &Run, NULL, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_ENABLED,
                                                DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0.3f, 20.0f, &Analysis);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return FALSE;

    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(Analysis, DWRITE_TEXTURE_ALIASED_1x1, &Image->Bounds);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr) && !IsRectEmpty(&Image->Bounds))
    {
        Image->Size = (Image->Bounds.right - Image->Bounds.left) * (Image->Bounds.bottom - Image->Bounds.top);
        Image->Pixels = HeapAlloc(GetProcessHeap(), 0, Image->Size);
        if (Image->Pixels != NULL)
        {
            hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(Analysis, DWRITE_TEXTURE_ALIASED_1x1, &Image->Bounds,
                                                            Image->Pixels, Image->Size);
            ok_hr(hr, S_OK);
        }
    }
    IDWriteGlyphRunAnalysis_Release(Analysis);
    return Image->Pixels != NULL;
}

static void
FreeImage(RUN_IMAGE *Image)
{
    if (Image->Pixels != NULL)
        HeapFree(GetProcessHeap(), 0, Image->Pixels);
    ZeroMemory(Image, sizeof(*Image));
}

static BOOL
SameImage(const RUN_IMAGE *Left, const RUN_IMAGE *Right)
{
    return EqualRect(&Left->Bounds, &Right->Bounds) && Left->Size == Right->Size &&
           memcmp(Left->Pixels, Right->Pixels, Left->Size) == 0;
}

START_TEST(GlyphRunSizes)
{
    static const UINT32 Codepoints[] = { 'H', 'a', 'm', 'b', 'u', 'r', 'g', 'e', 'f', 'o', 'n', 't', 's', 'i', 'v' };
    static const FLOAT Sizes[] = { 12.0f, 37.0f, 12.25f };
    UINT16 Glyphs[ARRAYSIZE(Codepoints)];
    RUN_IMAGE First[ARRAYSIZE(Sizes)];
    RUN_IMAGE Again;
    IDWriteFactory2 *Factory = NULL;
    IDWriteFontFace *Face;
    UINT Index;
    UINT Round;
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory2, (IUnknown **)&Factory);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    Face = CreateTextFace(Factory);
    if (Face == NULL)
    {
        IDWriteFactory2_Release(Factory);
        return;
    }
    hr = IDWriteFontFace_GetGlyphIndices(Face, Codepoints, ARRAYSIZE(Codepoints), Glyphs);
    ok_hr(hr, S_OK);

    for (Index = 0; Index < ARRAYSIZE(Sizes); ++Index)
        ok(RenderRun(Factory, Face, Glyphs, ARRAYSIZE(Glyphs), Sizes[Index], &First[Index]),
           "size %u: no pixels\n", Index);

    ok(!EqualRect(&First[0].Bounds, &First[1].Bounds), "12 and 37 px share bounds %s\n",
       wine_dbgstr_rect(&First[0].Bounds));
    ok(First[0].Bounds.right - First[0].Bounds.left < First[1].Bounds.right - First[1].Bounds.left,
       "12 px run %s is not narrower than 37 px run %s\n", wine_dbgstr_rect(&First[0].Bounds),
       wine_dbgstr_rect(&First[1].Bounds));
    ok(!SameImage(&First[0], &First[2]), "12 and 12.25 px runs render the same pixels\n");

    for (Round = 0; Round < 2; ++Round)
    {
        for (Index = 0; Index < ARRAYSIZE(Sizes); ++Index)
        {
            UINT Size = (Index + Round + 1) % ARRAYSIZE(Sizes);

            if (!RenderRun(Factory, Face, Glyphs, ARRAYSIZE(Glyphs), Sizes[Size], &Again))
                continue;
            ok(SameImage(&First[Size], &Again), "round %u size %u: %s differs from %s\n", Round, Size,
               wine_dbgstr_rect(&Again.Bounds), wine_dbgstr_rect(&First[Size].Bounds));
            FreeImage(&Again);
        }
    }

    for (Index = 0; Index < ARRAYSIZE(Sizes); ++Index)
        FreeImage(&First[Index]);
    IDWriteFontFace_Release(Face);
    IDWriteFactory2_Release(Factory);
}
