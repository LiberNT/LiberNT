/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the z-order, topmost state and messages of SetWindowPos with owner groups
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define NA (SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)

typedef struct _POS_MSG
{
    HWND hWnd;
    UINT Msg;
    UINT Flags;
} POS_MSG;

static HWND hwndOwner, hwndOwned, hwndPlain, hwndTop1, hwndTop2;
static POS_MSG Log[64];
static UINT LogCount;
static BOOL Logging;

static LRESULT CALLBACK
ZOrderWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (Logging && (Msg == WM_WINDOWPOSCHANGING || Msg == WM_WINDOWPOSCHANGED) && LogCount < _countof(Log))
    {
        Log[LogCount].hWnd = hWnd;
        Log[LogCount].Msg = Msg;
        Log[LogCount].Flags = ((WINDOWPOS *)lParam)->flags;
        LogCount++;
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

static UINT
CountMsg(HWND hWnd, UINT Msg)
{
    UINT i, Count = 0;

    for (i = 0; i < LogCount; i++)
    {
        if (Log[i].hWnd == hWnd && Log[i].Msg == Msg)
            Count++;
    }
    return Count;
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

static BOOL
IsBandValid(void)
{
    HWND hWnd;
    BOOL bNonTopmost = FALSE;

    for (hWnd = GetTopWindow(NULL); hWnd; hWnd = GetWindow(hWnd, GW_HWNDNEXT))
    {
        if (GetWindowThreadProcessId(hWnd, NULL) != GetCurrentThreadId())
            continue;
        if (!IsTopmost(hWnd))
            bNonTopmost = TRUE;
        else if (bNonTopmost)
            return FALSE;
    }
    return TRUE;
}

static void
DestroyTestWindows(void)
{
    HWND *Windows[] = { &hwndTop2, &hwndTop1, &hwndPlain, &hwndOwned, &hwndOwner };
    UINT i;

    for (i = 0; i < _countof(Windows); i++)
    {
        if (*Windows[i])
            DestroyWindow(*Windows[i]);
        *Windows[i] = NULL;
    }
    FlushMessages();
}

static HWND
CreateTestWindow(DWORD ExStyle, DWORD Visible, HWND hwndParent, int x)
{
    return CreateWindowExW(ExStyle, L"SetWindowPosZOrder", L"test", WS_OVERLAPPEDWINDOW | Visible,
                           x, 40 + x / 4, 150, 150, hwndParent, NULL, NULL, NULL);
}

static BOOL
Setup(DWORD PlainVisible)
{
    DestroyTestWindows();
    hwndOwner = CreateTestWindow(0, WS_VISIBLE, NULL, 10);
    hwndOwned = CreateTestWindow(0, WS_VISIBLE, hwndOwner, 60);
    hwndPlain = CreateTestWindow(0, PlainVisible, NULL, 110);
    hwndTop1 = CreateTestWindow(WS_EX_TOPMOST, WS_VISIBLE, NULL, 400);
    hwndTop2 = CreateTestWindow(WS_EX_TOPMOST, WS_VISIBLE, NULL, 450);
    if (!hwndOwner || !hwndOwned || !hwndPlain || !hwndTop1 || !hwndTop2)
    {
        skip("CreateWindowEx failed\n");
        return FALSE;
    }
    SetWindowPos(hwndOwner, HWND_TOP, 0, 0, 0, 0, NA | SWP_NOOWNERZORDER);
    SetWindowPos(hwndOwned, HWND_TOP, 0, 0, 0, 0, NA | SWP_NOOWNERZORDER);
    SetWindowPos(hwndPlain, HWND_TOP, 0, 0, 0, 0, NA | SWP_NOOWNERZORDER);
    SetWindowPos(hwndTop1, HWND_TOPMOST, 0, 0, 0, 0, NA);
    SetWindowPos(hwndTop2, HWND_TOPMOST, 0, 0, 0, 0, NA);
    FlushMessages();
    return TRUE;
}

static BOOL
Move(HWND hWnd, HWND hwndAfter, UINT Flags)
{
    BOOL Ret;

    FlushMessages();
    LogCount = 0;
    Logging = TRUE;
    Ret = SetWindowPos(hWnd, hwndAfter, 0, 0, 0, 0, Flags);
    FlushMessages();
    Logging = FALSE;
    return Ret;
}

static void
Test_InsertAfterSelf(void)
{
    RECT Before, After;
    HDWP hdwp;

    if (!Setup(WS_VISIBLE))
        return;
    GetWindowRect(hwndPlain, &Before);

    LogCount = 0;
    Logging = TRUE;
    ok(SetWindowPos(hwndPlain, hwndPlain, 70, 70, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE), "SetWindowPos failed\n");
    FlushMessages();
    Logging = FALSE;
    GetWindowRect(hwndPlain, &After);
    ok(LogCount == 0, "SetWindowPos after itself sent %u messages\n", LogCount);
    ok(EqualRect(&Before, &After), "SetWindowPos after itself moved the window\n");

    ok(Move(hwndOwner, hwndOwner, NA), "SetWindowPos failed\n");
    ok(LogCount == 0, "SetWindowPos of an owner after itself sent %u messages\n", LogCount);

    LogCount = 0;
    Logging = TRUE;
    hdwp = BeginDeferWindowPos(1);
    hdwp = DeferWindowPos(hdwp, hwndPlain, hwndPlain, 80, 80, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    ok(hdwp != NULL, "DeferWindowPos failed\n");
    ok(EndDeferWindowPos(hdwp), "EndDeferWindowPos failed\n");
    FlushMessages();
    Logging = FALSE;
    GetWindowRect(hwndPlain, &After);
    ok(LogCount == 0, "DeferWindowPos after itself sent %u messages\n", LogCount);
    ok(EqualRect(&Before, &After), "DeferWindowPos after itself moved the window\n");
}

static void
Test_InsertAfterTopmost(void)
{
    if (!Setup(WS_VISIBLE))
        return;
    ok(Move(hwndPlain, hwndTop1, NA), "SetWindowPos failed\n");
    ok(IsTopmost(hwndPlain), "the window inserted after a topmost window is not topmost\n");
    ok(GetWindow(hwndPlain, GW_HWNDPREV) == hwndTop1, "the window is not after the topmost window\n");
    ok(IsBandValid(), "the topmost band is broken\n");

    if (!Setup(WS_VISIBLE))
        return;
    ok(Move(hwndOwner, hwndTop1, NA), "SetWindowPos failed\n");
    ok(IsTopmost(hwndOwner), "the owner inserted after a topmost window is not topmost\n");
    ok(IsTopmost(hwndOwned), "the owned window of the owner is not topmost\n");
    ok(GetWindow(hwndOwned, GW_HWNDPREV) == hwndTop1, "the owned window is not after the topmost window\n");
    ok(GetWindow(hwndOwner, GW_HWNDPREV) == hwndOwned, "the owner is not after its owned window\n");
    ok(IsBandValid(), "the topmost band is broken\n");

    if (!Setup(WS_VISIBLE))
        return;
    ok(Move(hwndOwned, hwndTop1, NA), "SetWindowPos failed\n");
    ok(IsTopmost(hwndOwned), "the owned window inserted after a topmost window is not topmost\n");
    ok(GetWindow(hwndOwned, GW_HWNDPREV) == hwndTop1, "the owned window is not after the topmost window\n");
    ok(!IsTopmost(hwndOwner), "the owner became topmost\n");
    ok(IsAbove(hwndOwner, hwndPlain), "the owner did not move to the top of the non-topmost band\n");
    ok(CountMsg(hwndOwner, WM_WINDOWPOSCHANGED) == 1, "the owner got %u WM_WINDOWPOSCHANGED\n",
       CountMsg(hwndOwner, WM_WINDOWPOSCHANGED));
    ok(IsBandValid(), "the topmost band is broken\n");

    if (!Setup(WS_VISIBLE))
        return;
    ok(Move(hwndOwned, hwndPlain, NA), "SetWindowPos failed\n");
    ok(CountMsg(hwndOwner, WM_WINDOWPOSCHANGING) == 1, "the owner got %u WM_WINDOWPOSCHANGING\n",
       CountMsg(hwndOwner, WM_WINDOWPOSCHANGING));
}

static void
Test_NoTopmostGroup(void)
{
    HWND hwndOwnedTop;

    if (!Setup(WS_VISIBLE))
        return;
    hwndOwnedTop = CreateTestWindow(WS_EX_TOPMOST, WS_VISIBLE, hwndTop2, 500);
    ok(hwndOwnedTop != NULL, "CreateWindowEx failed\n");
    SetWindowPos(hwndOwnedTop, HWND_TOPMOST, 0, 0, 0, 0, NA);
    ok(Move(hwndTop2, HWND_NOTOPMOST, NA), "SetWindowPos failed\n");
    ok(!IsTopmost(hwndTop2), "the owner is topmost\n");
    ok(!IsTopmost(hwndOwnedTop), "the owned window is topmost\n");
    ok(GetWindow(hwndTop2, GW_HWNDPREV) == hwndOwnedTop, "the owner is not after its owned window\n");
    ok(IsAbove(hwndTop1, hwndOwnedTop), "the group is above a topmost window\n");
    ok(IsBandValid(), "the topmost band is broken\n");
    DestroyWindow(hwndOwnedTop);

    if (!Setup(WS_VISIBLE))
        return;
    ok(Move(hwndOwner, HWND_NOTOPMOST, NA), "SetWindowPos failed\n");
    ok(CountMsg(hwndOwned, WM_WINDOWPOSCHANGING) == 1, "the owned window got %u WM_WINDOWPOSCHANGING\n",
       CountMsg(hwndOwned, WM_WINDOWPOSCHANGING));
    ok(CountMsg(hwndOwner, WM_WINDOWPOSCHANGING) == 1, "the owner got %u WM_WINDOWPOSCHANGING\n",
       CountMsg(hwndOwner, WM_WINDOWPOSCHANGING));
    ok(CountMsg(hwndOwned, WM_WINDOWPOSCHANGED) + CountMsg(hwndOwner, WM_WINDOWPOSCHANGED) == 0,
       "the group got WM_WINDOWPOSCHANGED\n");
    ok(GetWindow(hwndOwned, GW_HWNDPREV) == hwndPlain, "the owned window moved\n");

    if (!Setup(WS_VISIBLE))
        return;
    SetWindowPos(hwndOwner, HWND_TOP, 0, 0, 0, 0, NA | SWP_NOOWNERZORDER);
    ok(Move(hwndOwner, HWND_NOTOPMOST, NA), "SetWindowPos failed\n");
    ok(GetWindow(hwndOwner, GW_HWNDPREV) == hwndOwned, "the owner is not after its owned window\n");
    ok(IsAbove(hwndOwner, hwndPlain), "the owner moved below the other window\n");
    ok(CountMsg(hwndOwned, WM_WINDOWPOSCHANGED) == 1, "the owned window got %u WM_WINDOWPOSCHANGED\n",
       CountMsg(hwndOwned, WM_WINDOWPOSCHANGED));
    ok(CountMsg(hwndOwner, WM_WINDOWPOSCHANGED) == 0, "the owner got %u WM_WINDOWPOSCHANGED\n",
       CountMsg(hwndOwner, WM_WINDOWPOSCHANGED));

    if (!Setup(WS_VISIBLE))
        return;
    hwndOwnedTop = CreateTestWindow(0, 0, hwndOwner, 160);
    ok(hwndOwnedTop != NULL, "CreateWindowEx failed\n");
    SetWindowPos(hwndOwnedTop, hwndOwned, 0, 0, 0, 0, NA | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    ok(Move(hwndOwnedTop, HWND_NOTOPMOST, NA), "SetWindowPos failed\n");
    ok(GetWindow(hwndOwnedTop, GW_HWNDPREV) == hwndOwned, "the second owned window moved\n");
    ok(IsAbove(hwndOwnedTop, hwndOwner), "the second owned window is below the owner\n");
    DestroyWindow(hwndOwnedTop);
}

static void
Test_HiddenChanged(void)
{
    if (!Setup(0))
        return;
    SetWindowPos(hwndPlain, HWND_BOTTOM, 0, 0, 0, 0, NA);
    ok(Move(hwndPlain, HWND_TOP, NA), "SetWindowPos failed\n");
    ok(IsAbove(hwndPlain, hwndOwned), "the hidden window did not move\n");
    ok(CountMsg(hwndPlain, WM_WINDOWPOSCHANGED) == 1, "HWND_TOP: got %u WM_WINDOWPOSCHANGED\n",
       CountMsg(hwndPlain, WM_WINDOWPOSCHANGED));

    ok(Move(hwndPlain, HWND_TOP, NA), "SetWindowPos failed\n");
    ok(CountMsg(hwndPlain, WM_WINDOWPOSCHANGED) == 0, "HWND_TOP again: got %u WM_WINDOWPOSCHANGED\n",
       CountMsg(hwndPlain, WM_WINDOWPOSCHANGED));

    ok(Move(hwndPlain, hwndOwner, NA), "SetWindowPos failed\n");
    ok(GetWindow(hwndPlain, GW_HWNDPREV) == hwndOwner, "the hidden window did not move\n");
    ok(CountMsg(hwndPlain, WM_WINDOWPOSCHANGED) == 1, "after a window: got %u WM_WINDOWPOSCHANGED\n",
       CountMsg(hwndPlain, WM_WINDOWPOSCHANGED));

    ok(Move(hwndPlain, HWND_TOP, SWP_NOMOVE | SWP_NOSIZE), "SetWindowPos failed\n");
    ok(GetActiveWindow() == hwndPlain, "the hidden window is not active\n");
    ok(CountMsg(hwndPlain, WM_WINDOWPOSCHANGED) == 1, "activated: got %u WM_WINDOWPOSCHANGED\n",
       CountMsg(hwndPlain, WM_WINDOWPOSCHANGED));
}

static void
Test_ActivateNoZOrder(void)
{
    if (!Setup(WS_VISIBLE))
        return;
    SetWindowPos(hwndPlain, HWND_BOTTOM, 0, 0, 0, 0, NA);
    SetForegroundWindow(hwndOwner);
    FlushMessages();
    ok(GetForegroundWindow() == hwndOwner, "the owner is not the foreground window\n");
    ok(Move(hwndPlain, HWND_BOTTOM, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER), "SetWindowPos failed\n");
    ok(LogCount >= 1 && Log[0].hWnd == hwndPlain && Log[0].Msg == WM_WINDOWPOSCHANGING,
       "the first message is not WM_WINDOWPOSCHANGING for the window\n");
    ok(LogCount >= 1 && (Log[0].Flags & SWP_NOZORDER), "the request lost SWP_NOZORDER: flags %x\n",
       LogCount ? Log[0].Flags : 0);
    ok(CountMsg(hwndPlain, WM_WINDOWPOSCHANGING) == 2, "got %u WM_WINDOWPOSCHANGING\n",
       CountMsg(hwndPlain, WM_WINDOWPOSCHANGING));
    ok(GetActiveWindow() == hwndPlain, "the window is not active\n");
    ok(IsAbove(hwndPlain, hwndOwned), "the window was not raised\n");
}

START_TEST(SetWindowPosZOrder)
{
    RegisterSimpleClass(ZOrderWndProc, L"SetWindowPosZOrder");

    Test_InsertAfterSelf();
    Test_InsertAfterTopmost();
    Test_NoTopmostGroup();
    Test_HiddenChanged();
    Test_ActivateNoZOrder();
    DestroyTestWindows();
}
