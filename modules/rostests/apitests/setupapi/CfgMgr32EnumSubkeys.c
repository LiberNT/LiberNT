/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for cfgmgr32 enumeration skipping non-device subkeys
 * COPYRIGHT:   Copyright 2026 Jean-Luc Robitaille <jlr@robitaille.us>
 */

#include <apitest.h>
#include <windows.h>
#include <cfgmgr32.h>

static const WCHAR TestClassKeyPath[] =
    L"SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\{7c4e9b21-3d8a-4f16-a5c2-91e0b8473d5e}";

static VOID
TestDeviceInterfaceSubkeys(VOID)
{
    GUID TestGuid = { 0x7c4e9b21, 0x3d8a, 0x4f16, { 0xa5, 0xc2, 0x91, 0xe0, 0xb8, 0x47, 0x3d, 0x5e } };
    HKEY ClassKey = NULL, SubKey = NULL;
    LSTATUS Error;
    CONFIGRET Cr;
    ULONG Size = 0, AllocSize;
    PWSTR Buf;

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, TestClassKeyPath, 0, NULL,
                            REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &ClassKey, NULL);
    if (Error == ERROR_ACCESS_DENIED)
    {
        skip("Insufficient privileges to create DeviceClasses test key\n");
        return;
    }
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW(test class) failed, error %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    Error = RegCreateKeyExW(ClassKey, L"Properties", 0, NULL,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &SubKey, NULL);
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW(Properties) failed, error %ld\n", Error);
    if (Error == ERROR_SUCCESS)
        RegCloseKey(SubKey);

    SubKey = NULL;
    Error = RegCreateKeyExW(ClassKey, L"##?#STALE_NO_DEVINST", 0, NULL,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &SubKey, NULL);
    ok(Error == ERROR_SUCCESS, "RegCreateKeyExW(stale subkey) failed, error %ld\n", Error);
    if (Error == ERROR_SUCCESS)
        RegCloseKey(SubKey);

    RegCloseKey(ClassKey);

    Cr = CM_Get_Device_Interface_List_SizeW(&Size, &TestGuid, NULL,
                                            CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES);
    ok(Cr == CR_SUCCESS, "CM_Get_Device_Interface_List_SizeW returned 0x%lx, expected CR_SUCCESS\n", Cr);
    ok(Size >= 1, "CM_Get_Device_Interface_List_SizeW returned Size %lu\n", Size);

    AllocSize = (Size > 0) ? Size : 1;
    Buf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, AllocSize * sizeof(WCHAR));
    ok(Buf != NULL, "HeapAlloc failed\n");
    if (Buf != NULL)
    {
        Cr = CM_Get_Device_Interface_ListW(&TestGuid, NULL, Buf, AllocSize,
                                           CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES);
        ok(Cr == CR_SUCCESS, "CM_Get_Device_Interface_ListW returned 0x%lx, expected CR_SUCCESS\n", Cr);
        ok(Buf[0] == UNICODE_NULL, "CM_Get_Device_Interface_ListW returned non-empty list\n");
        HeapFree(GetProcessHeap(), 0, Buf);
    }

    Error = RegDeleteTreeW(HKEY_LOCAL_MACHINE, TestClassKeyPath);
    ok(Error == ERROR_SUCCESS, "RegDeleteTreeW(test class) failed, error %ld\n", Error);
}

static VOID
TestEnumerateClasses(VOID)
{
    GUID ClassGuid;
    CONFIGRET Cr;
    ULONG Index;

    for (Index = 0; ; Index++)
    {
        ZeroMemory(&ClassGuid, sizeof(ClassGuid));
        Cr = CM_Enumerate_Classes_Ex(Index, &ClassGuid, 0, NULL);
        if (Cr == CR_NO_SUCH_VALUE)
            break;
        ok(Cr == CR_SUCCESS, "CM_Enumerate_Classes_Ex(%lu) returned 0x%lx\n", Index, Cr);
        if (Cr != CR_SUCCESS)
            break;
    }

    ok(Cr == CR_NO_SUCH_VALUE, "CM_Enumerate_Classes_Ex ended with 0x%lx, expected CR_NO_SUCH_VALUE\n", Cr);
    ok(Index > 0, "CM_Enumerate_Classes_Ex enumerated %lu classes\n", Index);
}

START_TEST(CfgMgr32EnumSubkeys)
{
    TestDeviceInterfaceSubkeys();
    TestEnumerateClasses();
}
