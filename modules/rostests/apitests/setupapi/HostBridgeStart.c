/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a PCI host bridge on a null driver runs raw, as Windows 11 starts it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

static BOOL
HasId(const WCHAR *List, const WCHAR *Id)
{
    for (; *List; List += wcslen(List) + 1)
    {
        if (!_wcsicmp(List, Id))
            return TRUE;
    }
    return FALSE;
}

START_TEST(HostBridgeStart)
{
    WCHAR Ids[1024], Service[64], InstanceId[MAX_DEVICE_ID_LEN];
    ULONG Status, Problem, Capabilities, Length;
    SP_DEVINFO_DATA Data;
    HDEVINFO Set;
    DWORD Index, Bridges = 0;

    Set = SetupDiGetClassDevsW(NULL, L"PCI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    Data.cbSize = sizeof(Data);
    for (Index = 0; SetupDiEnumDeviceInfo(Set, Index, &Data); Index++)
    {
        ZeroMemory(Ids, sizeof(Ids));
        if (!SetupDiGetDeviceRegistryPropertyW(Set, &Data, SPDRP_COMPATIBLEIDS, NULL, (BYTE *)Ids, sizeof(Ids) - 2 * sizeof(WCHAR), NULL) ||
            !HasId(Ids, L"PCI\\CC_0600"))
        {
            continue;
        }

        Bridges++;
        SetupDiGetDeviceInstanceIdW(Set, &Data, InstanceId, ARRAYSIZE(InstanceId), NULL);

        Capabilities = 0;
        Length = sizeof(Capabilities);
        ok(CM_Get_DevNode_Registry_PropertyW(Data.DevInst, CM_DRP_CAPABILITIES, NULL, &Capabilities, &Length, 0) == CR_SUCCESS,
           "%ls has no capabilities\n", InstanceId);
        ok(Capabilities == (CM_DEVCAP_RAWDEVICEOK | CM_DEVCAP_SURPRISEREMOVALOK), "%ls capabilities 0x%lx\n", InstanceId, Capabilities);

        /* With no function driver the bridge runs raw */
        Service[0] = UNICODE_NULL;
        if (!SetupDiGetDeviceRegistryPropertyW(Set, &Data, SPDRP_SERVICE, NULL, (BYTE *)Service, sizeof(Service), NULL) &&
            SetupDiGetDeviceRegistryPropertyW(Set, &Data, SPDRP_DRIVER, NULL, (BYTE *)Ids, sizeof(Ids), NULL))
        {
            Status = Problem = 0;
            ok(CM_Get_DevNode_Status(&Status, &Problem, Data.DevInst, 0) == CR_SUCCESS, "%ls has no status\n", InstanceId);
            ok((Status & DN_STARTED) && !(Status & DN_HAS_PROBLEM), "%ls status 0x%lx problem %lu\n", InstanceId, Status, Problem);
        }
    }

    ok(Bridges != 0, "No PCI host bridge found\n");
    SetupDiDestroyDeviceInfoList(Set);
}
