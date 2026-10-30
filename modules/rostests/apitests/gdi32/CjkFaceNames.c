/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the fonts GDI selects for requested and CJK face names
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <strsafe.h>

static int CALLBACK FindFaceProc(const LOGFONTW *plf, const TEXTMETRICW *ptm, DWORD dwType, LPARAM lParam)
{
    *(BYTE *)lParam = ptm->tmPitchAndFamily | 0x80;
    return 0;
}

static BOOL IsProportionalFace(HDC hDC, PCWSTR pszFace)
{
    LOGFONTW lf;
    BYTE bPitch = 0;

    ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    StringCchCopyW(lf.lfFaceName, _countof(lf.lfFaceName), pszFace);
    EnumFontFamiliesExW(hDC, &lf, FindFaceProc, (LPARAM)&bPitch, 0);
    return (bPitch & 0x80) && (bPitch & TMPF_FIXED_PITCH);
}

static HFONT CreateFaceFont(PCWSTR pszFace, BYTE bCharSet, BYTE bPitchAndFamily)
{
    return CreateFontW(-32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, bCharSet, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, bPitchAndFamily, pszFace);
}

static void TestFixedPitchRequest(HDC hDC)
{
    static const PCWSTR s_apszFaces[] =
    {
        L"Arial", L"Times New Roman", L"Tahoma", L"Comic Sans MS", L"Segoe UI",
        L"Noto Sans JP", L"Noto Sans SC", L"Yu Gothic UI", L"Meiryo"
    };
    TEXTMETRICW tm;
    HFONT hFont, hOldFont;
    UINT i, cTested = 0;

    for (i = 0; i < _countof(s_apszFaces); i++)
    {
        if (!IsProportionalFace(hDC, s_apszFaces[i]))
            continue;
        cTested++;
        hFont = CreateFaceFont(s_apszFaces[i], DEFAULT_CHARSET, FIXED_PITCH | FF_MODERN);
        hOldFont = SelectObject(hDC, hFont);
        GetTextMetricsW(hDC, &tm);
        ok(tm.tmPitchAndFamily & TMPF_FIXED_PITCH, "%ls was replaced by a fixed pitch font\n", s_apszFaces[i]);
        SelectObject(hDC, hOldFont);
        DeleteObject(hFont);
    }
    ok(cTested != 0, "no proportional face to test\n");

    hFont = CreateFaceFont(L"LiberNT No Such Face", DEFAULT_CHARSET, FIXED_PITCH | FF_MODERN);
    hOldFont = SelectObject(hDC, hFont);
    GetTextMetricsW(hDC, &tm);
    ok(!(tm.tmPitchAndFamily & TMPF_FIXED_PITCH), "an unknown face gave a proportional font\n");
    SelectObject(hDC, hOldFont);
    DeleteObject(hFont);
}

static void TestCjkFaces(HDC hDC)
{
    static const PCWSTR s_apszFaces[] =
    {
        L"MS UI Gothic", L"MS Gothic", L"MS PGothic", L"MS Mincho", L"MS PMincho", L"Meiryo", L"Meiryo UI",
        L"Yu Gothic", L"Yu Gothic UI", L"SimSun", L"NSimSun", L"Microsoft YaHei", L"Microsoft YaHei UI"
    };
    WORD awGlyphs[3];
    HFONT hFont, hOldFont;
    UINT i;

    for (i = 0; i < _countof(s_apszFaces); i++)
    {
        hFont = CreateFaceFont(s_apszFaces[i], DEFAULT_CHARSET, DEFAULT_PITCH | FF_DONTCARE);
        hOldFont = SelectObject(hDC, hFont);
        ok_long(GetGlyphIndicesW(hDC, L"\x65e5\x672c\x8a9e", 3, awGlyphs, GGI_MARK_NONEXISTING_GLYPHS), 3);
        ok(awGlyphs[0] != 0xFFFF && awGlyphs[1] != 0xFFFF && awGlyphs[2] != 0xFFFF,
           "%ls has no glyph for the kanji\n", s_apszFaces[i]);
        SelectObject(hDC, hOldFont);
        DeleteObject(hFont);
    }
}

static void DrawChar(HDC hDC, HFONT hFont, WCHAR wch)
{
    HFONT hOldFont;
    RECT rc = { 0, 0, 64, 64 };

    FillRect(hDC, &rc, GetStockObject(WHITE_BRUSH));
    hOldFont = SelectObject(hDC, hFont);
    TextOutW(hDC, 8, 8, &wch, 1);
    SelectObject(hDC, hOldFont);
    GdiFlush();
}

static void TestLinkedDrawing(void)
{
    static const PCWSTR s_apszFaces[] = { L"Tahoma", L"MS Shell Dlg", L"Microsoft Sans Serif", L"Arial", L"Courier New" };
    static const BYTE s_abCharSets[] = { DEFAULT_CHARSET, ANSI_CHARSET };
    BITMAPINFO bmi;
    BYTE *pBits, *pKanji;
    HBITMAP hBitmap, hOldBitmap;
    HFONT hFont;
    HDC hDC;
    UINT i, j;
    const SIZE_T cbBits = 64 * 64 * 4;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = 64;
    bmi.bmiHeader.biHeight = -64;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    hDC = CreateCompatibleDC(NULL);
    hBitmap = CreateDIBSection(hDC, &bmi, DIB_RGB_COLORS, (void **)&pBits, NULL, 0);
    hOldBitmap = SelectObject(hDC, hBitmap);
    pKanji = HeapAlloc(GetProcessHeap(), 0, cbBits);

    for (i = 0; i < _countof(s_apszFaces); i++)
    {
        for (j = 0; j < _countof(s_abCharSets); j++)
        {
            hFont = CreateFaceFont(s_apszFaces[i], s_abCharSets[j], DEFAULT_PITCH | FF_DONTCARE);
            DrawChar(hDC, hFont, 0x65e5);
            CopyMemory(pKanji, pBits, cbBits);
            DrawChar(hDC, hFont, 0x672c);
            ok(memcmp(pKanji, pBits, cbBits) != 0, "%ls with charset %u draws the kanji as boxes\n",
               s_apszFaces[i], s_abCharSets[j]);
            DeleteObject(hFont);
        }
    }

    HeapFree(GetProcessHeap(), 0, pKanji);
    SelectObject(hDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hDC);
}

START_TEST(CjkFaceNames)
{
    HDC hDC = CreateCompatibleDC(NULL);

    TestFixedPitchRequest(hDC);
    TestCjkFaces(hDC);
    DeleteDC(hDC);
    TestLinkedDrawing();
}
