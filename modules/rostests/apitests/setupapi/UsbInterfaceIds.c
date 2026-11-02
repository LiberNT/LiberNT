/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the MI_xx device ID of USB composite device functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <apitest.h>

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <winreg.h>
#include <cfgmgr32.h>
#include <setupapi.h>

static BOOL
GetDeviceIdInterface(PCWSTR InstanceId, PWSTR Token, SIZE_T TokenCount)
{
    PCWSTR DeviceIdEnd, Mi;

    DeviceIdEnd = wcschr(InstanceId + 4, L'\\');
    Mi = wcsstr(InstanceId, L"&MI_");
    if (!DeviceIdEnd || !Mi || Mi > DeviceIdEnd || DeviceIdEnd - Mi != 6 || TokenCount < 7)
        return FALSE;
    lstrcpynW(Token, Mi, 7);
    return TRUE;
}

static BOOL
HardwareIdsEndWith(PCWSTR HardwareIds, PCWSTR Token)
{
    SIZE_T TokenLength = wcslen(Token), Length;
    PCWSTR Id;

    for (Id = HardwareIds; *Id; Id += Length + 1)
    {
        Length = wcslen(Id);
        if (Length >= TokenLength && !_wcsicmp(Id + Length - TokenLength, Token))
            return TRUE;
    }
    return FALSE;
}

START_TEST(UsbInterfaceIds)
{
    WCHAR InstanceId[MAX_DEVICE_ID_LEN], Token[8], HardwareIds[2048];
    SP_DEVINFO_DATA DeviceInfo;
    HDEVINFO DeviceSet;
    DWORD Index, Type, Size;
    ULONG Checked = 0;

    DeviceSet = SetupDiGetClassDevsW(NULL, L"USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(DeviceSet != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed: %lu\n", GetLastError());
    if (DeviceSet == INVALID_HANDLE_VALUE)
        return;

    DeviceInfo.cbSize = sizeof(DeviceInfo);
    for (Index = 0; SetupDiEnumDeviceInfo(DeviceSet, Index, &DeviceInfo); Index++)
    {
        if (!SetupDiGetDeviceInstanceIdW(DeviceSet, &DeviceInfo, InstanceId, ARRAYSIZE(InstanceId), NULL) ||
            _wcsnicmp(InstanceId, L"USB\\", 4) ||
            !GetDeviceIdInterface(InstanceId, Token, ARRAYSIZE(Token)))
        {
            continue;
        }
        ZeroMemory(HardwareIds, sizeof(HardwareIds));
        if (!SetupDiGetDeviceRegistryPropertyW(DeviceSet, &DeviceInfo, SPDRP_HARDWAREID, &Type,
                                               (PBYTE)HardwareIds, sizeof(HardwareIds) - 2 * sizeof(WCHAR), &Size))
        {
            ok(FALSE, "%ls has no hardware IDs: %lu\n", InstanceId, GetLastError());
            continue;
        }
        trace("%ls\n", InstanceId);
        ok(Type == REG_MULTI_SZ, "%ls hardware ID type %lu\n", InstanceId, Type);
        ok(HardwareIdsEndWith(HardwareIds, Token), "%ls: no hardware ID ends with %ls (first %ls)\n",
           InstanceId, Token, HardwareIds);
        Checked++;
    }
    SetupDiDestroyDeviceInfoList(DeviceSet);

    if (!Checked)
        skip("No USB composite device function is present\n");
}
