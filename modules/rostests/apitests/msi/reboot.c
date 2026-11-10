/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Restart prompts and results of REBOOT, ScheduleReboot and ForceReboot
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
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductName', 'msi_apitest reboot')",
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

static const PCWSTR Schedule[] =
{
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('ScheduleReboot', 100)",
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

static UINT Prompts;
static UINT PromptType;
static INT PromptError;
static WCHAR PromptProduct[64];
static INT Answer = IDNO;

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
                                         L"{0E1F4B2C-5A63-4C8D-B7E9-3D2A6F1C8B42}") == ERROR_SUCCESS &&
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

static INT CALLBACK ui_handler(LPVOID context, UINT type, MSIHANDLE record)
{
    DWORD size = ARRAYSIZE(PromptProduct);

    if ((type & 0xff000000) != INSTALLMESSAGE_USER)
        return 0;
    Prompts++;
    PromptType = type;
    PromptError = MsiRecordGetInteger(record, 1);
    PromptProduct[0] = 0;
    MsiRecordGetStringW(record, 2, PromptProduct, &size);
    return Answer;
}

static BOOL set_shutdown_privilege(DWORD attributes)
{
    TOKEN_PRIVILEGES privileges;
    HANDLE token;
    BOOL res;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token))
        return FALSE;
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = attributes;
    res = LookupPrivilegeValueW(NULL, L"SeShutdownPrivilege", &privileges.Privileges[0].Luid) &&
          AdjustTokenPrivileges(token, FALSE, &privileges, 0, NULL, NULL) &&
          GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return res;
}

/* Returns the attributes of SeShutdownPrivilege in the process token, or -1 when the token lacks it. */
static LONG get_shutdown_privilege(void)
{
    BYTE buffer[1024];
    TOKEN_PRIVILEGES *privileges = (TOKEN_PRIVILEGES *)buffer;
    HANDLE token;
    DWORD size, i;
    LUID luid;
    BOOL res;

    if (!LookupPrivilegeValueW(NULL, L"SeShutdownPrivilege", &luid) ||
        !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return -2;
    res = GetTokenInformation(token, TokenPrivileges, privileges, sizeof(buffer), &size);
    CloseHandle(token);
    if (!res)
        return -2;
    for (i = 0; i < privileges->PrivilegeCount; i++)
    {
        if (privileges->Privileges[i].Luid.LowPart == luid.LowPart &&
            privileges->Privileges[i].Luid.HighPart == luid.HighPart)
            return privileges->Privileges[i].Attributes;
    }
    return -1;
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

static void test_install(PCWSTR package, PCWSTR product, PCWSTR properties, UINT expected, INT prompt)
{
    UINT r;

    Prompts = 0;
    PromptType = 0;
    PromptError = 0;
    PromptProduct[0] = 0;
    r = MsiInstallProductW(package, properties);
    remove_run_once(package, product);
    ok(r == expected, "\"%ls\": expected %u, got %u\n", properties, expected, r);
    if (!prompt)
    {
        ok(Prompts == 0, "\"%ls\": expected no prompt, got %u\n", properties, Prompts);
        return;
    }
    ok(Prompts == 1, "\"%ls\": expected 1 prompt, got %u\n", properties, Prompts);
    ok(PromptType == (INSTALLMESSAGE_USER | MB_YESNO), "\"%ls\": prompt type %#x\n", properties, PromptType);
    ok(PromptError == prompt, "\"%ls\": expected error %d, got %d\n", properties, prompt, PromptError);
    ok(!wcscmp(PromptProduct, L"msi_apitest reboot"), "\"%ls\": product \"%ls\"\n", properties, PromptProduct);
}

START_TEST(reboot)
{
    static const PCWSTR Products[] =
    {
        L"{6C1D5E3A-8F0B-4D27-9E4C-2B7A1F0D9C37}",
        L"{6C1D5E3A-8F0B-4D27-9E4C-2B7A1F0D9C38}",
        L"{6C1D5E3A-8F0B-4D27-9E4C-2B7A1F0D9C39}",
        L"{6C1D5E3A-8F0B-4D27-9E4C-2B7A1F0D9C3A}",
    };
    WCHAR temp[MAX_PATH], plain[MAX_PATH], schedule[MAX_PATH], force[3][MAX_PATH];
    BOOL created;
    UINT i;

    GetTempPathW(ARRAYSIZE(temp), temp);
    StringCbPrintfW(plain, sizeof(plain), L"%lsmsi_reboot_plain.msi", temp);
    StringCbPrintfW(schedule, sizeof(schedule), L"%lsmsi_reboot_schedule.msi", temp);
    created = create_package(plain, Plain, Products[0]) && create_package(schedule, Schedule, Products[0]);
    for (i = 0; i < ARRAYSIZE(force); i++)
    {
        StringCbPrintfW(force[i], sizeof(force[i]), L"%lsmsi_reboot_force%u.msi", temp, i);
        created = created && create_package(force[i], Force, Products[i + 1]);
    }
    if (!created)
    {
        skip("Cannot create the test packages\n");
        goto cleanup;
    }

    MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);
    MsiSetExternalUIRecord(ui_handler, INSTALLLOGMODE_USER, NULL, NULL);

    test_install(plain, Products[0], L"", ERROR_SUCCESS, 0);
    test_install(plain, Products[0], L"REBOOT=Force", ERROR_SUCCESS_REBOOT_REQUIRED, 1703);
    test_install(plain, Products[0], L"REBOOT=Force REBOOTPROMPT=\"\"", ERROR_SUCCESS_REBOOT_REQUIRED, 1703);
    test_install(schedule, Products[0], L"", ERROR_SUCCESS_REBOOT_REQUIRED, 1703);
    test_install(schedule, Products[0], L"REBOOT=Suppress", ERROR_SUCCESS_REBOOT_REQUIRED, 0);
    test_install(schedule, Products[0], L"REBOOT=ReallySuppress", ERROR_SUCCESS_REBOOT_REQUIRED, 0);
    test_install(schedule, Products[0], L"REBOOT=reallysuppress", ERROR_SUCCESS_REBOOT_REQUIRED, 0);
    test_install(schedule, Products[0], L"REBOOT=R", ERROR_SUCCESS_REBOOT_REQUIRED, 0);
    test_install(force[0], Products[1], L"", ERROR_INSTALL_SUSPEND, 1702);
    test_install(force[1], Products[2], L"REBOOT=Suppress", ERROR_INSTALL_SUSPEND, 1702);
    test_install(force[2], Products[3], L"REBOOT=ReallySuppress", ERROR_INSTALL_SUSPEND, 0);

    if (set_shutdown_privilege(0))
    {
        test_install(plain, Products[0], L"", ERROR_SUCCESS, 0);
        ok(get_shutdown_privilege() == SE_PRIVILEGE_ENABLED, "SeShutdownPrivilege attributes %ld\n",
           get_shutdown_privilege());
    }
    else
    {
        skip("Cannot disable SeShutdownPrivilege\n");
    }

    set_shutdown_privilege(SE_PRIVILEGE_REMOVED);
    if (get_shutdown_privilege() == -1)
    {
        Answer = IDYES;
        test_install(plain, Products[0], L"REBOOT=Force", ERROR_SUCCESS_REBOOT_INITIATED, 1703);
        Answer = IDNO;
        Sleep(5000);
        ok(!GetSystemMetrics(SM_SHUTTINGDOWN), "a restart started without SeShutdownPrivilege\n");
    }
    else
    {
        skip("Cannot remove SeShutdownPrivilege\n");
    }

    MsiSetExternalUIRecord(NULL, 0, NULL, NULL);

cleanup:
    DeleteFileW(plain);
    DeleteFileW(schedule);
    for (i = 0; i < ARRAYSIZE(force); i++)
        DeleteFileW(force[i]);
}
