/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Checks the scatter/gather list size NdisMRegisterScatterGatherDma reports to a miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <setupapi.h>
#include <newdev.h>
#include "NdisSgDma.h"

static BOOL NdisSgDevicePresent(VOID)
{
    static const DWORD Properties[] = { SPDRP_HARDWAREID, SPDRP_COMPATIBLEIDS };
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    WCHAR Ids[512];
    HDEVINFO Devices;
    BOOL Found = FALSE;
    DWORD Index, Property;
    PWSTR Id;

    Devices = SetupDiGetClassDevsW(NULL, L"PCI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (Devices == INVALID_HANDLE_VALUE)
        return FALSE;
    for (Index = 0; !Found && SetupDiEnumDeviceInfo(Devices, Index, &Device); ++Index)
    {
        for (Property = 0; !Found && Property < RTL_NUMBER_OF(Properties); ++Property)
        {
            ZeroMemory(Ids, sizeof(Ids));
            if (!SetupDiGetDeviceRegistryPropertyW(Devices, &Device, Properties[Property], NULL, (PBYTE)Ids,
                                                   sizeof(Ids) - sizeof(WCHAR), NULL))
                continue;
            for (Id = Ids; *Id && !Found; Id += wcslen(Id) + 1)
                Found = !_wcsicmp(Id, NDISSG_HARDWARE_ID);
        }
    }
    SetupDiDestroyDeviceInfoList(Devices);
    return Found;
}

static BOOL NdisSgRead(HKEY Key, PCWSTR Name, PULONG Value)
{
    DWORD Size = sizeof(*Value), Type = 0;

    return RegQueryValueExW(Key, Name, NULL, &Type, (PBYTE)Value, &Size) == ERROR_SUCCESS && Type == REG_DWORD;
}

START_TEST(NdisSgDma)
{
    WCHAR InfPath[MAX_PATH];
    ULONG Done = 0, Value, Size, Required;
    BOOL Reboot = FALSE;
    PWSTR Filename;
    HKEY Key = NULL;
    DWORD Length, Start;
    ULONG i;

    if (skip(NdisSgDevicePresent(), "No %ls device to bind the test miniport to\n", NDISSG_HARDWARE_ID))
        return;
    Length = GetModuleFileNameW(NULL, InfPath, RTL_NUMBER_OF(InfPath));
    Filename = Length && Length < RTL_NUMBER_OF(InfPath) ? wcsrchr(InfPath, L'\\') : NULL;
    if (!Filename || Filename - InfPath + RTL_NUMBER_OF(L"\\kmtest_ndissg.inf") > RTL_NUMBER_OF(InfPath))
    {
        ok(FALSE, "Cannot build the INF path\n");
        return;
    }
    wcscpy(Filename + 1, L"kmtest_ndissg.inf");
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, NDISSG_PARAMETERS_KEY);

    ok(UpdateDriverForPlugAndPlayDevicesW(NULL, NDISSG_HARDWARE_ID, InfPath, INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot),
       "Installing the test miniport failed: %lu\n", GetLastError());

    Start = GetTickCount();
    do
    {
        if (!Key)
            RegOpenKeyExW(HKEY_LOCAL_MACHINE, NDISSG_PARAMETERS_KEY, 0, KEY_QUERY_VALUE, &Key);
        if (Key && NdisSgRead(Key, L"Done", &Done) && Done)
            break;
        Sleep(100);
    } while (GetTickCount() - Start < 10000);
    ok(Done == 1, "The test miniport did not report its results\n");
    if (!Key || Done != 1)
    {
        if (Key)
            RegCloseKey(Key);
        return;
    }

    ok(NdisSgRead(Key, L"Registration", &Value) && Value == STATUS_SUCCESS, "Registration attributes: %#lx\n", Value);
    for (i = 0; i < NDISSG_MAPPING_COUNT; ++i)
    {
        Value = Size = Required = 0;
        ok(NdisSgRead(Key, NdisSgStatusNames[i], &Value) && Value == STATUS_SUCCESS,
           "Mapping %lu: NdisMRegisterScatterGatherDma returned %#lx\n", NdisSgMappings[i], Value);
        ok(NdisSgRead(Key, NdisSgSizeNames[i], &Size), "Mapping %lu: no size recorded\n", NdisSgMappings[i]);
        ok(NdisSgRead(Key, NdisSgRequiredNames[i], &Required), "Mapping %lu: no required size recorded\n", NdisSgMappings[i]);
        ok(Size != NDISSG_UNSET, "Mapping %lu: ScatterGatherListSize was not set\n", NdisSgMappings[i]);
        ok(Size >= Required, "Mapping %lu: ScatterGatherListSize %lu, a list needs %lu\n", NdisSgMappings[i], Size, Required);
        trace("Mapping %lu: ScatterGatherListSize %lu, list needs %lu\n", NdisSgMappings[i], Size, Required);
    }
    RegCloseKey(Key);
}
