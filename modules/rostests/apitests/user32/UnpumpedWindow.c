/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that GDI drawing into a window whose thread pumps no messages reaches the screen
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define PROBE_CX 96
#define PROBE_CY 64

static HWND Probe;
static RECT ProbeRect;

static COLORREF WaitScreenPixel(int x, int y, COLORREF Expected)
{
    HDC Screen = GetDC(NULL);
    DWORD Start = GetTickCount();
    COLORREF Pixel;

    for (;;)
    {
        Pixel = GetPixel(Screen, x, y);
        if (Pixel == Expected || GetTickCount() - Start >= 2000)
            break;
        Sleep(10);
    }
    ReleaseDC(NULL, Screen);
    return Pixel;
}

static BOOL FillProbe(int Left, int Right, COLORREF Color)
{
    RECT Rect = { Left, 0, Right, PROBE_CY };
    HBRUSH Brush = CreateSolidBrush(Color);
    HDC Dc = GetDC(Probe);
    int Filled = 0;

    ok(Dc != NULL, "GetDC failed\n");
    if (Dc != NULL && Brush != NULL)
        Filled = FillRect(Dc, &Rect, Brush);
    if (Dc != NULL)
        ReleaseDC(Probe, Dc);
    if (Brush != NULL)
        DeleteObject(Brush);
    return Filled != 0;
}

static DWORD WINAPI DrawFromOtherThread(void *Unused)
{
    static UINT32 Pixels[(PROBE_CX / 2) * PROBE_CY];
    BITMAPINFO Info;
    HDC Dc;
    UINT i;
    int Rows = 0;

    for (i = 0; i < ARRAYSIZE(Pixels); ++i)
        Pixels[i] = 0x000000ff;
    ZeroMemory(&Info, sizeof(Info));
    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = PROBE_CX / 2;
    Info.bmiHeader.biHeight = -PROBE_CY;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Dc = GetDC(Probe);
    ok(Dc != NULL, "GetDC failed\n");
    if (Dc != NULL)
    {
        Rows = SetDIBitsToDevice(Dc, 0, 0, PROBE_CX / 2, PROBE_CY, 0, 0, 0, PROBE_CY, Pixels, &Info, DIB_RGB_COLORS);
        ReleaseDC(Probe, Dc);
    }
    ok(Rows == PROBE_CY, "SetDIBitsToDevice returned %d\n", Rows);
    ok(FillProbe(PROBE_CX / 2, PROBE_CX, RGB(0, 255, 0)), "FillRect failed\n");
    return 0;
}

static void TestUnpumpedWindow(void)
{
    int LeftX, RightX, Y;
    HANDLE Thread;
    COLORREF Pixel;

    Probe = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"",
                            WS_POPUP | WS_VISIBLE, 200, 160, PROBE_CX, PROBE_CY, NULL, NULL, NULL, NULL);
    ok(Probe != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (Probe == NULL)
        return;
    ok(GetWindowRect(Probe, &ProbeRect), "GetWindowRect failed\n");
    LeftX = ProbeRect.left + PROBE_CX / 4;
    RightX = ProbeRect.left + 3 * PROBE_CX / 4;
    Y = ProbeRect.top + PROBE_CY / 2;

    Thread = CreateThread(NULL, 0, DrawFromOtherThread, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed, error %lu\n", GetLastError());
    if (Thread != NULL)
    {
        ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "the drawing thread did not end\n");
        CloseHandle(Thread);
    }
    Pixel = WaitScreenPixel(LeftX, Y, RGB(0, 0, 255));
    ok(Pixel == RGB(0, 0, 255), "SetDIBitsToDevice from another thread: screen pixel %06lx\n", Pixel);
    Pixel = WaitScreenPixel(RightX, Y, RGB(0, 255, 0));
    ok(Pixel == RGB(0, 255, 0), "FillRect from another thread: screen pixel %06lx\n", Pixel);

    ok(FillProbe(0, PROBE_CX / 2, RGB(255, 0, 0)), "FillRect failed\n");
    Pixel = WaitScreenPixel(LeftX, Y, RGB(255, 0, 0));
    ok(Pixel == RGB(255, 0, 0), "FillRect from the owner thread: screen pixel %06lx\n", Pixel);
    Pixel = WaitScreenPixel(RightX, Y, RGB(0, 255, 0));
    ok(Pixel == RGB(0, 255, 0), "content drawn earlier: screen pixel %06lx\n", Pixel);

    DestroyWindow(Probe);
    Probe = NULL;
}

START_TEST(UnpumpedWindow)
{
    BOOL composition = FALSE;
    HMODULE dwmapi;
    HRESULT (WINAPI *pDwmIsCompositionEnabled)(BOOL *);

    dwmapi = LoadLibraryW(L"dwmapi.dll");
    pDwmIsCompositionEnabled = dwmapi ? (void *)GetProcAddress(dwmapi, "DwmIsCompositionEnabled") : NULL;
    if (pDwmIsCompositionEnabled != NULL && FAILED(pDwmIsCompositionEnabled(&composition)))
        composition = FALSE;
    trace("desktop composition %s\n", composition ? "on" : "off");
    TestUnpumpedWindow();

    if (dwmapi)
        FreeLibrary(dwmapi);
}
