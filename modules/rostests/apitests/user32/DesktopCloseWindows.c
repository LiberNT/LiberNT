/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that closing a desktop destroys its desktop and message windows
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

typedef struct _DESKTOP_WINDOWS
{
    HDESK Desktop;
    HWND DesktopWindow;
    HWND MessageParent;
    BOOL Attached;
    HANDLE Ready;
    HANDLE Leave;
} DESKTOP_WINDOWS, *PDESKTOP_WINDOWS;

static DWORD WINAPI
QueryDesktopWindows(PVOID Parameter)
{
    PDESKTOP_WINDOWS Windows = Parameter;
    HWND Window;

    Windows->Attached = SetThreadDesktop(Windows->Desktop);
    if (!Windows->Attached)
        return 1;
    Windows->DesktopWindow = GetDesktopWindow();
    Window = CreateWindowExW(0, L"STATIC", L"DesktopCloseWindows", 0, 0, 0, 10, 10,
                             HWND_MESSAGE, NULL, NULL, NULL);
    if (Window)
    {
        Windows->MessageParent = GetAncestor(Window, GA_PARENT);
        DestroyWindow(Window);
    }
    if (Windows->Ready)
    {
        SetEvent(Windows->Ready);
        WaitForSingleObject(Windows->Leave, 10000);
    }
    return 0;
}

static BOOL
WindowAlive(HWND Window)
{
    SetLastError(0xdeadbeef);
    if (PostMessageW(Window, WM_NULL, 0, 0))
        return TRUE;
    return GetLastError() != ERROR_INVALID_WINDOW_HANDLE;
}

static BOOL
WaitWindowGone(HWND Window)
{
    DWORD Start = GetTickCount();

    while (WindowAlive(Window))
    {
        if (GetTickCount() - Start > 3000)
            return FALSE;
        Sleep(10);
    }
    return TRUE;
}

static HANDLE
StartChildOnDesktop(PCWSTR DesktopName)
{
    WCHAR WinStaName[64], FullName[160], CommandLine[MAX_PATH + 16];
    STARTUPINFOW StartupInfo = {sizeof(StartupInfo)};
    PROCESS_INFORMATION ProcessInfo;
    DWORD Length;

    if (!GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, WinStaName, sizeof(WinStaName), &Length))
        return NULL;
    swprintf(FullName, ARRAYSIZE(FullName), L"%s\\%s", WinStaName, DesktopName);
    Length = GetSystemDirectoryW(CommandLine, MAX_PATH);
    if (!Length || Length >= MAX_PATH)
        return NULL;
    wcscat(CommandLine, L"\\cmd.exe /c exit");
    StartupInfo.lpDesktop = FullName;
    if (!CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &StartupInfo, &ProcessInfo))
        return NULL;
    CloseHandle(ProcessInfo.hThread);
    return ProcessInfo.hProcess;
}

static VOID
TestCloseDesktop(UINT Round, HDESK InputDesktop, BOOL CloseInChild)
{
    DESKTOP_WINDOWS Windows = {0};
    WCHAR Name[64];
    HANDLE Thread, Child = NULL;

    swprintf(Name, ARRAYSIZE(Name), L"DesktopCloseWindows%lu_%u", GetCurrentProcessId(), Round);
    Windows.Desktop = CreateDesktopW(Name, NULL, NULL, 0, GENERIC_ALL, NULL);
    ok(Windows.Desktop != NULL, "Round %u: CreateDesktopW failed: %lu\n", Round, GetLastError());
    if (!Windows.Desktop)
        return;

    Thread = CreateThread(NULL, 0, QueryDesktopWindows, &Windows, 0, NULL);
    ok(Thread != NULL, "Round %u: CreateThread failed: %lu\n", Round, GetLastError());
    if (Thread)
    {
        ok_long(WaitForSingleObject(Thread, 10000), WAIT_OBJECT_0);
        CloseHandle(Thread);
    }
    ok(Windows.Attached, "Round %u: SetThreadDesktop failed\n", Round);
    ok(Windows.DesktopWindow != NULL, "Round %u: no desktop window\n", Round);
    ok(Windows.MessageParent != NULL, "Round %u: no message window\n", Round);
    ok(Windows.DesktopWindow != GetDesktopWindow(), "Round %u: got the desktop window of the current desktop\n", Round);
    ok(WindowAlive(Windows.DesktopWindow), "Round %u: desktop window is gone before the desktop is closed\n", Round);
    ok(WindowAlive(Windows.MessageParent), "Round %u: message window is gone before the desktop is closed\n", Round);

    if (InputDesktop)
    {
        ok(SwitchDesktop(Windows.Desktop), "Round %u: SwitchDesktop failed: %lu\n", Round, GetLastError());
        Sleep(200);
        ok(SwitchDesktop(InputDesktop), "Round %u: SwitchDesktop back failed: %lu\n", Round, GetLastError());
        Sleep(200);
    }

    if (CloseInChild)
    {
        Child = StartChildOnDesktop(Name);
        ok(Child != NULL, "Round %u: CreateProcessW failed: %lu\n", Round, GetLastError());
    }
    ok(CloseDesktop(Windows.Desktop), "Round %u: CloseDesktop failed: %lu\n", Round, GetLastError());
    if (Child)
    {
        ok_long(WaitForSingleObject(Child, 10000), WAIT_OBJECT_0);
        CloseHandle(Child);
    }
    ok(WaitWindowGone(Windows.DesktopWindow), "Round %u: desktop window %p still exists after the desktop was closed\n",
       Round, Windows.DesktopWindow);
    ok(WaitWindowGone(Windows.MessageParent), "Round %u: message window %p still exists after the desktop was closed\n",
       Round, Windows.MessageParent);
}

static VOID
TestCloseWhileAttached(UINT Round)
{
    DESKTOP_WINDOWS Windows = {0};
    WCHAR Name[64];
    HANDLE Thread;

    swprintf(Name, ARRAYSIZE(Name), L"DesktopCloseWindows%lu_%u", GetCurrentProcessId(), Round);
    Windows.Desktop = CreateDesktopW(Name, NULL, NULL, 0, GENERIC_ALL, NULL);
    ok(Windows.Desktop != NULL, "Round %u: CreateDesktopW failed: %lu\n", Round, GetLastError());
    if (!Windows.Desktop)
        return;
    Windows.Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    Windows.Leave = CreateEventW(NULL, TRUE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, QueryDesktopWindows, &Windows, 0, NULL);
    ok(Thread != NULL, "Round %u: CreateThread failed: %lu\n", Round, GetLastError());
    if (!Thread)
    {
        CloseDesktop(Windows.Desktop);
        CloseHandle(Windows.Ready);
        CloseHandle(Windows.Leave);
        return;
    }
    ok_long(WaitForSingleObject(Windows.Ready, 10000), WAIT_OBJECT_0);
    ok(Windows.Attached, "Round %u: SetThreadDesktop failed\n", Round);
    ok(WindowAlive(Windows.DesktopWindow), "Round %u: desktop window is gone before the desktop is closed\n", Round);

    SetLastError(0xdeadbeef);
    ok(!CloseDesktop(Windows.Desktop), "Round %u: CloseDesktop closed a desktop another thread is on\n", Round);
    ok_long(GetLastError(), ERROR_BUSY);
    ok(WindowAlive(Windows.DesktopWindow), "Round %u: desktop window is gone while a thread is still on the desktop\n",
       Round);

    SetEvent(Windows.Leave);
    ok_long(WaitForSingleObject(Thread, 10000), WAIT_OBJECT_0);
    CloseHandle(Thread);
    ok(WindowAlive(Windows.DesktopWindow), "Round %u: desktop window is gone while the desktop is still open\n", Round);
    ok(CloseDesktop(Windows.Desktop), "Round %u: CloseDesktop failed once the thread left: %lu\n", Round, GetLastError());
    ok(WaitWindowGone(Windows.DesktopWindow), "Round %u: desktop window %p still exists after the desktop was closed\n",
       Round, Windows.DesktopWindow);
    ok(WaitWindowGone(Windows.MessageParent), "Round %u: message window %p still exists after the desktop was closed\n",
       Round, Windows.MessageParent);
    CloseHandle(Windows.Ready);
    CloseHandle(Windows.Leave);
}

START_TEST(DesktopCloseWindows)
{
    HDESK InputDesktop;
    UINT Round;

    for (Round = 0; Round < 3; ++Round)
        TestCloseDesktop(Round, NULL, FALSE);
    for (Round = 6; Round < 9; ++Round)
        TestCloseDesktop(Round, NULL, TRUE);
    for (Round = 9; Round < 12; ++Round)
        TestCloseWhileAttached(Round);

    InputDesktop = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
    ok(InputDesktop != NULL, "OpenInputDesktop failed: %lu\n", GetLastError());
    if (!InputDesktop)
        return;
    for (Round = 3; Round < 6; ++Round)
        TestCloseDesktop(Round, InputDesktop, FALSE);
    CloseDesktop(InputDesktop);
}
