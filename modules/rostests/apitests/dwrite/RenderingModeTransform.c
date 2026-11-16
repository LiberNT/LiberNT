/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the em size at which a world transform turns the recommended rendering mode to outline
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <dwrite_2.h>

static IDWriteFontFace2 *
CreateTahomaFace2(IDWriteFactory *Factory)
{
    IDWriteFontCollection *Collection = NULL;
    IDWriteFontFamily *Family = NULL;
    IDWriteFontFace2 *Face2 = NULL;
    IDWriteFontFace *Face = NULL;
    IDWriteFont *Font = NULL;
    UINT32 Index = 0;
    BOOL Exists = FALSE;

    if (FAILED(IDWriteFactory_GetSystemFontCollection(Factory, &Collection, FALSE)))
        return NULL;
    if (SUCCEEDED(IDWriteFontCollection_FindFamilyName(Collection, L"Tahoma", &Index, &Exists)) && Exists &&
        SUCCEEDED(IDWriteFontCollection_GetFontFamily(Collection, Index, &Family)) &&
        SUCCEEDED(IDWriteFontFamily_GetFirstMatchingFont(Family, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                         DWRITE_FONT_STYLE_NORMAL, &Font)) &&
        SUCCEEDED(IDWriteFont_CreateFontFace(Font, &Face)))
    {
        IDWriteFontFace_QueryInterface(Face, &IID_IDWriteFontFace2, (void **)&Face2);
        IDWriteFontFace_Release(Face);
    }
    if (Font)
        IDWriteFont_Release(Font);
    if (Family)
        IDWriteFontFamily_Release(Family);
    IDWriteFontCollection_Release(Collection);
    return Face2;
}

static DWRITE_RENDERING_MODE
RecommendedMode(IDWriteFontFace2 *Face, float EmSize, const DWRITE_MATRIX *Transform,
                DWRITE_OUTLINE_THRESHOLD Threshold)
{
    DWRITE_RENDERING_MODE Mode = DWRITE_RENDERING_MODE_DEFAULT;
    DWRITE_GRID_FIT_MODE GridFit;
    HRESULT hr;

    hr = IDWriteFontFace2_GetRecommendedRenderingMode(Face, EmSize, 96.0f, 96.0f, Transform, FALSE, Threshold,
                                                      DWRITE_MEASURING_MODE_NATURAL, NULL, &Mode, &GridFit);
    ok_hr(hr, S_OK);
    return Mode;
}

START_TEST(RenderingModeTransform)
{
    static const struct
    {
        const char *Name;
        DWRITE_MATRIX Transform;
        DWRITE_OUTLINE_THRESHOLD Threshold;
        float Below;
        float Outline;
    }
    Tests[] =
    {
        { "identity",    { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, 99.5f, 100.0f },
        { "translation", { 1.0f, 0.0f, 0.0f, 1.0f, 50.0f, 70.0f }, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, 99.5f, 100.0f },
        { "scale 2",     { 2.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f }, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, 49.5f, 50.0f },
        { "scale x 2",   { 2.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, 49.5f, 50.0f },
        { "scale y 2",   { 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f }, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, 49.5f, 50.0f },
        { "scale 4 0.5", { 4.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f }, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, 24.5f, 25.0f },
        { "shear",       { 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f }, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, 61.5f, 62.0f },
        { "aliased",     { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }, DWRITE_OUTLINE_THRESHOLD_ALIASED, 349.5f, 350.0f },
        { "aliased x 2", { 2.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f }, DWRITE_OUTLINE_THRESHOLD_ALIASED, 174.5f, 175.0f },
    };
    IDWriteFactory *Factory;
    IDWriteFontFace2 *Face;
    UINT i;
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown **)&Factory);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
        return;
    Face = CreateTahomaFace2(Factory);
    ok(Face != NULL, "no Tahoma IDWriteFontFace2\n");
    if (Face)
    {
        for (i = 0; i < ARRAYSIZE(Tests); ++i)
        {
            DWRITE_RENDERING_MODE Below = RecommendedMode(Face, Tests[i].Below, &Tests[i].Transform, Tests[i].Threshold);
            DWRITE_RENDERING_MODE Outline = RecommendedMode(Face, Tests[i].Outline, &Tests[i].Transform, Tests[i].Threshold);

            ok(Below != DWRITE_RENDERING_MODE_OUTLINE, "%s: em %.1f gives outline\n", Tests[i].Name, Tests[i].Below);
            ok(Outline == DWRITE_RENDERING_MODE_OUTLINE, "%s: em %.1f gives mode %d\n", Tests[i].Name, Tests[i].Outline, Outline);
        }
        IDWriteFontFace2_Release(Face);
    }
    IDWriteFactory_Release(Factory);
}
