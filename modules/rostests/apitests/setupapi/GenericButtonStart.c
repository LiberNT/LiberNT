/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that every ACPI generic button device starts, whatever controls it describes
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

START_TEST(GenericButtonStart)
{
    WCHAR Ids[1024], InstanceId[MAX_DEVICE_ID_LEN];
    ULONG Status, Problem;
    SP_DEVINFO_DATA Data;
    HDEVINFO Set;
    DWORD Index, Devices = 0;

    Set = SetupDiGetClassDevsW(NULL, L"ACPI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    Data.cbSize = sizeof(Data);
    for (Index = 0; SetupDiEnumDeviceInfo(Set, Index, &Data); Index++)
    {
        ZeroMemory(Ids, sizeof(Ids));
        if (!SetupDiGetDeviceRegistryPropertyW(Set, &Data, SPDRP_HARDWAREID, NULL, (BYTE *)Ids, sizeof(Ids) - 2 * sizeof(WCHAR), NULL) ||
            !HasId(Ids, L"ACPI\\ACPI0011"))
        {
            continue;
        }

        Devices++;
        SetupDiGetDeviceInstanceIdW(Set, &Data, InstanceId, ARRAYSIZE(InstanceId), NULL);
        Status = Problem = 0;
        ok(CM_Get_DevNode_Status(&Status, &Problem, Data.DevInst, 0) == CR_SUCCESS, "%ls has no status\n", InstanceId);
        ok((Status & DN_STARTED) && !(Status & DN_HAS_PROBLEM), "%ls status 0x%lx problem %lu\n", InstanceId, Status, Problem);
    }

    if (Devices == 0)
        skip("No ACPI generic button device\n");
    SetupDiDestroyDeviceInfoList(Set);
}
