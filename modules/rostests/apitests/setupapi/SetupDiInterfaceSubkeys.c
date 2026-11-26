/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for interface enumeration with non-device and incomplete DeviceClasses subkeys
 * COPYRIGHT:   Copyright 2026 Jean-Luc Robitaille <jlr@robitaille.us>
 */

#include <apitest.h>
#include <windows.h>
#include <objbase.h>
#include <setupapi.h>

START_TEST(SetupDiInterfaceSubkeys)
{
    static const WCHAR DeviceClassesPrefix[] = L"SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\";
    static const WCHAR DeviceInstanceValue[] = L"ROOT\\LEGACY_TEST\\0000";
    WCHAR GuidStr[64];
    WCHAR ClassPath[ARRAYSIZE(DeviceClassesPrefix) + ARRAYSIZE(GuidStr)];
    HKEY ClassKey = NULL;
    HKEY SubKey = NULL;
    HKEY RefKey = NULL;
    HDEVINFO Set;
    GUID TestGuid;
    HRESULT hr;
    LSTATUS Error;
    DWORD LastError;
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    BOOL EnumRet;

    hr = CoCreateGuid(&TestGuid);
    ok(hr == S_OK, "CoCreateGuid failed, hr 0x%08lx\n", (unsigned long)hr);
    if (FAILED(hr))
        return;

    if (StringFromGUID2(&TestGuid, GuidStr, ARRAYSIZE(GuidStr)) == 0)
    {
        ok(FALSE, "StringFromGUID2 failed\n");
        return;
    }

    lstrcpyW(ClassPath, DeviceClassesPrefix);
    lstrcatW(ClassPath, GuidStr);

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, ClassPath, 0, NULL, REG_OPTION_NON_VOLATILE,
                            KEY_ALL_ACCESS, NULL, &ClassKey, NULL);
    if (Error == ERROR_ACCESS_DENIED)
    {
        skip("DeviceClasses registry key is not writable\n");
        return;
    }
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW(%ls) failed, error %lu\n", ClassPath, (unsigned long)Error);
    if (Error != ERROR_SUCCESS)
        return;

    Error = RegCreateKeyExW(ClassKey, L"Properties", 0, NULL, REG_OPTION_NON_VOLATILE,
                            KEY_ALL_ACCESS, NULL, &SubKey, NULL);
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW(Properties) failed, error %lu\n", (unsigned long)Error);
    if (Error != ERROR_SUCCESS)
        goto cleanup;
    RegCloseKey(SubKey);
    SubKey = NULL;

    Error = RegCreateKeyExW(ClassKey, L"##?#STALE_NO_DEVINST", 0, NULL, REG_OPTION_NON_VOLATILE,
                            KEY_ALL_ACCESS, NULL, &SubKey, NULL);
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW(##?#STALE_NO_DEVINST) failed, error %lu\n", (unsigned long)Error);
    if (Error != ERROR_SUCCESS)
        goto cleanup;
    RegCloseKey(SubKey);
    SubKey = NULL;

    Error = RegCreateKeyExW(ClassKey, L"##?#NO_SYMLINK", 0, NULL, REG_OPTION_NON_VOLATILE,
                            KEY_ALL_ACCESS, NULL, &SubKey, NULL);
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW(##?#NO_SYMLINK) failed, error %lu\n", (unsigned long)Error);
    if (Error != ERROR_SUCCESS)
        goto cleanup;

    Error = RegSetValueExW(SubKey, L"DeviceInstance", 0, REG_SZ,
                           (const BYTE *)DeviceInstanceValue, sizeof(DeviceInstanceValue));
    ok(Error == ERROR_SUCCESS, "RegSetValueExW(DeviceInstance) failed, error %lu\n", (unsigned long)Error);
    if (Error != ERROR_SUCCESS)
        goto cleanup;

    Error = RegCreateKeyExW(SubKey, L"#", 0, NULL, REG_OPTION_NON_VOLATILE,
                            KEY_ALL_ACCESS, NULL, &RefKey, NULL);
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW(#) failed, error %lu\n", (unsigned long)Error);
    if (Error != ERROR_SUCCESS)
        goto cleanup;

    RegCloseKey(RefKey);
    RefKey = NULL;
    RegCloseKey(SubKey);
    SubKey = NULL;
    RegCloseKey(ClassKey);
    ClassKey = NULL;

    SetLastError(0xdeadbeef);
    Set = SetupDiGetClassDevsW(&TestGuid, NULL, NULL, DIGCF_DEVICEINTERFACE);
    LastError = GetLastError();
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed, error %lu\n", LastError);
    if (Set != INVALID_HANDLE_VALUE)
    {
        ZeroMemory(&InterfaceData, sizeof(InterfaceData));
        InterfaceData.cbSize = sizeof(InterfaceData);
        SetLastError(0xdeadbeef);
        EnumRet = SetupDiEnumDeviceInterfaces(Set, NULL, &TestGuid, 0, &InterfaceData);
        LastError = GetLastError();
        ok(!EnumRet, "SetupDiEnumDeviceInterfaces unexpectedly succeeded\n");
        ok(LastError == ERROR_NO_MORE_ITEMS,
           "SetupDiEnumDeviceInterfaces error %lu, expected ERROR_NO_MORE_ITEMS\n", LastError);
        SetupDiDestroyDeviceInfoList(Set);
    }

cleanup:
    if (RefKey != NULL)
        RegCloseKey(RefKey);
    if (SubKey != NULL)
        RegCloseKey(SubKey);
    if (ClassKey != NULL)
        RegCloseKey(ClassKey);
    Error = RegDeleteTreeW(HKEY_LOCAL_MACHINE, ClassPath);
    ok(Error == ERROR_SUCCESS, "RegDeleteTreeW(%ls) failed, error %lu\n", ClassPath, (unsigned long)Error);
}
