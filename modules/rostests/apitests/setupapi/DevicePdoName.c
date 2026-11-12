/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SPDRP_PHYSICAL_DEVICE_OBJECT_NAME is the name of the device's physical device object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

START_TEST(DevicePdoName)
{
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    WCHAR Name[MAX_PATH];
    WCHAR CmName[MAX_PATH];
    HDEVINFO Devices;
    CONFIGRET Result;
    DWORD Index, Checked = 0;
    ULONG Size;

    Devices = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed: %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE) return;

    for (Index = 0; Checked < 8 && SetupDiEnumDeviceInfo(Devices, Index, &Device); Index++)
    {
        if (!SetupDiGetDeviceInstanceIdW(Devices, &Device, InstanceId, RTL_NUMBER_OF(InstanceId), NULL) ||
            _wcsnicmp(InstanceId, L"HTREE\\", 6) == 0)
        {
            continue;
        }

        if (!SetupDiGetDeviceRegistryPropertyW(Devices, &Device, SPDRP_PHYSICAL_DEVICE_OBJECT_NAME, NULL, (PBYTE)Name,
                                               sizeof(Name), NULL))
        {
            ok(FALSE, "%ls: no physical device object name, error %lu\n", InstanceId, GetLastError());
            continue;
        }

        ok(wcsncmp(Name, L"\\Device\\", 8) == 0, "%ls: physical device object name %ls\n", InstanceId, Name);

        Size = sizeof(CmName);
        Result = CM_Get_DevNode_Registry_PropertyW(Device.DevInst, CM_DRP_PHYSICAL_DEVICE_OBJECT_NAME, NULL, CmName,
                                                   &Size, 0);
        ok(Result == CR_SUCCESS && wcscmp(Name, CmName) == 0, "%ls: CM_Get_DevNode_Registry_PropertyW returned %lu [%ls]\n",
           InstanceId, Result, Result == CR_SUCCESS ? CmName : L"");
        Checked++;
    }

    ok(Checked != 0, "no present device was checked\n");
    SetupDiDestroyDeviceInfoList(Devices);
}
