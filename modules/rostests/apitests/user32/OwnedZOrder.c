/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Z-order of owned and input method windows relative to their owner
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "precomp.h"
#include <imm.h>

typedef struct _THREAD_WINDOWS
{
    HWND Windows[16];
    UINT Count;
} THREAD_WINDOWS;

static BOOL CALLBACK CollectWindow(HWND Window, LPARAM Param)
{
    THREAD_WINDOWS *List = (THREAD_WINDOWS *)Param;

    if (GetWindowThreadProcessId(Window, NULL) == GetCurrentThreadId() && List->Count < ARRAYSIZE(List->Windows))
        List->Windows[List->Count++] = Window;
    return TRUE;
}

static UINT Position(HWND Window)
{
    THREAD_WINDOWS List = {{0}, 0};
    UINT Index;

    EnumWindows(CollectWindow, (LPARAM)&List);
    for (Index = 0; Index < List.Count; ++Index)
    {
        if (List.Windows[Index] == Window)
            return Index;
    }
    return ~0u;
}

static HWND FindUiWindow(void)
{
    THREAD_WINDOWS List = {{0}, 0};
    WCHAR Class[32];
    UINT Index;

    EnumWindows(CollectWindow, (LPARAM)&List);
    for (Index = 0; Index < List.Count; ++Index)
    {
        if (GetClassNameW(List.Windows[Index], Class, ARRAYSIZE(Class)) && !wcscmp(Class, L"MSCTFIME UI"))
            return List.Windows[Index];
    }
    return NULL;
}

static void CheckBelow(const char *Step, HWND Upper, HWND Lower, const char *What)
{
    UINT Up = Position(Upper), Down = Position(Lower);

    if (!Upper || !Lower)
        return;
    ok(Up != ~0u && Down != ~0u && Up < Down, "%s: %s at %u, expected below %u\n", Step, What, Down, Up);
}

static void CheckImeBelow(const char *Step, HWND Owner, HWND Ime)
{
    CheckBelow(Step, Owner, Ime, "default IME window");
    CheckBelow(Step, Owner, FindUiWindow(), "IME UI window");
}

static DWORD WINAPI VisibleFirst(PVOID Param)
{
    HWND Owner, Hidden, Visible, Ime;

    Owner = CreateWindowW(L"static", L"owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    Ime = ImmGetDefaultIMEWnd(Owner);
    ok(Position(Owner) == 0, "visible first window at %u\n", Position(Owner));
    CheckImeBelow("first window", Owner, Ime);
    Hidden = CreateWindowW(L"static", L"hidden owned", WS_POPUP, 0, 0, 50, 50, Owner, NULL, NULL, NULL);
    CheckBelow("hidden owned popup created", Hidden, Owner, "owner");
    CheckImeBelow("hidden owned popup created", Owner, Ime);
    SetWindowPos(Owner, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    CheckBelow("owner raised", Hidden, Owner, "owner");
    CheckImeBelow("owner raised", Owner, Ime);
    Visible = CreateWindowW(L"static", L"visible owned", WS_POPUP | WS_VISIBLE, 0, 0, 50, 50, Owner, NULL, NULL, NULL);
    ok(Position(Visible) == 0, "visible owned popup at %u\n", Position(Visible));
    CheckBelow("visible owned popup created", Visible, Hidden, "hidden owned popup");
    CheckBelow("visible owned popup created", Hidden, Owner, "owner");
    CheckImeBelow("visible owned popup created", Owner, Ime);
    ShowWindow(Hidden, SW_SHOWNA);
    ok(Position(Hidden) == 0, "shown owned popup at %u\n", Position(Hidden));
    CheckImeBelow("hidden owned popup shown", Owner, Ime);
    SetWindowPos(Owner, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    CheckBelow("owner raised again", Visible, Owner, "owner");
    CheckImeBelow("owner raised again", Owner, Ime);
    DestroyWindow(Visible);
    DestroyWindow(Hidden);
    DestroyWindow(Owner);
    return 0;
}

static DWORD WINAPI HiddenFirst(PVOID Param)
{
    HWND Owner, Ime;

    Owner = CreateWindowW(L"static", L"owner", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    Ime = ImmGetDefaultIMEWnd(Owner);
    ok(Position(Owner) == 0, "hidden first window at %u\n", Position(Owner));
    CheckBelow("hidden first window", Owner, Ime, "default IME window");
    ShowWindow(Owner, SW_SHOW);
    ok(Position(Owner) == 0, "shown first window at %u\n", Position(Owner));
    CheckImeBelow("first window shown", Owner, Ime);
    DestroyWindow(Owner);
    return 0;
}

static DWORD WINAPI ImeLike(PVOID Param)
{
    WNDCLASSW Class = {CS_IME, DefWindowProcW, 0, 0, GetModuleHandleW(NULL), NULL, NULL, NULL, NULL, L"OwnedZOrderImeUi"};
    HWND Owner, Ui, Ime;

    RegisterClassW(&Class);
    Owner = CreateWindowW(L"static", L"owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    Ime = ImmGetDefaultIMEWnd(Owner);
    Ui = CreateWindowW(Class.lpszClassName, L"ime ui", WS_POPUP, 0, 0, 50, 50, Owner, NULL, NULL, NULL);
    ok(Position(Owner) == 0, "owner of an IME-like popup at %u\n", Position(Owner));
    ok(Position(Ui) == 1, "IME-like popup at %u, expected directly below its owner\n", Position(Ui));
    CheckImeBelow("IME-like popup created", Owner, Ime);
    ShowWindow(Ui, SW_SHOWNA);
    ok(Position(Owner) == 0 && Position(Ui) == 1, "shown IME-like popup at %u, owner at %u\n", Position(Ui), Position(Owner));
    SetWindowPos(Owner, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ok(Position(Owner) == 0 && Position(Ui) == 1, "IME-like popup at %u after owner raise, owner at %u\n", Position(Ui), Position(Owner));
    CheckImeBelow("owner of an IME-like popup raised", Owner, Ime);
    DestroyWindow(Ui);
    DestroyWindow(Owner);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
    return 0;
}

static void RunThread(LPTHREAD_START_ROUTINE Routine)
{
    HANDLE Thread = CreateThread(NULL, 0, Routine, NULL, 0, NULL);

    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        return;
    ok(WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0, "thread did not finish\n");
    CloseHandle(Thread);
}

START_TEST(OwnedZOrder)
{
    RunThread(VisibleFirst);
    RunThread(HiddenFirst);
    RunThread(ImeLike);
}
