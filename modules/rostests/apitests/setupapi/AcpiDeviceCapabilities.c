/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the capabilities ACPI gives its devices and which ACPI devices get a device node
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

static const WCHAR *RawIds[] =
{
    L"ACPI\\PNP0A05", L"ACPI\\PNP0A06", L"ACPI\\PNP0B00", L"ACPI\\ACPI0010", L"ACPI\\PNP0C09",
    L"ACPI\\PNP0C0B", L"ACPI\\PNP0C0C", L"ACPI\\PNP0C0D", L"ACPI\\PNP0C0E", L"ACPI\\ThermalZone",
};

static const WCHAR *NotRawIds[] = { L"ACPI\\PNP0C01", L"ACPI\\PNP0103" };

static BOOL
HasId(const WCHAR *List, const WCHAR *const *Ids, ULONG Count)
{
    ULONG Index;

    for (; *List; List += wcslen(List) + 1)
    {
        for (Index = 0; Index < Count; Index++)
        {
            if (!_wcsicmp(List, Ids[Index]))
                return TRUE;
        }
    }
    return FALSE;
}

START_TEST(AcpiDeviceCapabilities)
{
    static const WCHAR *LinkId[] = { L"ACPI\\PNP0C0F" };
    WCHAR Id[MAX_DEVICE_ID_LEN], HardwareIds[2048];
    SP_DEVINFO_DATA Data;
    ULONG Caps, Size, Type, Status, Problem, Raw = 0, Acpi = 0;
    HDEVINFO Set;
    DWORD Index;

    Set = SetupDiGetClassDevsW(NULL, L"ACPI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    Data.cbSize = sizeof(Data);
    for (Index = 0; SetupDiEnumDeviceInfo(Set, Index, &Data); Index++)
    {
        if (!SetupDiGetDeviceInstanceIdW(Set, &Data, Id, ARRAYSIZE(Id), NULL))
            continue;
        ZeroMemory(HardwareIds, sizeof(HardwareIds));
        Size = sizeof(HardwareIds) - 2 * sizeof(WCHAR);
        if (CM_Get_DevNode_Registry_PropertyW(Data.DevInst, CM_DRP_HARDWAREID, &Type, HardwareIds, &Size, 0) != CR_SUCCESS)
            continue;
        Caps = 0;
        Size = sizeof(Caps);
        if (CM_Get_DevNode_Registry_PropertyW(Data.DevInst, CM_DRP_CAPABILITIES, &Type, &Caps, &Size, 0) != CR_SUCCESS)
            continue;
        Status = Problem = 0;
        CM_Get_DevNode_Status(&Status, &Problem, Data.DevInst, 0);
        Acpi++;

        ok(!HasId(HardwareIds, LinkId, 1), "%ls: a PCI interrupt link has a device node\n", Id);
        ok(Caps & CM_DEVCAP_SILENTINSTALL, "%ls: capabilities 0x%lx lack SilentInstall\n", Id, Caps);
        if (HasId(HardwareIds, RawIds, ARRAYSIZE(RawIds)))
        {
            Raw++;
            ok(Caps & CM_DEVCAP_RAWDEVICEOK, "%ls: capabilities 0x%lx lack RawDeviceOK\n", Id, Caps);
            ok(Status & DN_STARTED, "%ls: not started (status 0x%lx problem %lu)\n", Id, Status, Problem);
        }
        if (HasId(HardwareIds, NotRawIds, ARRAYSIZE(NotRawIds)))
            ok(!(Caps & CM_DEVCAP_RAWDEVICEOK), "%ls: capabilities 0x%lx have RawDeviceOK\n", Id, Caps);
    }

    ok(Acpi > 0, "no ACPI device found\n");
    trace("%lu ACPI devices, %lu started raw\n", Acpi, Raw);
    SetupDiDestroyDeviceInfoList(Set);
}
