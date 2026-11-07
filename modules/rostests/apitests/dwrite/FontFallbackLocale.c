/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the system font fallback maps Chinese text for the locale it is given
 * COPYRIGHT:   Copyright 2026 Earzu Chan <contact@earzuchan.me>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <dwrite_2.h>

typedef struct _LOCALE_SOURCE
{
    IDWriteTextAnalysisSource IDWriteTextAnalysisSource_iface;
    const WCHAR *pszText;
    UINT32 cchText;
    const WCHAR *pszLocale;
} LOCALE_SOURCE;

static inline LOCALE_SOURCE *impl_from_source(IDWriteTextAnalysisSource *iface)
{
    return CONTAINING_RECORD(iface, LOCALE_SOURCE, IDWriteTextAnalysisSource_iface);
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
    LOCALE_SOURCE *This = impl_from_source(iface);

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
    *text = NULL;
    *length = 0;
    return S_OK;
}

static DWRITE_READING_DIRECTION WINAPI Source_GetParagraphReadingDirection(IDWriteTextAnalysisSource *iface)
{
    return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
}

static HRESULT WINAPI Source_GetLocaleName(IDWriteTextAnalysisSource *iface, UINT32 position, UINT32 *length,
                                           const WCHAR **locale)
{
    LOCALE_SOURCE *This = impl_from_source(iface);

    *locale = This->pszLocale;
    *length = (position < This->cchText) ? This->cchText - position : 0;
    return S_OK;
}

static HRESULT WINAPI Source_GetNumberSubstitution(IDWriteTextAnalysisSource *iface, UINT32 position,
                                                   UINT32 *length, IDWriteNumberSubstitution **substitution)
{
    *substitution = NULL;
    *length = 0;
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

static void TestFullMapping(IDWriteFontFallback *pFallback, const WCHAR *pszName, const WCHAR *pszText,
                            const WCHAR *pszLocale)
{
    LOCALE_SOURCE source = { { &SourceVtbl }, pszText, (UINT32)wcslen(pszText), pszLocale };
    IDWriteFont *pFont = NULL;
    UINT32 cchMapped = 0xdeadbeef, i;
    FLOAT scale = 0.0f;
    HRESULT hr;

    hr = IDWriteFontFallback_MapCharacters(pFallback, &source.IDWriteTextAnalysisSource_iface, 0, source.cchText,
                                           NULL, NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, &cchMapped, &pFont, &scale);

    ok(hr == S_OK, "%ls: MapCharacters returned 0x%lx\n", pszName, hr);
    ok(cchMapped == source.cchText, "%ls: mapped %u of %u\n", pszName, cchMapped, source.cchText);
    ok(pFont != NULL, "%ls: no font\n", pszName);

    if (pFont)
    {
        for (i = 0; i < source.cchText; i++)
        {
            BOOL fExists = FALSE;

            hr = IDWriteFont_HasCharacter(pFont, pszText[i], &fExists);
            ok(hr == S_OK && fExists, "%ls: the font lacks U+%04X\n", pszName, pszText[i]);
        }
        IDWriteFont_Release(pFont);
    }
}

static BOOL IsUserLocale(const WCHAR *pszLocale)
{
    WCHAR szUser[LOCALE_NAME_MAX_LENGTH];

    return GetUserDefaultLocaleName(szUser, _countof(szUser)) && !_wcsicmp(szUser, pszLocale);
}

START_TEST(FontFallbackLocale)
{
    static const WCHAR szChineseOnly[] = L"\x9898\x8bbe";
    static const WCHAR szMixed[] = L"\x4e3b\x9898\x8bbe";
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
        TestFullMapping(pFallback, L"Chinese-only, no locale", szChineseOnly, NULL);
        TestFullMapping(pFallback, L"mixed, zh-CN", szMixed, L"zh-CN");
        TestFullMapping(pFallback, L"mixed, zh-Hans", szMixed, L"zh-Hans");
        if (IsUserLocale(L"zh-CN"))
            TestFullMapping(pFallback, L"mixed, no locale, zh-CN user", szMixed, NULL);
        IDWriteFontFallback_Release(pFallback);
    }
    IDWriteFactory2_Release(pFactory);
}
