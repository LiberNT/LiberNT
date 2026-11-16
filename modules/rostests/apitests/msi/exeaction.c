/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Custom actions that run an executable named by a property
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>
#include <msi.h>
#include <msidefs.h>
#include <msiquery.h>
#include <strsafe.h>

static const PCWSTR Base[] =
{
    L"CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL PRIMARY KEY `Property`)",
    L"CREATE TABLE `InstallExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
    L"CREATE TABLE `CustomAction` (`Action` CHAR(72) NOT NULL, `Type` SHORT NOT NULL, `Source` CHAR(72), `Target` CHAR(255) PRIMARY KEY `Action`)",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductName', 'msi_apitest exeaction')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductCode', '{7C1A5E93-2D4B-4F68-8B0E-3A9D6C2F1E57}')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductVersion', '1.0.0')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductLanguage', '1033')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('Manufacturer', 'LiberNT')",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostInitialize', 100)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('SetProgram', 200)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('Run', 300)",
    NULL
};

static const PCWSTR Ping[] =
{
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('SetProgram', 51, 'PROGRAM', '[SystemFolder]ping.exe')",
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('Run', 50, 'PROGRAM', '-n 2 127.0.0.1')",
    NULL
};

static const PCWSTR Fail[] =
{
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('SetProgram', 51, 'PROGRAM', '[SystemFolder]cmd.exe')",
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('Run', 50, 'PROGRAM', '/c exit 3')",
    NULL
};

static WCHAR PackagePath[MAX_PATH];

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

static BOOL create_package(PCWSTR path, const PCWSTR *actions)
{
    MSIHANDLE db, summary;
    BOOL res;

    DeleteFileW(path);
    if (MsiOpenDatabaseW(path, (LPCWSTR)MSIDBOPEN_CREATE, &db) != ERROR_SUCCESS)
        return FALSE;
    res = run_queries(db, Base) && run_queries(db, actions);
    if (res && MsiGetSummaryInformationW(db, NULL, 5, &summary) == ERROR_SUCCESS)
    {
        res = MsiSummaryInfoSetPropertyW(summary, PID_TEMPLATE, VT_LPSTR, 0, NULL, L"Intel;1033") == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_REVNUMBER, VT_LPSTR, 0, NULL,
                                         L"{3B8E6D21-9F4C-4A57-B1E3-7D0C5A2F8B64}") == ERROR_SUCCESS &&
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

static void test_run(const PCWSTR *actions, PCWSTR kind, UINT expected, DWORD min_ms)
{
    DWORD start, elapsed;
    UINT r;

    winetest_push_context("%ls", kind);
    if (!create_package(PackagePath, actions))
    {
        skip("Cannot create the test package\n");
        winetest_pop_context();
        return;
    }
    start = GetTickCount();
    r = MsiInstallProductW(PackagePath, L"");
    elapsed = GetTickCount() - start;
    ok(r == expected, "MsiInstallProductW returned %u\n", r);
    ok(elapsed >= min_ms, "the install took %lu ms\n", elapsed);
    DeleteFileW(PackagePath);
    winetest_pop_context();
}

START_TEST(exeaction)
{
    WCHAR temp[MAX_PATH];
    INSTALLUILEVEL old;

    GetTempPathW(ARRAYSIZE(temp), temp);
    StringCbPrintfW(PackagePath, sizeof(PackagePath), L"%lsmsi_apitest_exeaction.msi", temp);
    old = MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);
    test_run(Ping, L"ping", ERROR_SUCCESS, 900);
    test_run(Fail, L"exit 3", ERROR_INSTALL_FAILURE, 0);
    MsiSetInternalUI(old, NULL);
}
