/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Report what a newly started session sees as its display and console
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <windows.h>
#include <stdio.h>

BOOL WINAPI RegisterLogonProcess(DWORD dwProcessId, BOOL bRegister);

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR commandLine, int show)
{
    DISPLAY_DEVICEW device = { sizeof(device) };
    WCHAR path[MAX_PATH], text[256];
    HWINSTA station;
    HDESK desktop;
    DEVMODEW mode = { 0 };
    HDC screen;
    DWORD session = 0, written, index, desktopError;
    int devices = 0, remote = 0, length;
    HANDLE file;

    RegisterLogonProcess(GetCurrentProcessId(), TRUE);
    station = CreateWindowStationW(L"WinSta0", 0, WINSTA_ALL_ACCESS, NULL);
    if (station) SetProcessWindowStation(station);
    desktop = CreateDesktopW(L"Default", NULL, NULL, 0, GENERIC_ALL, NULL);
    desktopError = desktop ? 0 : GetLastError();
    if (desktop)
    {
        SetThreadDesktop(desktop);
        SwitchDesktop(desktop);
    }
    screen = GetDC(NULL);
    for (index = 0; EnumDisplayDevicesW(NULL, index, &device, 0); index++)
    {
        devices++;
        if (wcsstr(device.DeviceString, L"Remote Display")) remote++;
        device.cb = sizeof(device);
    }
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &mode)) mode.dmPelsWidth = mode.dmPelsHeight = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    length = _snwprintf(text, ARRAYSIZE(text),
                        L"session %lu console %lu devices %d remote %d screen %dx%d mode %lux%lu desktop %d error %lu",
                        session, WTSGetActiveConsoleSessionId(), devices, remote,
                        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                        mode.dmPelsWidth, mode.dmPelsHeight, desktop != NULL, desktopError);
    if (screen) ReleaseDC(NULL, screen);
    GetWindowsDirectoryW(path, MAX_PATH);
    lstrcatW(path, L"\\Temp\\session_child.txt");
    file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        WriteFile(file, text, length * sizeof(WCHAR), &written, NULL);
        CloseHandle(file);
    }
    Sleep(INFINITE);
    return 0;
}
