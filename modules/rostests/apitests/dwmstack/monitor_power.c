/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that SC_MONITORPOWER turns the console display off and back on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <initguid.h>

DEFINE_GUID(TestGuidConsoleDisplayState, 0x6fe69556, 0x704a, 0x47a0, 0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47);

#define MONITOR_WAIT_MS 2000

static DWORD g_DisplayState = MAXDWORD;
static ULONG g_Notifications;

static LRESULT CALLBACK
MonitorWndProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_POWERBROADCAST && wParam == PBT_POWERSETTINGCHANGE)
    {
        const POWERBROADCAST_SETTING *Setting = (const POWERBROADCAST_SETTING *)lParam;

        if (IsEqualGUID(&Setting->PowerSetting, &TestGuidConsoleDisplayState) &&
            Setting->DataLength >= sizeof(DWORD))
        {
            g_DisplayState = *(const DWORD *)Setting->Data;
            ++g_Notifications;
        }
        return TRUE;
    }
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static BOOL
WaitForDisplayState(DWORD State)
{
    DWORD Start = GetTickCount();
    MSG Message;

    for (;;)
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Message);
        if (g_DisplayState == State)
            return TRUE;
        if (GetTickCount() - Start >= MONITOR_WAIT_MS)
            return FALSE;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
}

START_TEST(monitor_power)
{
    WNDCLASSW Class = {0};
    HPOWERNOTIFY Notify;
    INPUT Input = {0};
    HWND Window;

    Class.lpfnWndProc = MonitorWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"DwmMonitorPower";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed: %lu\n", GetLastError());
    Window = CreateWindowExW(0, Class.lpszClassName, L"monitor power", WS_OVERLAPPED,
                             0, 0, 10, 10, NULL, NULL, Class.hInstance, NULL);
    ok(Window != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (Window == NULL)
        return;

    Notify = RegisterPowerSettingNotification(Window, &TestGuidConsoleDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    ok(Notify != NULL, "RegisterPowerSettingNotification failed: %lu\n", GetLastError());
    if (Notify == NULL)
    {
        DestroyWindow(Window);
        return;
    }
    ok(WaitForDisplayState(1), "the current console display state was not delivered as on (%lu)\n", g_DisplayState);
    ok(g_Notifications >= 1, "no console display notification arrived\n");

    DefWindowProcW(Window, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
    ok(WaitForDisplayState(0), "SC_MONITORPOWER 2 left the console display at %lu\n", g_DisplayState);

    Input.type = INPUT_MOUSE;
    Input.mi.dwFlags = MOUSEEVENTF_MOVE;
    ok(SendInput(1, &Input, sizeof(Input)) == 1, "SendInput failed: %lu\n", GetLastError());
    ok(WaitForDisplayState(1), "input left the console display at %lu\n", g_DisplayState);

    ok(UnregisterPowerSettingNotification(Notify), "UnregisterPowerSettingNotification failed: %lu\n", GetLastError());
    DestroyWindow(Window);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
