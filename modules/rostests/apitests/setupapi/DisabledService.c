/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A device whose function driver service is disabled reports CM_PROB_DISABLED_SERVICE and does not start
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <devguid.h>

#define DEVICE_HWID L"LNT_DISABLED_SERVICE_TEST_DEVICE"
#define SERVICE_NAME L"LntDisabledSvcTest"

static const char DisabledInf[] =
    "[Version]\r\n"
    "Signature=\"$WINDOWS NT$\"\r\n"
    "Class=System\r\n"
    "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\n"
    "Provider=%Lnt%\r\n"
    "DriverVer=11/27/2026,1.0.0.0\r\n"
    "CatalogFile=lntdisabled.cat\r\n"
    "\r\n"
    "[Manufacturer]\r\n"
    "%Lnt%=Lnt,NTamd64,NTarm64,NTx86\r\n"
    "\r\n"
    "[Lnt.NTamd64]\r\n"
    "%Dev%=DevInstall,LNT_DISABLED_SERVICE_TEST_DEVICE\r\n"
    "[Lnt.NTarm64]\r\n"
    "%Dev%=DevInstall,LNT_DISABLED_SERVICE_TEST_DEVICE\r\n"
    "[Lnt.NTx86]\r\n"
    "%Dev%=DevInstall,LNT_DISABLED_SERVICE_TEST_DEVICE\r\n"
    "\r\n"
    "[DevInstall.NT]\r\n"
    "\r\n"
    "[DevInstall.NT.Services]\r\n"
    "AddService=LntDisabledSvcTest,0x00000002,LntDisabledSvcTest.Service\r\n"
    "\r\n"
    "[LntDisabledSvcTest.Service]\r\n"
    "ServiceType=1\r\n"
    "StartType=4\r\n"
    "ErrorControl=0\r\n"
    "ServiceBinary=%12%\\umpass.sys\r\n"
    "\r\n"
    "[Strings]\r\n"
    "Lnt=\"LiberNT tests\"\r\n"
    "Dev=\"LiberNT disabled service test device\"\r\n";

static BOOL
WriteInf(PCWSTR Path)
{
    HANDLE File;
    DWORD Written = 0;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, DisabledInf, (DWORD)strlen(DisabledInf), &Written, NULL) && Written == strlen(DisabledInf);
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
CheckDisabledService(HDEVINFO Set, PSP_DEVINFO_DATA Data)
{
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    ULONG Status = 0, Problem = 0;
    DEVINST DevInst;

    ok(SetupDiGetDeviceInstanceIdW(Set, Data, InstanceId, ARRAYSIZE(InstanceId), NULL),
       "SetupDiGetDeviceInstanceId failed: %lu\n", GetLastError());
    ok_int(CM_Locate_DevNodeW(&DevInst, InstanceId, CM_LOCATE_DEVNODE_NORMAL), CR_SUCCESS);
    ok_int(CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0), CR_SUCCESS);
    ok(!(Status & DN_STARTED), "the device started on a disabled service (status 0x%lx)\n", Status);
    ok((Status & DN_HAS_PROBLEM) && Problem == CM_PROB_DISABLED_SERVICE,
       "the device reports status 0x%lx problem %lu\n", Status, Problem);
}

START_TEST(DisabledService)
{
    WCHAR Dir[MAX_PATH], Path[MAX_PATH], Published[MAX_PATH] = L"";
    SP_DEVINFO_DATA Data;
    HDEVINFO Set;
    BOOL Reboot = FALSE;
    PWSTR Name;

    GetTempPathW(ARRAYSIZE(Dir), Dir);
    lstrcatW(Dir, L"lntdisabled");
    CreateDirectoryW(Dir, NULL);
    wsprintfW(Path, L"%s\\lntdisabled.inf", Dir);
    ok(WriteInf(Path), "Writing the package failed: %lu\n", GetLastError());

    Set = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_SYSTEM, NULL);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    Data.cbSize = sizeof(Data);
    if (!SetupDiCreateDeviceInfoW(Set, L"LNTDISABLEDSVC", &GUID_DEVCLASS_SYSTEM, NULL, NULL, DICD_GENERATE_ID, &Data) ||
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
        CheckDisabledService(Set, &Data);
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
