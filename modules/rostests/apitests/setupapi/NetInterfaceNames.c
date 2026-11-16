/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Network adapter interfaces are registered under names without embedded NULs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <ndisguid.h>

#define NET_CLASS_KEY L"SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\{cac88484-7515-4c03-82e6-71a87abac361}"
#define COPY_TARGET_KEY L"Software\\LiberNTApiTest\\NetInterfaceNames"

typedef LSTATUS (WINAPI *PFN_REG_COPY_TREE_W)(HKEY, LPCWSTR, HKEY);

static
DWORD
CheckSubkeyNames(
    _In_ HKEY Key,
    _In_ PCWSTR Path,
    _In_ ULONG Depth)
{
    WCHAR Name[256];
    DWORD Index, Length, Checked = 0;
    LSTATUS Error;
    HKEY SubKey;

    for (Index = 0;; Index++)
    {
        Length = RTL_NUMBER_OF(Name);
        Error = RegEnumKeyExW(Key, Index, Name, &Length, NULL, NULL, NULL, NULL);
        if (Error == ERROR_NO_MORE_ITEMS)
            break;
        ok(Error == ERROR_SUCCESS, "%ls: RegEnumKeyExW(%lu) returned %ld\n", Path, Index, Error);
        if (Error != ERROR_SUCCESS)
            break;

        ok(Length == wcslen(Name), "%ls: subkey %lu reports %lu characters, %Iu before the first NUL [%ls]\n",
           Path, Index, Length, wcslen(Name), Name);
        Checked++;

        if (Depth == 0)
            continue;

        Error = RegOpenKeyExW(Key, Name, 0, KEY_READ, &SubKey);
        ok(Error == ERROR_SUCCESS, "%ls: subkey [%ls] cannot be opened by its enumerated name: %ld\n", Path, Name, Error);
        if (Error != ERROR_SUCCESS)
            continue;
        Checked += CheckSubkeyNames(SubKey, Name, Depth - 1);
        RegCloseKey(SubKey);
    }

    return Checked;
}

static
VOID
TestRegistryNames(VOID)
{
    PFN_REG_COPY_TREE_W pRegCopyTreeW;
    HKEY ClassKey, Target;
    LSTATUS Error;

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, NET_CLASS_KEY, 0, KEY_READ, &ClassKey);
    if (Error == ERROR_FILE_NOT_FOUND)
    {
        skip("no network adapter interface is registered\n");
        return;
    }
    ok(Error == ERROR_SUCCESS, "RegOpenKeyExW(network interface class) returned %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    ok(CheckSubkeyNames(ClassKey, L"{cac88484-7515-4c03-82e6-71a87abac361}", 2) != 0,
       "the network interface class has no interface\n");

    pRegCopyTreeW = (PFN_REG_COPY_TREE_W)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegCopyTreeW");
    ok(pRegCopyTreeW != NULL, "RegCopyTreeW is not exported\n");
    if (pRegCopyTreeW != NULL)
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, COPY_TARGET_KEY);
        Error = RegCreateKeyExW(HKEY_CURRENT_USER, COPY_TARGET_KEY, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                                NULL, &Target, NULL);
        ok(Error == ERROR_SUCCESS, "RegCreateKeyExW(copy target) returned %ld\n", Error);
        if (Error == ERROR_SUCCESS)
        {
            Error = pRegCopyTreeW(ClassKey, NULL, Target);
            ok(Error == ERROR_SUCCESS, "RegCopyTreeW(network interface class) returned %ld\n", Error);
            RegCloseKey(Target);
            RegDeleteTreeW(HKEY_CURRENT_USER, COPY_TARGET_KEY);
            RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\LiberNTApiTest");
        }
    }

    RegCloseKey(ClassKey);
}

static
VOID
TestInterfaceList(VOID)
{
    PWSTR List, Entry;
    CONFIGRET Result;
    ULONG Length, Used = 0, Paths = 0;

    Result = CM_Get_Device_Interface_List_SizeW(&Length, (LPGUID)&GUID_DEVINTERFACE_NET, NULL,
                                                CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    ok(Result == CR_SUCCESS, "CM_Get_Device_Interface_List_SizeW returned %lu\n", Result);
    if (Result != CR_SUCCESS)
        return;
    if (Length <= 1)
    {
        skip("no network adapter interface is present\n");
        return;
    }

    List = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length * sizeof(WCHAR));
    ok(List != NULL, "out of memory\n");
    if (List == NULL)
        return;

    Result = CM_Get_Device_Interface_ListW((LPGUID)&GUID_DEVINTERFACE_NET, NULL, List, Length,
                                           CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    ok(Result == CR_SUCCESS, "CM_Get_Device_Interface_ListW returned %lu\n", Result);
    if (Result == CR_SUCCESS)
    {
        for (Entry = List; *Entry != UNICODE_NULL; Entry += wcslen(Entry) + 1)
        {
            ok(_wcsnicmp(Entry, L"\\\\?\\", 4) == 0, "interface path %ls\n", Entry);
            Used += (ULONG)wcslen(Entry) + 1;
            Paths++;
        }
        ok(Paths != 0, "the interface list is empty\n");
        ok(Used + 1 == Length, "the paths use %lu of %lu characters: the list holds an empty entry\n", Used + 1, Length);
    }

    HeapFree(GetProcessHeap(), 0, List);
}

START_TEST(NetInterfaceNames)
{
    TestRegistryNames();
    TestInterfaceList();
}
