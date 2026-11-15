/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the processor identifier in the registry and the identifiers of the ACPI processor devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <intrin.h>
#include <stdio.h>

static const GUID ProcessorClass = { 0x50127dc3, 0x0f36, 0x415e, { 0xa6, 0xcc, 0x4c, 0xb3, 0xbe, 0x91, 0x0b, 0x65 } };

static BOOL
ReadCpuValue(const WCHAR *Name, WCHAR *Buffer, DWORD Chars)
{
    DWORD Size = (Chars - 1) * sizeof(WCHAR), Type;
    HKEY Key;
    LONG Error;

    ZeroMemory(Buffer, Chars * sizeof(WCHAR));
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &Key);
    if (Error != ERROR_SUCCESS)
        return FALSE;
    Error = RegQueryValueExW(Key, Name, NULL, &Type, (BYTE *)Buffer, &Size);
    RegCloseKey(Key);
    return Error == ERROR_SUCCESS && Type == REG_SZ;
}

static VOID
Underscores(WCHAR *String)
{
    for (; *String; String++)
    {
        if (*String == L' ')
            *String = L'_';
    }
}

static VOID
CheckIdentifier(const WCHAR *Vendor, const WCHAR *Identifier)
{
    SYSTEM_INFO Info;
    int Registers[4];
    ULONG Family, Model;
    WCHAR Expected[128];
    const WCHAR *Arch;

    GetNativeSystemInfo(&Info);
    if (Info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
    {
        if (!wcscmp(Vendor, L"GenuineIntel"))
            Arch = L"Intel64";
        else if (!wcscmp(Vendor, L"AuthenticAMD"))
            Arch = L"AMD64";
        else
            Arch = NULL;
    }
    else if (Info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
    {
        Arch = L"x86";
    }
    else
    {
        Arch = NULL;
    }
    if (Arch == NULL)
    {
        skip("No Windows 11 identifier for vendor %ls on architecture %u\n", Vendor, Info.wProcessorArchitecture);
        return;
    }

    __cpuid(Registers, 1);
    Family = (Registers[0] >> 8) & 0xF;
    Model = (Registers[0] >> 4) & 0xF;
    if (Family == 0xF)
        Family += (Registers[0] >> 20) & 0xFF;
    if (Family == 0x6 || Family >= 0xF)
        Model |= ((Registers[0] >> 16) & 0xF) << 4;

    _snwprintf(Expected, ARRAYSIZE(Expected) - 1, L"%ls Family %lu Model %lu Stepping %lu", Arch, Family, Model,
               (ULONG)(Registers[0] & 0xF));
    Expected[ARRAYSIZE(Expected) - 1] = UNICODE_NULL;
    ok(!wcscmp(Identifier, Expected), "Identifier '%ls', expected '%ls'\n", Identifier, Expected);
}

static BOOL
CheckList(const WCHAR *Id, const WCHAR *Kind, const WCHAR *Actual, const WCHAR *Expected)
{
    for (; *Expected; Expected += wcslen(Expected) + 1, Actual += wcslen(Actual) + 1)
    {
        ok(_wcsicmp(Actual, Expected) == 0, "%ls: %ls '%ls', expected '%ls'\n", Id, Kind, Actual, Expected);
        if (!*Actual)
            return FALSE;
    }
    ok(*Actual == UNICODE_NULL, "%ls: extra %ls '%ls'\n", Id, Kind, Actual);
    return TRUE;
}

static ULONG
Add(WCHAR *List, ULONG Used, ULONG Chars, const WCHAR *Prefix, const WCHAR *Vendor, const WCHAR *Rest)
{
    int Written = _snwprintf(List + Used, Chars - Used - 2, L"%ls%ls_-_%ls", Prefix, Vendor, Rest);

    if (Written < 0)
        return Used;
    List[Used + Written] = UNICODE_NULL;
    List[Used + Written + 1] = UNICODE_NULL;
    return Used + Written + 1;
}

START_TEST(ProcessorDeviceIds)
{
    WCHAR Vendor[64], Identifier[128], Name[128], Short[128], Family[128], Arch[64], DeviceId[512];
    WCHAR Id[MAX_DEVICE_ID_LEN], Hardware[1024], Compatible[512], ExpectedHw[1024], ExpectedCompat[32], Friendly[256];
    SP_DEVINFO_DATA Data;
    WCHAR *Cut;
    ULONG Size, Type, Used, Caps, Checked = 0;
    HDEVINFO Set;
    DWORD Index;

    if (!ReadCpuValue(L"VendorIdentifier", Vendor, ARRAYSIZE(Vendor)) ||
        !ReadCpuValue(L"Identifier", Identifier, ARRAYSIZE(Identifier)) ||
        !ReadCpuValue(L"ProcessorNameString", Name, ARRAYSIZE(Name)))
    {
        ok(FALSE, "CentralProcessor\\0 lacks VendorIdentifier, Identifier or ProcessorNameString\n");
        return;
    }
    CheckIdentifier(Vendor, Identifier);

    /* The identifiers use the registry identifier without its stepping, and only the device ID has the name */
    wcscpy(Short, Identifier);
    Cut = wcsstr(Short, L" Stepping");
    if (Cut != NULL)
        *Cut = UNICODE_NULL;
    wcscpy(Family, Short);
    Cut = wcsstr(Family, L" Model");
    if (Cut != NULL)
        *Cut = UNICODE_NULL;
    wcscpy(Arch, Short);
    Cut = wcschr(Arch, L' ');
    if (Cut != NULL)
        *Cut = UNICODE_NULL;
    Underscores(Short);
    Underscores(Family);

    _snwprintf(DeviceId, ARRAYSIZE(DeviceId) - 1, L"ACPI\\%ls_-_%ls_-_%ls", Vendor, Short, Name);
    DeviceId[ARRAYSIZE(DeviceId) - 1] = UNICODE_NULL;
    Underscores(DeviceId);

    Used = Add(ExpectedHw, 0, ARRAYSIZE(ExpectedHw), L"ACPI\\", Vendor, Short);
    Used = Add(ExpectedHw, Used, ARRAYSIZE(ExpectedHw), L"*", Vendor, Short);
    Used = Add(ExpectedHw, Used, ARRAYSIZE(ExpectedHw), L"ACPI\\", Vendor, Family);
    Used = Add(ExpectedHw, Used, ARRAYSIZE(ExpectedHw), L"*", Vendor, Family);
    Used = Add(ExpectedHw, Used, ARRAYSIZE(ExpectedHw), L"ACPI\\", Vendor, Arch);
    Add(ExpectedHw, Used, ARRAYSIZE(ExpectedHw), L"*", Vendor, Arch);
    ZeroMemory(ExpectedCompat, sizeof(ExpectedCompat));
    wcscpy(ExpectedCompat, L"ACPI\\Processor");

    Set = SetupDiGetClassDevsW(&ProcessorClass, NULL, NULL, DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    Data.cbSize = sizeof(Data);
    for (Index = 0; SetupDiEnumDeviceInfo(Set, Index, &Data); Index++)
    {
        if (!SetupDiGetDeviceInstanceIdW(Set, &Data, Id, ARRAYSIZE(Id), NULL) || _wcsnicmp(Id, L"ACPI\\", 5) != 0)
            continue;
        Checked++;

        Cut = wcsrchr(Id, L'\\');
        ok(Cut != NULL && (SIZE_T)(Cut - Id) == wcslen(DeviceId) && !_wcsnicmp(Id, DeviceId, Cut - Id),
           "%ls: device ID differs from '%ls'\n", Id, DeviceId);

        ZeroMemory(Hardware, sizeof(Hardware));
        Size = sizeof(Hardware) - 2 * sizeof(WCHAR);
        ok(CM_Get_DevNode_Registry_PropertyW(Data.DevInst, CM_DRP_HARDWAREID, &Type, Hardware, &Size, 0) == CR_SUCCESS,
           "%ls: no hardware IDs\n", Id);
        CheckList(Id, L"hardware ID", Hardware, ExpectedHw);

        ZeroMemory(Compatible, sizeof(Compatible));
        Size = sizeof(Compatible) - 2 * sizeof(WCHAR);
        ok(CM_Get_DevNode_Registry_PropertyW(Data.DevInst, CM_DRP_COMPATIBLEIDS, &Type, Compatible, &Size, 0) == CR_SUCCESS,
           "%ls: no compatible IDs\n", Id);
        CheckList(Id, L"compatible ID", Compatible, ExpectedCompat);

        Caps = 0;
        Size = sizeof(Caps);
        ok(CM_Get_DevNode_Registry_PropertyW(Data.DevInst, CM_DRP_CAPABILITIES, &Type, &Caps, &Size, 0) == CR_SUCCESS,
           "%ls: no capabilities\n", Id);
        ok(!(Caps & (CM_DEVCAP_EJECTSUPPORTED | CM_DEVCAP_REMOVABLE)), "%ls: capabilities 0x%lx offer removal\n", Id, Caps);

        ZeroMemory(Friendly, sizeof(Friendly));
        Size = sizeof(Friendly) - sizeof(WCHAR);
        ok(CM_Get_DevNode_Registry_PropertyW(Data.DevInst, CM_DRP_FRIENDLYNAME, &Type, Friendly, &Size, 0) == CR_SUCCESS &&
           !wcscmp(Friendly, Name), "%ls: friendly name '%ls', expected '%ls'\n", Id, Friendly, Name);
    }

    ok(Checked > 0, "no ACPI processor device found\n");
    trace("%lu processors checked\n", Checked);
    SetupDiDestroyDeviceInfoList(Set);
}
