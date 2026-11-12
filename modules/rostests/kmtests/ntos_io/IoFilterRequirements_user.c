/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that IRP_MN_FILTER_RESOURCE_REQUIREMENTS carries the requirements in IoStatus.Information
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/cmfuncs.h>
#include <sddl.h>
#include <winuser.h>
#include <winreg.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>

#define TEST_REPORTED_MAGIC 0x4B465251
#define TEST_DMA_CHANNEL 3
#define TEST_DMA_REQUEST_LINE 21
#define TEST_DMA_WIDTH 2
#define TEST_CONNECTION_ID_LOW 0x5A7E

static const GUID TestClass = {0x4d36e97d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const GUID ChildInterfaceGuid = { 0x5e1a6a42, 0x8c3b, 0x4b21, { 0x9c, 0x7e, 0x3f, 0x2d, 0x8a, 0x61, 0xb0, 0xc4 } };
static const WCHAR BusHardwareIds[] = L"ROOT\\KMTESTFILTERREQ\0";
static const WCHAR ChildHardwareIds[] = L"KMTEST\\FILTERREQCHILD\0";
static const WCHAR ChildInstance[] = L"KMTEST\\FILTERREQCHILD\\0";
static const WCHAR NullDeviceId[] = L"KMTEST\\FILTERREQNULL\\";
static const WCHAR ServiceKey[] = L"SYSTEM\\CurrentControlSet\\Services\\KmtestFilterReq";
static const PCWSTR Values[] = { L"FilterInformationIsList", L"FilterReportedMagic", L"FilterBusKeptList", L"StartPrivateMask",
                                 L"StartCount", L"StopSeen", L"RestartPrivateMask", L"EjectChild", L"EjectAfterRemove",
                                 L"BusRelationsQueried", L"StartDmaV3", L"StartConnection", L"BusStateQueried", L"FailBus",
                                 L"HideChild", L"ChildSurprises", L"ChildFdoRemoves", L"BusSurprised", L"BusFdoRemoves",
                                 L"ChildPdoRemoves", L"ChildPdoQueryRemoves", L"EjectMissing", L"ChildInterfaceEnabled",
                                 L"LargeMemory", L"StartLargeMemory", L"NullChild" };

static BOOL FixtureInf(PWSTR InfPath, DWORD Count)
{
    DWORD Length = GetModuleFileNameW(NULL, InfPath, Count);
    PWSTR Filename;

    ok(Length != 0 && Length < Count, "Cannot locate kmtest.exe: %lu\n", GetLastError());
    if (!Length || Length >= Count)
        return FALSE;
    Filename = wcsrchr(InfPath, L'\\');
    if (!Filename || Filename - InfPath + RTL_NUMBER_OF(L"\\kmtest_filterreq.inf") > Count)
    {
        ok(FALSE, "INF path is too long\n");
        return FALSE;
    }
    wcscpy(Filename + 1, L"kmtest_filterreq.inf");
    return TRUE;
}

static VOID ClearValues(VOID)
{
    HKEY Key;
    ULONG Index;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(Values); Index++)
        RegDeleteValueW(Key, Values[Index]);
    RegCloseKey(Key);
}

static BOOL ReadValue(PCWSTR Name, PDWORD Value)
{
    DWORD Size = sizeof(*Value), Type = 0;
    HKEY Key;
    LONG Error;

    *Value = MAXDWORD;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return FALSE;
    Error = RegQueryValueExW(Key, Name, NULL, &Type, (BYTE *)Value, &Size);
    RegCloseKey(Key);
    return Error == ERROR_SUCCESS && Type == REG_DWORD;
}

static BOOL ChildStarted(DEVINST Bus, DWORD Timeout)
{
    ULONG Status = 0, Problem = 0;
    DWORD Start = GetTickCount();
    DEVINST Child;

    do
    {
        if (CM_Get_Child(&Child, Bus, 0) == CR_SUCCESS && CM_Get_DevNode_Status(&Status, &Problem, Child, 0) == CR_SUCCESS &&
            (Status & DN_STARTED))
        {
            return TRUE;
        }
        Sleep(10);
    } while (GetTickCount() - Start < Timeout);

    return FALSE;
}

/* The path of the child's interface while the child is present */
static BOOL ChildInterface(PWSTR Path, ULONG Length)
{
    return CM_Get_Device_Interface_ListW((LPGUID)&ChildInterfaceGuid, NULL, Path, Length, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) ==
               CR_SUCCESS &&
           *Path != UNICODE_NULL;
}

static DWORD OpenInterface(PCWSTR Path)
{
    HANDLE File = CreateFileW(Path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (File == INVALID_HANDLE_VALUE)
        return GetLastError();
    CloseHandle(File);
    return ERROR_SUCCESS;
}

static BOOL InstallChild(PCWSTR InfPath)
{
    BOOL Reboot = FALSE;
    ULONG Attempt;

    for (Attempt = 0; Attempt < 20; Attempt++)
    {
        if (UpdateDriverForPlugAndPlayDevicesW(NULL, ChildHardwareIds, InfPath, INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot))
            return TRUE;
        if (GetLastError() != ERROR_NO_SUCH_DEVINST)
            break;
        Sleep(250);
    }
    trace("Installing the child returned error %lu\n", GetLastError());
    return FALSE;
}

static VOID CheckRecorded(VOID)
{
    DWORD Value;

    ok(ReadValue(L"FilterInformationIsList", &Value), "The filter request never reached the function driver\n");
    ok(Value == 1, "IoStatus.Information did not point to the requirements list (%lu)\n", Value);
    ok(ReadValue(L"FilterReportedMagic", &Value), "The function driver did not record the supplied list\n");
    ok(Value == TEST_REPORTED_MAGIC, "The supplied list was not the bus driver's requirements (0x%lx)\n", Value);
    ok(ReadValue(L"FilterBusKeptList", &Value), "The filter request did not come back up\n");
    ok(Value == 1, "The bus driver did not leave IoStatus.Information as it was (%lu)\n", Value);
    ok(ReadValue(L"StartPrivateMask", &Value), "The child device did not start\n");
    ok(Value == 3, "The start resources were not the filtered requirements (mask %lu)\n", Value);
    ok(ReadValue(L"BusStateQueried", &Value) && Value != 0, "The bus was not asked for its state after its start\n");
    ok(ReadValue(L"StartDmaV3", &Value) && Value == (TEST_DMA_CHANNEL | (TEST_DMA_REQUEST_LINE << 8) | (TEST_DMA_WIDTH << 16)),
       "The start resources lost the version 3 DMA channel, request line or width (0x%lx)\n", Value);
    ok(ReadValue(L"StartConnection", &Value) &&
       Value == (CM_RESOURCE_CONNECTION_CLASS_SERIAL | (CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C << 8) | (TEST_CONNECTION_ID_LOW << 16)),
       "The start resources lost the connection class, type or ID (0x%lx)\n", Value);
}

/* The function driver reports new requirements after its first start; the wait only bounds a lost restart */
static VOID CheckRestart(VOID)
{
    DWORD Start = GetTickCount();
    DWORD Value = 0;

    while ((!ReadValue(L"StartCount", &Value) || Value < 2) && GetTickCount() - Start < 1000)
        Sleep(10);

    ok(Value == 2, "The device was not started again after its requirements changed (%lu starts)\n", Value);
    ok(!ReadValue(L"StopSeen", &Value), "The device was stopped for a change of its requirements\n");
    ok(ReadValue(L"RestartPrivateMask", &Value) && Value == 3, "The restart did not carry the device's resources (mask %lu)\n", Value);
}

/* A user-mode reenumeration asks the bus driver for its children again */
static VOID CheckReenumerate(DEVINST Bus)
{
    DWORD Queried = 0, Value = 0;

    ok(ReadValue(L"BusRelationsQueried", &Queried), "The bus driver was never asked for its children\n");
    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);
    ok(ReadValue(L"BusRelationsQueried", &Value) && Value > Queried, "Reenumerating the bus did not ask it for its children\n");
}

static VOID SetRequest(PCWSTR Name, DWORD Value)
{
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
        return;
    RegSetValueExW(Key, Name, 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
    RegCloseKey(Key);
}

static HANDLE OpenDevice(DEVINST Device, DWORD Access)
{
    WCHAR Path[MAX_PATH] = L"\\\\?\\GLOBALROOT";
    ULONG Size = sizeof(Path) - sizeof(L"\\\\?\\GLOBALROOT");
    HANDLE File;

    if (CM_Get_DevNode_Registry_PropertyW(Device, CM_DRP_PHYSICAL_DEVICE_OBJECT_NAME, NULL, Path + wcslen(Path), &Size, 0) !=
        CR_SUCCESS)
    {
        ok(FALSE, "The device has no PDO name\n");
        return INVALID_HANDLE_VALUE;
    }

    File = CreateFileW(Path, Access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Opening %ls failed: %lu\n", Path, GetLastError());
    return File;
}

/* A child its bus stops reporting is surprise-removed, gets its remove request after its last handle closes
 * and leaves the tree; the waits only bound a lost request */
static VOID CheckSurpriseRemoval(DEVINST Bus)
{
    PLUGPLAY_CONTROL_DEVICE_CONTROL_DATA Reset;
    WCHAR Instance[MAX_DEVICE_ID_LEN];
    ULONG Status = 0, Problem = 0;
    DWORD Start, Value = 0, Returned;
    DEVINST Child;
    HANDLE File;

    if (CM_Get_Child(&Child, Bus, 0) != CR_SUCCESS ||
        CM_Get_Device_IDW(Child, Instance, RTL_NUMBER_OF(Instance), 0) != CR_SUCCESS)
    {
        ok(FALSE, "The bus has no child to remove\n");
        return;
    }

    File = OpenDevice(Child, 0);
    if (File == INVALID_HANDLE_VALUE)
        return;

    SetRequest(L"HideChild", 1);
    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);

    ok(ReadValue(L"ChildSurprises", &Value) && Value == 1, "The missing child was not surprise-removed\n");

    /* While it waits for its last close, the stack takes no state query, eject or reset, and a stream file of its
     * driver is no close of its own */
    SetRequest(L"EjectMissing", 1);
    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);
    SetRequest(L"EjectMissing", 0);
    RtlInitUnicodeString(&Reset.DeviceInstance, Instance);
    NtPlugPlayControl(PlugPlayControlResetDevice, &Reset, sizeof(Reset));
    ok(DeviceIoControl(File, CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_NEITHER, FILE_ANY_ACCESS), NULL, 0, NULL, 0,
                       &Returned, NULL),
       "The child's driver opened no stream file: %lu\n", GetLastError());
    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);

    ok(ReadValue(L"ChildSurprises", &Value) && Value == 1, "The missing child was surprise-removed again (%lu)\n", Value);
    ok(!ReadValue(L"ChildFdoRemoves", &Value), "The missing child was removed while a handle was open\n");

    CloseHandle(File);

    Start = GetTickCount();
    while (!ReadValue(L"ChildFdoRemoves", &Value) && GetTickCount() - Start < 1000)
        Sleep(10);
    ok(ReadValue(L"ChildFdoRemoves", &Value) && Value == 1, "The missing child was not removed after its last close\n");

    Start = GetTickCount();
    while (CM_Get_Child(&Child, Bus, 0) == CR_SUCCESS && GetTickCount() - Start < 1000)
        Sleep(10);
    ok(CM_Get_Child(&Child, Bus, 0) != CR_SUCCESS, "The missing child is still in the device tree\n");

    SetRequest(L"HideChild", 0);
    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);

    Start = GetTickCount();
    while ((CM_Get_Child(&Child, Bus, 0) != CR_SUCCESS ||
            CM_Get_DevNode_Status(&Status, &Problem, Child, 0) != CR_SUCCESS || !(Status & DN_STARTED)) &&
           GetTickCount() - Start < 1000)
    {
        Sleep(10);
    }
    ok(Status & DN_STARTED, "The child did not come back when its bus reported it again (status 0x%lx)\n", Status);
}

/* An uninstalled child its bus still reports is set up and installed again when user mode rescans the devices,
 * as Windows 11 does; the wait only bounds the installation */
static VOID CheckReinstall(DEVINST Bus, PCWSTR InfPath)
{
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    WCHAR Instance[MAX_DEVICE_ID_LEN];
    WCHAR HardwareIds[64];
    ULONG Size = sizeof(HardwareIds);
    HDEVINFO Devices;
    DEVINST Child, Root;
    BOOL Removed = FALSE;

    if (CM_Get_Child(&Child, Bus, 0) != CR_SUCCESS ||
        CM_Get_Device_IDW(Child, Instance, RTL_NUMBER_OF(Instance), 0) != CR_SUCCESS)
    {
        ok(FALSE, "The bus has no child to uninstall\n");
        return;
    }

    Devices = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (Devices != INVALID_HANDLE_VALUE && SetupDiOpenDeviceInfoW(Devices, Instance, NULL, 0, &Device))
        Removed = SetupDiCallClassInstaller(DIF_REMOVE, Devices, &Device);
    if (Devices != INVALID_HANDLE_VALUE)
        SetupDiDestroyDeviceInfoList(Devices);
    ok(Removed, "Uninstalling %ls failed: %lu\n", Instance, GetLastError());
    if (!Removed)
        return;

    ok_eq_ulong(CM_Locate_DevNodeW(&Root, NULL, CM_LOCATE_DEVNODE_NORMAL), (ULONG)CR_SUCCESS);
    ok_eq_ulong(CM_Reenumerate_DevNode(Root, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);

    ok(CM_Get_Child(&Child, Bus, 0) == CR_SUCCESS &&
       CM_Get_DevNode_Registry_PropertyW(Child, CM_DRP_HARDWAREID, NULL, HardwareIds, &Size, 0) == CR_SUCCESS &&
       !wcscmp(HardwareIds, L"KMTEST\\FILTERREQCHILD"),
       "The uninstalled child was not set up again on a rescan\n");

    ok(ChildStarted(Bus, 1000), "The uninstalled child was not installed again on a rescan\n");
    if (!ChildStarted(Bus, 0))
        ok(InstallChild(InfPath), "Installing the child again failed\n");
}

/* A child plugged in again while its old stack still has an open handle starts at once under its instance ID, as
 * Windows 11 does; the old stack gets its remove request after the close. The waits only bound a lost request */
static VOID CheckReplug(DEVINST Bus)
{
    WCHAR Before[MAX_DEVICE_ID_LEN] = L"", After[MAX_DEVICE_ID_LEN] = L"";
    WCHAR Interface[2 * MAX_PATH] = L"";
    DWORD Start, Removes = 0, Value = 0;
    DEVINST Child;
    HANDLE File;

    if (CM_Get_Child(&Child, Bus, 0) != CR_SUCCESS ||
        CM_Get_Device_IDW(Child, Before, RTL_NUMBER_OF(Before), 0) != CR_SUCCESS)
    {
        ok(FALSE, "The bus has no child to plug in again\n");
        return;
    }
    if (!ReadValue(L"ChildFdoRemoves", &Removes))
        Removes = 0;

    File = OpenDevice(Child, 0);
    if (File == INVALID_HANDLE_VALUE)
        return;

    SetRequest(L"HideChild", 1);
    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);
    SetRequest(L"HideChild", 0);
    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);

    ok(ChildStarted(Bus, 1000), "The child plugged in again did not start while the old stack was open\n");
    ok(CM_Get_Child(&Child, Bus, 0) == CR_SUCCESS &&
       CM_Get_Device_IDW(Child, After, RTL_NUMBER_OF(After), 0) == CR_SUCCESS && !wcscmp(Before, After),
       "The child plugged in again is %ls, not %ls\n", After, Before);
    ok(!ReadValue(L"ChildFdoRemoves", &Value) || Value == Removes, "The old stack was removed while a handle was open\n");
    ok(ChildInterface(Interface, RTL_NUMBER_OF(Interface)), "The child plugged in again has no present interface\n");
    ok_eq_ulong(OpenInterface(Interface), (ULONG)ERROR_SUCCESS);

    CloseHandle(File);

    Start = GetTickCount();
    while ((!ReadValue(L"ChildFdoRemoves", &Value) || Value == Removes) && GetTickCount() - Start < 1000)
        Sleep(10);
    ok(Value == Removes + 1, "The old stack was not removed after its last close (%lu removals)\n", Value);
    ok(ChildStarted(Bus, 0), "The child plugged in again stopped when the old stack went\n");
}

/* Removing a child that is removed already asks its stack nothing and sends it no second remove request; set up
 * again, it starts */
static VOID CheckRemoveTwice(DEVINST Bus)
{
    WCHAR Interface[2 * MAX_PATH] = L"";
    WCHAR VetoName[MAX_PATH];
    PNP_VETO_TYPE VetoType;
    DWORD Removes = 0, Queries = 0, Value = 0;
    DEVINST Child;

    if (CM_Get_Child(&Child, Bus, 0) != CR_SUCCESS)
    {
        ok(FALSE, "The bus has no child to remove\n");
        return;
    }

    ok(ChildInterface(Interface, RTL_NUMBER_OF(Interface)), "The child has no present interface\n");
    ok_eq_ulong(OpenInterface(Interface), (ULONG)ERROR_SUCCESS);

    ok_eq_ulong(CM_Query_And_Remove_SubTreeW(Child, &VetoType, VetoName, RTL_NUMBER_OF(VetoName), CM_REMOVE_UI_NOT_OK),
                (ULONG)CR_SUCCESS);
    ok(ReadValue(L"ChildPdoRemoves", &Removes), "The child got no remove request\n");
    /* The PnP manager retires the interface its driver left enabled, link included (Windows 11) */
    ok_eq_ulong(OpenInterface(Interface), (ULONG)ERROR_FILE_NOT_FOUND);
    ok(ReadValue(L"ChildPdoQueryRemoves", &Queries), "The child was not asked whether it can be removed\n");

    CM_Query_And_Remove_SubTreeW(Child, &VetoType, VetoName, RTL_NUMBER_OF(VetoName), CM_REMOVE_UI_NOT_OK);
    ok(ReadValue(L"ChildPdoRemoves", &Value) && Value == Removes, "The removed child got a second remove request\n");
    ok(ReadValue(L"ChildPdoQueryRemoves", &Value) && Value == Queries, "The removed child was asked again\n");

    ok_eq_ulong(CM_Setup_DevNode(Child, CM_SETUP_DEVNODE_READY), (ULONG)CR_SUCCESS);
    ok(ChildStarted(Bus, 1000), "The removed child did not start again\n");
    ok_eq_ulong(OpenInterface(Interface), (ULONG)ERROR_SUCCESS);
}

/* Sets the child's security property from an SDDL string, restarts the child and opens it for reading */
static DWORD SecureChild(DEVINST Bus, PCWSTR Sddl)
{
    WCHAR Path[MAX_PATH] = L"\\\\?\\GLOBALROOT";
    ULONG Length = sizeof(Path) - sizeof(L"\\\\?\\GLOBALROOT");
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    WCHAR VetoName[MAX_PATH];
    PNP_VETO_TYPE VetoType;
    DEVINST Child;
    HANDLE File;
    ULONG Size;
    DWORD Error;

    if (CM_Get_Child(&Child, Bus, 0) != CR_SUCCESS ||
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(Sddl, SDDL_REVISION_1, &Descriptor, &Size))
    {
        return ERROR_INVALID_DATA;
    }
    ok_eq_ulong(CM_Set_DevNode_Registry_PropertyW(Child, CM_DRP_SECURITY, Descriptor, Size, 0), (ULONG)CR_SUCCESS);
    LocalFree(Descriptor);

    ok_eq_ulong(CM_Query_And_Remove_SubTreeW(Child, &VetoType, VetoName, RTL_NUMBER_OF(VetoName), CM_REMOVE_UI_NOT_OK),
                (ULONG)CR_SUCCESS);
    ok_eq_ulong(CM_Setup_DevNode(Child, CM_SETUP_DEVNODE_READY), (ULONG)CR_SUCCESS);
    ok(ChildStarted(Bus, 1000), "The secured child did not start again\n");

    if (CM_Get_DevNode_Registry_PropertyW(Child, CM_DRP_PHYSICAL_DEVICE_OBJECT_NAME, NULL, Path + wcslen(Path), &Length, 0) !=
        CR_SUCCESS)
    {
        return ERROR_INVALID_DATA;
    }

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    Error = (File == INVALID_HANDLE_VALUE) ? GetLastError() : ERROR_SUCCESS;
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
    return Error;
}

/* The security descriptor a device's registry holds covers its stack once its drivers are added (SPDRP_SECURITY) */
static VOID CheckDeviceSecurity(DEVINST Bus)
{
    ok_eq_ulong(SecureChild(Bus, L"D:(D;;FA;;;SY)(A;;FA;;;WD)"), (ULONG)ERROR_ACCESS_DENIED);
    ok_eq_ulong(SecureChild(Bus, L"D:(A;;FA;;;WD)"), (ULONG)ERROR_SUCCESS);
}

static VOID RestartChild(DEVINST Bus)
{
    WCHAR VetoName[MAX_PATH];
    PNP_VETO_TYPE VetoType;
    DEVINST Child;

    if (CM_Get_Child(&Child, Bus, 0) != CR_SUCCESS)
    {
        ok(FALSE, "The bus has no child to restart\n");
        return;
    }
    ok_eq_ulong(CM_Query_And_Remove_SubTreeW(Child, &VetoType, VetoName, RTL_NUMBER_OF(VetoName), CM_REMOVE_UI_NOT_OK),
                (ULONG)CR_SUCCESS);
    ok_eq_ulong(CM_Setup_DevNode(Child, CM_SETUP_DEVNODE_READY), (ULONG)CR_SUCCESS);
    ok(ChildStarted(Bus, 1000), "The child did not start again\n");
}

/* A memory window of 4 GB or more is assigned whole, with its scaled length (CM_RESOURCE_MEMORY_LARGE): here
 * 8 GB at its 64 GB minimum */
static VOID CheckLargeMemory(DEVINST Bus)
{
    DWORD Value = 0;

    SetRequest(L"LargeMemory", 1);
    RestartChild(Bus);
    ok(ReadValue(L"StartLargeMemory", &Value) && Value == (8 | (64 << 16)),
       "The child got no 8 GB window at 64 GB (0x%lx)\n", Value);

    SetRequest(L"LargeMemory", 0);
    RestartChild(Bus);
}

/* The bus driver asks for its child's eject at the next enumeration; the wait only bounds a lost eject */
static VOID CheckEject(DEVINST Bus)
{
    DWORD Start, Value = 1;
    ULONG Status = 0, Problem = 0;
    DEVINST Child;
    HKEY Key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
    {
        ok(FALSE, "The fixture's service key is missing\n");
        return;
    }
    RegSetValueExW(Key, L"EjectChild", 0, REG_DWORD, (const BYTE *)&Value, sizeof(Value));
    RegCloseKey(Key);

    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);

    Start = GetTickCount();
    while (!ReadValue(L"EjectAfterRemove", &Value) && GetTickCount() - Start < 1000)
        Sleep(10);

    ok(ReadValue(L"EjectAfterRemove", &Value), "The child was not ejected\n");
    ok(Value == 1, "The child was ejected before it was removed\n");

    /* A removal hands user mode nothing to install, so the ejected child stays down */
    ok_eq_ulong(CMP_WaitNoPendingInstallEvents(1000), (ULONG)WAIT_OBJECT_0);

    if (CM_Get_Child(&Child, Bus, 0) != CR_SUCCESS)
        return;
    ok_eq_ulong(CM_Get_DevNode_Status(&Status, &Problem, Child, 0), (ULONG)CR_SUCCESS);
    ok(Problem == CM_PROB_HELD_FOR_EJECT, "The ejected child has problem %lu (status 0x%lx)\n", Problem, Status);
}

/* The bus reports itself failed at the next enumeration: it is surprise-removed and gets its remove request
 * after its last handle closes; the waits only bound a lost report */
static VOID CheckFailed(DEVINST Bus)
{
    DWORD Start, Value = 0;
    ULONG Status = 0, Problem = 0;
    HANDLE File;

    File = OpenDevice(Bus, 0);
    if (File == INVALID_HANDLE_VALUE)
        return;

    SetRequest(L"FailBus", 1);
    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);

    Start = GetTickCount();
    while ((CM_Get_DevNode_Status(&Status, &Problem, Bus, 0) != CR_SUCCESS || Problem != CM_PROB_FAILED_POST_START) &&
           GetTickCount() - Start < 1000)
    {
        Sleep(10);
    }

    ok(Problem == CM_PROB_FAILED_POST_START, "The failed bus has problem %lu (status 0x%lx)\n", Problem, Status);
    ok(ReadValue(L"BusSurprised", &Value) && Value == 1, "The failed bus was not surprise-removed\n");
    ok(!ReadValue(L"BusFdoRemoves", &Value), "The failed bus was removed while a handle was open\n");

    CloseHandle(File);

    Start = GetTickCount();
    while (!ReadValue(L"BusFdoRemoves", &Value) && GetTickCount() - Start < 1000)
        Sleep(10);
    ok(ReadValue(L"BusFdoRemoves", &Value) && Value == 1, "The failed bus was not removed after its last close\n");
}

static VOID RemoveInstance(PCWSTR Instance)
{
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    HDEVINFO Devices = SetupDiCreateDeviceInfoList(NULL, NULL);

    if (Devices == INVALID_HANDLE_VALUE)
        return;
    if (SetupDiOpenDeviceInfoW(Devices, Instance, NULL, 0, &Device))
        SetupDiCallClassInstaller(DIF_REMOVE, Devices, &Device);
    SetupDiDestroyDeviceInfoList(Devices);
}

static BOOL FindNullChild(DEVINST Bus, PDEVINST Child, PWSTR Instance, ULONG Length)
{
    CONFIGRET Result;

    for (Result = CM_Get_Child(Child, Bus, 0); Result == CR_SUCCESS; Result = CM_Get_Sibling(Child, *Child, 0))
    {
        if (CM_Get_Device_IDW(*Child, Instance, Length, 0) == CR_SUCCESS &&
            !_wcsnicmp(Instance, NullDeviceId, wcslen(NullDeviceId)))
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* A reported child whose staged driver is a null one is installed by user mode and left down without a problem */
static VOID CheckNullChild(DEVINST Bus)
{
    WCHAR Driver[MAX_PATH], Instance[MAX_DEVICE_ID_LEN] = L"";
    ULONG Status = 0, Problem = 0, Size;
    BOOL Configured = FALSE;
    DWORD Start;
    DEVINST Child;

    SetRequest(L"NullChild", 1);
    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);

    Start = GetTickCount();
    do
    {
        Size = sizeof(Driver);
        Configured = FindNullChild(Bus, &Child, Instance, RTL_NUMBER_OF(Instance)) &&
                     CM_Get_DevNode_Status(&Status, &Problem, Child, 0) == CR_SUCCESS && !(Status & DN_HAS_PROBLEM) &&
                     CM_Get_DevNode_Registry_PropertyW(Child, CM_DRP_DRIVER, NULL, Driver, &Size, 0) == CR_SUCCESS;
        if (!Configured)
            Sleep(10);
    } while (!Configured && GetTickCount() - Start < 3000);

    ok(Instance[0] != UNICODE_NULL, "The bus did not report the null driver child\n");
    ok(Configured, "%ls was not installed (status 0x%lx, problem %lu)\n", Instance, Status, Problem);
    ok(!(Status & DN_STARTED), "%ls was started (status 0x%lx)\n", Instance, Status);

    SetRequest(L"NullChild", 0);
    ok_eq_ulong(CM_Reenumerate_DevNode(Bus, CM_REENUMERATE_SYNCHRONOUS), (ULONG)CR_SUCCESS);
    if (Instance[0] != UNICODE_NULL)
        RemoveInstance(Instance);
}

static VOID RunFixture(HDEVINFO Devices, PSP_DEVINFO_DATA Device, PCWSTR InfPath)
{
    BOOL Success, Staged, Reboot = FALSE;
    WCHAR OemInf[MAX_PATH];
    PWSTR OemName = NULL;

    /* The child's driver package is only staged: the child is installed when its bus reports it */
    Staged = SetupCopyOEMInfW(InfPath, NULL, SPOST_PATH, 0, OemInf, RTL_NUMBER_OF(OemInf), NULL, &OemName);
    ok(Staged, "Staging the fixture INF failed: %lu\n", GetLastError());

    Success = SetupDiSetDeviceRegistryPropertyW(Devices, Device, SPDRP_HARDWAREID, (const BYTE *)BusHardwareIds, sizeof(BusHardwareIds));
    ok(Success, "Setting hardware ID failed: %lu\n", GetLastError());
    if (!Success)
        return;
    Success = SetupDiCallClassInstaller(DIF_REGISTERDEVICE, Devices, Device);
    ok(Success, "Registering test devnode failed: %lu\n", GetLastError());
    if (!Success)
        return;

    Success = UpdateDriverForPlugAndPlayDevicesW(NULL, BusHardwareIds, InfPath, INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot);
    ok(Success, "Installing the bus fixture failed: %lu\n", GetLastError());
    if (Success)
    {
        ok(ChildStarted(Device->DevInst, 5000), "The child the bus reported was not installed and started\n");
        if (!ChildStarted(Device->DevInst, 0))
            ok(InstallChild(InfPath), "Installing the child fixture failed\n");
        CheckRecorded();
        CheckRestart();
        CheckReenumerate(Device->DevInst);
        CheckSurpriseRemoval(Device->DevInst);
        CheckReinstall(Device->DevInst, InfPath);
        CheckReplug(Device->DevInst);
        CheckRemoveTwice(Device->DevInst);
        CheckDeviceSecurity(Device->DevInst);
        CheckLargeMemory(Device->DevInst);
        CheckNullChild(Device->DevInst);
        CheckEject(Device->DevInst);
        CheckFailed(Device->DevInst);
    }

    Success = SetupDiCallClassInstaller(DIF_REMOVE, Devices, Device);
    ok(Success, "Removing the test devnode failed: %lu\n", GetLastError());
    RemoveInstance(ChildInstance);

    if (Staged)
        ok(SetupUninstallOEMInfW(OemName, SUOI_FORCEDELETE, NULL), "Removing the staged INF failed: %lu\n", GetLastError());
}

START_TEST(IoFilterRequirements)
{
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    WCHAR InfPath[MAX_PATH];
    HDEVINFO Devices;
    BOOL Success;

    if (!FixtureInf(InfPath, RTL_NUMBER_OF(InfPath)))
        return;
    ClearValues();

    Devices = SetupDiCreateDeviceInfoList(&TestClass, NULL);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE)
        return;
    Success = SetupDiCreateDeviceInfoW(Devices, L"KmtestFilterReq", &TestClass, L"Kmtest filter requirements bus", NULL, DICD_GENERATE_ID, &Device);
    ok(Success, "SetupDiCreateDeviceInfo failed: %lu\n", GetLastError());
    if (Success)
        RunFixture(Devices, &Device, InfPath);
    Success = SetupDiDestroyDeviceInfoList(Devices);
    ok(Success, "SetupDiDestroyDeviceInfoList failed: %lu\n", GetLastError());
}
