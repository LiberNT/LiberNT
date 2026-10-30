/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test update regions outside the client area and on windows without a client area
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static int NcPaints, Erases, Paints;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_NCPAINT)
        NcPaints++;
    else if (uMsg == WM_ERASEBKGND)
        Erases++;
    else if (uMsg == WM_PAINT)
        Paints++;
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static void PumpMessages(void)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
}

static int PeekPaints(HWND hwnd)
{
    MSG msg;
    int Count = 0;

    while (Count < 5 && PeekMessageW(&msg, hwnd, WM_PAINT, WM_PAINT, PM_REMOVE))
        Count++;
    return Count;
}

static void Check(const char *Name, HWND hwnd, BOOL ExpectedDirty, int ExpectedNcPaints, int ExpectedErases)
{
    RECT rect;
    int Peeks;

    ok(GetUpdateRect(hwnd, &rect, FALSE) == ExpectedDirty, "%s: GetUpdateRect returned %d\n", Name, !ExpectedDirty);
    ok(NcPaints == ExpectedNcPaints, "%s: %d WM_NCPAINT, expected %d\n", Name, NcPaints, ExpectedNcPaints);
    ok(Erases == ExpectedErases, "%s: %d WM_ERASEBKGND, expected %d\n", Name, Erases, ExpectedErases);
    Peeks = PeekPaints(hwnd);
    ok(Peeks == (ExpectedDirty ? 5 : 0), "%s: WM_PAINT retrieved %d times\n", Name, Peeks);
    Paints = 0;
    PumpMessages();
    ok(Paints == (ExpectedDirty ? 1 : 0), "%s: %d WM_PAINT dispatched\n", Name, Paints);
    ok(!GetUpdateRect(hwnd, &rect, FALSE), "%s: the window is still invalid after painting\n", Name);
    NcPaints = Erases = Paints = 0;
}

static void TestClientWindow(void)
{
    HWND hwnd;
    HRGN hrgn;
    RECT rect;

    hwnd = CreateWindowExW(0, L"NonClientUpdateClass", NULL, WS_OVERLAPPED | WS_VISIBLE,
                           10, 10, 200, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hwnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwnd)
        return;
    PumpMessages();
    NcPaints = Erases = Paints = 0;

    hrgn = CreateRectRgn(0, -10, 10, -5);
    InvalidateRgn(hwnd, hrgn, FALSE);
    ok(GetUpdateRgn(hwnd, hrgn, FALSE) == NULLREGION, "GetUpdateRgn did not return NULLREGION\n");
    Check("caption region", hwnd, TRUE, 0, 0);

    SetRectRgn(hrgn, -4, -4, -1, -1);
    RedrawWindow(hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_ERASE);
    Check("frame region", hwnd, TRUE, 0, 0);

    RedrawWindow(hwnd, NULL, hrgn, RDW_INVALIDATE | RDW_FRAME | RDW_ERASENOW);
    Check("frame region with RDW_FRAME", hwnd, FALSE, 1, 0);

    SetRectRgn(hrgn, -50, -50, -40, -40);
    InvalidateRgn(hwnd, hrgn, FALSE);
    Check("region outside the window", hwnd, FALSE, 0, 0);

    InvalidateRgn(hwnd, NULL, FALSE);
    ok(GetUpdateRect(hwnd, &rect, FALSE), "the client area is not invalid\n");
    PumpMessages();
    NcPaints = Erases = Paints = 0;

    DeleteObject(hrgn);
    DestroyWindow(hwnd);
    PumpMessages();
}

static void TestNoClientWindow(void)
{
    HWND hwnd;
    HRGN hrgn;
    RECT rect;

    hwnd = CreateWindowExW(0, L"NonClientUpdateClass", NULL, WS_OVERLAPPED,
                           10, 10, 20, 20, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hwnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwnd)
        return;
    GetClientRect(hwnd, &rect);
    ok(rect.bottom == 0, "the client height is %ld\n", rect.bottom);

    NcPaints = Erases = Paints = 0;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    Check("shown", hwnd, TRUE, 1, 1);

    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ERASE | RDW_ERASENOW);
    Check("redrawn with the frame", hwnd, TRUE, 1, 1);

    hrgn = CreateRectRgn(0, 0, 1, 1);
    RedrawWindow(hwnd, NULL, hrgn, RDW_INTERNALPAINT | RDW_INVALIDATE);
    Check("region at the client origin", hwnd, TRUE, 0, 0);

    DeleteObject(hrgn);
    DestroyWindow(hwnd);
    PumpMessages();
}

START_TEST(NonClientUpdate)
{
    WNDCLASSW wc = { 0 };

    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"NonClientUpdateClass";
    RegisterClassW(&wc);

    TestClientWindow();
    TestNoClientWindow();

    UnregisterClassW(L"NonClientUpdateClass", GetModuleHandleW(NULL));
}
