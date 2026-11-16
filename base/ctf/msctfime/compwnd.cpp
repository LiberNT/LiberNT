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
    , m_pAttr(NULL)
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
    cicMemFree(m_pAttr);
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
        cicMemFree(m_pAttr);
        m_pszText = NULL;
        m_pAttr = NULL;
        m_cchText = 0;
        if (cchText)
        {
            m_pszText = (LPWSTR)cicMemAlloc(cchText * sizeof(WCHAR));
            m_pAttr = (LPBYTE)cicMemAllocClear(cchText);
            if (!m_pszText || !m_pAttr)
            {
                Hide();
                return;
            }
            CopyMemory(m_pszText, (const BYTE *)pCS + pCS->dwCompStrOffset, cchText * sizeof(WCHAR));
            if (pCS->dwCompAttrLen == cchText)
                CopyMemory(m_pAttr, (const BYTE *)pCS + pCS->dwCompAttrOffset, cchText);
            m_cchText = cchText;
        }
    }
    m_dwCursor = min(dwCursor, cchText);

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

void CCompositionWindow::Paint(HDC hDC)
{
    RECT rcClient;
    ::GetClientRect(m_hWnd, &rcClient);
    ::FillRect(hDC, &rcClient, ::GetSysColorBrush(COLOR_WINDOW));
    if (!m_cchText)
        return;

    HGDIOBJ hOldFont = ::SelectObject(hDC, m_hFont ? (HGDIOBJ)m_hFont : ::GetStockObject(DEFAULT_GUI_FONT));
    ::SetBkMode(hDC, TRANSPARENT);
    ::SetTextColor(hDC, ::GetSysColor(COLOR_WINDOWTEXT));

    TEXTMETRICW tm;
    ::GetTextMetricsW(hDC, &tm);
    INT y = 0;
    INT baseline = y + tm.tmHeight;

    DWORD start = 0;
    INT x = 0;
    while (start < m_cchText)
    {
        DWORD end = start + 1;
        while (end < m_cchText && m_pAttr[end] == m_pAttr[start])
            ++end;

        SIZE size;
        ::GetTextExtentPoint32W(hDC, m_pszText + start, end - start, &size);
        BYTE bAttr = m_pAttr[start];
        BOOL bTarget = (bAttr == ATTR_TARGET_CONVERTED || bAttr == ATTR_TARGET_NOTCONVERTED);
        ::ExtTextOutW(hDC, x, y, 0, NULL, m_pszText + start, end - start, NULL);

        /* FIXME: The line style and colors of the text service's display attributes are not used */
        HPEN hPen = ::CreatePen(bTarget ? PS_SOLID : PS_DOT, 1, ::GetSysColor(COLOR_WINDOWTEXT));
        HGDIOBJ hOldPen = ::SelectObject(hDC, hPen);
        for (INT yLine = bTarget ? baseline - 1 : baseline; yLine <= baseline; ++yLine)
        {
            ::MoveToEx(hDC, x + 1, yLine, NULL);
            ::LineTo(hDC, x + size.cx - 1, yLine);
        }
        ::SelectObject(hDC, hOldPen);
        ::DeleteObject(hPen);

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
