/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The removal policy of each device follows its removability, its bus and its parent, as Windows 11 reports it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <wdmguid.h>

static
ULONG
QueryUlong(
    _In_ DEVINST Device,
    _In_ ULONG Property,
    _In_ ULONG Missing)
{
    ULONG Value = 0, Size = sizeof(Value);

    if (CM_Get_DevNode_Registry_PropertyW(Device, Property, NULL, &Value, &Size, 0) != CR_SUCCESS) return Missing;
    return Value;
}

static
ULONG
ExpectedPolicy(
    _In_ DEVINST Device)
{
    ULONG Size = sizeof(GUID), Inherited;
    DEVINST Parent;
    GUID Bus;

    if (QueryUlong(Device, CM_DRP_CAPABILITIES, 0) & CM_DEVCAP_REMOVABLE)
    {
        if (CM_Get_DevNode_Registry_PropertyW(Device, CM_DRP_BUSTYPEGUID, NULL, &Bus, &Size, 0) == CR_SUCCESS &&
            (IsEqualGUID(&Bus, &GUID_BUS_TYPE_USB) || IsEqualGUID(&Bus, &GUID_BUS_TYPE_1394) ||
             IsEqualGUID(&Bus, &GUID_BUS_TYPE_PCMCIA)))
        {
            return CM_REMOVAL_POLICY_EXPECT_SURPRISE_REMOVAL;
        }

        return CM_REMOVAL_POLICY_EXPECT_ORDERLY_REMOVAL;
    }

    if (CM_Get_Parent(&Parent, Device, 0) != CR_SUCCESS) return CM_REMOVAL_POLICY_EXPECT_NO_REMOVAL;

    Inherited = QueryUlong(Parent, CM_DRP_REMOVAL_POLICY_HW_DEFAULT, MAXULONG);
    if (Inherited == CM_REMOVAL_POLICY_EXPECT_ORDERLY_REMOVAL || Inherited == CM_REMOVAL_POLICY_EXPECT_SURPRISE_REMOVAL)
    {
        return Inherited;
    }

    return CM_REMOVAL_POLICY_EXPECT_NO_REMOVAL;
}

START_TEST(IoRemovalPolicy)
{
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    ULONG Index, Policy, Expected, Surprise = 0, Checked = 0;
    HDEVINFO Devices;

    Devices = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed: %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE) return;

    for (Index = 0; SetupDiEnumDeviceInfo(Devices, Index, &Device); Index++)
    {
        if (!SetupDiGetDeviceInstanceIdW(Devices, &Device, InstanceId, RTL_NUMBER_OF(InstanceId), NULL) ||
            _wcsnicmp(InstanceId, L"HTREE\\", 6) == 0)
        {
            continue;
        }

        Policy = QueryUlong(Device.DevInst, CM_DRP_REMOVAL_POLICY, MAXULONG);
        Expected = ExpectedPolicy(Device.DevInst);
        ok(Policy == Expected, "%ls: removal policy %lu, expected %lu\n", InstanceId, Policy, Expected);
        ok_eq_ulong(QueryUlong(Device.DevInst, CM_DRP_REMOVAL_POLICY_HW_DEFAULT, MAXULONG), Expected);

        if (Expected == CM_REMOVAL_POLICY_EXPECT_SURPRISE_REMOVAL) Surprise++;
        Checked++;
    }

    SetupDiDestroyDeviceInfoList(Devices);

    ok(Checked != 0, "no device was checked\n");
    trace("%lu devices checked, %lu expect surprise removal\n", Checked, Surprise);
}
