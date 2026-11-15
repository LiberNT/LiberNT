/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The FeatureScore directive of an INF is a hexadecimal byte that ranks its driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>

static const CHAR PlainInf[] =
    "[Version]\r\n"
    "Signature=\"$Windows NT$\"\r\n"
    "Class=Mouse\r\n"
    "ClassGuid={4d36e96f-e325-11ce-bfc1-08002be10318}\r\n"
    "Provider=LiberNT\r\n"
    "DriverVer=01/01/2026,1.0.0.0\r\n"
    "[Manufacturer]\r\n"
    "LiberNT=Models\r\n"
    "[Models]\r\n"
    "\"Plain driver\"=Plain_Inst,ROOT\\LNTFEATURESCORE\r\n"
    "[Plain_Inst]\r\n";

static const CHAR ScoredInf[] =
    "[Version]\r\n"
    "Signature=\"$Windows NT$\"\r\n"
    "Class=Mouse\r\n"
    "ClassGuid={4d36e96f-e325-11ce-bfc1-08002be10318}\r\n"
    "Provider=LiberNT\r\n"
    "DriverVer=01/01/2025,1.0.0.0\r\n"
    "[Manufacturer]\r\n"
    "LiberNT=Models\r\n"
    "[Models]\r\n"
    "\"Scored driver\"=Scored_Inst,ROOT\\LNTFEATUREOTHER,ROOT\\LNTFEATURESCORE\r\n"
    "[Scored_Inst]\r\n"
    "FeatureScore=FE\r\n";

static BOOL
WriteInf(PCWSTR Directory, PCWSTR Name, PCSTR Text)
{
    WCHAR Path[MAX_PATH];
    DWORD Written = 0;
    HANDLE File;
    BOOL Result;

    wsprintfW(Path, L"%s\\%s", Directory, Name);
    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, Text, lstrlenA(Text), &Written, NULL) && Written == (DWORD)lstrlenA(Text);
    CloseHandle(File);
    return Result;
}

START_TEST(DriverFeatureScore)
{
    static const WCHAR HardwareId[] = L"ROOT\\LNTFEATURESCORE\0";
    WCHAR Directory[MAX_PATH], Path[MAX_PATH];
    SP_DEVINSTALL_PARAMS_W InstallParams;
    SP_DRVINSTALL_PARAMS DriverParams;
    SP_DRVINFO_DATA_W Driver;
    SP_DEVINFO_DATA Device;
    DWORD Index, PlainRank = MAXDWORD, ScoredRank = MAXDWORD;
    HDEVINFO Devices;
    BOOL Result;

    GetTempPathW(ARRAYSIZE(Directory), Directory);
    lstrcatW(Directory, L"LntFeatureScore");
    CreateDirectoryW(Directory, NULL);
    if (!WriteInf(Directory, L"lntplain.inf", PlainInf) || !WriteInf(Directory, L"lntscored.inf", ScoredInf))
    {
        skip("Could not write the test INF files (%lu)\n", GetLastError());
        return;
    }

    Devices = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_MOUSE, NULL);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed (%lu)\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE)
        return;

    Device.cbSize = sizeof(Device);
    Result = SetupDiCreateDeviceInfoW(Devices, L"LntFeatureScore", &GUID_DEVCLASS_MOUSE, NULL, NULL, DICD_GENERATE_ID, &Device);
    ok(Result, "SetupDiCreateDeviceInfoW failed (%lu)\n", GetLastError());
    if (!Result)
        goto Cleanup;

    Result = SetupDiSetDeviceRegistryPropertyW(Devices, &Device, SPDRP_HARDWAREID, (const BYTE *)HardwareId, sizeof(HardwareId));
    ok(Result, "Setting the hardware ID failed (%lu)\n", GetLastError());

    ZeroMemory(&InstallParams, sizeof(InstallParams));
    InstallParams.cbSize = sizeof(InstallParams);
    Result = SetupDiGetDeviceInstallParamsW(Devices, &Device, &InstallParams);
    ok(Result, "SetupDiGetDeviceInstallParamsW failed (%lu)\n", GetLastError());
    lstrcpynW(InstallParams.DriverPath, Directory, ARRAYSIZE(InstallParams.DriverPath));
    InstallParams.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
    Result = SetupDiSetDeviceInstallParamsW(Devices, &Device, &InstallParams);
    ok(Result, "SetupDiSetDeviceInstallParamsW failed (%lu)\n", GetLastError());

    Result = SetupDiBuildDriverInfoList(Devices, &Device, SPDIT_COMPATDRIVER);
    ok(Result, "SetupDiBuildDriverInfoList failed (%lu)\n", GetLastError());

    Driver.cbSize = sizeof(Driver);
    for (Index = 0; SetupDiEnumDriverInfoW(Devices, &Device, SPDIT_COMPATDRIVER, Index, &Driver); Index++)
    {
        DriverParams.cbSize = sizeof(DriverParams);
        if (!SetupDiGetDriverInstallParamsW(Devices, &Device, &Driver, &DriverParams))
            continue;
        if (!lstrcmpW(Driver.Description, L"Plain driver"))
            PlainRank = DriverParams.Rank;
        else if (!lstrcmpW(Driver.Description, L"Scored driver"))
            ScoredRank = DriverParams.Rank;
    }

    ok(PlainRank != MAXDWORD, "The driver without a feature score was not listed\n");
    ok(ScoredRank != MAXDWORD, "The driver with a feature score was not listed\n");
    if (PlainRank != MAXDWORD && ScoredRank != MAXDWORD)
    {
        ok(((PlainRank >> 16) & 0xFF) == 0xFF, "Rank %#lx of a driver without the directive\n", PlainRank);
        ok(((ScoredRank >> 16) & 0xFF) == 0xFE, "Rank %#lx of a driver with FeatureScore=FE\n", ScoredRank);
        ok((PlainRank & 0xFFFF) < (ScoredRank & 0xFFFF), "Identifier scores %#lx and %#lx\n", PlainRank & 0xFFFF, ScoredRank & 0xFFFF);
        ok(ScoredRank < PlainRank, "The feature score must outweigh the identifier score: %#lx against %#lx\n", ScoredRank, PlainRank);

        Result = SetupDiSelectBestCompatDrv(Devices, &Device);
        ok(Result, "SetupDiSelectBestCompatDrv failed (%lu)\n", GetLastError());
        Driver.cbSize = sizeof(Driver);
        Result = SetupDiGetSelectedDriverW(Devices, &Device, &Driver);
        ok(Result, "SetupDiGetSelectedDriverW failed (%lu)\n", GetLastError());
        if (Result)
            ok(!lstrcmpW(Driver.Description, L"Scored driver"), "Selected '%ls'\n", Driver.Description);
    }

    SetupDiDestroyDriverInfoList(Devices, &Device, SPDIT_COMPATDRIVER);

Cleanup:
    SetupDiDestroyDeviceInfoList(Devices);
    wsprintfW(Path, L"%s\\lntplain.inf", Directory);
    DeleteFileW(Path);
    wsprintfW(Path, L"%s\\lntscored.inf", Directory);
    DeleteFileW(Path);
    RemoveDirectoryW(Directory);
}
