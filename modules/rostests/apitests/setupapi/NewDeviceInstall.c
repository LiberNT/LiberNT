/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a new device with no driver finishes its installation without user interaction
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <cfgmgr32.h>
#include <regstr.h>

static ULONG
GetConfigFlags(DEVINST Device)
{
    ULONG Flags = 0, Length = sizeof(Flags);

    CM_Get_DevNode_Registry_PropertyW(Device, CM_DRP_CONFIGFLAGS, NULL, &Flags, &Length, 0);
    return Flags;
}

/* Wait until an installation of the device has run, which marks a device without a driver. */
static BOOL
WaitForInstallAttempt(DEVINST Device, DWORD TimeoutMs, ULONG *Status, ULONG *Problem)
{
    DWORD Start = GetTickCount();

    do
    {
        if (CM_Get_DevNode_Status(Status, Problem, Device, 0) == CR_SUCCESS &&
            ((*Status & DN_HAS_PROBLEM) || (GetConfigFlags(Device) & CONFIGFLAG_FAILEDINSTALL)))
        {
            return TRUE;
        }
        Sleep(100);
    } while (GetTickCount() - Start < TimeoutMs);
    return FALSE;
}

START_TEST(NewDeviceInstall)
{
    static const WCHAR HardwareId[] = L"LNT_NEW_DEVICE_WITHOUT_DRIVER\0";
    WCHAR DeviceId[MAX_DEVICE_ID_LEN] = L"LNTNODRIVER";
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    DEVINST Root, Phantom, Device;
    ULONG Status = 0, Problem = 0;
    DWORD Start, Elapsed;
    CONFIGRET Result;
    BOOL Attempted;

    Result = CM_Locate_DevNodeW(&Root, NULL, CM_LOCATE_DEVNODE_NORMAL);
    ok(Result == CR_SUCCESS, "CM_Locate_DevNodeW failed: cr %lu\n", Result);
    if (Result != CR_SUCCESS)
        return;

    /* Register the device the way setupapi does: a phantom first, then its hardware ID. */
    Result = CM_Create_DevInstW(&Phantom, DeviceId, Root, CM_CREATE_DEVINST_PHANTOM | CM_CREATE_DEVINST_GENERATE_ID);
    if (Result == CR_ACCESS_DENIED)
    {
        skip("Creating a device needs an administrator\n");
        return;
    }
    ok(Result == CR_SUCCESS, "CM_Create_DevInstW(phantom) failed: cr %lu\n", Result);
    if (Result != CR_SUCCESS)
        return;

    InstanceId[0] = UNICODE_NULL;
    Result = CM_Get_Device_IDW(Phantom, InstanceId, ARRAYSIZE(InstanceId), 0);
    ok(Result == CR_SUCCESS, "CM_Get_Device_IDW failed: cr %lu\n", Result);
    Result = CM_Set_DevNode_Registry_PropertyW(Phantom, CM_DRP_HARDWAREID, HardwareId, sizeof(HardwareId), 0);
    ok(Result == CR_SUCCESS, "CM_Set_DevNode_Registry_PropertyW failed: cr %lu\n", Result);

    /* Making it present is the arrival of a new device. */
    Start = GetTickCount();
    Result = CM_Create_DevInstW(&Device, InstanceId, Root, CM_CREATE_DEVINST_NORMAL);
    ok(Result == CR_SUCCESS, "CM_Create_DevInstW(%ls) failed: cr %lu\n", InstanceId, Result);
    if (Result == CR_SUCCESS)
    {
        /* Windows 11 leaves a device created this way uninstalled; LiberNT installs it. Neither may wait on the user. */
        Attempted = WaitForInstallAttempt(Device, 10000, &Status, &Problem);
        ok(CMP_WaitNoPendingInstallEvents(5000) == WAIT_OBJECT_0, "An installation is still pending\n");
        Elapsed = GetTickCount() - Start;
        CM_Get_DevNode_Status(&Status, &Problem, Device, 0);
        ok(!(Status & DN_STARTED), "%ls: a device without a driver started, status 0x%lx\n", InstanceId, Status);
        trace("MEASURE %ls: attempted %d status 0x%lx problem %lu flags 0x%lx after %lu ms\n",
              InstanceId, Attempted, Status, Problem, GetConfigFlags(Device), Elapsed);
    }

    Result = CM_Uninstall_DevNode(Phantom, 0);
    ok(Result == CR_SUCCESS, "CM_Uninstall_DevNode failed: cr %lu\n", Result);
}
