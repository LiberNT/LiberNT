/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test IsDialogMessage mnemonic matching with escaped ampersands
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return FALSE;
}

static HWND CreateLabelledEdit(HWND hDlg, LPCWSTR pszLabel, INT y, INT id)
{
    CreateWindowExW(0, L"STATIC", pszLabel, WS_CHILD | WS_VISIBLE | SS_LEFT,
                    10, y, 100, 20, hDlg, (HMENU)(INT_PTR)id, NULL, NULL);
    return CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER,
                           120, y, 100, 20, hDlg, (HMENU)(INT_PTR)(id + 1), NULL, NULL);
}

static BOOL SendDlgChar(HWND hDlg, HWND hwndTarget, WCHAR ch)
{
    MSG msg = { 0 };

    msg.hwnd = hwndTarget;
    msg.message = WM_CHAR;
    msg.wParam = ch;
    msg.lParam = 1;
    return IsDialogMessageW(hDlg, &msg);
}

START_TEST(DialogAccelerator)
{
    DWORD Buffer[8] = { 0 };
    DLGTEMPLATE *pTemplate = (DLGTEMPLATE *)Buffer;
    HWND hDlg, hButton, hEditGo, hEditHold, hEditRun;

    pTemplate->style = WS_POPUP | WS_CAPTION | WS_VISIBLE;
    pTemplate->cx = 200;
    pTemplate->cy = 120;
    hDlg = CreateDialogIndirectParamW(GetModuleHandleW(NULL), pTemplate, NULL, DlgProc, 0);
    ok(hDlg != NULL, "CreateDialogIndirectParamW failed, error %lu\n", GetLastError());
    if (!hDlg)
        return;

    hButton = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                              10, 100, 80, 20, hDlg, (HMENU)IDOK, NULL, NULL);
    hEditGo = CreateLabelledEdit(hDlg, L"A&&B &Go", 10, 100);
    hEditHold = CreateLabelledEdit(hDlg, L"&&Hold", 40, 110);
    hEditRun = CreateLabelledEdit(hDlg, L"&&&Run", 70, 120);
    ok(hButton != NULL && hEditGo != NULL && hEditHold != NULL && hEditRun != NULL,
       "control creation failed, error %lu\n", GetLastError());

    SetFocus(hButton);
    ok_int(SendDlgChar(hDlg, hButton, L'g'), TRUE);
    ok(GetFocus() == hEditGo, "focus is %p, expected %p\n", GetFocus(), hEditGo);

    SetFocus(hButton);
    ok_int(SendDlgChar(hDlg, hButton, L'b'), TRUE);
    ok(GetFocus() == hButton, "focus is %p, expected %p\n", GetFocus(), hButton);

    ok_int(SendDlgChar(hDlg, hButton, L'h'), TRUE);
    ok(GetFocus() == hButton, "focus is %p, expected %p\n", GetFocus(), hButton);

    ok_int(SendDlgChar(hDlg, hButton, L'R'), TRUE);
    ok(GetFocus() == hEditRun, "focus is %p, expected %p\n", GetFocus(), hEditRun);

    DestroyWindow(hDlg);
}
