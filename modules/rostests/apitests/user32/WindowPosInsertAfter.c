/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the hwndInsertAfter that WM_WINDOWPOSCHANGING and WM_WINDOWPOSCHANGED report
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

static HWND hwndTest, hwndOther, hwndChild;
static HWND ChangingAfter, ChangedAfter;
static BOOL GotChanging, GotChanged;
static HWND ReplaceAfter;

static LRESULT CALLBACK
InsertAfterWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (hWnd == hwndTest || hWnd == hwndChild)
    {
        WINDOWPOS *Pos = (WINDOWPOS *)lParam;

        if (Msg == WM_WINDOWPOSCHANGING)
        {
            GotChanging = TRUE;
            ChangingAfter = Pos->hwndInsertAfter;
            if (ReplaceAfter)
                Pos->hwndInsertAfter = ReplaceAfter;
        }
        else if (Msg == WM_WINDOWPOSCHANGED)
        {
            GotChanged = TRUE;
            ChangedAfter = Pos->hwndInsertAfter;
        }
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static void
FlushMessages(void)
{
    MSG Msg;

    while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&Msg);
}

static BOOL
IsTopmost(HWND hWnd)
{
    return (GetWindowLongPtrW(hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

static BOOL
IsAbove(HWND hwndTop, HWND hwndBelow)
{
    HWND hWnd;

    for (hWnd = GetWindow(hwndTop, GW_HWNDNEXT); hWnd; hWnd = GetWindow(hWnd, GW_HWNDNEXT))
    {
        if (hWnd == hwndBelow)
            return TRUE;
    }
    return FALSE;
}

static void
Move(HWND hWnd, HWND hwndAfter, UINT Flags)
{
    FlushMessages();
    GotChanging = GotChanged = FALSE;
    ChangingAfter = ChangedAfter = (HWND)(ULONG_PTR)0xdead;
    ok(SetWindowPos(hWnd, hwndAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | Flags),
       "SetWindowPos failed\n");
    FlushMessages();
}

static void
MakeNotTopmost(void)
{
    SetWindowPos(hwndTest, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(hwndOther, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    FlushMessages();
}

START_TEST(WindowPosInsertAfter)
{
    HWND hwndPrev;

    RegisterSimpleClass(InsertAfterWndProc, L"InsertAfterTest");
    hwndTest = CreateWindowExW(0, L"InsertAfterTest", L"test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               10, 10, 200, 200, NULL, NULL, NULL, NULL);
    hwndOther = CreateWindowExW(0, L"InsertAfterTest", L"other", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                40, 40, 200, 200, NULL, NULL, NULL, NULL);
    ok(hwndTest != NULL && hwndOther != NULL, "CreateWindowEx failed\n");
    if (!hwndTest || !hwndOther)
        return;
    hwndChild = CreateWindowExW(0, L"InsertAfterTest", L"child", WS_CHILD | WS_VISIBLE,
                                0, 0, 50, 50, hwndOther, NULL, NULL, NULL);
    CreateWindowExW(0, L"InsertAfterTest", L"child2", WS_CHILD | WS_VISIBLE,
                    10, 10, 50, 50, hwndOther, NULL, NULL, NULL);

    MakeNotTopmost();
    Move(hwndTest, HWND_TOP, 0);
    hwndPrev = GetWindow(hwndTest, GW_HWNDPREV);
    ok(hwndPrev == NULL || IsTopmost(hwndPrev), "the window above is not topmost\n");
    ok(GotChanging && ChangingAfter == hwndPrev, "HWND_TOP: changing %p, expected %p\n", ChangingAfter, hwndPrev);
    ok(GotChanged && ChangedAfter == hwndPrev, "HWND_TOP: changed %p, expected %p\n", ChangedAfter, hwndPrev);

    Move(hwndTest, HWND_TOP, 0);
    ok(GetWindow(hwndTest, GW_HWNDPREV) == hwndPrev, "the window moved\n");
    ok(GotChanging && ChangingAfter == hwndPrev, "HWND_TOP again: changing %p, expected %p\n", ChangingAfter, hwndPrev);

    Move(hwndTest, HWND_NOTOPMOST, 0);
    ok(GotChanging && ChangingAfter == hwndPrev, "HWND_NOTOPMOST: changing %p, expected %p\n", ChangingAfter, hwndPrev);

    Move(hwndTest, HWND_BOTTOM, 0);
    ok(GotChanging && ChangingAfter == HWND_BOTTOM, "HWND_BOTTOM: changing %p\n", ChangingAfter);
    ok(GotChanged && ChangedAfter == HWND_BOTTOM, "HWND_BOTTOM: changed %p\n", ChangedAfter);

    Move(hwndTest, hwndOther, 0);
    ok(GotChanging && ChangingAfter == hwndOther, "hwndOther: changing %p, expected %p\n", ChangingAfter, hwndOther);
    ok(GotChanged && ChangedAfter == hwndOther, "hwndOther: changed %p, expected %p\n", ChangedAfter, hwndOther);

    Move(hwndTest, HWND_BOTTOM, SWP_NOZORDER);
    ok(GotChanging && ChangingAfter == HWND_BOTTOM, "SWP_NOZORDER: changing %p\n", ChangingAfter);

    Move(hwndTest, HWND_TOPMOST, 0);
    ok(IsTopmost(hwndTest), "the window is not topmost\n");
    ok(GotChanging && ChangingAfter == HWND_TOP, "HWND_TOPMOST: changing %p\n", ChangingAfter);
    ok(GotChanged && ChangedAfter == HWND_TOP, "HWND_TOPMOST: changed %p\n", ChangedAfter);

    Move(hwndTest, HWND_TOP, 0);
    ok(GotChanging && ChangingAfter == HWND_TOP, "topmost HWND_TOP: changing %p\n", ChangingAfter);

    Move(hwndTest, HWND_NOTOPMOST, 0);
    hwndPrev = GetWindow(hwndTest, GW_HWNDPREV);
    ok(!IsTopmost(hwndTest), "the window is topmost\n");
    ok(hwndPrev == NULL || IsTopmost(hwndPrev), "the window above is not topmost\n");
    ok(GotChanging && ChangingAfter == hwndPrev, "topmost HWND_NOTOPMOST: changing %p, expected %p\n", ChangingAfter, hwndPrev);
    ok(GotChanged && ChangedAfter == hwndPrev, "topmost HWND_NOTOPMOST: changed %p, expected %p\n", ChangedAfter, hwndPrev);

    MakeNotTopmost();
    Move(hwndTest, HWND_TOP, 0);
    ok(IsAbove(hwndTest, hwndOther), "the window is below hwndOther\n");
    ReplaceAfter = HWND_BOTTOM;
    Move(hwndTest, HWND_TOP, 0);
    ReplaceAfter = NULL;
    ok(IsAbove(hwndOther, hwndTest), "the window did not go below hwndOther\n");
    ok(GotChanged && ChangedAfter == HWND_BOTTOM, "replaced: changed %p\n", ChangedAfter);

    Move(hwndChild, HWND_BOTTOM, 0);
    Move(hwndChild, HWND_TOP, 0);
    ok(GetWindow(hwndChild, GW_HWNDPREV) == NULL, "the child is not at the top\n");
    ok(GotChanging && ChangingAfter == HWND_TOP, "child HWND_TOP: changing %p\n", ChangingAfter);
    ok(GotChanged && ChangedAfter == HWND_TOP, "child HWND_TOP: changed %p\n", ChangedAfter);

    DestroyWindow(hwndOther);
    DestroyWindow(hwndTest);
}
