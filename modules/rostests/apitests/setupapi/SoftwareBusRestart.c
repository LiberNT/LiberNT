/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Restart the software device enumerator through its class installer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

static ULONG
CountChildren(DEVINST Device)
{
    DEVINST Child;
    ULONG Count = 0;

    if (CM_Get_Child(&Child, Device, 0) != CR_SUCCESS)
        return 0;
    do
        Count++;
    while (CM_Get_Sibling(&Child, Child, 0) == CR_SUCCESS);
    return Count;
}

START_TEST(SoftwareBusRestart)
{
    SP_DEVINSTALL_PARAMS_W InstallParams;
    SP_PROPCHANGE_PARAMS Params;
    SP_DEVINFO_DATA Data;
    ULONG Status, Problem, Before, After, Wait;
    DEVINST Device;
    HDEVINFO Set;
    CONFIGRET Cr;
    BOOL Ret;

    Cr = CM_Locate_DevNodeW(&Device, (DEVINSTID_W)L"ROOT\\SYSTEM\\0000", CM_LOCATE_DEVNODE_NORMAL);
    if (Cr != CR_SUCCESS)
    {
        skip("ROOT\\SYSTEM\\0000 is not present, CM_Locate_DevNodeW returned 0x%lx\n", Cr);
        return;
    }
    ok(CM_Get_DevNode_Status(&Status, &Problem, Device, 0) == CR_SUCCESS, "CM_Get_DevNode_Status failed\n");
    ok(Status & DN_STARTED, "the software bus is not started, status 0x%lx problem %lu\n", Status, Problem);
    Before = CountChildren(Device);

    Set = SetupDiCreateDeviceInfoList(NULL, NULL);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;
    Data.cbSize = sizeof(Data);
    Ret = SetupDiOpenDeviceInfoW(Set, L"ROOT\\SYSTEM\\0000", NULL, 0, &Data);
    ok(Ret, "SetupDiOpenDeviceInfoW failed with %lu\n", GetLastError());
    if (!Ret)
        goto Done;

    Params.ClassInstallHeader.cbSize = sizeof(Params.ClassInstallHeader);
    Params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    Params.StateChange = DICS_PROPCHANGE;
    Params.Scope = DICS_FLAG_GLOBAL;
    Params.HwProfile = 0;
    ok(SetupDiSetClassInstallParamsW(Set, &Data, &Params.ClassInstallHeader, sizeof(Params)),
       "SetupDiSetClassInstallParamsW failed with %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Ret = SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, Set, &Data);
    if (!Ret && GetLastError() == ERROR_ACCESS_DENIED)
    {
        skip("Restarting a device needs administrator rights\n");
        goto Done;
    }
    ok(Ret, "DIF_PROPERTYCHANGE failed with 0x%lx\n", GetLastError());

    InstallParams.cbSize = sizeof(InstallParams);
    ok(SetupDiGetDeviceInstallParamsW(Set, &Data, &InstallParams), "SetupDiGetDeviceInstallParamsW failed with %lu\n", GetLastError());
    if (InstallParams.Flags & DI_NEEDREBOOT)
        trace("A child in use kept the software bus from being removed, flags 0x%lx\n", InstallParams.Flags);
    else
        ok(!(InstallParams.Flags & DI_NEEDRESTART), "the restart asks for a restart, flags 0x%lx\n", InstallParams.Flags);

    Status = Problem = 0;
    ok(CM_Get_DevNode_Status(&Status, &Problem, Device, 0) == CR_SUCCESS, "CM_Get_DevNode_Status failed after the restart\n");
    ok(Status & DN_STARTED, "the software bus is not started after the restart, status 0x%lx problem %lu\n", Status, Problem);
    ok(!(Status & DN_HAS_PROBLEM), "the software bus has problem %lu after the restart\n", Problem);
    for (Wait = 0, After = CountChildren(Device); After != Before && Wait < 40; Wait++, After = CountChildren(Device))
        Sleep(50);
    ok(After == Before, "the software bus has %lu children after the restart, %lu before\n", After, Before);

Done:
    SetupDiDestroyDeviceInfoList(Set);
}
