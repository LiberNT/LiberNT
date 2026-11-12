/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a device keeps the description of its driver and reports the bus description apart
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <initguid.h>
#include <devpkey.h>
#include <devguid.h>
#include <setupapi.h>
#include <cfgmgr32.h>

static BOOL
GetStringProperty(HDEVINFO Devices, SP_DEVINFO_DATA *Device, const DEVPROPKEY *Key, PWSTR Text, DWORD Count)
{
    DEVPROPTYPE Type = DEVPROP_TYPE_EMPTY;

    ZeroMemory(Text, Count * sizeof(WCHAR));
    return SetupDiGetDevicePropertyW(Devices, Device, Key, &Type, (PBYTE)Text, (Count - 1) * sizeof(WCHAR), NULL, 0) &&
           Type == DEVPROP_TYPE_STRING;
}

static BOOL
ReadDriverDescription(HDEVINFO Devices, SP_DEVINFO_DATA *Device, PWSTR Text, DWORD Count)
{
    DWORD Type = 0;
    DWORD Size = (Count - 1) * sizeof(WCHAR);
    LONG Error;
    HKEY Key;

    Key = SetupDiOpenDevRegKey(Devices, Device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
    if (Key == INVALID_HANDLE_VALUE)
        return FALSE;
    ZeroMemory(Text, Count * sizeof(WCHAR));
    Error = RegQueryValueExW(Key, L"DriverDesc", NULL, &Type, (PBYTE)Text, &Size);
    RegCloseKey(Key);
    return Error == ERROR_SUCCESS && Type == REG_SZ && Text[0] != UNICODE_NULL;
}

START_TEST(DeviceDescription)
{
    SP_DEVINFO_DATA Device;
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    WCHAR DriverDesc[512];
    WCHAR DeviceDesc[512];
    WCHAR BusDesc[512];
    WCHAR FriendlyName[512];
    DEVPROPTYPE Type;
    HDEVINFO Devices;
    DWORD Index;
    ULONG Compared = 0;
    ULONG PciDevices = 0;
    ULONG NamedDevices = 0;
    BOOL Result;

    Devices = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed: %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE)
        return;

    Device.cbSize = sizeof(Device);
    for (Index = 0; SetupDiEnumDeviceInfo(Devices, Index, &Device); Index++)
    {
        if (CM_Get_Device_IDW(Device.DevInst, InstanceId, ARRAYSIZE(InstanceId), 0) != CR_SUCCESS)
            continue;

        if (!_wcsnicmp(InstanceId, L"PCI\\", 4))
        {
            PciDevices++;
            ZeroMemory(BusDesc, sizeof(BusDesc));
            Type = DEVPROP_TYPE_EMPTY;
            Result = SetupDiGetDevicePropertyW(Devices, &Device, &DEVPKEY_Device_BusReportedDeviceDesc, &Type,
                                               (PBYTE)BusDesc, sizeof(BusDesc) - sizeof(WCHAR), NULL, 0);
            ok(Result, "%ls: no bus reported description: %lu\n", InstanceId, GetLastError());
            ok(!Result || (Type == DEVPROP_TYPE_STRING && BusDesc[0] != UNICODE_NULL),
               "%ls: bus reported description type %lu\n", InstanceId, Type);
        }

        if (IsEqualGUID(&Device.ClassGuid, &GUID_DEVCLASS_DISKDRIVE) ||
            IsEqualGUID(&Device.ClassGuid, &GUID_DEVCLASS_MONITOR))
        {
            NamedDevices++;
            ok(GetStringProperty(Devices, &Device, &DEVPKEY_Device_BusReportedDeviceDesc, BusDesc, ARRAYSIZE(BusDesc)),
               "%ls: no bus reported description: %lu\n", InstanceId, GetLastError());
            ok(GetStringProperty(Devices, &Device, &DEVPKEY_Device_FriendlyName, FriendlyName, ARRAYSIZE(FriendlyName)),
               "%ls: no friendly name: %lu\n", InstanceId, GetLastError());
            ok(!wcscmp(FriendlyName, BusDesc), "%ls: friendly name '%ls', bus reported '%ls'\n",
               InstanceId, FriendlyName, BusDesc);
        }

        if (!_wcsnicmp(InstanceId, L"SWD\\", 4) ||
            !ReadDriverDescription(Devices, &Device, DriverDesc, ARRAYSIZE(DriverDesc)))
        {
            continue;
        }

        ZeroMemory(DeviceDesc, sizeof(DeviceDesc));
        Result = SetupDiGetDeviceRegistryPropertyW(Devices, &Device, SPDRP_DEVICEDESC, NULL, (PBYTE)DeviceDesc,
                                                   sizeof(DeviceDesc) - sizeof(WCHAR), NULL);
        ok(Result, "%ls: no device description: %lu\n", InstanceId, GetLastError());
        ok(!Result || !wcscmp(DeviceDesc, DriverDesc), "%ls: device description '%ls', driver '%ls'\n",
           InstanceId, DeviceDesc, DriverDesc);
        Compared++;
    }

    ok(Compared != 0, "No device with a driver\n");
    ok(NamedDevices != 0, "No disk drive or monitor\n");
    trace("%lu devices with a driver, %lu PCI devices, %lu disk drives and monitors\n", Compared, PciDevices,
          NamedDevices);
    SetupDiDestroyDeviceInfoList(Devices);
}
