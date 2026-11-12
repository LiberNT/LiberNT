/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device setup classes present on a fresh system before any device of the class is installed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <cfgmgr32.h>

static const struct
{
    const GUID *ClassGuid;
    PCWSTR ClassName;
} InboxClasses[] =
{
    { &GUID_DEVCLASS_BATTERY, L"Battery" },
    { &GUID_DEVCLASS_BLUETOOTH, L"Bluetooth" },
    { &GUID_DEVCLASS_COMPUTER, L"Computer" },
    { &GUID_DEVCLASS_LEGACYDRIVER, L"LegacyDriver" },
    { &GUID_DEVCLASS_MONITOR, L"Monitor" },
    { &GUID_DEVCLASS_PORTS, L"Ports" },
    { &GUID_DEVCLASS_UNKNOWN, L"Unknown" },
};

START_TEST(InboxClasses)
{
    WCHAR ClassName[MAX_CLASS_NAME_LEN];
    DWORD RequiredSize;
    HKEY ClassKey;
    ULONG Index;
    BOOL Result;

    for (Index = 0; Index < ARRAYSIZE(InboxClasses); Index++)
    {
        ClassName[0] = UNICODE_NULL;
        RequiredSize = 0;
        Result = SetupDiClassNameFromGuidW(InboxClasses[Index].ClassGuid, ClassName, ARRAYSIZE(ClassName),
                                           &RequiredSize);
        ok(Result, "%S: SetupDiClassNameFromGuidW failed: %lu\n", InboxClasses[Index].ClassName, GetLastError());
        ok(Result && !wcscmp(ClassName, InboxClasses[Index].ClassName), "Expected class name %S, got %S\n",
           InboxClasses[Index].ClassName, ClassName);
        ok(!Result || RequiredSize == wcslen(InboxClasses[Index].ClassName) + 1, "%S: required size %lu\n",
           InboxClasses[Index].ClassName, RequiredSize);

        ClassKey = SetupDiOpenClassRegKey(InboxClasses[Index].ClassGuid, KEY_QUERY_VALUE);
        ok(ClassKey != INVALID_HANDLE_VALUE, "%S: SetupDiOpenClassRegKey failed: %lu\n", InboxClasses[Index].ClassName,
           GetLastError());
        if (ClassKey != INVALID_HANDLE_VALUE)
            RegCloseKey(ClassKey);
    }
}
