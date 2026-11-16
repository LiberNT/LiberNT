/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the composition font and position an edit control gives its input context
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "precomp.h"
#include <imm.h>

static void TestEdit(HWND hwndParent, DWORD dwStyle, HFONT hFont)
{
    HWND hwndEdit;
    HIMC hIMC;
    COMPOSITIONFORM cf;
    LOGFONTW lf;
    POINT ptCaret;

    hwndEdit = CreateWindowExW(0, L"EDIT", L"abcdef", WS_CHILD | WS_VISIBLE | WS_BORDER | dwStyle, 10, 10, 400, 80,
                               hwndParent, NULL, GetModuleHandleW(NULL), NULL);
    ok(hwndEdit != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!hwndEdit)
        return;
    SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SetFocus(hwndEdit);
    SendMessageW(hwndEdit, EM_SETSEL, 6, 6);

    hIMC = ImmGetContext(hwndEdit);
    if (!hIMC)
    {
        skip("the edit control has no input context\n");
        DestroyWindow(hwndEdit);
        return;
    }

    ZeroMemory(&lf, sizeof(lf));
    ok(ImmGetCompositionFontW(hIMC, &lf), "ImmGetCompositionFontW failed\n");
    ok_long(lf.lfHeight, -32);
    ok(!lstrcmpW(lf.lfFaceName, L"Arial"), "face %ls\n", lf.lfFaceName);

    ZeroMemory(&cf, sizeof(cf));
    ok(ImmGetCompositionWindow(hIMC, &cf), "ImmGetCompositionWindow failed\n");
    GetCaretPos(&ptCaret);
    trace("style 0x%lx pt %ld,%ld area %ld,%ld-%ld,%ld caret %ld,%ld\n", cf.dwStyle, cf.ptCurrentPos.x,
          cf.ptCurrentPos.y, cf.rcArea.left, cf.rcArea.top, cf.rcArea.right, cf.rcArea.bottom, ptCaret.x, ptCaret.y);
    ok_long(cf.dwStyle, (dwStyle & ES_MULTILINE) ? CFS_RECT : CFS_POINT);
    ok_long(cf.ptCurrentPos.x, ptCaret.x);
    ok_long(cf.ptCurrentPos.y, ptCaret.y);

    SendMessageW(hwndEdit, EM_SETSEL, 1, 1);
    ZeroMemory(&cf, sizeof(cf));
    ok(ImmGetCompositionWindow(hIMC, &cf), "ImmGetCompositionWindow failed\n");
    GetCaretPos(&ptCaret);
    ok_long(cf.ptCurrentPos.x, ptCaret.x);
    ok_long(cf.ptCurrentPos.y, ptCaret.y);

    ImmReleaseContext(hwndEdit, hIMC);
    DestroyWindow(hwndEdit);
}

static void TestLayout(HWND hwndParent, HFONT hFont)
{
    winetest_push_context("hkl %p", GetKeyboardLayout(0));
    TestEdit(hwndParent, ES_AUTOHSCROLL, hFont);
    TestEdit(hwndParent, ES_MULTILINE, hFont);
    winetest_pop_context();
}

START_TEST(EditCompositionForm)
{
    HWND hwndParent;
    HFONT hFont;
    HKL hklOld, hklUS;

    hwndParent = CreateWindowExW(0, L"STATIC", L"EditCompositionForm", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 500,
                                 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hwndParent != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!hwndParent)
        return;
    SetForegroundWindow(hwndParent);
    hFont = CreateFontW(-32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");

    TestLayout(hwndParent, hFont);

    hklOld = GetKeyboardLayout(0);
    hklUS = LoadKeyboardLayoutW(L"00000409", KLF_ACTIVATE);
    if (hklUS && LOWORD(GetKeyboardLayout(0)) == 0x0409 && hklUS != hklOld)
    {
        TestLayout(hwndParent, hFont);
        ActivateKeyboardLayout(hklOld, 0);
    }
    else
    {
        skip("Cannot activate the US layout\n");
    }

    DeleteObject(hFont);
    DestroyWindow(hwndParent);
}
