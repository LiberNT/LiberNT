/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the z-order and the enumeration order of the default IME window
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <imm.h>

typedef struct _THREAD_WINDOWS
{
    HWND Windows[16];
    UINT Count;
} THREAD_WINDOWS;

static BOOL
IsImeLike(HWND hWnd)
{
    WCHAR Class[16];

    if (GetClassLongW(hWnd, GCL_STYLE) & CS_IME)
        return TRUE;
    return GetClassNameW(hWnd, Class, ARRAYSIZE(Class)) && !wcscmp(Class, L"IME");
}

static HWND
NextNotImeLike(HWND hWnd)
{
    do
    {
        hWnd = GetWindow(hWnd, GW_HWNDNEXT);
    } while (hWnd && IsImeLike(hWnd));
    return hWnd;
}

static BOOL
IsAbove(HWND hWndTop, HWND hWndBelow)
{
    HWND hWnd;

    for (hWnd = GetWindow(hWndTop, GW_HWNDNEXT); hWnd; hWnd = GetWindow(hWnd, GW_HWNDNEXT))
    {
        if (hWnd == hWndBelow)
            return TRUE;
    }
    return FALSE;
}

static BOOL CALLBACK
CollectWindow(HWND hWnd, LPARAM Param)
{
    THREAD_WINDOWS *List = (THREAD_WINDOWS *)Param;

    if (GetWindowThreadProcessId(hWnd, NULL) == GetCurrentThreadId() && List->Count < ARRAYSIZE(List->Windows))
        List->Windows[List->Count++] = hWnd;
    return TRUE;
}

static VOID
CheckImeLast(const char *Step, const THREAD_WINDOWS *List, HWND hwndIme)
{
    UINT i;
    BOOL Found = FALSE, ImeSeen = FALSE;

    for (i = 0; i < List->Count; i++)
    {
        if (List->Windows[i] == hwndIme)
            Found = TRUE;
        if (IsImeLike(List->Windows[i]))
            ImeSeen = TRUE;
        else
            ok(!ImeSeen, "%s: window %u comes after an IME-like window\n", Step, i);
    }
    ok(Found, "%s: the default IME window is not listed\n", Step);
}

static DWORD WINAPI
TwoWindows(PVOID Parameter)
{
    THREAD_WINDOWS List = {{0}, 0};
    HWND hwndOwner, hwndOther, hwndIme;

    hwndOwner = CreateWindowW(L"static", L"owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    hwndIme = ImmGetDefaultIMEWnd(hwndOwner);
    hwndOther = CreateWindowW(L"static", L"other", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    if (!hwndIme)
    {
        skip("No default IME window\n");
        goto Cleanup;
    }

    ok(GetWindow(hwndIme, GW_OWNER) == hwndOther, "Created: the IME window is owned by %p\n", GetWindow(hwndIme, GW_OWNER));
    ok(NextNotImeLike(hwndIme) == hwndOther, "Created: the IME window is not directly above the active window\n");

    SetActiveWindow(hwndOwner);
    ok(GetWindow(hwndIme, GW_OWNER) == hwndOwner, "Activated: the IME window is owned by %p\n", GetWindow(hwndIme, GW_OWNER));
    ok(NextNotImeLike(hwndIme) == hwndOwner, "Activated: the IME window is not directly above the active window\n");
    ok(NextNotImeLike(hwndOwner) == hwndOther, "Activated: the other window is not below the active window\n");

    SetWindowPos(hwndOther, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ok(NextNotImeLike(hwndOther) == hwndOwner, "Other raised: the active window is not below the other window\n");
    ok(IsAbove(hwndOther, hwndIme), "Other raised: the IME window moved above the other window\n");
    ok(NextNotImeLike(hwndIme) == hwndOwner, "Other raised: the IME window left its owner\n");

    SetWindowPos(hwndOwner, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ok(NextNotImeLike(hwndIme) == hwndOwner, "Owner raised: the IME window did not move with its owner\n");
    ok(NextNotImeLike(hwndOwner) == hwndOther, "Owner raised: the other window is not below the owner\n");

    EnumWindows(CollectWindow, (LPARAM)&List);
    CheckImeLast("EnumWindows", &List, hwndIme);
    List.Count = 0;
    EnumThreadWindows(GetCurrentThreadId(), CollectWindow, (LPARAM)&List);
    CheckImeLast("EnumThreadWindows", &List, hwndIme);

Cleanup:
    DestroyWindow(hwndOther);
    DestroyWindow(hwndOwner);
    return 0;
}

static DWORD WINAPI
OwnedGroup(PVOID Parameter)
{
    HWND hwndOwner, hwndHidden, hwndVisible, hwndIme;

    hwndOwner = CreateWindowW(L"static", L"owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    hwndIme = ImmGetDefaultIMEWnd(hwndOwner);
    hwndHidden = CreateWindowW(L"static", L"hidden owned", WS_POPUP, 0, 0, 50, 50, hwndOwner, NULL, NULL, NULL);
    hwndVisible = CreateWindowW(L"static", L"visible owned", WS_POPUP | WS_VISIBLE, 0, 0, 50, 50, hwndOwner, NULL, NULL, NULL);
    if (!hwndIme)
    {
        skip("No default IME window\n");
        goto Cleanup;
    }

    ok(GetActiveWindow() == hwndVisible, "The visible owned window is not active\n");
    ok(NextNotImeLike(hwndIme) == hwndVisible, "Owned window active: the IME window is not directly above it\n");

    ShowWindow(hwndHidden, SW_SHOWNA);
    SetWindowPos(hwndOwner, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ok(GetActiveWindow() == hwndOwner, "The owner is not active\n");
    ok(GetWindow(hwndIme, GW_OWNER) == hwndOwner, "Owner activated: the IME window is owned by %p\n", GetWindow(hwndIme, GW_OWNER));
    ok(IsAbove(hwndIme, hwndOwner), "Owner activated: the IME window is below its owner\n");
    ok(IsAbove(hwndHidden, hwndOwner) && IsAbove(hwndVisible, hwndOwner), "Owner activated: an owned window is below the owner\n");

Cleanup:
    DestroyWindow(hwndVisible);
    DestroyWindow(hwndHidden);
    DestroyWindow(hwndOwner);
    return 0;
}

static DWORD WINAPI
ImeLikeCreated(PVOID Parameter)
{
    WNDCLASSW Class = { CS_IME, DefWindowProcW, 0, 0, GetModuleHandleW(NULL), NULL, NULL, NULL, NULL, L"ImeWindowZOrderUi" };
    HWND hwndOwner, hwndUi;

    RegisterClassW(&Class);
    hwndOwner = CreateWindowW(L"static", L"owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    hwndUi = CreateWindowW(Class.lpszClassName, L"ime ui", WS_POPUP, 0, 0, 50, 50, hwndOwner, NULL, NULL, NULL);
    ok(hwndUi != NULL, "CreateWindowW failed: %lu\n", GetLastError());
    ok(IsAbove(hwndUi, hwndOwner), "An IME-like window was created below its owner\n");
    DestroyWindow(hwndUi);
    DestroyWindow(hwndOwner);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
    return 0;
}

static VOID
RunThread(LPTHREAD_START_ROUTINE Routine)
{
    HANDLE Thread = CreateThread(NULL, 0, Routine, NULL, 0, NULL);

    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        return;
    ok(WaitForSingleObject(Thread, 10000) == WAIT_OBJECT_0, "The thread did not finish\n");
    CloseHandle(Thread);
}

START_TEST(ImeWindowZOrder)
{
    RunThread(TwoWindows);
    RunThread(OwnedGroup);
    RunThread(ImeLikeCreated);
}
