/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a linked or fallback font is drawn on the baseline of the selected font
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define WIDTH 400
#define HEIGHT 120

static void InkRows(const DWORD *pBits, int xStart, int xEnd, int *pTop, int *pBottom)
{
    int x, y;

    *pTop = -1;
    *pBottom = -1;
    for (y = 0; y < HEIGHT; y++)
    {
        for (x = xStart; x < xEnd; x++)
        {
            if ((pBits[y * WIDTH + x] & 0xffffff) != 0xffffff)
            {
                if (*pTop < 0)
                    *pTop = y;
                *pBottom = y;
                break;
            }
        }
    }
}

static void TestFace(LPCWSTR pszFace, DWORD dwQuality)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), WIDTH, -HEIGHT, 1, 32, BI_RGB } };
    HDC hdc;
    HBITMAP hbm, hbmOld;
    HFONT hFont, hFontOld;
    DWORD *pBits = NULL;
    TEXTMETRICW tm;
    SIZE sizeLatin;
    RECT rc = { 0, 0, WIDTH, HEIGHT };
    int topLatin, bottomLatin, topCjk, bottomCjk, baseline;

    winetest_push_context("%ls quality %lu", pszFace, dwQuality);
    hdc = CreateCompatibleDC(NULL);
    hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&pBits, NULL, 0);
    ok(hbm != NULL, "CreateDIBSection failed\n");
    hbmOld = SelectObject(hdc, hbm);
    FillRect(hdc, &rc, GetStockObject(WHITE_BRUSH));
    hFont = CreateFontW(-32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, dwQuality, DEFAULT_PITCH, pszFace);
    hFontOld = SelectObject(hdc, hFont);
    GetTextMetricsW(hdc, &tm);
    GetTextExtentPoint32W(hdc, L"bx", 2, &sizeLatin);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));
    ok(TextOutW(hdc, 10, 20, L"bx\x65e5\x672c", 4), "TextOutW failed\n");
    GdiFlush();

    baseline = 20 + tm.tmAscent;
    InkRows(pBits, 10, 10 + sizeLatin.cx, &topLatin, &bottomLatin);
    InkRows(pBits, 10 + sizeLatin.cx + 1, WIDTH, &topCjk, &bottomCjk);
    trace("ascent %ld descent %ld baseline %d, latin %d-%d, CJK %d-%d\n", tm.tmAscent, tm.tmDescent, baseline,
          topLatin, bottomLatin, topCjk, bottomCjk);
    ok(bottomLatin >= 0 && bottomCjk >= 0, "nothing was drawn\n");
    ok(bottomCjk <= bottomLatin + 3, "the CJK glyphs end at %d, the latin ones at %d\n", bottomCjk, bottomLatin);
    ok(bottomCjk >= bottomLatin - 3, "the CJK glyphs end at %d, the latin ones at %d\n", bottomCjk, bottomLatin);
    ok(topCjk >= 20, "the CJK glyphs start at %d, above the line at 20\n", topCjk);

    SelectObject(hdc, hFontOld);
    DeleteObject(hFont);
    SelectObject(hdc, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdc);
    winetest_pop_context();
}

static void TestEdit(void)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), WIDTH, -HEIGHT, 1, 32, BI_RGB } };
    HWND hwndParent, hwndEdit;
    HDC hdc, hdcEdit;
    HBITMAP hbm, hbmOld;
    HFONT hFont, hFontOld;
    DWORD *pBits = NULL;
    TEXTMETRICW tm;
    SIZE sizeLatin;
    RECT rc = { 0, 0, WIDTH, HEIGHT };
    int topLatin, bottomLatin, topCjk, bottomCjk, xText;
    LRESULT pos;

    hwndParent = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"FontLinkBaseline", WS_POPUP | WS_VISIBLE, 0, 0, WIDTH, HEIGHT, NULL,
                                 NULL, GetModuleHandleW(NULL), NULL);
    hwndEdit = CreateWindowExW(0, L"EDIT", L"bx\x65e5\x672c", WS_CHILD | WS_VISIBLE, 0, 0, WIDTH, HEIGHT, hwndParent,
                               NULL, GetModuleHandleW(NULL), NULL);
    ok(hwndEdit != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!hwndEdit)
    {
        DestroyWindow(hwndParent);
        return;
    }
    hFont = CreateFontW(-32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
    SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    UpdateWindow(hwndParent);
    UpdateWindow(hwndEdit);
    {
        MSG msg;
        for (int i = 0; i < 10; i++)
        {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessageW(&msg);
            Sleep(30);
        }
    }

    hdcEdit = GetDC(hwndEdit);
    hFontOld = SelectObject(hdcEdit, hFont);
    GetTextMetricsW(hdcEdit, &tm);
    GetTextExtentPoint32W(hdcEdit, L"bx", 2, &sizeLatin);
    SelectObject(hdcEdit, hFontOld);
    ReleaseDC(hwndEdit, hdcEdit);

    hdc = CreateCompatibleDC(NULL);
    hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&pBits, NULL, 0);
    hbmOld = SelectObject(hdc, hbm);
    FillRect(hdc, &rc, GetStockObject(WHITE_BRUSH));
    hdcEdit = GetDC(hwndEdit);
    ok(BitBlt(hdc, 0, 0, WIDTH, HEIGHT, hdcEdit, 0, 0, SRCCOPY), "BitBlt failed\n");
    ReleaseDC(hwndEdit, hdcEdit);
    GdiFlush();

    pos = SendMessageW(hwndEdit, EM_POSFROMCHAR, 0, 0);
    xText = (short)LOWORD(pos);
    InkRows(pBits, xText, xText + sizeLatin.cx, &topLatin, &bottomLatin);
    InkRows(pBits, xText + sizeLatin.cx + 1, WIDTH, &topCjk, &bottomCjk);
    trace("edit: text at %d,%d ascent %ld, latin %d-%d, CJK %d-%d\n", xText, (short)HIWORD(pos), tm.tmAscent,
          topLatin, bottomLatin, topCjk, bottomCjk);
    ok(bottomLatin >= 0 && bottomCjk >= 0, "nothing was drawn\n");
    ok(bottomCjk <= bottomLatin + 3, "the CJK glyphs end at %d, the latin ones at %d\n", bottomCjk, bottomLatin);
    ok(bottomCjk >= bottomLatin - 3, "the CJK glyphs end at %d, the latin ones at %d\n", bottomCjk, bottomLatin);

    SelectObject(hdc, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdc);
    DestroyWindow(hwndParent);
    DeleteObject(hFont);
}

START_TEST(FontLinkBaseline)
{
    static const DWORD Qualities[] = { NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY, DEFAULT_QUALITY };

    for (UINT i = 0; i < _countof(Qualities); i++)
    {
        TestFace(L"Arial", Qualities[i]);
        TestFace(L"Tahoma", Qualities[i]);
    }
    TestEdit();
}
