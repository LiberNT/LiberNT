/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The frame of a window without a caption must cover all four edges
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>

#define PARENT_KEY RGB(0xFF, 0x00, 0xFF)

static void
flush_messages(DWORD Timeout)
{
    DWORD Deadline = GetTickCount() + Timeout;
    MSG Message;

    do
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
        if ((LONG)(Deadline - GetTickCount()) <= 0)
            break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 25, QS_ALLINPUT);
    } while (TRUE);
}

static DWORD *
capture_window(HWND Window, int Width, int Height)
{
    BITMAPINFO Info;
    HDC WindowDc, MemDc;
    HBITMAP Bitmap, Old;
    DWORD *Bits = NULL, *Pixels;
    BOOL Result = FALSE;

    Pixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Width * Height * sizeof(DWORD));
    if (!Pixels)
        return NULL;

    ZeroMemory(&Info, sizeof(Info));
    Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info.bmiHeader.biWidth = Width;
    Info.bmiHeader.biHeight = -Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;

    WindowDc = GetWindowDC(Window);
    MemDc = CreateCompatibleDC(WindowDc);
    Bitmap = CreateDIBSection(WindowDc, &Info, DIB_RGB_COLORS, (void **)&Bits, NULL, 0);
    if (WindowDc && MemDc && Bitmap)
    {
        Old = SelectObject(MemDc, Bitmap);
        Result = BitBlt(MemDc, 0, 0, Width, Height, WindowDc, 0, 0, SRCCOPY);
        GdiFlush();
        CopyMemory(Pixels, Bits, Width * Height * sizeof(DWORD));
        SelectObject(MemDc, Old);
    }
    if (Bitmap)
        DeleteObject(Bitmap);
    if (MemDc)
        DeleteDC(MemDc);
    if (WindowDc)
        ReleaseDC(Window, WindowDc);
    if (!Result)
    {
        HeapFree(GetProcessHeap(), 0, Pixels);
        return NULL;
    }
    return Pixels;
}

static DWORD
to_dib(COLORREF Color)
{
    return (GetRValue(Color) << 16) | (GetGValue(Color) << 8) | GetBValue(Color);
}

static DWORD
pixel_at(const DWORD *Pixels, int Width, int x, int y)
{
    return Pixels[y * Width + x] & 0xFFFFFF;
}

static void
test_frame(HWND Child, const char *Name, BOOL CheckBorderColor)
{
    RECT WindowRect, ClientRect;
    POINT Origin = { 0, 0 };
    DWORD *Pixels, Key = to_dib(PARENT_KEY), Frame = to_dib(GetSysColor(COLOR_WINDOWFRAME)), Inner;
    int Width, Height, Left, Top, Right, Bottom, x, y, d, Unpainted = 0, Asymmetric = 0, WrongColor = 0;

    GetWindowRect(Child, &WindowRect);
    GetClientRect(Child, &ClientRect);
    ClientToScreen(Child, &Origin);
    Width = WindowRect.right - WindowRect.left;
    Height = WindowRect.bottom - WindowRect.top;
    Left = Origin.x - WindowRect.left;
    Top = Origin.y - WindowRect.top;
    Right = Width - Left - ClientRect.right;
    Bottom = Height - Top - ClientRect.bottom;
    ok(Left > 0 && Left == Right && Top > 0 && Top == Bottom,
       "%s: frame widths left %d right %d top %d bottom %d\n", Name, Left, Right, Top, Bottom);
    if (Left <= 0 || Top <= 0)
        return;

    Pixels = capture_window(Child, Width, Height);
    ok(Pixels != NULL, "%s: capture failed\n", Name);
    if (!Pixels)
        return;

    for (y = 0; y < Height; y++)
    {
        for (x = 0; x < Width; x++)
        {
            if (pixel_at(Pixels, Width, x, y) == Key)
                Unpainted++;
        }
    }
    ok(Unpainted == 0, "%s: %d pixels of the window were left unpainted\n", Name, Unpainted);

    for (d = (Left > 2) ? 2 : 0; d < Left; d++)
    {
        for (y = Top; y < Height - Bottom; y++)
        {
            if (pixel_at(Pixels, Width, d, y) != pixel_at(Pixels, Width, Width - 1 - d, y))
                Asymmetric++;
        }
    }
    for (d = (Top > 2) ? 2 : 0; d < Top; d++)
    {
        for (x = Left; x < Width - Right; x++)
        {
            if (pixel_at(Pixels, Width, x, d) != pixel_at(Pixels, Width, x, Height - 1 - d))
                Asymmetric++;
        }
    }
    ok(Asymmetric == 0, "%s: %d frame pixels differ from the opposite edge\n", Name, Asymmetric);

    Inner = pixel_at(Pixels, Width, Width / 2, Height / 2);
    for (y = Top; y < Height - Bottom; y++)
    {
        if (pixel_at(Pixels, Width, Left, y) != Inner || pixel_at(Pixels, Width, Width - 1 - Right, y) != Inner)
            WrongColor++;
    }
    for (x = Left; x < Width - Right; x++)
    {
        if (pixel_at(Pixels, Width, x, Top) != Inner || pixel_at(Pixels, Width, x, Height - 1 - Bottom) != Inner)
            WrongColor++;
    }
    ok(WrongColor == 0, "%s: %d client edge pixels differ from the client center\n", Name, WrongColor);

    if (CheckBorderColor)
    {
        WrongColor = 0;
        for (y = 0; y < Height; y++)
        {
            if (pixel_at(Pixels, Width, 0, y) != Frame || pixel_at(Pixels, Width, Width - 1, y) != Frame)
                WrongColor++;
        }
        for (x = 0; x < Width; x++)
        {
            if (pixel_at(Pixels, Width, x, 0) != Frame || pixel_at(Pixels, Width, x, Height - 1) != Frame)
                WrongColor++;
        }
        ok(WrongColor == 0, "%s: %d border pixels are not COLOR_WINDOWFRAME\n", Name, WrongColor);
    }

    HeapFree(GetProcessHeap(), 0, Pixels);
}

START_TEST(NonClientBorder)
{
    WNDCLASSW Class;
    HBRUSH Brush = CreateSolidBrush(PARENT_KEY);
    HINSTANCE Instance = GetModuleHandleW(NULL);
    HWND Parent, Border, Thick;

    ZeroMemory(&Class, sizeof(Class));
    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = Instance;
    Class.hbrBackground = Brush;
    Class.lpszClassName = L"NonClientBorderParent";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed, error %lu\n", GetLastError());

    Parent = CreateWindowExW(WS_EX_TOPMOST, Class.lpszClassName, L"", WS_POPUP, 60, 60, 320, 160, NULL, NULL, Instance, NULL);
    ok(Parent != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Parent)
        return;
    Border = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | SS_WHITERECT, 20, 20, 90, 50, Parent, NULL,
                             Instance, NULL);
    Thick = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_THICKFRAME | SS_WHITERECT, 140, 20, 120, 90, Parent,
                            NULL, Instance, NULL);
    ok(Border != NULL && Thick != NULL, "child creation failed, error %lu\n", GetLastError());
    ShowWindow(Parent, SW_SHOWNOACTIVATE);
    flush_messages(300);

    RedrawWindow(Parent, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    flush_messages(300);
    test_frame(Border, "WS_BORDER", TRUE);
    test_frame(Thick, "WS_THICKFRAME", FALSE);

    SetWindowPos(Border, NULL, 0, 0, 90, 70, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(Thick, NULL, 0, 0, 120, 110, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RedrawWindow(Parent, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    flush_messages(300);
    test_frame(Border, "WS_BORDER grown", TRUE);
    test_frame(Thick, "WS_THICKFRAME grown", FALSE);

    DestroyWindow(Parent);
    UnregisterClassW(Class.lpszClassName, Instance);
    DeleteObject(Brush);
}
