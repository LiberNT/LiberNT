/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for opening and enumerating device interfaces
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <winreg.h>
#include <objbase.h>
#include <setupapi.h>
#include <wchar.h>

typedef struct _OPEN_COUNTS
{
    ULONG Count;
    ULONG Failures;
    ULONG UpperFailures;
    ULONG DotFailures;
} OPEN_COUNTS;

static VOID
TestOpenPath(PCWSTR Path, const SP_DEVICE_INTERFACE_DATA *Expected, PCWSTR What, PULONG Failures)
{
    SP_DEVICE_INTERFACE_DATA Data = { sizeof(Data) };
    HDEVINFO Set;
    BOOL Ret;

    Set = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (Set == INVALID_HANDLE_VALUE)
    {
        ++*Failures;
        return;
    }
    Ret = SetupDiOpenDeviceInterfaceW(Set, Path, 0, &Data);
    if (!Ret)
    {
        if (++*Failures <= 5)
            ok(FALSE, "%ls %ls: SetupDiOpenDeviceInterfaceW failed, error %lu\n", What, Path, GetLastError());
    }
    else if (!IsEqualGUID(&Data.InterfaceClassGuid, &Expected->InterfaceClassGuid) || Data.Flags != Expected->Flags)
    {
        if (++*Failures <= 5)
            ok(FALSE, "%ls %ls: flags 0x%lx, expected 0x%lx\n", What, Path, Data.Flags, Expected->Flags);
    }
    SetupDiDestroyDeviceInfoList(Set);
}

static VOID
TestDotPath(PCWSTR Path, PULONG Failures)
{
    SP_DEVICE_INTERFACE_DATA Data = { sizeof(Data) };
    HDEVINFO Set;
    BOOL Ret;

    Set = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (Set == INVALID_HANDLE_VALUE)
    {
        ++*Failures;
        return;
    }
    SetLastError(0xdeadbeef);
    Ret = SetupDiOpenDeviceInterfaceW(Set, Path, 0, &Data);
    if (Ret || GetLastError() != ERROR_BAD_PATHNAME)
    {
        if (++*Failures <= 5)
            ok(FALSE, "%ls: ret %d, error %lu\n", Path, Ret, GetLastError());
    }
    SetupDiDestroyDeviceInfoList(Set);
}

static VOID
TestInterfaceClass(const GUID *ClassGuid, PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail, DWORD DetailSize,
                   OPEN_COUNTS *Counts)
{
    SP_DEVICE_INTERFACE_DATA Data = { sizeof(Data) };
    WCHAR Variant[1024];
    HDEVINFO Set;
    DWORD Index;

    Set = SetupDiGetClassDevsW(ClassGuid, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (Set == INVALID_HANDLE_VALUE)
        return;
    for (Index = 0; SetupDiEnumDeviceInterfaces(Set, NULL, ClassGuid, Index, &Data); Index++)
    {
        Detail->cbSize = sizeof(*Detail);
        if (!SetupDiGetDeviceInterfaceDetailW(Set, &Data, Detail, DetailSize, NULL, NULL) ||
            wcslen(Detail->DevicePath) >= _countof(Variant))
        {
            continue;
        }
        Counts->Count++;
        TestOpenPath(Detail->DevicePath, &Data, L"path", &Counts->Failures);

        wcscpy(Variant, Detail->DevicePath);
        _wcsupr(Variant);
        TestOpenPath(Variant, &Data, L"upper-case path", &Counts->UpperFailures);

        wcscpy(Variant, Detail->DevicePath);
        Variant[2] = L'.';
        TestDotPath(Variant, &Counts->DotFailures);
    }
    SetupDiDestroyDeviceInfoList(Set);
}

static VOID
TestNullInterfaceClass(VOID)
{
    SP_DEVICE_INTERFACE_DATA Data = { sizeof(Data) };
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    HDEVINFO Set;
    BOOL Ret;

    Set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed, error %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    SetLastError(0xdeadbeef);
    Ret = SetupDiEnumDeviceInterfaces(Set, NULL, NULL, 0, &Data);
    ok(!Ret, "SetupDiEnumDeviceInterfaces succeeded without an interface class\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "error %lu\n", GetLastError());

    if (SetupDiEnumDeviceInfo(Set, 0, &Device))
    {
        SetLastError(0xdeadbeef);
        Ret = SetupDiEnumDeviceInterfaces(Set, &Device, NULL, 0, &Data);
        ok(!Ret, "SetupDiEnumDeviceInterfaces succeeded on a device without an interface class\n");
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "device: error %lu\n", GetLastError());
    }
    SetupDiDestroyDeviceInfoList(Set);
}

START_TEST(SetupDiOpenDeviceInterface)
{
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
    OPEN_COUNTS Counts = { 0 };
    WCHAR Name[64];
    DWORD Index, NameLength, DetailSize;
    HKEY Classes;
    GUID ClassGuid;

    TestNullInterfaceClass();

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\DeviceClasses", 0, KEY_READ,
                      &Classes) != ERROR_SUCCESS)
    {
        ok(FALSE, "cannot open the DeviceClasses key\n");
        return;
    }
    DetailSize = FIELD_OFFSET(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath) + 1024 * sizeof(WCHAR);
    Detail = HeapAlloc(GetProcessHeap(), 0, DetailSize);
    ok(Detail != NULL, "out of memory\n");
    if (!Detail)
    {
        RegCloseKey(Classes);
        return;
    }

    for (Index = 0; ; Index++)
    {
        NameLength = _countof(Name);
        if (RegEnumKeyExW(Classes, Index, Name, &NameLength, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (CLSIDFromString(Name, &ClassGuid) != NOERROR)
            continue;
        TestInterfaceClass(&ClassGuid, Detail, DetailSize, &Counts);
    }

    ok(Counts.Count > 0, "no present device interfaces\n");
    ok(Counts.Failures == 0, "%lu of %lu interfaces failed to open\n", Counts.Failures, Counts.Count);
    ok(Counts.UpperFailures == 0, "%lu of %lu upper-case paths failed to open\n", Counts.UpperFailures, Counts.Count);
    ok(Counts.DotFailures == 0, "%lu of %lu \\\\.\\ paths were not rejected as bad path names\n", Counts.DotFailures, Counts.Count);

    HeapFree(GetProcessHeap(), 0, Detail);
    RegCloseKey(Classes);
}
