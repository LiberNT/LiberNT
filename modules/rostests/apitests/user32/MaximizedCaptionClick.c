/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A click on a maximized caption keeps the window's restore position
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

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

static void
click(LONG x, LONG y)
{
    INPUT Input[2];

    SetCursorPos(x, y);
    ZeroMemory(Input, sizeof(Input));
    Input[0].type = INPUT_MOUSE;
    Input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    Input[1].type = INPUT_MOUSE;
    Input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, Input, sizeof(INPUT));
}

START_TEST(MaximizedCaptionClick)
{
    WINDOWPLACEMENT Placement = { sizeof(Placement) };
    WNDCLASSW Class = { 0 };
    RECT Work, WindowRect;
    POINT Point;
    LRESULT Hit;
    HWND Window;

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.lpszClassName = L"MaximizedCaptionClick";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed, error %lu\n", GetLastError());

    Window = CreateWindowExW(0, L"MaximizedCaptionClick", L"Caption click", WS_OVERLAPPEDWINDOW,
                             100, 100, 400, 300, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Window)
        return;
    ShowWindow(Window, SW_SHOWMAXIMIZED);
    SetForegroundWindow(Window);
    flush_messages(300);
    ok(IsZoomed(Window), "window is not maximized\n");

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &Work, 0);
    GetWindowRect(Window, &WindowRect);
    Point.x = (WindowRect.left + WindowRect.right) / 2;
    Point.y = Work.top;
    Hit = SendMessageW(Window, WM_NCHITTEST, 0, MAKELPARAM(Point.x, Point.y));
    ok(Hit == HTCAPTION, "top edge of the maximized caption hit %Id, expected HTCAPTION\n", Hit);

    click(Point.x, Point.y);
    flush_messages(500);

    ok(IsZoomed(Window), "window is no longer maximized after the click\n");
    ok(GetWindowPlacement(Window, &Placement), "GetWindowPlacement failed, error %lu\n", GetLastError());
    ok(Placement.rcNormalPosition.left == 100 && Placement.rcNormalPosition.top == 100 &&
       Placement.rcNormalPosition.right == 500 && Placement.rcNormalPosition.bottom == 400,
       "restore position %ld,%ld-%ld,%ld, expected 100,100-500,400\n",
       Placement.rcNormalPosition.left, Placement.rcNormalPosition.top,
       Placement.rcNormalPosition.right, Placement.rcNormalPosition.bottom);

    ShowWindow(Window, SW_RESTORE);
    flush_messages(300);
    GetWindowRect(Window, &WindowRect);
    ok(WindowRect.left == 100 && WindowRect.top == 100 && WindowRect.right == 500 && WindowRect.bottom == 400,
       "restored window %ld,%ld-%ld,%ld, expected 100,100-500,400\n",
       WindowRect.left, WindowRect.top, WindowRect.right, WindowRect.bottom);

    DestroyWindow(Window);
    UnregisterClassW(L"MaximizedCaptionClick", GetModuleHandleW(NULL));
}
