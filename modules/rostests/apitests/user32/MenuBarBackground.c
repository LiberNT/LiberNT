/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the menu bar is painted with the menu background brush
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"

#define ID_STRING 1
#define ID_OWNERDRAW 2
#define BACK_COLOR RGB(255, 0, 0)
#define ITEM_COLOR RGB(0, 0, 255)

static UINT DrawCount;

static LRESULT CALLBACK
WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg)
    {
        case WM_MEASUREITEM:
        {
            MEASUREITEMSTRUCT *pmis = (MEASUREITEMSTRUCT *)lParam;

            if (pmis->CtlType == ODT_MENU && pmis->itemID == ID_OWNERDRAW)
            {
                pmis->itemWidth = 40;
                pmis->itemHeight = 16;
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT *pdis = (DRAWITEMSTRUCT *)lParam;

            if (pdis->CtlType == ODT_MENU && pdis->itemID == ID_OWNERDRAW)
            {
                HBRUSH Brush = CreateSolidBrush(ITEM_COLOR);

                FillRect(pdis->hDC, &pdis->rcItem, Brush);
                DeleteObject(Brush);
                DrawCount++;
                return TRUE;
            }
            break;
        }
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static void
FlushMessages(void)
{
    DWORD Deadline = GetTickCount() + 300;
    MSG Msg;

    do
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Msg);
        Sleep(10);
    } while ((LONG)(Deadline - GetTickCount()) > 0);
}

static COLORREF
BarPixel(HDC hdc, const RECT *Window, LONG x, LONG y)
{
    return GetPixel(hdc, x - Window->left, y - Window->top);
}

START_TEST(MenuBarBackground)
{
    MENUBARINFO BarInfo = {sizeof(BarInfo)};
    MENUINFO Info = {sizeof(Info)};
    WNDCLASSW Class = {0};
    RECT Window, Item;
    HBRUSH Brush;
    HMENU Menu;
    HWND hWnd;
    HDC hdc;
    LONG y;

    Class.lpfnWndProc = WndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hbrBackground = GetStockObject(WHITE_BRUSH);
    Class.lpszClassName = L"MenuBarBackground";
    RegisterClassW(&Class);
    hWnd = CreateWindowExW(WS_EX_TOPMOST, Class.lpszClassName, L"MenuBarBackground",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 400, 200,
                           NULL, NULL, Class.hInstance, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd)
        return;

    Menu = CreateMenu();
    ok(AppendMenuW(Menu, MF_STRING, ID_STRING, L"&File"), "AppendMenuW failed\n");
    ok(AppendMenuW(Menu, MF_OWNERDRAW, ID_OWNERDRAW, (LPCWSTR)ID_OWNERDRAW), "AppendMenuW failed\n");
    Brush = CreateSolidBrush(BACK_COLOR);
    Info.fMask = MIM_BACKGROUND;
    Info.hbrBack = Brush;
    ok(SetMenuInfo(Menu, &Info), "SetMenuInfo failed\n");
    ok(SetMenu(hWnd, Menu), "SetMenu failed\n");
    SetForegroundWindow(hWnd);
    RedrawWindow(hWnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    FlushMessages();

    ok(DrawCount != 0, "The owner-drawn item was not drawn\n");
    ok(GetMenuBarInfo(hWnd, OBJID_MENU, 0, &BarInfo), "GetMenuBarInfo failed\n");
    GetWindowRect(hWnd, &Window);
    y = (BarInfo.rcBar.top + BarInfo.rcBar.bottom) / 2;
    hdc = GetWindowDC(hWnd);

    ok(BarPixel(hdc, &Window, BarInfo.rcBar.right - 4, y) == BACK_COLOR,
       "Bar after the items: 0x%06lx\n", BarPixel(hdc, &Window, BarInfo.rcBar.right - 4, y));
    ok(BarPixel(hdc, &Window, BarInfo.rcBar.right - 4, BarInfo.rcBar.bottom - 1) == BACK_COLOR,
       "Bottom row of the bar: 0x%06lx\n", BarPixel(hdc, &Window, BarInfo.rcBar.right - 4, BarInfo.rcBar.bottom - 1));

    ok(GetMenuItemRect(hWnd, Menu, 0, &Item), "GetMenuItemRect(0) failed\n");
    ok(BarPixel(hdc, &Window, Item.left + 1, Item.top + 1) == BACK_COLOR,
       "String item background: 0x%06lx\n", BarPixel(hdc, &Window, Item.left + 1, Item.top + 1));

    ok(GetMenuItemRect(hWnd, Menu, 1, &Item), "GetMenuItemRect(1) failed\n");
    ok(BarPixel(hdc, &Window, (Item.left + Item.right) / 2, y) == ITEM_COLOR,
       "Owner-drawn item: 0x%06lx\n", BarPixel(hdc, &Window, (Item.left + Item.right) / 2, y));

    ReleaseDC(hWnd, hdc);
    DestroyWindow(hWnd);
    DeleteObject(Brush);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
