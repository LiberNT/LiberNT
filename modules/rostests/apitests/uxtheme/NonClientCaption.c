/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Themed caption text must keep its ascenders and descenders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <reactos/dwmframe.h>

#define CAPTURE_WIDTH 200
#define TEXT_LEFT 32
#define BUTTON_WIDTH 46
#define BUTTON_HEIGHT 32
#define BUTTON_KEY 0x00202020u

typedef BOOL (WINAPI *PFN_THEME_DWM_DRAW_CAPTION_BUTTON)(HDC, const RECT *, UINT, ULONG, BOOL, BOOL);

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

static BOOL
capture_caption(HWND Window, DWORD *Pixels, int Height)
{
    BITMAPINFO Info;
    HDC WindowDc, MemDc;
    HBITMAP Bitmap, Old;
    DWORD *Bits = NULL;
    BOOL Result = FALSE;

    ZeroMemory(&Info, sizeof(Info));
    Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info.bmiHeader.biWidth = CAPTURE_WIDTH;
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
        Result = BitBlt(MemDc, 0, 0, CAPTURE_WIDTH, Height, WindowDc, 0, 0, SRCCOPY);
        GdiFlush();
        CopyMemory(Pixels, Bits, CAPTURE_WIDTH * Height * sizeof(DWORD));
        SelectObject(MemDc, Old);
    }
    if (Bitmap)
        DeleteObject(Bitmap);
    if (MemDc)
        DeleteDC(MemDc);
    if (WindowDc)
        ReleaseDC(Window, WindowDc);
    return Result;
}

static void
text_rows(const DWORD *Background, const DWORD *Image, int Height, int *Top, int *Bottom)
{
    int x, y;

    *Top = -1;
    *Bottom = -1;
    for (y = 0; y < Height; y++)
    {
        for (x = TEXT_LEFT; x < CAPTURE_WIDTH; x++)
        {
            if ((Background[y * CAPTURE_WIDTH + x] ^ Image[y * CAPTURE_WIDTH + x]) & 0xffffff)
            {
                if (*Top < 0)
                    *Top = y;
                *Bottom = y;
                break;
            }
        }
    }
}

static BOOL
render_title(HWND Window, const WCHAR *Title, DWORD *Pixels, int Height)
{
    SetWindowTextW(Window, Title);
    RedrawWindow(Window, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    flush_messages(150);
    return capture_caption(Window, Pixels, Height);
}

START_TEST(NonClientCaption)
{
    HWND Window;
    RECT WindowRect;
    POINT ClientOrigin = { 0, 0 };
    DWORD *Background, *Round, *Descender, *Ascender;
    int Height, RoundTop, RoundBottom, DescTop, DescBottom, AscTop, AscBottom;

    if (!IsThemeActive())
    {
        skip("Visual styles are not active\n");
        return;
    }

    Window = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             60, 60, 360, 160, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Window)
        return;
    flush_messages(300);

    GetWindowRect(Window, &WindowRect);
    ClientToScreen(Window, &ClientOrigin);
    Height = ClientOrigin.y - WindowRect.top;
    ok(Height > 0 && Height < 100, "unexpected caption band height %d\n", Height);
    if (Height <= 0 || Height >= 100)
    {
        DestroyWindow(Window);
        return;
    }

    Background = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 4 * CAPTURE_WIDTH * Height * sizeof(DWORD));
    Round = Background + CAPTURE_WIDTH * Height;
    Descender = Round + CAPTURE_WIDTH * Height;
    Ascender = Descender + CAPTURE_WIDTH * Height;

    ok(render_title(Window, L"", Background, Height), "cannot capture the empty caption\n");
    ok(render_title(Window, L"oooo", Round, Height), "cannot capture the round caption\n");
    ok(render_title(Window, L"gggg", Descender, Height), "cannot capture the descender caption\n");
    ok(render_title(Window, L"llll", Ascender, Height), "cannot capture the ascender caption\n");

    text_rows(Background, Round, Height, &RoundTop, &RoundBottom);
    text_rows(Background, Descender, Height, &DescTop, &DescBottom);
    text_rows(Background, Ascender, Height, &AscTop, &AscBottom);
    trace("band %d: oooo rows %d-%d, gggg rows %d-%d, llll rows %d-%d\n",
          Height, RoundTop, RoundBottom, DescTop, DescBottom, AscTop, AscBottom);

    ok(RoundTop >= 0, "caption text was not rendered\n");
    if (RoundTop >= 0)
    {
        ok(DescBottom >= RoundBottom + 2, "descenders are clipped: g ends at row %d, o at row %d\n", DescBottom, RoundBottom);
        ok(AscTop <= RoundTop - 2, "ascenders are clipped: l starts at row %d, o at row %d\n", AscTop, RoundTop);
        ok(DescBottom < Height - 1, "descenders touch the client edge: g ends at row %d of %d\n", DescBottom, Height);
        ok(AscTop > 0, "ascenders touch the frame edge\n");
    }

    HeapFree(GetProcessHeap(), 0, Background);
    DestroyWindow(Window);
}

static COLORREF
paint_and_activate(HWND Window, LPARAM Parameter, COLORREF *Before)
{
    HDC Dc = GetWindowDC(Window);
    COLORREF After;

    PatBlt(Dc, 60, 10, 20, 8, BLACKNESS);
    GdiFlush();
    *Before = GetPixel(Dc, 70, 14);
    DefWindowProcW(Window, WM_NCACTIVATE, FALSE, Parameter);
    GdiFlush();
    After = GetPixel(Dc, 70, 14);
    ReleaseDC(Window, Dc);
    return After;
}

START_TEST(NonClientActivate)
{
    HWND Window;
    COLORREF Before, After, ControlBefore, ControlAfter;

    if (!IsThemeActive())
    {
        skip("Visual styles are not active\n");
        return;
    }
    Window = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"Activation test",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             60, 60, 360, 160, NULL, NULL,
                             GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Window)
        return;
    UpdateWindow(Window);
    ControlAfter = paint_and_activate(Window, 0, &ControlBefore);
    After = paint_and_activate(Window, -1, &Before);
    trace("control %08lx -> %08lx, suppressed %08lx -> %08lx\n",
          ControlBefore, ControlAfter, Before, After);
    ok(Before != CLR_INVALID, "unable to read the non-client area\n");
    if (ControlBefore == CLR_INVALID || ControlAfter == CLR_INVALID || ControlBefore == ControlAfter)
        skip("The control activation did not visibly repaint the sampled area\n");
    else
        ok(After == Before, "WM_NCACTIVATE -1 repainted: %08lx -> %08lx\n", Before, After);
    DestroyWindow(Window);
}

static LRESULT CALLBACK
custom_caption_inset_proc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (Message == WM_NCCALCSIZE)
    {
        RECT *Client = WParam ? &((NCCALCSIZE_PARAMS *)LParam)->rgrc[0] : (RECT *)LParam;
        Client->left += 7;
        Client->top += 7;
        Client->right -= 7;
        Client->bottom -= 7;
        return 0;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

START_TEST(CustomCaptionInset)
{
    WNDCLASSW Class = {0};
    HWND Window;
    HDC Screen;
    HBRUSH Blue;
    RECT Rect;
    COLORREF Pixel, Example = CLR_INVALID;
    UINT RedPixels = 0, ValidPixels = 0;
    LRESULT Hit, TopHit, ClientHit;
    LONG x, y;

    if (!IsThemeActive())
    {
        skip("Visual styles are not active\n");
        return;
    }
    Blue = CreateSolidBrush(RGB(0, 0, 255));
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpfnWndProc = custom_caption_inset_proc;
    Class.hbrBackground = Blue;
    Class.lpszClassName = L"LiberNTCustomCaptionInset";
    RegisterClassW(&Class);
    Window = CreateWindowExW(WS_EX_TOPMOST, Class.lpszClassName, L"Custom title",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             100, 100, 400, 240, NULL, NULL, Class.hInstance, NULL);
    ok(Window != NULL, "window creation failed %lu\n", GetLastError());
    if (Window)
    {
        SetForegroundWindow(Window);
        UpdateWindow(Window);
        SendMessageW(Window, WM_NCPAINT, 1, 0);
        flush_messages(1000);
        GetWindowRect(Window, &Rect);
        Screen = GetDC(NULL);
        for (y = Rect.top + 1; y < Rect.top + 7; ++y)
        {
            for (x = Rect.right - 48; x < Rect.right - 8; ++x)
            {
                Pixel = GetPixel(Screen, x, y);
                if (Pixel != CLR_INVALID)
                    ++ValidPixels;
                if (Pixel != CLR_INVALID && GetRValue(Pixel) > GetGValue(Pixel) + 40 &&
                    GetRValue(Pixel) > GetBValue(Pixel) + 40)
                {
                    ++RedPixels;
                    Example = Pixel;
                }
            }
        }
        ReleaseDC(NULL, Screen);
        Hit = SendMessageW(Window, WM_NCHITTEST, 0, MAKELPARAM(Rect.right - 25, Rect.top + 4));
        TopHit = SendMessageW(Window, WM_NCHITTEST, 0, MAKELPARAM(Rect.left + 200, Rect.top + 4));
        ClientHit = SendMessageW(Window, WM_NCHITTEST, 0, MAKELPARAM(Rect.left + 200, Rect.top + 20));
        trace("native close button red pixels: %u example %08lx hits %ld,%ld,%ld\n",
              RedPixels, Example, (LONG)Hit, (LONG)TopHit, (LONG)ClientHit);
        ok(Hit == HTTOP || Hit == HTTOPRIGHT, "custom titlebar close-button area hit test: %ld\n", (LONG)Hit);
        ok(TopHit == HTTOP, "custom titlebar top hit test: %ld\n", (LONG)TopHit);
        ok(ClientHit == HTCLIENT, "custom titlebar client hit test: %ld\n", (LONG)ClientHit);
        ok(ValidPixels != 0, "unable to sample the custom titlebar inset\n");
        if (ValidPixels != 0)
            ok(RedPixels == 0, "native close button painted into custom titlebar inset (%u pixels)\n", RedPixels);
        DestroyWindow(Window);
    }
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
    DeleteObject(Blue);
}

static BOOL
render_caption_button(PFN_THEME_DWM_DRAW_CAPTION_BUTTON Draw, HTHEME Theme, UINT Button, ULONG State,
                      DWORD *Pixels)
{
    static const int Parts[DWM_CAPTION_BUTTONS] = { WP_CLOSEBUTTON, WP_MAXBUTTON, WP_MINBUTTON };
    RECT Rect = { 0, 0, BUTTON_WIDTH, BUTTON_HEIGHT };
    BITMAPINFO Info;
    HBITMAP Bitmap, Old;
    DWORD *Bits = NULL;
    BOOL Drawn;
    HDC Dc;
    int i;

    ZeroMemory(&Info, sizeof(Info));
    Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info.bmiHeader.biWidth = BUTTON_WIDTH;
    Info.bmiHeader.biHeight = -BUTTON_HEIGHT;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    Dc = CreateCompatibleDC(NULL);
    Bitmap = Dc ? CreateDIBSection(Dc, &Info, DIB_RGB_COLORS, (void **)&Bits, NULL, 0) : NULL;
    if (!Bitmap || !Bits)
    {
        if (Bitmap)
            DeleteObject(Bitmap);
        if (Dc)
            DeleteDC(Dc);
        return FALSE;
    }
    for (i = 0; i < BUTTON_WIDTH * BUTTON_HEIGHT; i++)
        Bits[i] = BUTTON_KEY;
    Old = SelectObject(Dc, Bitmap);
    if (Draw)
        Drawn = Draw(Dc, &Rect, Button, State, TRUE, TRUE);
    else
        Drawn = SUCCEEDED(DrawThemeBackground(Theme, Dc, Parts[Button],
                                              (State & DWM_CAPTION_STATE_HOT(Button)) ? CBS_HOT : CBS_NORMAL,
                                              &Rect, NULL));
    GdiFlush();
    for (i = 0; i < BUTTON_WIDTH * BUTTON_HEIGHT; i++)
        Pixels[i] = Bits[i] & 0x00FFFFFFu;
    SelectObject(Dc, Old);
    DeleteObject(Bitmap);
    DeleteDC(Dc);
    return Drawn;
}

START_TEST(DwmCaptionButton)
{
    static const char *Names[DWM_CAPTION_BUTTONS] = { "close", "maximize", "minimize" };
    PFN_THEME_DWM_DRAW_CAPTION_BUTTON Draw;
    DWORD Dwm[BUTTON_WIDTH * BUTTON_HEIGHT], Theme[BUTTON_WIDTH * BUTTON_HEIGHT];
    COLORREF Fill;
    HTHEME Window;
    BOOL DarkCaption;
    UINT Button, Hot;

    Draw = (PFN_THEME_DWM_DRAW_CAPTION_BUTTON)GetProcAddress(GetModuleHandleW(L"uxtheme.dll"),
                                                            "ThemeDwmDrawCaptionButton");
    if (!Draw)
    {
        skip("ThemeDwmDrawCaptionButton is not exported\n");
        return;
    }
    if (!IsThemeActive())
    {
        skip("Visual styles are not active\n");
        return;
    }
    Window = OpenThemeDataEx(NULL, L"WINDOW", OTD_NONCLIENT);
    ok(Window != NULL, "OpenThemeDataEx(WINDOW) failed\n");
    if (!Window)
        return;
    DarkCaption = SUCCEEDED(GetThemeColor(Window, WP_CAPTION, FS_ACTIVE, TMT_FILLCOLORHINT, &Fill)) &&
                  (GetRValue(Fill) * 299 + GetGValue(Fill) * 587 + GetBValue(Fill) * 114) / 1000 < 128;
    trace("caption fill hint %06lx, dark caption %d\n", Fill, DarkCaption);
    for (Button = 0; Button < DWM_CAPTION_BUTTONS; Button++)
    {
        for (Hot = 0; Hot < 2; Hot++)
        {
            ULONG State = Hot ? DWM_CAPTION_STATE_HOT(Button) : 0;
            BOOL Same;

            ok(render_caption_button(Draw, NULL, Button, State, Dwm),
               "the DWM %s button (hot %u) was not drawn\n", Names[Button], Hot);
            ok(render_caption_button(NULL, Window, Button, State, Theme),
               "the theme %s button (hot %u) was not drawn\n", Names[Button], Hot);
            Same = !memcmp(Dwm, Theme, sizeof(Dwm));
            if (DarkCaption)
                ok(Same, "a dark window's DWM %s button (hot %u) differs from the dark theme's\n",
                   Names[Button], Hot);
            else
                ok(!Same, "a dark window's DWM %s button (hot %u) kept the light theme's\n",
                   Names[Button], Hot);
        }
    }
    CloseThemeData(Window);
}
