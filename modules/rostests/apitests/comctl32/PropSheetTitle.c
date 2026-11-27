/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the window title of property sheets and wizards, PSH_PROPTITLE included
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wine/test.h"
#include <windows.h>
#include <commctrl.h>
#include <prsht.h>

typedef struct _TITLE_TEMPLATE
{
    DLGTEMPLATE Header;
    WORD Data[32];
} TITLE_TEMPLATE;

static TITLE_TEMPLATE Plain, Captioned;

static INT_PTR CALLBACK PageProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return FALSE;
}

static void InitTemplate(TITLE_TEMPLATE *Template, const WCHAR *Caption)
{
    WORD *Data = Template->Data;

    ZeroMemory(Template, sizeof(*Template));
    Template->Header.style = WS_CHILD | DS_CONTROL | (Caption ? WS_CAPTION : 0);
    Template->Header.cx = 200;
    Template->Header.cy = 100;
    *Data++ = 0;
    *Data++ = 0;
    if (Caption)
        lstrcpyW((WCHAR *)Data, Caption);
}

static void CheckTitle(int Line, DWORD Flags, TITLE_TEMPLATE *Template, const WCHAR *Caption,
                       const WCHAR *PageTitle, const WCHAR *Expected)
{
    PROPSHEETPAGEW Page = { sizeof(Page) };
    PROPSHEETHEADERW Header = { sizeof(Header) };
    HPROPSHEETPAGE PageHandle;
    WCHAR Title[128] = L"";
    HWND Sheet;

    Page.dwFlags = PSP_DLGINDIRECT | (PageTitle ? PSP_USETITLE : 0);
    Page.pResource = &Template->Header;
    Page.pfnDlgProc = PageProc;
    Page.pszTitle = PageTitle;
    PageHandle = CreatePropertySheetPageW(&Page);
    ok_(__FILE__, Line)(PageHandle != NULL, "CreatePropertySheetPageW failed\n");
    if (!PageHandle)
        return;

    Header.dwFlags = Flags | PSH_MODELESS;
    Header.hInstance = GetModuleHandleW(NULL);
    Header.pszCaption = Caption;
    Header.nPages = 1;
    Header.phpage = &PageHandle;
    Sheet = (HWND)PropertySheetW(&Header);
    ok_(__FILE__, Line)((INT_PTR)Sheet > 0, "PropertySheetW returned %p\n", Sheet);
    if ((INT_PTR)Sheet <= 0)
        return;

    GetWindowTextW(Sheet, Title, ARRAYSIZE(Title));
    ok_(__FILE__, Line)(!lstrcmpW(Title, Expected), "flags %#lx: title %s, expected %s\n",
                        Flags, wine_dbgstr_w(Title), wine_dbgstr_w(Expected));
    DestroyWindow(Sheet);
}

START_TEST(PropSheetTitle)
{
    InitTemplate(&Plain, NULL);
    InitTemplate(&Captioned, L"Template Caption");

    CheckTitle(__LINE__, PSH_WIZARD, &Plain, L"Probe Setup", NULL, L"");
    CheckTitle(__LINE__, PSH_WIZARD97, &Plain, L"Probe Setup", L"Page Title", L"Page Title");
    CheckTitle(__LINE__, PSH_WIZARD, &Captioned, L"Probe Setup", NULL, L"Template Caption");
    CheckTitle(__LINE__, PSH_WIZARD, &Captioned, NULL, NULL, L"Template Caption");
    CheckTitle(__LINE__, PSH_WIZARD | PSH_PROPTITLE, &Captioned, L"Probe Setup", NULL, L"Template Caption");
    CheckTitle(__LINE__, PSH_WIZARD | PSH_PROPTITLE | PSH_NOAPPLYNOW | PSH_NOCONTEXTHELP, &Captioned, L"Probe Setup", NULL, L"Template Caption");
    CheckTitle(__LINE__, PSH_WIZARD97 | PSH_PROPTITLE, &Captioned, L"Probe Setup", NULL, L"Template Caption");
    CheckTitle(__LINE__, PSH_WIZARD | PSH_PROPTITLE, &Plain, L"Probe Setup", NULL, L"");
    CheckTitle(__LINE__, 0, &Captioned, L"Probe Setup", NULL, L"Probe Setup");

    if (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_ENGLISH)
        CheckTitle(__LINE__, PSH_PROPTITLE, &Plain, L"Probe", NULL, L"Probe Properties");
    else
        skip("PSH_PROPTITLE text is checked on English user interfaces only\n");
}
