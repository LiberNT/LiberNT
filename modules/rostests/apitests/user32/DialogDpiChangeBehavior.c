/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the dialog DPI change behavior functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_SET_BEHAVIOR)(HWND, UINT, UINT);
typedef UINT (WINAPI *PFN_GET_BEHAVIOR)(HWND);

static PFN_SET_BEHAVIOR pSetDialogControlDpiChangeBehavior;
static PFN_GET_BEHAVIOR pGetDialogControlDpiChangeBehavior;
static PFN_SET_BEHAVIOR pSetDialogDpiChangeBehavior;
static PFN_GET_BEHAVIOR pGetDialogDpiChangeBehavior;

static INT_PTR CALLBACK
EmptyDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return FALSE;
}

static HWND
CreateTestDialog(void)
{
    static WORD Template[64];
    DLGTEMPLATE *Dialog = (DLGTEMPLATE *)Template;
    DLGITEMTEMPLATE *Item;
    WORD *p;

    ZeroMemory(Template, sizeof(Template));
    Dialog->style = WS_POPUP | WS_CAPTION | DS_MODALFRAME;
    Dialog->cdit = 1;
    Dialog->cx = 100;
    Dialog->cy = 50;
    p = (WORD *)(Dialog + 1);
    *p++ = 0;
    *p++ = 0;
    *p++ = 0;
    p = (WORD *)(((ULONG_PTR)p + 3) & ~3);
    Item = (DLGITEMTEMPLATE *)p;
    Item->style = WS_CHILD | WS_VISIBLE;
    Item->x = 5;
    Item->y = 5;
    Item->cx = 40;
    Item->cy = 10;
    Item->id = 100;
    p = (WORD *)(Item + 1);
    *p++ = 0xFFFF;
    *p++ = 0x0082;
    *p++ = 0;
    *p++ = 0;
    return CreateDialogIndirectParamW(GetModuleHandleW(NULL), Dialog, NULL, EmptyDlgProc, 0);
}

static void
TestControlBehavior(void)
{
    HWND hWnd;
    UINT Value;
    BOOL Ret;

    hWnd = CreateWindowExW(0, L"STATIC", L"test", WS_OVERLAPPED, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd)
        return;

    SetLastError(0xdeadbeef);
    Value = pGetDialogControlDpiChangeBehavior(hWnd);
    ok(Value == 0, "Expected 0, got %u\n", Value);
    ok(GetLastError() == 0xdeadbeef, "Last error changed to %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pSetDialogControlDpiChangeBehavior(hWnd, DCDC_DISABLE_FONT_UPDATE, DCDC_DISABLE_FONT_UPDATE);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    ok(GetLastError() == 0xdeadbeef, "Last error changed to %lu\n", GetLastError());
    ok(pGetDialogControlDpiChangeBehavior(hWnd) == DCDC_DISABLE_FONT_UPDATE, "Wrong flags\n");

    Ret = pSetDialogControlDpiChangeBehavior(hWnd, DCDC_DISABLE_RELAYOUT, DCDC_DISABLE_FONT_UPDATE | DCDC_DISABLE_RELAYOUT);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    Value = pGetDialogControlDpiChangeBehavior(hWnd);
    ok(Value == (DCDC_DISABLE_FONT_UPDATE | DCDC_DISABLE_RELAYOUT), "Expected 3, got %u\n", Value);

    Ret = pSetDialogControlDpiChangeBehavior(hWnd, DCDC_DISABLE_FONT_UPDATE, 0);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    Value = pGetDialogControlDpiChangeBehavior(hWnd);
    ok(Value == DCDC_DISABLE_RELAYOUT, "Expected 2, got %u\n", Value);

    SetLastError(0xdeadbeef);
    Ret = pSetDialogControlDpiChangeBehavior(hWnd, 0x8, 0x8);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    ok(GetLastError() == 0xdeadbeef, "Last error changed to %lu\n", GetLastError());
    Value = pGetDialogControlDpiChangeBehavior(hWnd);
    ok(Value == DCDC_DISABLE_RELAYOUT, "Expected 2, got %u\n", Value);

    Ret = pSetDialogControlDpiChangeBehavior(hWnd, 0xFFFFFFFF, 0xFFFFFFFF);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    Value = pGetDialogControlDpiChangeBehavior(hWnd);
    ok(Value == (DCDC_DISABLE_FONT_UPDATE | DCDC_DISABLE_RELAYOUT), "Expected 3, got %u\n", Value);

    Ret = pSetDialogControlDpiChangeBehavior(hWnd, 0, 0);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    Ret = pSetDialogControlDpiChangeBehavior(hWnd, 0xFFFFFFFF, 0);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    ok(pGetDialogControlDpiChangeBehavior(hWnd) == 0, "Flags were not cleared\n");

    SetLastError(0xdeadbeef);
    Value = pGetDialogDpiChangeBehavior(hWnd);
    ok(Value == 0, "Expected 0, got %u\n", Value);
    ok(GetLastError() == ERROR_WINDOW_NOT_DIALOG, "Expected ERROR_WINDOW_NOT_DIALOG, got %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pSetDialogDpiChangeBehavior(hWnd, DDC_DISABLE_ALL, DDC_DISABLE_ALL);
    ok(Ret == FALSE, "Expected FALSE, got %d\n", Ret);
    ok(GetLastError() == ERROR_WINDOW_NOT_DIALOG, "Expected ERROR_WINDOW_NOT_DIALOG, got %lu\n", GetLastError());

    DestroyWindow(hWnd);

    SetLastError(0xdeadbeef);
    Value = pGetDialogControlDpiChangeBehavior(hWnd);
    ok(Value == 0, "Expected 0, got %u\n", Value);
    ok(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "Expected ERROR_INVALID_WINDOW_HANDLE, got %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pSetDialogControlDpiChangeBehavior(hWnd, DCDC_DISABLE_FONT_UPDATE, DCDC_DISABLE_FONT_UPDATE);
    ok(Ret == FALSE, "Expected FALSE, got %d\n", Ret);
    ok(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "Expected ERROR_INVALID_WINDOW_HANDLE, got %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = pSetDialogControlDpiChangeBehavior(NULL, DCDC_DISABLE_FONT_UPDATE, DCDC_DISABLE_FONT_UPDATE);
    ok(Ret == FALSE, "Expected FALSE, got %d\n", Ret);
    ok(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "Expected ERROR_INVALID_WINDOW_HANDLE, got %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Value = pGetDialogControlDpiChangeBehavior(NULL);
    ok(Value == 0, "Expected 0, got %u\n", Value);
    ok(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "Expected ERROR_INVALID_WINDOW_HANDLE, got %lu\n", GetLastError());
}

static void
TestDialogBehavior(void)
{
    HWND hDlg, hChild;
    UINT Value;
    BOOL Ret;

    hDlg = CreateTestDialog();
    ok(hDlg != NULL, "CreateDialogIndirectParamW failed: %lu\n", GetLastError());
    if (!hDlg)
        return;
    hChild = GetDlgItem(hDlg, 100);
    ok(hChild != NULL, "The dialog has no child\n");

    SetLastError(0xdeadbeef);
    Value = pGetDialogDpiChangeBehavior(hDlg);
    ok(Value == 0, "Expected 0, got %u\n", Value);
    ok(GetLastError() == 0xdeadbeef, "Last error changed to %lu\n", GetLastError());

    Ret = pSetDialogDpiChangeBehavior(hDlg, DDC_DISABLE_ALL, DDC_DISABLE_ALL);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    ok(pGetDialogDpiChangeBehavior(hDlg) == DDC_DISABLE_ALL, "Wrong flags\n");

    Ret = pSetDialogDpiChangeBehavior(hDlg, DDC_DISABLE_RESIZE | DDC_DISABLE_CONTROL_RELAYOUT, 0x7);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    Value = pGetDialogDpiChangeBehavior(hDlg);
    ok(Value == 7, "Expected 7, got %u\n", Value);

    Ret = pSetDialogDpiChangeBehavior(hDlg, 0x8, 0x8);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    Value = pGetDialogDpiChangeBehavior(hDlg);
    ok(Value == 7, "Expected 7, got %u\n", Value);

    Ret = pSetDialogDpiChangeBehavior(hDlg, 0xFFFFFFFF, 0);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    ok(pGetDialogDpiChangeBehavior(hDlg) == 0, "Flags were not cleared\n");

    ok(pGetDialogControlDpiChangeBehavior(hDlg) == 0, "The dialog has control flags\n");
    Ret = pSetDialogControlDpiChangeBehavior(hDlg, 3, 3);
    ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
    ok(pGetDialogDpiChangeBehavior(hDlg) == 0, "Control flags changed the dialog flags\n");
    ok(pGetDialogControlDpiChangeBehavior(hDlg) == 3, "Wrong control flags on the dialog\n");

    if (hChild)
    {
        Ret = pSetDialogControlDpiChangeBehavior(hChild, 3, 2);
        ok(Ret == TRUE, "Expected TRUE, got %d\n", Ret);
        ok(pGetDialogControlDpiChangeBehavior(hChild) == 2, "Wrong control flags on the child\n");

        SetLastError(0xdeadbeef);
        Ret = pSetDialogDpiChangeBehavior(hChild, DDC_DISABLE_ALL, DDC_DISABLE_ALL);
        ok(Ret == FALSE, "Expected FALSE, got %d\n", Ret);
        ok(GetLastError() == ERROR_WINDOW_NOT_DIALOG, "Expected ERROR_WINDOW_NOT_DIALOG, got %lu\n", GetLastError());
    }

    DestroyWindow(hDlg);
}

static void
TestOtherProcess(void)
{
    HWND hDesktop = GetDesktopWindow();
    HWND hTray = FindWindowW(L"Shell_TrayWnd", NULL);
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = pSetDialogControlDpiChangeBehavior(hDesktop, DCDC_DISABLE_FONT_UPDATE, DCDC_DISABLE_FONT_UPDATE);
    ok(Ret == FALSE, "Expected FALSE, got %d\n", Ret);
    ok(GetLastError() == ERROR_ACCESS_DENIED, "Expected ERROR_ACCESS_DENIED, got %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ok(pGetDialogControlDpiChangeBehavior(hDesktop) == 0, "Unexpected flags on the desktop window\n");
    ok(GetLastError() == 0xdeadbeef, "Last error changed to %lu\n", GetLastError());

    if (!hTray)
    {
        skip("No taskbar window\n");
        return;
    }

    SetLastError(0xdeadbeef);
    Ret = pSetDialogDpiChangeBehavior(hTray, DDC_DISABLE_ALL, DDC_DISABLE_ALL);
    ok(Ret == FALSE, "Expected FALSE, got %d\n", Ret);
    ok(GetLastError() == ERROR_ACCESS_DENIED, "Expected ERROR_ACCESS_DENIED, got %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ok(pGetDialogDpiChangeBehavior(hTray) == 0, "Unexpected flags on the taskbar\n");
    ok(GetLastError() == ERROR_WINDOW_NOT_DIALOG, "Expected ERROR_WINDOW_NOT_DIALOG, got %lu\n", GetLastError());
}

START_TEST(DialogDpiChangeBehavior)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");

    pSetDialogControlDpiChangeBehavior = (PFN_SET_BEHAVIOR)GetProcAddress(hUser32, "SetDialogControlDpiChangeBehavior");
    pGetDialogControlDpiChangeBehavior = (PFN_GET_BEHAVIOR)GetProcAddress(hUser32, "GetDialogControlDpiChangeBehavior");
    pSetDialogDpiChangeBehavior = (PFN_SET_BEHAVIOR)GetProcAddress(hUser32, "SetDialogDpiChangeBehavior");
    pGetDialogDpiChangeBehavior = (PFN_GET_BEHAVIOR)GetProcAddress(hUser32, "GetDialogDpiChangeBehavior");

    if (!pSetDialogControlDpiChangeBehavior || !pGetDialogControlDpiChangeBehavior ||
        !pSetDialogDpiChangeBehavior || !pGetDialogDpiChangeBehavior)
    {
        skip("The dialog DPI change behavior functions are not exported\n");
        return;
    }

    TestControlBehavior();
    TestDialogBehavior();
    TestOtherProcess();
}
