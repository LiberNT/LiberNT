/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the system font fallback maps Japanese text
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <dwrite_2.h>

typedef struct _TEXT_SOURCE
{
    IDWriteTextAnalysisSource IDWriteTextAnalysisSource_iface;
    const WCHAR *pszText;
    UINT32 cchText;
    const WCHAR *pszLocale;
} TEXT_SOURCE;

static inline TEXT_SOURCE *impl_from_source(IDWriteTextAnalysisSource *iface)
{
    return CONTAINING_RECORD(iface, TEXT_SOURCE, IDWriteTextAnalysisSource_iface);
}

static HRESULT WINAPI Source_QueryInterface(IDWriteTextAnalysisSource *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IDWriteTextAnalysisSource) || IsEqualIID(riid, &IID_IUnknown))
    {
        *ppv = iface;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI Source_AddRef(IDWriteTextAnalysisSource *iface)
{
    return 2;
}

static ULONG WINAPI Source_Release(IDWriteTextAnalysisSource *iface)
{
    return 1;
}

static HRESULT WINAPI Source_GetTextAtPosition(IDWriteTextAnalysisSource *iface, UINT32 position,
                                               const WCHAR **text, UINT32 *length)
{
    TEXT_SOURCE *This = impl_from_source(iface);

    if (position >= This->cchText)
    {
        *text = NULL;
        *length = 0;
    }
    else
    {
        *text = This->pszText + position;
        *length = This->cchText - position;
    }
    return S_OK;
}

static HRESULT WINAPI Source_GetTextBeforePosition(IDWriteTextAnalysisSource *iface, UINT32 position,
                                                   const WCHAR **text, UINT32 *length)
{
    TEXT_SOURCE *This = impl_from_source(iface);

    if (!position || position > This->cchText)
    {
        *text = NULL;
        *length = 0;
    }
    else
    {
        *text = This->pszText;
        *length = position;
    }
    return S_OK;
}

static DWRITE_READING_DIRECTION WINAPI Source_GetParagraphReadingDirection(IDWriteTextAnalysisSource *iface)
{
    return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
}

static HRESULT WINAPI Source_GetLocaleName(IDWriteTextAnalysisSource *iface, UINT32 position, UINT32 *length,
                                           const WCHAR **locale)
{
    TEXT_SOURCE *This = impl_from_source(iface);

    *locale = This->pszLocale;
    *length = (position < This->cchText) ? This->cchText - position : 0;
    return S_OK;
}

static HRESULT WINAPI Source_GetNumberSubstitution(IDWriteTextAnalysisSource *iface, UINT32 position,
                                                   UINT32 *length, IDWriteNumberSubstitution **substitution)
{
    *substitution = NULL;
    *length = (position < impl_from_source(iface)->cchText) ? impl_from_source(iface)->cchText - position : 0;
    return S_OK;
}

static IDWriteTextAnalysisSourceVtbl SourceVtbl =
{
    Source_QueryInterface,
    Source_AddRef,
    Source_Release,
    Source_GetTextAtPosition,
    Source_GetTextBeforePosition,
    Source_GetParagraphReadingDirection,
    Source_GetLocaleName,
    Source_GetNumberSubstitution,
};

static void TestMapCharacters(IDWriteFontFallback *pFallback, const WCHAR *pszText, const WCHAR *pszLocale)
{
    TEXT_SOURCE source = { { &SourceVtbl }, pszText, (UINT32)wcslen(pszText), pszLocale };
    UINT32 position = 0, i;
    HRESULT hr;

    while (position < source.cchText)
    {
        IDWriteFontFamily *pFamily = NULL;
        IDWriteLocalizedStrings *pNames = NULL;
        WCHAR szName[LF_FACESIZE] = L"";
        IDWriteFont *pFont = NULL;
        UINT32 cchMapped = 0;
        FLOAT scale = 0.0f;

        hr = IDWriteFontFallback_MapCharacters(pFallback, &source.IDWriteTextAnalysisSource_iface, position,
                                               source.cchText - position, NULL, NULL, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &cchMapped,
                                               &pFont, &scale);
        ok(hr == S_OK, "%ls: MapCharacters at %u returned 0x%lx\n", pszLocale, position, hr);
        ok(cchMapped != 0, "%ls: nothing mapped at %u\n", pszLocale, position);
        ok(pFont != NULL, "%ls: no font at %u\n", pszLocale, position);
        ok(scale == 1.0f, "%ls: scale %f at %u\n", pszLocale, scale, position);
        if (FAILED(hr) || !cchMapped || !pFont)
        {
            if (pFont)
                IDWriteFont_Release(pFont);
            return;
        }

        for (i = position; i < position + cchMapped; i++)
        {
            BOOL fExists = FALSE;
            hr = IDWriteFont_HasCharacter(pFont, pszText[i], &fExists);
            ok(hr == S_OK && fExists, "%ls: the font lacks U+%04X\n", pszLocale, pszText[i]);
        }

        if (SUCCEEDED(IDWriteFont_GetFontFamily(pFont, &pFamily)) &&
            SUCCEEDED(IDWriteFontFamily_GetFamilyNames(pFamily, &pNames)))
        {
            IDWriteLocalizedStrings_GetString(pNames, 0, szName, _countof(szName));
        }
        trace("%ls: %u characters at %u mapped to %ls\n", pszLocale, cchMapped, position, szName);
        if (pNames)
            IDWriteLocalizedStrings_Release(pNames);
        if (pFamily)
            IDWriteFontFamily_Release(pFamily);
        IDWriteFont_Release(pFont);
        position += cchMapped;
    }
}

START_TEST(SystemFontFallback)
{
    static const WCHAR szJapanese[] = L"\x65e5\x672c\x8a9e\x306e\x30c6\x30ad\x30b9\x30c8";
    IDWriteFactory2 *pFactory = NULL;
    IDWriteFontFallback *pFallback = NULL;
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory2, (IUnknown **)&pFactory);
    ok(hr == S_OK, "DWriteCreateFactory returned 0x%lx\n", hr);
    if (FAILED(hr))
        return;

    hr = IDWriteFactory2_GetSystemFontFallback(pFactory, &pFallback);
    ok(hr == S_OK, "GetSystemFontFallback returned 0x%lx\n", hr);
    if (SUCCEEDED(hr))
    {
        TestMapCharacters(pFallback, szJapanese, L"ja-JP");
        TestMapCharacters(pFallback, szJapanese, L"en-US");
        IDWriteFontFallback_Release(pFallback);
    }
    IDWriteFactory2_Release(pFactory);
}
