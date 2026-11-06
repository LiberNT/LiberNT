/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test window activation after switching between desktops
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

typedef struct _ACTIVATE_THREAD
{
    HDESK Desktop;
    HANDLE Request;
    HANDLE Done;
    HWND Window;
    HWND Foreground;
    HWND Active;
    BOOL Stop;
} ACTIVATE_THREAD, *PACTIVATE_THREAD;

static VOID
Pump(DWORD Milliseconds)
{
    DWORD Start = GetTickCount();
    MSG Message;

    do
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Message);
        Sleep(10);
    } while (GetTickCount() - Start < Milliseconds);
}

static DWORD WINAPI
ActivateThread(PVOID Parameter)
{
    PACTIVATE_THREAD Context = Parameter;
    DWORD Wait;

    if (!SetThreadDesktop(Context->Desktop))
    {
        SetEvent(Context->Done);
        return 1;
    }
    Context->Window = CreateWindowW(L"STATIC", L"SwitchDesktopActivate B", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                    10, 10, 200, 100, NULL, NULL, NULL, NULL);
    SetEvent(Context->Done);
    for (;;)
    {
        Wait = MsgWaitForMultipleObjects(1, &Context->Request, FALSE, INFINITE, QS_ALLINPUT);
        if (Wait != WAIT_OBJECT_0 && Wait != WAIT_OBJECT_0 + 1)
            break;
        if (Wait == WAIT_OBJECT_0)
        {
            if (Context->Stop)
                break;
            SetForegroundWindow(Context->Window);
            SetActiveWindow(Context->Window);
            Pump(200);
            Context->Foreground = GetForegroundWindow();
            Context->Active = GetActiveWindow();
            SetEvent(Context->Done);
        }
        Pump(0);
    }
    if (Context->Window)
        DestroyWindow(Context->Window);
    return 0;
}

START_TEST(SwitchDesktopActivate)
{
    ACTIVATE_THREAD Context = { 0 };
    HDESK InputDesktop;
    HANDLE Thread;
    HWND Window;

    InputDesktop = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
    ok(InputDesktop != NULL, "OpenInputDesktop failed with %lu\n", GetLastError());
    Context.Desktop = CreateDesktopW(L"SwitchDesktopActivate", NULL, NULL, 0, GENERIC_ALL, NULL);
    ok(Context.Desktop != NULL, "CreateDesktopW failed with %lu\n", GetLastError());
    if (!InputDesktop || !Context.Desktop)
        return;

    Window = CreateWindowW(L"STATIC", L"SwitchDesktopActivate A", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           10, 10, 200, 100, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindowW failed with %lu\n", GetLastError());
    SetForegroundWindow(Window);
    Pump(300);
    ok(GetForegroundWindow() == Window, "Foreground %p, expected %p\n", GetForegroundWindow(), Window);

    Context.Request = CreateEventW(NULL, FALSE, FALSE, NULL);
    Context.Done = CreateEventW(NULL, FALSE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, ActivateThread, &Context, 0, NULL);
    WaitForSingleObject(Context.Done, 5000);
    ok(Context.Window != NULL, "No window on the second desktop\n");

    ok(SwitchDesktop(Context.Desktop), "SwitchDesktop failed with %lu\n", GetLastError());
    SetEvent(Context.Request);
    WaitForSingleObject(Context.Done, 5000);
    ok(Context.Foreground == Context.Window, "Second desktop: foreground %p, expected %p\n",
       Context.Foreground, Context.Window);
    ok(Context.Active == Context.Window, "Second desktop: active %p, expected %p\n", Context.Active, Context.Window);

    ok(SwitchDesktop(InputDesktop), "SwitchDesktop back failed with %lu\n", GetLastError());
    Pump(300);
    ok(GetForegroundWindow() == Window, "After switching back: foreground %p, expected %p\n",
       GetForegroundWindow(), Window);
    ok(SetActiveWindow(Window) == Window, "SetActiveWindow did not return %p\n", Window);
    ok(GetActiveWindow() == Window, "Active %p, expected %p\n", GetActiveWindow(), Window);
    ok(GetForegroundWindow() == Window, "After SetActiveWindow: foreground %p, expected %p\n",
       GetForegroundWindow(), Window);

    Context.Stop = TRUE;
    SetEvent(Context.Request);
    WaitForSingleObject(Thread, 5000);
    CloseHandle(Thread);
    CloseHandle(Context.Request);
    CloseHandle(Context.Done);
    DestroyWindow(Window);
    SwitchDesktop(InputDesktop);
    CloseDesktop(Context.Desktop);
    CloseDesktop(InputDesktop);
}
