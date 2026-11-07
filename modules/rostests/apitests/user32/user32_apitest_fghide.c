/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Window helper process for the ForegroundAfterHide test
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <windows.h>
#include "fghide.h"

static LRESULT CALLBACK
ControlProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    HWND hwndNew;

    switch (Message)
    {
        case FGHIDE_CREATE:
            hwndNew = CreateWindowExW(0, FGHIDE_WINDOW_CLASS, L"ForegroundAfterHide helper", WS_OVERLAPPEDWINDOW,
                                      (INT)wParam, (INT)lParam, 240, 160, NULL, NULL, GetModuleHandleW(NULL), NULL);
            ShowWindow(hwndNew, SW_SHOWNOACTIVATE);
            return (LRESULT)hwndNew;
        case FGHIDE_DESTROY:
            return DestroyWindow((HWND)wParam);
        case FGHIDE_ALLOW:
            return AllowSetForegroundWindow((DWORD)wParam);
        case FGHIDE_ACTIVATE:
            return SetForegroundWindow((HWND)wParam);
        case FGHIDE_SLEEP:
            Sleep((DWORD)wParam);
            return 0;
        case FGHIDE_HIDE:
            return ShowWindow((HWND)wParam, SW_HIDE);
    }
    return DefWindowProcW(hWnd, Message, wParam, lParam);
}

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    WNDCLASSW Class = { 0 };
    HANDLE Ready;
    MSG Message;

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = hInstance;
    Class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    Class.lpszClassName = FGHIDE_WINDOW_CLASS;
    RegisterClassW(&Class);
    Class.lpfnWndProc = ControlProc;
    Class.lpszClassName = FGHIDE_CONTROL_CLASS;
    RegisterClassW(&Class);
    if (!CreateWindowExW(0, FGHIDE_CONTROL_CLASS, lpCmdLine, WS_POPUP, 0, 0, 1, 1, NULL, NULL, hInstance, NULL))
        return 1;
    Ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, FGHIDE_READY_EVENT);
    if (!Ready)
        return 2;
    SetEvent(Ready);
    CloseHandle(Ready);
    while (GetMessageW(&Message, NULL, 0, 0) > 0)
        DispatchMessageW(&Message);
    return 0;
}
