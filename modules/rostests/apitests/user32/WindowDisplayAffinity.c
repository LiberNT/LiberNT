/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Clearing window display affinity, validating handles and what screen reads see of a protected window
 */

#include "precomp.h"

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

typedef HRESULT (WINAPI *DWM_IS_COMPOSITION_ENABLED)(BOOL *);

static LRESULT CALLBACK
AffinityWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HBRUSH hBrush;
    HDC hdc;

    if (Msg == WM_PAINT)
    {
        hdc = BeginPaint(hWnd, &ps);
        hBrush = CreateSolidBrush((COLORREF)GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        FillRect(hdc, &ps.rcPaint, hBrush);
        DeleteObject(hBrush);
        EndPaint(hWnd, &ps);
        return 0;
    }
    if (Msg == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static void
PumpMessages(void)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
    Sleep(100);
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&msg);
}

static COLORREF
ScreenBlt(INT x, INT y, BOOL Stretch)
{
    HDC hdcScreen = GetDC(NULL), hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, 4, 4), hbmOld = SelectObject(hdcMem, hbm);
    COLORREF Color;

    if (Stretch)
        StretchBlt(hdcMem, 0, 0, 2, 2, hdcScreen, x, y, 4, 4, SRCCOPY | CAPTUREBLT);
    else
        BitBlt(hdcMem, 0, 0, 4, 4, hdcScreen, x, y, SRCCOPY);
    Color = GetPixel(hdcMem, 1, 1);
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return Color;
}

enum
{
    READ_ALPHABLEND,
    READ_TRANSPARENTBLT,
    READ_PLGBLT
};

static COLORREF
ScreenRead(INT x, INT y, INT Mode, BOOL *Ret)
{
    static const BLENDFUNCTION Blend = { AC_SRC_OVER, 0, 255, 0 };
    POINT Target[3] = { { 1, 0 }, { 5, 0 }, { 0, 4 } };
    HDC hdcScreen = GetDC(NULL), hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, 8, 8), hbmOld = SelectObject(hdcMem, hbm);
    RECT rc = { 0, 0, 8, 8 };
    COLORREF Color;

    FillRect(hdcMem, &rc, GetStockObject(WHITE_BRUSH));
    if (Mode == READ_ALPHABLEND)
        *Ret = GdiAlphaBlend(hdcMem, 0, 0, 4, 4, hdcScreen, x, y, 4, 4, Blend);
    else if (Mode == READ_TRANSPARENTBLT)
        *Ret = GdiTransparentBlt(hdcMem, 0, 0, 4, 4, hdcScreen, x, y, 4, 4, RGB(1, 2, 3));
    else
        *Ret = PlgBlt(hdcMem, Target, hdcScreen, x, y, 4, 4, NULL, 0, 0);
    Color = GetPixel(hdcMem, Mode == READ_PLGBLT ? 2 : 1, 2);
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return Color;
}

static COLORREF
ScreenGetPixel(INT x, INT y)
{
    HDC hdcScreen = GetDC(NULL);
    COLORREF Color = GetPixel(hdcScreen, x, y);

    ReleaseDC(NULL, hdcScreen);
    return Color;
}

static COLORREF
PrintedPixel(HWND hWnd)
{
    HDC hdcScreen = GetDC(NULL), hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, 100, 100), hbmOld = SelectObject(hdcMem, hbm);
    COLORREF Color = CLR_INVALID;

    if (PrintWindow(hWnd, hdcMem, 0))
        Color = GetPixel(hdcMem, 50, 50);
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return Color;
}

static void
TestValidation(HWND hWnd, HWND hChild)
{
    DWORD Affinity;
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = SetWindowDisplayAffinity(hChild, WDA_MONITOR);
    ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "Child window: %d, error %lu\n", Ret, GetLastError());

    SetLastError(0xdeadbeef);
    Ret = SetWindowDisplayAffinity(GetDesktopWindow(), WDA_MONITOR);
    ok(!Ret && GetLastError() == ERROR_ACCESS_DENIED, "Desktop window: %d, error %lu\n", Ret, GetLastError());

    SetLastError(0xdeadbeef);
    Ret = SetWindowDisplayAffinity(NULL, WDA_MONITOR);
    ok(!Ret && GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "NULL window: %d, error %lu\n", Ret, GetLastError());

    SetLastError(0xdeadbeef);
    Ret = SetWindowDisplayAffinity(hWnd, 2);
    ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "Affinity 2: %d, error %lu\n", Ret, GetLastError());

    SetLastError(0xdeadbeef);
    Ret = GetWindowDisplayAffinity(hWnd, NULL);
    ok(!Ret && GetLastError() == ERROR_NOACCESS, "NULL affinity pointer: %d, error %lu\n", Ret, GetLastError());

    SetLastError(0xdeadbeef);
    Affinity = 0xdeadbeef;
    Ret = GetWindowDisplayAffinity(NULL, &Affinity);
    ok(!Ret && GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "Get on NULL: %d, error %lu\n", Ret, GetLastError());
}

static void
TestWithoutComposition(HWND hWnd)
{
    static const DWORD Values[] = { WDA_NONE, WDA_MONITOR, WDA_EXCLUDEFROMCAPTURE };
    DWORD Affinity;
    ULONG Index;
    BOOL Ret;

    for (Index = 0; Index < RTL_NUMBER_OF(Values); Index++)
    {
        SetLastError(0xdeadbeef);
        Ret = SetWindowDisplayAffinity(hWnd, Values[Index]);
        ok(!Ret && GetLastError() == ERROR_NOT_ENOUGH_MEMORY, "Affinity 0x%lx without composition: %d, error %lu\n",
           Values[Index], Ret, GetLastError());
    }
    Affinity = 0xdeadbeef;
    ok(GetWindowDisplayAffinity(hWnd, &Affinity), "GetWindowDisplayAffinity failed: %lu\n", GetLastError());
    ok(Affinity == WDA_NONE, "Affinity is 0x%lx\n", Affinity);
}

static void
CheckScreenReads(COLORREF Expected, PCSTR What)
{
    static const PCSTR Names[] = { "AlphaBlend", "TransparentBlt", "PlgBlt" };
    COLORREF Color;
    BOOL Ret;
    INT Mode;

    for (Mode = READ_ALPHABLEND; Mode <= READ_PLGBLT; Mode++)
    {
        Ret = FALSE;
        Color = ScreenRead(150, 150, Mode, &Ret);
        ok(Ret, "%s of %s failed\n", Names[Mode], What);
        ok(Color == Expected, "%s of %s reads 0x%06lx\n", Names[Mode], What, Color);
    }
}

static void
TestCapture(HWND hWnd, HWND hOther)
{
    const COLORREF Green = RGB(0, 200, 0), Red = RGB(200, 0, 0);
    DWORD Affinity;

    ok(ScreenBlt(150, 150, FALSE) == Green, "Unprotected window reads 0x%06lx\n", ScreenBlt(150, 150, FALSE));
    CheckScreenReads(Green, "the unprotected window");
    ok(PrintedPixel(hWnd) == Green, "Unprotected window prints 0x%06lx\n", PrintedPixel(hWnd));

    ok(SetWindowDisplayAffinity(hWnd, WDA_MONITOR), "WDA_MONITOR failed: %lu\n", GetLastError());
    Affinity = 0xdeadbeef;
    ok(GetWindowDisplayAffinity(hWnd, &Affinity) && Affinity == WDA_MONITOR, "Affinity is 0x%lx\n", Affinity);
    PumpMessages();
    ok(ScreenBlt(150, 150, FALSE) == RGB(0, 0, 0), "BitBlt of a WDA_MONITOR window reads 0x%06lx\n", ScreenBlt(150, 150, FALSE));
    ok(ScreenBlt(150, 150, TRUE) == RGB(0, 0, 0), "StretchBlt of a WDA_MONITOR window reads 0x%06lx\n", ScreenBlt(150, 150, TRUE));
    ok(ScreenGetPixel(150, 150) == RGB(0, 0, 0), "GetPixel of a WDA_MONITOR window reads 0x%06lx\n", ScreenGetPixel(150, 150));
    CheckScreenReads(RGB(0, 0, 0), "a WDA_MONITOR window");
    ok(PrintedPixel(hWnd) == RGB(0, 0, 0), "A WDA_MONITOR window prints 0x%06lx\n", PrintedPixel(hWnd));
    ok(ScreenBlt(350, 150, FALSE) == Red, "The unprotected neighbour reads 0x%06lx\n", ScreenBlt(350, 150, FALSE));

    ok(SetWindowDisplayAffinity(hWnd, WDA_EXCLUDEFROMCAPTURE), "WDA_EXCLUDEFROMCAPTURE failed: %lu\n", GetLastError());
    Affinity = 0xdeadbeef;
    ok(GetWindowDisplayAffinity(hWnd, &Affinity) && Affinity == WDA_EXCLUDEFROMCAPTURE, "Affinity is 0x%lx\n", Affinity);
    PumpMessages();
    ok(ScreenBlt(150, 150, FALSE) != Green, "A WDA_EXCLUDEFROMCAPTURE window was captured\n");

    ok(SetWindowDisplayAffinity(hWnd, WDA_NONE), "WDA_NONE failed: %lu\n", GetLastError());
    Affinity = 0xdeadbeef;
    ok(GetWindowDisplayAffinity(hWnd, &Affinity) && Affinity == WDA_NONE, "Affinity is 0x%lx\n", Affinity);
    PumpMessages();
    ok(ScreenBlt(150, 150, FALSE) == Green, "The window reads 0x%06lx after WDA_NONE\n", ScreenBlt(150, 150, FALSE));
}

static void
TestClearAndHandles(BOOL Composition)
{
    HWND Window, Child;
    DWORD Affinity = 0xcccccccc;
    BOOL Result;

    Window = CreateWindowExW(0, L"STATIC", L"Affinity test", WS_OVERLAPPEDWINDOW,
                             0, 0, 200, 100, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (!Window) return;

    SetLastError(0xdeadbeef);
    Result = SetWindowDisplayAffinity(Window, WDA_NONE);
    if (Composition)
        ok(Result, "Clearing affinity failed: %lu\n", GetLastError());
    else
        ok(!Result && GetLastError() == ERROR_NOT_ENOUGH_MEMORY, "Clearing affinity without composition: %d, error %lu\n", Result, GetLastError());
    Result = GetWindowDisplayAffinity(Window, &Affinity);
    ok(Result, "Query failed: %lu\n", GetLastError());
    if (Result) ok_long(Affinity, WDA_NONE);

    Child = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 10, 10, Window, NULL, NULL, NULL);
    ok(Child != NULL, "Create child failed\n");
    if (Child)
    {
        Result = SetWindowDisplayAffinity(Child, WDA_NONE);
        ok(!Result, "Child window accepted\n");
        ok_long(GetLastError(), ERROR_INVALID_PARAMETER);
        DestroyWindow(Child);
    }
    DestroyWindow(Window);
    Result = SetWindowDisplayAffinity(Window, WDA_NONE);
    ok(!Result, "Destroyed window accepted\n");
    ok_long(GetLastError(), ERROR_INVALID_WINDOW_HANDLE);
    Result = GetWindowDisplayAffinity(Window, &Affinity);
    ok(!Result, "Destroyed window query succeeded\n");
    ok_long(GetLastError(), ERROR_INVALID_WINDOW_HANDLE);
}

START_TEST(WindowDisplayAffinity)
{
    DWM_IS_COMPOSITION_ENABLED pDwmIsCompositionEnabled = NULL;
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    WNDCLASSW wc = { 0 };
    HWND hWnd, hOther, hChild;
    BOOL Composition = FALSE;

    if (hDwm)
        pDwmIsCompositionEnabled = (DWM_IS_COMPOSITION_ENABLED)GetProcAddress(hDwm, "DwmIsCompositionEnabled");
    if (pDwmIsCompositionEnabled && FAILED(pDwmIsCompositionEnabled(&Composition)))
        Composition = FALSE;

    TestClearAndHandles(Composition);

    wc.lpfnWndProc = AffinityWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"WindowDisplayAffinity";
    RegisterClassW(&wc);
    hWnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Protected", WS_POPUP,
                           100, 100, 200, 200, NULL, NULL, wc.hInstance, NULL);
    hOther = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Other", WS_POPUP,
                             300, 100, 200, 200, NULL, NULL, wc.hInstance, NULL);
    ok(hWnd != NULL && hOther != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd || !hOther)
        return;
    SetWindowLongPtrW(hWnd, GWLP_USERDATA, RGB(0, 200, 0));
    SetWindowLongPtrW(hOther, GWLP_USERDATA, RGB(200, 0, 0));
    ShowWindow(hWnd, SW_SHOWNOACTIVATE);
    ShowWindow(hOther, SW_SHOWNOACTIVATE);
    hChild = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hWnd, NULL, NULL, NULL);
    UpdateWindow(hWnd);
    UpdateWindow(hOther);
    PumpMessages();

    TestValidation(hWnd, hChild);
    if (Composition)
        TestCapture(hWnd, hOther);
    else
        TestWithoutComposition(hWnd);

    DestroyWindow(hOther);
    DestroyWindow(hWnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (hDwm)
        FreeLibrary(hDwm);
}
