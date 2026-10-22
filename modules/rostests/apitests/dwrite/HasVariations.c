/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests IDWriteFontFace5::HasVariations against each font's fvar table
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <dwrite_3.h>

static BOOL
HasVariationAxes(
    _In_ IDWriteFontFace5 *Face)
{
    const BYTE *Bytes = NULL;
    void *Context = NULL;
    UINT32 Size = 0;
    BOOL Exists = FALSE, Axes = FALSE;

    if (FAILED(IDWriteFontFace5_TryGetFontTable(Face, DWRITE_MAKE_OPENTYPE_TAG('f', 'v', 'a', 'r'),
                                                (const void **)&Bytes, &Size, &Context, &Exists)))
        return FALSE;
    if (Exists && Size >= 10)
        Axes = Bytes[0] == 0 && Bytes[1] == 1 && Bytes[2] == 0 && Bytes[3] == 0 && (Bytes[8] | Bytes[9]);
    if (Exists)
        IDWriteFontFace5_ReleaseFontTable(Face, Context);
    return Axes;
}

START_TEST(HasVariations)
{
    IDWriteFontCollection *Collection = NULL;
    IDWriteFactory *Factory = NULL;
    UINT32 Family, Families, Checked = 0, Mismatched = 0;
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **)&Factory);
    ok(hr == S_OK, "DWriteCreateFactory failed, hr %#lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IDWriteFactory_GetSystemFontCollection(Factory, &Collection, FALSE);
    ok(hr == S_OK, "GetSystemFontCollection failed, hr %#lx\n", hr);
    if (FAILED(hr))
    {
        IDWriteFactory_Release(Factory);
        return;
    }

    Families = IDWriteFontCollection_GetFontFamilyCount(Collection);
    for (Family = 0; Family < Families; Family++)
    {
        IDWriteFontFamily *FontFamily = NULL;
        IDWriteFont *Font = NULL;
        IDWriteFontFace *Face = NULL;
        IDWriteFontFace5 *Face5 = NULL;

        if (SUCCEEDED(IDWriteFontCollection_GetFontFamily(Collection, Family, &FontFamily)) &&
            SUCCEEDED(IDWriteFontFamily_GetFont(FontFamily, 0, &Font)) &&
            SUCCEEDED(IDWriteFont_CreateFontFace(Font, &Face)) &&
            SUCCEEDED(IDWriteFontFace_QueryInterface(Face, &IID_IDWriteFontFace5, (void **)&Face5)))
        {
            if (!IDWriteFontFace5_HasVariations(Face5) != !HasVariationAxes(Face5))
                Mismatched++;
            Checked++;
            IDWriteFontFace5_Release(Face5);
        }
        if (Face) IDWriteFontFace_Release(Face);
        if (Font) IDWriteFont_Release(Font);
        if (FontFamily) IDWriteFontFamily_Release(FontFamily);
    }
    ok(Checked != 0, "No font face was checked\n");
    ok(Mismatched == 0, "%u of %u font faces report variations that differ from their fvar table\n", Mismatched, Checked);

    IDWriteFontCollection_Release(Collection);
    IDWriteFactory_Release(Factory);
}
