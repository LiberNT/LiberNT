/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that icon view items are laid out, arranged and snapped inside the list view work areas
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <commctrl.h>
#include <strsafe.h>

static LONG Sx, Sy, Ox, Oy;

static
HWND
CreateList(
    _In_ DWORD Style,
    _In_ DWORD ExStyle)
{
    HWND List = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_POPUP | WS_VISIBLE | LVS_ICON | Style, 0, 0, 900, 600,
                                NULL, NULL, GetModuleHandleW(NULL), NULL);

    ok(List != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (List && ExStyle)
        ListView_SetExtendedListViewStyleEx(List, ExStyle, ExStyle);
    return List;
}

static
VOID
AddItems(
    _In_ HWND List,
    _In_ INT Count)
{
    INT First = ListView_GetItemCount(List), Index;
    WCHAR Text[16];
    LVITEMW Item;

    for (Index = First; Index < First + Count; Index++)
    {
        StringCchPrintfW(Text, ARRAYSIZE(Text), L"Item %d", Index);
        ZeroMemory(&Item, sizeof(Item));
        Item.mask = LVIF_TEXT;
        Item.iItem = Index;
        Item.pszText = Text;
        ok(ListView_InsertItem(List, &Item) == Index, "Inserting item %d failed\n", Index);
    }
}

#define ok_pos(List, Item, X, Y) \
    do { \
        POINT Pt_ = { MAXLONG, MAXLONG }; \
        ok(ListView_GetItemPosition((List), (Item), &Pt_), "LVM_GETITEMPOSITION %d failed\n", (Item)); \
        ok(Pt_.x == (X) && Pt_.y == (Y), "item %d at (%ld,%ld), expected (%ld,%ld)\n", (Item), Pt_.x, Pt_.y, \
           (LONG)(X), (LONG)(Y)); \
    } while (0)

static
VOID
MoveItem(
    _In_ HWND List,
    _In_ INT Item,
    _In_ LONG X,
    _In_ LONG Y)
{
    POINT Pt = { X, Y };

    ok(SendMessageW(List, LVM_SETITEMPOSITION32, Item, (LPARAM)&Pt) != 0, "LVM_SETITEMPOSITION32 %d failed\n", Item);
}

static
UINT
GetCount(
    _In_ HWND List)
{
    UINT Count = 0xdeadbeef;

    ok(SendMessageW(List, LVM_GETNUMBEROFWORKAREAS, 0, (LPARAM)&Count) == 0, "LVM_GETNUMBEROFWORKAREAS did not return 0\n");
    return Count;
}

static
VOID
TestMessages(VOID)
{
    RECT Two[2] = { { 0, 0, 450, 560 }, { 450, 0, 900, 600 } };
    RECT Many[LV_MAX_WORKAREAS + 1];
    RECT Got[20];
    HWND List;
    INT Index;

    List = CreateList(0, 0);
    if (!List)
        return;
    ok_int(GetCount(List), 0);
    ok(SendMessageW(List, LVM_SETWORKAREAS, 2, (LPARAM)Two) == 0, "LVM_SETWORKAREAS did not return 0\n");
    ok_int(GetCount(List), 2);
    memset(Got, 0xcc, sizeof(Got));
    ok(SendMessageW(List, LVM_GETWORKAREAS, ARRAYSIZE(Got), (LPARAM)Got) == 0, "LVM_GETWORKAREAS did not return 0\n");
    ok(EqualRect(&Got[0], &Two[0]), "work area 0 is %s\n", wine_dbgstr_rect(&Got[0]));
    ok(EqualRect(&Got[1], &Two[1]), "work area 1 is %s\n", wine_dbgstr_rect(&Got[1]));
    ok(IsRectEmpty(&Got[2]) && Got[2].left == 0 && Got[2].top == 0, "slot 2 is %s\n", wine_dbgstr_rect(&Got[2]));

    SendMessageW(List, LVM_SETWORKAREAS, 0, (LPARAM)Two);
    ok_int(GetCount(List), 0);
    SendMessageW(List, LVM_SETWORKAREAS, 2, (LPARAM)Two);
    SendMessageW(List, LVM_SETWORKAREAS, 1, 0);
    ok_int(GetCount(List), 0);

    for (Index = 0; Index < ARRAYSIZE(Many); Index++)
        SetRect(&Many[Index], Index * 50, 0, Index * 50 + 50, 100);
    SendMessageW(List, LVM_SETWORKAREAS, ARRAYSIZE(Many), (LPARAM)Many);
    ok_int(GetCount(List), LV_MAX_WORKAREAS);
    memset(Got, 0xcc, sizeof(Got));
    SendMessageW(List, LVM_GETWORKAREAS, ARRAYSIZE(Got), (LPARAM)Got);
    ok(EqualRect(&Got[LV_MAX_WORKAREAS - 1], &Many[LV_MAX_WORKAREAS - 1]), "last work area is %s\n",
       wine_dbgstr_rect(&Got[LV_MAX_WORKAREAS - 1]));
    DestroyWindow(List);
}

static
VOID
TestNewItems(VOID)
{
    RECT Area = { 2 * Sx, Sy, 2 * Sx + 4 * Sx + Sx / 2, Sy + 3 * Sy + Sy / 2 };
    HWND List;
    INT Index;

    List = CreateList(0, 0);
    if (!List)
        return;
    SendMessageW(List, LVM_SETWORKAREAS, 1, (LPARAM)&Area);
    AddItems(List, 6);
    for (Index = 0; Index < 4; Index++)
        ok_pos(List, Index, Area.left + Ox + Index * Sx, Area.top + Oy);
    ok_pos(List, 4, Area.left + Ox, Area.top + Oy + Sy);
    ok_pos(List, 5, Area.left + Ox + Sx, Area.top + Oy + Sy);
    DestroyWindow(List);

    List = CreateList(LVS_ALIGNLEFT, 0);
    if (!List)
        return;
    SendMessageW(List, LVM_SETWORKAREAS, 1, (LPARAM)&Area);
    AddItems(List, 5);
    for (Index = 0; Index < 3; Index++)
        ok_pos(List, Index, Area.left + Ox, Area.top + Oy + Index * Sy);
    ok_pos(List, 3, Area.left + Ox + Sx, Area.top + Oy);
    ok_pos(List, 4, Area.left + Ox + Sx, Area.top + Oy + Sy);
    DestroyWindow(List);
}

static
VOID
TestTwoAreas(VOID)
{
    RECT Two[2] = { { 0, 0, 10 * Sx + Sx / 2, 13 * Sy }, { 10 * Sx + Sx / 2, 0, 21 * Sx, 14 * Sy } };
    LONG Left2 = Two[1].left;
    HWND List;

    List = CreateList(0, LVS_EX_SNAPTOGRID);
    if (!List)
        return;
    SendMessageW(List, LVM_SETWORKAREAS, 2, (LPARAM)Two);
    AddItems(List, 4);
    ok_pos(List, 3, Ox + 3 * Sx, Oy);

    MoveItem(List, 3, Left2 + Ox + 3 * Sx + Sx / 4, Oy + 2 * Sy + Sy / 4);
    ok_pos(List, 3, Left2 + Ox + 3 * Sx, Oy + 2 * Sy);
    MoveItem(List, 3, Left2 + 1, Oy + Sy + Sy / 4);
    ok_pos(List, 3, Left2 + Ox, Oy + Sy);
    MoveItem(List, 3, Ox + 23 * Sx + Sx / 4, Oy + Sy + Sy / 4);
    ok_pos(List, 3, Ox + 23 * Sx, Oy + Sy);
    MoveItem(List, 3, Ox + 5 * Sx + Sx / 4, Oy + 16 * Sy + Sy / 4);
    ok_pos(List, 3, Ox + 5 * Sx, Oy + 16 * Sy);

    MoveItem(List, 2, Left2 + Ox + 3 * Sx, Oy + 4 * Sy);
    MoveItem(List, 3, Left2 + Ox + 5 * Sx, Oy + 7 * Sy);
    ok(ListView_Arrange(List, LVA_DEFAULT), "LVA_DEFAULT failed\n");
    ok_pos(List, 0, Ox, Oy);
    ok_pos(List, 1, Ox + Sx, Oy);
    ok_pos(List, 2, Left2 + Ox, Oy);
    ok_pos(List, 3, Left2 + Ox + Sx, Oy);
    AddItems(List, 2);
    ok_pos(List, 4, Ox + 2 * Sx, Oy);
    ok_pos(List, 5, Ox + 3 * Sx, Oy);
    DestroyWindow(List);
}

START_TEST(ListViewWorkAreas)
{
    INITCOMMONCONTROLSEX Init = { sizeof(Init), ICC_LISTVIEW_CLASSES };
    POINT First = { MAXLONG, MAXLONG };
    DWORD Spacing;
    HWND List;

    ok(InitCommonControlsEx(&Init), "InitCommonControlsEx failed\n");
    List = CreateList(0, 0);
    if (!List)
        return;
    AddItems(List, 1);
    Spacing = (DWORD)SendMessageW(List, LVM_GETITEMSPACING, FALSE, 0);
    Sx = LOWORD(Spacing);
    Sy = HIWORD(Spacing);
    ok(ListView_GetItemPosition(List, 0, &First), "LVM_GETITEMPOSITION 0 failed\n");
    Ox = First.x;
    Oy = First.y;
    DestroyWindow(List);
    ok(Sx > 8 && Sy > 8, "Icon spacing %ld x %ld\n", Sx, Sy);
    ok(Ox >= 0 && Ox <= Sx / 2 && Oy >= 0 && Oy <= Sy / 2, "First item at (%ld,%ld)\n", Ox, Oy);
    if (Sx <= 8 || Sy <= 8)
        return;

    TestMessages();
    TestNewItems();
    TestTwoAreas();
}
