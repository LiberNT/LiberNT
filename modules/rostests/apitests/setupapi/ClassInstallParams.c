/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Class install parameters of DIF_REMOVE
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>

static const GUID SystemClass = { 0x4d36e97d, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };

START_TEST(ClassInstallParams)
{
    SP_REMOVEDEVICE_PARAMS Params, Read;
    SP_DEVINFO_DATA Device;
    HDEVINFO Set;
    DWORD Required = 0;
    BOOL Ret;

    Set = SetupDiCreateDeviceInfoList(NULL, NULL);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;
    Device.cbSize = sizeof(Device);
    Ret = SetupDiCreateDeviceInfoW(Set, L"ROOT\\LNTCLASSPARAMS\\0000", &SystemClass, NULL, NULL, 0, &Device);
    ok(Ret, "SetupDiCreateDeviceInfoW: %lu\n", GetLastError());
    if (!Ret)
    {
        SetupDiDestroyDeviceInfoList(Set);
        return;
    }

    memset(&Params, 0, sizeof(Params));
    Params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    Params.ClassInstallHeader.InstallFunction = DIF_REMOVE;
    Params.Scope = DI_REMOVEDEVICE_GLOBAL;
    Params.HwProfile = 0;
    SetLastError(0xdeadbeef);
    Ret = SetupDiSetClassInstallParamsW(Set, &Device, &Params.ClassInstallHeader, sizeof(Params));
    ok(Ret, "SetupDiSetClassInstallParamsW(DIF_REMOVE): %lu\n", GetLastError());

    memset(&Read, 0xcc, sizeof(Read));
    Read.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    SetLastError(0xdeadbeef);
    Ret = SetupDiGetClassInstallParamsW(Set, &Device, &Read.ClassInstallHeader, sizeof(SP_CLASSINSTALL_HEADER), &Required);
    ok(!Ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "Short buffer: ret %d error %lu\n", Ret, GetLastError());
    ok(Required == sizeof(SP_REMOVEDEVICE_PARAMS), "RequiredSize %lu\n", Required);

    Ret = SetupDiGetClassInstallParamsW(Set, &Device, &Read.ClassInstallHeader, sizeof(Read), &Required);
    ok(Ret, "SetupDiGetClassInstallParamsW: %lu\n", GetLastError());
    ok(Read.ClassInstallHeader.InstallFunction == DIF_REMOVE, "InstallFunction %u\n", Read.ClassInstallHeader.InstallFunction);
    ok(Read.Scope == DI_REMOVEDEVICE_GLOBAL && Read.HwProfile == 0, "Scope %lu HwProfile %lu\n", Read.Scope, Read.HwProfile);

    Params.Scope = DI_REMOVEDEVICE_CONFIGSPECIFIC;
    Params.HwProfile = 1;
    Ret = SetupDiSetClassInstallParamsW(Set, &Device, &Params.ClassInstallHeader, sizeof(Params));
    ok(Ret, "SetupDiSetClassInstallParamsW(CONFIGSPECIFIC): %lu\n", GetLastError());
    Ret = SetupDiGetClassInstallParamsW(Set, &Device, &Read.ClassInstallHeader, sizeof(Read), NULL);
    ok(Ret && Read.Scope == DI_REMOVEDEVICE_CONFIGSPECIFIC && Read.HwProfile == 1,
       "Second read: ret %d Scope %lu HwProfile %lu\n", Ret, Read.Scope, Read.HwProfile);

    SetupDiDestroyDeviceInfoList(Set);
}
