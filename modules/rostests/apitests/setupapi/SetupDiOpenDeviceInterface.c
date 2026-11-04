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
#include <setupapi.h>
#include <wchar.h>

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
    TestNullInterfaceClass();
}
