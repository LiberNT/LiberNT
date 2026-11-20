/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that LVS_EX_SNAPTOGRID and LVA_SNAPTOGRID put icon view items on the icon grid
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <commctrl.h>
#include <strsafe.h>

#define ITEM_COUNT 6

static POINT Grid;
static SIZE Spacing;

static
POINT
GetPos(
    _In_ HWND List,
    _In_ INT Item)
{
    POINT Pt = { MAXLONG, MAXLONG };

    ok(ListView_GetItemPosition(List, Item, &Pt), "LVM_GETITEMPOSITION %d failed\n", Item);
    Pt.x -= Grid.x;
    Pt.y -= Grid.y;
    return Pt;
}

#define ok_pos(List, Item, X, Y) \
    do { \
        POINT Pt_ = GetPos(List, Item); \
        ok(Pt_.x == (X) && Pt_.y == (Y), "item %d at (%ld,%ld) from the grid origin, expected (%ld,%ld)\n", \
           (Item), Pt_.x, Pt_.y, (LONG)(X), (LONG)(Y)); \
    } while (0)

static
VOID
SetPos32(
    _In_ HWND List,
    _In_ INT Item,
    _In_ LONG X,
    _In_ LONG Y)
{
    POINT Pt = { Grid.x + X, Grid.y + Y };

    ok(SendMessageW(List, LVM_SETITEMPOSITION32, Item, (LPARAM)&Pt) != 0, "LVM_SETITEMPOSITION32 %d failed\n", Item);
}

static
VOID
SetPos16(
    _In_ HWND List,
    _In_ INT Item,
    _In_ LONG X,
    _In_ LONG Y)
{
    ok(SendMessageW(List, LVM_SETITEMPOSITION, Item, MAKELPARAM(Grid.x + X, Grid.y + Y)) != 0,
       "LVM_SETITEMPOSITION %d failed\n", Item);
}

START_TEST(ListViewSnapToGrid)
{
    INITCOMMONCONTROLSEX Init = { sizeof(Init), ICC_LISTVIEW_CLASSES };
    WCHAR Text[16];
    LVITEMW Item;
    DWORD Value;
    HWND List;
    INT Index;

    ok(InitCommonControlsEx(&Init), "InitCommonControlsEx failed\n");
    List = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_POPUP | WS_VISIBLE | LVS_ICON, 0, 0, 640, 480,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(List != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!List)
        return;

    for (Index = 0; Index < ITEM_COUNT; Index++)
    {
        StringCchPrintfW(Text, ARRAYSIZE(Text), L"Item %d", Index);
        ZeroMemory(&Item, sizeof(Item));
        Item.mask = LVIF_TEXT;
        Item.iItem = Index;
        Item.pszText = Text;
        ok(ListView_InsertItem(List, &Item) == Index, "Inserting item %d failed\n", Index);
    }

    Value = (DWORD)SendMessageW(List, LVM_GETITEMSPACING, FALSE, 0);
    Spacing.cx = LOWORD(Value);
    Spacing.cy = HIWORD(Value);
    ok(Spacing.cx > 8 && Spacing.cy > 8, "Icon spacing %ld x %ld\n", Spacing.cx, Spacing.cy);
    ok(ListView_GetItemPosition(List, 0, &Grid), "LVM_GETITEMPOSITION 0 failed\n");
    ok_pos(List, 1, Spacing.cx, 0);

    SetPos32(List, 0, Spacing.cx / 4, Spacing.cy / 3);
    ok_pos(List, 0, Spacing.cx / 4, Spacing.cy / 3);

    Value = ListView_SetExtendedListViewStyleEx(List, LVS_EX_SNAPTOGRID, LVS_EX_SNAPTOGRID);
    ok((Value & LVS_EX_SNAPTOGRID) == 0, "Extended style was %08lx\n", Value);
    ok((ListView_GetExtendedListViewStyle(List) & LVS_EX_SNAPTOGRID) != 0, "LVS_EX_SNAPTOGRID not kept\n");

    SetPos32(List, 1, 2 * Spacing.cx + Spacing.cx / 4, Spacing.cy + Spacing.cy / 4);
    ok_pos(List, 1, 2 * Spacing.cx, Spacing.cy);
    SetPos32(List, 2, 2 * Spacing.cx + 3 * Spacing.cx / 4, 3 * Spacing.cy / 4);
    ok_pos(List, 2, 3 * Spacing.cx, Spacing.cy);
    SetPos16(List, 3, Spacing.cx - Spacing.cx / 3, 4 * Spacing.cy + Spacing.cy / 3);
    ok_pos(List, 3, Spacing.cx, 4 * Spacing.cy);
    SetPos32(List, 4, -7, -9);
    ok_pos(List, 4, 0, 0);

    ListView_SetExtendedListViewStyleEx(List, LVS_EX_SNAPTOGRID, 0);
    SetPos32(List, 5, 4 * Spacing.cx - 5, Spacing.cy + 6);
    ok_pos(List, 5, 4 * Spacing.cx - 5, Spacing.cy + 6);

    ok(ListView_Arrange(List, LVA_SNAPTOGRID), "LVA_SNAPTOGRID failed\n");
    ok_pos(List, 0, 0, 0);
    ok_pos(List, 1, 2 * Spacing.cx, Spacing.cy);
    ok_pos(List, 2, 3 * Spacing.cx, Spacing.cy);
    ok_pos(List, 3, Spacing.cx, 4 * Spacing.cy);
    ok_pos(List, 4, 0, 0);
    ok_pos(List, 5, 4 * Spacing.cx, Spacing.cy);

    DestroyWindow(List);
}
