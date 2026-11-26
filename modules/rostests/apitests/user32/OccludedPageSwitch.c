/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that child windows switched behind a topmost window are drawn once uncovered
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define PAGE1_COLOR RGB(255, 0, 0)
#define PAGE2_COLOR RGB(0, 255, 0)
#define COVER_COLOR RGB(0, 0, 255)

static void Flush(void)
{
    HMODULE hDwm = GetModuleHandleW(L"dwmapi.dll");
    HRESULT (WINAPI *pDwmFlush)(void) = NULL;
    DWORD Start = GetTickCount();
    MSG Msg;

    if (!hDwm)
        hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm)
        pDwmFlush = (void *)GetProcAddress(hDwm, "DwmFlush");

    do
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
        }
        if (pDwmFlush)
            pDwmFlush();
        Sleep(20);
    } while (GetTickCount() - Start < 400);
}

static COLORREF ScreenPixel(int x, int y)
{
    HDC hdc = GetDC(NULL);
    COLORREF Color = GetPixel(hdc, x, y);
    ReleaseDC(NULL, hdc);
    return Color;
}

static ATOM RegisterSolidClass(PCWSTR Name, COLORREF Color)
{
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(Color);
    wc.lpszClassName = Name;
    return RegisterClassW(&wc);
}

static void RunCase(DWORD ParentStyle, BOOL MoveParent)
{
    HWND hParent, hPage1, hPage2, hCover;
    POINT Visible, Covered;
    COLORREF Color;

    hParent = CreateWindowExW(0, L"OPS_Parent", L"", WS_POPUP | ParentStyle,
                              40, 40, 200, 120, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hParent != NULL, "CreateWindowExW (parent) failed %lu\n", GetLastError());
    if (!hParent)
        return;
    hPage1 = CreateWindowExW(0, L"OPS_Page1", L"", WS_CHILD | WS_VISIBLE,
                             0, 0, 200, 120, hParent, NULL, GetModuleHandleW(NULL), NULL);
    hPage2 = CreateWindowExW(0, L"OPS_Page2", L"", WS_CHILD,
                             0, 0, 200, 120, hParent, NULL, GetModuleHandleW(NULL), NULL);
    hCover = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"OPS_Cover", L"", WS_POPUP,
                             140, 20, 160, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hPage1 && hPage2 && hCover, "CreateWindowExW failed %lu\n", GetLastError());

    ShowWindow(hParent, SW_SHOW);
    SetForegroundWindow(hParent);
    ShowWindow(hCover, SW_SHOWNOACTIVATE);
    Flush();

    Color = ScreenPixel(90, 100);
    ok(Color == PAGE1_COLOR, "style %lx move %d: visible part before the switch is %06lx\n", ParentStyle, MoveParent, Color);
    Color = ScreenPixel(190, 100);
    ok(Color == COVER_COLOR, "style %lx move %d: covered part before the switch is %06lx\n", ParentStyle, MoveParent, Color);

    ShowWindow(hPage1, SW_HIDE);
    ShowWindow(hPage2, SW_SHOW);
    Flush();

    Color = ScreenPixel(90, 100);
    ok(Color == PAGE2_COLOR, "style %lx move %d: visible part after the switch is %06lx\n", ParentStyle, MoveParent, Color);
    Color = ScreenPixel(190, 100);
    ok(Color == COVER_COLOR, "style %lx move %d: covered part after the switch is %06lx\n", ParentStyle, MoveParent, Color);

    if (MoveParent)
    {
        SetWindowPos(hParent, NULL, 40, 260, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        Visible.x = 90; Visible.y = 320;
        Covered.x = 190; Covered.y = 320;
    }
    else
    {
        SetWindowPos(hCover, NULL, 500, 300, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        Visible.x = 90; Visible.y = 100;
        Covered.x = 190; Covered.y = 100;
    }
    Flush();

    Color = ScreenPixel(Visible.x, Visible.y);
    ok(Color == PAGE2_COLOR, "style %lx move %d: part that was visible is %06lx once uncovered\n", ParentStyle, MoveParent, Color);
    Color = ScreenPixel(Covered.x, Covered.y);
    ok(Color == PAGE2_COLOR, "style %lx move %d: part that was covered is %06lx once uncovered\n", ParentStyle, MoveParent, Color);
    ok(IsWindowVisible(hPage2) && !IsWindowVisible(hPage1), "style %lx move %d: wrong page visible\n", ParentStyle, MoveParent);

    DestroyWindow(hCover);
    DestroyWindow(hParent);
    Flush();
}

START_TEST(OccludedPageSwitch)
{
    ok(RegisterSolidClass(L"OPS_Parent", RGB(255, 255, 255)) != 0, "RegisterClassW failed\n");
    ok(RegisterSolidClass(L"OPS_Page1", PAGE1_COLOR) != 0, "RegisterClassW failed\n");
    ok(RegisterSolidClass(L"OPS_Page2", PAGE2_COLOR) != 0, "RegisterClassW failed\n");
    ok(RegisterSolidClass(L"OPS_Cover", COVER_COLOR) != 0, "RegisterClassW failed\n");

    RunCase(0, FALSE);
    RunCase(WS_CLIPCHILDREN, FALSE);
    RunCase(0, TRUE);
    RunCase(WS_CLIPCHILDREN, TRUE);
}
