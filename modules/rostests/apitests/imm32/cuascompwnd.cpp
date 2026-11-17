/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests where a text service composition is shown in an edit control
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#define WIN32_NO_STATUS
#include <windows.h>
#include <ole2.h>
#include <msctf.h>
#include <imm.h>
#include <apitest.h>
#include "../msctf/testtip.h"

struct COMPWND_SEARCH
{
    HWND hwndExclude1;
    HWND hwndExclude2;
    POINT pt;
    HWND hwndFound;
};

static void Pump(void)
{
    MSG msg;
    for (int i = 0; i < 10; i++)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(30);
    }
}

static BOOL CALLBACK FindCompWnd(HWND hwnd, LPARAM lParam)
{
    COMPWND_SEARCH *pSearch = (COMPWND_SEARCH *)lParam;
    WCHAR szClass[64];
    RECT rc;

    if (hwnd == pSearch->hwndExclude1 || hwnd == pSearch->hwndExclude2 || !IsWindowVisible(hwnd))
        return TRUE;
    GetClassNameW(hwnd, szClass, _countof(szClass));
    GetWindowRect(hwnd, &rc);
    trace("window %p class %ls rect %ld,%ld-%ld,%ld style 0x%08lx\n", hwnd, szClass, rc.left, rc.top, rc.right,
          rc.bottom, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE));
    if (PtInRect(&rc, pSearch->pt))
        pSearch->hwndFound = hwnd;
    return TRUE;
}

static void TestComposition(HWND hwndParent, HWND hwndEdit)
{
    COMPWND_SEARCH search = { hwndParent, hwndEdit };
    WCHAR szText[16];
    TEXTMETRICW tm;
    HDC hdc;
    LONG lLineHeight;
    POINT ptCaret;
    RECT rc;
    HRESULT hr;

    hdc = GetDC(hwndEdit);
    SelectObject(hdc, (HFONT)SendMessageW(hwndEdit, WM_GETFONT, 0, 0));
    GetTextMetricsW(hdc, &tm);
    ReleaseDC(hwndEdit, hdc);
    lLineHeight = tm.tmHeight;

    hr = g_pfnCompose(L"\x3042\x3044", TESTTIP_START);
    Pump();
    ok_hr(hr, S_OK);

    GetWindowTextW(hwndEdit, szText, _countof(szText));
    ok(!lstrcmpW(szText, L"ab"), "text during the composition is %ls\n", szText);

    GetCaretPos(&ptCaret);
    ClientToScreen(hwndEdit, &ptCaret);
    search.pt = ptCaret;
    EnumThreadWindows(GetCurrentThreadId(), FindCompWnd, (LPARAM)&search);
    ok(search.hwndFound != NULL, "no window shows the composition at the caret %ld,%ld\n", ptCaret.x, ptCaret.y);
    if (search.hwndFound)
    {
        GetWindowRect(search.hwndFound, &rc);
        ok_long(rc.left, ptCaret.x);
        ok_long(rc.top, ptCaret.y);
        ok(rc.bottom - rc.top == lLineHeight + 2, "the composition window is %ld high, expected %ld\n",
           rc.bottom - rc.top, lLineHeight + 2);
        ok(!(GetWindowLongPtrW(search.hwndFound, GWL_STYLE) & WS_BORDER), "the composition window has a border\n");
    }

    hr = g_pfnCompose(NULL, TESTTIP_END);
    Pump();
    ok_hr(hr, S_OK);
    GetWindowTextW(hwndEdit, szText, _countof(szText));
    ok(!lstrcmpW(szText, L"ab\x3042\x3044"), "text after the composition is %ls\n", szText);
}

struct ATTR_CASE
{
    TF_DISPLAYATTRIBUTE da;
    ULONG cchFirstClause;
    TF_DISPLAYATTRIBUTE da2;
    BOOL fNoAttribute;
};

static COLORREF CaseColor(const TF_DA_COLOR *pColor, COLORREF crDefault)
{
    if (pColor->type == TF_CT_SYSCOLOR)
        return GetSysColor(pColor->nIndex);
    if (pColor->type == TF_CT_COLORREF)
        return pColor->cr;
    return crDefault;
}

static void ExpectedLine(const TF_DISPLAYATTRIBUTE *pda, LONG x, LONG cxLine, LONG cy, LONG y, LONG px, BOOL *pfLine)
{
    LONG yLine = cy - 2;
    *pfLine = FALSE;
    if (pda->lsStyle == TF_LS_NONE || px < x || px >= x + cxLine)
        return;
    if (pda->lsStyle == TF_LS_SQUIGGLE)
    {
        static const LONG Wave[4] = { 0, 1, 2, 1 };
        static const LONG Trim[4] = { 2, 0, 0, 1 };
        LONG p = px - (x + 2), pLast = cxLine - 3, yWave;
        if (p < 0 || pLast < 0 || p > pLast - Trim[pLast % 4])
            return;
        yWave = yLine - 2 + Wave[p % 4];
        if (pda->fBoldLine)
            *pfLine = (y >= ((p ? yWave - 1 : yWave)) && y <= min(yWave + 1, yLine));
        else
            *pfLine = (y == yWave);
        return;
    }
    LONG cOn = 1, cOff = 0;
    if (pda->lsStyle == TF_LS_DOT)
        cOn = 2, cOff = 1;
    else if (pda->lsStyle == TF_LS_DASH)
        cOn = 4, cOff = 1;
    LONG offset = px - x;
    BOOL fOn = (pda->lsStyle == TF_LS_SOLID) || (offset % (cOn + cOff) < cOn);
    if (y == yLine)
        *pfLine = fOn;
    else if (y == yLine - 1 && pda->fBoldLine)
        *pfLine = fOn && (pda->lsStyle == TF_LS_SOLID ? offset >= 1 : offset % (cOn + cOff) >= 1);
}

static void CheckCompWnd(HWND hwnd, HWND hwndEdit, LPCWSTR pszText, const ATTR_CASE *pCase, UINT iCase)
{
    RECT rc;
    HDC hdcScreen, hdcMem, hdcEdit;
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER) } };
    DWORD *pBits = NULL;
    HBITMAP hbm;
    SIZE sizeFirst = { 0, 0 }, sizeAll;
    TEXTMETRICW tm;
    LONG cx, cy, cchText = lstrlenW(pszText);
    ULONG cBad = 0;
    LONG xBad = -1, yBad = -1;

    hdcEdit = GetDC(hwndEdit);
    SelectObject(hdcEdit, (HFONT)SendMessageW(hwndEdit, WM_GETFONT, 0, 0));
    GetTextExtentPoint32W(hdcEdit, pszText, cchText, &sizeAll);
    if (pCase->cchFirstClause)
        GetTextExtentPoint32W(hdcEdit, pszText, pCase->cchFirstClause, &sizeFirst);
    GetTextMetricsW(hdcEdit, &tm);
    ReleaseDC(hwndEdit, hdcEdit);

    GetWindowRect(hwnd, &rc);
    cx = rc.right - rc.left;
    cy = rc.bottom - rc.top;
    ok(cy == tm.tmHeight + 2, "case %u: window height %ld\n", iCase, cy);
    ok(cx >= sizeAll.cx, "case %u: window width %ld, text %ld\n", iCase, cx, sizeAll.cx);

    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    hdcScreen = GetDC(NULL);
    hdcMem = CreateCompatibleDC(hdcScreen);
    hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, (void **)&pBits, NULL, 0);
    SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, cx, cy, hdcScreen, rc.left, rc.top, SRCCOPY);
    GdiFlush();

    BOOL fTextColorSeen = FALSE;
    for (LONG y = 0; y < cy; ++y)
    {
        for (LONG x = 0; x < sizeAll.cx && x < cx; ++x)
        {
            DWORD px = pBits[y * cx + x];
            COLORREF cr = RGB((px >> 16) & 0xff, (px >> 8) & 0xff, px & 0xff);
            BOOL fSecond = pCase->cchFirstClause && x >= sizeFirst.cx;
            const TF_DISPLAYATTRIBUTE *pda = fSecond ? &pCase->da2 : &pCase->da;
            COLORREF crText = CaseColor(&pda->crText, GetSysColor(COLOR_WINDOWTEXT));
            COLORREF crBk = CaseColor(&pda->crBk, GetSysColor(COLOR_WINDOW));
            COLORREF crLine = CaseColor(&pda->crLine, crText);
            LONG xRun = fSecond ? sizeFirst.cx : 0;
            LONG cxRun = pCase->cchFirstClause ? (fSecond ? sizeAll.cx - sizeFirst.cx : sizeFirst.cx) : sizeAll.cx;
            LONG cxLine = (pCase->cchFirstClause && !fSecond) ? cxRun - (tm.tmHeight - 4) / 4 : cxRun;
            BOOL fLine = FALSE;

            if (cr == crText)
                fTextColorSeen = TRUE;
            if (y < cy - 5 && y > 0)
                continue;
            if (pCase->fNoAttribute)
                fLine = FALSE;
            else
                ExpectedLine(pda, xRun, cxLine, cy, y, x, &fLine);
            if (cr != (fLine ? crLine : crBk))
            {
                if (!cBad++)
                {
                    xBad = x;
                    yBad = y;
                }
            }
        }
    }
    ok(cBad == 0, "case %u: %lu pixels differ, first at %ld,%ld\n", iCase, cBad, xBad, yBad);
    if (!pCase->fNoAttribute && pCase->da.crText.type != TF_CT_NONE)
        ok(fTextColorSeen, "case %u: no pixel in the text color\n", iCase);

    DeleteDC(hdcMem);
    DeleteObject(hbm);
    ReleaseDC(NULL, hdcScreen);
}

#define RED { TF_CT_COLORREF, { RGB(255, 0, 0) } }
#define BLUE { TF_CT_COLORREF, { RGB(0, 0, 255) } }
#define GREEN { TF_CT_COLORREF, { RGB(0, 255, 0) } }
#define MAGENTA { TF_CT_COLORREF, { RGB(255, 0, 255) } }
#define NOCOLOR { TF_CT_NONE, { 0 } }

static void TestDisplayAttributes(HWND hwndParent, HWND hwndEdit)
{
    static const ATTR_CASE Cases[] =
    {
        { { RED, BLUE, TF_LS_SOLID, FALSE, GREEN, TF_ATTR_INPUT } },
        { { RED, BLUE, TF_LS_SOLID, TRUE, GREEN, TF_ATTR_INPUT } },
        { { RED, BLUE, TF_LS_DOT, FALSE, GREEN, TF_ATTR_INPUT } },
        { { RED, BLUE, TF_LS_DASH, FALSE, GREEN, TF_ATTR_INPUT } },
        { { RED, BLUE, TF_LS_SQUIGGLE, FALSE, GREEN, TF_ATTR_INPUT } },
        { { RED, BLUE, TF_LS_NONE, FALSE, GREEN, TF_ATTR_INPUT } },
        { { NOCOLOR, NOCOLOR, TF_LS_SOLID, FALSE, NOCOLOR, TF_ATTR_TARGET_CONVERTED } },
        { { NOCOLOR, NOCOLOR, TF_LS_DOT, TRUE, NOCOLOR, TF_ATTR_INPUT } },
        { { { TF_CT_SYSCOLOR, { COLOR_HIGHLIGHTTEXT } }, { TF_CT_SYSCOLOR, { COLOR_HIGHLIGHT } }, TF_LS_SOLID, FALSE,
            GREEN, TF_ATTR_INPUT } },
        { { RED, BLUE, TF_LS_SQUIGGLE, TRUE, GREEN, TF_ATTR_INPUT } },
        { { RED, BLUE, TF_LS_SOLID, FALSE, NOCOLOR, TF_ATTR_INPUT } },
        { { RED, BLUE, TF_LS_SOLID, TRUE, GREEN, TF_ATTR_TARGET_CONVERTED }, 1,
          { RED, BLUE, TF_LS_SOLID, FALSE, MAGENTA, TF_ATTR_CONVERTED } },
        { { RED, BLUE, TF_LS_SOLID, FALSE, GREEN, TF_ATTR_CONVERTED }, 1,
          { RED, BLUE, TF_LS_DOT, FALSE, MAGENTA, TF_ATTR_CONVERTED } },
        { { }, 0, { }, TRUE },
    };
    static const struct { LPCWSTR pszText; INT nHeight; } Texts[] =
    {
        { L"\x3042\x3044", -32 },
        { L"abc", -20 },
        { L"ar", -64 },
        { L"arr", -64 },
        { L"arrr", -64 },
    };
    COMPWND_SEARCH search = { hwndParent, hwndEdit };
    ITfCategoryMgr *pCategoryMgr = NULL;
    POINT ptCaret;
    HRESULT hr;
    UINT iCase = 0;

    hr = CoCreateInstance(CLSID_TF_CategoryMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, (void **)&pCategoryMgr);
    ok_hr(hr, S_OK);
    if (pCategoryMgr)
        ok_hr(pCategoryMgr->RegisterCategory(CLSID_TestTip, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, CLSID_TestTip), S_OK);

    for (UINT t = 0; t < _countof(Texts); ++t)
    {
        HFONT hFont = CreateFontW(Texts[t].nHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0,
                                  0, L"Arial");
        HFONT hOldFont = (HFONT)SendMessageW(hwndEdit, WM_GETFONT, 0, 0);
        SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        for (UINT i = 0; i < _countof(Cases); ++i, ++iCase)
        {
            g_pTip->fSetAttribute = !Cases[i].fNoAttribute;
            g_pTip->da = Cases[i].da;
            g_pTip->da2 = Cases[i].da2;
            g_pTip->cchFirstClause = Cases[i].cchFirstClause;
            g_pTip->guidAttribute = GUID_TestDisplayAttribute;
            g_pTip->guidAttribute.Data1 += 0x100 + iCase;
            g_pTip->guidAttribute2 = GUID_TestDisplayAttribute;
            g_pTip->guidAttribute2.Data1 += 0x200 + iCase;
            SetWindowTextW(hwndEdit, L"ab");
            SendMessageW(hwndEdit, EM_SETSEL, 2, 2);
            hr = g_pfnCompose(Texts[t].pszText, TESTTIP_START);
            Pump();
            ok_hr(hr, S_OK);
            GetCaretPos(&ptCaret);
            ClientToScreen(hwndEdit, &ptCaret);
            search.pt = ptCaret;
            search.hwndFound = NULL;
            EnumThreadWindows(GetCurrentThreadId(), FindCompWnd, (LPARAM)&search);
            ok(search.hwndFound != NULL, "case %u: no composition window\n", iCase);
            if (search.hwndFound)
                CheckCompWnd(search.hwndFound, hwndEdit, Texts[t].pszText, &Cases[i], iCase);
            g_pfnCompose(NULL, TESTTIP_END);
            Pump();
        }
        SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)hOldFont, TRUE);
        DeleteObject(hFont);
    }
    g_pTip->fSetAttribute = FALSE;
    g_pTip->cchFirstClause = 0;
    if (pCategoryMgr)
    {
        pCategoryMgr->UnregisterCategory(CLSID_TestTip, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, CLSID_TestTip);
        pCategoryMgr->Release();
    }
}

START_TEST(cuascompwnd)
{
    ITfThreadMgr *pThreadMgr = NULL;
    TfClientId tid;
    HWND hwndParent, hwndEdit;
    HFONT hFont;

    CoInitialize(NULL);
    if (!TestTip_Register())
    {
        skip("Cannot register the test text service; run as administrator\n");
        TestTip_Unregister();
        CoUninitialize();
        return;
    }

    hwndParent = CreateWindowW(L"STATIC", L"cuascompwnd", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 560, 160, NULL,
                               NULL, GetModuleHandleW(NULL), NULL);
    hwndEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"ab", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 20, 500,
                               50, hwndParent, NULL, GetModuleHandleW(NULL), NULL);
    hFont = CreateFontW(-32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
    SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SetForegroundWindow(hwndParent);
    SetFocus(hwndEdit);
    SendMessageW(hwndEdit, EM_SETSEL, 2, 2);
    Pump();

    CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void **)&pThreadMgr);
    if (pThreadMgr)
        pThreadMgr->Activate(&tid);
    TestTip_Activate();
    if (pThreadMgr)
    {
        ITfKeystrokeMgr *pKeystrokeMgr = NULL;
        BOOL fEaten;
        pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr);
        if (pKeystrokeMgr)
        {
            pKeystrokeMgr->TestKeyDown(VK_F24, 1, &fEaten);
            pKeystrokeMgr->Release();
        }
    }
    Pump();

    if (g_pTip->tid && g_pfnCompose)
    {
        TestComposition(hwndParent, hwndEdit);
        TestDisplayAttributes(hwndParent, hwndEdit);
    }
    else
        skip("The test text service did not activate\n");

    DestroyWindow(hwndParent);
    DeleteObject(hFont);
    TestTip_Deactivate();
    if (pThreadMgr)
    {
        pThreadMgr->Deactivate();
        pThreadMgr->Release();
    }
    TestTip_Unregister();
    CoUninitialize();
}
