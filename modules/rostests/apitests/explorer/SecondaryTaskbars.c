/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that each secondary display keeps one taskbar across back-to-back display changes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>

#define SETTLE_WAIT_MS 3000

typedef struct _TRAY_SCAN
{
    ULONG Count;
    ULONG Misplaced;
    RECT Last;
} TRAY_SCAN;

static BOOL CALLBACK
ScanTray(HWND Window, LPARAM Param)
{
    TRAY_SCAN *Scan = (TRAY_SCAN *)Param;
    MONITORINFO Info;
    WCHAR Class[64];
    RECT Rect;

    if (!GetClassNameW(Window, Class, ARRAYSIZE(Class)) || wcscmp(Class, L"Shell_SecondaryTrayWnd") || !IsWindowVisible(Window))
        return TRUE;
    Scan->Count++;
    Info.cbSize = sizeof(Info);
    if (!GetWindowRect(Window, &Rect) ||
        !GetMonitorInfoW(MonitorFromRect(&Rect, MONITOR_DEFAULTTONULL), &Info) ||
        Rect.left != Info.rcMonitor.left || Rect.right != Info.rcMonitor.right || Rect.bottom != Info.rcMonitor.bottom)
    {
        Scan->Last = Rect;
        Scan->Misplaced++;
    }
    return TRUE;
}

static BOOL
TraysSettled(ULONG Expected, TRAY_SCAN *Scan)
{
    DWORD Start = GetTickCount();

    do
    {
        ZeroMemory(Scan, sizeof(*Scan));
        EnumWindows(ScanTray, (LPARAM)Scan);
        if (Scan->Count == Expected && Scan->Misplaced == 0)
            return TRUE;
        Sleep(50);
    } while (GetTickCount() - Start < SETTLE_WAIT_MS);
    return FALSE;
}

START_TEST(SecondaryTaskbars)
{
    DISPLAY_DEVICEW Device;
    DEVMODEW Original, Mode;
    TRAY_SCAN Scan;
    ULONG Expected;
    DWORD Index;
    LONG Changed, Restored;
    BOOL Found = FALSE;

    if (GetSystemMetrics(SM_CMONITORS) < 2 || !FindWindowW(L"Shell_TrayWnd", NULL))
    {
        skip("no taskbar on a desktop of several displays\n");
        return;
    }
    Expected = GetSystemMetrics(SM_CMONITORS) - 1;
    ZeroMemory(&Scan, sizeof(Scan));
    EnumWindows(ScanTray, (LPARAM)&Scan);
    if (Scan.Count == 0)
    {
        skip("taskbars are shown on the main display only\n");
        return;
    }
    ok(TraysSettled(Expected, &Scan), "%lu secondary taskbars (%lu misplaced, last at %s) for %lu secondary displays\n",
       Scan.Count, Scan.Misplaced, wine_dbgstr_rect(&Scan.Last), Expected);

    for (Index = 0; !Found; Index++)
    {
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        Found = (Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
                !(Device.StateFlags & (DISPLAY_DEVICE_PRIMARY_DEVICE | DISPLAY_DEVICE_MIRRORING_DRIVER));
    }
    ok(Found, "no secondary display device\n");
    if (!Found)
        return;
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
    Original.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    Changed = ChangeDisplaySettingsExW(Device.DeviceName, &Mode, NULL, 0, NULL);
    Restored = ChangeDisplaySettingsExW(Device.DeviceName, &Original, NULL, 0, NULL);
    ok(Changed == DISP_CHANGE_SUCCESSFUL, "changing %ls returned %ld\n", Device.DeviceName, Changed);
    ok(Restored == DISP_CHANGE_SUCCESSFUL, "restoring %ls returned %ld\n", Device.DeviceName, Restored);
    ok(TraysSettled(Expected, &Scan), "after two display changes: %lu secondary taskbars (%lu misplaced, last at %s) for %lu secondary displays\n",
       Scan.Count, Scan.Misplaced, wine_dbgstr_rect(&Scan.Last), Expected);
}
