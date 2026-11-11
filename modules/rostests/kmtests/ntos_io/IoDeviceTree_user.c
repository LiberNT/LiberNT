/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The names and states of device nodes that every boot creates, as Windows gives them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <setupapi.h>
#include <cfgmgr32.h>

static
BOOLEAN
LocateStarted(
    _In_ PCWSTR InstanceId,
    _Out_ PDEVINST Device)
{
    ULONG Status = 0, Problem = 0;
    CONFIGRET Result;

    Result = CM_Locate_DevNodeW(Device, (DEVINSTID_W)InstanceId, CM_LOCATE_DEVNODE_NORMAL);
    ok(Result == CR_SUCCESS, "%ls: CM_Locate_DevNodeW returned %lu\n", InstanceId, Result);
    if (Result != CR_SUCCESS) return FALSE;

    Result = CM_Get_DevNode_Status(&Status, &Problem, *Device, 0);
    ok(Result == CR_SUCCESS, "%ls: CM_Get_DevNode_Status returned %lu\n", InstanceId, Result);
    ok((Status & DN_STARTED) && !(Status & DN_HAS_PROBLEM),
       "%ls: status 0x%lx, problem %lu\n", InstanceId, Status, Problem);
    return TRUE;
}

/* IoReportDetectedDevice names the device ROOT\<service>\0000 and gives it DETECTED<interface>\<service> and DETECTED\<service> */
static
VOID
CheckReportedHal(VOID)
{
    WCHAR Ids[256];
    ULONG Size = sizeof(Ids) - 2 * sizeof(WCHAR);
    CONFIGRET Result;
    DEVINST Hal;

    if (!LocateStarted(L"ROOT\\ACPI_HAL\\0000", &Hal)) return;

    RtlZeroMemory(Ids, sizeof(Ids));
    Result = CM_Get_DevNode_Registry_PropertyW(Hal, CM_DRP_COMPATIBLEIDS, NULL, Ids, &Size, 0);
    ok_eq_ulong(Result, (ULONG)CR_SUCCESS);
    if (Result != CR_SUCCESS) return;

    ok(_wcsicmp(Ids, L"DETECTEDInternal\\ACPI_HAL") == 0, "first compatible ID %ls\n", Ids);
    ok(_wcsicmp(Ids + wcslen(Ids) + 1, L"DETECTED\\ACPI_HAL") == 0,
       "second compatible ID %ls\n", Ids + wcslen(Ids) + 1);
}

/* A reported device's driver object name never becomes part of a root device ID */
static
VOID
CheckRootDevices(VOID)
{
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    SP_DEVINFO_DATA Device;
    HDEVINFO Devices;
    ULONG Index, Count = 0;

    Devices = SetupDiGetClassDevsW(NULL, L"ROOT", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Devices != INVALID_HANDLE_VALUE, "no root device list, error %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE) return;

    Device.cbSize = sizeof(Device);
    for (Index = 0; SetupDiEnumDeviceInfo(Devices, Index, &Device); Index++)
    {
        if (!SetupDiGetDeviceInstanceIdW(Devices, &Device, InstanceId, RTL_NUMBER_OF(InstanceId), NULL)) continue;

        Count++;
        ok(_wcsnicmp(InstanceId, L"ROOT\\DRIVER\\", 12) != 0, "root device %ls\n", InstanceId);
    }

    ok(Count != 0, "no root device is present\n");
    SetupDiDestroyDeviceInfoList(Devices);
}

/* An ACPI string _UID is the instance ID with the characters an ID may not hold replaced, as Windows 8.1 shows under QEMU */
static
VOID
CheckAcpiInstances(VOID)
{
    static const PCWSTR QemuResources[] =
    {
        L"ACPI\\PNP0A06\\CPU_HOTPLUG_RESOURCES",
        L"ACPI\\PNP0A06\\GPE0_RESOURCES",
        L"ACPI\\PNP0A06\\PCI_HOTPLUG_RESOURCES",
    };
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    SP_DEVINFO_DATA Device;
    HDEVINFO Devices;
    BOOLEAN Qemu = FALSE;
    CONFIGRET Result;
    ULONG Index, Char;
    DEVINST Node;

    Devices = SetupDiGetClassDevsW(NULL, L"ACPI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (skip(Devices != INVALID_HANDLE_VALUE, "No ACPI device list\n")) return;

    Device.cbSize = sizeof(Device);
    for (Index = 0; SetupDiEnumDeviceInfo(Devices, Index, &Device); Index++)
    {
        if (!SetupDiGetDeviceInstanceIdW(Devices, &Device, InstanceId, RTL_NUMBER_OF(InstanceId), NULL)) continue;

        if (_wcsnicmp(InstanceId, L"ACPI\\QEMU0002\\", 14) == 0) Qemu = TRUE;

        for (Char = 0; InstanceId[Char] != UNICODE_NULL; Char++)
        {
            if (InstanceId[Char] <= L' ' || InstanceId[Char] > 0x7F || InstanceId[Char] == L',') break;
        }

        ok(InstanceId[Char] == UNICODE_NULL, "%ls holds character 0x%x\n", InstanceId, InstanceId[Char]);
    }

    SetupDiDestroyDeviceInfoList(Devices);

    if (skip(Qemu, "Not a QEMU machine\n")) return;

    for (Index = 0; Index < RTL_NUMBER_OF(QemuResources); Index++)
    {
        Result = CM_Locate_DevNodeW(&Node, (DEVINSTID_W)QemuResources[Index], CM_LOCATE_DEVNODE_NORMAL);
        ok(Result == CR_SUCCESS, "%ls: CM_Locate_DevNodeW returned %lu\n", QemuResources[Index], Result);
    }
}

/* The root Basic Display fallback is started even when another display adapter holds the boot display, as on Windows 11 */
static
VOID
CheckBasicDisplayFallback(VOID)
{
    DEVINST Node;

    if (skip(CM_Locate_DevNodeW(&Node, (DEVINSTID_W)L"ROOT\\BASICDISPLAY\\0000", CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS,
             "No Basic Display fallback\n"))
    {
        return;
    }

    LocateStarted(L"ROOT\\BASICDISPLAY\\0000", &Node);
}

START_TEST(IoDeviceTree)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    CheckReportedHal();
#endif
    CheckRootDevices();
    CheckAcpiInstances();
    CheckBasicDisplayFallback();
}
