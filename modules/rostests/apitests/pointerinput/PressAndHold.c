/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Records what a classic window receives for a touch press and hold
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>

typedef BOOL (WINAPI *PFN_INITIALIZE_TOUCH_INJECTION)(UINT32, DWORD);
typedef BOOL (WINAPI *PFN_INJECT_TOUCH_INPUT)(UINT32, const POINTER_TOUCH_INFO *);

#define HOLD_FEEDBACK_NONE 0x3

static DWORD StartTick;
static HWND TestWindow;
static BOOL MoveLogged;

static LRESULT CALLBACK
HoldWndProc(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    DWORD Elapsed = GetTickCount() - StartTick;

    switch (Msg)
    {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_LBUTTONDBLCLK:
            trace("  %5lu ms msg 0x%04x wParam 0x%Ix pos (%d,%d) extra 0x%Ix\n", Elapsed, Msg, wParam,
                  (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam), (size_t)GetMessageExtraInfo());
            break;
        case WM_MOUSEMOVE:
            if (!MoveLogged)
            {
                MoveLogged = TRUE;
                trace("  %5lu ms first WM_MOUSEMOVE wParam 0x%Ix pos (%d,%d)\n", Elapsed, wParam,
                      (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam));
            }
            break;
        case WM_CONTEXTMENU:
            trace("  %5lu ms WM_CONTEXTMENU hwnd %s pos (%d,%d)\n", Elapsed, (HWND)wParam == hwnd ? "self" : "other",
                  (SHORT)LOWORD(lParam), (SHORT)HIWORD(lParam));
            break;
        case WM_POINTERDOWN:
        case WM_POINTERUP:
            trace("  %5lu ms pointer 0x%04x flags 0x%04x\n", Elapsed, Msg, HIWORD(wParam));
            break;
    }
    return DefWindowProcW(hwnd, Msg, wParam, lParam);
}

static void
Pump(DWORD Milliseconds)
{
    DWORD Until = GetTickCount() + Milliseconds;
    MSG Msg;

    do
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
        }
        Sleep(5);
    } while ((LONG)(Until - GetTickCount()) > 0);
}

static BOOL
Inject(PFN_INJECT_TOUCH_INPUT InjectTouchInputPtr, POINT Pt, POINTER_FLAGS Flags)
{
    POINTER_TOUCH_INFO Info;

    ZeroMemory(&Info, sizeof(Info));
    Info.pointerInfo.pointerType = PT_TOUCH;
    Info.pointerInfo.pointerId = 0;
    Info.pointerInfo.ptPixelLocation = Pt;
    Info.pointerInfo.pointerFlags = Flags;
    Info.touchFlags = TOUCH_FLAG_NONE;
    Info.touchMask = TOUCH_MASK_CONTACTAREA;
    Info.rcContact.left = Pt.x - 2;
    Info.rcContact.right = Pt.x + 2;
    Info.rcContact.top = Pt.y - 2;
    Info.rcContact.bottom = Pt.y + 2;
    return InjectTouchInputPtr(1, &Info);
}

static void
Hold(PFN_INJECT_TOUCH_INPUT InjectTouchInputPtr, POINT Start, DWORD HoldMs, LONG Drift, DWORD DriftMs)
{
    DWORD Begin;
    POINT Pt = Start;

    if (!DriftMs)
        DriftMs = HoldMs;
    MoveLogged = TRUE;
    trace("hold %lu ms drift %ld px over %lu ms\n", HoldMs, Drift, DriftMs);
    StartTick = GetTickCount();
    if (!Inject(InjectTouchInputPtr, Pt, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT))
    {
        trace("  down failed %lu\n", GetLastError());
        return;
    }
    Begin = GetTickCount();
    while (GetTickCount() - Begin < HoldMs)
    {
        Pump(16);
        Pt.x = Start.x + (LONG)((Drift * (LONG)min(GetTickCount() - Begin, DriftMs)) / (LONG)max(DriftMs, 1));
        if (Pt.x != Start.x)
            MoveLogged = MoveLogged && Pt.x == Start.x;
        Inject(InjectTouchInputPtr, Pt, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT);
    }
    trace("  %5lu ms up\n", GetTickCount() - StartTick);
    Inject(InjectTouchInputPtr, Pt, POINTER_FLAG_UP);
    Pump(1500);
}

START_TEST(PressAndHold)
{
    static const DWORD Holds[] = { 450, 500, 550, 600, 650 };
    static const LONG Drifts[] = { 9, 10, 11, 12 };
    PFN_INITIALIZE_TOUCH_INJECTION InitializeTouchInjectionPtr;
    PFN_INJECT_TOUCH_INPUT InjectTouchInputPtr;
    HMODULE User32 = GetModuleHandleW(L"user32.dll");
    WNDCLASSW Class = { 0 };
    POINT Center;
    RECT Rect;
    UINT Index;

    winetest_mute_threshold = 0x7fffffff;
    InitializeTouchInjectionPtr = (PFN_INITIALIZE_TOUCH_INJECTION)GetProcAddress(User32, "InitializeTouchInjection");
    InjectTouchInputPtr = (PFN_INJECT_TOUCH_INPUT)GetProcAddress(User32, "InjectTouchInput");
    if (!InitializeTouchInjectionPtr || !InjectTouchInputPtr)
    {
        skip("touch injection is not available\n");
        return;
    }
    if (!InitializeTouchInjectionPtr(1, HOLD_FEEDBACK_NONE))
    {
        skip("InitializeTouchInjection failed %lu\n", GetLastError());
        return;
    }

    Class.lpfnWndProc = HoldWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"PressAndHoldTest";
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.style = CS_DBLCLKS;
    RegisterClassW(&Class);
    TestWindow = CreateWindowExW(WS_EX_TOPMOST, L"PressAndHoldTest", L"Press and hold", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 100, 100, 600, 400, NULL, NULL, Class.hInstance, NULL);
    ok(TestWindow != NULL, "CreateWindowEx failed %lu\n", GetLastError());
    if (!TestWindow)
        return;
    SetForegroundWindow(TestWindow);
    Pump(1000);
    GetClientRect(TestWindow, &Rect);
    Center.x = (Rect.right - Rect.left) / 2;
    Center.y = (Rect.bottom - Rect.top) / 2;
    ClientToScreen(TestWindow, &Center);

    for (Index = 0; Index < ARRAYSIZE(Holds); Index++)
        Hold(InjectTouchInputPtr, Center, Holds[Index], 0, 0);
    for (Index = 0; Index < ARRAYSIZE(Drifts); Index++)
        Hold(InjectTouchInputPtr, Center, 1500, Drifts[Index], 1000);
    Hold(InjectTouchInputPtr, Center, 1500, 60, 200);
    Hold(InjectTouchInputPtr, Center, 400, 60, 200);

    DestroyWindow(TestWindow);
}
