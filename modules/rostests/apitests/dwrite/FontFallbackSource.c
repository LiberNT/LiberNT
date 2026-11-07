/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests which positions the system font fallback asks a text analysis source for
 * COPYRIGHT:   Copyright 2026 Earzu Chan <contact@earzuchan.me>
 */

#define COBJMACROS
#include <apitest.h>
#include <initguid.h>
#include <dwrite_2.h>

typedef struct _CHUNK_SOURCE
{
    IDWriteTextAnalysisSource IDWriteTextAnalysisSource_iface;
    const WCHAR *pszText;
    UINT32 cchText;
    UINT32 cchChunk;
    BOOL fFirstOnly;
    UINT32 uMaxPosition;
    UINT32 cNulls;
} CHUNK_SOURCE;

static inline CHUNK_SOURCE *impl_from_source(IDWriteTextAnalysisSource *iface)
{
    return CONTAINING_RECORD(iface, CHUNK_SOURCE, IDWriteTextAnalysisSource_iface);
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
    CHUNK_SOURCE *This = impl_from_source(iface);
    UINT32 cchAvailable;

    if (position > This->uMaxPosition)
        This->uMaxPosition = position;

    if (position >= This->cchText || (This->fFirstOnly && position != 0))
    {
        *text = NULL;
        *length = 0;
        This->cNulls++;
        return S_OK;
    }

    cchAvailable = This->cchText - position;
    *text = This->pszText + position;
    *length = (This->cchChunk && This->cchChunk < cchAvailable) ? This->cchChunk : cchAvailable;
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
    *locale = NULL;
    *length = 0;
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

typedef struct _TEXT_CASE
{
    const char *pszName;
    const WCHAR *pszText;
    BOOL fFullyMapped;
} TEXT_CASE;

typedef struct _SOURCE_MODE
{
    const char *pszName;
    UINT32 cchChunk;
    BOOL fFirstOnly;
} SOURCE_MODE;

static void TestMapCharacters(IDWriteFontFallback *pFallback, const SOURCE_MODE *pMode, const TEXT_CASE *pCase,
                              UINT32 position, UINT32 cchRange, UINT32 uExpectedMaxPosition, BOOL fCheckMaxPosition)
{
    CHUNK_SOURCE source = { { &SourceVtbl }, pCase->pszText, (UINT32)wcslen(pCase->pszText), pMode->cchChunk,
                            pMode->fFirstOnly, 0, 0 };
    IDWriteFont *pFont = NULL;
    UINT32 cchMapped = 0xdeadbeef;
    FLOAT scale = 0.0f;
    HRESULT hr;

    hr = IDWriteFontFallback_MapCharacters(pFallback, &source.IDWriteTextAnalysisSource_iface, position, cchRange,
                                           NULL, L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, &cchMapped, &pFont, &scale);

    ok(hr == S_OK, "%s/%s: MapCharacters returned 0x%lx\n", pMode->pszName, pCase->pszName, hr);
    ok(cchMapped >= 1 && cchMapped <= cchRange, "%s/%s: mapped %u of %u\n", pMode->pszName, pCase->pszName,
       cchMapped, cchRange);
    if (pCase->fFullyMapped)
    {
        ok(cchMapped == cchRange, "%s/%s: mapped %u of %u\n", pMode->pszName, pCase->pszName, cchMapped, cchRange);
    }
    if (fCheckMaxPosition)
    {
        ok(source.uMaxPosition == uExpectedMaxPosition, "%s/%s: the source was asked up to position %u, expected %u\n",
           pMode->pszName, pCase->pszName, source.uMaxPosition, uExpectedMaxPosition);
    }
    ok(source.cNulls == 0, "%s/%s: the source was asked %u times for text it does not have\n", pMode->pszName,
       pCase->pszName, source.cNulls);

    if (pFont)
        IDWriteFont_Release(pFont);
}

START_TEST(FontFallbackSource)
{
    static const TEXT_CASE Cases[] =
    {
        { "A", L"A", TRUE },
        { "AB", L"AB", TRUE },
        { "ABC", L"ABC", TRUE },
        { "emoji", L"\xD83D\xDE00", TRUE },
        { "A+emoji", L"A\xD83D\xDE00", FALSE },
        { "emoji+A", L"\xD83D\xDE00" L"A", FALSE },
        { "A+lone high", L"A\xD83D", FALSE },
        { "lone high+A", L"\xD83D" L"A", FALSE },
    };
    static const SOURCE_MODE Modes[] =
    {
        { "whole", 0, FALSE },
        { "first-only", 0, TRUE },
        { "chunk1", 1, FALSE },
        { "chunk2", 2, FALSE },
    };
    static const TEXT_CASE Start1 = { "ABC from 1", L"ABC", TRUE };
    IDWriteFactory2 *pFactory = NULL;
    IDWriteFontFallback *pFallback = NULL;
    UINT32 iMode, iCase, cch;
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory2, (IUnknown **)&pFactory);
    ok(hr == S_OK, "DWriteCreateFactory returned 0x%lx\n", hr);
    if (FAILED(hr))
        return;

    hr = IDWriteFactory2_GetSystemFontFallback(pFactory, &pFallback);
    ok(hr == S_OK, "GetSystemFontFallback returned 0x%lx\n", hr);
    if (SUCCEEDED(hr))
    {
        for (iMode = 0; iMode < _countof(Modes); iMode++)
        {
            for (iCase = 0; iCase < _countof(Cases); iCase++)
            {
                BOOL fSplit;

                cch = (UINT32)wcslen(Cases[iCase].pszText);
                fSplit = Modes[iMode].cchChunk && Modes[iMode].cchChunk < cch;
                TestMapCharacters(pFallback, &Modes[iMode], &Cases[iCase], 0, cch, fSplit ? cch - 1 : 0,
                                  !Modes[iMode].cchChunk || Cases[iCase].fFullyMapped);
            }
        }

        TestMapCharacters(pFallback, &Modes[0], &Start1, 1, 2, 1, TRUE);
        IDWriteFontFallback_Release(pFallback);
    }
    IDWriteFactory2_Release(pFactory);
}
