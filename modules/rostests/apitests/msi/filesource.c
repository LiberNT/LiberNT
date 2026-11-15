/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Source file names InstallFiles uses for an uncompressed source image
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <windows.h>
#include <msi.h>
#include <msidefs.h>
#include <msiquery.h>
#include <strsafe.h>

static const PCWSTR Tables[] =
{
    L"CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL PRIMARY KEY `Property`)",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductName', 'msi_apitest filesource')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductVersion', '1.0.0')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductLanguage', '1033')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('Manufacturer', 'LiberNT')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductCode', '{3B8E5F21-7C4A-4E19-A6D2-9F0B1C7E4A53}')",
    L"CREATE TABLE `Directory` (`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Directory`)",
    L"INSERT INTO `Directory` (`Directory`, `Directory_Parent`, `DefaultDir`) VALUES ('TARGETDIR', '', 'SourceDir')",
    L"INSERT INTO `Directory` (`Directory`, `Directory_Parent`, `DefaultDir`) VALUES ('INSTALLDIR', 'TARGETDIR', '.')",
    L"CREATE TABLE `Feature` (`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64), `Description` CHAR(255), `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL PRIMARY KEY `Feature`)",
    L"INSERT INTO `Feature` (`Feature`, `Feature_Parent`, `Title`, `Description`, `Display`, `Level`, `Directory_`, `Attributes`) VALUES ('f', '', '', '', 0, 1, '', 0)",
    L"CREATE TABLE `Component` (`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72) PRIMARY KEY `Component`)",
    L"INSERT INTO `Component` (`Component`, `ComponentId`, `Directory_`, `Attributes`, `Condition`, `KeyPath`) VALUES ('c', '{8D2C6A47-1E5B-4F93-B0A8-6C3E9D7F2B14}', 'INSTALLDIR', 0, '', 'file')",
    L"CREATE TABLE `FeatureComponents` (`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Feature_`, `Component_`)",
    L"INSERT INTO `FeatureComponents` (`Feature_`, `Component_`) VALUES ('f', 'c')",
    L"CREATE TABLE `File` (`File` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL LOCALIZABLE, `FileSize` LONG NOT NULL, `Version` CHAR(72), `Language` CHAR(20), `Attributes` SHORT, `Sequence` LONG NOT NULL PRIMARY KEY `File`)",
    L"INSERT INTO `File` (`File`, `Component_`, `FileName`, `FileSize`, `Version`, `Language`, `Attributes`, `Sequence`) VALUES ('file', 'c', 'SHORTN~1.TXT|longname.txt', 5, '', '', 8192, 1)",
    L"CREATE TABLE `Media` (`DiskId` SHORT NOT NULL, `LastSequence` LONG NOT NULL, `DiskPrompt` CHAR(64), `Cabinet` CHAR(255), `VolumeLabel` CHAR(32), `Source` CHAR(72) PRIMARY KEY `DiskId`)",
    L"INSERT INTO `Media` (`DiskId`, `LastSequence`, `DiskPrompt`, `Cabinet`, `VolumeLabel`, `Source`) VALUES (1, 1, '', '', 'DISK1', '')",
    L"CREATE TABLE `InstallExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostInitialize', 100)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('FileCost', 200)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostFinalize', 300)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallValidate', 400)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallInitialize', 500)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallFiles', 600)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallFinalize', 700)",
    NULL
};

static WCHAR SourceDir[MAX_PATH], TargetDir[MAX_PATH], Package[MAX_PATH];

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

static BOOL create_package(INT word_count)
{
    MSIHANDLE db, summary;
    BOOL res;

    DeleteFileW(Package);
    if (MsiOpenDatabaseW(Package, (LPCWSTR)MSIDBOPEN_CREATE, &db) != ERROR_SUCCESS)
        return FALSE;
    res = run_queries(db, Tables);

    if (res && MsiGetSummaryInformationW(db, NULL, 5, &summary) == ERROR_SUCCESS)
    {
        res = MsiSummaryInfoSetPropertyW(summary, PID_TEMPLATE, VT_LPSTR, 0, NULL, L"Intel;1033") == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_REVNUMBER, VT_LPSTR, 0, NULL,
                                         L"{5A7D3C19-2B6E-4F80-9C4D-E1F8A2B6C735}") == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_PAGECOUNT, VT_I4, 200, NULL, NULL) == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_WORDCOUNT, VT_I4, word_count, NULL, NULL) == ERROR_SUCCESS &&
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

static void put_source(PCWSTR name, PCSTR data)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD written;

    StringCbPrintfW(path, sizeof(path), L"%ls\\%ls", SourceDir, name);
    if (!data)
    {
        DeleteFileW(path);
        return;
    }
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(file != INVALID_HANDLE_VALUE, "CreateFile %ls failed: %lu\n", path, GetLastError());
    if (file == INVALID_HANDLE_VALUE)
        return;
    WriteFile(file, data, (DWORD)strlen(data), &written, NULL);
    CloseHandle(file);
}

static void get_target(PSTR data, DWORD size)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD read = 0;

    data[0] = 0;
    StringCbPrintfW(path, sizeof(path), L"%ls\\longname.txt", TargetDir);
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    ReadFile(file, data, size - 1, &read, NULL);
    data[read] = 0;
    CloseHandle(file);
}

static void test_source(INT word_count, PCSTR long_data, PCSTR short_data, UINT expected, PCSTR installed)
{
    WCHAR properties[MAX_PATH + 32];
    CHAR data[16];
    UINT r;

    if (!create_package(word_count))
    {
        skip("Cannot create the test package\n");
        return;
    }
    put_source(L"longname.txt", long_data);
    put_source(L"SHORTN~1.TXT", short_data);

    StringCbPrintfW(properties, sizeof(properties), L"INSTALLDIR=\"%ls\"", TargetDir);
    r = MsiInstallProductW(Package, properties);
    ok(r == expected, "word count %d, long %s, short %s: expected %u, got %u\n",
       word_count, long_data, short_data, expected, r);
    get_target(data, sizeof(data));
    ok(!strcmp(data, installed), "word count %d, long %s, short %s: expected \"%s\", got \"%s\"\n",
       word_count, long_data, short_data, installed, data);

    StringCbPrintfW(properties, sizeof(properties), L"%ls\\longname.txt", TargetDir);
    DeleteFileW(properties);
}

START_TEST(filesource)
{
    WCHAR temp[MAX_PATH];

    GetTempPathW(ARRAYSIZE(temp), temp);
    StringCbPrintfW(SourceDir, sizeof(SourceDir), L"%lsmsi_filesource", temp);
    StringCbPrintfW(TargetDir, sizeof(TargetDir), L"%lsmsi_filesource_target", temp);
    StringCbPrintfW(Package, sizeof(Package), L"%ls\\filesource.msi", SourceDir);
    CreateDirectoryW(SourceDir, NULL);
    CreateDirectoryW(TargetDir, NULL);

    MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);

    test_source(0, "long", "short", ERROR_SUCCESS, "long");
    test_source(msidbSumInfoSourceTypeSFN, "long", "short", ERROR_SUCCESS, "short");
    test_source(0, NULL, "short", ERROR_INSTALL_FAILURE, "");
    test_source(msidbSumInfoSourceTypeSFN, "long", NULL, ERROR_INSTALL_FAILURE, "");

    put_source(L"longname.txt", NULL);
    put_source(L"SHORTN~1.TXT", NULL);
    DeleteFileW(Package);
    RemoveDirectoryW(SourceDir);
    RemoveDirectoryW(TargetDir);
}
