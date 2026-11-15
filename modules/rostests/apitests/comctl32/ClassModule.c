/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the window classes of comctl32 belong to comctl32
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>
#include <commctrl.h>

START_TEST(ClassModule)
{
    static const LPCWSTR s_apszClasses[] =
    {
        WC_EDITW, WC_BUTTONW, WC_STATICW, WC_COMBOBOXW, WC_LISTBOXW, WC_LISTVIEWW, WC_TREEVIEWW,
        TOOLBARCLASSNAMEW, STATUSCLASSNAMEW, PROGRESS_CLASSW, WC_TABCONTROLW,
    };
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    HMODULE hComctl = NULL;
    UINT i;

    InitCommonControlsEx(&icc);
    ok(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)InitCommonControlsEx, &hComctl),
       "GetModuleHandleExW failed with error %lu\n", GetLastError());

    for (i = 0; i < ARRAYSIZE(s_apszClasses); i++)
    {
        HWND hwnd = CreateWindowExW(0, s_apszClasses[i], L"", WS_POPUP, 0, 0, 100, 20, NULL, NULL, NULL, NULL);

        ok(hwnd != NULL, "%ls: CreateWindowExW failed with error %lu\n", s_apszClasses[i], GetLastError());
        if (!hwnd)
            continue;
        ok((HMODULE)GetClassLongPtrW(hwnd, GCLP_HMODULE) == hComctl, "%ls: class module %p, comctl32 %p\n",
           s_apszClasses[i], (HMODULE)GetClassLongPtrW(hwnd, GCLP_HMODULE), hComctl);
        DestroyWindow(hwnd);
    }
}
