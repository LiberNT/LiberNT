/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test Ctrl+Tab in a dialog with a tab control, with and without a dialog message filter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wine/test.h"
#include <windows.h>
#include <commctrl.h>

static UINT g_SelChange;
static HWND g_hMain, g_hTab;
static HHOOK g_hHook;
static INT g_LastFilterCode;

static INT_PTR CALLBACK MainDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_NOTIFY && ((LPNMHDR)lParam)->code == TCN_SELCHANGE)
        g_SelChange++;
    return FALSE;
}

static INT_PTR CALLBACK PageDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return FALSE;
}

static LRESULT CALLBACK MsgFilterProc(INT nCode, WPARAM wParam, LPARAM lParam)
{
    LPMSG pMsg = (LPMSG)lParam;
    INT nCount;

    if (nCode >= 0)
        g_LastFilterCode = nCode;

    if (nCode == MSGF_DIALOGBOX && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_TAB &&
        (GetKeyState(VK_CONTROL) & 0x8000) &&
        (pMsg->hwnd == g_hMain || IsChild(g_hMain, pMsg->hwnd)) &&
        (nCount = TabCtrl_GetItemCount(g_hTab)) > 0)
    {
        INT nIndex = TabCtrl_GetCurSel(g_hTab) + ((GetKeyState(VK_SHIFT) & 0x8000) ? nCount - 1 : 1);
        TabCtrl_SetCurFocus(g_hTab, nIndex % nCount);
        return TRUE;
    }

    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

static HWND CreateEmptyDialog(HWND hwndParent, DWORD dwStyle, DLGPROC DlgProc)
{
    DWORD Buffer[8] = { 0 };
    DLGTEMPLATE *pTemplate = (DLGTEMPLATE *)Buffer;

    pTemplate->style = dwStyle;
    pTemplate->cx = 200;
    pTemplate->cy = 140;
    return CreateDialogIndirectParamW(GetModuleHandleW(NULL), pTemplate, hwndParent, DlgProc, 0);
}

static BOOL SendDlgKey(HWND hDlg, HWND hwndTarget, UINT uKey, BOOL bControl, BOOL bShift)
{
    BYTE SavedState[256], KeyState[256];
    MSG msg = { 0 };
    BOOL bRet;

    GetKeyboardState(SavedState);
    CopyMemory(KeyState, SavedState, sizeof(KeyState));
    KeyState[VK_CONTROL] = bControl ? 0x80 : 0;
    KeyState[VK_LCONTROL] = bControl ? 0x80 : 0;
    KeyState[VK_RCONTROL] = 0;
    KeyState[VK_SHIFT] = bShift ? 0x80 : 0;
    KeyState[VK_LSHIFT] = bShift ? 0x80 : 0;
    KeyState[VK_RSHIFT] = 0;
    SetKeyboardState(KeyState);

    msg.hwnd = hwndTarget;
    msg.message = WM_KEYDOWN;
    msg.wParam = uKey;
    msg.lParam = (MapVirtualKeyW(uKey, MAPVK_VK_TO_VSC) << 16) | 1;
    bRet = IsDialogMessageW(hDlg, &msg);

    SetKeyboardState(SavedState);
    return bRet;
}

START_TEST(TabControlKeys)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES };
    TCITEMW Item = { 0 };
    HWND hPage, hRadio, hButton;
    INT i;

    InitCommonControlsEx(&icc);

    g_hMain = CreateEmptyDialog(NULL, WS_POPUP | WS_CAPTION | WS_VISIBLE, MainDlgProc);
    ok(g_hMain != NULL, "CreateDialogIndirectParamW failed, error %lu\n", GetLastError());
    if (!g_hMain)
        return;

    g_hTab = CreateWindowExW(0, WC_TABCONTROLW, NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             0, 0, 300, 220, g_hMain, (HMENU)100, NULL, NULL);
    ok(g_hTab != NULL, "tab control creation failed, error %lu\n", GetLastError());

    Item.mask = TCIF_TEXT;
    for (i = 0; i < 3; i++)
    {
        Item.pszText = (LPWSTR)L"Page";
        ok_int((INT)SendMessageW(g_hTab, TCM_INSERTITEMW, i, (LPARAM)&Item), i);
    }

    hPage = CreateEmptyDialog(g_hMain, WS_CHILD | WS_VISIBLE | DS_CONTROL, PageDlgProc);
    ok(hPage != NULL, "page creation failed, error %lu\n", GetLastError());
    SetWindowPos(hPage, HWND_TOP, 10, 30, 280, 180, 0);

    hRadio = CreateWindowExW(0, L"BUTTON", L"One", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
                             10, 10, 100, 20, hPage, (HMENU)200, NULL, NULL);
    hButton = CreateWindowExW(0, L"BUTTON", L"Two", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_PUSHBUTTON,
                              10, 40, 100, 20, hPage, (HMENU)201, NULL, NULL);
    ok(hRadio != NULL && hButton != NULL, "button creation failed, error %lu\n", GetLastError());

    ok_int(TabCtrl_GetCurSel(g_hTab), 0);
    g_SelChange = 0;

    SetFocus(hRadio);
    ok(GetFocus() == hRadio, "focus is %p, expected %p\n", GetFocus(), hRadio);
    ok_int(SendDlgKey(g_hMain, hRadio, VK_TAB, TRUE, FALSE), TRUE);
    ok_int(TabCtrl_GetCurSel(g_hTab), 0);
    ok(GetFocus() == hButton, "focus is %p, expected %p\n", GetFocus(), hButton);

    SetFocus(hRadio);
    ok_int(SendDlgKey(g_hMain, hRadio, VK_TAB, TRUE, TRUE), TRUE);
    ok_int(TabCtrl_GetCurSel(g_hTab), 0);
    ok(GetFocus() == g_hTab, "focus is %p, expected %p\n", GetFocus(), g_hTab);

    SetFocus(g_hTab);
    ok_int(SendDlgKey(g_hMain, g_hTab, VK_TAB, TRUE, FALSE), TRUE);
    ok_int(TabCtrl_GetCurSel(g_hTab), 0);
    ok(GetFocus() == hRadio, "focus is %p, expected %p\n", GetFocus(), hRadio);
    ok_int(g_SelChange, 0);

    g_hHook = SetWindowsHookExW(WH_MSGFILTER, MsgFilterProc, NULL, GetCurrentThreadId());
    ok(g_hHook != NULL, "SetWindowsHookExW failed, error %lu\n", GetLastError());

    SetFocus(hRadio);
    g_LastFilterCode = -1;
    ok_int(SendDlgKey(g_hMain, hRadio, VK_TAB, TRUE, FALSE), TRUE);
    ok_int(g_LastFilterCode, MSGF_DIALOGBOX);
    ok_int(TabCtrl_GetCurSel(g_hTab), 1);
    ok(GetFocus() == hRadio, "focus is %p, expected %p\n", GetFocus(), hRadio);
    ok_int(g_SelChange, 1);

    ok_int(SendDlgKey(g_hMain, hRadio, VK_TAB, TRUE, TRUE), TRUE);
    ok_int(TabCtrl_GetCurSel(g_hTab), 0);
    ok_int(SendDlgKey(g_hMain, hRadio, VK_TAB, TRUE, TRUE), TRUE);
    ok_int(TabCtrl_GetCurSel(g_hTab), 2);
    ok(GetFocus() == hRadio, "focus is %p, expected %p\n", GetFocus(), hRadio);
    ok_int(g_SelChange, 3);

    ok_int(SendDlgKey(g_hMain, hRadio, VK_TAB, FALSE, FALSE), TRUE);
    ok_int(TabCtrl_GetCurSel(g_hTab), 2);
    ok(GetFocus() == hButton, "focus is %p, expected %p\n", GetFocus(), hButton);
    ok_int(g_SelChange, 3);

    if (g_hHook)
        UnhookWindowsHookEx(g_hHook);
    DestroyWindow(g_hMain);
}
