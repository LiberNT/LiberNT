/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Message boxes of the internal UI during an install
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>
#include <msi.h>
#include <msidefs.h>
#include <msiquery.h>
#include <strsafe.h>

#define PRODUCT_NAME L"msi_apitest msgbox"

static const PCWSTR Base[] =
{
    L"CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL PRIMARY KEY `Property`)",
    L"CREATE TABLE `InstallExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
    L"CREATE TABLE `CustomAction` (`Action` CHAR(72) NOT NULL, `Type` SHORT NOT NULL, `Source` CHAR(72), `Target` CHAR(255) PRIMARY KEY `Action`)",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductName', 'msi_apitest msgbox')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductCode', '{4C8E2B71-5A3D-4F96-9E07-B1D6C3A8F254}')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductVersion', '1.0.0')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductLanguage', '1033')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('Manufacturer', 'LiberNT')",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostInitialize', 100)",
    NULL
};

static const PCWSTR Reboot[] =
{
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('ScheduleReboot', 200)",
    NULL
};

static const PCWSTR Error[] =
{
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('Fail', 19, '', 'msi_apitest msgbox error')",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('Fail', 200)",
    NULL
};

typedef struct _BOX
{
    BOOL Seen;
    BOOL Topmost;
    BOOL OwnedByProgress;
    BOOL ProgressDisabled;
    WCHAR Title[128];
} BOX;

static WCHAR PackagePath[MAX_PATH];
static volatile LONG InstallDone;
static UINT InstallResult;

static BOOL run_queries(MSIHANDLE db, const PCWSTR *queries)
{
    MSIHANDLE view;
    BOOL res = TRUE;

    for (; *queries && res; queries++)
    {
        res = MsiDatabaseOpenViewW(db, *queries, &view) == ERROR_SUCCESS;
        if (res)
        {
            res = MsiViewExecute(view, 0) == ERROR_SUCCESS;
            MsiViewClose(view);
            MsiCloseHandle(view);
        }
    }
    return res;
}

static BOOL create_package(PCWSTR path, const PCWSTR *sequence)
{
    MSIHANDLE db, summary;
    BOOL res;

    DeleteFileW(path);
    if (MsiOpenDatabaseW(path, (LPCWSTR)MSIDBOPEN_CREATE, &db) != ERROR_SUCCESS)
        return FALSE;
    res = run_queries(db, Base) && run_queries(db, sequence);
    if (res && MsiGetSummaryInformationW(db, NULL, 5, &summary) == ERROR_SUCCESS)
    {
        res = MsiSummaryInfoSetPropertyW(summary, PID_TEMPLATE, VT_LPSTR, 0, NULL, L"Intel;1033") == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_REVNUMBER, VT_LPSTR, 0, NULL,
                                         L"{9D3A6E15-2B8C-4F70-A4E1-6C5B0D7F2E93}") == ERROR_SUCCESS &&
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

static BOOL CALLBACK find_last_button(HWND hwnd, LPARAM context)
{
    WCHAR name[32];

    if (IsWindowVisible(hwnd) && IsWindowEnabled(hwnd) && GetClassNameW(hwnd, name, ARRAYSIZE(name)) &&
        !_wcsicmp(name, L"Button"))
    {
        *(HWND *)context = hwnd;
    }
    return TRUE;
}

static BOOL CALLBACK find_box(HWND hwnd, LPARAM context)
{
    BOX *box = (BOX *)context;
    WCHAR name[32];
    HWND owner, button = NULL;
    DWORD pid;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || !GetClassNameW(hwnd, name, ARRAYSIZE(name)) ||
        wcscmp(name, L"#32770") || GetDlgItem(hwnd, 10))
    {
        return TRUE;
    }

    box->Seen = TRUE;
    box->Topmost = (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    GetWindowTextW(hwnd, box->Title, ARRAYSIZE(box->Title));
    owner = GetWindow(hwnd, GW_OWNER);
    box->OwnedByProgress = owner && GetDlgItem(owner, 10) != NULL;
    box->ProgressDisabled = box->OwnedByProgress && !IsWindowEnabled(owner);
    EnumChildWindows(hwnd, find_last_button, (LPARAM)&button);
    if (button)
        PostMessageW(button, BM_CLICK, 0, 0);
    return FALSE;
}

static void test_box(const PCWSTR *sequence, PCWSTR kind, UINT expected)
{
    BOX box = { 0 };
    HANDLE thread;

    winetest_push_context("%ls", kind);
    if (!create_package(PackagePath, sequence))
    {
        skip("Cannot create the test package\n");
        winetest_pop_context();
        return;
    }
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
        if (!box.Seen)
            EnumWindows(find_box, (LPARAM)&box);
        Sleep(10);
    }
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

    ok(InstallResult == expected, "MsiInstallProductW returned %u\n", InstallResult);
    ok(box.Seen, "no message box\n");
    ok(!wcscmp(box.Title, PRODUCT_NAME), "title '%ls'\n", box.Title);
    ok(box.Topmost, "the message box is not topmost\n");
    ok(box.OwnedByProgress, "the message box is not owned by the progress window\n");
    ok(box.ProgressDisabled, "the progress window stayed enabled\n");
    DeleteFileW(PackagePath);
    winetest_pop_context();
}

START_TEST(msgbox)
{
    WCHAR temp[MAX_PATH];
    INSTALLUILEVEL old;

    GetTempPathW(ARRAYSIZE(temp), temp);
    StringCbPrintfW(PackagePath, sizeof(PackagePath), L"%lsmsi_apitest_msgbox.msi", temp);
    old = MsiSetInternalUI(INSTALLUILEVEL_BASIC, NULL);
    test_box(Reboot, L"reboot", ERROR_SUCCESS_REBOOT_REQUIRED);
    test_box(Error, L"error", ERROR_INSTALL_FAILURE);
    MsiSetInternalUI(old, NULL);
}
