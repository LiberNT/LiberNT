/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the desktop attachment state reported for each display device
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define MAX_TEST_DEVICES 16

typedef struct _MONITOR_LIST
{
    ULONG Count;
    WCHAR Names[MAX_TEST_DEVICES][CCHDEVICENAME];
    RECT Rects[MAX_TEST_DEVICES];
} MONITOR_LIST, *PMONITOR_LIST;

static BOOL CALLBACK
CollectMonitor(
    _In_ HMONITOR Monitor,
    _In_ HDC Hdc,
    _In_ LPRECT Rect,
    _In_ LPARAM Param)
{
    PMONITOR_LIST List = (PMONITOR_LIST)Param;
    MONITORINFOEXW Info;

    UNREFERENCED_PARAMETER(Hdc);
    UNREFERENCED_PARAMETER(Rect);

    if (List->Count >= MAX_TEST_DEVICES)
        return FALSE;
    ZeroMemory(&Info, sizeof(Info));
    Info.cbSize = sizeof(Info);
    if (GetMonitorInfoW(Monitor, (LPMONITORINFO)&Info))
    {
        lstrcpynW(List->Names[List->Count], Info.szDevice, CCHDEVICENAME);
        List->Rects[List->Count] = Info.rcMonitor;
        List->Count++;
    }
    return TRUE;
}

static LONG
FindMonitor(
    _In_ const MONITOR_LIST *List,
    _In_ PCWSTR DeviceName)
{
    ULONG Index;

    for (Index = 0; Index < List->Count; Index++)
    {
        if (lstrcmpiW(List->Names[Index], DeviceName) == 0)
            return (LONG)Index;
    }
    return -1;
}

static ULONG
CountModes(
    _In_opt_ PCWSTR DeviceName)
{
    DEVMODEW Mode;
    ULONG Count = 0;

    for (;;)
    {
        ZeroMemory(&Mode, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        if (!EnumDisplaySettingsW(DeviceName, Count, &Mode))
            break;
        Count++;
    }
    return Count;
}

static VOID
TestDetachedDevice(
    _In_ const DISPLAY_DEVICEW *Device,
    _In_ const DEVMODEW *Primary)
{
    DEVMODEW Mode;

    ZeroMemory(&Mode, sizeof(Mode));
    Mode.dmSize = sizeof(Mode);
    if (!EnumDisplaySettingsW(Device->DeviceName, 0, &Mode))
        return;

    Mode.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    Mode.dmPosition.x = Primary->dmPelsWidth;
    Mode.dmPosition.y = 0;
    ok_long(ChangeDisplaySettingsExW(Device->DeviceName, &Mode, NULL, CDS_TEST, NULL), DISP_CHANGE_FAILED);
    ok_long(ChangeDisplaySettingsExW(Device->DeviceName, &Mode, NULL, 0, NULL), DISP_CHANGE_FAILED);

    ZeroMemory(&Mode, sizeof(Mode));
    Mode.dmSize = sizeof(Mode);
    Mode.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    ok_long(ChangeDisplaySettingsExW(Device->DeviceName, &Mode, NULL, CDS_UPDATEREGISTRY | CDS_NORESET, NULL),
            DISP_CHANGE_SUCCESSFUL);
    ok_long(ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL), DISP_CHANGE_SUCCESSFUL);

    ZeroMemory(&Mode, sizeof(Mode));
    Mode.dmSize = sizeof(Mode);
    ok(!EnumDisplaySettingsW(Device->DeviceName, ENUM_CURRENT_SETTINGS, &Mode),
       "Staging a detach attached %ls\n", Device->DeviceName);
}

START_TEST(DisplayDeviceAttach)
{
    DISPLAY_DEVICEW Device;
    DISPLAY_DEVICEW Primary;
    DEVMODEW PrimaryBefore;
    DEVMODEW PrimaryAfter;
    DEVMODEW Mode;
    MONITOR_LIST Monitors;
    ULONG Index;
    ULONG DeviceCount = 0;
    ULONG PrimaryCount = 0;
    ULONG AttachedCount = 0;
    ULONG DetachedCount = 0;
    BOOL Result;

    ZeroMemory(&Primary, sizeof(Primary));
    ZeroMemory(&Monitors, sizeof(Monitors));
    ok(EnumDisplayMonitors(NULL, NULL, CollectMonitor, (LPARAM)&Monitors), "EnumDisplayMonitors failed\n");

    ZeroMemory(&PrimaryBefore, sizeof(PrimaryBefore));
    PrimaryBefore.dmSize = sizeof(PrimaryBefore);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &PrimaryBefore), "EnumDisplaySettingsW(NULL) failed\n");

    for (Index = 0; Index < MAX_TEST_DEVICES; Index++)
    {
        ULONG ModeCount;

        ZeroMemory(&Device, sizeof(Device));
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        DeviceCount++;

        ZeroMemory(&Mode, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        SetLastError(0xdeadbeef);
        Result = EnumDisplaySettingsW(Device.DeviceName, ENUM_CURRENT_SETTINGS, &Mode);
        ModeCount = CountModes(Device.DeviceName);
        trace("%lu: %ls flags 0x%lx '%ls' current %d %lux%lux%lu at %ld,%ld, %lu modes\n",
              Index, Device.DeviceName, Device.StateFlags, Device.DeviceString, Result,
              Mode.dmPelsWidth, Mode.dmPelsHeight, Mode.dmBitsPerPel,
              Mode.dmPosition.x, Mode.dmPosition.y, ModeCount);

        if (Device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)
            continue;

        if (Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            PrimaryCount++;
            Primary = Device;
            ok(Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP,
               "Primary %ls is not attached to the desktop (0x%lx)\n", Device.DeviceName, Device.StateFlags);
        }

        if (Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
        {
            LONG MonitorIndex;

            AttachedCount++;
            ok(Result, "EnumDisplaySettingsW(%ls, ENUM_CURRENT_SETTINGS) failed\n", Device.DeviceName);
            ok(Mode.dmPelsWidth != 0 && Mode.dmPelsHeight != 0, "%ls has an empty current mode\n", Device.DeviceName);
            ok(ModeCount != 0, "%ls lists no mode\n", Device.DeviceName);
            MonitorIndex = FindMonitor(&Monitors, Device.DeviceName);
            ok(MonitorIndex >= 0, "Attached %ls has no monitor\n", Device.DeviceName);
            if (MonitorIndex >= 0 && Result)
            {
                ok(Monitors.Rects[MonitorIndex].left == Mode.dmPosition.x &&
                   Monitors.Rects[MonitorIndex].top == Mode.dmPosition.y &&
                   Monitors.Rects[MonitorIndex].right - Monitors.Rects[MonitorIndex].left == (LONG)Mode.dmPelsWidth &&
                   Monitors.Rects[MonitorIndex].bottom - Monitors.Rects[MonitorIndex].top == (LONG)Mode.dmPelsHeight,
                   "%ls monitor rect does not match its current mode\n", Device.DeviceName);
            }
        }
        else
        {
            DetachedCount++;
            ok(!(Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE),
               "Detached %ls is the primary device\n", Device.DeviceName);
            ok(FindMonitor(&Monitors, Device.DeviceName) < 0, "Detached %ls has a desktop monitor\n", Device.DeviceName);
            ok(!Result, "Detached %ls has a current mode\n", Device.DeviceName);
            ok(ModeCount != 0, "Detached %ls lists no mode\n", Device.DeviceName);
            TestDetachedDevice(&Device, &PrimaryBefore);
        }
    }

    ok(DeviceCount != 0, "No display device was enumerated\n");
    ok(PrimaryCount == 1, "%lu primary devices\n", PrimaryCount);
    ok(AttachedCount == Monitors.Count, "%lu attached devices but %lu monitors\n", AttachedCount, Monitors.Count);
    ok(GetSystemMetrics(SM_CMONITORS) == (INT)AttachedCount,
       "SM_CMONITORS is %d, %lu attached devices\n", GetSystemMetrics(SM_CMONITORS), AttachedCount);

    if (PrimaryCount == 1)
    {
        ZeroMemory(&Mode, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        ok(EnumDisplaySettingsW(Primary.DeviceName, ENUM_CURRENT_SETTINGS, &Mode), "EnumDisplaySettingsW(%ls) failed\n", Primary.DeviceName);
        ok(Mode.dmPosition.x == 0 && Mode.dmPosition.y == 0,
           "Primary %ls is at %ld,%ld\n", Primary.DeviceName, Mode.dmPosition.x, Mode.dmPosition.y);
        ok(Mode.dmPelsWidth == PrimaryBefore.dmPelsWidth && Mode.dmPelsHeight == PrimaryBefore.dmPelsHeight,
           "Primary %ls is %lux%lu, the default device is %lux%lu\n", Primary.DeviceName,
           Mode.dmPelsWidth, Mode.dmPelsHeight, PrimaryBefore.dmPelsWidth, PrimaryBefore.dmPelsHeight);
        ok(GetSystemMetrics(SM_CXSCREEN) == (INT)Mode.dmPelsWidth && GetSystemMetrics(SM_CYSCREEN) == (INT)Mode.dmPelsHeight,
           "Screen is %dx%d, primary is %lux%lu\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
           Mode.dmPelsWidth, Mode.dmPelsHeight);
        ok(CountModes(NULL) == CountModes(Primary.DeviceName), "The default device lists %lu modes, primary %ls lists %lu\n",
           CountModes(NULL), Primary.DeviceName, CountModes(Primary.DeviceName));

        ZeroMemory(&Mode, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        ZeroMemory(&PrimaryAfter, sizeof(PrimaryAfter));
        PrimaryAfter.dmSize = sizeof(PrimaryAfter);
        ok(EnumDisplaySettingsW(Primary.DeviceName, ENUM_REGISTRY_SETTINGS, &Mode), "No registry settings for %ls\n", Primary.DeviceName);
        ok(EnumDisplaySettingsW(NULL, ENUM_REGISTRY_SETTINGS, &PrimaryAfter), "No registry settings for the default device\n");
        ok(Mode.dmPelsWidth == PrimaryAfter.dmPelsWidth && Mode.dmPelsHeight == PrimaryAfter.dmPelsHeight &&
           Mode.dmBitsPerPel == PrimaryAfter.dmBitsPerPel,
           "Default device registry mode %lux%lux%lu, primary %ls %lux%lux%lu\n",
           PrimaryAfter.dmPelsWidth, PrimaryAfter.dmPelsHeight, PrimaryAfter.dmBitsPerPel, Primary.DeviceName,
           Mode.dmPelsWidth, Mode.dmPelsHeight, Mode.dmBitsPerPel);
    }

    ZeroMemory(&PrimaryAfter, sizeof(PrimaryAfter));
    PrimaryAfter.dmSize = sizeof(PrimaryAfter);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &PrimaryAfter), "EnumDisplaySettingsW(NULL) failed\n");
    ok(PrimaryAfter.dmPelsWidth == PrimaryBefore.dmPelsWidth &&
       PrimaryAfter.dmPelsHeight == PrimaryBefore.dmPelsHeight &&
       PrimaryAfter.dmBitsPerPel == PrimaryBefore.dmBitsPerPel,
       "Primary mode changed from %lux%lux%lu to %lux%lux%lu\n",
       PrimaryBefore.dmPelsWidth, PrimaryBefore.dmPelsHeight, PrimaryBefore.dmBitsPerPel,
       PrimaryAfter.dmPelsWidth, PrimaryAfter.dmPelsHeight, PrimaryAfter.dmBitsPerPel);

    if (DetachedCount == 0)
        skip("No detached display device\n");
}
