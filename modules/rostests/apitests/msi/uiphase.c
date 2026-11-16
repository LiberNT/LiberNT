/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The progress window of the internal UI in the UI and execute sequences
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#include <windows.h>
#include <msi.h>
#include <msidefs.h>
#include <msiquery.h>
#include <strsafe.h>

static const PCWSTR Queries[] =
{
    L"CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL PRIMARY KEY `Property`)",
    L"CREATE TABLE `InstallUISequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
    L"CREATE TABLE `InstallExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
    L"CREATE TABLE `CustomAction` (`Action` CHAR(72) NOT NULL, `Type` SHORT NOT NULL, `Source` CHAR(72), `Target` CHAR(255) PRIMARY KEY `Action`)",
    L"CREATE TABLE `Directory` (`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Directory`)",
    L"INSERT INTO `Directory` (`Directory`, `Directory_Parent`, `DefaultDir`) VALUES ('TARGETDIR', '', 'SourceDir')",
    L"INSERT INTO `Directory` (`Directory`, `Directory_Parent`, `DefaultDir`) VALUES ('Sub', 'TARGETDIR', 'msiuiphd')",
    L"CREATE TABLE `CreateFolder` (`Directory_` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Directory_`, `Component_`)",
    L"INSERT INTO `CreateFolder` (`Directory_`, `Component_`) VALUES ('Sub', 'Main')",
    L"CREATE TABLE `Feature` (`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64), `Description` CHAR(255), `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL PRIMARY KEY `Feature`)",
    L"INSERT INTO `Feature` (`Feature`, `Feature_Parent`, `Title`, `Description`, `Display`, `Level`, `Directory_`, `Attributes`) VALUES ('Main', '', '', '', 0, 1, 'TARGETDIR', 0)",
    L"CREATE TABLE `Component` (`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72) PRIMARY KEY `Component`)",
    L"INSERT INTO `Component` (`Component`, `ComponentId`, `Directory_`, `Attributes`, `Condition`, `KeyPath`) VALUES ('Main', '{5D2C8E41-7B3A-4F96-A0E5-2C9B1D7F4E83}', 'Sub', 0, '', '')",
    L"CREATE TABLE `FeatureComponents` (`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Feature_`, `Component_`)",
    L"INSERT INTO `FeatureComponents` (`Feature_`, `Component_`) VALUES ('Main', 'Main')",
    L"CREATE TABLE `ActionText` (`Action` CHAR(72) NOT NULL, `Description` CHAR(0) LOCALIZABLE, `Template` CHAR(0) LOCALIZABLE PRIMARY KEY `Action`)",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductName', 'msi_apitest uiphase')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductCode', '{2E7B9D14-6A3C-4F85-B0D2-8C1E5A7F3B96}')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductVersion', '1.0.0')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductLanguage', '1033')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('Manufacturer', 'LiberNT')",
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('SetShell', 51, 'SHELL', '[SystemFolder]cmd.exe')",
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('UiWait', 114, 'SHELL', '/c echo x>\"[TempFolder]msiuiphase.ui\"&ping -n 3 127.0.0.1>nul')",
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('SetTarget', 51, 'TARGETDIR', '[TempFolder]')",
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('UiDone', 114, 'SHELL', '/c echo x>\"[TempFolder]msiuiphase.ud\"')",
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('ScriptWait', 1122, 'TARGETDIR', '\"[SystemFolder]cmd.exe\" /c echo x>\"[TempFolder]msiuiphase.sc\"&ping -n 3 127.0.0.1>nul')",
    L"INSERT INTO `ActionText` (`Action`, `Description`, `Template`) VALUES ('ScriptWait', 'ScriptWait running', '')",
    L"INSERT INTO `ActionText` (`Action`, `Description`, `Template`) VALUES ('UiWait', 'UiWait running', '')",
    L"INSERT INTO `InstallUISequence` (`Action`, `Sequence`) VALUES ('CostInitialize', 100)",
    L"INSERT INTO `InstallUISequence` (`Action`, `Sequence`) VALUES ('SetShell', 200)",
    L"INSERT INTO `InstallUISequence` (`Action`, `Sequence`) VALUES ('UiWait', 300)",
    L"INSERT INTO `InstallUISequence` (`Action`, `Sequence`) VALUES ('SetTarget', 340)",
    L"INSERT INTO `InstallUISequence` (`Action`, `Sequence`) VALUES ('CostFinalize', 350)",
    L"INSERT INTO `InstallUISequence` (`Action`, `Sequence`) VALUES ('UiDone', 360)",
    L"INSERT INTO `InstallUISequence` (`Action`, `Sequence`) VALUES ('ExecuteAction', 400)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostInitialize', 100)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('SetTarget', 900)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostFinalize', 1000)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CreateFolders', 1550)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallValidate', 1400)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallInitialize', 1500)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('ScriptWait', 1600)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallFinalize', 6600)",
    NULL
};

typedef struct _PHASE
{
    BOOL Seen;
    BOOL BarVisible;
    WCHAR Action[512];
} PHASE;

static WCHAR PackagePath[MAX_PATH];
static WCHAR UiFlag[MAX_PATH];
static WCHAR UiDoneFlag[MAX_PATH];
static WCHAR ScriptFlag[MAX_PATH];
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
                                         L"{8A3F1C6E-4D2B-4790-9E5A-1B7C3D9F0E24}") == ERROR_SUCCESS &&
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

static BOOL CALLBACK find_dialog(HWND hwnd, LPARAM context)
{
    HWND *dialog = (HWND *)context;
    WCHAR name[32];
    DWORD pid;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && GetClassNameW(hwnd, name, ARRAYSIZE(name)) && !wcscmp(name, L"#32770") &&
        GetDlgItem(hwnd, 8) && GetDlgItem(hwnd, 10))
    {
        *dialog = hwnd;
        return FALSE;
    }
    return TRUE;
}

static void sample(PHASE *phase)
{
    HWND dialog = NULL;
    WCHAR text[128];

    EnumWindows(find_dialog, (LPARAM)&dialog);
    if (!dialog)
        return;
    phase->Seen = TRUE;
    if (IsWindowVisible(GetDlgItem(dialog, 10)))
        phase->BarVisible = TRUE;
    if (GetDlgItemTextW(dialog, 11, text, ARRAYSIZE(text)) && text[0] && !wcsstr(phase->Action, text))
    {
        StringCbCatW(phase->Action, sizeof(phase->Action), text);
        StringCbCatW(phase->Action, sizeof(phase->Action), L"|");
    }
}

START_TEST(uiphase)
{
    WCHAR temp[MAX_PATH];
    INSTALLUILEVEL old;
    PHASE ui = { 0 }, script = { 0 };
    HANDLE thread;

    GetTempPathW(ARRAYSIZE(temp), temp);
    StringCbPrintfW(PackagePath, sizeof(PackagePath), L"%lsmsi_apitest_uiphase.msi", temp);
    StringCbPrintfW(UiFlag, sizeof(UiFlag), L"%lsmsiuiphase.ui", temp);
    StringCbPrintfW(UiDoneFlag, sizeof(UiDoneFlag), L"%lsmsiuiphase.ud", temp);
    StringCbPrintfW(ScriptFlag, sizeof(ScriptFlag), L"%lsmsiuiphase.sc", temp);
    DeleteFileW(UiFlag);
    DeleteFileW(UiDoneFlag);
    DeleteFileW(ScriptFlag);
    if (!create_package(PackagePath))
    {
        skip("Cannot create the test package\n");
        return;
    }
    old = MsiSetInternalUI(INSTALLUILEVEL_FULL, NULL);
    thread = CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
    ok(thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (thread)
    {
        while (!InstallDone)
        {
            if (GetFileAttributesW(ScriptFlag) != INVALID_FILE_ATTRIBUTES)
                sample(&script);
            else if (GetFileAttributesW(UiFlag) != INVALID_FILE_ATTRIBUTES &&
                     GetFileAttributesW(UiDoneFlag) == INVALID_FILE_ATTRIBUTES)
                sample(&ui);
            Sleep(10);
        }
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);

        ok(InstallResult == ERROR_SUCCESS, "MsiInstallProductW returned %u\n", InstallResult);
        ok(ui.Seen, "no progress window in the UI sequence\n");
        ok(!ui.BarVisible, "the progress bar is visible in the UI sequence\n");
        ok(!ui.Action[0], "action text '%ls' in the UI sequence\n", ui.Action);
        ok(script.Seen, "no progress window in the script\n");
        ok(script.BarVisible, "the progress bar is hidden in the script\n");
        ok(script.Action[0] && !wcsstr(script.Action, L"ScriptWait running"), "action text '%ls' in the script\n",
           script.Action);
    }
    MsiSetInternalUI(old, NULL);
    DeleteFileW(UiFlag);
    DeleteFileW(UiDoneFlag);
    DeleteFileW(ScriptFlag);
    DeleteFileW(PackagePath);
    StringCbCatW(temp, sizeof(temp), L"msiuiphd");
    RemoveDirectoryW(temp);
}
