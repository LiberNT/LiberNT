/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Checks that themed caption button glyphs follow the DPI and the caption colors
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <uxtheme.h>

typedef DPI_AWARENESS_CONTEXT (WINAPI *PSETTHREADDPIAWARENESSCONTEXT)(DPI_AWARENESS_CONTEXT);
typedef UINT (WINAPI *PGETDPIFORSYSTEM)(void);

#define GLYPH_CONTRAST 64

typedef struct _GLYPH_SCAN
{
    COLORREF Background;
    RECT Box;
    int Count;
    COLORREF Strongest;
} GLYPH_SCAN;

static void Pump(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Msg;

    while (GetTickCount() - Start < Milliseconds)
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Msg);
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
}

static int Win11GlyphSize(UINT Dpi)
{
    if (Dpi >= 192)
        return 20;
    if (Dpi >= 144)
        return 15;
    if (Dpi >= 120)
        return 12;
    return 10;
}

static int Luma(COLORREF Color)
{
    return (GetRValue(Color) * 299 + GetGValue(Color) * 587 + GetBValue(Color) * 114) / 1000;
}

static COLORREF PixelColor(DWORD Pixel)
{
    return RGB((Pixel >> 16) & 0xFF, (Pixel >> 8) & 0xFF, Pixel & 0xFF);
}

static BOOL ScanButton(const RECT *Button, int Reach, GLYPH_SCAN *Scan)
{
    int Width = Button->right - Button->left, Height = Button->bottom - Button->top;
    int Left = max(0, Width / 2 - Reach), Right = min(Width, Width / 2 + Reach);
    int Top = max(0, Height / 2 - Reach), Bottom = min(Height, Height / 2 + Reach);
    BITMAPINFO Info = {{sizeof(BITMAPINFOHEADER)}};
    int Histogram[256] = {0};
    DWORD *Bits;
    HBITMAP Bitmap;
    HDC Screen, Memory;
    HGDIOBJ Old;
    int x, y, i, Median, Seen = 0, Strength = -1;
    BOOL Result;

    if (Width <= 0 || Height <= 0)
        return FALSE;
    Info.bmiHeader.biWidth = Width;
    Info.bmiHeader.biHeight = -Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Screen = GetDC(NULL);
    Memory = CreateCompatibleDC(Screen);
    Bitmap = CreateDIBSection(Screen, &Info, DIB_RGB_COLORS, (void **)&Bits, NULL, 0);
    if (!Memory || !Bitmap)
    {
        if (Bitmap)
            DeleteObject(Bitmap);
        if (Memory)
            DeleteDC(Memory);
        ReleaseDC(NULL, Screen);
        return FALSE;
    }
    Old = SelectObject(Memory, Bitmap);
    Result = BitBlt(Memory, 0, 0, Width, Height, Screen, Button->left, Button->top, SRCCOPY);
    GdiFlush();
    if (Result)
    {
        for (i = 0; i < Width * Height; i++)
            Histogram[Luma(PixelColor(Bits[i]))]++;
        for (Median = 0; Median < 255; Median++)
        {
            Seen += Histogram[Median];
            if (Seen * 2 >= Width * Height)
                break;
        }
        Scan->Background = PixelColor(Bits[0]);
        for (i = 0; i < Width * Height; i++)
        {
            if (Luma(PixelColor(Bits[i])) == Median)
            {
                Scan->Background = PixelColor(Bits[i]);
                break;
            }
        }
        SetRect(&Scan->Box, Width, Height, -1, -1);
        Scan->Count = 0;
        Scan->Strongest = Scan->Background;
        for (y = Top; y < Bottom; y++)
        {
            for (x = Left; x < Right; x++)
            {
                COLORREF Color = PixelColor(Bits[y * Width + x]);
                int Difference = abs(Luma(Color) - Luma(Scan->Background));

                if (Difference < GLYPH_CONTRAST)
                    continue;
                Scan->Count++;
                Scan->Box.left = min(Scan->Box.left, x);
                Scan->Box.top = min(Scan->Box.top, y);
                Scan->Box.right = max(Scan->Box.right, x + 1);
                Scan->Box.bottom = max(Scan->Box.bottom, y + 1);
                if (Difference > Strength)
                {
                    Strength = Difference;
                    Scan->Strongest = Color;
                }
            }
        }
    }
    SelectObject(Memory, Old);
    DeleteObject(Bitmap);
    DeleteDC(Memory);
    ReleaseDC(NULL, Screen);
    return Result;
}

static void CheckGlyph(const char *Name, const RECT *Button, UINT Dpi, BOOL Line)
{
    int Expected = Win11GlyphSize(Dpi);
    int Width, Height, Center;
    GLYPH_SCAN Scan;

    if (!ScanButton(Button, Expected, &Scan))
    {
        ok(0, "%s: cannot read the button pixels\n", Name);
        return;
    }
    ok(Scan.Count > 0, "%s: no glyph pixel on background %06lx\n", Name, Scan.Background);
    if (!Scan.Count)
        return;
    Width = Scan.Box.right - Scan.Box.left;
    Height = Scan.Box.bottom - Scan.Box.top;
    trace("%s at %u DPI: button %ldx%ld, glyph %dx%d at %ld,%ld, background %06lx, glyph color %06lx\n", Name, Dpi,
          Button->right - Button->left, Button->bottom - Button->top, Width, Height, Scan.Box.left, Scan.Box.top,
          Scan.Background, Scan.Strongest);
    ok(abs(Width - Expected) <= Expected / 4, "%s: glyph width %d, expected about %d at %u DPI\n", Name, Width,
       Expected, Dpi);
    if (Line)
        ok(Height <= MulDiv(2, Dpi, 96) + 1, "%s: glyph height %d at %u DPI\n", Name, Height, Dpi);
    else
        ok(abs(Height - Expected) <= Expected / 4, "%s: glyph height %d, expected about %d at %u DPI\n", Name,
           Height, Expected, Dpi);
    Center = Scan.Box.left + Scan.Box.right - (Button->right - Button->left);
    ok(abs(Center) <= 6, "%s: glyph off center horizontally by %d half pixels\n", Name, Center);
    Center = Scan.Box.top + Scan.Box.bottom - (Button->bottom - Button->top);
    ok(abs(Center) <= 6, "%s: glyph off center vertically by %d half pixels\n", Name, Center);
    if (Luma(Scan.Background) >= 128)
        ok(Luma(Scan.Strongest) < Luma(Scan.Background), "%s: glyph %06lx is not darker than the light caption %06lx\n",
           Name, Scan.Strongest, Scan.Background);
    else
        ok(Luma(Scan.Strongest) > Luma(Scan.Background), "%s: glyph %06lx is not lighter than the dark caption %06lx\n",
           Name, Scan.Strongest, Scan.Background);
}

static void CheckHover(const RECT *Max, const RECT *Close, UINT Dpi)
{
    int Reach = Win11GlyphSize(Dpi);
    GLYPH_SCAN Normal, Hot;
    POINT Old;

    GetCursorPos(&Old);
    if (!ScanButton(Max, Reach, &Normal))
        return;
    SetCursorPos((Max->left + Max->right) / 2, (Max->top + Max->bottom) / 2);
    Pump(600);
    if (ScanButton(Max, Reach, &Hot))
    {
        if (Luma(Normal.Background) >= 128)
            ok(Luma(Hot.Background) < Luma(Normal.Background),
               "max button: hot background %06lx is not darker than %06lx on a light caption\n", Hot.Background,
               Normal.Background);
        else
            ok(Luma(Hot.Background) > Luma(Normal.Background),
               "max button: hot background %06lx is not lighter than %06lx on a dark caption\n", Hot.Background,
               Normal.Background);
    }
    SetCursorPos((Close->left + Close->right) / 2, (Close->top + Close->bottom) / 2);
    Pump(600);
    if (ScanButton(Close, Reach, &Hot))
    {
        ok(GetRValue(Hot.Background) > GetGValue(Hot.Background) + 64 &&
           GetRValue(Hot.Background) > GetBValue(Hot.Background) + 64,
           "close button: hot background %06lx is not red\n", Hot.Background);
        ok(Luma(Hot.Strongest) > Luma(Hot.Background), "close button: hot glyph %06lx is not lighter than %06lx\n",
           Hot.Strongest, Hot.Background);
    }
    SetCursorPos(Old.x, Old.y);
    Pump(200);
}

START_TEST(CaptionGlyphs)
{
    HMODULE User32 = GetModuleHandleW(L"user32.dll");
    PSETTHREADDPIAWARENESSCONTEXT pSetThreadDpiAwarenessContext =
        (PSETTHREADDPIAWARENESSCONTEXT)GetProcAddress(User32, "SetThreadDpiAwarenessContext");
    PGETDPIFORSYSTEM pGetDpiForSystem = (PGETDPIFORSYSTEM)GetProcAddress(User32, "GetDpiForSystem");
    TITLEBARINFOEX Info = {sizeof(Info)};
    DPI_AWARENESS_CONTEXT OldContext;
    WNDCLASSW Class = {0};
    HWND Wnd;
    UINT Dpi;

    if (!IsThemeActive())
    {
        skip("No visual style is active\n");
        return;
    }
    if (!pSetThreadDpiAwarenessContext || !pGetDpiForSystem)
    {
        skip("No per-thread DPI awareness\n");
        return;
    }
    OldContext = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    Dpi = pGetDpiForSystem();
    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"CaptionGlyphs";
    Class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&Class);
    Wnd = CreateWindowExW(WS_EX_TOPMOST, Class.lpszClassName, L"Caption glyphs", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          MulDiv(40, Dpi, 96), MulDiv(40, Dpi, 96), MulDiv(360, Dpi, 96), MulDiv(200, Dpi, 96), NULL,
                          NULL, Class.hInstance, NULL);
    ok(Wnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!Wnd)
    {
        pSetThreadDpiAwarenessContext(OldContext);
        return;
    }
    SetForegroundWindow(Wnd);
    SetCursorPos(MulDiv(220, Dpi, 96), MulDiv(180, Dpi, 96));
    Pump(800);
    SendMessageW(Wnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&Info);
    CheckGlyph("min button", &Info.rgrect[2], Dpi, TRUE);
    CheckGlyph("max button", &Info.rgrect[3], Dpi, FALSE);
    CheckGlyph("close button", &Info.rgrect[5], Dpi, FALSE);
    CheckHover(&Info.rgrect[3], &Info.rgrect[5], Dpi);
    DestroyWindow(Wnd);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
    pSetThreadDpiAwarenessContext(OldContext);
}
