/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a started device that cannot be disabled keeps its parent from being disabled
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <cfgmgr32.h>

START_TEST(DisableableParents)
{
    ULONG Length = 0, Checked = 0;
    CONFIGRET Result;
    PWSTR List, Id;

    Result = CM_Get_Device_ID_List_SizeW(&Length, NULL, CM_GETIDLIST_FILTER_NONE);
    ok(Result == CR_SUCCESS, "CM_Get_Device_ID_List_SizeW returned %lu\n", Result);
    if (Result != CR_SUCCESS || Length == 0)
        return;

    List = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length * sizeof(WCHAR));
    ok(List != NULL, "HeapAlloc failed\n");
    if (List == NULL)
        return;

    Result = CM_Get_Device_ID_ListW(NULL, List, Length, CM_GETIDLIST_FILTER_NONE);
    ok(Result == CR_SUCCESS, "CM_Get_Device_ID_ListW returned %lu\n", Result);

    for (Id = List; Result == CR_SUCCESS && *Id; Id += wcslen(Id) + 1)
    {
        DEVINST Instance, Parent;
        ULONG Status, Problem, ParentStatus, ParentProblem;
        WCHAR ParentId[MAX_DEVICE_ID_LEN];

        if (CM_Locate_DevNodeW(&Instance, Id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
            continue;
        if (CM_Get_DevNode_Status(&Status, &Problem, Instance, 0) != CR_SUCCESS)
            continue;
        if (!(Status & DN_STARTED) || (Status & DN_DISABLEABLE))
            continue;
        if (CM_Get_Parent(&Parent, Instance, 0) != CR_SUCCESS)
            continue;
        if (CM_Get_DevNode_Status(&ParentStatus, &ParentProblem, Parent, 0) != CR_SUCCESS)
            continue;

        ParentId[0] = UNICODE_NULL;
        CM_Get_Device_IDW(Parent, ParentId, RTL_NUMBER_OF(ParentId), 0);
        ok(!(ParentStatus & DN_DISABLEABLE), "%ls cannot be disabled, but its parent %ls can (0x%08lx)\n",
           Id, ParentId, ParentStatus);
        Checked++;
    }

    ok(Checked != 0, "no started device that cannot be disabled\n");
    HeapFree(GetProcessHeap(), 0, List);
}
