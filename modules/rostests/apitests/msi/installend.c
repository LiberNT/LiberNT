/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Return values reported at the end of actions and installs
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <hello@astroshippers.com>
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
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductName', 'msi_apitest installend')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductVersion', '1.0.0')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductLanguage', '1033')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('Manufacturer', 'LiberNT')",
    NULL
};

static const PCWSTR Plain[] =
{
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostInitialize', 100)",
    NULL
};

static const PCWSTR Fail[] =
{
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('Fail', 19, '', 'msi_apitest installend')",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostInitialize', 100)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('Fail', 200)",
    NULL
};

static const PCWSTR Cancel[] =
{
    L"INSERT INTO `CustomAction` (`Action`, `Type`, `Source`, `Target`) VALUES ('SetIt', 51, 'MYPROP', '1')",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostInitialize', 100)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('SetIt', 200)",
    NULL
};

static const PCWSTR Force[] =
{
    L"CREATE TABLE `Directory` (`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Directory`)",
    L"INSERT INTO `Directory` (`Directory`, `Directory_Parent`, `DefaultDir`) VALUES ('TARGETDIR', '', 'SourceDir')",
    L"CREATE TABLE `Feature` (`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64), `Description` CHAR(255), `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL PRIMARY KEY `Feature`)",
    L"INSERT INTO `Feature` (`Feature`, `Feature_Parent`, `Title`, `Description`, `Display`, `Level`, `Directory_`, `Attributes`) VALUES ('f', '', '', '', 0, 1, '', 0)",
    L"CREATE TABLE `Component` (`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72) PRIMARY KEY `Component`)",
    L"INSERT INTO `Component` (`Component`, `ComponentId`, `Directory_`, `Attributes`, `Condition`, `KeyPath`) VALUES ('c', '', 'TARGETDIR', 0, '', '')",
    L"CREATE TABLE `FeatureComponents` (`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Feature_`, `Component_`)",
    L"INSERT INTO `FeatureComponents` (`Feature_`, `Component_`) VALUES ('f', 'c')",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostInitialize', 100)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('FileCost', 200)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostFinalize', 300)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallInitialize', 400)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Condition`, `Sequence`) VALUES ('ForceReboot', 'NOT AFTERREBOOT', 500)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallFinalize', 600)",
    NULL
};

static PCWSTR WatchedAction;
static PCWSTR CancelledAction;
static UINT InstallEnds;
static INT InstallEndValue;
static INT InstallEndedValue;
static INT ActionEndedValue;

static BOOL run_queries(MSIHANDLE db, const PCWSTR *queries)
{
    MSIHANDLE view;
    UINT r;

    for (; *queries; queries++)
    {
        r = MsiDatabaseOpenViewW(db, *queries, &view);
        if (r != ERROR_SUCCESS)
            return FALSE;
        r = MsiViewExecute(view, 0);
        MsiViewClose(view);
        MsiCloseHandle(view);
        if (r != ERROR_SUCCESS)
            return FALSE;
    }
    return TRUE;
}

static BOOL create_package(PCWSTR path, const PCWSTR *sequence, PCWSTR product)
{
    WCHAR query[128];
    PCWSTR queries[] = { query, NULL };
    MSIHANDLE db, summary;
    BOOL res;

    StringCbPrintfW(query, sizeof(query),
                    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductCode', '%ls')", product);
    DeleteFileW(path);
    if (MsiOpenDatabaseW(path, (LPCWSTR)MSIDBOPEN_CREATE, &db) != ERROR_SUCCESS)
        return FALSE;
    res = run_queries(db, Base) && run_queries(db, queries) && run_queries(db, sequence);

    if (res && MsiGetSummaryInformationW(db, NULL, 5, &summary) == ERROR_SUCCESS)
    {
        res = MsiSummaryInfoSetPropertyW(summary, PID_TEMPLATE, VT_LPSTR, 0, NULL, L"Intel;1033") == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_REVNUMBER, VT_LPSTR, 0, NULL,
                                         L"{3B8E2F61-74C9-4A0D-8E15-9C6D2A7F4E03}") == ERROR_SUCCESS &&
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

static BOOL record_names(MSIHANDLE record, PCWSTR action)
{
    WCHAR name[72];
    DWORD size = ARRAYSIZE(name);

    if (!action || MsiRecordGetStringW(record, 1, name, &size) != ERROR_SUCCESS)
        return FALSE;
    return !wcscmp(name, action);
}

static INT CALLBACK ui_handler(LPVOID context, UINT type, MSIHANDLE record)
{
    INT value;

    switch (type & 0xff000000)
    {
    case INSTALLMESSAGE_INSTALLEND:
        InstallEnds++;
        InstallEndValue = MsiRecordGetInteger(record, 3);
        return 0;
    case INSTALLMESSAGE_ACTIONSTART:
        return record_names(record, CancelledAction) ? IDCANCEL : 0;
    case INSTALLMESSAGE_INFO:
        if (MsiRecordGetFieldCount(record) != 2 || MsiRecordIsNull(record, 2))
            return 0;
        value = MsiRecordGetInteger(record, 2);
        if (record_names(record, L"INSTALL"))
            InstallEndedValue = value;
        else if (record_names(record, WatchedAction))
            ActionEndedValue = value;
        return 0;
    default:
        return (type & 0xf) == MB_YESNO ? IDNO : 0;
    }
}

/* Removes the RunOnce entries a suspended install of the test package leaves. */
static void remove_run_once(PCWSTR package, PCWSTR product)
{
    WCHAR name[64], data[512];
    DWORD name_size, data_size, index = 0, type;
    HKEY entries, run_once;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Installer\\RunOnceEntries",
                      0, KEY_ALL_ACCESS, &entries) != ERROR_SUCCESS)
        return;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
                      0, KEY_ALL_ACCESS, &run_once) != ERROR_SUCCESS)
        run_once = NULL;
    for (;;)
    {
        name_size = ARRAYSIZE(name);
        data_size = sizeof(data) - sizeof(WCHAR);
        ZeroMemory(data, sizeof(data));
        if (RegEnumValueW(entries, index, name, &name_size, NULL, &type, (BYTE *)data, &data_size) != ERROR_SUCCESS)
            break;
        if (type == REG_SZ && (wcsstr(data, package) || wcsstr(data, product)))
        {
            if (run_once)
                RegDeleteValueW(run_once, name);
            if (RegDeleteValueW(entries, name) == ERROR_SUCCESS)
                continue;
        }
        index++;
    }
    if (run_once)
        RegCloseKey(run_once);
    RegCloseKey(entries);
}

static void test_install(PCWSTR package, PCWSTR product, PCWSTR properties, PCWSTR action, PCWSTR cancelled,
                         UINT expected, INT value, INT action_value)
{
    UINT r;

    WatchedAction = action;
    CancelledAction = cancelled;
    InstallEnds = 0;
    InstallEndValue = -1;
    InstallEndedValue = -1;
    ActionEndedValue = -1;
    r = MsiInstallProductW(package, properties);
    remove_run_once(package, product);
    ok(r == expected, "%ls: expected %u, got %u\n", action, expected, r);
    ok(InstallEnds == 1, "%ls: expected 1 install end, got %u\n", action, InstallEnds);
    ok(InstallEndValue == value, "%ls: expected install end %d, got %d\n", action, value, InstallEndValue);
    ok(InstallEndedValue == value, "%ls: expected INSTALL ended %d, got %d\n", action, value, InstallEndedValue);
    if (action_value >= 0)
        ok(ActionEndedValue == action_value, "%ls: expected action ended %d, got %d\n", action, action_value, ActionEndedValue);
}

START_TEST(installend)
{
    static const PCWSTR Products[] =
    {
        L"{8A4F2C1E-3D5B-4E69-A7C0-1F2E3D4C5B61}",
        L"{8A4F2C1E-3D5B-4E69-A7C0-1F2E3D4C5B62}",
        L"{8A4F2C1E-3D5B-4E69-A7C0-1F2E3D4C5B63}",
        L"{8A4F2C1E-3D5B-4E69-A7C0-1F2E3D4C5B64}",
    };
    static const PCWSTR *Sequences[] = { Plain, Fail, Cancel, Force };
    WCHAR temp[MAX_PATH], packages[ARRAYSIZE(Products)][MAX_PATH];
    BOOL created = TRUE;
    UINT i;

    GetTempPathW(ARRAYSIZE(temp), temp);
    for (i = 0; i < ARRAYSIZE(Products); i++)
    {
        StringCbPrintfW(packages[i], sizeof(packages[i]), L"%lsmsi_installend%u.msi", temp, i);
        created = created && create_package(packages[i], Sequences[i], Products[i]);
    }
    if (!created)
    {
        skip("Cannot create the test packages\n");
        goto cleanup;
    }

    MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);
    MsiSetExternalUIRecord(ui_handler, INSTALLLOGMODE_INSTALLEND | INSTALLLOGMODE_ACTIONSTART | INSTALLLOGMODE_INFO |
                           INSTALLLOGMODE_USER | INSTALLLOGMODE_ERROR, NULL, NULL);

    test_install(packages[0], Products[0], L"", L"CostInitialize", NULL, ERROR_SUCCESS, 1, 1);
    test_install(packages[1], Products[1], L"", L"Fail", NULL, ERROR_INSTALL_FAILURE, 3, 3);
    test_install(packages[2], Products[2], L"", L"SetIt", L"SetIt", ERROR_INSTALL_USEREXIT, 2, -1);
    test_install(packages[3], Products[3], L"REBOOT=ReallySuppress", L"ForceReboot", NULL, ERROR_INSTALL_SUSPEND, 4, 4);

    MsiSetExternalUIRecord(NULL, 0, NULL, NULL);

cleanup:
    for (i = 0; i < ARRAYSIZE(Products); i++)
        DeleteFileW(packages[i]);
}
