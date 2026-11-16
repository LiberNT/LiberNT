/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that an INF AddComponent makes a software device under its device, whose package CopyINF imported and whose AddSoftware runs from the driver store
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <devguid.h>

#define PARENT_HWID L"LNT_SWC_TEST_PARENT"
#define SOFTWARE_RESULT_KEY L"SOFTWARE\\LiberNTApiTest\\SoftwareComponent"

static const char ParentInf[] =
    "[Version]\r\n"
    "Signature=\"$WINDOWS NT$\"\r\n"
    "Class=System\r\n"
    "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\n"
    "Provider=%Lnt%\r\n"
    "DriverVer=11/15/2026,1.0.0.0\r\n"
    "CatalogFile=lntswparent.cat\r\n"
    "\r\n"
    "[Manufacturer]\r\n"
    "%Lnt%=Lnt,NTamd64,NTarm64,NTx86\r\n"
    "\r\n"
    "[Lnt.NTamd64]\r\n"
    "%Parent%=ParentInstall,LNT_SWC_TEST_PARENT\r\n"
    "[Lnt.NTarm64]\r\n"
    "%Parent%=ParentInstall,LNT_SWC_TEST_PARENT\r\n"
    "[Lnt.NTx86]\r\n"
    "%Parent%=ParentInstall,LNT_SWC_TEST_PARENT\r\n"
    "\r\n"
    "[ParentInstall.NT]\r\n"
    "Include=umpass.inf\r\n"
    "Needs=UmPass\r\n"
    "CopyINF=.\\comp\\lntswcomp.inf\r\n"
    "\r\n"
    "[ParentInstall.NT.HW]\r\n"
    "Include=umpass.inf\r\n"
    "Needs=UmPass.HW\r\n"
    "\r\n"
    "[ParentInstall.NT.Interfaces]\r\n"
    "Include=umpass.inf\r\n"
    "Needs=UmPass.Interfaces\r\n"
    "\r\n"
    "[ParentInstall.NT.Services]\r\n"
    "Include=umpass.inf\r\n"
    "Needs=UmPass.Services\r\n"
    "\r\n"
    "[ParentInstall.NT.Components]\r\n"
    "AddComponent=LntTestComp,,CompInstall\r\n"
    "\r\n"
    "[CompInstall]\r\n"
    "ComponentIDs=LNT_SWC_TEST_COMPONENT\r\n"
    "Description=%CompDesc%\r\n"
    "\r\n"
    "[Strings]\r\n"
    "Lnt=\"LiberNT tests\"\r\n"
    "Parent=\"LiberNT component parent\"\r\n"
    "CompDesc=\"LiberNT test component\"\r\n";

static const char ComponentInf[] =
    "[Version]\r\n"
    "Signature=\"$WINDOWS NT$\"\r\n"
    "Class=SoftwareComponent\r\n"
    "ClassGuid={5c4c3332-344d-483c-8739-259e934c9cc8}\r\n"
    "Provider=%Lnt%\r\n"
    "DriverVer=11/15/2026,1.0.0.0\r\n"
    "CatalogFile=lntswcomp.cat\r\n"
    "\r\n"
    "[Manufacturer]\r\n"
    "%Lnt%=Lnt,NTamd64,NTarm64,NTx86\r\n"
    "\r\n"
    "[Lnt.NTamd64]\r\n"
    "%Comp%=CompInstall,SWC\\LNT_SWC_TEST_COMPONENT\r\n"
    "[Lnt.NTarm64]\r\n"
    "%Comp%=CompInstall,SWC\\LNT_SWC_TEST_COMPONENT\r\n"
    "[Lnt.NTx86]\r\n"
    "%Comp%=CompInstall,SWC\\LNT_SWC_TEST_COMPONENT\r\n"
    "\r\n"
    "[CompInstall.NT]\r\n"
    "CopyFiles=CompFiles\r\n"
    "\r\n"
    "[CompInstall.NT.Services]\r\n"
    "AddService=,2\r\n"
    "\r\n"
    "[CompInstall.NT.Software]\r\n"
    "AddSoftware=LntTestSoftware,,CompSoftware\r\n"
    "\r\n"
    "[CompSoftware]\r\n"
    "SoftwareType=1\r\n"
    "SoftwareBinary=lntswreg.exe\r\n"
    "SoftwareArguments=add,HKLM\\SOFTWARE\\LiberNTApiTest\\SoftwareComponent,/v,Instance,/d,<<DeviceInstanceID>>,/f\r\n"
    "SoftwareVersion=1.0.0.0\r\n"
    "\r\n"
    "[CompFiles]\r\n"
    "lntswreg.exe\r\n"
    "\r\n"
    "[DestinationDirs]\r\n"
    "CompFiles=13\r\n"
    "\r\n"
    "[SourceDisksNames]\r\n"
    "1=%Disk%\r\n"
    "\r\n"
    "[SourceDisksFiles]\r\n"
    "lntswreg.exe=1\r\n"
    "\r\n"
    "[Strings]\r\n"
    "Lnt=\"LiberNT tests\"\r\n"
    "Comp=\"LiberNT test component driver\"\r\n"
    "Disk=\"LiberNT test disk\"\r\n";

static BOOL
WriteTextFile(PCWSTR Path, const char *Text)
{
    HANDLE File;
    DWORD Written = 0;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, Text, (DWORD)strlen(Text), &Written, NULL) && Written == strlen(Text);
    CloseHandle(File);
    return Result;
}

static BOOL
CreateParentDevice(HDEVINFO Set, SP_DEVINFO_DATA *Data)
{
    static const WCHAR HardwareId[] = PARENT_HWID L"\0";

    Data->cbSize = sizeof(*Data);
    if (!SetupDiCreateDeviceInfoW(Set, L"LNTSWCTEST", &GUID_DEVCLASS_SYSTEM, NULL, NULL, DICD_GENERATE_ID, Data))
        return FALSE;
    if (!SetupDiSetDeviceRegistryPropertyW(Set, Data, SPDRP_HARDWAREID, (const BYTE *)HardwareId, sizeof(HardwareId)))
        return FALSE;
    return SetupDiCallClassInstaller(DIF_REGISTERDEVICE, Set, Data);
}

static BOOL
GetMultiSz(DEVINST Device, ULONG Property, WCHAR *Buffer, ULONG Size)
{
    ULONG Length = Size;

    ZeroMemory(Buffer, Size);
    return CM_Get_DevNode_Registry_PropertyW(Device, Property, NULL, Buffer, &Length, 0) == CR_SUCCESS;
}

static BOOL
EqualMultiSz(const WCHAR *Left, const WCHAR *Right)
{
    while (*Left && *Right)
    {
        if (wcscmp(Left, Right))
            return FALSE;
        Left += wcslen(Left) + 1;
        Right += wcslen(Right) + 1;
    }
    return !*Left && !*Right;
}

/* Windows 11 records the component under Devices of its parent's instance key */
static VOID
CheckComponentRecord(PCWSTR ParentId)
{
    WCHAR Path[MAX_PATH], Buffer[256];
    DWORD Size, Type;
    HKEY Key;
    LONG Error;

    wsprintfW(Path, L"System\\CurrentControlSet\\Enum\\%s\\Devices\\LntTestComp", ParentId);
    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_READ, &Key);
    ok(Error == ERROR_SUCCESS, "RegOpenKeyExW error %ld for %ls\n", Error, Path);
    if (Error != ERROR_SUCCESS)
        return;

    Size = sizeof(Buffer);
    ZeroMemory(Buffer, sizeof(Buffer));
    Error = RegQueryValueExW(Key, L"HardwareIds", NULL, &Type, (BYTE *)Buffer, &Size);
    ok(Error == ERROR_SUCCESS && Type == REG_MULTI_SZ, "HardwareIds: error %ld type %lu\n", Error, Type);
    ok(EqualMultiSz(Buffer, L"SWC\\LNT_SWC_TEST_COMPONENT\0"), "HardwareIds is '%ls'\n", Buffer);

    Size = sizeof(Buffer);
    ZeroMemory(Buffer, sizeof(Buffer));
    Error = RegQueryValueExW(Key, L"CompatibleIds", NULL, &Type, (BYTE *)Buffer, &Size);
    ok(Error == ERROR_SUCCESS && Type == REG_MULTI_SZ, "CompatibleIds: error %ld type %lu\n", Error, Type);
    ok(EqualMultiSz(Buffer, L"SWC\\Generic\0"), "CompatibleIds is '%ls'\n", Buffer);

    Size = sizeof(Buffer);
    ZeroMemory(Buffer, sizeof(Buffer));
    Error = RegQueryValueExW(Key, L"Description", NULL, &Type, (BYTE *)Buffer, &Size);
    ok(Error == ERROR_SUCCESS && Type == REG_SZ, "Description: error %ld type %lu\n", Error, Type);
    ok(!wcscmp(Buffer, L"LiberNT test component"), "Description is '%ls'\n", Buffer);

    RegCloseKey(Key);
}

static VOID
CheckComponentDevice(DEVINST Parent, PCWSTR ParentId, DEVINST *Component)
{
    WCHAR Id[MAX_DEVICE_ID_LEN], Buffer[256], Prefix[64];
    ULONG Status = 0, Problem = 0, Capabilities = 0, Length;
    DWORD Size = sizeof(Prefix), Type;
    WCHAR Path[MAX_PATH];
    CONFIGRET Result;
    HKEY Key;

    *Component = 0;

    Result = CM_Get_Child(Component, Parent, 0);
    ok(Result == CR_SUCCESS, "The parent has no child: cr %lu\n", Result);
    if (Result != CR_SUCCESS)
        return;

    Result = CM_Get_Device_IDW(*Component, Id, ARRAYSIZE(Id), 0);
    ok(Result == CR_SUCCESS, "CM_Get_Device_IDW failed: cr %lu\n", Result);
    ok(!_wcsnicmp(Id, L"SWD\\DRIVERENUM\\LntTestComp&", 27), "The component device is '%ls'\n", Id);

    /* The instance ends with the prefix the parent gives its children, where the parent records one */
    wsprintfW(Path, L"System\\CurrentControlSet\\Enum\\%s", ParentId);
    ZeroMemory(Prefix, sizeof(Prefix));
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_READ, &Key) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(Key, L"ParentIdPrefix", NULL, &Type, (BYTE *)Prefix, &Size) == ERROR_SUCCESS)
            ok(!_wcsicmp(Id + 27, Prefix), "Instance '%ls' does not end with the prefix '%ls'\n", Id, Prefix);
        RegCloseKey(Key);
    }

    ok(GetMultiSz(*Component, CM_DRP_HARDWAREID, Buffer, sizeof(Buffer)), "No hardware IDs\n");
    ok(EqualMultiSz(Buffer, L"SWC\\LNT_SWC_TEST_COMPONENT\0"), "Hardware IDs are '%ls'\n", Buffer);

    ok(GetMultiSz(*Component, CM_DRP_COMPATIBLEIDS, Buffer, sizeof(Buffer)), "No compatible IDs\n");
    ok(EqualMultiSz(Buffer, L"SWC\\Generic\0SWD\\GenericRaw\0SWD\\Generic\0"), "Compatible IDs are '%ls'\n", Buffer);

    Length = sizeof(Capabilities);
    Result = CM_Get_DevNode_Registry_PropertyW(*Component, CM_DRP_CAPABILITIES, NULL, &Capabilities, &Length, 0);
    ok(Result == CR_SUCCESS, "No capabilities: cr %lu\n", Result);
    ok(Capabilities == (CM_DEVCAP_UNIQUEID | CM_DEVCAP_RAWDEVICEOK | CM_DEVCAP_SURPRISEREMOVALOK),
       "Capabilities are 0x%lx\n", Capabilities);

    Length = sizeof(Buffer);
    ZeroMemory(Buffer, sizeof(Buffer));
    Result = CM_Get_DevNode_Registry_PropertyW(*Component, CM_DRP_BASE_CONTAINERID, NULL, Buffer, &Length, 0);
    ok(Result == CR_SUCCESS, "No container ID: cr %lu\n", Result);
    ok(!_wcsicmp(Buffer, L"{00000000-0000-0000-ffff-ffffffffffff}"), "Container ID is '%ls'\n", Buffer);

    Result = CM_Get_DevNode_Status(&Status, &Problem, *Component, 0);
    ok(Result == CR_SUCCESS, "CM_Get_DevNode_Status failed: cr %lu\n", Result);
    ok(Status & DN_STARTED, "The component device is not started: status 0x%lx problem %lu\n", Status, Problem);
}

/* The bare SoftwareBinary ran from the component's driver store directory with the component's instance ID */
static VOID
CheckComponentSoftware(DEVINST Component, PCWSTR ComponentId)
{
    WCHAR Buffer[MAX_DEVICE_ID_LEN];
    DWORD Size, Type, Waited;
    LONG Error = ERROR_FILE_NOT_FOUND;
    HKEY Key;

    for (Waited = 0; Waited <= 3000; Waited += 50)
    {
        Size = sizeof(Buffer);
        ZeroMemory(Buffer, sizeof(Buffer));
        Error = RegGetValueW(HKEY_LOCAL_MACHINE, SOFTWARE_RESULT_KEY, L"Instance", RRF_RT_REG_SZ, &Type, Buffer, &Size);
        if (Error == ERROR_SUCCESS)
            break;
        Sleep(50);
    }
    ok(Error == ERROR_SUCCESS, "The AddSoftware binary did not run: error %ld\n", Error);
    ok(!_wcsicmp(Buffer, ComponentId), "The AddSoftware binary got '%ls' for the component '%ls'\n", Buffer, ComponentId);

    ok(CM_Open_DevNode_Key(Component, KEY_READ, 0, RegDisposition_OpenExisting, &Key, CM_REGISTRY_SOFTWARE) == CR_SUCCESS,
       "The component has no driver key\n");
    Size = sizeof(Buffer);
    ZeroMemory(Buffer, sizeof(Buffer));
    Error = RegGetValueW(Key, L"Software", L"LntTestSoftware", RRF_RT_REG_SZ, &Type, Buffer, &Size);
    ok(Error == ERROR_SUCCESS, "The software version is not recorded: error %ld\n", Error);
    ok(!wcscmp(Buffer, L"1.0.0.0"), "The recorded software version is '%ls'\n", Buffer);
    RegCloseKey(Key);
}

/* CopyINF put the component's package in the driver store: importing it again finds it there */
static BOOL
FindImportedInf(PCWSTR Inf, WCHAR *Published, DWORD Size)
{
    PWSTR Component = NULL;

    Published[0] = UNICODE_NULL;
    if (SetupCopyOEMInfW(Inf, NULL, SPOST_NONE, SP_COPY_NOOVERWRITE, Published, Size, NULL, &Component))
    {
        /* It was not there: this call imported it */
        SetupUninstallOEMInfW(Component, SUOI_FORCEDELETE, NULL);
        return FALSE;
    }
    return GetLastError() == ERROR_FILE_EXISTS;
}

static VOID
RemoveDevice(PCWSTR InstanceId)
{
    HDEVINFO Set;
    SP_DEVINFO_DATA Data;

    Set = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (Set == INVALID_HANDLE_VALUE)
        return;
    Data.cbSize = sizeof(Data);
    if (SetupDiOpenDeviceInfoW(Set, InstanceId, NULL, 0, &Data))
        ok(SetupDiCallClassInstaller(DIF_REMOVE, Set, &Data), "Removing %ls failed: %lu\n", InstanceId, GetLastError());
    SetupDiDestroyDeviceInfoList(Set);
}

START_TEST(SoftwareComponent)
{
    WCHAR Dir[MAX_PATH], CompDir[MAX_PATH], ParentPath[MAX_PATH], CompPath[MAX_PATH], RegPath[MAX_PATH], SoftwarePath[MAX_PATH];
    WCHAR ParentId[MAX_DEVICE_ID_LEN], ComponentId[MAX_DEVICE_ID_LEN] = L"";
    WCHAR Published[MAX_PATH], ParentPublished[MAX_PATH];
    ULONG Status = 0, Problem = 0;
    SP_DEVINFO_DATA Data;
    DEVINST Parent, Component = 0;
    HDEVINFO Set;
    BOOL Reboot = FALSE, Imported;
    DWORD Wait;
    PWSTR Name;

    GetTempPathW(ARRAYSIZE(Dir), Dir);
    lstrcatW(Dir, L"lntswc");
    CreateDirectoryW(Dir, NULL);
    wsprintfW(CompDir, L"%s\\comp", Dir);
    CreateDirectoryW(CompDir, NULL);
    wsprintfW(ParentPath, L"%s\\lntswparent.inf", Dir);
    wsprintfW(CompPath, L"%s\\lntswcomp.inf", CompDir);
    ok(WriteTextFile(ParentPath, ParentInf), "Writing %ls failed: %lu\n", ParentPath, GetLastError());
    ok(WriteTextFile(CompPath, ComponentInf), "Writing %ls failed: %lu\n", CompPath, GetLastError());
    GetSystemDirectoryW(RegPath, ARRAYSIZE(RegPath));
    lstrcatW(RegPath, L"\\reg.exe");
    wsprintfW(SoftwarePath, L"%s\\lntswreg.exe", CompDir);
    ok(CopyFileW(RegPath, SoftwarePath, FALSE), "Copying %ls failed: %lu\n", RegPath, GetLastError());
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, SOFTWARE_RESULT_KEY);

    Set = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_SYSTEM, NULL);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    if (!CreateParentDevice(Set, &Data))
    {
        if (GetLastError() == ERROR_ACCESS_DENIED)
            skip("Creating a device needs an administrator\n");
        else
            ok(0, "Creating the parent device failed: %lu\n", GetLastError());
        SetupDiDestroyDeviceInfoList(Set);
        return;
    }
    ok(SetupDiGetDeviceInstanceIdW(Set, &Data, ParentId, ARRAYSIZE(ParentId), NULL), "No parent instance ID\n");
    Parent = Data.DevInst;

    ok(UpdateDriverForPlugAndPlayDevicesW(NULL, PARENT_HWID, ParentPath, INSTALLFLAG_FORCE, &Reboot),
       "Installing the parent failed: %lu\n", GetLastError());
    ok(CM_Reenumerate_DevNode(Parent, CM_REENUMERATE_SYNCHRONOUS) == CR_SUCCESS, "CM_Reenumerate_DevNode failed\n");
    Wait = CMP_WaitNoPendingInstallEvents(5000);
    ok(Wait == WAIT_OBJECT_0, "Waiting for the installations returned %lu, error %lu\n", Wait, GetLastError());

    ok(CM_Get_DevNode_Status(&Status, &Problem, Parent, 0) == CR_SUCCESS && (Status & DN_STARTED),
       "The parent on umpass did not start: status 0x%lx problem %lu\n", Status, Problem);

    CheckComponentRecord(ParentId);
    CheckComponentDevice(Parent, ParentId, &Component);
    if (Component)
    {
        CM_Get_Device_IDW(Component, ComponentId, ARRAYSIZE(ComponentId), 0);
        CheckComponentSoftware(Component, ComponentId);
    }

    Imported = FindImportedInf(CompPath, Published, ARRAYSIZE(Published));
    ok(Imported, "CopyINF did not import the component package: %lu\n", GetLastError());

    if (Component)
    {
        WCHAR Class[64];
        ULONG Length = sizeof(Class);

        ZeroMemory(Class, sizeof(Class));
        CM_Get_DevNode_Registry_PropertyW(Component, CM_DRP_CLASSGUID, NULL, Class, &Length, 0);
        ok(!_wcsicmp(Class, L"{5c4c3332-344d-483c-8739-259e934c9cc8}"),
           "The component device got no SoftwareComponent driver: class '%ls'\n", Class);
    }

    if (ComponentId[0])
        RemoveDevice(ComponentId);
    ok(SetupDiCallClassInstaller(DIF_REMOVE, Set, &Data), "Removing the parent failed: %lu\n", GetLastError());
    SetupDiDestroyDeviceInfoList(Set);

    /* The parent goes first: looking it up again processes its CopyINF, which must still find the component there */
    ParentPublished[0] = UNICODE_NULL;
    if (FindImportedInf(ParentPath, ParentPublished, ARRAYSIZE(ParentPublished)) &&
        (Name = wcsrchr(ParentPublished, L'\\')) != NULL)
    {
        SetupUninstallOEMInfW(Name + 1, SUOI_FORCEDELETE, NULL);
    }

    if (Imported && (Name = wcsrchr(Published, L'\\')) != NULL)
        ok(SetupUninstallOEMInfW(Name + 1, SUOI_FORCEDELETE, NULL), "Uninstalling %ls failed: %lu\n", Name + 1, GetLastError());

    RegDeleteKeyW(HKEY_LOCAL_MACHINE, SOFTWARE_RESULT_KEY);
    DeleteFileW(SoftwarePath);
    DeleteFileW(CompPath);
    DeleteFileW(ParentPath);
    RemoveDirectoryW(CompDir);
    RemoveDirectoryW(Dir);
}
