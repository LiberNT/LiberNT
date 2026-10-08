/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device information set routines called with an invalid set handle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winuser.h>
#include <winreg.h>
#include <prsht.h>
#include <setupapi.h>

#define CHECK_INVALID_HANDLE(Call) \
    do \
    { \
        BOOL Result_; \
        DWORD Error_; \
        SetLastError(0xdeadbeef); \
        Result_ = (Call); \
        Error_ = GetLastError(); \
        ok(!Result_, "%s succeeded\n", #Call); \
        ok(Error_ == ERROR_INVALID_HANDLE, "%s set error %lu\n", #Call, Error_); \
    } while (0)

START_TEST(InvalidDeviceInfoSet)
{
    static const WCHAR DeviceId[] = L"Root\\LEGACY_BOGUS\\0000";
    static const WCHAR InterfacePath[] = L"\\\\?\\Root#LEGACY_BOGUS#0000#{6a55b5a4-3f65-11db-b704-0011955c2bdb}";
    SP_DEVICE_INTERFACE_DATA InterfaceData = { sizeof(InterfaceData) };
    SP_DEVINSTALL_PARAMS_W InstallParams = { sizeof(InstallParams) };
    SP_DEVINFO_DATA DeviceInfoData = { sizeof(DeviceInfoData) };
    PROPSHEETHEADERW Header = { sizeof(Header) };
    DWORD Required = 0;
    GUID ClassGuid;
    HDEVINFO Set;

    CHECK_INVALID_HANDLE(SetupDiGetDeviceInfoListClass(INVALID_HANDLE_VALUE, &ClassGuid));
    CHECK_INVALID_HANDLE(SetupDiGetDeviceInstallParamsW(INVALID_HANDLE_VALUE, NULL, &InstallParams));
    CHECK_INVALID_HANDLE(SetupDiSetDeviceInstallParamsW(INVALID_HANDLE_VALUE, NULL, &InstallParams));
    CHECK_INVALID_HANDLE(SetupDiDeleteDeviceInfo(INVALID_HANDLE_VALUE, &DeviceInfoData));
    CHECK_INVALID_HANDLE(SetupDiOpenDeviceInfoW(INVALID_HANDLE_VALUE, DeviceId, NULL, 0, &DeviceInfoData));
    CHECK_INVALID_HANDLE(SetupDiGetSelectedDevice(INVALID_HANDLE_VALUE, &DeviceInfoData));
    CHECK_INVALID_HANDLE(SetupDiSetSelectedDevice(INVALID_HANDLE_VALUE, &DeviceInfoData));
    CHECK_INVALID_HANDLE(SetupDiOpenDeviceInterfaceW(INVALID_HANDLE_VALUE, InterfacePath, 0, &InterfaceData));
    CHECK_INVALID_HANDLE(SetupDiOpenDeviceInterfaceW(NULL, InterfacePath, 0, &InterfaceData));
    CHECK_INVALID_HANDLE(SetupDiGetClassDevPropertySheetsW(INVALID_HANDLE_VALUE, NULL, &Header, 0, &Required,
                                                           DIGCDP_FLAG_ADVANCED));
    CHECK_INVALID_HANDLE(SetupDiBuildDriverInfoList(INVALID_HANDLE_VALUE, NULL, SPDIT_CLASSDRIVER));
    CHECK_INVALID_HANDLE(SetupDiDestroyDriverInfoList(INVALID_HANDLE_VALUE, NULL, SPDIT_CLASSDRIVER));

    Set = SetupDiGetClassDevsExW(NULL, NULL, NULL, DIGCF_ALLCLASSES, INVALID_HANDLE_VALUE, NULL, NULL);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsExW failed with error %lu\n", GetLastError());
    if (Set != INVALID_HANDLE_VALUE)
        SetupDiDestroyDeviceInfoList(Set);
}
