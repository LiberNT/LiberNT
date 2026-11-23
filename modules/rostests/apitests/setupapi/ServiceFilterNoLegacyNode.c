/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A service filter on the device ID list creates no legacy device node
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <cfgmgr32.h>

static
VOID
TestServiceList(
    _In_ PCWSTR Service,
    _In_ ULONG Flags,
    _In_ BOOL ExpectDevices)
{
    WCHAR Buffer[4096];
    ULONG Size = 0;
    CONFIGRET Cr;

    Cr = CM_Get_Device_ID_List_SizeW(&Size, Service, Flags);
    ok(Cr == CR_SUCCESS, "%ls 0x%lx: size returned %lu\n", Service, Flags, Cr);
    if (ExpectDevices)
        ok(Size > 1, "%ls 0x%lx: size %lu for a service with devices\n", Service, Flags, Size);
    else
        ok(Size == 1, "%ls 0x%lx: size %lu for a service without devices\n", Service, Flags, Size);

    Buffer[0] = Buffer[1] = 0xCCCC;
    Cr = CM_Get_Device_ID_ListW(Service, Buffer, ARRAYSIZE(Buffer), Flags);
    ok(Cr == CR_SUCCESS, "%ls 0x%lx: list returned %lu\n", Service, Flags, Cr);
    if (ExpectDevices)
        ok(Buffer[0] != UNICODE_NULL, "%ls 0x%lx: the list is empty\n", Service, Flags);
    else
        ok(Buffer[0] == UNICODE_NULL, "%ls 0x%lx: the list holds '%ls'\n", Service, Flags, Buffer);
}

static
VOID
TestNoLegacyNode(
    _In_ PCWSTR InstanceId)
{
    DEVINST Instance;
    CONFIGRET Cr;

    Cr = CM_Locate_DevNodeW(&Instance, (DEVINSTID_W)InstanceId, CM_LOCATE_DEVNODE_PHANTOM);
    ok(Cr == CR_NO_SUCH_DEVNODE, "%ls: locating returned %lu\n", InstanceId, Cr);
}

START_TEST(ServiceFilterNoLegacyNode)
{
    static const PCWSTR LegacyServices[] = { L"Null", L"Beep", L"LiberNTNoSuchService" };
    static const PCWSTR LegacyNodes[] = { L"ROOT\\LEGACY_NULL\\0000", L"ROOT\\LEGACY_BEEP\\0000",
                                          L"ROOT\\LEGACY_LIBERNTNOSUCHSERVICE\\0000" };
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(LegacyServices); Index++)
    {
        TestServiceList(LegacyServices[Index], CM_GETIDLIST_FILTER_SERVICE | CM_GETIDLIST_DONOTGENERATE, FALSE);
        TestServiceList(LegacyServices[Index], CM_GETIDLIST_FILTER_SERVICE, FALSE);
        TestServiceList(LegacyServices[Index], CM_GETIDLIST_FILTER_SERVICE | CM_GETIDLIST_FILTER_PRESENT, FALSE);
    }

    for (Index = 0; Index < ARRAYSIZE(LegacyNodes); Index++)
        TestNoLegacyNode(LegacyNodes[Index]);

    TestServiceList(L"disk", CM_GETIDLIST_FILTER_SERVICE | CM_GETIDLIST_DONOTGENERATE, TRUE);
    TestServiceList(L"disk", CM_GETIDLIST_FILTER_SERVICE, TRUE);
}
