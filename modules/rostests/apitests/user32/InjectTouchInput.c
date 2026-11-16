/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Injected touch contacts tap, double tap and pan a list view with one or two fingers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <commctrl.h>
#include <strsafe.h>

#define ROWS 300
#define SLIDE_STEPS 12
#define SLIDE_STEP_PIXELS 25
#define DOUBLE_TAP_TOLERANCE 17

static LONG Activations;
static LONG LastActivated;

static
LRESULT
CALLBACK
ParentProc(
    _In_ HWND hWnd,
    _In_ UINT Msg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    if (Msg == WM_NOTIFY && ((NMHDR *)lParam)->code == LVN_ITEMACTIVATE)
    {
        Activations++;
        LastActivated = ((NMITEMACTIVATE *)lParam)->iItem;
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static
VOID
Pump(
    _In_ DWORD Milliseconds)
{
    DWORD End = GetTickCount() + Milliseconds;
    MSG Msg;

    for (;;)
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
        }
        if ((LONG)(GetTickCount() - End) >= 0)
            break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
}

static
VOID
Contact(
    _Out_ POINTER_TOUCH_INFO *Touch,
    _In_ UINT32 Id,
    _In_ LONG X,
    _In_ LONG Y,
    _In_ POINTER_FLAGS Flags)
{
    ZeroMemory(Touch, sizeof(*Touch));
    Touch->pointerInfo.pointerType = PT_TOUCH;
    Touch->pointerInfo.pointerId = Id;
    Touch->pointerInfo.ptPixelLocation.x = X;
    Touch->pointerInfo.ptPixelLocation.y = Y;
    Touch->pointerInfo.pointerFlags = Flags;
    Touch->touchFlags = TOUCH_FLAG_NONE;
    Touch->touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
    Touch->orientation = 90;
    Touch->pressure = 32000;
    SetRect(&Touch->rcContact, X - 2, Y - 2, X + 2, Y + 2);
}

static
BOOL
Inject(
    _In_ UINT32 Fingers,
    _In_ LONG X,
    _In_ LONG Y,
    _In_ POINTER_FLAGS Flags)
{
    POINTER_TOUCH_INFO Touch[2];
    UINT32 i;

    for (i = 0; i < Fingers; i++)
        Contact(&Touch[i], i, X + (LONG)i * 60, Y, Flags);
    return InjectTouchInput(Fingers, Touch);
}

static
VOID
Slide(
    _In_ HWND List,
    _In_ UINT32 Fingers)
{
    RECT Rect;
    LONG X, Y;
    int Step, Rows;
    LRESULT ItemHeight;

    SendMessageW(List, WM_VSCROLL, SB_TOP, 0);
    SendMessageW(List, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&(LVITEMW){ .stateMask = LVIS_SELECTED });
    Pump(150);
    GetWindowRect(List, &Rect);
    X = (Rect.left + Rect.right) / 2 - 20;
    Y = Rect.bottom - 60;
    ItemHeight = HIWORD(SendMessageW(List, LVM_GETITEMSPACING, TRUE, 0));
    ok(ItemHeight > 0, "Item height %Id\n", ItemHeight);
    if (ItemHeight <= 0)
        return;

    for (Step = 0; Step <= SLIDE_STEPS; Step++)
    {
        POINTER_FLAGS Flags = Step == 0 ? (POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT)
                                        : (POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT);
        ok(Inject(Fingers, X, Y - Step * SLIDE_STEP_PIXELS, Flags), "%u fingers, step %d: error %lu\n",
           Fingers, Step, GetLastError());
        Pump(16);
    }
    ok(Inject(Fingers, X, Y - SLIDE_STEPS * SLIDE_STEP_PIXELS, POINTER_FLAG_UP), "%u fingers, lift: error %lu\n",
       Fingers, GetLastError());
    Pump(600);

    Rows = (int)SendMessageW(List, LVM_GETTOPINDEX, 0, 0);
    ok(Rows >= SLIDE_STEPS * SLIDE_STEP_PIXELS / ItemHeight - 1,
       "%u fingers slid %d pixels: the list moved %d rows of %Id pixels\n",
       Fingers, SLIDE_STEPS * SLIDE_STEP_PIXELS, Rows, ItemHeight);
    ok_long((LONG)SendMessageW(List, LVM_GETSELECTEDCOUNT, 0, 0), 0);
}

static
VOID
Tap(
    _In_ HWND List)
{
    RECT Item = { LVIR_LABEL };
    LONG X, Y;

    SendMessageW(List, WM_VSCROLL, SB_TOP, 0);
    Pump(150);
    ok(SendMessageW(List, LVM_GETITEMRECT, 3, (LPARAM)&Item), "LVM_GETITEMRECT failed\n");
    MapWindowPoints(List, NULL, (POINT *)&Item, 2);
    X = Item.left + 10;
    Y = (Item.top + Item.bottom) / 2;
    ok(Inject(1, X, Y, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT), "Tap down: error %lu\n", GetLastError());
    Pump(50);
    ok(Inject(1, X, Y, POINTER_FLAG_UP), "Tap up: error %lu\n", GetLastError());
    Pump(400);

    ok_long((LONG)SendMessageW(List, LVM_GETSELECTEDCOUNT, 0, 0), 1);
    ok_long((LONG)SendMessageW(List, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED), 3);
    ok_long((LONG)SendMessageW(List, LVM_GETTOPINDEX, 0, 0), 0);
}

static
VOID
TapAt(
    _In_ LONG X,
    _In_ LONG Y)
{
    ok(Inject(1, X, Y, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT), "Tap down: error %lu\n", GetLastError());
    Pump(40);
    ok(Inject(1, X, Y, POINTER_FLAG_UP), "Tap up: error %lu\n", GetLastError());
}

static
VOID
DoubleTap(
    _In_ HWND List,
    _In_ LONG Dx,
    _In_ LONG Dy,
    _In_ LONG ExpectedActivations)
{
    RECT Item = { LVIR_LABEL };
    LONG X, Y;

    Pump(GetDoubleClickTime() + 300);
    ok(SendMessageW(List, LVM_GETITEMRECT, 3, (LPARAM)&Item), "LVM_GETITEMRECT failed\n");
    MapWindowPoints(List, NULL, (POINT *)&Item, 2);
    X = Item.left + 10;
    Y = (Item.top + Item.bottom) / 2;
    Activations = 0;
    LastActivated = -1;
    TapAt(X, Y);
    Pump(120);
    TapAt(X + Dx, Y + Dy);
    Pump(500);

    ok(Activations == ExpectedActivations, "Second tap %ld,%ld away: %ld activations, expected %ld\n",
       Dx, Dy, Activations, ExpectedActivations);
    if (ExpectedActivations)
        ok(LastActivated == 3, "Second tap %ld,%ld away activated item %ld\n", Dx, Dy, LastActivated);
}

START_TEST(InjectTouchInput)
{
    WNDCLASSW Class = { 0 };
    RECT Row = { LVIR_BOUNDS };
    INITCOMMONCONTROLSEX Controls = { sizeof(Controls), ICC_LISTVIEW_CLASSES };
    LVCOLUMNW Column = { LVCF_TEXT | LVCF_WIDTH };
    LVITEMW Item = { LVIF_TEXT };
    POINTER_TOUCH_INFO Touch;
    WCHAR Text[32];
    HWND Window, List;
    int i;

    Contact(&Touch, 0, 100, 100, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT);
    SetLastError(0xdeadbeef);
    ok(!InjectTouchInput(1, &Touch), "Injecting before initializing succeeded\n");
    ok_err(ERROR_ACCESS_DENIED);

    SetLastError(0xdeadbeef);
    ok(!InitializeTouchInjection(0, TOUCH_FEEDBACK_NONE), "No contacts accepted\n");
    ok_err(ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok(!InitializeTouchInjection(MAX_TOUCH_COUNT + 1, TOUCH_FEEDBACK_NONE), "Too many contacts accepted\n");
    ok_err(ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok(!InitializeTouchInjection(2, 0), "No feedback mode accepted\n");
    ok_err(ERROR_INVALID_PARAMETER);
    ok(InitializeTouchInjection(2, TOUCH_FEEDBACK_NONE), "InitializeTouchInjection failed with %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ok(!InjectTouchInput(3, (POINTER_TOUCH_INFO[3]){ 0 }), "More contacts than initialized accepted\n");
    ok_err(ERROR_INVALID_PARAMETER);

    InitCommonControlsEx(&Controls);
    Class.lpfnWndProc = ParentProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"InjectTouchInputParent";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed with %lu\n", GetLastError());
    Window = CreateWindowExW(WS_EX_TOPMOST, Class.lpszClassName, L"InjectTouchInput", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             20, 20, 460, 620, NULL, NULL, Class.hInstance, NULL);
    ok(Window != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!Window)
        return;
    List = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT,
                           0, 0, 440, 570, Window, NULL, GetModuleHandleW(NULL), NULL);
    ok(List != NULL, "Creating the list view failed with %lu\n", GetLastError());
    if (!List)
    {
        DestroyWindow(Window);
        return;
    }
    Column.cx = 400;
    Column.pszText = (PWSTR)L"Item";
    SendMessageW(List, LVM_INSERTCOLUMNW, 0, (LPARAM)&Column);
    for (i = 0; i < ROWS; i++)
    {
        StringCchPrintfW(Text, ARRAYSIZE(Text), L"Row %d", i);
        Item.iItem = i;
        Item.pszText = Text;
        SendMessageW(List, LVM_INSERTITEMW, 0, (LPARAM)&Item);
    }
    SetForegroundWindow(Window);
    Pump(500);

    Tap(List);
    DoubleTap(List, DOUBLE_TAP_TOLERANCE, 0, 1);
    DoubleTap(List, DOUBLE_TAP_TOLERANCE + 1, 0, 0);
    DoubleTap(List, DOUBLE_TAP_TOLERANCE, DOUBLE_TAP_TOLERANCE, 1);
    SendMessageW(List, LVM_GETITEMRECT, 3, (LPARAM)&Row);
    if (Row.bottom - Row.top <= DOUBLE_TAP_TOLERANCE)
        DoubleTap(List, 0, Row.bottom - Row.top, 1);
    else
        skip("Rows of %ld pixels are taller than the double tap tolerance\n", Row.bottom - Row.top);
    Slide(List, 1);
    Slide(List, 2);

    DestroyWindow(Window);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
