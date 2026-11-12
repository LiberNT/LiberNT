/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test PSM_RECALCPAGESIZES after a larger page is added to a property sheet
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wine/test.h"
#include <windows.h>
#include <commctrl.h>
#include <prsht.h>

typedef struct _PAGE_TEMPLATE
{
    DLGTEMPLATE Header;
    WORD Menu;
    WORD Class;
    WORD Title;
    WORD Padding;
} PAGE_TEMPLATE;

static INT_PTR CALLBACK PageProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return FALSE;
}

static void InitTemplate(PAGE_TEMPLATE *Template, short Width, short Height)
{
    ZeroMemory(Template, sizeof(*Template));
    Template->Header.style = WS_CHILD | WS_DISABLED | WS_CAPTION | DS_3DLOOK;
    Template->Header.cx = Width;
    Template->Header.cy = Height;
}

static void InitPage(PROPSHEETPAGEW *Page, PAGE_TEMPLATE *Template)
{
    ZeroMemory(Page, sizeof(*Page));
    Page->dwSize = sizeof(*Page);
    Page->dwFlags = PSP_DLGINDIRECT;
    Page->hInstance = GetModuleHandleW(NULL);
    Page->pResource = &Template->Header;
    Page->pfnDlgProc = PageProc;
}

static void PumpMessages(void)
{
    MSG Msg;

    while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Msg);
        DispatchMessageW(&Msg);
    }
}

START_TEST(PropSheetRecalc)
{
    PAGE_TEMPLATE SmallTemplate, LargeTemplate;
    PROPSHEETPAGEW SmallPage, LargePage;
    PROPSHEETHEADERW Header;
    HPROPSHEETPAGE LargeHandle;
    HWND Sheet, Tab;
    RECT Display, Needed;
    LRESULT Result;

    InitTemplate(&SmallTemplate, 100, 50);
    InitTemplate(&LargeTemplate, 300, 200);
    InitPage(&SmallPage, &SmallTemplate);
    InitPage(&LargePage, &LargeTemplate);

    ZeroMemory(&Header, sizeof(Header));
    Header.dwSize = sizeof(Header);
    Header.dwFlags = PSH_MODELESS | PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW;
    Header.hInstance = GetModuleHandleW(NULL);
    Header.pszCaption = L"PropSheetRecalc";
    Header.nPages = 1;
    Header.ppsp = &SmallPage;

    Sheet = (HWND)PropertySheetW(&Header);
    ok(Sheet != NULL && IsWindow(Sheet), "The modeless property sheet was not created\n");
    if (!Sheet || !IsWindow(Sheet))
        return;
    PumpMessages();

    LargeHandle = CreatePropertySheetPageW(&LargePage);
    ok(LargeHandle != NULL, "The large page was not created\n");
    ok(PropSheet_AddPage(Sheet, LargeHandle), "The large page was not added\n");

    Result = SendMessageW(Sheet, PSM_RECALCPAGESIZES, 0, 0);
    ok(Result == TRUE, "PSM_RECALCPAGESIZES returned %Id\n", Result);
    PumpMessages();

    Tab = PropSheet_GetTabControl(Sheet);
    ok(Tab != NULL, "The sheet has no tab control\n");
    GetClientRect(Tab, &Display);
    TabCtrl_AdjustRect(Tab, FALSE, &Display);
    SetRect(&Needed, 0, 0, LargeTemplate.Header.cx, LargeTemplate.Header.cy);
    MapDialogRect(Sheet, &Needed);
    ok(Display.right - Display.left >= Needed.right && Display.bottom - Display.top >= Needed.bottom,
       "The page area is %ldx%ld, the large page needs %ldx%ld\n", Display.right - Display.left,
       Display.bottom - Display.top, Needed.right, Needed.bottom);

    DestroyWindow(Sheet);
    PumpMessages();
}
