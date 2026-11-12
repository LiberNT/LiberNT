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
#include <regstr.h>

/* ok_eq_ulong evaluates its value twice: a call with an effect goes through this once */
#define ok_once(call, expected) \
    do { ULONG Once_ = (ULONG)(call); ok(Once_ == (ULONG)(expected), #call " = %lu, expected %lu\n", Once_, (ULONG)(expected)); } while (0)

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

#if defined(_M_IX86) || defined(_M_AMD64)
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
#endif

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

/* With an ESRT the HAL reports ACPI_HAL\UEFI, and each firmware resource has HARDWARE\UEFI\ESRT\{guid} with the
 * values Windows 11 writes there; without one there is neither */
static VOID CheckFirmwareResources(VOID)
{
    static const PCWSTR Values[] = { L"Type", L"Version", L"LowestSupportedVersion", L"LastAttemptVersion",
                                     L"LastAttemptStatus" };
    WCHAR Name[64];
    DWORD Index, Value, Length, Size, Type, Resources = 0;
    DEVINST Uefi;
    HKEY Esrt, Resource;
    BOOL Device;
    LONG Error;
    ULONG Item;

    Device = (CM_Locate_DevNodeW(&Uefi, (DEVINSTID_W)L"ACPI_HAL\\UEFI\\0", CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS);
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\UEFI\\ESRT", 0, KEY_READ, &Esrt);
    if (!Device)
    {
        ok(Error != ERROR_SUCCESS, "Firmware resources are published without an ACPI_HAL\\UEFI device\n");
        if (Error == ERROR_SUCCESS)
            RegCloseKey(Esrt);
        return;
    }

    ok(Error == ERROR_SUCCESS, "ACPI_HAL\\UEFI exists but HARDWARE\\UEFI\\ESRT does not (%ld)\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    for (Index = 0;; Index++)
    {
        Length = RTL_NUMBER_OF(Name);
        if (RegEnumKeyExW(Esrt, Index, Name, &Length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(Esrt, Name, 0, KEY_QUERY_VALUE, &Resource) != ERROR_SUCCESS)
            continue;
        Resources++;
        for (Item = 0; Item < RTL_NUMBER_OF(Values); Item++)
        {
            Size = sizeof(Value);
            ok(RegQueryValueExW(Resource, Values[Item], NULL, &Type, (PBYTE)&Value, &Size) == ERROR_SUCCESS && Type == REG_DWORD,
               "The firmware resource %ls has no %ls value\n", Name, Values[Item]);
        }
        RegCloseKey(Resource);
    }
    RegCloseKey(Esrt);

    ok(Resources != 0, "HARDWARE\\UEFI\\ESRT lists no firmware resource\n");
    trace("%lu firmware resources\n", Resources);
}

static
BOOLEAN
IsNullDriverDevice(
    _In_ DEVINST Device)
{
    WCHAR Value[MAX_PATH];
    ULONG Flags = 0;
    ULONG Size;

    Size = sizeof(Value);
    if (CM_Get_DevNode_Registry_PropertyW(Device, CM_DRP_DRIVER, NULL, Value, &Size, 0) != CR_SUCCESS) return FALSE;

    Size = sizeof(Value);
    if (CM_Get_DevNode_Registry_PropertyW(Device, CM_DRP_SERVICE, NULL, Value, &Size, 0) == CR_SUCCESS && Value[0] != UNICODE_NULL)
        return FALSE;

    Size = sizeof(Flags);
    if (CM_Get_DevNode_Registry_PropertyW(Device, CM_DRP_CAPABILITIES, NULL, &Flags, &Size, 0) == CR_SUCCESS &&
        (Flags & CM_DEVCAP_RAWDEVICEOK))
    {
        return FALSE;
    }

    Flags = 0;
    Size = sizeof(Flags);
    CM_Get_DevNode_Registry_PropertyW(Device, CM_DRP_CONFIGFLAGS, NULL, &Flags, &Size, 0);
    return !(Flags & (CONFIGFLAG_REINSTALL | CONFIGFLAG_FAILEDINSTALL));
}

/* A bus-reported device left needing install was offered to user mode: its install or its failure gave it a class */
static VOID CheckInstallOffered(PCWSTR Id, DEVINST Device, DEVINST Root)
{
    ULONG Status = 0, Problem = 0;
    WCHAR Guid[64];
    ULONG Size = sizeof(Guid);
    DEVINST Parent;

    if (CM_Get_DevNode_Status(&Status, &Problem, Device, 0) != CR_SUCCESS || !(Status & DN_HAS_PROBLEM) ||
        Problem != CM_PROB_FAILED_INSTALL || CM_Get_Parent(&Parent, Device, 0) != CR_SUCCESS || Parent == Root)
    {
        return;
    }

    ok(CM_Get_DevNode_Registry_PropertyW(Device, CM_DRP_CLASSGUID, NULL, Guid, &Size, 0) == CR_SUCCESS,
       "%ls needs install and was not offered to user mode\n", Id);
}

/* A null-driver device that cannot run raw stays down without a problem, also after a setup (Windows 11: "Configured" only) */
static VOID CheckNullDrivers(VOID)
{
    ULONG Status, Problem, Size = 0, Checked = 0;
    DEVINST Device, Root = 0;
    PWSTR List, Id;

    ok_once(CMP_WaitNoPendingInstallEvents(5000), (ULONG)WAIT_OBJECT_0);
    ok_once(CM_Locate_DevNodeW(&Root, NULL, CM_LOCATE_DEVNODE_NORMAL), (ULONG)CR_SUCCESS);

    if (CM_Get_Device_ID_List_SizeW(&Size, NULL, CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS || Size == 0) return;
    List = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size * sizeof(WCHAR));
    if (List == NULL) return;

    if (CM_Get_Device_ID_ListW(NULL, List, Size, CM_GETIDLIST_FILTER_PRESENT) == CR_SUCCESS)
    {
        for (Id = List; *Id != UNICODE_NULL; Id += wcslen(Id) + 1)
        {
            if (CM_Locate_DevNodeW(&Device, Id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) continue;

            CheckInstallOffered(Id, Device, Root);
            if (!IsNullDriverDevice(Device)) continue;

            ok_once(CM_Get_DevNode_Status(&Status, &Problem, Device, 0), (ULONG)CR_SUCCESS);
            ok(!(Status & (DN_STARTED | DN_HAS_PROBLEM)), "%ls: status 0x%lx, problem %lu\n", Id, Status, Problem);

            ok_once(CM_Setup_DevNode(Device, CM_SETUP_DEVNODE_READY), (ULONG)CR_SUCCESS);
            ok_once(CM_Get_DevNode_Status(&Status, &Problem, Device, 0), (ULONG)CR_SUCCESS);
            ok(!(Status & (DN_STARTED | DN_HAS_PROBLEM)), "%ls after a setup request: status 0x%lx, problem %lu\n", Id,
               Status, Problem);
            Checked++;
        }
    }

    HeapFree(GetProcessHeap(), 0, List);
    ok(Checked != 0, "No present device is configured with a null driver\n");
}

START_TEST(IoDeviceTree)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    CheckReportedHal();
#endif
    CheckRootDevices();
    CheckAcpiInstances();
    CheckBasicDisplayFallback();
    CheckFirmwareResources();
    CheckNullDrivers();
}
