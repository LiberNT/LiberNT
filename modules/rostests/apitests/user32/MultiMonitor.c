/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The monitors of the desktop agree with their display devices and the virtual screen
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "precomp.h"

#define MAX_MONITORS 16
#define PROBE_COLOR RGB(0x12, 0x9A, 0x56)

typedef struct _MONITOR_LIST
{
    ULONG Count;
    HMONITOR Handles[MAX_MONITORS];
} MONITOR_LIST;

static BOOL CALLBACK
CollectMonitor(HMONITOR Monitor, HDC Dc, LPRECT Rect, LPARAM Param)
{
    MONITOR_LIST *List = (MONITOR_LIST *)Param;

    UNREFERENCED_PARAMETER(Dc);
    UNREFERENCED_PARAMETER(Rect);
    if (List->Count < MAX_MONITORS)
        List->Handles[List->Count] = Monitor;
    List->Count++;
    return TRUE;
}

static ULONG
CountAttachedDevices(void)
{
    DISPLAY_DEVICEW Device;
    ULONG Attached = 0;
    DWORD Index;

    for (Index = 0;; Index++)
    {
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        if (Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
            Attached++;
    }
    return Attached;
}

static DWORD
DeviceState(PCWSTR Name)
{
    DISPLAY_DEVICEW Device;
    DWORD Index;

    for (Index = 0;; Index++)
    {
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            return 0;
        if (!wcscmp(Device.DeviceName, Name))
            return Device.StateFlags;
    }
}

static void
TestMonitor(HMONITOR Monitor, RECT *Union, ULONG *Primaries)
{
    MONITORINFOEXW Info;
    DEVMODEW Mode;
    DWORD State;
    POINT Center;

    Info.cbSize = sizeof(Info);
    ok(GetMonitorInfoW(Monitor, (MONITORINFO *)&Info), "GetMonitorInfo failed with %lu\n", GetLastError());
    ok(Info.rcWork.left >= Info.rcMonitor.left && Info.rcWork.top >= Info.rcMonitor.top &&
       Info.rcWork.right <= Info.rcMonitor.right && Info.rcWork.bottom <= Info.rcMonitor.bottom,
       "%ls: work area %s outside the monitor %s\n", Info.szDevice, wine_dbgstr_rect(&Info.rcWork),
       wine_dbgstr_rect(&Info.rcMonitor));

    State = DeviceState(Info.szDevice);
    ok(State & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP, "%ls: state 0x%lx is not attached to the desktop\n", Info.szDevice, State);

    ZeroMemory(&Mode, sizeof(Mode));
    Mode.dmSize = sizeof(Mode);
    ok(EnumDisplaySettingsExW(Info.szDevice, ENUM_CURRENT_SETTINGS, &Mode, 0), "%ls: no current mode\n", Info.szDevice);
    ok(Mode.dmPosition.x == Info.rcMonitor.left && Mode.dmPosition.y == Info.rcMonitor.top,
       "%ls: mode position (%ld,%ld), monitor %s\n", Info.szDevice, Mode.dmPosition.x, Mode.dmPosition.y,
       wine_dbgstr_rect(&Info.rcMonitor));
    ok((LONG)Mode.dmPelsWidth == Info.rcMonitor.right - Info.rcMonitor.left &&
       (LONG)Mode.dmPelsHeight == Info.rcMonitor.bottom - Info.rcMonitor.top,
       "%ls: mode %lux%lu, monitor %s\n", Info.szDevice, Mode.dmPelsWidth, Mode.dmPelsHeight,
       wine_dbgstr_rect(&Info.rcMonitor));

    Center.x = (Info.rcMonitor.left + Info.rcMonitor.right) / 2;
    Center.y = (Info.rcMonitor.top + Info.rcMonitor.bottom) / 2;
    ok(MonitorFromPoint(Center, MONITOR_DEFAULTTONULL) == Monitor, "%ls: its centre belongs to another monitor\n", Info.szDevice);

    if (Info.dwFlags & MONITORINFOF_PRIMARY)
    {
        (*Primaries)++;
        ok(State & DISPLAY_DEVICE_PRIMARY_DEVICE, "%ls: primary monitor, device state 0x%lx\n", Info.szDevice, State);
        ok(Info.rcMonitor.left == 0 && Info.rcMonitor.top == 0, "%ls: primary monitor at %s\n", Info.szDevice,
           wine_dbgstr_rect(&Info.rcMonitor));
        ok(Info.rcMonitor.right == GetSystemMetrics(SM_CXSCREEN) && Info.rcMonitor.bottom == GetSystemMetrics(SM_CYSCREEN),
           "%ls: primary monitor %s, screen %dx%d\n", Info.szDevice, wine_dbgstr_rect(&Info.rcMonitor),
           GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    }
    UnionRect(Union, Union, &Info.rcMonitor);
}

/* A window on a monitor other than the primary one has pixels of its own there */
static void
TestDrawingOnMonitor(HMONITOR Monitor)
{
    MONITORINFO Info;
    COLORREF Color;
    HBRUSH Brush;
    RECT Client;
    HWND Window;
    HDC Dc;
    int x, y;

    Info.cbSize = sizeof(Info);
    if (!GetMonitorInfoW(Monitor, &Info) || (Info.dwFlags & MONITORINFOF_PRIMARY))
        return;
    x = Info.rcMonitor.left + 40;
    y = Info.rcMonitor.top + 40;
    Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", NULL, WS_POPUP | WS_VISIBLE, x, y, 64, 64,
                             NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindowEx failed with %lu\n", GetLastError());
    if (Window == NULL)
        return;
    ok(MonitorFromWindow(Window, MONITOR_DEFAULTTONULL) == Monitor, "the window is not on its monitor\n");

    Dc = GetDC(Window);
    Brush = CreateSolidBrush(PROBE_COLOR);
    GetClientRect(Window, &Client);
    FillRect(Dc, &Client, Brush);
    DeleteObject(Brush);
    Color = GetPixel(Dc, 32, 32);
    ReleaseDC(Window, Dc);
    ok(Color == PROBE_COLOR, "the window reads 0x%06lx at (%d,%d), expected 0x%06lx\n", Color, x + 32, y + 32,
       (ULONG)PROBE_COLOR);
    DestroyWindow(Window);
}

START_TEST(MultiMonitor)
{
    MONITOR_LIST List;
    RECT Union, Virtual;
    ULONG Primaries = 0;
    ULONG Index;

    ZeroMemory(&List, sizeof(List));
    ok(EnumDisplayMonitors(NULL, NULL, CollectMonitor, (LPARAM)&List), "EnumDisplayMonitors failed with %lu\n", GetLastError());
    ok(List.Count != 0 && List.Count <= MAX_MONITORS, "%lu monitors\n", List.Count);
    if (List.Count == 0 || List.Count > MAX_MONITORS)
        return;

    ok(GetSystemMetrics(SM_CMONITORS) == (int)List.Count, "SM_CMONITORS %d, %lu monitors enumerated\n",
       GetSystemMetrics(SM_CMONITORS), List.Count);
    ok(CountAttachedDevices() == List.Count, "%lu display devices attached to the desktop, %lu monitors\n",
       CountAttachedDevices(), List.Count);

    SetRectEmpty(&Union);
    for (Index = 0; Index < List.Count; Index++)
        TestMonitor(List.Handles[Index], &Union, &Primaries);
    ok(Primaries == 1, "%lu primary monitors\n", Primaries);

    Virtual.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    Virtual.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    Virtual.right = Virtual.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    Virtual.bottom = Virtual.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    ok(EqualRect(&Union, &Virtual), "the monitors cover %s, the virtual screen is %s\n", wine_dbgstr_rect(&Union),
       wine_dbgstr_rect(&Virtual));

    if (List.Count < 2)
    {
        skip("one monitor: no drawing beyond the primary display\n");
        return;
    }
    for (Index = 0; Index < List.Count; Index++)
        TestDrawingOnMonitor(List.Handles[Index]);
}
