/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The children of a disabled device leave the device tree, and a new set comes back with the device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#define XHCI_ID L"PCI\\VEN_1B36&DEV_000D"

static
BOOLEAN
FindController(
    _Out_writes_(Length) PWSTR InstanceId,
    _In_ ULONG Length)
{
    SP_DEVINFO_DATA Device;
    HDEVINFO Devices;
    BOOLEAN Found = FALSE;
    ULONG Index;

    Devices = SetupDiGetClassDevsW(NULL, L"PCI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (Devices == INVALID_HANDLE_VALUE) return FALSE;

    Device.cbSize = sizeof(Device);
    for (Index = 0; !Found && SetupDiEnumDeviceInfo(Devices, Index, &Device); Index++)
    {
        if (!SetupDiGetDeviceInstanceIdW(Devices, &Device, InstanceId, Length, NULL)) continue;
        Found = (_wcsnicmp(InstanceId, XHCI_ID, wcslen(XHCI_ID)) == 0);
    }

    SetupDiDestroyDeviceInfoList(Devices);
    return Found;
}

/* The enable request starts the root hub before it returns; the wait only bounds a slow start */
static
CONFIGRET
WaitStartedChild(
    _In_ DEVINST Controller,
    _Out_ PDEVINST Child,
    _Out_ PULONG Status,
    _Out_ PULONG Problem)
{
    CONFIGRET Result = CR_NO_SUCH_DEVNODE;
    ULONG Waited;

    *Status = 0;
    *Problem = 0;

    for (Waited = 0; Waited < 2000; Waited += 10)
    {
        Result = CM_Get_Child(Child, Controller, 0);
        if (Result == CR_SUCCESS &&
            CM_Get_DevNode_Status(Status, Problem, *Child, 0) == CR_SUCCESS &&
            (*Status & (DN_STARTED | DN_HAS_PROBLEM)))
        {
            break;
        }

        Sleep(10);
    }

    return Result;
}

/* Disables and enables the xHCI controller of the test VM, so it is never part of a sweep */
START_TEST(IoRemoveChildren)
{
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    WCHAR HubId[MAX_DEVICE_ID_LEN];
    DEVINST Controller, Hub, Other;
    ULONG Status, Problem, Round;
    CONFIGRET Result;

    if (skip(FindController(InstanceId, RTL_NUMBER_OF(InstanceId)), "No QEMU xHCI controller\n"))
        return;

    Result = CM_Locate_DevNodeW(&Controller, InstanceId, CM_LOCATE_DEVNODE_NORMAL);
    ok_eq_ulong(Result, (ULONG)CR_SUCCESS);
    if (Result != CR_SUCCESS) return;

    Result = WaitStartedChild(Controller, &Hub, &Status, &Problem);
    if (skip(Result == CR_SUCCESS && (Status & DN_STARTED), "The root hub is not started\n"))
        return;

    Result = CM_Get_Device_IDW(Hub, HubId, RTL_NUMBER_OF(HubId), 0);
    ok_eq_ulong(Result, (ULONG)CR_SUCCESS);
    if (Result != CR_SUCCESS) return;

    for (Round = 1; Round <= 2; Round++)
    {
        Result = CM_Disable_DevNode(Controller, 0);
        ok(Result == CR_SUCCESS, "round %lu: disabling gave %lu\n", Round, Result);

        Result = CM_Get_Child(&Other, Controller, 0);
        ok(Result == CR_NO_SUCH_DEVNODE, "round %lu: the disabled controller has a child (%lu)\n", Round, Result);

        Result = CM_Locate_DevNodeW(&Other, HubId, CM_LOCATE_DEVNODE_NORMAL);
        ok(Result == CR_NO_SUCH_DEVNODE, "round %lu: the root hub is present without its controller (%lu)\n", Round, Result);

        Result = CM_Enable_DevNode(Controller, 0);
        ok(Result == CR_SUCCESS, "round %lu: enabling gave %lu\n", Round, Result);

        Result = WaitStartedChild(Controller, &Hub, &Status, &Problem);
        ok(Result == CR_SUCCESS, "round %lu: the enabled controller has no child (%lu)\n", Round, Result);
        ok((Status & DN_STARTED) != 0, "round %lu: the root hub is not started (status 0x%lx, problem %lu)\n",
           Round, Status, Problem);

        Result = CM_Get_Sibling(&Other, Hub, 0);
        ok(Result == CR_NO_SUCH_DEVNODE, "round %lu: a second node sits beside the root hub (%lu)\n", Round, Result);
    }
}
