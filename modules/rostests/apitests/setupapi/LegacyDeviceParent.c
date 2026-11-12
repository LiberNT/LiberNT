/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Place of the PS/2 keyboard and mouse controllers in the device tree
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

static
BOOL
MultiSzHasId(
    _In_ PCWSTR List,
    _In_ PCWSTR Id)
{
    for (; *List; List += wcslen(List) + 1)
    {
        if (!_wcsicmp(List, Id))
            return TRUE;
    }
    return FALSE;
}

static
BOOL
DevNodeHasId(
    _In_ DEVINST DevInst,
    _In_ PCWSTR Id)
{
    static const ULONG Properties[] = { CM_DRP_HARDWAREID, CM_DRP_COMPATIBLEIDS };
    WCHAR Buffer[1024];
    ULONG Index, Size;

    for (Index = 0; Index < ARRAYSIZE(Properties); Index++)
    {
        ZeroMemory(Buffer, sizeof(Buffer));
        Size = sizeof(Buffer) - 2 * sizeof(WCHAR);
        if (CM_Get_DevNode_Registry_PropertyW(DevInst, Properties[Index], NULL, Buffer, &Size, 0) == CR_SUCCESS &&
            MultiSzHasId(Buffer, Id))
        {
            return TRUE;
        }
    }
    return FALSE;
}

static
BOOL
HasPciRootAncestor(
    _In_ DEVINST DevInst)
{
    DEVINST Parent;

    while (CM_Get_Parent(&Parent, DevInst, 0) == CR_SUCCESS)
    {
        if (DevNodeHasId(Parent, L"*PNP0A03") || DevNodeHasId(Parent, L"*PNP0A08"))
            return TRUE;
        DevInst = Parent;
    }
    return FALSE;
}

START_TEST(LegacyDeviceParent)
{
    SP_DEVINFO_DATA Data = { sizeof(Data) };
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    ULONG Status, Problem;
    HDEVINFO Set;
    DWORD Index;
    ULONG Count = 0;

    Set = SetupDiGetClassDevsW(NULL, L"ACPI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    for (Index = 0; SetupDiEnumDeviceInfo(Set, Index, &Data); Index++)
    {
        if (!DevNodeHasId(Data.DevInst, L"*PNP0303") && !DevNodeHasId(Data.DevInst, L"*PNP0F13"))
            continue;

        if (CM_Get_Device_IDW(Data.DevInst, InstanceId, ARRAYSIZE(InstanceId), 0) != CR_SUCCESS)
            InstanceId[0] = UNICODE_NULL;
        trace("PS/2 device %S\n", InstanceId);
        Count++;

        ok(HasPciRootAncestor(Data.DevInst), "%S has no PCI root among its ancestors\n", InstanceId);
        Status = Problem = 0;
        ok_long(CM_Get_DevNode_Status(&Status, &Problem, Data.DevInst, 0), CR_SUCCESS);
        ok(!(Status & DN_HAS_PROBLEM) || Problem != CM_PROB_NORMAL_CONFLICT,
           "%S has a resource conflict\n", InstanceId);
    }
    ok_long(GetLastError(), ERROR_NO_MORE_ITEMS);
    SetupDiDestroyDeviceInfoList(Set);

    if (!Count)
        skip("No PS/2 keyboard or mouse controller\n");
}
