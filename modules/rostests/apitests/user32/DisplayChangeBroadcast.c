/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a mode change does not wait for WM_DISPLAYCHANGE handlers
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

START_TEST(DisplayChangeBroadcast)
{
    WNDCLASSW Class = { 0 };
    DEVMODEW Current = { 0 }, Mode = { 0 }, Target = { 0 };
    HANDLE Threads[BLOCKED_WINDOWS], Ready;
    LARGE_INTEGER Frequency, Before, After;
    DWORD Index, Start;
    LONG Result;
    ULONGLONG Elapsed;

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
        return;
    }
    Target.dmSize = sizeof(Target);
    Target.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
    Target.dmBitsPerPel = Mode.dmBitsPerPel;
    Target.dmPelsWidth = Mode.dmPelsWidth;
    Target.dmPelsHeight = Mode.dmPelsHeight;
    ExpectedBits = Target.dmBitsPerPel;
    ExpectedSize = MAKELPARAM(Target.dmPelsWidth, Target.dmPelsHeight);

    Class.lpfnWndProc = BlockedProc;
    Class.lpszClassName = L"DisplayChangeBlocked";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed with %lu\n", GetLastError());
    ReleaseEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    for (Index = 0; Index < BLOCKED_WINDOWS; Index++)
    {
        Threads[Index] = CreateThread(NULL, 0, WindowThread, Ready, 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread failed with %lu\n", GetLastError());
        WaitForSingleObject(Ready, 5000);
    }

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Before);
    Result = ChangeDisplaySettingsExW(NULL, &Target, NULL, 0, NULL);
    QueryPerformanceCounter(&After);
    Elapsed = (After.QuadPart - Before.QuadPart) * 1000 / Frequency.QuadPart;
    ok(Result == DISP_CHANGE_SUCCESSFUL, "Dynamic change returned %ld\n", Result);
    ok(Elapsed < 1000, "The change waited %I64u ms for %d blocked WM_DISPLAYCHANGE handlers\n",
       Elapsed, BLOCKED_WINDOWS);

    Start = GetTickCount();
    while (Received < BLOCKED_WINDOWS && GetTickCount() - Start < 5000)
        Sleep(10);
    ok(Received == BLOCKED_WINDOWS, "%ld of %d windows got WM_DISPLAYCHANGE\n", Received, BLOCKED_WINDOWS);
    ok(WrongParameters == 0, "%ld windows got wrong WM_DISPLAYCHANGE parameters\n", WrongParameters);

    SetEvent(ReleaseEvent);
    WaitForMultipleObjects(BLOCKED_WINDOWS, Threads, TRUE, 10000);
    for (Index = 0; Index < BLOCKED_WINDOWS; Index++)
        CloseHandle(Threads[Index]);
    CloseHandle(Ready);
    CloseHandle(ReleaseEvent);
    ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
    UnregisterClassW(L"DisplayChangeBlocked", NULL);
}
