/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Progress window of the internal UI for a package without dialogs
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>
#include <msi.h>
#include <msidefs.h>
#include <msiquery.h>
#include <strsafe.h>

#define PRODUCT_NAME L"msi_apitest basicui"

static const PCWSTR Queries[] =
{
    L"CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL PRIMARY KEY `Property`)",
    L"CREATE TABLE `InstallExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
    L"CREATE TABLE `CustomAction` (`Action` CHAR(72) NOT NULL, `Type` SHORT NOT NULL, `Source` CHAR(72), `Target` CHAR(255) PRIMARY KEY `Action`)",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductName', 'msi_apitest basicui')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductCode', '{6F1D7C42-93B5-4E0A-B2C8-5A7E1D3F9C64}')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductVersion', '1.0.0')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductLanguage', '1033')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('Manufacturer', 'LiberNT')",
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('SetWaiter', 51, 'WAITER', '[SystemFolder]ping.exe')",
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('Wait', 114, 'WAITER', '-n 3 127.0.0.1')",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostInitialize', 100)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('SetWaiter', 200)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('Wait', 300)",
    NULL
};

typedef struct _WATCH
{
    HWND Dialog;
    BOOL Layout;
    BOOL NamedTitle;
    BOOL NamedText;
} WATCH;

static WCHAR PackagePath[MAX_PATH];
static volatile LONG InstallDone;
static UINT InstallResult;

static BOOL create_package(PCWSTR path)
{
    MSIHANDLE db, summary, view;
    const PCWSTR *query;
    BOOL res = TRUE;

    DeleteFileW(path);
    if (MsiOpenDatabaseW(path, (LPCWSTR)MSIDBOPEN_CREATE, &db) != ERROR_SUCCESS)
        return FALSE;
    for (query = Queries; *query && res; query++)
    {
        res = MsiDatabaseOpenViewW(db, *query, &view) == ERROR_SUCCESS;
        if (res)
        {
            res = MsiViewExecute(view, 0) == ERROR_SUCCESS;
            MsiViewClose(view);
            MsiCloseHandle(view);
        }
    }
    if (res && MsiGetSummaryInformationW(db, NULL, 5, &summary) == ERROR_SUCCESS)
    {
        res = MsiSummaryInfoSetPropertyW(summary, PID_TEMPLATE, VT_LPSTR, 0, NULL, L"Intel;1033") == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_REVNUMBER, VT_LPSTR, 0, NULL,
                                         L"{0B4E8A17-2C6D-4F93-A51E-7D2B9C0E6F38}") == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_PAGECOUNT, VT_I4, 200, NULL, NULL) == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_WORDCOUNT, VT_I4, 10, NULL, NULL) == ERROR_SUCCESS &&
              MsiSummaryInfoPersist(summary) == ERROR_SUCCESS;
        MsiCloseHandle(summary);
    }
    else
    {
        res = FALSE;
    }
    res = res && MsiDatabaseCommit(db) == ERROR_SUCCESS;
    MsiCloseHandle(db);
    return res;
}

static DWORD WINAPI install_thread(LPVOID context)
{
    InstallResult = MsiInstallProductW(PackagePath, L"");
    InterlockedExchange(&InstallDone, 1);
    return 0;
}

static BOOL control_is(HWND dialog, int id, PCWSTR expected)
{
    WCHAR name[64];
    HWND control = GetDlgItem(dialog, id);

    return control && GetClassNameW(control, name, ARRAYSIZE(name)) && !_wcsicmp(name, expected);
}

static BOOL CALLBACK find_dialog(HWND hwnd, LPARAM context)
{
    WATCH *watch = (WATCH *)context;
    WCHAR text[256];
    DWORD pid;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !GetClassNameW(hwnd, text, ARRAYSIZE(text)) || wcscmp(text, L"#32770"))
        return TRUE;
    if (!GetDlgItem(hwnd, 8) || !GetDlgItem(hwnd, 10))
        return TRUE;

    watch->Dialog = hwnd;
    if (control_is(hwnd, 8, L"Static") && control_is(hwnd, 11, L"Static") &&
        control_is(hwnd, 10, L"msctls_progress32") && control_is(hwnd, IDCANCEL, L"Button"))
    {
        watch->Layout = TRUE;
    }
    if (GetWindowTextW(hwnd, text, ARRAYSIZE(text)) && !wcscmp(text, PRODUCT_NAME))
        watch->NamedTitle = TRUE;
    if (GetDlgItemTextW(hwnd, 8, text, ARRAYSIZE(text)) && wcsstr(text, PRODUCT_NAME))
        watch->NamedText = TRUE;
    return FALSE;
}

static void test_level(INSTALLUILEVEL level, BOOL expected)
{
    WATCH watch = { 0 };
    HANDLE thread;
    BOOL seen = FALSE;

    winetest_push_context("level %d", level);
    MsiSetInternalUI(level, NULL);
    InterlockedExchange(&InstallDone, 0);
    thread = CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
    ok(thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!thread)
    {
        winetest_pop_context();
        return;
    }
    while (!InstallDone)
    {
        watch.Dialog = NULL;
        EnumWindows(find_dialog, (LPARAM)&watch);
        if (watch.Dialog)
            seen = TRUE;
        Sleep(1);
    }
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

    ok(InstallResult == ERROR_SUCCESS, "MsiInstallProductW returned %u\n", InstallResult);
    ok(seen == expected, "progress window seen %d\n", seen);
    if (expected)
    {
        ok(watch.Layout, "the progress window has the wrong controls\n");
        ok(watch.NamedTitle, "the title never showed the product name\n");
        ok(watch.NamedText, "the text never showed the product name\n");
    }
    watch.Dialog = NULL;
    EnumWindows(find_dialog, (LPARAM)&watch);
    ok(watch.Dialog == NULL, "the progress window is still there\n");
    winetest_pop_context();
}

START_TEST(basicui)
{
    WCHAR temp[MAX_PATH];
    INSTALLUILEVEL old;

    GetTempPathW(ARRAYSIZE(temp), temp);
    StringCbPrintfW(PackagePath, sizeof(PackagePath), L"%lsmsi_apitest_basicui.msi", temp);
    if (!create_package(PackagePath))
    {
        skip("Cannot create the test package\n");
        return;
    }
    old = MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);
    test_level(INSTALLUILEVEL_NONE, FALSE);
    test_level(INSTALLUILEVEL_BASIC, TRUE);
    test_level(INSTALLUILEVEL_FULL, TRUE);
    MsiSetInternalUI(old, NULL);
    DeleteFileW(PackagePath);
}
