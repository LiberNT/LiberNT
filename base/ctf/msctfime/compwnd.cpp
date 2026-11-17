/*
 * PROJECT:     LiberNT msctfime.ime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Composition window of the IME UI
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "msctfime.h"

#define COMPWND_CLASS L"MSCTFIME CompositionString"

static ATOM s_CompWndAtom = 0;

CCompositionWindow::CCompositionWindow(HWND hwndUI)
    : m_hwndUI(hwndUI)
    , m_hWnd(NULL)
    , m_hFont(NULL)
    , m_pszText(NULL)
    , m_pDispAttr(NULL)
    , m_pClauseStart(NULL)
    , m_cchText(0)
    , m_dwCursor(0)
    , m_bAllowed(TRUE)
{
}

CCompositionWindow::~CCompositionWindow()
{
    if (m_hWnd)
        ::DestroyWindow(m_hWnd);
    if (m_hFont)
        ::DeleteObject(m_hFont);
    cicMemFree(m_pszText);
    cicMemFree(m_pDispAttr);
    cicMemFree(m_pClauseStart);
}

BOOL CCompositionWindow::EnsureWindow()
{
    if (m_hWnd && ::IsWindow(m_hWnd))
        return TRUE;

    if (!s_CompWndAtom)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_IME | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = CCompositionWindow::WndProc;
        wc.cbWndExtra = sizeof(LONG_PTR);
        wc.hInstance = g_hInst;
        wc.hCursor = ::LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = COMPWND_CLASS;
        s_CompWndAtom = ::RegisterClassExW(&wc);
        if (!s_CompWndAtom && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return FALSE;
    }

    m_hWnd = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, COMPWND_CLASS, NULL,
                               WS_POPUP | WS_DISABLED, 0, 0, 0, 0, m_hwndUI, NULL, g_hInst, this);
    return m_hWnd != NULL;
}

void CCompositionWindow::Hide()
{
    if (m_hWnd && ::IsWindowVisible(m_hWnd))
        ::ShowWindow(m_hWnd, SW_HIDE);
}

BOOL CCompositionWindow::GetVisibleRect(_Out_ RECT *prc)
{
    HWND hWnd = NULL;
    while ((hWnd = ::FindWindowExW(NULL, hWnd, COMPWND_CLASS, NULL)) != NULL)
    {
        if (::GetWindowThreadProcessId(hWnd, NULL) == ::GetCurrentThreadId() && ::IsWindowVisible(hWnd))
            return ::GetWindowRect(hWnd, prc);
    }
    return FALSE;
}

void CCompositionWindow::UnregisterWndClass()
{
    if (s_CompWndAtom)
    {
        ::UnregisterClassW(COMPWND_CLASS, g_hInst);
        s_CompWndAtom = 0;
    }
}

void CCompositionWindow::SetAllowed(BOOL bAllowed)
{
    m_bAllowed = bAllowed;
    if (!bAllowed)
        Hide();
}

void CCompositionWindow::Update(HIMC hIMC)
{
    if (!m_bAllowed || !hIMC)
    {
        Hide();
        return;
    }

    CicIMCLock imcLock(hIMC);
    if (FAILED(imcLock.m_hr))
    {
        Hide();
        return;
    }

    HWND hwndTarget = imcLock.get().hWnd;
    DWORD cchText = 0, dwCursor = 0;
    {
        CicIMCCLock<COMPOSITIONSTRING> compStr(imcLock.get().hCompStr);
        if (FAILED(compStr.m_hr))
        {
            Hide();
            return;
        }
        const COMPOSITIONSTRING *pCS = &compStr.get();
        cchText = pCS->dwCompStrLen;
        dwCursor = pCS->dwCursorPos;

        cicMemFree(m_pszText);
        cicMemFree(m_pDispAttr);
        cicMemFree(m_pClauseStart);
        m_pszText = NULL;
        m_pDispAttr = NULL;
        m_pClauseStart = NULL;
        m_cchText = 0;
        if (cchText)
        {
            m_pszText = (LPWSTR)cicMemAlloc(cchText * sizeof(WCHAR));
            m_pDispAttr = (TF_DISPLAYATTRIBUTE *)cicMemAllocClear(cchText * sizeof(TF_DISPLAYATTRIBUTE));
            m_pClauseStart = (LPBYTE)cicMemAllocClear(cchText + 1);
            if (!m_pszText || !m_pDispAttr || !m_pClauseStart)
            {
                Hide();
                return;
            }
            CopyMemory(m_pszText, (const BYTE *)pCS + pCS->dwCompStrOffset, cchText * sizeof(WCHAR));
            const DWORD *pClause = (const DWORD *)((const BYTE *)pCS + pCS->dwCompClauseOffset);
            for (DWORD i = 0; i < pCS->dwCompClauseLen / sizeof(DWORD); ++i)
            {
                if (pClause[i] < cchText)
                    m_pClauseStart[pClause[i]] = TRUE;
            }
            m_cchText = cchText;
        }
    }
    m_dwCursor = min(dwCursor, cchText);

    if (m_cchText)
    {
        CicIMCCLock<CTFIMECONTEXT> imeContext(imcLock.get().hCtfImeContext);
        CicInputContext *pCicIC = SUCCEEDED(imeContext.m_hr) ? imeContext.get().m_pCicIC : NULL;
        DWORD cchDispAttr = 0;
        const TF_DISPLAYATTRIBUTE *pDispAttr = pCicIC ? pCicIC->GetCompositionDisplayAttributes(&cchDispAttr) : NULL;
        if (pDispAttr && cchDispAttr == m_cchText)
            CopyMemory(m_pDispAttr, pDispAttr, m_cchText * sizeof(TF_DISPLAYATTRIBUTE));
    }

    if (!m_cchText || !hwndTarget || !EnsureWindow())
    {
        Hide();
        return;
    }

    if (m_hFont)
    {
        ::DeleteObject(m_hFont);
        m_hFont = NULL;
    }
    if (imcLock.get().lfFont.W.lfFaceName[0] || imcLock.get().lfFont.W.lfHeight)
    {
        LOGFONTW lf = imcLock.get().lfFont.W;
        lf.lfEscapement = lf.lfOrientation = 0;
        if (lf.lfFaceName[0] == L'@')
            MoveMemory(lf.lfFaceName, &lf.lfFaceName[1], sizeof(lf.lfFaceName) - sizeof(WCHAR));
        m_hFont = ::CreateFontIndirectW(&lf);
    }

    POINT pt = { 0, 0 };
    if (imcLock.get().cfCompForm.dwStyle & (CFS_POINT | CFS_FORCE_POSITION | CFS_RECT))
        pt = imcLock.get().cfCompForm.ptCurrentPos;
    else if (!::GetCaretPos(&pt))
        pt.x = pt.y = 0;
    ::ClientToScreen(hwndTarget, &pt);
    imcLock.unlock();

    HDC hDC = ::GetDC(m_hWnd);
    HGDIOBJ hOldFont = ::SelectObject(hDC, m_hFont ? (HGDIOBJ)m_hFont : ::GetStockObject(DEFAULT_GUI_FONT));
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hDC, m_pszText, m_cchText, &size);
    ::SelectObject(hDC, hOldFont);
    ::ReleaseDC(m_hWnd, hDC);

    ::SetWindowPos(m_hWnd, HWND_TOPMOST, pt.x, pt.y, size.cx + 1, size.cy + 2, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::InvalidateRect(m_hWnd, NULL, TRUE);
}

static COLORREF
DisplayAttributeColor(_In_ const TF_DA_COLOR *pColor, _In_ COLORREF crDefault)
{
    switch (pColor->type)
    {
        case TF_CT_SYSCOLOR:
            return ::GetSysColor(pColor->nIndex);
        case TF_CT_COLORREF:
            return pColor->cr;
        default:
            return crDefault;
    }
}

static void
FillPixels(_In_ HDC hDC, _In_ INT x0, _In_ INT x1, _In_ INT y, _In_ HBRUSH hbr)
{
    if (x0 > x1)
        return;
    RECT rc = { x0, y, x1 + 1, y + 1 };
    ::FillRect(hDC, &rc, hbr);
}

static void
DrawDisplayAttributeLine(_In_ HDC hDC, _In_ const TF_DISPLAYATTRIBUTE *pda, _In_ INT x, _In_ INT cx, _In_ INT cy,
                         _In_ COLORREF crLine)
{
    if (pda->lsStyle == TF_LS_NONE || cx <= 0)
        return;

    HBRUSH hbr = ::CreateSolidBrush(crLine);
    INT yLine = cy - 2;
    INT xEnd = x + cx - 1;
    if (pda->lsStyle == TF_LS_SQUIGGLE)
    {
        static const INT Wave[4] = { 0, 1, 2, 1 };
        static const INT Trim[4] = { 2, 0, 0, 1 };
        INT pLast = xEnd - (x + 2);
        if (pLast >= 0)
            xEnd -= Trim[pLast % 4];
        for (INT px = x + 2; px <= xEnd; ++px)
        {
            INT p = px - (x + 2);
            INT y = yLine - 2 + Wave[p % 4];
            INT yTop = (pda->fBoldLine && p) ? y - 1 : y;
            INT yBottom = pda->fBoldLine ? min(y + 1, yLine) : y;
            RECT rc = { px, yTop, px + 1, yBottom + 1 };
            ::FillRect(hDC, &rc, hbr);
        }
    }
    else
    {
        INT cOn = cx, cOff = 0;
        if (pda->lsStyle == TF_LS_DOT)
            cOn = 2, cOff = 1;
        else if (pda->lsStyle == TF_LS_DASH)
            cOn = 4, cOff = 1;
        for (INT x0 = x; x0 <= xEnd; x0 += cOn + cOff)
        {
            INT x1 = min(x0 + cOn - 1, xEnd);
            FillPixels(hDC, x0, x1, yLine, hbr);
            if (pda->fBoldLine)
                FillPixels(hDC, x0 + 1, x1, yLine - 1, hbr);
        }
    }
    ::DeleteObject(hbr);
}

void CCompositionWindow::Paint(HDC hDC)
{
    RECT rcClient;
    ::GetClientRect(m_hWnd, &rcClient);
    ::FillRect(hDC, &rcClient, ::GetSysColorBrush(COLOR_WINDOW));
    if (!m_cchText)
        return;

    HGDIOBJ hOldFont = ::SelectObject(hDC, m_hFont ? (HGDIOBJ)m_hFont : ::GetStockObject(DEFAULT_GUI_FONT));
    ::SetBkMode(hDC, TRANSPARENT);

    TEXTMETRICW tm;
    ::GetTextMetricsW(hDC, &tm);
    INT y = 0;
    INT baseline = y + tm.tmHeight;
    INT cy = rcClient.bottom - rcClient.top;

    DWORD start = 0;
    INT x = 0;
    while (start < m_cchText)
    {
        DWORD end = start + 1;
        while (end < m_cchText && !m_pClauseStart[end] &&
               !memcmp(&m_pDispAttr[end], &m_pDispAttr[start], sizeof(TF_DISPLAYATTRIBUTE)))
            ++end;

        const TF_DISPLAYATTRIBUTE *pda = &m_pDispAttr[start];
        SIZE size;
        ::GetTextExtentPoint32W(hDC, m_pszText + start, end - start, &size);
        if (pda->crBk.type != TF_CT_NONE)
        {
            HBRUSH hbrBk = ::CreateSolidBrush(DisplayAttributeColor(&pda->crBk, ::GetSysColor(COLOR_WINDOW)));
            RECT rcBk = { x, 0, x + size.cx, cy };
            ::FillRect(hDC, &rcBk, hbrBk);
            ::DeleteObject(hbrBk);
        }
        COLORREF crText = DisplayAttributeColor(&pda->crText, ::GetSysColor(COLOR_WINDOWTEXT));
        ::SetTextColor(hDC, crText);
        ::ExtTextOutW(hDC, x, y, 0, NULL, m_pszText + start, end - start, NULL);
        INT cxLine = (end < m_cchText) ? size.cx - (tm.tmHeight - 4) / 4 : size.cx;
        DrawDisplayAttributeLine(hDC, pda, x, cxLine, cy, DisplayAttributeColor(&pda->crLine, crText));

        x += size.cx;
        start = end;
    }

    SIZE caret = { 0, 0 };
    if (m_dwCursor)
        ::GetTextExtentPoint32W(hDC, m_pszText, m_dwCursor, &caret);
    RECT rcCaret = { caret.cx, y, caret.cx + 1, baseline };
    ::FillRect(hDC, &rcCaret, ::GetSysColorBrush(COLOR_WINDOWTEXT));

    ::SelectObject(hDC, hOldFont);
}

LRESULT CALLBACK CCompositionWindow::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_NCCREATE)
    {
        CREATESTRUCTW *pcs = (CREATESTRUCTW *)lParam;
        ::SetWindowLongPtrW(hWnd, 0, (LONG_PTR)pcs->lpCreateParams);
        return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    CCompositionWindow *pThis = (CCompositionWindow *)::GetWindowLongPtrW(hWnd, 0);
    switch (uMsg)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hDC = ::BeginPaint(hWnd, &ps);
            if (pThis)
                pThis->Paint(hDC);
            ::EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return TRUE;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCDESTROY:
            if (pThis)
                pThis->m_hWnd = NULL;
            break;
        default:
            break;
    }
    return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);
}
