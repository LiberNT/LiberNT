/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a mode change or an attached display does not wait for WM_DISPLAYCHANGE handlers
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define BLOCKED_WINDOWS 12

static HANDLE ReleaseEvent;
static volatile LONG Received;
static volatile LONG WrongParameters;
static WPARAM ExpectedBits;
static LPARAM ExpectedSize;

static LRESULT CALLBACK
BlockedProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_DISPLAYCHANGE)
    {
        if (wParam != ExpectedBits || lParam != ExpectedSize)
            InterlockedIncrement(&WrongParameters);
        InterlockedIncrement(&Received);
        WaitForSingleObject(ReleaseEvent, 5000);
        return 0;
    }
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static DWORD WINAPI
WindowThread(PVOID Ready)
{
    MSG Message;
    HWND Window;
    DWORD Start;

    Window = CreateWindowW(L"DisplayChangeBlocked", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    SetEvent(Ready);
    if (!Window)
        return 1;
    Start = GetTickCount();
    while (GetTickCount() - Start < 8000)
    {
        if (MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT) == WAIT_OBJECT_0)
        {
            while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
                DispatchMessageW(&Message);
        }
        if (Received == BLOCKED_WINDOWS && WaitForSingleObject(ReleaseEvent, 0) == WAIT_OBJECT_0)
            break;
    }
    DestroyWindow(Window);
    return 0;
}

typedef LONG (*DISPLAY_CHANGE)(PVOID Context);

static VOID
TestBlockedHandlers(
    _In_ DISPLAY_CHANGE Change,
    _In_ PVOID Context,
    _In_ PCSTR What)
{
    HANDLE Threads[BLOCKED_WINDOWS], Ready;
    LARGE_INTEGER Frequency, Before, After;
    DWORD Index, Start;
    LONG Result;
    ULONGLONG Elapsed;

    Received = 0;
    WrongParameters = 0;
    ReleaseEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    for (Index = 0; Index < BLOCKED_WINDOWS; Index++)
    {
        Threads[Index] = CreateThread(NULL, 0, WindowThread, Ready, 0, NULL);
        ok(Threads[Index] != NULL, "%s: CreateThread failed with %lu\n", What, GetLastError());
        WaitForSingleObject(Ready, 5000);
    }

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Before);
    Result = Change(Context);
    QueryPerformanceCounter(&After);
    Elapsed = (After.QuadPart - Before.QuadPart) * 1000 / Frequency.QuadPart;
    ok(Result == DISP_CHANGE_SUCCESSFUL, "%s returned %ld\n", What, Result);
    ok(Elapsed < 1000, "%s waited %I64u ms for %d blocked WM_DISPLAYCHANGE handlers\n",
       What, Elapsed, BLOCKED_WINDOWS);

    Start = GetTickCount();
    while (Received < BLOCKED_WINDOWS && GetTickCount() - Start < 5000)
        Sleep(10);
    ok(Received == BLOCKED_WINDOWS, "%s: %ld of %d windows got WM_DISPLAYCHANGE\n", What, Received, BLOCKED_WINDOWS);
    ok(WrongParameters == 0, "%s: %ld windows got wrong WM_DISPLAYCHANGE parameters\n", What, WrongParameters);

    SetEvent(ReleaseEvent);
    WaitForMultipleObjects(BLOCKED_WINDOWS, Threads, TRUE, 10000);
    for (Index = 0; Index < BLOCKED_WINDOWS; Index++)
        CloseHandle(Threads[Index]);
    CloseHandle(Ready);
    CloseHandle(ReleaseEvent);
}

static LONG
ChangeMode(PVOID Context)
{
    return ChangeDisplaySettingsExW(NULL, Context, NULL, 0, NULL);
}

static LONG
ApplyStored(PCWSTR Device, PDEVMODEW Mode)
{
    LONG Result = ChangeDisplaySettingsExW(Device, Mode, NULL, CDS_UPDATEREGISTRY | CDS_NORESET, NULL);

    if (Result != DISP_CHANGE_SUCCESSFUL)
        return Result;
    return ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
}

typedef struct _ATTACH_CONTEXT
{
    PCWSTR Device;
    DEVMODEW Mode;
} ATTACH_CONTEXT, *PATTACH_CONTEXT;

static LONG
AttachDisplay(PVOID Context)
{
    PATTACH_CONTEXT Attach = Context;

    return ApplyStored(Attach->Device, &Attach->Mode);
}

static VOID
TestAttach(VOID)
{
    DISPLAY_DEVICEW Device;
    ATTACH_CONTEXT Attach = { 0 };
    DEVMODEW Primary = { 0 }, Detach = { 0 };
    RECT Desktop;
    DWORD Index;
    int Monitors = GetSystemMetrics(SM_CMONITORS);
    BOOL Found = FALSE;

    for (Index = 0; !Found; Index++)
    {
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        Found = !(Device.StateFlags & (DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | DISPLAY_DEVICE_MIRRORING_DRIVER));
    }
    if (!Found)
    {
        skip("No detached display device\n");
        return;
    }
    Attach.Mode.dmSize = sizeof(Attach.Mode);
    if (!EnumDisplaySettingsW(Device.DeviceName, 0, &Attach.Mode))
    {
        skip("%ls lists no mode\n", Device.DeviceName);
        return;
    }
    Primary.dmSize = sizeof(Primary);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Primary), "No current settings\n");
    Attach.Device = Device.DeviceName;
    Attach.Mode.dmFields = DM_POSITION | DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
    Attach.Mode.dmPosition.x = (LONG)Primary.dmPelsWidth;
    Attach.Mode.dmPosition.y = 0;
    ExpectedBits = Primary.dmBitsPerPel;
    ExpectedSize = MAKELPARAM(Primary.dmPelsWidth, Primary.dmPelsHeight);

    TestBlockedHandlers(AttachDisplay, &Attach, "Attaching a display");

    ok(GetSystemMetrics(SM_CMONITORS) == Monitors + 1, "%d monitors after attaching %ls to %d\n",
       GetSystemMetrics(SM_CMONITORS), Device.DeviceName, Monitors);
    ok(GetWindowRect(GetDesktopWindow(), &Desktop), "GetWindowRect of the desktop failed with %lu\n", GetLastError());
    ok(Desktop.left == GetSystemMetrics(SM_XVIRTUALSCREEN) && Desktop.top == GetSystemMetrics(SM_YVIRTUALSCREEN) &&
       Desktop.right - Desktop.left == GetSystemMetrics(SM_CXVIRTUALSCREEN) &&
       Desktop.bottom - Desktop.top == GetSystemMetrics(SM_CYVIRTUALSCREEN),
       "The desktop window is %s after attaching %ls\n", wine_dbgstr_rect(&Desktop), Device.DeviceName);

    Detach.dmSize = sizeof(Detach);
    Detach.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    ok(ApplyStored(Device.DeviceName, &Detach) == DISP_CHANGE_SUCCESSFUL, "Detaching %ls failed\n", Device.DeviceName);
}

START_TEST(DisplayChangeBroadcast)
{
    WNDCLASSW Class = { 0 };
    DEVMODEW Current = { 0 }, Mode = { 0 }, Target = { 0 };
    DWORD Index;

    Class.lpfnWndProc = BlockedProc;
    Class.lpszClassName = L"DisplayChangeBlocked";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed with %lu\n", GetLastError());

    Current.dmSize = Mode.dmSize = sizeof(DEVMODEW);
    ok(EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &Current), "No current settings\n");
    for (Index = 0; EnumDisplaySettingsW(NULL, Index, &Mode); Index++)
    {
        if (Mode.dmBitsPerPel == Current.dmBitsPerPel &&
            (Mode.dmPelsWidth != Current.dmPelsWidth || Mode.dmPelsHeight != Current.dmPelsHeight))
            break;
    }
    if (Mode.dmPelsWidth == Current.dmPelsWidth && Mode.dmPelsHeight == Current.dmPelsHeight)
    {
        skip("The display has a single mode\n");
    }
    else
    {
        Target.dmSize = sizeof(Target);
        Target.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
        Target.dmBitsPerPel = Mode.dmBitsPerPel;
        Target.dmPelsWidth = Mode.dmPelsWidth;
        Target.dmPelsHeight = Mode.dmPelsHeight;
        ExpectedBits = Target.dmBitsPerPel;
        ExpectedSize = MAKELPARAM(Target.dmPelsWidth, Target.dmPelsHeight);
        TestBlockedHandlers(ChangeMode, &Target, "The mode change");
        ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
    }

    TestAttach();
    UnregisterClassW(L"DisplayChangeBlocked", NULL);
}
