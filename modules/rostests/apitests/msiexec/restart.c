/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     msiexec /norestart, /forcerestart and /promptrestart
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <hello@astroshippers.com>
 */

#include <apitest.h>
#include <windows.h>
#include <msi.h>
#include <msidefs.h>
#include <msiquery.h>
#include <strsafe.h>

#define NOT_EXITED 0xffffffff

static const PCWSTR Tables[] =
{
    L"CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL PRIMARY KEY `Property`)",
    L"CREATE TABLE `LaunchCondition` (`Condition` CHAR(255) NOT NULL, `Description` CHAR(255) NOT NULL PRIMARY KEY `Condition`)",
    L"CREATE TABLE `InstallExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductCode', '{6C1D5E3A-8F0B-4D27-9E4C-2B7A1F0D9C35}')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductName', 'msiexec_apitest')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductVersion', '1.0.0')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductLanguage', '1033')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('Manufacturer', 'LiberNT')",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('LaunchConditions', 100)",
};

static BOOL run_query(MSIHANDLE db, PCWSTR query)
{
    MSIHANDLE view;
    UINT r;

    r = MsiDatabaseOpenViewW(db, query, &view);
    if (r != ERROR_SUCCESS)
        return FALSE;
    r = MsiViewExecute(view, 0);
    MsiViewClose(view);
    MsiCloseHandle(view);
    return r == ERROR_SUCCESS;
}

static BOOL create_package(PCWSTR path, PCWSTR condition)
{
    WCHAR query[512];
    MSIHANDLE db, summary;
    BOOL res = TRUE;
    UINT i;

    DeleteFileW(path);
    if (MsiOpenDatabaseW(path, (LPCWSTR)MSIDBOPEN_CREATE, &db) != ERROR_SUCCESS)
        return FALSE;
    for (i = 0; i < ARRAYSIZE(Tables); i++)
        res = res && run_query(db, Tables[i]);
    StringCbPrintfW(query, sizeof(query),
                    L"INSERT INTO `LaunchCondition` (`Condition`, `Description`) VALUES ('%ls', 'condition')",
                    condition);
    res = res && run_query(db, query);

    if (res && MsiGetSummaryInformationW(db, NULL, 5, &summary) == ERROR_SUCCESS)
    {
        res = MsiSummaryInfoSetPropertyW(summary, PID_TEMPLATE, VT_LPSTR, 0, NULL, L"Intel;1033") == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_REVNUMBER, VT_LPSTR, 0, NULL,
                                         L"{0E1F4B2C-5A63-4C8D-B7E9-3D2A6F1C8B40}") == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_PAGECOUNT, VT_I4, 200, NULL, NULL) == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_WORDCOUNT, VT_I4, 2, NULL, NULL) == ERROR_SUCCESS &&
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

/* The exit code, or NOT_EXITED when msiexec had to be stopped after the time limit. */
static DWORD run_msiexec(PCWSTR arguments)
{
    WCHAR msiexec[MAX_PATH], command[3 * MAX_PATH];
    PROCESS_INFORMATION info;
    STARTUPINFOW startup = { sizeof(startup) };
    DWORD code = NOT_EXITED;

    GetSystemDirectoryW(msiexec, ARRAYSIZE(msiexec));
    StringCbCatW(msiexec, sizeof(msiexec), L"\\msiexec.exe");
    StringCbPrintfW(command, sizeof(command), L"\"%ls\" %ls", msiexec, arguments);
    if (!CreateProcessW(msiexec, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info))
    {
        ok(0, "CreateProcessW(%ls) failed: %lu\n", command, GetLastError());
        return NOT_EXITED;
    }
    if (WaitForSingleObject(info.hProcess, 60000) == WAIT_OBJECT_0)
        GetExitCodeProcess(info.hProcess, &code);
    else
        TerminateProcess(info.hProcess, 1);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return code;
}

static void test_option(PCWSTR package, PCWSTR options, DWORD expected)
{
    WCHAR arguments[2 * MAX_PATH];
    DWORD code;

    StringCbPrintfW(arguments, sizeof(arguments), options, package);
    code = run_msiexec(arguments);
    ok(code == expected, "msiexec %ls: expected %lu, got %lu\n", arguments, expected, code);
}

START_TEST(restart)
{
    WCHAR temp[MAX_PATH], suppress[MAX_PATH], force[MAX_PATH], prompt[MAX_PATH];

    GetTempPathW(ARRAYSIZE(temp), temp);
    StringCbPrintfW(suppress, sizeof(suppress), L"%lsmsiexec_suppress.msi", temp);
    StringCbPrintfW(force, sizeof(force), L"%lsmsiexec_force.msi", temp);
    StringCbPrintfW(prompt, sizeof(prompt), L"%lsmsiexec_prompt.msi", temp);

    if (!create_package(suppress, L"REBOOT=\"ReallySuppress\"") ||
        !create_package(force, L"REBOOT<>\"Force\"") ||
        !create_package(prompt, L"REBOOTPROMPT=\"S\""))
    {
        skip("Cannot create the test packages\n");
        goto cleanup;
    }

    test_option(suppress, L"/qn /i \"%ls\"", ERROR_INSTALL_FAILURE);
    test_option(suppress, L"/qn /i \"%ls\" /norestart", ERROR_SUCCESS);
    test_option(suppress, L"/qn /norestart /i \"%ls\"", ERROR_SUCCESS);
    test_option(suppress, L"/qn /i \"%ls\" -NORESTART", ERROR_SUCCESS);

    test_option(force, L"/qn /i \"%ls\"", ERROR_SUCCESS);
    test_option(force, L"/qn /i \"%ls\" /forcerestart", ERROR_INSTALL_FAILURE);
    test_option(force, L"/qn /forcerestart /i \"%ls\"", ERROR_INSTALL_FAILURE);

    test_option(prompt, L"/qn /i \"%ls\" REBOOTPROMPT=S", ERROR_SUCCESS);
    test_option(prompt, L"/qn /i \"%ls\" REBOOTPROMPT=S /promptrestart", ERROR_INSTALL_FAILURE);

cleanup:
    DeleteFileW(suppress);
    DeleteFileW(force);
    DeleteFileW(prompt);
}
