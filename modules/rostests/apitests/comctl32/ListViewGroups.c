/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests list view groups
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <commctrl.h>
#include <stdio.h>

static HWND
CreateList(DWORD Style)
{
    LVCOLUMNW Column = {0};
    HWND hWnd;

    hWnd = CreateWindowExW(0, WC_LISTVIEWW, NULL, WS_POPUP | Style,
                           0, 0, 300, 400, NULL, NULL, NULL, NULL);
    if (!hWnd)
        return NULL;
    Column.mask = LVCF_WIDTH | LVCF_TEXT;
    Column.cx = 200;
    Column.pszText = (LPWSTR)L"Name";
    SendMessageW(hWnd, LVM_INSERTCOLUMNW, 0, (LPARAM)&Column);
    return hWnd;
}

static LRESULT
InsertGroup(HWND hWnd, INT Index, INT Id, LPCWSTR Header)
{
    LVGROUP Group = {0};

    Group.cbSize = sizeof(Group);
    Group.mask = LVGF_HEADER | LVGF_GROUPID;
    Group.pszHeader = (LPWSTR)Header;
    Group.iGroupId = Id;
    return SendMessageW(hWnd, LVM_INSERTGROUP, (WPARAM)Index, (LPARAM)&Group);
}

static LRESULT
InsertItem(HWND hWnd, INT Index, INT GroupId, LPCWSTR Text)
{
    LVITEMW Item = {0};

    Item.mask = LVIF_TEXT | LVIF_GROUPID;
    Item.iItem = Index;
    Item.iGroupId = GroupId;
    Item.pszText = (LPWSTR)Text;
    return SendMessageW(hWnd, LVM_INSERTITEMW, 0, (LPARAM)&Item);
}

static INT
ItemGroup(HWND hWnd, INT Index)
{
    LVITEMW Item = {0};

    Item.mask = LVIF_GROUPID;
    Item.iItem = Index;
    Item.iGroupId = 12345;
    if (!SendMessageW(hWnd, LVM_GETITEMW, 0, (LPARAM)&Item))
        return -12345;
    return Item.iGroupId;
}

static BOOL
ItemRect(HWND hWnd, INT Index, RECT *Rect)
{
    SetRectEmpty(Rect);
    Rect->left = LVIR_BOUNDS;
    return (BOOL)SendMessageW(hWnd, LVM_GETITEMRECT, Index, (LPARAM)Rect);
}

static BOOL
GroupRect(HWND hWnd, INT Id, LONG Type, RECT *Rect)
{
    SetRectEmpty(Rect);
    Rect->top = Type;
    return (BOOL)SendMessageW(hWnd, LVM_GETGROUPRECT, Id, (LPARAM)Rect);
}

static void
TestEnable(void)
{
    HWND hWnd = CreateList(LVS_REPORT);

    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd)
        return;
    ok_long((LONG)SendMessageW(hWnd, LVM_ISGROUPVIEWENABLED, 0, 0), FALSE);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPCOUNT, 0, 0), 0);
    ok_long((LONG)SendMessageW(hWnd, LVM_ENABLEGROUPVIEW, TRUE, 0), 1);
    ok_long((LONG)SendMessageW(hWnd, LVM_ISGROUPVIEWENABLED, 0, 0), TRUE);
    ok_long((LONG)SendMessageW(hWnd, LVM_ENABLEGROUPVIEW, TRUE, 0), 0);
    ok_long((LONG)SendMessageW(hWnd, LVM_ENABLEGROUPVIEW, FALSE, 0), 1);
    ok_long((LONG)SendMessageW(hWnd, LVM_ISGROUPVIEWENABLED, 0, 0), FALSE);
    ok_long((LONG)SendMessageW(hWnd, LVM_ENABLEGROUPVIEW, FALSE, 0), 0);
    DestroyWindow(hWnd);

    hWnd = CreateList(LVS_LIST);
    if (hWnd)
    {
        ok_long((LONG)SendMessageW(hWnd, LVM_ENABLEGROUPVIEW, TRUE, 0), 1);
        ok_long((LONG)SendMessageW(hWnd, LVM_ISGROUPVIEWENABLED, 0, 0), TRUE);
        DestroyWindow(hWnd);
    }
}

static void
TestInsertQuery(void)
{
    LVGROUP Group;
    WCHAR Buffer[64];
    HWND hWnd = CreateList(LVS_REPORT);

    if (!hWnd)
        return;

    ok_long((LONG)InsertGroup(hWnd, -1, 10, L"Ten"), 0);
    ok_long((LONG)InsertGroup(hWnd, -1, 20, L"Twenty"), 1);
    ok_long((LONG)InsertGroup(hWnd, 0, 30, L"Thirty"), 0);
    ok_long((LONG)InsertGroup(hWnd, 100, 40, L"Forty"), 3);
    ok_long((LONG)InsertGroup(hWnd, -1, 10, L"Duplicate"), 4);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPCOUNT, 0, 0), 5);

    ZeroMemory(&Group, sizeof(Group));
    Group.cbSize = sizeof(Group);
    Group.mask = LVGF_HEADER;
    Group.pszHeader = (LPWSTR)L"No id";
    ok_long((LONG)SendMessageW(hWnd, LVM_INSERTGROUP, (WPARAM)-1, (LPARAM)&Group), -1);

    ZeroMemory(&Group, sizeof(Group));
    Group.cbSize = 4;
    Group.mask = LVGF_GROUPID;
    Group.iGroupId = 50;
    ok_long((LONG)SendMessageW(hWnd, LVM_INSERTGROUP, (WPARAM)-1, (LPARAM)&Group), -1);
    ok_long((LONG)SendMessageW(hWnd, LVM_INSERTGROUP, (WPARAM)-1, 0), -1);

    ok_long((LONG)SendMessageW(hWnd, LVM_HASGROUP, 10, 0), TRUE);
    ok_long((LONG)SendMessageW(hWnd, LVM_HASGROUP, 30, 0), TRUE);
    ok_long((LONG)SendMessageW(hWnd, LVM_HASGROUP, 50, 0), FALSE);

    ZeroMemory(&Group, sizeof(Group));
    Group.cbSize = sizeof(Group);
    Group.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE | LVGF_ALIGN;
    Group.stateMask = (UINT)-1;
    Group.pszHeader = Buffer;
    Group.cchHeader = _countof(Buffer);
    Buffer[0] = 0;
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFO, 20, (LPARAM)&Group), 20);
    ok_long(Group.iGroupId, 20);
    ok(!wcscmp(Buffer, L"Twenty"), "header %ls\n", Buffer);
    ok_long(Group.state, LVGS_NORMAL);
    trace("default align %#x\n", Group.uAlign);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFO, 99, (LPARAM)&Group), -1);

    ZeroMemory(&Group, sizeof(Group));
    Group.cbSize = LVGROUP_V5_SIZE;
    Group.mask = LVGF_HEADER | LVGF_GROUPID;
    Group.pszHeader = Buffer;
    Group.cchHeader = _countof(Buffer);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFO, 30, (LPARAM)&Group), 30);
    ok(!wcscmp(Buffer, L"Thirty"), "header %ls\n", Buffer);

    ZeroMemory(&Group, sizeof(Group));
    Group.cbSize = sizeof(Group);
    Group.mask = LVGF_HEADER;
    Group.pszHeader = Buffer;
    Group.cchHeader = 4;
    wcscpy(Buffer, L"XXXXXXXX");
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFO, 20, (LPARAM)&Group), 20);
    ok(!wcscmp(Buffer, L"Twe"), "truncated header %ls\n", Buffer);

    ZeroMemory(&Group, sizeof(Group));
    Group.cbSize = sizeof(Group);
    Group.mask = LVGF_GROUPID;
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFOBYINDEX, 0, (LPARAM)&Group), TRUE);
    ok_long(Group.iGroupId, 30);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFOBYINDEX, 1, (LPARAM)&Group), TRUE);
    ok_long(Group.iGroupId, 10);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFOBYINDEX, 3, (LPARAM)&Group), TRUE);
    ok_long(Group.iGroupId, 40);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFOBYINDEX, 4, (LPARAM)&Group), TRUE);
    ok_long(Group.iGroupId, 10);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFOBYINDEX, 5, (LPARAM)&Group), FALSE);

    ZeroMemory(&Group, sizeof(Group));
    Group.cbSize = sizeof(Group);
    Group.mask = LVGF_HEADER;
    Group.pszHeader = (LPWSTR)L"Renamed";
    ok_long((LONG)SendMessageW(hWnd, LVM_SETGROUPINFO, 20, (LPARAM)&Group), 20);
    ok_long((LONG)SendMessageW(hWnd, LVM_SETGROUPINFO, 99, (LPARAM)&Group), -1);
    Group.mask = LVGF_HEADER;
    Group.pszHeader = Buffer;
    Group.cchHeader = _countof(Buffer);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFO, 20, (LPARAM)&Group), 20);
    ok(!wcscmp(Buffer, L"Renamed"), "renamed header %ls\n", Buffer);

    ZeroMemory(&Group, sizeof(Group));
    Group.cbSize = sizeof(Group);
    Group.mask = LVGF_GROUPID;
    Group.iGroupId = 25;
    ok_long((LONG)SendMessageW(hWnd, LVM_SETGROUPINFO, 20, (LPARAM)&Group), 20);
    ok_long((LONG)SendMessageW(hWnd, LVM_HASGROUP, 20, 0), FALSE);
    ok_long((LONG)SendMessageW(hWnd, LVM_HASGROUP, 25, 0), TRUE);

    ok_long((LONG)SendMessageW(hWnd, LVM_REMOVEGROUP, 10, 0), 1);
    ok_long((LONG)SendMessageW(hWnd, LVM_REMOVEGROUP, 10, 0), 3);
    ok_long((LONG)SendMessageW(hWnd, LVM_REMOVEGROUP, 10, 0), -1);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPCOUNT, 0, 0), 3);
    SendMessageW(hWnd, LVM_REMOVEALLGROUPS, 0, 0);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPCOUNT, 0, 0), 0);
    ok_long((LONG)SendMessageW(hWnd, LVM_HASGROUP, 30, 0), FALSE);
    DestroyWindow(hWnd);
}

static void
TestItems(void)
{
    LVGROUP Group;
    LVITEMW Item;
    HWND hWnd = CreateList(LVS_REPORT);

    if (!hWnd)
        return;

    ok_long((LONG)InsertItem(hWnd, 0, 1, L"before groups"), -1);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETITEMCOUNT, 0, 0), 0);

    InsertGroup(hWnd, -1, 1, L"One");
    InsertGroup(hWnd, -1, 2, L"Two");
    ok_long((LONG)InsertItem(hWnd, 0, 1, L"a"), 0);
    ok_long((LONG)InsertItem(hWnd, 1, 2, L"b"), 1);
    ok_long((LONG)InsertItem(hWnd, 2, 1, L"c"), 2);
    ok_long((LONG)InsertItem(hWnd, 3, 7, L"unknown"), -1);
    ZeroMemory(&Item, sizeof(Item));
    Item.mask = LVIF_TEXT;
    Item.iItem = 3;
    Item.pszText = (LPWSTR)L"plain";
    ok_long((LONG)SendMessageW(hWnd, LVM_INSERTITEMW, 0, (LPARAM)&Item), 3);
    ok_long(ItemGroup(hWnd, 0), 1);
    ok_long(ItemGroup(hWnd, 1), 2);
    ok_long(ItemGroup(hWnd, 2), 1);
    ok_long(ItemGroup(hWnd, 3), I_GROUPIDNONE);

    ZeroMemory(&Item, sizeof(Item));
    Item.mask = LVIF_GROUPID;
    Item.iItem = 2;
    Item.iGroupId = 2;
    ok_long((LONG)SendMessageW(hWnd, LVM_SETITEMW, 0, (LPARAM)&Item), TRUE);
    ok_long(ItemGroup(hWnd, 2), 2);
    Item.iGroupId = 9;
    ok_long((LONG)SendMessageW(hWnd, LVM_SETITEMW, 0, (LPARAM)&Item), TRUE);
    ok_long(ItemGroup(hWnd, 2), I_GROUPIDNONE);
    Item.iGroupId = 2;
    ok_long((LONG)SendMessageW(hWnd, LVM_SETITEMW, 0, (LPARAM)&Item), TRUE);
    ok_long(ItemGroup(hWnd, 2), 2);

    ZeroMemory(&Group, sizeof(Group));
    Group.cbSize = sizeof(Group);
    Group.mask = LVGF_ITEMS;
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFO, 2, (LPARAM)&Group), 2);
    ok_long((LONG)Group.cItems, 2);
    ok_long(Group.iFirstItem, 1);
    Group.mask = LVGF_ITEMS;
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPINFO, 1, (LPARAM)&Group), 1);
    ok_long((LONG)Group.cItems, 1);
    ok_long(Group.iFirstItem, 0);

    SendMessageW(hWnd, LVM_MOVEITEMTOGROUP, 0, 2);
    ok_long(ItemGroup(hWnd, 0), 1);

    ok_long((LONG)SendMessageW(hWnd, LVM_REMOVEGROUP, 2, 0), 1);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETITEMCOUNT, 0, 0), 4);
    ok_long(ItemGroup(hWnd, 1), I_GROUPIDNONE);
    ok_long(ItemGroup(hWnd, 0), 1);
    DestroyWindow(hWnd);
}

static void
TestLayout(void)
{
    RECT A, B, C, Header, Whole, Label, Collapsed;
    HWND hWnd = CreateList(LVS_REPORT | LVS_NOCOLUMNHEADER);
    LVGROUP Group;

    if (!hWnd)
        return;

    InsertGroup(hWnd, -1, 1, L"First");
    InsertGroup(hWnd, -1, 2, L"Second");
    InsertItem(hWnd, 0, 2, L"in second");
    InsertItem(hWnd, 1, 1, L"in first");
    InsertItem(hWnd, 2, 1, L"in first too");

    ok(ItemRect(hWnd, 0, &A), "rect 0 without groups\n");
    ok(ItemRect(hWnd, 1, &B), "rect 1 without groups\n");
    ok(A.top < B.top, "without groups item 0 (%ld) is above item 1 (%ld)\n", A.top, B.top);
    ok(GroupRect(hWnd, 1, LVGGR_HEADER, &Header), "group rect without group view\n");
    trace("header without group view %ld,%ld,%ld,%ld\n", Header.left, Header.top, Header.right, Header.bottom);

    SendMessageW(hWnd, LVM_ENABLEGROUPVIEW, TRUE, 0);
    ok(ItemRect(hWnd, 0, &A), "rect 0 with groups\n");
    ok(ItemRect(hWnd, 1, &B), "rect 1 with groups\n");
    ok(ItemRect(hWnd, 2, &C), "rect 2 with groups\n");
    ok(B.top < C.top, "first group items keep their order: %ld %ld\n", B.top, C.top);
    ok(C.top < A.top, "the second group follows the first: %ld %ld\n", C.top, A.top);
    ok(B.top > 0, "a header is above the first item: %ld\n", B.top);

    ok(GroupRect(hWnd, 1, LVGGR_HEADER, &Header), "header rect\n");
    ok(GroupRect(hWnd, 1, LVGGR_GROUP, &Whole), "group rect\n");
    ok(GroupRect(hWnd, 1, LVGGR_LABEL, &Label), "label rect\n");
    ok(Header.bottom <= B.top, "header %ld..%ld above item %ld\n", Header.top, Header.bottom, B.top);
    ok(Whole.top == Header.top, "group starts at its header: %ld %ld\n", Whole.top, Header.top);
    ok(Whole.bottom >= C.bottom, "group %ld covers its last item %ld\n", Whole.bottom, C.bottom);
    ok(Label.left >= Header.left && Label.right <= Header.right, "label inside header\n");
    trace("header %ld,%ld,%ld,%ld group %ld,%ld,%ld,%ld label %ld,%ld,%ld,%ld\n",
          Header.left, Header.top, Header.right, Header.bottom,
          Whole.left, Whole.top, Whole.right, Whole.bottom,
          Label.left, Label.top, Label.right, Label.bottom);
    trace("items %ld,%ld,%ld,%ld %ld,%ld,%ld,%ld second group item %ld,%ld,%ld,%ld\n",
          B.left, B.top, B.right, B.bottom, C.left, C.top, C.right, C.bottom, A.left, A.top, A.right, A.bottom);
    ok(!GroupRect(hWnd, 9, LVGGR_HEADER, &Collapsed), "rect of a missing group\n");

    ZeroMemory(&Group, sizeof(Group));
    Group.cbSize = sizeof(Group);
    Group.mask = LVGF_STATE;
    Group.stateMask = LVGS_COLLAPSIBLE | LVGS_COLLAPSED;
    Group.state = LVGS_COLLAPSIBLE | LVGS_COLLAPSED;
    ok_long((LONG)SendMessageW(hWnd, LVM_SETGROUPINFO, 1, (LPARAM)&Group), 1);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPSTATE, 1, LVGS_COLLAPSED | LVGS_COLLAPSIBLE),
            LVGS_COLLAPSED | LVGS_COLLAPSIBLE);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPSTATE, 1, LVGS_COLLAPSED), LVGS_COLLAPSED);
    ok(GroupRect(hWnd, 1, LVGGR_GROUP, &Collapsed), "collapsed group rect\n");
    ok_long(Collapsed.bottom - Collapsed.top, Whole.bottom - Whole.top);
    ok(GroupRect(hWnd, 1, LVGGR_HEADER, &Collapsed), "collapsed header rect\n");
    trace("collapsed header %ld,%ld,%ld,%ld\n", Collapsed.left, Collapsed.top, Collapsed.right, Collapsed.bottom);
    ok(ItemRect(hWnd, 0, &C), "rect 0 after collapse\n");
    ok(C.top < A.top, "second group moves up after collapse: %ld %ld\n", C.top, A.top);

    Group.stateMask = LVGS_COLLAPSED;
    Group.state = 0;
    SendMessageW(hWnd, LVM_SETGROUPINFO, 1, (LPARAM)&Group);
    ok(ItemRect(hWnd, 0, &C), "rect 0 after expand\n");
    ok_long(C.top, A.top);

    Group.stateMask = LVGS_HIDDEN;
    Group.state = LVGS_HIDDEN;
    SendMessageW(hWnd, LVM_SETGROUPINFO, 2, (LPARAM)&Group);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPSTATE, 2, LVGS_HIDDEN), LVGS_HIDDEN);
    Group.state = 0;
    SendMessageW(hWnd, LVM_SETGROUPINFO, 2, (LPARAM)&Group);

    Group.stateMask = LVGS_FOCUSED;
    Group.state = LVGS_FOCUSED;
    ok_long((LONG)SendMessageW(hWnd, LVM_GETFOCUSEDGROUP, 0, 0), -1);
    SendMessageW(hWnd, LVM_SETGROUPINFO, 2, (LPARAM)&Group);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETFOCUSEDGROUP, 0, 0), 1);

    DestroyWindow(hWnd);
}

static void
TestHitTest(void)
{
    LVHITTESTINFO Hit;
    RECT Header, Item;
    HWND hWnd = CreateList(LVS_REPORT | LVS_NOCOLUMNHEADER);
    LRESULT Result;

    if (!hWnd)
        return;

    InsertGroup(hWnd, -1, 5, L"Hit");
    InsertGroup(hWnd, -1, 9, L"Second");
    InsertItem(hWnd, 0, 5, L"target");
    InsertItem(hWnd, 1, 9, L"other");
    SendMessageW(hWnd, LVM_ENABLEGROUPVIEW, TRUE, 0);
    if (!GroupRect(hWnd, 5, LVGGR_HEADER, &Header) || !ItemRect(hWnd, 0, &Item))
    {
        skip("no group rectangles\n");
        DestroyWindow(hWnd);
        return;
    }

    ZeroMemory(&Hit, sizeof(Hit));
    Hit.iGroup = -7;
    Hit.pt.x = (Header.left + Header.right) / 2;
    Hit.pt.y = (Header.top + Header.bottom) / 2;
    Result = SendMessageW(hWnd, LVM_HITTEST, (WPARAM)-1, (LPARAM)&Hit);
    ok_long((LONG)Result, 5);
    ok(Hit.flags & LVHT_EX_GROUP_HEADER, "header hit flags %#x\n", Hit.flags);
    ok_long(Hit.iGroup, 0);
    ok_long(Hit.iItem, 5);

    if (GroupRect(hWnd, 9, LVGGR_HEADER, &Header))
    {
        ZeroMemory(&Hit, sizeof(Hit));
        Hit.iGroup = -7;
        Hit.pt.x = Header.left + 2;
        Hit.pt.y = (Header.top + Header.bottom) / 2;
        Result = SendMessageW(hWnd, LVM_HITTEST, (WPARAM)-1, (LPARAM)&Hit);
        ok_long((LONG)Result, 9);
        ok(Hit.flags & LVHT_EX_GROUP_HEADER, "second header hit flags %#x\n", Hit.flags);
        ok_long(Hit.iGroup, 0);
    }

    ZeroMemory(&Hit, sizeof(Hit));
    Hit.iGroup = -7;
    Hit.pt.x = Item.left + 4;
    Hit.pt.y = (Item.top + Item.bottom) / 2;
    Result = SendMessageW(hWnd, LVM_HITTEST, (WPARAM)-1, (LPARAM)&Hit);
    ok_long((LONG)Result, 0);
    ok(Hit.flags & LVHT_ONITEM, "item hit flags %#x\n", Hit.flags);
    ok_long(Hit.iGroup, 0);
    DestroyWindow(hWnd);
}

static INT CALLBACK
CompareDescending(INT Id1, INT Id2, VOID *Context)
{
    ok(Context == (VOID *)0x1234, "context %p\n", Context);
    return Id2 - Id1;
}

static void
TestSort(void)
{
    LVINSERTGROUPSORTED Sorted;
    LVGROUP Group;
    HWND hWnd = CreateList(LVS_REPORT);
    LRESULT Result;

    if (!hWnd)
        return;

    InsertGroup(hWnd, -1, 1, L"1");
    InsertGroup(hWnd, -1, 3, L"3");
    InsertGroup(hWnd, -1, 2, L"2");
    Result = SendMessageW(hWnd, LVM_SORTGROUPS, (WPARAM)CompareDescending, (LPARAM)0x1234);
    ok_long((LONG)Result, 1);
    ZeroMemory(&Group, sizeof(Group));
    Group.cbSize = sizeof(Group);
    Group.mask = LVGF_GROUPID;
    SendMessageW(hWnd, LVM_GETGROUPINFOBYINDEX, 0, (LPARAM)&Group);
    ok_long(Group.iGroupId, 3);
    SendMessageW(hWnd, LVM_GETGROUPINFOBYINDEX, 2, (LPARAM)&Group);
    ok_long(Group.iGroupId, 1);

    ZeroMemory(&Sorted, sizeof(Sorted));
    Sorted.pfnGroupCompare = CompareDescending;
    Sorted.pvData = (VOID *)0x1234;
    Sorted.lvGroup.cbSize = sizeof(Sorted.lvGroup);
    Sorted.lvGroup.mask = LVGF_GROUPID | LVGF_HEADER;
    Sorted.lvGroup.iGroupId = 4;
    Sorted.lvGroup.pszHeader = (LPWSTR)L"4";
    Result = SendMessageW(hWnd, LVM_INSERTGROUPSORTED, (WPARAM)&Sorted, 0);
    ok_long((LONG)Result, 0);
    ok_long((LONG)SendMessageW(hWnd, LVM_GETGROUPCOUNT, 0, 0), 4);
    SendMessageW(hWnd, LVM_GETGROUPINFOBYINDEX, 0, (LPARAM)&Group);
    ok_long(Group.iGroupId, 4);

    Sorted.lvGroup.iGroupId = 0;
    Sorted.lvGroup.pszHeader = (LPWSTR)L"0";
    Result = SendMessageW(hWnd, LVM_INSERTGROUPSORTED, (WPARAM)&Sorted, 0);
    ok_long((LONG)Result, 4);
    SendMessageW(hWnd, LVM_GETGROUPINFOBYINDEX, 4, (LPARAM)&Group);
    ok_long(Group.iGroupId, 0);

    Result = SendMessageW(hWnd, LVM_MOVEGROUP, 4, 3);
    ok_long((LONG)Result, 0);
    SendMessageW(hWnd, LVM_GETGROUPINFOBYINDEX, 0, (LPARAM)&Group);
    ok_long(Group.iGroupId, 4);
    DestroyWindow(hWnd);
}

static void
TestMetrics(void)
{
    LVGROUPMETRICS Metrics;
    HWND hWnd = CreateList(LVS_REPORT);

    if (!hWnd)
        return;

    ZeroMemory(&Metrics, sizeof(Metrics));
    Metrics.cbSize = sizeof(Metrics);
    Metrics.mask = LVGMF_BORDERSIZE;
    SendMessageW(hWnd, LVM_GETGROUPMETRICS, 0, (LPARAM)&Metrics);
    ok_long((LONG)Metrics.Left, 0);
    ok_long((LONG)Metrics.Top, 12);
    ok_long((LONG)Metrics.Right, 0);
    ok_long((LONG)Metrics.Bottom, 0);

    Metrics.mask = LVGMF_BORDERSIZE;
    Metrics.Left = 3;
    Metrics.Top = 4;
    Metrics.Right = 5;
    Metrics.Bottom = 6;
    SendMessageW(hWnd, LVM_SETGROUPMETRICS, 0, (LPARAM)&Metrics);
    ZeroMemory(&Metrics, sizeof(Metrics));
    Metrics.cbSize = sizeof(Metrics);
    Metrics.mask = LVGMF_BORDERSIZE;
    SendMessageW(hWnd, LVM_GETGROUPMETRICS, 0, (LPARAM)&Metrics);
    ok_long((LONG)Metrics.Left, 3);
    ok_long((LONG)Metrics.Top, 4);
    ok_long((LONG)Metrics.Right, 5);
    ok_long((LONG)Metrics.Bottom, 6);
    DestroyWindow(hWnd);
}

static ULONG
CountInk(HWND hWnd, const RECT *Rect)
{
    BITMAPINFO Info = {{sizeof(BITMAPINFOHEADER), 300, -400, 1, 32, BI_RGB}};
    DWORD *Bits = NULL, Background;
    HBITMAP Bitmap;
    HDC Dc;
    ULONG Count = 0;
    LONG x, y;

    Dc = CreateCompatibleDC(NULL);
    Bitmap = CreateDIBSection(Dc, &Info, DIB_RGB_COLORS, (VOID **)&Bits, NULL, 0);
    if (!Bitmap)
    {
        DeleteDC(Dc);
        return 0;
    }
    SelectObject(Dc, Bitmap);
    SendMessageW(hWnd, WM_PRINTCLIENT, (WPARAM)Dc, PRF_CLIENT | PRF_ERASEBKGND);
    GdiFlush();
    Background = Bits[399 * 300 + 299] & 0xffffff;
    for (y = max(Rect->top, 0); y < min(Rect->bottom, 400); y++)
        for (x = max(Rect->left, 0); x < min(Rect->right, 300); x++)
            if ((Bits[y * 300 + x] & 0xffffff) != Background)
                Count++;
    DeleteObject(Bitmap);
    DeleteDC(Dc);
    return Count;
}

static void
TestPaint(void)
{
    RECT Header;
    HWND hWnd = CreateList(LVS_REPORT | LVS_NOCOLUMNHEADER);

    if (!hWnd)
        return;

    InsertGroup(hWnd, -1, 1, L"Painted header");
    InsertItem(hWnd, 0, 1, L"item");
    SendMessageW(hWnd, LVM_ENABLEGROUPVIEW, TRUE, 0);
    if (GroupRect(hWnd, 1, LVGGR_HEADER, &Header))
        ok(CountInk(hWnd, &Header) > 20, "the group header is painted\n");
    else
        ok(FALSE, "no header rect\n");
    DestroyWindow(hWnd);
}

START_TEST(ListViewGroups)
{
    InitCommonControls();

    TestEnable();
    TestInsertQuery();
    TestItems();
    TestLayout();
    TestHitTest();
    TestSort();
    TestMetrics();
    TestPaint();
}
