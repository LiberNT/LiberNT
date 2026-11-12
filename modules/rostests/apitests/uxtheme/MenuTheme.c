/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the MENU visual style class
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <windows.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>

#define TEXT_WIDTH 64
#define TEXT_HEIGHT 24
#define TEXT_BACKGROUND 0x00202020

static void
test_text_color(HTHEME Theme, int Part, int State, COLORREF Expected)
{
    COLORREF Color = 0xdeadbe;
    HRESULT hr;

    hr = GetThemeColor(Theme, Part, State, TMT_TEXTCOLOR, &Color);
    ok(hr == S_OK, "part %d state %d: GetThemeColor returned 0x%08lx\n", Part, State, hr);
    if (GetSysColor(COLOR_MENUTEXT) == RGB(0, 0, 0))
        ok(Color == Expected, "part %d state %d: text color 0x%06lx, expected 0x%06lx\n", Part, State, Color, Expected);
}

static void
test_draw_text(HTHEME Theme, int Part, int State)
{
    BITMAPINFO Info = {{sizeof(BITMAPINFOHEADER), TEXT_WIDTH, -TEXT_HEIGHT, 1, 32, BI_RGB}};
    RECT Rect = {0, 0, TEXT_WIDTH, TEXT_HEIGHT};
    DTTOPTS Options = {sizeof(Options)};
    HBITMAP Bitmap, OldBitmap;
    DWORD *Bits;
    HDC Dc;
    HRESULT hr;
    int i, Lit = 0, Other = 0;

    Dc = CreateCompatibleDC(NULL);
    ok(Dc != NULL, "CreateCompatibleDC failed\n");
    Bitmap = CreateDIBSection(Dc, &Info, DIB_RGB_COLORS, (void **)&Bits, NULL, 0);
    ok(Bitmap != NULL, "CreateDIBSection failed\n");
    if (!Dc || !Bitmap)
        goto Cleanup;
    OldBitmap = SelectObject(Dc, Bitmap);
    for (i = 0; i < TEXT_WIDTH * TEXT_HEIGHT; i++)
        Bits[i] = TEXT_BACKGROUND;

    Options.dwFlags = DTT_TEXTCOLOR;
    Options.crText = RGB(255, 255, 255);
    hr = DrawThemeTextEx(Theme, Dc, Part, State, L"File", -1, DT_SINGLELINE | DT_CENTER | DT_VCENTER, &Rect, &Options);
    ok(hr == S_OK, "part %d state %d: DrawThemeTextEx returned 0x%08lx\n", Part, State, hr);
    GdiFlush();
    for (i = 0; i < TEXT_WIDTH * TEXT_HEIGHT; i++)
    {
        if (Bits[i] == 0x00ffffff)
            Lit++;
        else if (Bits[i] != TEXT_BACKGROUND)
            Other++;
    }
    ok(Lit > 0, "part %d state %d: no pixel in the text color\n", Part, State);
    ok(Lit + Other < TEXT_WIDTH * TEXT_HEIGHT / 2, "part %d state %d: %d of %d pixels changed\n",
       Part, State, Lit + Other, TEXT_WIDTH * TEXT_HEIGHT);
    SelectObject(Dc, OldBitmap);

Cleanup:
    if (Bitmap)
        DeleteObject(Bitmap);
    if (Dc)
        DeleteDC(Dc);
}

START_TEST(MenuTheme)
{
    WNDCLASSW Class = {0};
    HTHEME Theme;
    HWND Window;

    if (!IsThemeActive())
    {
        skip("No visual style is active\n");
        return;
    }

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"MenuThemeTest";
    RegisterClassW(&Class);
    Window = CreateWindowExW(0, Class.lpszClassName, L"MenuTheme", WS_OVERLAPPEDWINDOW,
                             0, 0, 200, 100, NULL, NULL, Class.hInstance, NULL);
    ok(Window != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!Window)
        return;

    SetLastError(0xdeadbeef);
    Theme = OpenThemeData(Window, VSCLASS_MENU);
    ok(Theme != NULL, "OpenThemeData(MENU) failed: 0x%08lx\n", GetLastError());
    if (Theme)
    {
        ok(IsThemePartDefined(Theme, MENU_BARITEM, 0), "MENU_BARITEM is not defined\n");
        ok(IsThemePartDefined(Theme, MENU_POPUPITEM, 0), "MENU_POPUPITEM is not defined\n");

        test_text_color(Theme, MENU_BARITEM, MBI_NORMAL, RGB(0, 0, 0));
        test_text_color(Theme, MENU_BARITEM, MBI_HOT, RGB(0, 0, 0));
        test_text_color(Theme, MENU_BARITEM, MBI_PUSHED, RGB(0, 0, 0));
        test_text_color(Theme, MENU_BARITEM, MBI_DISABLED, RGB(0x6d, 0x6d, 0x6d));
        test_text_color(Theme, MENU_BARITEM, MBI_DISABLEDHOT, RGB(0x6d, 0x6d, 0x6d));
        test_text_color(Theme, MENU_BARITEM, MBI_DISABLEDPUSHED, RGB(0x6d, 0x6d, 0x6d));
        test_text_color(Theme, MENU_POPUPITEM, MPI_NORMAL, RGB(0, 0, 0));
        test_text_color(Theme, MENU_POPUPITEM, MPI_HOT, RGB(0, 0, 0));
        test_text_color(Theme, MENU_POPUPITEM, MPI_DISABLED, RGB(0x9f, 0x9f, 0x9f));
        test_text_color(Theme, MENU_POPUPITEM, MPI_DISABLEDHOT, RGB(0x9f, 0x9f, 0x9f));

        test_draw_text(Theme, MENU_BARITEM, MBI_NORMAL);
        test_draw_text(Theme, MENU_POPUPITEM, MPI_NORMAL);

        CloseThemeData(Theme);
    }

    DestroyWindow(Window);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
