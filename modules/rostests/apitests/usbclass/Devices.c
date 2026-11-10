/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that every USB device present has a started driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

START_TEST(Devices)
{
    SP_DEVINFO_DATA Info;
    HDEVINFO Set;
    WCHAR HardwareId[512], Service[128];
    ULONG Status, Problem, Count = 0, Started = 0;
    DWORD Index;

    Set = SetupDiGetClassDevsW(NULL, L"USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevs failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;
    Info.cbSize = sizeof(Info);
    for (Index = 0; SetupDiEnumDeviceInfo(Set, Index, &Info); ++Index)
    {
        HardwareId[0] = Service[0] = 0;
        SetupDiGetDeviceRegistryPropertyW(Set, &Info, SPDRP_HARDWAREID, NULL, (PBYTE)HardwareId,
                                          sizeof(HardwareId) - sizeof(WCHAR), NULL);
        SetupDiGetDeviceRegistryPropertyW(Set, &Info, SPDRP_SERVICE, NULL, (PBYTE)Service,
                                          sizeof(Service) - sizeof(WCHAR), NULL);
        Status = Problem = 0;
        ok_int(CM_Get_DevNode_Status(&Status, &Problem, Info.DevInst, 0), CR_SUCCESS);
        trace("%S service %S status %08lx problem %lu\n", HardwareId, Service[0] ? Service : L"(none)", Status,
              Problem);
        ++Count;
        if ((Status & DN_STARTED) && !(Status & DN_HAS_PROBLEM))
            ++Started;
        else
            ok(0, "%S (service %S) is not started: status %08lx problem %lu\n", HardwareId,
               Service[0] ? Service : L"(none)", Status, Problem);
    }
    SetupDiDestroyDeviceInfoList(Set);
    trace("%lu USB devices, %lu started\n", Count, Started);
    ok(Count > 0, "No USB device is present\n");
}
