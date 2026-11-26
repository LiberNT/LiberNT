/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Dragging a maximized caption restores the window with a frame change
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static UINT RestoreFlags;
static UINT RestoreShowCmd;
static UINT CalcSizeCount;
static BOOL Recording;

static LRESULT CALLBACK
WndProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Recording && Message == WM_NCCALCSIZE && wParam)
        CalcSizeCount++;
    if (Recording && Message == WM_WINDOWPOSCHANGED && !RestoreFlags &&
        !(((WINDOWPOS *)lParam)->flags & SWP_NOSIZE) && !IsZoomed(Window))
    {
        WINDOWPLACEMENT Placement = { sizeof(Placement) };

        RestoreFlags = ((WINDOWPOS *)lParam)->flags;
        GetWindowPlacement(Window, &Placement);
        RestoreShowCmd = Placement.showCmd;
    }
    return DefWindowProcW(Window, Message, wParam, lParam);
}

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
set_mouse(INPUT *Input, DWORD Flags, LONG x, LONG y)
{
    ZeroMemory(Input, sizeof(*Input));
    Input->type = INPUT_MOUSE;
    Input->mi.dwFlags = Flags | MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    Input->mi.dx = MulDiv(x, 65535, GetSystemMetrics(SM_CXSCREEN) - 1);
    Input->mi.dy = MulDiv(y, 65535, GetSystemMetrics(SM_CYSCREEN) - 1);
}

static void
check_restore(HWND Window, PCSTR How)
{
    ok(!IsZoomed(Window), "%s: window is still maximized\n", How);
    ok(RestoreFlags != 0, "%s: no resizing WM_WINDOWPOSCHANGED\n", How);
    ok(RestoreFlags == (SWP_NOZORDER | SWP_FRAMECHANGED | SWP_STATECHANGED),
       "%s: restore flags 0x%x, expected 0x%x\n", How, RestoreFlags, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_STATECHANGED);
    ok(RestoreShowCmd == SW_SHOWNORMAL, "%s: show command %u during the restore, expected SW_SHOWNORMAL\n",
       How, RestoreShowCmd);
    ok(CalcSizeCount == 1, "%s: %u WM_NCCALCSIZE, expected 1\n", How, CalcSizeCount);
}

START_TEST(MaximizedCaptionDrag)
{
    WNDCLASSW Class = { 0 };
    RECT WindowRect;
    BOOL FullDrag = FALSE;
    INPUT Input[12];
    LONG x, y, i;
    HWND Window;

    SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS, 0, &FullDrag, 0);
    ok(FullDrag, "full window drag is off\n");

    Class.lpfnWndProc = WndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.lpszClassName = L"MaximizedCaptionDrag";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed, error %lu\n", GetLastError());

    Window = CreateWindowExW(0, L"MaximizedCaptionDrag", L"Caption drag", WS_OVERLAPPEDWINDOW,
                             100, 100, 400, 300, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Window)
        return;

    ShowWindow(Window, SW_SHOWMAXIMIZED);
    SetForegroundWindow(Window);
    flush_messages(300);
    ok(IsZoomed(Window), "window is not maximized\n");
    Recording = TRUE;
    ShowWindow(Window, SW_RESTORE);
    flush_messages(300);
    Recording = FALSE;
    check_restore(Window, "SW_RESTORE");

    ShowWindow(Window, SW_SHOWMAXIMIZED);
    SetForegroundWindow(Window);
    flush_messages(300);
    ok(IsZoomed(Window), "window is not maximized again\n");

    GetWindowRect(Window, &WindowRect);
    x = (WindowRect.left + WindowRect.right) / 2;
    y = max(WindowRect.top, 0) + GetSystemMetrics(SM_CYCAPTION) / 2;
    ok(SendMessageW(Window, WM_NCHITTEST, 0, MAKELPARAM(x, y)) == HTCAPTION, "drag point is not on the caption\n");

    RestoreFlags = RestoreShowCmd = CalcSizeCount = 0;
    Recording = TRUE;
    SetCursorPos(x, y);
    set_mouse(&Input[0], MOUSEEVENTF_LEFTDOWN, x, y);
    for (i = 1; i <= 10; i++)
        set_mouse(&Input[i], 0, x, y + i * 20);
    set_mouse(&Input[11], MOUSEEVENTF_LEFTUP, x, y + 200);
    ok(SendInput(ARRAYSIZE(Input), Input, sizeof(INPUT)) == ARRAYSIZE(Input), "SendInput failed, error %lu\n", GetLastError());
    flush_messages(500);
    Recording = FALSE;
    check_restore(Window, "caption drag");

    GetWindowRect(Window, &WindowRect);
    ok(WindowRect.right - WindowRect.left == 400 && WindowRect.bottom - WindowRect.top == 300,
       "dragged window is %ldx%ld, expected 400x300\n",
       WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top);

    DestroyWindow(Window);
    UnregisterClassW(L"MaximizedCaptionDrag", GetModuleHandleW(NULL));
}
