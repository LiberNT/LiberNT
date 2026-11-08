/*
 * PROJECT:     LiberNT Driver Installer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that removing a catalog driver removes it from every device that uses it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include "devices.hpp"

#include <setupapi.h>
#include <newdev.h>
#include <devguid.h>

using namespace lntdrv;

static const WCHAR TestHardwareId[] = L"ROOT\\LNTDRVTEST";
static const char TestInf[] =
    "[Version]\r\n"
    "Signature=\"$WINDOWS NT$\"\r\n"
    "Class=System\r\n"
    "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\n"
    "Provider=%Provider%\r\n"
    "DriverVer=11/08/2026,1.0.0.0\r\n"
    "\r\n"
    "[Manufacturer]\r\n"
    "%Provider%=Models,NTamd64,NTx86,NTarm64\r\n"
    "\r\n"
    "[Models.NTamd64]\r\n"
    "%Device%=Install,ROOT\\LNTDRVTEST\r\n"
    "[Models.NTx86]\r\n"
    "%Device%=Install,ROOT\\LNTDRVTEST\r\n"
    "[Models.NTarm64]\r\n"
    "%Device%=Install,ROOT\\LNTDRVTEST\r\n"
    "\r\n"
    "[Install]\r\n"
    "[Install.Services]\r\n"
    "AddService=,0x00000002\r\n"
    "\r\n"
    "[Strings]\r\n"
    "Provider=\"LiberNT Test\"\r\n"
    "Device=\"lntdrv remove test device\"\r\n";

static bool IsTestDevice(HDEVINFO set, SP_DEVINFO_DATA *data)
{
    WCHAR ids[256] = L"";

    return SetupDiGetDeviceRegistryPropertyW(set, data, SPDRP_HARDWAREID, NULL, (PBYTE)ids, sizeof(ids) - 2 * sizeof(WCHAR),
                                             NULL) &&
           _wcsicmp(ids, TestHardwareId) == 0;
}

static void RemoveTestDevices()
{
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_SYSTEM, L"ROOT", NULL, 0);
    SP_DEVINFO_DATA data = { sizeof(data) };

    if (set == INVALID_HANDLE_VALUE)
        return;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &data); i++)
    {
        if (IsTestDevice(set, &data))
            SetupDiCallClassInstaller(DIF_REMOVE, set, &data);
    }
    SetupDiDestroyDeviceInfoList(set);
}

static bool CreateTestDevice()
{
    HDEVINFO set = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_SYSTEM, NULL);
    SP_DEVINFO_DATA data = { sizeof(data) };
    WCHAR ids[ARRAYSIZE(TestHardwareId) + 1] = L"";
    bool created;

    if (set == INVALID_HANDLE_VALUE)
        return false;
    memcpy(ids, TestHardwareId, sizeof(TestHardwareId));
    created = SetupDiCreateDeviceInfoW(set, L"LNTDRVTEST", &GUID_DEVCLASS_SYSTEM, NULL, NULL, DICD_GENERATE_ID, &data) &&
              SetupDiSetDeviceRegistryPropertyW(set, &data, SPDRP_HARDWAREID, (const BYTE *)ids, sizeof(ids)) &&
              SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &data);
    if (!created)
        trace("creating a test device failed with %lu\n", GetLastError());
    SetupDiDestroyDeviceInfoList(set);
    return created;
}

static std::vector<DeviceInfo> TestDevices()
{
    std::vector<DeviceInfo> devices, found;

    EnumerateDevices(devices);
    for (const DeviceInfo &device : devices)
    {
        if (!device.HardwareIds.empty() && _wcsicmp(device.HardwareIds.front().c_str(), TestHardwareId) == 0)
            found.push_back(device);
    }
    return found;
}

START_TEST(RemoveDriver)
{
    WCHAR directory[MAX_PATH], inf[MAX_PATH], windows[MAX_PATH];
    std::vector<DeviceInfo> devices;
    std::wstring oem;
    HistoryEntry entry;
    BOOL reboot = FALSE;
    HANDLE file;
    DWORD written = 0, error;

    RemoveTestDevices();
    if (!GetTempPathW(ARRAYSIZE(directory), directory) || !GetWindowsDirectoryW(windows, ARRAYSIZE(windows)))
    {
        skip("no temporary or Windows directory\n");
        return;
    }
    wcscat(directory, L"lntdrvtest");
    CreateDirectoryW(directory, NULL);
    wcscpy(inf, directory);
    wcscat(inf, L"\\lntdrvtest.inf");
    file = CreateFileW(inf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "creating the test INF failed with %lu\n", GetLastError());
    if (file == INVALID_HANDLE_VALUE)
        return;
    WriteFile(file, TestInf, sizeof(TestInf) - 1, &written, NULL);
    CloseHandle(file);

    if (!CreateTestDevice() || !CreateTestDevice())
    {
        skip("cannot create root test devices\n");
        RemoveTestDevices();
        return;
    }
    if (!UpdateDriverForPlugAndPlayDevicesW(NULL, TestHardwareId, inf, INSTALLFLAG_FORCE, &reboot))
    {
        skip("installing the test driver failed with %lu\n", GetLastError());
        RemoveTestDevices();
        return;
    }

    devices = TestDevices();
    ok(devices.size() == 2, "%u test devices, expected 2\n", (unsigned)devices.size());
    if (devices.size() != 2 || devices[0].Inf.empty() || _wcsicmp(devices[0].Inf.c_str(), devices[1].Inf.c_str()) != 0)
    {
        skip("the test devices do not share one installed INF\n");
        RemoveTestDevices();
        return;
    }
    oem = devices[0].Inf;
    trace("both test devices use %ls\n", oem.c_str());

    entry.Action = L"install";
    entry.InstanceId = devices[0].InstanceId;
    entry.Inf = oem;
    error = RemoveCatalogDriver(entry, reboot);
    ok(error == ERROR_SUCCESS, "RemoveCatalogDriver returned %lu\n", error);

    for (const DeviceInfo &device : TestDevices())
        ok(_wcsicmp(device.Inf.c_str(), oem.c_str()) != 0, "%ls still uses %ls after its INF was removed\n",
           device.InstanceId.c_str(), oem.c_str());
    ok(GetFileAttributesW((std::wstring(windows) + L"\\inf\\" + oem).c_str()) == INVALID_FILE_ATTRIBUTES,
       "%ls is still installed\n", oem.c_str());

    RemoveTestDevices();
    SetupUninstallOEMInfW(oem.c_str(), SUOI_FORCEDELETE, NULL);
    DeleteFileW(inf);
    RemoveDirectoryW(directory);
}
