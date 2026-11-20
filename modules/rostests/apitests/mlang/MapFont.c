/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that IMLangFontLink::MapFont maps a code page to a font that has its characters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <objbase.h>
#include <mlang.h>

START_TEST(MapFont)
{
    static const WCHAR Text[] = L"\x6211\x7684\x7535\x8111";
    IMLangFontLink *FontLink = NULL;
    DWORD CodePages = 0, ChineseSimplified = 0, Count;
    LONG Processed = 0;
    HFONT Base, Mapped = NULL;
    HGDIOBJ Old;
    WORD Glyphs[ARRAYSIZE(Text) - 1];
    HRESULT hr;
    HDC hdc;
    ULONG i;

    hr = CoInitialize(NULL);
    ok_hr(hr, S_OK);
    hr = CoCreateInstance(&CLSID_CMultiLanguage, NULL, CLSCTX_INPROC_SERVER, &IID_IMLangFontLink, (void **)&FontLink);
    ok_hr(hr, S_OK);
    if (FAILED(hr))
    {
        CoUninitialize();
        return;
    }

    hr = IMLangFontLink_CodePageToCodePages(FontLink, 936, &ChineseSimplified);
    ok_hr(hr, S_OK);
    hr = IMLangFontLink_GetStrCodePages(FontLink, Text, ARRAYSIZE(Glyphs), 0, &CodePages, &Processed);
    ok_hr(hr, S_OK);
    ok_long(Processed, ARRAYSIZE(Glyphs));
    ok(CodePages == ChineseSimplified, "Code pages are 0x%lx, expected 0x%lx\n", CodePages, ChineseSimplified);

    hdc = CreateCompatibleDC(NULL);
    ok(hdc != NULL, "CreateCompatibleDC failed\n");
    Base = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
    ok(Base != NULL, "CreateFontW failed\n");

    hr = IMLangFontLink_MapFont(FontLink, hdc, ChineseSimplified, Base, &Mapped);
    ok_hr(hr, S_OK);
    if (SUCCEEDED(hr))
    {
        ok(Mapped != NULL, "MapFont returned no font\n");
        Old = SelectObject(hdc, Mapped);
        Count = GetGlyphIndicesW(hdc, Text, ARRAYSIZE(Glyphs), Glyphs, GGI_MARK_NONEXISTING_GLYPHS);
        ok_long(Count, ARRAYSIZE(Glyphs));
        for (i = 0; i < ARRAYSIZE(Glyphs); i++)
            ok(Glyphs[i] != 0xffff, "The mapped font has no glyph for U+%04X\n", Text[i]);
        SelectObject(hdc, Old);
        hr = IMLangFontLink_ReleaseFont(FontLink, Mapped);
        ok_hr(hr, S_OK);
    }

    DeleteObject(Base);
    DeleteDC(hdc);
    IMLangFontLink_Release(FontLink);
    CoUninitialize();
}
