/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a package entering the driver store creates the root devices of its DeviceInstall32 section
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#define DEVICE_ID L"ROOT\\LNTROOTDEV\\0000"
#define DEVICE_HWID L"ROOT\\LNTROOTDEV"

static const char RootDeviceInf[] =
    "[Version]\r\n"
    "Signature=\"$WINDOWS NT$\"\r\n"
    "Class=System\r\n"
    "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\n"
    "Provider=%Lnt%\r\n"
    "DriverVer=11/22/2026,1.0.0.0\r\n"
    "CatalogFile=lntrootdev.cat\r\n"
    "\r\n"
    "[DeviceInstall32]\r\n"
    "AddDevice=ROOT\\LNTROOTDEV\\0000,,LntRootDev.RootDevice\r\n"
    "\r\n"
    "[LntRootDev.RootDevice]\r\n"
    "HardwareIds=ROOT\\LNTROOTDEV\r\n"
    "\r\n"
    "[Manufacturer]\r\n"
    "%Lnt%=Lnt,NTamd64,NTarm64,NTx86\r\n"
    "\r\n"
    "[Lnt.NTamd64]\r\n"
    "%Dev%=DevInstall,ROOT\\LNTROOTDEV\r\n"
    "[Lnt.NTarm64]\r\n"
    "%Dev%=DevInstall,ROOT\\LNTROOTDEV\r\n"
    "[Lnt.NTx86]\r\n"
    "%Dev%=DevInstall,ROOT\\LNTROOTDEV\r\n"
    "\r\n"
    "[DevInstall.NT]\r\n"
    "\r\n"
    "[DevInstall.NT.Services]\r\n"
    "AddService=,0x00000002\r\n"
    "\r\n"
    "[Strings]\r\n"
    "Lnt=\"LiberNT tests\"\r\n"
    "Dev=\"LiberNT root device test device\"\r\n";

static BOOL
WriteInf(PCWSTR Path)
{
    HANDLE File;
    DWORD Written = 0;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, RootDeviceInf, (DWORD)strlen(RootDeviceInf), &Written, NULL) &&
             Written == strlen(RootDeviceInf);
    CloseHandle(File);
    return Result;
}

static BOOL
RemoveRootDevice(VOID)
{
    SP_DEVINFO_DATA Data;
    HDEVINFO Set;
    BOOL Removed;

    Set = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (Set == INVALID_HANDLE_VALUE)
        return FALSE;
    Data.cbSize = sizeof(Data);
    Removed = SetupDiOpenDeviceInfoW(Set, DEVICE_ID, NULL, 0, &Data) &&
              SetupDiCallClassInstaller(DIF_REMOVE, Set, &Data);
    SetupDiDestroyDeviceInfoList(Set);
    return Removed;
}

static BOOL
CheckRootDeviceCreated(VOID)
{
    WCHAR Ids[64], Driver[64];
    ULONG Size;
    DEVINST DevInst;
    CONFIGRET cr;

    cr = CM_Locate_DevNodeW(&DevInst, DEVICE_ID, CM_LOCATE_DEVNODE_PHANTOM);
    ok(cr == CR_SUCCESS, "Staging created no %ls: %lu\n", DEVICE_ID, cr);
    if (cr != CR_SUCCESS)
        return FALSE;

    ZeroMemory(Ids, sizeof(Ids));
    Size = sizeof(Ids);
    cr = CM_Get_DevNode_Registry_PropertyW(DevInst, CM_DRP_HARDWAREID, NULL, Ids, &Size, 0);
    ok(cr == CR_SUCCESS, "Reading the hardware IDs failed: %lu\n", cr);
    ok(!_wcsicmp(Ids, DEVICE_HWID) && Ids[wcslen(Ids) + 1] == UNICODE_NULL, "Hardware IDs are '%ls'\n", Ids);

    ok(CMP_WaitNoPendingInstallEvents(5000) == WAIT_OBJECT_0, "PnP installs still pending\n");
    cr = CM_Locate_DevNodeW(&DevInst, DEVICE_ID, CM_LOCATE_DEVNODE_NORMAL);
    ok(cr == CR_SUCCESS, "%ls is not present: %lu\n", DEVICE_ID, cr);
    Size = sizeof(Driver);
    cr = CM_Get_DevNode_Registry_PropertyW(DevInst, CM_DRP_DRIVER, NULL, Driver, &Size, 0);
    ok(cr == CR_SUCCESS, "PnP installed no driver on %ls: %lu\n", DEVICE_ID, cr);
    return TRUE;
}

START_TEST(PackageRootDevice)
{
    WCHAR Dir[MAX_PATH], Path[MAX_PATH], Published[MAX_PATH] = L"", Again[MAX_PATH] = L"";
    DEVINST DevInst;
    PWSTR Name = NULL;

    GetTempPathW(ARRAYSIZE(Dir), Dir);
    lstrcatW(Dir, L"lntrootdev");
    CreateDirectoryW(Dir, NULL);
    wsprintfW(Path, L"%s\\lntrootdev.inf", Dir);
    ok(WriteInf(Path), "Writing the package failed: %lu\n", GetLastError());

    if (CM_Locate_DevNodeW(&DevInst, DEVICE_ID, CM_LOCATE_DEVNODE_PHANTOM) == CR_SUCCESS)
        RemoveRootDevice();

    if (!SetupCopyOEMInfW(Path, NULL, SPOST_PATH, 0, Published, ARRAYSIZE(Published), NULL, &Name))
    {
        skip("Staging the package failed: 0x%lx\n", GetLastError());
        DeleteFileW(Path);
        RemoveDirectoryW(Dir);
        return;
    }

    if (CheckRootDeviceCreated())
        ok(RemoveRootDevice(), "Removing %ls failed: 0x%lx\n", DEVICE_ID, GetLastError());
    ok(SetupCopyOEMInfW(Path, NULL, SPOST_PATH, 0, Again, ARRAYSIZE(Again), NULL, NULL),
       "Staging the package again failed: 0x%lx\n", GetLastError());
    ok(!_wcsicmp(Again, Published), "Staging again published %ls, not %ls\n", Again, Published);
    ok(CM_Locate_DevNodeW(&DevInst, DEVICE_ID, CM_LOCATE_DEVNODE_PHANTOM) == CR_NO_SUCH_DEVNODE,
       "Staging a package already in the store created %ls again\n", DEVICE_ID);

    if (CM_Locate_DevNodeW(&DevInst, DEVICE_ID, CM_LOCATE_DEVNODE_PHANTOM) == CR_SUCCESS)
        RemoveRootDevice();
    if (Name)
        ok(SetupUninstallOEMInfW(Name, SUOI_FORCEDELETE, NULL), "Removing %ls failed: 0x%lx\n", Name, GetLastError());
    DeleteFileW(Path);
    RemoveDirectoryW(Dir);
}
