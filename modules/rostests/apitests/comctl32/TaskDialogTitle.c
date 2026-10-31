/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test for the window title of a task dialog
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include "wine/test.h"

#include <windows.h>
#include <commctrl.h>

#define IDS_TASKDIALOG_TITLE 100

typedef HRESULT (WINAPI *FN_TaskDialogIndirect)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);

typedef struct
{
    WCHAR Title[128];
    BOOL Created;
} TITLE_RESULT;

static HRESULT CALLBACK
TitleCallback(HWND hwnd, UINT uNotification, WPARAM wParam, LPARAM lParam, LONG_PTR lpRefData)
{
    TITLE_RESULT *Result = (TITLE_RESULT *)lpRefData;

    if (uNotification == TDN_CREATED)
    {
        Result->Created = TRUE;
        GetWindowTextW(hwnd, Result->Title, ARRAYSIZE(Result->Title));
        PostMessageW(hwnd, TDM_CLICK_BUTTON, IDOK, 0);
    }
    return S_OK;
}

static void
TestTitle(FN_TaskDialogIndirect pTaskDialogIndirect, PCWSTR pszWindowTitle, PCWSTR pszExpected)
{
    TASKDIALOGCONFIG Config;
    TITLE_RESULT Result;
    int Button = 0;
    HRESULT hr;

    ZeroMemory(&Config, sizeof(Config));
    ZeroMemory(&Result, sizeof(Result));
    Config.cbSize = sizeof(Config);
    Config.hInstance = GetModuleHandleW(NULL);
    Config.pszWindowTitle = pszWindowTitle;
    Config.pszMainInstruction = L"Main instruction";
    Config.dwCommonButtons = TDCBF_OK_BUTTON;
    Config.pfCallback = TitleCallback;
    Config.lpCallbackData = (LONG_PTR)&Result;

    hr = pTaskDialogIndirect(&Config, &Button, NULL, NULL);
    ok(hr == S_OK, "TaskDialogIndirect returned 0x%08lx\n", hr);
    ok(Result.Created, "the dialog was not created\n");
    ok(Button == IDOK, "Button = %d\n", Button);
    ok(!wcscmp(Result.Title, pszExpected), "Title = '%S', expected '%S'\n", Result.Title, pszExpected);
}

START_TEST(TaskDialogTitle)
{
    FN_TaskDialogIndirect pTaskDialogIndirect;
    HMODULE hComctl32 = LoadLibraryW(L"comctl32.dll");

    pTaskDialogIndirect = (FN_TaskDialogIndirect)GetProcAddress(hComctl32, "TaskDialogIndirect");
    if (!pTaskDialogIndirect)
    {
        skip("TaskDialogIndirect is not available\n");
        return;
    }

    TestTitle(pTaskDialogIndirect, MAKEINTRESOURCEW(IDS_TASKDIALOG_TITLE), L"Task dialog title");
    TestTitle(pTaskDialogIndirect, L"Literal title", L"Literal title");
}
