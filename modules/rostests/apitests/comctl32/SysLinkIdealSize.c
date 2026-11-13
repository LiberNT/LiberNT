/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests LM_GETIDEALSIZE of the SysLink control
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "wine/test.h"

#include <windows.h>
#include <commctrl.h>

static const WCHAR LinkText[] = L"<a href=\"https://forum.example.org\">Asking questions in the Forum</a>";

static void
TestIdealSize(HWND hWnd)
{
    SIZE Unbounded, Zero, Narrow;
    LRESULT Height;

    Unbounded.cx = Unbounded.cy = 0;
    Height = SendMessageW(hWnd, LM_GETIDEALSIZE, 4096, (LPARAM)&Unbounded);
    ok(Unbounded.cx > 40, "cx = %ld for a wide limit\n", Unbounded.cx);
    ok(Unbounded.cy > 0, "cy = %ld for a wide limit\n", Unbounded.cy);
    ok(Height == Unbounded.cy, "Height = %Id, cy = %ld\n", Height, Unbounded.cy);

    Zero.cx = Zero.cy = -1;
    Height = SendMessageW(hWnd, LM_GETIDEALSIZE, 0, (LPARAM)&Zero);
    ok(Zero.cx == Unbounded.cx, "cx = %ld for no limit, expected %ld\n", Zero.cx, Unbounded.cx);
    ok(Zero.cy == Unbounded.cy, "cy = %ld for no limit, expected %ld\n", Zero.cy, Unbounded.cy);
    ok(Height == Zero.cy, "Height = %Id, cy = %ld\n", Height, Zero.cy);

    Narrow.cx = Narrow.cy = 0;
    SendMessageW(hWnd, LM_GETIDEALSIZE, Unbounded.cx / 2, (LPARAM)&Narrow);
    ok(Narrow.cx <= Unbounded.cx / 2, "cx = %ld above the limit %ld\n", Narrow.cx, Unbounded.cx / 2);
    ok(Narrow.cy > Unbounded.cy, "cy = %ld, expected more than %ld\n", Narrow.cy, Unbounded.cy);
}

START_TEST(SysLinkIdealSize)
{
    HWND hWnd;
    INITCOMMONCONTROLSEX Icc = { sizeof(Icc), ICC_LINK_CLASS };

    InitCommonControlsEx(&Icc);
    hWnd = CreateWindowExW(0, WC_LINK, LinkText, WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd)
        return;

    SendMessageW(hWnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), FALSE);
    TestIdealSize(hWnd);
    DestroyWindow(hWnd);
}
