/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The monitors of the desktop agree with their display devices and the virtual screen
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "precomp.h"
#include <d3dkmthk.h>

#define MAX_MONITORS 16
#define PROBE_COLOR RGB(0x12, 0x9A, 0x56)

static LONG (WINAPI *pGetDisplayConfigBufferSizes)(UINT32, UINT32 *, UINT32 *);
static LONG (WINAPI *pQueryDisplayConfig)(UINT32, UINT32 *, DISPLAYCONFIG_PATH_INFO *, UINT32 *,
                                          DISPLAYCONFIG_MODE_INFO *, DISPLAYCONFIG_TOPOLOGY_ID *);
static LONG (WINAPI *pDisplayConfigGetDeviceInfo)(DISPLAYCONFIG_DEVICE_INFO_HEADER *);

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
    POINT Center, Cursor;

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
    ok(SetCursorPos(Center.x, Center.y), "%ls: SetCursorPos failed with %lu\n", Info.szDevice, GetLastError());
    ok(GetCursorPos(&Cursor) && Cursor.x == Center.x && Cursor.y == Center.y,
       "%ls: the cursor went to (%ld,%ld) for (%ld,%ld)\n", Info.szDevice, Cursor.x, Cursor.y, Center.x, Center.y);

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
    HRGN Region;
    POINT Origin;
    RECT Client, Box;
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
    ok(GetDCOrgEx(Dc, &Origin), "GetDCOrgEx failed with %lu\n", GetLastError());
    ok(Origin.x == x && Origin.y == y, "the window DC origin is (%ld,%ld), the window is at (%d,%d)\n", Origin.x,
       Origin.y, x, y);
    Region = CreateRectRgn(0, 0, 0, 0);
    ok(GetRandomRgn(Dc, Region, SYSRGN) == 1, "GetRandomRgn(SYSRGN) failed\n");
    GetRgnBox(Region, &Box);
    ok(Box.left == x && Box.top == y && Box.right == x + 64 && Box.bottom == y + 64,
       "the system region of the window at (%d,%d) is %s\n", x, y, wine_dbgstr_rect(&Box));
    DeleteObject(Region);
    ReleaseDC(Window, Dc);
    ok(Color == PROBE_COLOR, "the window reads 0x%06lx at (%d,%d), expected 0x%06lx\n", Color, x + 32, y + 32,
       (ULONG)PROBE_COLOR);
    DestroyWindow(Window);
}

/* Each active display path names the monitor its source mode covers */
static void
TestDisplayConfig(const MONITOR_LIST *List)
{
    DISPLAYCONFIG_PATH_INFO Paths[MAX_MONITORS];
    DISPLAYCONFIG_MODE_INFO Modes[MAX_MONITORS * 2];
    DISPLAYCONFIG_SOURCE_DEVICE_NAME SourceName;
    const DISPLAYCONFIG_SOURCE_MODE *Source;
    MONITORINFOEXW Info;
    UINT32 PathCount = 0, ModeCount = 0;
    UINT32 Path;
    ULONG Index;
    LONG Result;
    BOOL Found;

    pGetDisplayConfigBufferSizes = (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDisplayConfigBufferSizes");
    pQueryDisplayConfig = (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "QueryDisplayConfig");
    pDisplayConfigGetDeviceInfo = (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "DisplayConfigGetDeviceInfo");
    if (pGetDisplayConfigBufferSizes == NULL || pQueryDisplayConfig == NULL || pDisplayConfigGetDeviceInfo == NULL)
    {
        skip("no display configuration API\n");
        return;
    }
    Result = pGetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &PathCount, &ModeCount);
    ok(Result == ERROR_SUCCESS, "GetDisplayConfigBufferSizes returned %ld\n", Result);
    ok(PathCount == List->Count, "%u active paths, %lu monitors\n", PathCount, List->Count);
    if (Result != ERROR_SUCCESS || PathCount == 0 || PathCount > MAX_MONITORS || ModeCount > RTL_NUMBER_OF(Modes))
        return;
    Result = pQueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &PathCount, Paths, &ModeCount, Modes, NULL);
    ok(Result == ERROR_SUCCESS, "QueryDisplayConfig returned %ld\n", Result);
    if (Result != ERROR_SUCCESS)
        return;

    for (Path = 0; Path < PathCount; Path++)
    {
        ok(Paths[Path].sourceInfo.modeInfoIdx < ModeCount, "path %u: source mode %u of %u\n", Path,
           Paths[Path].sourceInfo.modeInfoIdx, ModeCount);
        if (Paths[Path].sourceInfo.modeInfoIdx >= ModeCount)
            continue;
        ok(Modes[Paths[Path].sourceInfo.modeInfoIdx].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE,
           "path %u: mode type %d\n", Path, Modes[Paths[Path].sourceInfo.modeInfoIdx].infoType);
        Source = &Modes[Paths[Path].sourceInfo.modeInfoIdx].sourceMode;

        ZeroMemory(&SourceName, sizeof(SourceName));
        SourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        SourceName.header.size = sizeof(SourceName);
        SourceName.header.adapterId = Paths[Path].sourceInfo.adapterId;
        SourceName.header.id = Paths[Path].sourceInfo.id;
        Result = pDisplayConfigGetDeviceInfo(&SourceName.header);
        ok(Result == ERROR_SUCCESS, "path %u: source name query returned %ld\n", Path, Result);

        Found = FALSE;
        for (Index = 0; Index < List->Count && !Found; Index++)
        {
            Info.cbSize = sizeof(Info);
            if (!GetMonitorInfoW(List->Handles[Index], (MONITORINFO *)&Info))
                continue;
            Found = Info.rcMonitor.left == Source->position.x && Info.rcMonitor.top == Source->position.y &&
                    Info.rcMonitor.right - Info.rcMonitor.left == (LONG)Source->width &&
                    Info.rcMonitor.bottom - Info.rcMonitor.top == (LONG)Source->height;
            if (Found && Result == ERROR_SUCCESS)
                ok(!wcscmp(SourceName.viewGdiDeviceName, Info.szDevice), "path %u: source %ls covers the monitor of %ls\n",
                   Path, SourceName.viewGdiDeviceName, Info.szDevice);
        }
        ok(Found, "path %u: no monitor at (%ld,%ld) %ux%u\n", Path, Source->position.x, Source->position.y,
           Source->width, Source->height);
    }
}

static BOOL
ListsMode(PCWSTR Device, UINT Width, UINT Height)
{
    DEVMODEW Mode;
    DWORD Index;

    for (Index = 0; ; Index++)
    {
        ZeroMemory(&Mode, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        if (!EnumDisplaySettingsExW(Device, Index, &Mode, EDS_RAWMODE))
            return FALSE;
        if (Mode.dmPelsWidth == Width && Mode.dmPelsHeight == Height && Mode.dmBitsPerPel == 32)
            return TRUE;
    }
}

static void
TestModeLists(void)
{
    NTSTATUS (APIENTRY *pOpenAdapter)(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *);
    NTSTATUS (APIENTRY *pGetModeList)(D3DKMT_GETDISPLAYMODELIST *);
    NTSTATUS (APIENTRY *pCloseAdapter)(const D3DKMT_CLOSEADAPTER *);
    HMODULE Gdi32 = GetModuleHandleW(L"gdi32.dll");
    HMODULE Win32u = LoadLibraryW(L"win32u.dll");
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
    D3DKMT_GETDISPLAYMODELIST List;
    D3DKMT_CLOSEADAPTER Close;
    DISPLAY_DEVICEW Device;
    D3DKMT_DISPLAYMODE *Modes;
    ULONG Missing, Checked, Mode;
    DWORD Index;

    pOpenAdapter = (void *)GetProcAddress(Gdi32, "D3DKMTOpenAdapterFromGdiDisplayName");
    pGetModeList = (void *)GetProcAddress(Gdi32, "D3DKMTGetDisplayModeList");
    pCloseAdapter = (void *)GetProcAddress(Gdi32, "D3DKMTCloseAdapter");
    if (!pGetModeList && Win32u)
        pGetModeList = (void *)GetProcAddress(Win32u, "NtGdiDdDDIGetDisplayModeList");
    if (!pCloseAdapter && Win32u)
        pCloseAdapter = (void *)GetProcAddress(Win32u, "NtGdiDdDDICloseAdapter");
    if (!pOpenAdapter || !pGetModeList || !pCloseAdapter)
    {
        skip("no D3DKMT display mode list\n");
        return;
    }
    for (Index = 0; ; Index++)
    {
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        if (!(Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) || (Device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER))
            continue;
        ZeroMemory(&Open, sizeof(Open));
        lstrcpynW(Open.DeviceName, Device.DeviceName, ARRAYSIZE(Open.DeviceName));
        if (pOpenAdapter(&Open) < 0)
        {
            skip("%ls: no adapter for D3DKMT\n", Device.DeviceName);
            continue;
        }
        ZeroMemory(&List, sizeof(List));
        List.hAdapter = Open.hAdapter;
        List.VidPnSourceId = Open.VidPnSourceId;
        Modes = NULL;
        if (pGetModeList(&List) >= 0 && List.ModeCount != 0 &&
            (Modes = HeapAlloc(GetProcessHeap(), 0, List.ModeCount * sizeof(*Modes))) != NULL)
        {
            List.pModeList = Modes;
            ok(pGetModeList(&List) >= 0, "%ls: D3DKMTGetDisplayModeList failed\n", Device.DeviceName);
            Missing = 0;
            Checked = 0;
            for (Mode = 0; Mode < List.ModeCount; Mode++)
            {
                if ((Modes[Mode].Format != D3DDDIFMT_X8R8G8B8 && Modes[Mode].Format != D3DDDIFMT_A8R8G8B8) ||
                    (Modes[Mode].DisplayOrientation != D3DDDI_ROTATION_IDENTITY && Modes[Mode].DisplayOrientation != 0))
                {
                    continue;
                }
                Checked++;
                if (!ListsMode(Device.DeviceName, Modes[Mode].Width, Modes[Mode].Height))
                {
                    if (Missing == 0)
                        trace("%ls: %ux%u is not a display setting\n", Device.DeviceName, Modes[Mode].Width, Modes[Mode].Height);
                    Missing++;
                }
            }
            ok(Missing == 0, "%ls: %lu of %lu source modes are not display settings\n", Device.DeviceName, Missing, Checked);
            trace("%ls: %lu source modes checked\n", Device.DeviceName, Checked);
        }
        HeapFree(GetProcessHeap(), 0, Modes);
        Close.hAdapter = Open.hAdapter;
        pCloseAdapter(&Close);
    }
}

static void
CheckDisplaySize(PCWSTR Device, DWORD Width, DWORD Height, const char *What)
{
    DEVMODEW Current;
    MONITORINFOEXW Info;
    HMONITOR Monitor;
    RECT Desktop;
    POINT Center;

    ZeroMemory(&Current, sizeof(Current));
    Current.dmSize = sizeof(Current);
    ok(EnumDisplaySettingsW(Device, ENUM_CURRENT_SETTINGS, &Current), "%s: %ls has no current settings\n", What, Device);
    ok(Current.dmPelsWidth == Width && Current.dmPelsHeight == Height, "%s: %ls is %lux%lu, expected %lux%lu\n",
       What, Device, Current.dmPelsWidth, Current.dmPelsHeight, Width, Height);
    Center.x = Current.dmPosition.x + (LONG)Width / 2;
    Center.y = Current.dmPosition.y + (LONG)Height / 2;
    Monitor = MonitorFromPoint(Center, MONITOR_DEFAULTTONULL);
    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    ok(Monitor != NULL && GetMonitorInfoW(Monitor, (MONITORINFO *)&Info), "%s: no monitor at (%ld,%ld)\n", What, Center.x, Center.y);
    ok(!wcscmp(Info.szDevice, Device), "%s: the monitor at (%ld,%ld) is %ls\n", What, Center.x, Center.y, Info.szDevice);
    ok(Info.rcMonitor.left == Current.dmPosition.x && Info.rcMonitor.top == Current.dmPosition.y &&
       Info.rcMonitor.right - Info.rcMonitor.left == (LONG)Width && Info.rcMonitor.bottom - Info.rcMonitor.top == (LONG)Height,
       "%s: %ls monitor %s, expected %lux%lu at (%ld,%ld)\n", What, Device, wine_dbgstr_rect(&Info.rcMonitor),
       Width, Height, Current.dmPosition.x, Current.dmPosition.y);
    ok(Info.rcMonitor.right <= GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) &&
       Info.rcMonitor.bottom <= GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN),
       "%s: %ls monitor %s is outside the virtual screen\n", What, Device, wine_dbgstr_rect(&Info.rcMonitor));
    ok(GetWindowRect(GetDesktopWindow(), &Desktop), "GetWindowRect of the desktop failed with %lu\n", GetLastError());
    ok(Desktop.left == GetSystemMetrics(SM_XVIRTUALSCREEN) && Desktop.top == GetSystemMetrics(SM_YVIRTUALSCREEN) &&
       Desktop.right - Desktop.left == GetSystemMetrics(SM_CXVIRTUALSCREEN) &&
       Desktop.bottom - Desktop.top == GetSystemMetrics(SM_CYVIRTUALSCREEN),
       "%s: the desktop window is %s\n", What, wine_dbgstr_rect(&Desktop));
}

static void
TestModeChange(void)
{
    DISPLAY_DEVICEW Device;
    DEVMODEW Original, Mode;
    DWORD Index;
    LONG Result;
    BOOL Found = FALSE;

    for (Index = 0; !Found; Index++)
    {
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        Found = (Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
                !(Device.StateFlags & (DISPLAY_DEVICE_PRIMARY_DEVICE | DISPLAY_DEVICE_MIRRORING_DRIVER));
    }
    if (!Found)
    {
        skip("no display beside the primary one\n");
        return;
    }
    ZeroMemory(&Original, sizeof(Original));
    Original.dmSize = sizeof(Original);
    ok(EnumDisplaySettingsW(Device.DeviceName, ENUM_CURRENT_SETTINGS, &Original), "%ls has no current settings\n", Device.DeviceName);
    Found = FALSE;
    for (Index = 0; !Found; Index++)
    {
        ZeroMemory(&Mode, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        if (!EnumDisplaySettingsW(Device.DeviceName, Index, &Mode))
            break;
        Found = Mode.dmBitsPerPel == 32 && Mode.dmPelsWidth >= 800 && Mode.dmPelsHeight >= 600 &&
                (Mode.dmPelsWidth != Original.dmPelsWidth || Mode.dmPelsHeight != Original.dmPelsHeight);
    }
    if (!Found)
    {
        skip("%ls offers one size only\n", Device.DeviceName);
        return;
    }
    Mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    Result = ChangeDisplaySettingsExW(Device.DeviceName, &Mode, NULL, 0, NULL);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "changing %ls to %lux%lu returned %ld\n", Device.DeviceName,
       Mode.dmPelsWidth, Mode.dmPelsHeight, Result);
    if (Result == DISP_CHANGE_SUCCESSFUL)
        CheckDisplaySize(Device.DeviceName, Mode.dmPelsWidth, Mode.dmPelsHeight, "changed");

    Original.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    Result = ChangeDisplaySettingsExW(Device.DeviceName, &Original, NULL, 0, NULL);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "restoring %ls to %lux%lu returned %ld\n", Device.DeviceName,
       Original.dmPelsWidth, Original.dmPelsHeight, Result);
    if (Result == DISP_CHANGE_SUCCESSFUL)
        CheckDisplaySize(Device.DeviceName, Original.dmPelsWidth, Original.dmPelsHeight, "restored");
}

static LONG
ApplyStored(PCWSTR Device, DEVMODEW *Mode)
{
    LONG Result = ChangeDisplaySettingsExW(Device, Mode, NULL, CDS_UPDATEREGISTRY | CDS_NORESET, NULL);

    if (Result != DISP_CHANGE_SUCCESSFUL)
        return Result;
    return ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
}

static void
TestAttachDetach(void)
{
    DISPLAY_DEVICEW Device;
    DEVMODEW Original, Detach;
    POINT Center;
    RECT Desktop;
    DWORD Index;
    LONG Result;
    int Monitors = GetSystemMetrics(SM_CMONITORS);
    BOOL Found = FALSE;

    for (Index = 0; !Found; Index++)
    {
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        Found = (Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
                !(Device.StateFlags & (DISPLAY_DEVICE_PRIMARY_DEVICE | DISPLAY_DEVICE_MIRRORING_DRIVER));
    }
    if (!Found)
    {
        skip("no display beside the primary one\n");
        return;
    }
    ZeroMemory(&Original, sizeof(Original));
    Original.dmSize = sizeof(Original);
    ok(EnumDisplaySettingsW(Device.DeviceName, ENUM_CURRENT_SETTINGS, &Original), "%ls has no current settings\n", Device.DeviceName);
    Center.x = Original.dmPosition.x + (LONG)Original.dmPelsWidth / 2;
    Center.y = Original.dmPosition.y + (LONG)Original.dmPelsHeight / 2;

    ZeroMemory(&Detach, sizeof(Detach));
    Detach.dmSize = sizeof(Detach);
    Detach.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    Result = ApplyStored(Device.DeviceName, &Detach);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "detaching %ls returned %ld\n", Device.DeviceName, Result);
    ok(GetSystemMetrics(SM_CMONITORS) == Monitors - 1, "%d monitors after detaching %ls, expected %d\n",
       GetSystemMetrics(SM_CMONITORS), Device.DeviceName, Monitors - 1);
    ok(MonitorFromPoint(Center, MONITOR_DEFAULTTONULL) == NULL, "a monitor is left at (%ld,%ld)\n", Center.x, Center.y);
    Device.cb = sizeof(Device);
    ok(EnumDisplayDevicesW(NULL, Index - 1, &Device, 0) && !(Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP),
       "%ls is still attached: %#lx\n", Device.DeviceName, Device.StateFlags);
    ok(GetWindowRect(GetDesktopWindow(), &Desktop) && Desktop.right - Desktop.left == GetSystemMetrics(SM_CXVIRTUALSCREEN),
       "the desktop window is %s after the detach\n", wine_dbgstr_rect(&Desktop));

    Original.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    Result = ApplyStored(Device.DeviceName, &Original);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "attaching %ls again returned %ld\n", Device.DeviceName, Result);
    ok(GetSystemMetrics(SM_CMONITORS) == Monitors, "%d monitors after attaching %ls again, expected %d\n",
       GetSystemMetrics(SM_CMONITORS), Device.DeviceName, Monitors);
    if (Result == DISP_CHANGE_SUCCESSFUL)
    {
        CheckDisplaySize(Device.DeviceName, Original.dmPelsWidth, Original.dmPelsHeight, "attached again");
        if (MonitorFromPoint(Center, MONITOR_DEFAULTTONULL) != NULL)
            TestDrawingOnMonitor(MonitorFromPoint(Center, MONITOR_DEFAULTTONULL));
    }
}

START_TEST(MultiMonitor)
{
    MONITOR_LIST List;
    RECT Union, Virtual;
    ULONG Primaries = 0;
    ULONG Index;
    POINT Saved, Origin;
    HDC Dc;

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
    GetCursorPos(&Saved);
    for (Index = 0; Index < List.Count; Index++)
        TestMonitor(List.Handles[Index], &Union, &Primaries);
    SetCursorPos(Saved.x, Saved.y);
    ok(Primaries == 1, "%lu primary monitors\n", Primaries);

    /* The screen DC draws in screen coordinates, whatever monitor holds the virtual screen's corner */
    Dc = GetDC(NULL);
    ok(Dc != NULL && GetDCOrgEx(Dc, &Origin), "GetDCOrgEx on the screen DC failed with %lu\n", GetLastError());
    ok(Origin.x == 0 && Origin.y == 0, "the screen DC origin is (%ld,%ld)\n", Origin.x, Origin.y);
    ReleaseDC(NULL, Dc);

    Virtual.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    Virtual.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    Virtual.right = Virtual.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    Virtual.bottom = Virtual.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    ok(EqualRect(&Union, &Virtual), "the monitors cover %s, the virtual screen is %s\n", wine_dbgstr_rect(&Union),
       wine_dbgstr_rect(&Virtual));
    TestDisplayConfig(&List);
    TestModeLists();
    TestModeChange();
    TestAttachDetach();

    if (List.Count < 2)
    {
        skip("one monitor: no drawing beyond the primary display\n");
        return;
    }
    for (Index = 0; Index < List.Count; Index++)
        TestDrawingOnMonitor(List.Handles[Index]);
}
