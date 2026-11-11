/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A driver opening the driver key of a device registered without one gets a new key of the device's class
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <winreg.h>
#include <setupapi.h>

#define RESULT_KEY L"SOFTWARE\\KmtIoDriverKey"
#define SYSTEM_CLASS_KEY L"{4d36e97d-e325-11ce-bfc1-08002be10318}\\"

static const GUID SystemClass = {0x4d36e97d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR HardwareIds[] = L"ROOT\\KMTESTDRIVERKEY\0";

static
VOID
OpenDriverKey(
    _In_ HDEVINFO Devices,
    _In_ PSP_DEVINFO_DATA Device)
{
    WCHAR Name[128];
    WCHAR Driver[128];
    LONG Error;
    HKEY Key;

    ok(!SetupDiGetDeviceRegistryPropertyW(Devices, Device, SPDRP_DRIVER, NULL, (PBYTE)Driver, sizeof(Driver), NULL),
       "the registered device already has a driver key\n");

    if (!SetupDiGetDeviceRegistryPropertyW(Devices, Device, SPDRP_PHYSICAL_DEVICE_OBJECT_NAME, NULL, (PBYTE)Name,
                                           sizeof(Name), NULL))
    {
        ok(FALSE, "the registered device has no physical device object: %lu\n", GetLastError());
        return;
    }

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, RESULT_KEY, 0, NULL, REG_OPTION_VOLATILE, KEY_SET_VALUE, NULL, &Key, NULL);
    ok_eq_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return;

    Error = RegSetValueExW(Key, L"Pdo", 0, REG_SZ, (const BYTE *)Name, (DWORD)(wcslen(Name) + 1) * sizeof(WCHAR));
    RegCloseKey(Key);
    ok_eq_long(Error, ERROR_SUCCESS);

    KmtRunKernelTest("IoDriverKeyOpen");
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, RESULT_KEY);

    if (!SetupDiGetDeviceRegistryPropertyW(Devices, Device, SPDRP_DRIVER, NULL, (PBYTE)Driver, sizeof(Driver), NULL))
    {
        ok(FALSE, "the device has no driver key after the open: %lu\n", GetLastError());
        return;
    }

    ok(_wcsnicmp(Driver, SYSTEM_CLASS_KEY, wcslen(SYSTEM_CLASS_KEY)) == 0, "driver key %ls\n", Driver);
}

START_TEST(IoDriverKey)
{
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    HDEVINFO Devices;
    BOOL Success;

    Devices = SetupDiCreateDeviceInfoList(&SystemClass, NULL);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE) return;

    Success = SetupDiCreateDeviceInfoW(Devices, L"KmtestDriverKey", &SystemClass, L"Kmtest driver key", NULL,
                                       DICD_GENERATE_ID, &Device);
    if (Success)
    {
        Success = SetupDiSetDeviceRegistryPropertyW(Devices, &Device, SPDRP_HARDWAREID, (const BYTE *)HardwareIds,
                                                    sizeof(HardwareIds));
    }
    if (Success) Success = SetupDiCallClassInstaller(DIF_REGISTERDEVICE, Devices, &Device);
    ok(Success, "Registering the test device failed: %lu\n", GetLastError());

    if (Success)
    {
        OpenDriverKey(Devices, &Device);

        Success = SetupDiCallClassInstaller(DIF_REMOVE, Devices, &Device);
        ok(Success, "Removing the test device failed: %lu\n", GetLastError());
    }

    SetupDiDestroyDeviceInfoList(Devices);
}
