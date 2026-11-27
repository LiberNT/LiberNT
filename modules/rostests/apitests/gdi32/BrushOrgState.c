/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that pattern fills use the brush origin the DC reports after RestoreDC and DC reuse
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define BAND_COLOR RGB(23, 58, 115)

static HBRUSH
CreateBandBrush(VOID)
{
    BITMAPINFO Info = { { sizeof(BITMAPINFOHEADER), 8, -8, 1, 32, BI_RGB } };
    HBITMAP Bitmap;
    HBRUSH Brush;
    ULONG *Bits;
    ULONG i;

    Bitmap = CreateDIBSection(NULL, &Info, DIB_RGB_COLORS, (PVOID *)&Bits, NULL, 0);
    ok(Bitmap != NULL, "CreateDIBSection failed\n");
    if (Bitmap == NULL)
        return NULL;
    for (i = 0; i < 64; i++)
        Bits[i] = (i % 8) < 4 ? 0x173a73 : 0xffffff;
    Brush = CreatePatternBrush(Bitmap);
    ok(Brush != NULL, "CreatePatternBrush failed\n");
    DeleteObject(Bitmap);
    return Brush;
}

static COLORREF
FillAndRead(
    _In_ HDC Dc,
    _In_ HBRUSH Brush)
{
    RECT Rect = { 0, 0, 16, 8 };

    FillRect(Dc, &Rect, Brush);
    GdiFlush();
    return GetPixel(Dc, 0, 0);
}

static VOID
CheckOrigin(
    _In_ const char *Tag,
    _In_ HDC Dc,
    _In_ LONG X)
{
    POINT Origin;

    ok(GetBrushOrgEx(Dc, &Origin), "%s: GetBrushOrgEx failed\n", Tag);
    ok(Origin.x == X && Origin.y == 0, "%s: origin (%ld,%ld)\n", Tag, Origin.x, Origin.y);
}

static VOID
TestRestoreDC(
    _In_ HBRUSH Brush)
{
    BITMAPINFO Info = { { sizeof(BITMAPINFOHEADER), 16, -8, 1, 32, BI_RGB } };
    HBITMAP Bitmap, Old;
    PVOID Bits;
    HDC Dc;
    INT Level;

    Dc = CreateCompatibleDC(NULL);
    Bitmap = CreateDIBSection(Dc, &Info, DIB_RGB_COLORS, &Bits, NULL, 0);
    ok(Dc != NULL && Bitmap != NULL, "memory DC setup failed\n");
    if (Dc == NULL || Bitmap == NULL)
        return;
    Old = SelectObject(Dc, Bitmap);

    Level = SaveDC(Dc);
    ok(Level != 0, "SaveDC failed\n");
    SetBrushOrgEx(Dc, 4, 0, NULL);
    ok(FillAndRead(Dc, Brush) == RGB(255, 255, 255), "shifted origin was not used\n");
    ok(RestoreDC(Dc, -1), "RestoreDC failed\n");
    CheckOrigin("restored zero", Dc, 0);
    ok(FillAndRead(Dc, Brush) == BAND_COLOR, "restored zero origin: pixel %06lx\n", FillAndRead(Dc, Brush));

    SetBrushOrgEx(Dc, 4, 0, NULL);
    Level = SaveDC(Dc);
    ok(Level != 0, "SaveDC failed\n");
    SetBrushOrgEx(Dc, 0, 0, NULL);
    ok(FillAndRead(Dc, Brush) == BAND_COLOR, "zero origin was not used\n");
    ok(RestoreDC(Dc, -1), "RestoreDC failed\n");
    CheckOrigin("restored shift", Dc, 4);
    ok(FillAndRead(Dc, Brush) == RGB(255, 255, 255), "restored shifted origin: pixel %06lx\n", FillAndRead(Dc, Brush));

    SelectObject(Dc, Old);
    DeleteObject(Bitmap);
    DeleteDC(Dc);
}

static VOID
TestWindowDcReuse(
    _In_ HBRUSH Brush)
{
    HWND Window;
    HDC Dc;

    Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"Static", NULL, WS_POPUP | WS_VISIBLE,
                             0, 0, 64, 16, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (Window == NULL)
        return;
    UpdateWindow(Window);

    Dc = GetDC(Window);
    SetBrushOrgEx(Dc, 4, 0, NULL);
    ok(FillAndRead(Dc, Brush) == RGB(255, 255, 255), "window DC: shifted origin was not used\n");
    ReleaseDC(Window, Dc);

    Dc = GetDC(Window);
    CheckOrigin("window DC reuse", Dc, 0);
    ok(FillAndRead(Dc, Brush) == BAND_COLOR, "window DC reuse: pixel %06lx\n", FillAndRead(Dc, Brush));
    ReleaseDC(Window, Dc);

    DestroyWindow(Window);
}

START_TEST(BrushOrgState)
{
    HBRUSH Brush = CreateBandBrush();

    if (Brush == NULL)
        return;
    TestRestoreDC(Brush);
    TestWindowDcReuse(Brush);
    DeleteObject(Brush);
}
