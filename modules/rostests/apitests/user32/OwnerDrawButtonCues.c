/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the cue flags an owner-drawn user32 button reports to its parent
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static UINT DrawCount;
static UINT ItemState;

static LRESULT CALLBACK
ParentProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (Message == WM_DRAWITEM)
    {
        const DRAWITEMSTRUCT *Draw = (const DRAWITEMSTRUCT *)LParam;

        if (Draw->CtlType == ODT_BUTTON)
        {
            DrawCount++;
            ItemState = Draw->itemState;
        }
        return TRUE;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static void
CheckCues(HWND Button, WORD Action, WORD Flags, LRESULT Expected, BOOL CuesForced)
{
    LRESULT UiState;
    UINT Cues;
    HDC Dc;

    SendMessageW(Button, WM_UPDATEUISTATE, MAKEWPARAM(Action, Flags), 0);
    UiState = SendMessageW(Button, WM_QUERYUISTATE, 0, 0) & (UISF_HIDEFOCUS | UISF_HIDEACCEL);
    if (!CuesForced)
        ok(UiState == Expected, "UI state %Ix after action %u flags 0x%x, expected %Ix\n", (UINT_PTR)UiState, Action, Flags, (UINT_PTR)Expected);

    DrawCount = 0;
    ItemState = 0;
    Dc = GetDC(Button);
    SendMessageW(Button, WM_PRINTCLIENT, (WPARAM)Dc, PRF_CLIENT);
    ReleaseDC(Button, Dc);
    ok(DrawCount == 1, "WM_DRAWITEM sent %u times\n", DrawCount);

    Cues = ((UiState & UISF_HIDEFOCUS) ? ODS_NOFOCUSRECT : 0) | ((UiState & UISF_HIDEACCEL) ? ODS_NOACCEL : 0);
    ok((ItemState & (ODS_NOFOCUSRECT | ODS_NOACCEL)) == Cues,
       "itemState 0x%x for UI state %Ix, expected cue flags 0x%x\n", ItemState, (UINT_PTR)UiState, Cues);
}

static HWND
CreateOwnerDrawButton(HWND *Parent)
{
    WNDCLASSW Class = {0};

    Class.lpfnWndProc = ParentProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"OwnerDrawButtonCuesParent";
    RegisterClassW(&Class);
    *Parent = CreateWindowExW(0, Class.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, NULL, NULL, Class.hInstance, NULL);
    ok(*Parent != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!*Parent)
        return NULL;
    return CreateWindowExW(0, L"Button", L"Pause", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 10, 10, 60, 30, *Parent, (HMENU)100, Class.hInstance, NULL);
}

static void
TestOwnerDrawButtonCues(HWND Button)
{
    BOOL CuesForced = FALSE;

    ok(SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &CuesForced, 0), "SPI_GETKEYBOARDCUES failed: %lu\n", GetLastError());
    CheckCues(Button, UIS_SET, UISF_HIDEFOCUS | UISF_HIDEACCEL, UISF_HIDEFOCUS | UISF_HIDEACCEL, CuesForced);
    CheckCues(Button, UIS_CLEAR, UISF_HIDEACCEL, UISF_HIDEFOCUS, CuesForced);
    CheckCues(Button, UIS_CLEAR, UISF_HIDEFOCUS, 0, CuesForced);
    CheckCues(Button, UIS_SET, UISF_HIDEACCEL, UISF_HIDEACCEL, CuesForced);
    CheckCues(Button, UIS_CLEAR, UISF_HIDEACCEL, 0, CuesForced);
}

START_TEST(OwnerDrawButtonCues)
{
    HWND Parent, Button;

    Button = CreateOwnerDrawButton(&Parent);
    ok(Button != NULL, "Button creation failed: %lu\n", GetLastError());
    if (Button)
        TestOwnerDrawButtonCues(Button);
    if (Parent)
        DestroyWindow(Parent);
}
