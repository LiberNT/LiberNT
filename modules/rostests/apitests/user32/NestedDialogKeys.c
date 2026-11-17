/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which dialog gets the Enter and Escape of a control in a child dialog
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"

#define ID_RADIO_NOW    1201
#define ID_RADIO_LATER  1202

static WPARAM g_LastCommand;
static HWND g_LastCommandWindow;

static INT_PTR CALLBACK OuterProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_COMMAND)
    {
        g_LastCommand = wParam;
        g_LastCommandWindow = (HWND)lParam;
        return TRUE;
    }
    return FALSE;
}

static WPARAM g_InnerCommand;

static INT_PTR CALLBACK InnerProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_COMMAND)
        g_InnerCommand = wParam;
    return FALSE;
}

static HWND CreateTemplateDialog(DWORD Style, DWORD ExStyle, HWND hParent, DLGPROC Proc)
{
    DWORD Buffer[16] = { 0 };
    DLGTEMPLATE *pTemplate = (DLGTEMPLATE *)Buffer;

    pTemplate->style = Style;
    pTemplate->dwExtendedStyle = ExStyle;
    pTemplate->cx = 200;
    pTemplate->cy = 120;
    return CreateDialogIndirectParamW(GetModuleHandleW(NULL), pTemplate, hParent, Proc, 0);
}

static BOOL SendDlgKey(HWND hDlg, UINT Message, WPARAM wParam)
{
    MSG msg = { 0 };

    msg.hwnd = GetFocus();
    msg.message = Message;
    msg.wParam = wParam;
    msg.lParam = 1;
    return IsDialogMessageW(hDlg, &msg);
}

static void TestNested(DWORD InnerStyle, DWORD InnerExStyle, BOOL ClearControlParent, BOOL ToOuter, PCSTR Name)
{
    HWND hOuter, hInner, hFinish, hNow, hLater;

    hOuter = CreateTemplateDialog(WS_POPUP | WS_CAPTION | WS_VISIBLE | WS_SYSMENU, 0, NULL, OuterProc);
    ok(hOuter != NULL, "%s: outer dialog creation failed, error %lu\n", Name, GetLastError());
    if (!hOuter)
        return;
    hFinish = CreateWindowExW(0, L"BUTTON", L"&Finish", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                              10, 150, 80, 24, hOuter, (HMENU)IDOK, NULL, NULL);
    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    100, 150, 80, 24, hOuter, (HMENU)IDCANCEL, NULL, NULL);
    hInner = CreateTemplateDialog(InnerStyle, InnerExStyle, hOuter, InnerProc);
    ok(hInner != NULL, "%s: inner dialog creation failed, error %lu\n", Name, GetLastError());
    if (ClearControlParent)
        SetWindowLongPtrW(hInner, GWL_EXSTYLE, GetWindowLongPtrW(hInner, GWL_EXSTYLE) & ~WS_EX_CONTROLPARENT);
    hNow = CreateWindowExW(0, L"BUTTON", L"Reboot &now", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                           10, 10, 150, 20, hInner, (HMENU)ID_RADIO_NOW, NULL, NULL);
    hLater = CreateWindowExW(0, L"BUTTON", L"Reboot &later", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                             10, 40, 150, 20, hInner, (HMENU)ID_RADIO_LATER, NULL, NULL);
    SendMessageW(hNow, BM_SETCHECK, BST_CHECKED, 0);
    SetForegroundWindow(hOuter);

    SetFocus(hNow);
    g_LastCommand = g_InnerCommand = 0;
    ok_int(SendDlgKey(hInner, WM_KEYDOWN, VK_RETURN), TRUE);
    ok(g_LastCommand == (ToOuter ? MAKEWPARAM(IDOK, BN_CLICKED) : 0), "%s: return sent 0x%Ix to the outer dialog\n",
       Name, g_LastCommand);
    ok(g_InnerCommand == (ToOuter ? 0 : IDOK), "%s: return sent 0x%Ix to the inner dialog\n", Name, g_InnerCommand);
    if (ToOuter)
        ok(g_LastCommandWindow == hFinish, "%s: return came from %p, expected %p\n", Name, g_LastCommandWindow, hFinish);

    SetFocus(hLater);
    g_LastCommand = g_InnerCommand = 0;
    ok_int(SendDlgKey(hInner, WM_KEYDOWN, VK_ESCAPE), TRUE);
    ok(g_LastCommand == (ToOuter ? IDCANCEL : 0), "%s: escape sent 0x%Ix to the outer dialog\n", Name, g_LastCommand);
    ok(g_InnerCommand == (ToOuter ? 0 : IDCANCEL), "%s: escape sent 0x%Ix to the inner dialog\n", Name, g_InnerCommand);

    SetFocus(hLater);
    g_LastCommand = g_InnerCommand = 0;
    ok_int(SendDlgKey(hOuter, WM_KEYDOWN, VK_RETURN), TRUE);
    ok(g_LastCommand == MAKEWPARAM(IDOK, BN_CLICKED), "%s: return through the outer dialog sent 0x%Ix\n", Name,
       g_LastCommand);

    DestroyWindow(hOuter);
}

START_TEST(NestedDialogKeys)
{
    TestNested(WS_CHILD | WS_VISIBLE | DS_CONTROL, WS_EX_CONTROLPARENT, FALSE, TRUE, "control+controlparent");
    TestNested(WS_CHILD | WS_VISIBLE | DS_CONTROL, 0, FALSE, TRUE, "control");
    TestNested(WS_CHILD | WS_VISIBLE | DS_CONTROL, 0, TRUE, FALSE, "control without controlparent");
    TestNested(WS_CHILD | WS_VISIBLE, WS_EX_CONTROLPARENT, FALSE, TRUE, "controlparent");
    TestNested(WS_CHILD | WS_VISIBLE, 0, FALSE, FALSE, "plain");
}
