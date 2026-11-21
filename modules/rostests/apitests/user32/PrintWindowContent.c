/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests what PrintWindow copies, the flags it accepts and the paint messages it sends
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

static COLORREF FillColor;
static LONG Paints, Erases, NcPaints;

static LRESULT CALLBACK
PrintWindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HBRUSH hBrush;
    HDC hdc;

    switch (Msg)
    {
        case WM_PAINT:
            hdc = BeginPaint(hWnd, &ps);
            hBrush = CreateSolidBrush(FillColor);
            FillRect(hdc, &ps.rcPaint, hBrush);
            DeleteObject(hBrush);
            EndPaint(hWnd, &ps);
            Paints++;
            return 0;
        case WM_ERASEBKGND:
            Erases++;
            return 1;
        case WM_NCPAINT:
            NcPaints++;
            break;
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static void
PumpMessages(void)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
}

static BOOL
PrintPixel(HWND hWnd, UINT Flags, INT x, INT y, COLORREF *Pixel, DWORD *Error)
{
    RECT rc = { 0, 0, 300, 300 };
    HDC hdcScreen, hdcMem;
    HBITMAP hbm, hbmOld;
    BOOL Ret;

    hdcScreen = GetDC(NULL);
    hdcMem = CreateCompatibleDC(hdcScreen);
    hbm = CreateCompatibleBitmap(hdcScreen, 300, 300);
    hbmOld = SelectObject(hdcMem, hbm);
    FillRect(hdcMem, &rc, GetStockObject(WHITE_BRUSH));

    Paints = Erases = NcPaints = 0;
    SetLastError(0xdeadbeef);
    Ret = PrintWindow(hWnd, hdcMem, Flags);
    *Error = GetLastError();
    *Pixel = GetPixel(hdcMem, x, y);

    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return Ret;
}

START_TEST(PrintWindowContent)
{
    static const UINT ValidFlags[] = { 0, PW_CLIENTONLY, PW_RENDERFULLCONTENT, PW_CLIENTONLY | PW_RENDERFULLCONTENT };
    static const UINT InvalidFlags[] = { 4, 0x80000000 };
    WNDCLASSW wc = { 0 };
    HWND hWnd, hFramed, hCover;
    COLORREF Pixel;
    DWORD Error;
    ULONG Index;
    BOOL Ret;

    wc.lpfnWndProc = PrintWindowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"PrintWindowContent";
    ok(RegisterClassW(&wc) != 0, "RegisterClassW failed: %lu\n", GetLastError());

    FillColor = RGB(0, 200, 0);
    hWnd = CreateWindowExW(0, wc.lpszClassName, L"Popup", WS_POPUP | WS_VISIBLE,
                           100, 100, 200, 200, NULL, NULL, wc.hInstance, NULL);
    hFramed = CreateWindowExW(0, wc.lpszClassName, L"Framed", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              400, 100, 250, 200, NULL, NULL, wc.hInstance, NULL);
    ok(hWnd != NULL && hFramed != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd || !hFramed)
        return;
    UpdateWindow(hWnd);
    UpdateWindow(hFramed);
    PumpMessages();

    for (Index = 0; Index < RTL_NUMBER_OF(ValidFlags); Index++)
    {
        Ret = PrintPixel(hWnd, ValidFlags[Index], 100, 100, &Pixel, &Error);
        ok(Ret, "PrintWindow with flags 0x%x failed: %lu\n", ValidFlags[Index], Error);
        ok(Pixel == RGB(0, 200, 0), "Flags 0x%x copied pixel 0x%06lx\n", ValidFlags[Index], Pixel);
        ok(Paints == 1, "Flags 0x%x sent %ld WM_PAINT\n", ValidFlags[Index], Paints);
        ok(Erases == 1, "Flags 0x%x sent %ld WM_ERASEBKGND\n", ValidFlags[Index], Erases);
        ok(NcPaints == 1, "Flags 0x%x sent %ld WM_NCPAINT\n", ValidFlags[Index], NcPaints);
    }

    for (Index = 0; Index < RTL_NUMBER_OF(InvalidFlags); Index++)
    {
        Ret = PrintPixel(hWnd, InvalidFlags[Index], 100, 100, &Pixel, &Error);
        ok(!Ret, "PrintWindow with flags 0x%x succeeded\n", InvalidFlags[Index]);
        ok(Error == ERROR_INVALID_PARAMETER, "Flags 0x%x set error %lu\n", InvalidFlags[Index], Error);
        ok(Pixel == RGB(255, 255, 255), "Flags 0x%x wrote pixel 0x%06lx\n", InvalidFlags[Index], Pixel);
        ok(Paints == 0, "Flags 0x%x sent %ld WM_PAINT\n", InvalidFlags[Index], Paints);
    }

    Ret = PrintPixel(hFramed, 0, 5, 5, &Pixel, &Error);
    ok(Ret, "PrintWindow of the framed window failed: %lu\n", Error);
    ok(Pixel != RGB(0, 200, 0) && Pixel != RGB(255, 255, 255), "The frame corner copied pixel 0x%06lx\n", Pixel);
    Ret = PrintPixel(hFramed, PW_CLIENTONLY, 5, 5, &Pixel, &Error);
    ok(Ret, "PrintWindow of the framed client failed: %lu\n", Error);
    ok(Pixel == RGB(0, 200, 0), "The client corner copied pixel 0x%06lx\n", Pixel);

    hCover = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"", WS_POPUP | WS_VISIBLE | SS_BLACKRECT,
                             100, 100, 200, 200, NULL, NULL, NULL, NULL);
    ok(hCover != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    PumpMessages();
    FillColor = RGB(200, 0, 0);
    Ret = PrintPixel(hWnd, 0, 100, 100, &Pixel, &Error);
    ok(Ret, "PrintWindow of the covered window failed: %lu\n", Error);
    ok(Pixel == RGB(200, 0, 0), "The covered window copied pixel 0x%06lx\n", Pixel);
    ok(Paints == 1, "The covered window got %ld WM_PAINT\n", Paints);
    if (hCover)
        DestroyWindow(hCover);
    PumpMessages();

    ShowWindow(hWnd, SW_HIDE);
    PumpMessages();
    Ret = PrintPixel(hWnd, 0, 100, 100, &Pixel, &Error);
    ok(Ret, "PrintWindow of the hidden window failed: %lu\n", Error);
    ok(Pixel == RGB(0, 0, 0), "The hidden window copied pixel 0x%06lx\n", Pixel);

    Ret = PrintPixel(NULL, 0, 1, 1, &Pixel, &Error);
    ok(!Ret, "PrintWindow of NULL succeeded\n");
    ok(Error == ERROR_INVALID_WINDOW_HANDLE, "PrintWindow of NULL set error %lu\n", Error);

    Ret = PrintPixel(GetDesktopWindow(), 0, 1, 1, &Pixel, &Error);
    ok(!Ret, "PrintWindow of the desktop succeeded\n");
    ok(Error == 0xdeadbeef, "PrintWindow of the desktop set error %lu\n", Error);

    DestroyWindow(hFramed);
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
