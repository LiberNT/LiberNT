/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power state of a PCI device that is disabled and enabled again
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winreg.h>
#include <cfgmgr32.h>

#define NET_CLASS_GUID L"{4d36e972-e325-11ce-bfc1-08002be10318}"
#define USB_CLASS_GUID L"{36fc9e60-c465-11cf-8056-444553540000}"

typedef struct _TEST_POWER_DATA
{
    ULONG PD_Size;
    DEVICE_POWER_STATE PD_MostRecentPowerState;
    ULONG PD_Capabilities;
    ULONG PD_D1Latency;
    ULONG PD_D2Latency;
    ULONG PD_D3Latency;
    DEVICE_POWER_STATE PD_PowerStateMapping[PowerSystemMaximum];
    SYSTEM_POWER_STATE PD_DeepestSystemWake;
} TEST_POWER_DATA;

static DEVINST DevInst;
static WCHAR InstanceId[MAX_DEVICE_ID_LEN];

static BOOL IsStartedWithClass(DEVINST Candidate, PCWSTR Class)
{
    WCHAR ClassGuid[64];
    ULONG Size = sizeof(ClassGuid);
    ULONG Status, Problem;

    if (CM_Get_DevNode_Status(&Status, &Problem, Candidate, 0) != CR_SUCCESS || !(Status & DN_STARTED))
        return FALSE;
    if (CM_Get_DevNode_Registry_PropertyW(Candidate, CM_DRP_CLASSGUID, NULL, ClassGuid, &Size, 0) != CR_SUCCESS)
        return FALSE;
    return _wcsicmp(ClassGuid, Class) == 0;
}

static BOOL IsNetAdapter(DEVINST Candidate)
{
    return IsStartedWithClass(Candidate, NET_CLASS_GUID);
}

static BOOL IsIdleUsbController(DEVINST Candidate)
{
    DEVINST RootHub, Device;

    if (!IsStartedWithClass(Candidate, USB_CLASS_GUID) ||
        CM_Get_Child(&RootHub, Candidate, 0) != CR_SUCCESS)
    {
        return FALSE;
    }
    return CM_Get_Child(&Device, RootHub, 0) == CR_NO_SUCH_DEVNODE;
}

static BOOL FindPciDevice(PCWSTR List, BOOL (*Match)(DEVINST))
{
    PCWSTR Id;
    DEVINST Candidate;

    for (Id = List; *Id; Id += wcslen(Id) + 1)
    {
        if (CM_Locate_DevNodeW(&Candidate, (DEVINSTID_W)Id, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS &&
            Match(Candidate))
        {
            DevInst = Candidate;
            wcsncpy(InstanceId, Id, _countof(InstanceId) - 1);
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL FindTestDevice(void)
{
    static WCHAR List[65536];

    if (CM_Get_Device_ID_ListW(L"PCI", List, _countof(List),
                               CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS)
    {
        return FALSE;
    }
    return FindPciDevice(List, IsNetAdapter) || FindPciDevice(List, IsIdleUsbController);
}

static ULONG ReadPowerState(void)
{
    TEST_POWER_DATA PowerData;
    ULONG Size = sizeof(PowerData);
    CONFIGRET Result;

    ZeroMemory(&PowerData, sizeof(PowerData));
    Result = CM_Get_DevNode_Registry_PropertyW(DevInst, CM_DRP_DEVICE_POWER_DATA, NULL, &PowerData, &Size, 0);
    ok_int(Result, CR_SUCCESS);
    if (Result != CR_SUCCESS)
        return PowerDeviceUnspecified;
    ok_long(PowerData.PD_Size, sizeof(PowerData));
    return PowerData.PD_MostRecentPowerState;
}

static BOOL WaitStarted(void)
{
    ULONG Status, Problem, Try;

    for (Try = 0; Try < 50; Try++)
    {
        if (CM_Get_DevNode_Status(&Status, &Problem, DevInst, 0) == CR_SUCCESS &&
            ((Status & DN_STARTED) || (Status & DN_HAS_PROBLEM)))
        {
            return (Status & DN_STARTED) != 0;
        }
        Sleep(200);
    }
    return FALSE;
}

START_TEST(DevicePowerOnDisable)
{
    CONFIGRET Result;

    if (!FindTestDevice())
    {
        skip("No started PCI network adapter or idle USB host controller\n");
        return;
    }
    trace("Test device %S\n", InstanceId);

    ok_long(ReadPowerState(), PowerDeviceD0);

    Result = CM_Disable_DevNode(DevInst, CM_DISABLE_UI_NOT_OK);
    if (Result == CR_ACCESS_DENIED)
    {
        skip("Disabling a device needs administrator rights\n");
        return;
    }
    ok_int(Result, CR_SUCCESS);
    if (Result != CR_SUCCESS)
        return;
    ok_long(ReadPowerState(), PowerDeviceD3);

    ok_int(CM_Enable_DevNode(DevInst, 0), CR_SUCCESS);
    ok(WaitStarted(), "The device did not start again\n");
    ok_long(ReadPowerState(), PowerDeviceD0);
}
