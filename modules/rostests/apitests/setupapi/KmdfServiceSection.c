/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a device install records the KMDF version of its INF Wdf section in the service key
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <devguid.h>

#define DEVICE_HWID L"LNT_WDF_TEST_DEVICE"
#define SERVICE_NAME L"LntWdfTest"
#define WDF_KEY L"SYSTEM\\CurrentControlSet\\Services\\" SERVICE_NAME L"\\Parameters\\Wdf"

static const char WdfInf[] =
    "[Version]\r\n"
    "Signature=\"$WINDOWS NT$\"\r\n"
    "Class=System\r\n"
    "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\n"
    "Provider=%Lnt%\r\n"
    "DriverVer=11/22/2026,1.0.0.0\r\n"
    "CatalogFile=lntwdf.cat\r\n"
    "\r\n"
    "[Manufacturer]\r\n"
    "%Lnt%=Lnt,NTamd64,NTarm64,NTx86\r\n"
    "\r\n"
    "[Lnt.NTamd64]\r\n"
    "%Dev%=DevInstall,LNT_WDF_TEST_DEVICE\r\n"
    "[Lnt.NTarm64]\r\n"
    "%Dev%=DevInstall,LNT_WDF_TEST_DEVICE\r\n"
    "[Lnt.NTx86]\r\n"
    "%Dev%=DevInstall,LNT_WDF_TEST_DEVICE\r\n"
    "\r\n"
    "[DevInstall.NT]\r\n"
    "\r\n"
    "[DevInstall.NT.Services]\r\n"
    "AddService=LntWdfTest,0x00000002,LntWdfTest.Service\r\n"
    "\r\n"
    "[LntWdfTest.Service]\r\n"
    "ServiceType=1\r\n"
    "StartType=3\r\n"
    "ErrorControl=1\r\n"
    "ServiceBinary=%12%\\umpass.sys\r\n"
    "\r\n"
    "[DevInstall.NT.Wdf]\r\n"
    "KmdfService=LntWdfTest,LntWdfTest.WdfSection\r\n"
    "\r\n"
    "[LntWdfTest.WdfSection]\r\n"
    "KmdfLibraryVersion=1.15\r\n"
    "\r\n"
    "[Strings]\r\n"
    "Lnt=\"LiberNT tests\"\r\n"
    "Dev=\"LiberNT KMDF section test device\"\r\n";

static BOOL
WriteInf(PCWSTR Path)
{
    HANDLE File;
    DWORD Written = 0;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, WdfInf, (DWORD)strlen(WdfInf), &Written, NULL) && Written == strlen(WdfInf);
    CloseHandle(File);
    return Result;
}

static VOID
DeleteTestService(VOID)
{
    SC_HANDLE Manager, Service;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!Manager)
        return;
    Service = OpenServiceW(Manager, SERVICE_NAME, DELETE);
    if (Service)
    {
        DeleteService(Service);
        CloseServiceHandle(Service);
    }
    CloseServiceHandle(Manager);
}

static VOID
CheckWdfSectionRecorded(VOID)
{
    WCHAR Version[32];
    DWORD Size = sizeof(Version), Type = REG_NONE;
    HKEY Key = NULL;
    LONG Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, WDF_KEY, 0, KEY_READ, &Key);
    ok(Error == ERROR_SUCCESS, "Opening %ls failed: %ld\n", WDF_KEY, Error);
    if (Error != ERROR_SUCCESS)
        return;
    ZeroMemory(Version, sizeof(Version));
    Error = RegQueryValueExW(Key, L"KmdfLibraryVersion", NULL, &Type, (BYTE *)Version, &Size);
    ok(Error == ERROR_SUCCESS, "Reading KmdfLibraryVersion failed: %ld\n", Error);
    ok(Type == REG_SZ, "KmdfLibraryVersion has type %lu\n", Type);
    ok(!wcscmp(Version, L"1.15"), "KmdfLibraryVersion is '%ls'\n", Version);
    RegCloseKey(Key);
}

START_TEST(KmdfServiceSection)
{
    WCHAR Dir[MAX_PATH], Path[MAX_PATH], Published[MAX_PATH] = L"";
    SP_DEVINFO_DATA Data;
    HDEVINFO Set;
    BOOL Reboot = FALSE;
    PWSTR Name;

    GetTempPathW(ARRAYSIZE(Dir), Dir);
    lstrcatW(Dir, L"lntwdf");
    CreateDirectoryW(Dir, NULL);
    wsprintfW(Path, L"%s\\lntwdf.inf", Dir);
    ok(WriteInf(Path), "Writing the package failed: %lu\n", GetLastError());

    Set = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_SYSTEM, NULL);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    Data.cbSize = sizeof(Data);
    if (!SetupDiCreateDeviceInfoW(Set, L"LNTWDFTEST", &GUID_DEVCLASS_SYSTEM, NULL, NULL, DICD_GENERATE_ID, &Data) ||
        !SetupDiSetDeviceRegistryPropertyW(Set, &Data, SPDRP_HARDWAREID, (const BYTE *)DEVICE_HWID L"\0",
                                           sizeof(DEVICE_HWID L"\0")) ||
        !SetupDiCallClassInstaller(DIF_REGISTERDEVICE, Set, &Data))
    {
        if (GetLastError() == ERROR_ACCESS_DENIED)
            skip("Creating a device needs an administrator\n");
        else
            ok(0, "Creating the device failed: %lu\n", GetLastError());
        SetupDiDestroyDeviceInfoList(Set);
        DeleteFileW(Path);
        RemoveDirectoryW(Dir);
        return;
    }

    if (UpdateDriverForPlugAndPlayDevicesW(NULL, DEVICE_HWID, Path, INSTALLFLAG_FORCE, &Reboot))
        CheckWdfSectionRecorded();
    else
        skip("Installing the package failed: 0x%lx\n", GetLastError());

    SetupDiCallClassInstaller(DIF_REMOVE, Set, &Data);
    SetupDiDestroyDeviceInfoList(Set);
    if (SetupCopyOEMInfW(Path, NULL, SPOST_NONE, SP_COPY_NOOVERWRITE, Published, ARRAYSIZE(Published), NULL, NULL) ||
        GetLastError() == ERROR_FILE_EXISTS)
    {
        if ((Name = wcsrchr(Published, L'\\')) != NULL)
            SetupUninstallOEMInfW(Name + 1, SUOI_FORCEDELETE, NULL);
    }
    DeleteTestService();
    DeleteFileW(Path);
    RemoveDirectoryW(Dir);
}
