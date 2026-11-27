/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     InstallFiles skips a file that Windows Resource Protection owns
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <msi.h>
#include <msidefs.h>
#include <msiquery.h>
#include <strsafe.h>

#define PRODUCT_CODE L"{6E1A9C42-3B7D-4F58-A2E6-0D9C4B71F835}"

static const PCWSTR Tables[] =
{
    L"CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL PRIMARY KEY `Property`)",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductName', 'msi_apitest protectedfile')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductVersion', '1.0.0')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductLanguage', '1033')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('Manufacturer', 'LiberNT')",
    L"INSERT INTO `Property` (`Property`, `Value`) VALUES ('ProductCode', '" PRODUCT_CODE L"')",
    L"CREATE TABLE `Directory` (`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Directory`)",
    L"INSERT INTO `Directory` (`Directory`, `Directory_Parent`, `DefaultDir`) VALUES ('TARGETDIR', '', 'SourceDir')",
    L"INSERT INTO `Directory` (`Directory`, `Directory_Parent`, `DefaultDir`) VALUES ('INSTALLDIR', 'TARGETDIR', '.')",
    L"CREATE TABLE `Feature` (`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64), `Description` CHAR(255), `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL PRIMARY KEY `Feature`)",
    L"INSERT INTO `Feature` (`Feature`, `Feature_Parent`, `Title`, `Description`, `Display`, `Level`, `Directory_`, `Attributes`) VALUES ('f', '', '', '', 0, 1, '', 0)",
    L"CREATE TABLE `Component` (`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72) PRIMARY KEY `Component`)",
    L"INSERT INTO `Component` (`Component`, `ComponentId`, `Directory_`, `Attributes`, `Condition`, `KeyPath`) VALUES ('c', '{A47C2E19-6D3B-4B85-9F10-E2C83D5A7B64}', 'INSTALLDIR', 16, '', 'file')",
    L"CREATE TABLE `FeatureComponents` (`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Feature_`, `Component_`)",
    L"INSERT INTO `FeatureComponents` (`Feature_`, `Component_`) VALUES ('f', 'c')",
    L"CREATE TABLE `File` (`File` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL LOCALIZABLE, `FileSize` LONG NOT NULL, `Version` CHAR(72), `Language` CHAR(20), `Attributes` SHORT, `Sequence` LONG NOT NULL PRIMARY KEY `File`)",
    L"INSERT INTO `File` (`File`, `Component_`, `FileName`, `FileSize`, `Version`, `Language`, `Attributes`, `Sequence`) VALUES ('file', 'c', 'notepad.exe', 5, '65535.65535.65535.65535', '', 8192, 1)",
    L"CREATE TABLE `Media` (`DiskId` SHORT NOT NULL, `LastSequence` LONG NOT NULL, `DiskPrompt` CHAR(64), `Cabinet` CHAR(255), `VolumeLabel` CHAR(32), `Source` CHAR(72) PRIMARY KEY `DiskId`)",
    L"INSERT INTO `Media` (`DiskId`, `LastSequence`, `DiskPrompt`, `Cabinet`, `VolumeLabel`, `Source`) VALUES (1, 1, '', '', 'DISK1', '')",
    L"CREATE TABLE `InstallExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostInitialize', 100)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('FileCost', 200)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('CostFinalize', 300)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallValidate', 400)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallInitialize', 500)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('ProcessComponents', 550)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallFiles', 600)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('RegisterProduct', 650)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('PublishProduct', 660)",
    L"INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES ('InstallFinalize', 700)",
    NULL
};

typedef struct _FILE_SNAPSHOT
{
    BOOL Present;
    ULARGE_INTEGER Size;
    FILETIME LastWrite;
    BYTE Head[64];
    DWORD HeadLength;
} FILE_SNAPSHOT;

static WCHAR SourceDir[MAX_PATH], Package[MAX_PATH], Target[MAX_PATH];

static PCWSTR PackageTemplate(void)
{
    SYSTEM_INFO info;

    GetSystemInfo(&info);
    switch (info.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_INTEL: return L"Intel;1033";
        case PROCESSOR_ARCHITECTURE_AMD64: return L"x64;1033";
        case PROCESSOR_ARCHITECTURE_ARM64: return L"Arm64;1033";
        default: return NULL;
    }
}

static BOOL RunQueries(MSIHANDLE db, const PCWSTR *queries)
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

static BOOL CreatePackage(PCWSTR template)
{
    MSIHANDLE db, summary;
    BOOL res;

    DeleteFileW(Package);
    if (MsiOpenDatabaseW(Package, (LPCWSTR)MSIDBOPEN_CREATE, &db) != ERROR_SUCCESS)
        return FALSE;
    res = RunQueries(db, Tables);
    if (res && MsiGetSummaryInformationW(db, NULL, 5, &summary) == ERROR_SUCCESS)
    {
        res = MsiSummaryInfoSetPropertyW(summary, PID_TEMPLATE, VT_LPSTR, 0, NULL, template) == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_REVNUMBER, VT_LPSTR, 0, NULL,
                                         L"{C3D85B2A-91E4-4A67-8B3F-5E0A7C1D9264}") == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_PAGECOUNT, VT_I4, 200, NULL, NULL) == ERROR_SUCCESS &&
              MsiSummaryInfoSetPropertyW(summary, PID_WORDCOUNT, VT_I4, 0, NULL, NULL) == ERROR_SUCCESS &&
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

static BOOL PutSource(void)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD written = 0;
    BOOL res;

    StringCbPrintfW(path, sizeof(path), L"%ls\\notepad.exe", SourceDir);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    res = WriteFile(file, "newer", 5, &written, NULL) && written == 5;
    CloseHandle(file);
    return res;
}

static void TakeSnapshot(FILE_SNAPSHOT *snapshot)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    HANDLE file;

    ZeroMemory(snapshot, sizeof(*snapshot));
    if (!GetFileAttributesExW(Target, GetFileExInfoStandard, &data))
        return;
    snapshot->Present = TRUE;
    snapshot->Size.LowPart = data.nFileSizeLow;
    snapshot->Size.HighPart = data.nFileSizeHigh;
    snapshot->LastWrite = data.ftLastWriteTime;
    file = CreateFileW(Target, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    ReadFile(file, snapshot->Head, sizeof(snapshot->Head), &snapshot->HeadLength, NULL);
    CloseHandle(file);
}

static void CheckUnchanged(const FILE_SNAPSHOT *before, PCSTR step)
{
    FILE_SNAPSHOT after;

    TakeSnapshot(&after);
    ok(after.Present, "%s: %ls is gone\n", step, Target);
    ok(after.Size.QuadPart == before->Size.QuadPart, "%s: size changed from %I64u to %I64u\n",
       step, before->Size.QuadPart, after.Size.QuadPart);
    ok(CompareFileTime(&after.LastWrite, &before->LastWrite) == 0, "%s: last write time changed\n", step);
    ok(after.HeadLength == before->HeadLength &&
       memcmp(after.Head, before->Head, before->HeadLength) == 0, "%s: contents changed\n", step);
}

START_TEST(protectedfile)
{
    BOOL (WINAPI *pSfcIsFileProtected)(HANDLE, LPCWSTR);
    WCHAR temp[MAX_PATH], system[MAX_PATH], properties[MAX_PATH + 32], path[MAX_PATH];
    FILE_SNAPSHOT before;
    PCWSTR template;
    HMODULE sfc;
    UINT r;

    template = PackageTemplate();
    if (!template)
    {
        skip("No package platform for this processor architecture\n");
        return;
    }
    GetSystemDirectoryW(system, ARRAYSIZE(system));
    StringCbPrintfW(Target, sizeof(Target), L"%ls\\notepad.exe", system);
    sfc = LoadLibraryW(L"sfc_os.dll");
    pSfcIsFileProtected = sfc ? (void *)GetProcAddress(sfc, "SfcIsFileProtected") : NULL;
    ok(pSfcIsFileProtected != NULL, "SfcIsFileProtected not found\n");
    if (!pSfcIsFileProtected)
        return;
    if (!pSfcIsFileProtected(NULL, Target))
    {
        skip("%ls is not protected by Windows Resource Protection\n", Target);
        FreeLibrary(sfc);
        return;
    }
    FreeLibrary(sfc);

    GetTempPathW(ARRAYSIZE(temp), temp);
    StringCbPrintfW(SourceDir, sizeof(SourceDir), L"%lsmsi_protectedfile", temp);
    StringCbPrintfW(Package, sizeof(Package), L"%ls\\protectedfile.msi", SourceDir);
    CreateDirectoryW(SourceDir, NULL);
    if (!CreatePackage(template) || !PutSource())
    {
        skip("Cannot create the test package\n");
        return;
    }

    MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);
    if (MsiQueryProductStateW(PRODUCT_CODE) != INSTALLSTATE_UNKNOWN)
        MsiConfigureProductW(PRODUCT_CODE, INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT);
    TakeSnapshot(&before);
    ok(before.Present, "%ls not found\n", Target);

    StringCbPrintfW(properties, sizeof(properties), L"INSTALLDIR=\"%ls\"", system);
    r = MsiInstallProductW(Package, properties);
    ok(r == ERROR_SUCCESS, "MsiInstallProduct returned %u\n", r);
    CheckUnchanged(&before, "install");
    ok(MsiQueryProductStateW(PRODUCT_CODE) == INSTALLSTATE_DEFAULT, "product state %d\n",
       MsiQueryProductStateW(PRODUCT_CODE));

    r = MsiConfigureProductW(PRODUCT_CODE, INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT);
    ok(r == ERROR_SUCCESS, "MsiConfigureProduct returned %u\n", r);
    CheckUnchanged(&before, "uninstall");
    ok(MsiQueryProductStateW(PRODUCT_CODE) == INSTALLSTATE_UNKNOWN, "product state %d after uninstall\n",
       MsiQueryProductStateW(PRODUCT_CODE));

    StringCbPrintfW(path, sizeof(path), L"%ls\\notepad.exe", SourceDir);
    DeleteFileW(path);
    DeleteFileW(Package);
    RemoveDirectoryW(SourceDir);
}
