/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a mode change notifies the windows of desktops other than the input desktop
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

typedef struct _DESKTOP_WINDOW
{
    PCSTR Name;
    HDESK Desktop;
    HANDLE Ready;
    HANDLE Query;
    HANDLE Thread;
    HWND Window;
    volatile LONG Received;
    WPARAM Bits;
    LPARAM Size;
    RECT DesktopRect;
    RECT QueriedRect;
    BOOL Reset;
    LONG ResetResult;
} DESKTOP_WINDOW, *PDESKTOP_WINDOW;

static HANDLE StopEvent;

static LRESULT CALLBACK
DesktopsProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    PDESKTOP_WINDOW Context = (PDESKTOP_WINDOW)GetWindowLongPtrW(Window, GWLP_USERDATA);

    if (Message == WM_DISPLAYCHANGE && Context)
    {
        Context->Bits = wParam;
        Context->Size = lParam;
        GetWindowRect(GetDesktopWindow(), &Context->DesktopRect);
        InterlockedIncrement(&Context->Received);
        return 0;
    }
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static DWORD WINAPI
DesktopsThread(PVOID Parameter)
{
    PDESKTOP_WINDOW Context = Parameter;
    HANDLE Events[2] = { StopEvent, Context->Query };
    MSG Message;
    DWORD Wait;

    if (Context->Desktop && !SetThreadDesktop(Context->Desktop))
    {
        SetEvent(Context->Ready);
        return 1;
    }
    Context->Window = CreateWindowW(L"DisplayChangeDesktops", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    if (Context->Window)
        SetWindowLongPtrW(Context->Window, GWLP_USERDATA, (LONG_PTR)Context);
    SetEvent(Context->Ready);
    if (!Context->Window)
        return 1;
    for (;;)
    {
        Wait = MsgWaitForMultipleObjects(2, Events, FALSE, INFINITE, QS_ALLINPUT);
        if (Wait != WAIT_OBJECT_0 + 1 && Wait != WAIT_OBJECT_0 + 2)
            break;
        if (Wait == WAIT_OBJECT_0 + 1)
        {
            if (Context->Reset)
                Context->ResetResult = ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
            else
                GetWindowRect(GetDesktopWindow(), &Context->QueriedRect);
            SetEvent(Context->Ready);
            continue;
        }
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Message);
    }
    DestroyWindow(Context->Window);
    return 0;
}

static BOOL
StartDesktopWindow(PDESKTOP_WINDOW Context)
{
    Context->Ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    Context->Query = CreateEventW(NULL, FALSE, FALSE, NULL);
    Context->Thread = CreateThread(NULL, 0, DesktopsThread, Context, 0, NULL);
    ok(Context->Thread != NULL, "%s: CreateThread failed with %lu\n", Context->Name, GetLastError());
    if (!Context->Thread)
        return FALSE;
    WaitForSingleObject(Context->Ready, 5000);
    ok(Context->Window != NULL, "%s: no window\n", Context->Name);
    return Context->Window != NULL;
}

static VOID
StopDesktopWindow(PDESKTOP_WINDOW Context)
{
    if (Context->Thread)
    {
        WaitForSingleObject(Context->Thread, 5000);
        CloseHandle(Context->Thread);
    }
    if (Context->Ready)
        CloseHandle(Context->Ready);
    if (Context->Query)
        CloseHandle(Context->Query);
    if (Context->Desktop)
        CloseDesktop(Context->Desktop);
}

static VOID
WaitForDisplayChange(PDESKTOP_WINDOW Context, LONG Expected)
{
    DWORD Start = GetTickCount();

    while (Context->Window && Context->Received < Expected && GetTickCount() - Start < 3000)
        Sleep(20);
}

static VOID
SettleDisplayChange(VOID)
{
    Sleep(1000);
}

static VOID
CheckDisplayChange(PDESKTOP_WINDOW Context, PCSTR Step, LONG Expected, const DEVMODEW *Mode)
{
    if (!Context->Window)
        return;
    ok(Context->Received == Expected, "%s, %s: %ld WM_DISPLAYCHANGE, expected %ld\n",
       Step, Context->Name, Context->Received, Expected);
    if (!Mode || Context->Received != Expected)
        return;
    ok(Context->Bits == Mode->dmBitsPerPel && Context->Size == MAKELPARAM(Mode->dmPelsWidth, Mode->dmPelsHeight),
       "%s, %s: WM_DISPLAYCHANGE %Iu %ux%u, expected %lu %lux%lu\n", Step, Context->Name, Context->Bits,
       LOWORD(Context->Size), HIWORD(Context->Size), Mode->dmBitsPerPel, Mode->dmPelsWidth, Mode->dmPelsHeight);
    ok(Context->DesktopRect.right - Context->DesktopRect.left == (LONG)Mode->dmPelsWidth &&
       Context->DesktopRect.bottom - Context->DesktopRect.top == (LONG)Mode->dmPelsHeight,
       "%s, %s: desktop window %s, expected %lux%lu\n", Step, Context->Name,
       wine_dbgstr_rect(&Context->DesktopRect), Mode->dmPelsWidth, Mode->dmPelsHeight);
}

static VOID
CheckDesktopRect(PDESKTOP_WINDOW Context, PCSTR Step, const DEVMODEW *Mode)
{
    if (!Context->Window)
        return;
    SetRectEmpty(&Context->QueriedRect);
    Context->Reset = FALSE;
    SetEvent(Context->Query);
    WaitForSingleObject(Context->Ready, 5000);
    ok(Context->QueriedRect.right - Context->QueriedRect.left == (LONG)Mode->dmPelsWidth &&
       Context->QueriedRect.bottom - Context->QueriedRect.top == (LONG)Mode->dmPelsHeight,
       "%s, %s: desktop window %s, expected %lux%lu\n", Step, Context->Name,
       wine_dbgstr_rect(&Context->QueriedRect), Mode->dmPelsWidth, Mode->dmPelsHeight);
}

START_TEST(DisplayChangeDesktops)
{
    WNDCLASSW Class = { 0 };
    DEVMODEW Current = { 0 }, Mode = { 0 }, Target = { 0 };
    DESKTOP_WINDOW Input = { "input desktop" }, Second = { "second desktop" }, Other = { "other window station" };
    HWINSTA WindowStation, OtherStation;
    HDESK InputDesktop;
    ULONG Index;
    LONG Result;

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

    InputDesktop = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
    ok(InputDesktop != NULL, "OpenInputDesktop failed with %lu\n", GetLastError());
    Class.lpfnWndProc = DesktopsProc;
    Class.lpszClassName = L"DisplayChangeDesktops";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed with %lu\n", GetLastError());
    StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    Second.Desktop = CreateDesktopW(L"DisplayChangeDesktops", NULL, NULL, 0, GENERIC_ALL, NULL);
    ok(Second.Desktop != NULL, "CreateDesktopW failed with %lu\n", GetLastError());

    WindowStation = GetProcessWindowStation();
    OtherStation = CreateWindowStationW(L"DisplayChangeDesktops", 0, GENERIC_ALL, NULL);
    ok(OtherStation != NULL, "CreateWindowStationW failed with %lu\n", GetLastError());
    if (OtherStation && SetProcessWindowStation(OtherStation))
    {
        Other.Desktop = CreateDesktopW(L"DisplayChangeDesktops", NULL, NULL, 0, GENERIC_ALL, NULL);
        ok(Other.Desktop != NULL, "CreateDesktopW on the other window station failed with %lu\n", GetLastError());
        if (Other.Desktop)
            StartDesktopWindow(&Other);
        ok(SetProcessWindowStation(WindowStation), "SetProcessWindowStation failed with %lu\n", GetLastError());
    }

    StartDesktopWindow(&Input);
    if (Second.Desktop)
        StartDesktopWindow(&Second);

    Result = ChangeDisplaySettingsExW(NULL, &Target, NULL, 0, NULL);
    ok(Result == DISP_CHANGE_SUCCESSFUL, "Dynamic change returned %ld\n", Result);
    if (Result != DISP_CHANGE_SUCCESSFUL || !InputDesktop || !Second.Window)
        goto cleanup;
    WaitForDisplayChange(&Input, 1);
    SettleDisplayChange();
    CheckDisplayChange(&Input, "change", 1, &Target);
    CheckDisplayChange(&Second, "change", 0, NULL);
    CheckDisplayChange(&Other, "change", 0, NULL);
    CheckDesktopRect(&Second, "change", &Target);
    CheckDesktopRect(&Other, "change", &Target);

    ok(SwitchDesktop(Second.Desktop), "SwitchDesktop failed with %lu\n", GetLastError());
    WaitForDisplayChange(&Second, 1);
    SettleDisplayChange();
    CheckDisplayChange(&Input, "switch to the second desktop", 1, &Target);
    CheckDisplayChange(&Second, "switch to the second desktop", 1, &Target);
    CheckDisplayChange(&Other, "switch to the second desktop", 0, NULL);

    Result = ChangeDisplaySettingsExW(NULL, &Current, NULL, CDS_TEST, NULL);
    ok(Result == DISP_CHANGE_FAILED, "Test from a desktop that is not the input desktop returned %ld\n", Result);
    Result = ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
    ok(Result == DISP_CHANGE_FAILED, "Reset from a desktop that is not the input desktop returned %ld\n", Result);
    SettleDisplayChange();
    CheckDisplayChange(&Input, "reset from the other desktop", 1, &Target);
    CheckDisplayChange(&Second, "reset from the other desktop", 1, &Target);

    Second.Reset = TRUE;
    Second.ResetResult = DISP_CHANGE_BADPARAM;
    SetEvent(Second.Query);
    WaitForSingleObject(Second.Ready, 5000);
    ok(Second.ResetResult == DISP_CHANGE_SUCCESSFUL, "Reset from the input desktop returned %ld\n", Second.ResetResult);
    WaitForDisplayChange(&Second, 2);
    SettleDisplayChange();
    CheckDisplayChange(&Input, "reset", 1, &Target);
    CheckDisplayChange(&Second, "reset", 2, &Current);
    CheckDisplayChange(&Other, "reset", 0, NULL);
    CheckDesktopRect(&Input, "reset", &Current);

    ok(SwitchDesktop(InputDesktop), "SwitchDesktop back failed with %lu\n", GetLastError());
    WaitForDisplayChange(&Input, 2);
    SettleDisplayChange();
    CheckDisplayChange(&Input, "switch back", 2, &Current);
    CheckDisplayChange(&Second, "switch back", 2, &Current);
    CheckDisplayChange(&Other, "switch back", 0, NULL);

    ok(SwitchDesktop(Second.Desktop), "SwitchDesktop failed with %lu\n", GetLastError());
    SettleDisplayChange();
    ok(SwitchDesktop(InputDesktop), "SwitchDesktop back failed with %lu\n", GetLastError());
    SettleDisplayChange();
    CheckDisplayChange(&Input, "switch without a change", 2, &Current);
    CheckDisplayChange(&Second, "switch without a change", 2, &Current);

cleanup:
    if (InputDesktop)
    {
        SwitchDesktop(InputDesktop);
        CloseDesktop(InputDesktop);
    }
    SetEvent(StopEvent);
    StopDesktopWindow(&Input);
    StopDesktopWindow(&Second);
    StopDesktopWindow(&Other);
    if (OtherStation)
        CloseWindowStation(OtherStation);
    CloseHandle(StopEvent);
    ChangeDisplaySettingsExW(NULL, NULL, NULL, 0, NULL);
    UnregisterClassW(L"DisplayChangeDesktops", NULL);
}
