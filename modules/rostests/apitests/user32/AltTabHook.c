/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test Alt+Tab with low-level keyboard hooks and windows that handle WM_SYSCOMMAND
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"

static BOOL SwallowAlt;
static int SwallowedAltDown;
static int SwallowedAltUp;
static BOOL SwallowSysCommand;
static SHORT TabHookAsyncAlt;
static DWORD TabHookFlags;
static UINT TabMessage;
static SHORT TabKeyStateAlt;
static int SysCommandCount;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    PKBDLLHOOKSTRUCT pKbd = (PKBDLLHOOKSTRUCT)lParam;

    if (nCode == HC_ACTION && pKbd->vkCode == VK_TAB && !(wParam == WM_KEYUP || wParam == WM_SYSKEYUP))
    {
        TabHookAsyncAlt = GetAsyncKeyState(VK_MENU);
        TabHookFlags = pKbd->flags;
    }
    if (nCode == HC_ACTION && (pKbd->vkCode == VK_LMENU || pKbd->vkCode == VK_MENU))
    {
        BOOL Up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;

        if (SwallowAlt)
        {
            if (Up)
                SwallowedAltUp++;
            else
                SwallowedAltDown++;
            return 1;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static void PumpFor(DWORD Milliseconds)
{
    MSG msg;
    DWORD End = GetTickCount() + Milliseconds;

    while ((LONG)(End - GetTickCount()) > 0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjectsEx(0, NULL, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}

static BOOL IsSwitcherVisible(void)
{
    HWND hwnd = FindWindowW(L"#32771", NULL);

    return hwnd && IsWindowVisible(hwnd);
}

static void SendKey(WORD vk, DWORD flags)
{
    INPUT input = { 0 };

    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = flags;
    ok_int(SendInput(1, &input, sizeof(input)), 1);
}

static HWND CreateTopLevel(const WCHAR *Title, DWORD ExStyle, HWND hwndOwner, int x)
{
    HWND hwnd;

    hwnd = CreateWindowExW(ExStyle, L"AltTabHookClass", Title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           x, 100, 200, 200, hwndOwner, NULL, GetModuleHandleW(NULL), NULL);
    ok(hwnd != NULL, "CreateWindowEx failed for %ls\n", Title);
    return hwnd;
}

static DWORD WINAPI SendAltTab(PVOID Param)
{
    SendKey(VK_LMENU, 0);
    Sleep(100);
    SendKey(VK_TAB, 0);
    Sleep(100);
    SendKey(VK_TAB, KEYEVENTF_KEYUP);
    Sleep(100);
    SendKey(VK_LMENU, KEYEVENTF_KEYUP);
    return 0;
}

static void RunAltTab(const char *Name, BOOL Swallow, BOOL ExpectSwitch, UINT_PTR TimerWindowTick)
{
    HWND hwndActive;
    DWORD Start;
    BOOL Gone;
    HANDLE hThread;

    SwallowAlt = Swallow;
    SwallowedAltDown = 0;
    SwallowedAltUp = 0;
    TabHookAsyncAlt = TabKeyStateAlt = 0;
    TabHookFlags = TabMessage = 0;
    SysCommandCount = 0;

    hwndActive = GetForegroundWindow();
    hThread = CreateThread(NULL, 0, SendAltTab, NULL, 0, NULL);
    ok(hThread != NULL, "CreateThread failed %lu\n", GetLastError());
    while (hThread && WaitForSingleObject(hThread, 0) == WAIT_TIMEOUT)
        PumpFor(20);
    if (hThread)
        CloseHandle(hThread);

    Start = GetTickCount();
    do
    {
        PumpFor(20);
        Gone = !IsSwitcherVisible();
    } while (!Gone && GetTickCount() - Start < 1500);

    trace("%s: swallowed down %d up %d, switcher gone %d after %lu ms, timer ticks %Iu\n",
          Name, SwallowedAltDown, SwallowedAltUp, Gone, GetTickCount() - Start, TimerWindowTick);
    ok(Gone, "%s: switcher still shown 1.5 s after Alt was released\n", Name);
    ok(GetCapture() == NULL, "%s: capture %p left after Alt+Tab\n", Name, GetCapture());
    ok((GetAsyncKeyState(VK_MENU) & 0x8000) == 0, "%s: Alt still down\n", Name);
    ok(SysCommandCount == 0, "%s: the window got %d SC_NEXTWINDOW/SC_PREVWINDOW\n", Name, SysCommandCount);
    if (Swallow)
    {
        ok(SwallowedAltDown >= 1, "%s: hook saw no Alt down\n", Name);
        ok(SwallowedAltUp >= 1, "%s: hook saw no Alt up\n", Name);
        ok(TabHookFlags & LLKHF_ALTDOWN, "%s: hook flags 0x%lx for Tab\n", Name, TabHookFlags);
        ok((TabHookAsyncAlt & 0x8000) == 0, "%s: swallowed Alt is down in the async state\n", Name);
        ok(TabMessage == WM_KEYDOWN, "%s: Tab reached the window as 0x%x\n", Name, TabMessage);
        ok((TabKeyStateAlt & 0x8000) == 0, "%s: swallowed Alt is down in the window's key state\n", Name);
    }
    else
    {
        ok(TabMessage == 0, "%s: Tab reached the window as 0x%x\n", Name, TabMessage);
    }
    if (ExpectSwitch)
        ok(GetForegroundWindow() != hwndActive, "%s: foreground still %p after Alt+Tab\n", Name, hwndActive);
    else
        ok(GetForegroundWindow() == hwndActive, "%s: foreground moved from %p to %p\n", Name, hwndActive, GetForegroundWindow());
}

static UINT_PTR TimerTicks;

static VOID CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    TimerTicks++;
    InvalidateRect(hwnd, NULL, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if ((uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) && wParam == VK_TAB)
    {
        TabMessage = uMsg;
        TabKeyStateAlt = GetKeyState(VK_MENU);
    }
    if (uMsg == WM_SYSCOMMAND &&
        ((wParam & 0xfff0) == SC_NEXTWINDOW || (wParam & 0xfff0) == SC_PREVWINDOW))
        SysCommandCount++;
    if (SwallowSysCommand && uMsg == WM_SYSCOMMAND &&
        ((wParam & 0xfff0) == SC_NEXTWINDOW || (wParam & 0xfff0) == SC_PREVWINDOW))
        return 0;
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

START_TEST(AltTabHook)
{
    WNDCLASSW wc = { 0 };
    HWND hwndMain, hwndOwned, hwndTool, hwndOther;
    HHOOK hHook;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"AltTabHookClass";
    RegisterClassW(&wc);

    hwndMain = CreateTopLevel(L"AltTabHook main", 0, NULL, 50);
    hwndOwned = CreateTopLevel(L"AltTabHook owned", 0, hwndMain, 150);
    hwndTool = CreateTopLevel(L"AltTabHook tool", WS_EX_TOOLWINDOW, NULL, 250);
    hwndOther = CreateTopLevel(L"AltTabHook other", 0, NULL, 350);
    SetForegroundWindow(hwndMain);
    PumpFor(200);

    RunAltTab("no hook", FALSE, TRUE, TimerTicks);

    hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(NULL), 0);
    ok(hHook != NULL, "SetWindowsHookEx failed %lu\n", GetLastError());

    SetForegroundWindow(hwndMain);
    PumpFor(200);
    RunAltTab("hook swallows Alt", TRUE, FALSE, TimerTicks);

    SetTimer(hwndMain, 1, 5, TimerProc);
    SetTimer(hwndOther, 1, 5, TimerProc);
    SetForegroundWindow(hwndMain);
    PumpFor(200);
    RunAltTab("hook swallows Alt, busy queue", TRUE, FALSE, TimerTicks);
    KillTimer(hwndMain, 1);
    KillTimer(hwndOther, 1);

    SwallowSysCommand = TRUE;
    SetForegroundWindow(hwndMain);
    PumpFor(200);
    RunAltTab("window swallows SC_NEXTWINDOW", FALSE, TRUE, TimerTicks);
    SwallowSysCommand = FALSE;

    UnhookWindowsHookEx(hHook);
    DestroyWindow(hwndOther);
    DestroyWindow(hwndTool);
    DestroyWindow(hwndOwned);
    DestroyWindow(hwndMain);
    UnregisterClassW(L"AltTabHookClass", GetModuleHandleW(NULL));
}
