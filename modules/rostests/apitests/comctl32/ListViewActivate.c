/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Enter activates the focused list view item with the state of the modifier keys
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <commctrl.h>

static LONG Activations;
static LONG Returns;
static NMITEMACTIVATE Activated;

static
LRESULT
CALLBACK
ParentProc(
    _In_ HWND hWnd,
    _In_ UINT Msg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    if (Msg == WM_NOTIFY)
    {
        NMHDR *Header = (NMHDR *)lParam;

        if (Header->code == LVN_ITEMACTIVATE)
        {
            Activations++;
            Activated = *(NMITEMACTIVATE *)lParam;
        }
        else if (Header->code == NM_RETURN)
        {
            Returns++;
        }
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static
VOID
PressEnter(
    _In_ HWND List,
    _In_ BYTE ShiftState,
    _In_ UINT ExpectedKeyFlags)
{
    BYTE Saved[256], State[256];

    ok(GetKeyboardState(Saved), "GetKeyboardState failed with %lu\n", GetLastError());
    RtlCopyMemory(State, Saved, sizeof(State));
    State[VK_SHIFT] = ShiftState;
    State[VK_CONTROL] = 0;
    State[VK_MENU] = 0;
    ok(SetKeyboardState(State), "SetKeyboardState failed with %lu\n", GetLastError());

    Activations = Returns = 0;
    FillMemory(&Activated, sizeof(Activated), 0xCC);
    SendMessageW(List, WM_KEYDOWN, VK_RETURN, 0x001C0001);
    SetKeyboardState(Saved);

    ok_long(Returns, 1);
    ok_long(Activations, 1);
    ok(Activated.hdr.hwndFrom == List, "Notification from %p, expected %p\n", Activated.hdr.hwndFrom, List);
    ok_int(Activated.iItem, 7);
    ok_hex(Activated.uKeyFlags, ExpectedKeyFlags);
}

START_TEST(ListViewActivate)
{
    WNDCLASSW Class = { 0 };
    LVCOLUMNW Column = { LVCF_TEXT | LVCF_WIDTH };
    LVITEMW Item = { LVIF_TEXT };
    WCHAR Text[16];
    HWND Parent, List;
    int i;

    Class.lpfnWndProc = ParentProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"ListViewActivateParent";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed with %lu\n", GetLastError());
    Parent = CreateWindowExW(0, Class.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                             0, 0, 400, 400, NULL, NULL, Class.hInstance, NULL);
    ok(Parent != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!Parent)
        return;
    List = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT,
                           0, 0, 380, 360, Parent, NULL, Class.hInstance, NULL);
    ok(List != NULL, "Creating the list view failed with %lu\n", GetLastError());
    if (!List)
    {
        DestroyWindow(Parent);
        return;
    }

    Column.cx = 300;
    Column.pszText = (PWSTR)L"Name";
    SendMessageW(List, LVM_INSERTCOLUMNW, 0, (LPARAM)&Column);
    for (i = 0; i < 10; i++)
    {
        wsprintfW(Text, L"Item %d", i);
        Item.iItem = i;
        Item.pszText = Text;
        SendMessageW(List, LVM_INSERTITEMW, 0, (LPARAM)&Item);
    }
    Item.stateMask = LVIS_FOCUSED | LVIS_SELECTED;
    Item.state = LVIS_FOCUSED | LVIS_SELECTED;
    SendMessageW(List, LVM_SETITEMSTATE, 7, (LPARAM)&Item);

    PressEnter(List, 0, 0);
    PressEnter(List, 0x80, LVKF_SHIFT);

    DestroyWindow(Parent);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
