/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that WM_SETREDRAW on a list view also hides and shows its header
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <commctrl.h>

static WNDPROC OldProc;
static INT EraseCount;
static INT NcPaintCount;

static
LRESULT
CALLBACK
CountProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    if (Message == WM_ERASEBKGND)
        EraseCount++;
    else if (Message == WM_NCPAINT)
        NcPaintCount++;
    return CallWindowProcW(OldProc, Window, Message, wParam, lParam);
}

static
VOID
Pump(VOID)
{
    MSG Msg;

    while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&Msg);
}

static
BOOL
HasVisibleStyle(
    _In_ HWND Window)
{
    return (GetWindowLongW(Window, GWL_STYLE) & WS_VISIBLE) != 0;
}

static
HWND
CreateDetailsList(
    _In_ HWND Parent)
{
    LVCOLUMNW Column = { LVCF_WIDTH | LVCF_TEXT, 0, 100, (LPWSTR)L"Name" };
    LVITEMW Item = { LVIF_TEXT, 0, 0, 0, 0, (LPWSTR)L"item" };
    HWND List;

    List = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT,
                           0, 0, 300, 200, Parent, NULL, NULL, NULL);
    ok(List != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (List == NULL)
        return NULL;
    ok(SendMessageW(List, LVM_INSERTCOLUMNW, 0, (LPARAM)&Column) == 0, "LVM_INSERTCOLUMNW failed\n");
    ok(SendMessageW(List, LVM_INSERTITEMW, 0, (LPARAM)&Item) == 0, "LVM_INSERTITEMW failed\n");
    UpdateWindow(Parent);
    Pump();
    return List;
}

START_TEST(ListViewRedrawHeader)
{
    INITCOMMONCONTROLSEX Init = { sizeof(Init), ICC_LISTVIEW_CLASSES };
    HWND Parent, List, Header;

    InitCommonControlsEx(&Init);
    Parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             0, 0, 400, 300, NULL, NULL, NULL, NULL);
    ok(Parent != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (Parent == NULL)
        return;

    List = CreateDetailsList(Parent);
    if (List == NULL)
        goto Cleanup;
    Header = (HWND)SendMessageW(List, LVM_GETHEADER, 0, 0);
    ok(Header != NULL, "no header\n");
    ok(HasVisibleStyle(List), "list view not visible\n");
    ok(HasVisibleStyle(Header), "header not visible in details view\n");

    SendMessageW(List, WM_SETREDRAW, FALSE, 0);
    ok(HasVisibleStyle(List), "redraw off cleared the list view's WS_VISIBLE\n");
    ok(!HasVisibleStyle(Header), "redraw off left the header visible\n");
    SendMessageW(List, WM_SETREDRAW, TRUE, 0);
    ok(HasVisibleStyle(List), "redraw on cleared the list view's WS_VISIBLE\n");
    ok(HasVisibleStyle(Header), "redraw on left the header hidden in details view\n");

    SendMessageW(List, WM_SETREDRAW, FALSE, 0);
    OldProc = (WNDPROC)SetWindowLongPtrW(List, GWLP_WNDPROC, (LONG_PTR)CountProc);
    EraseCount = NcPaintCount = 0;
    ok(SendMessageW(List, LVM_SETVIEW, LV_VIEW_LIST, 0) == 1, "LVM_SETVIEW list failed\n");
    ok(EraseCount == 0, "leaving details view with redraw off sent %d WM_ERASEBKGND\n", EraseCount);
    ok(NcPaintCount == 0, "leaving details view with redraw off sent %d WM_NCPAINT\n", NcPaintCount);
    SetWindowLongPtrW(List, GWLP_WNDPROC, (LONG_PTR)OldProc);
    ok(!HasVisibleStyle(Header), "header visible in list view with redraw off\n");
    SendMessageW(List, WM_SETREDRAW, TRUE, 0);
    ok(!HasVisibleStyle(Header), "redraw on showed the header in list view\n");

    SendMessageW(List, WM_SETREDRAW, FALSE, 0);
    ok(SendMessageW(List, LVM_SETVIEW, LV_VIEW_DETAILS, 0) == 1, "LVM_SETVIEW details failed\n");
    ok(!HasVisibleStyle(Header), "entering details view with redraw off showed the header\n");
    SendMessageW(List, WM_SETREDRAW, TRUE, 0);
    ok(HasVisibleStyle(Header), "redraw on left the header hidden in details view\n");

    DestroyWindow(List);
    List = CreateDetailsList(Parent);
    if (List == NULL)
        goto Cleanup;
    OldProc = (WNDPROC)SetWindowLongPtrW(List, GWLP_WNDPROC, (LONG_PTR)CountProc);
    EraseCount = 0;
    ok(SendMessageW(List, LVM_SETVIEW, LV_VIEW_LIST, 0) == 1, "LVM_SETVIEW list failed\n");
    ok(EraseCount == 1, "leaving details view with redraw on sent %d WM_ERASEBKGND, expected 1\n", EraseCount);
    SetWindowLongPtrW(List, GWLP_WNDPROC, (LONG_PTR)OldProc);

Cleanup:
    DestroyWindow(Parent);
}
