/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that extension INFs apply over a device's base driver and never become it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <devguid.h>

#define DEVICE_HWID L"LNT_EXT_TEST_DEVICE"
#define ONLY_HWID L"LNT_EXT_ONLY_DEVICE"
#define EXTENSION_KEY L"{6d3f0c1e-4a52-4f3b-9c1d-2b8e7a5f9e01}#LntExtComp"

static const char BaseInf[] =
    "[Version]\r\n"
    "Signature=\"$WINDOWS NT$\"\r\n"
    "Class=System\r\n"
    "ClassGuid={4d36e97d-e325-11ce-bfc1-08002be10318}\r\n"
    "Provider=%Lnt%\r\n"
    "DriverVer=11/15/2026,1.0.0.0\r\n"
    "CatalogFile=lntextbase.cat\r\n"
    "\r\n"
    "[Manufacturer]\r\n"
    "%Lnt%=Lnt,NTamd64,NTarm64,NTx86\r\n"
    "\r\n"
    "[Lnt.NTamd64]\r\n"
    "%Dev%=DevInstall,LNT_EXT_TEST_DEVICE\r\n"
    "[Lnt.NTarm64]\r\n"
    "%Dev%=DevInstall,LNT_EXT_TEST_DEVICE\r\n"
    "[Lnt.NTx86]\r\n"
    "%Dev%=DevInstall,LNT_EXT_TEST_DEVICE\r\n"
    "\r\n"
    "[DevInstall.NT]\r\n"
    "Include=umpass.inf\r\n"
    "Needs=UmPass\r\n"
    "AddReg=DevInstall.AddReg\r\n"
    "\r\n"
    "[DevInstall.AddReg]\r\n"
    "HKR,,LntExtDriverValue,,\"base\"\r\n"
    "\r\n"
    "[DevInstall.NT.HW]\r\n"
    "Include=umpass.inf\r\n"
    "Needs=UmPass.HW\r\n"
    "\r\n"
    "[DevInstall.NT.Interfaces]\r\n"
    "Include=umpass.inf\r\n"
    "Needs=UmPass.Interfaces\r\n"
    "\r\n"
    "[DevInstall.NT.Services]\r\n"
    "Include=umpass.inf\r\n"
    "Needs=UmPass.Services\r\n"
    "\r\n"
    "[Strings]\r\n"
    "Lnt=\"LiberNT tests\"\r\n"
    "Dev=\"LiberNT extension test device\"\r\n";

#define EXTENSION_HEAD(guid, ver, cat) \
    "[Version]\r\n" \
    "Signature=\"$WINDOWS NT$\"\r\n" \
    "Class=Extension\r\n" \
    "ClassGuid={e2f84ce7-8efa-411c-aa69-97454ca4cb57}\r\n" \
    "ExtensionId=" guid "\r\n" \
    "Provider=%Lnt%\r\n" \
    "DriverVer=" ver "\r\n" \
    "CatalogFile=" cat "\r\n" \
    "\r\n" \
    "[Manufacturer]\r\n" \
    "%Lnt%=Lnt,NTamd64,NTarm64,NTx86\r\n" \
    "\r\n" \
    "[Lnt.NTamd64]\r\n" \
    "%Ext%=ExtInstall,LNT_EXT_TEST_DEVICE,LNT_EXT_ONLY_DEVICE\r\n" \
    "[Lnt.NTarm64]\r\n" \
    "%Ext%=ExtInstall,LNT_EXT_TEST_DEVICE,LNT_EXT_ONLY_DEVICE\r\n" \
    "[Lnt.NTx86]\r\n" \
    "%Ext%=ExtInstall,LNT_EXT_TEST_DEVICE,LNT_EXT_ONLY_DEVICE\r\n" \
    "\r\n"

#define EXTENSION_TAIL \
    "[Strings]\r\n" \
    "Lnt=\"LiberNT tests\"\r\n" \
    "Ext=\"LiberNT test extension\"\r\n"

static const char OldExtensionInf[] =
    EXTENSION_HEAD("{6d3f0c1e-4a52-4f3b-9c1d-2b8e7a5f9e01}", "11/01/2026,1.0.0.0", "lntextold.cat")
    "[ExtInstall.NT]\r\n"
    "AddReg=Ext.AddReg\r\n"
    "\r\n"
    "[Ext.AddReg]\r\n"
    "HKR,,LntExtDriverValue,,\"old\"\r\n"
    "\r\n"
    EXTENSION_TAIL;

static const char NewExtensionInf[] =
    EXTENSION_HEAD("{6d3f0c1e-4a52-4f3b-9c1d-2b8e7a5f9e01}", "11/15/2026,2.0.0.0", "lntextnew.cat")
    "[ExtInstall.NT]\r\n"
    "AddReg=Ext.AddReg\r\n"
    "\r\n"
    "[Ext.AddReg]\r\n"
    "HKR,,LntExtDriverValue,,\"new\"\r\n"
    "\r\n"
    "[ExtInstall.NT.HW]\r\n"
    "AddReg=Ext.HW.AddReg\r\n"
    "\r\n"
    "[Ext.HW.AddReg]\r\n"
    "HKR,,LntExtHwValue,0x10001,7\r\n"
    "\r\n"
    "[ExtInstall.NT.Components]\r\n"
    "AddComponent=LntExtComp,,ExtComp\r\n"
    "\r\n"
    "[ExtComp]\r\n"
    "ComponentIDs=LNT_EXT_TEST_COMPONENT\r\n"
    "\r\n"
    EXTENSION_TAIL;

static const char LateExtensionInf[] =
    EXTENSION_HEAD("{1b6f0d2a-93c4-4e8b-a7f5-6c2d9e3b8a14}", "11/15/2026,1.0.0.0", "lntextlate.cat")
    "[ExtInstall.NT]\r\n"
    "AddReg=Ext.AddReg\r\n"
    "\r\n"
    "[Ext.AddReg]\r\n"
    "HKR,,LntLateExtValue,,\"late\"\r\n"
    "\r\n"
    EXTENSION_TAIL;

struct package
{
    const char *Text;
    PCWSTR Name;
    WCHAR Dir[MAX_PATH];
    WCHAR Path[MAX_PATH];
    WCHAR Published[MAX_PATH];
};

static BOOL
WritePackage(PCWSTR Root, struct package *Package)
{
    HANDLE File;
    DWORD Written = 0;
    BOOL Result;

    wsprintfW(Package->Dir, L"%s\\%s", Root, Package->Name);
    wsprintfW(Package->Path, L"%s\\%s.inf", Package->Dir, Package->Name);
    CreateDirectoryW(Package->Dir, NULL);
    File = CreateFileW(Package->Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    Result = WriteFile(File, Package->Text, (DWORD)strlen(Package->Text), &Written, NULL) &&
             Written == strlen(Package->Text);
    CloseHandle(File);
    return Result;
}

/* Importing a package again finds it in the driver store and names it */
static BOOL
FindPublished(struct package *Package)
{
    Package->Published[0] = UNICODE_NULL;
    if (SetupCopyOEMInfW(Package->Path, NULL, SPOST_NONE, SP_COPY_NOOVERWRITE, Package->Published,
                         ARRAYSIZE(Package->Published), NULL, NULL))
    {
        return TRUE;
    }
    return GetLastError() == ERROR_FILE_EXISTS;
}

static VOID
Unpublish(struct package *Package)
{
    PWSTR Name;

    if (Package->Published[0] && (Name = wcsrchr(Package->Published, L'\\')) != NULL)
        SetupUninstallOEMInfW(Name + 1, SUOI_FORCEDELETE, NULL);
    DeleteFileW(Package->Path);
    RemoveDirectoryW(Package->Dir);
}

static BOOL
CreateRootDevice(HDEVINFO Set, PCWSTR Name, PCWSTR HardwareId, SP_DEVINFO_DATA *Data)
{
    WCHAR Ids[64];
    DWORD Size = (DWORD)((wcslen(HardwareId) + 2) * sizeof(WCHAR));

    ZeroMemory(Ids, sizeof(Ids));
    wcscpy(Ids, HardwareId);
    Data->cbSize = sizeof(*Data);
    return SetupDiCreateDeviceInfoW(Set, Name, &GUID_DEVCLASS_SYSTEM, NULL, NULL, DICD_GENERATE_ID, Data) &&
           SetupDiSetDeviceRegistryPropertyW(Set, Data, SPDRP_HARDWAREID, (const BYTE *)Ids, Size) &&
           SetupDiCallClassInstaller(DIF_REGISTERDEVICE, Set, Data);
}

static BOOL
ReadString(HKEY Key, PCWSTR Name, WCHAR *Buffer, DWORD Size)
{
    DWORD Type;

    ZeroMemory(Buffer, Size);
    return RegQueryValueExW(Key, Name, NULL, &Type, (BYTE *)Buffer, &Size) == ERROR_SUCCESS && Type == REG_SZ;
}

static VOID
CheckExtensionApplied(HDEVINFO Set, SP_DEVINFO_DATA *Data, PCWSTR InstanceId)
{
    WCHAR Text[128], Path[MAX_PATH], Id[MAX_DEVICE_ID_LEN];
    DWORD Value = 0, Size = sizeof(Value), Type;
    DEVINST Child;
    HKEY Key;

    ZeroMemory(Text, sizeof(Text));
    ok(SetupDiGetDeviceRegistryPropertyW(Set, Data, SPDRP_CLASSGUID, NULL, (BYTE *)Text, sizeof(Text), NULL),
       "No class: %lu\n", GetLastError());
    ok(!_wcsicmp(Text, L"{4d36e97d-e325-11ce-bfc1-08002be10318}"), "The base driver has class '%ls'\n", Text);

    Key = SetupDiOpenDevRegKey(Set, Data, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
    ok(Key != INVALID_HANDLE_VALUE, "No driver key: %lu\n", GetLastError());
    if (Key != INVALID_HANDLE_VALUE)
    {
        ok(ReadString(Key, L"LntExtDriverValue", Text, sizeof(Text)) && !wcscmp(Text, L"new"),
           "The newest extension did not apply over the base: '%ls'\n", Text);
        RegCloseKey(Key);
    }

    Key = SetupDiOpenDevRegKey(Set, Data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    ok(Key != INVALID_HANDLE_VALUE, "No hardware key: %lu\n", GetLastError());
    if (Key != INVALID_HANDLE_VALUE)
    {
        ok(RegQueryValueExW(Key, L"LntExtHwValue", NULL, &Type, (BYTE *)&Value, &Size) == ERROR_SUCCESS &&
           Type == REG_DWORD && Value == 7, "The extension's .HW section did not apply: %lu\n", Value);
        RegCloseKey(Key);
    }

    wsprintfW(Path, L"System\\CurrentControlSet\\Enum\\%s\\Devices\\%s", InstanceId, EXTENSION_KEY);
    Key = NULL;
    ok(RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_READ, &Key) == ERROR_SUCCESS, "No record %ls\n", Path);
    if (Key)
        RegCloseKey(Key);

    Id[0] = UNICODE_NULL;
    if (CM_Get_Child(&Child, Data->DevInst, 0) == CR_SUCCESS)
        CM_Get_Device_IDW(Child, Id, ARRAYSIZE(Id), 0);
    ok(!_wcsnicmp(Id, L"SWD\\DRIVERENUM\\" EXTENSION_KEY L"&", wcslen(L"SWD\\DRIVERENUM\\" EXTENSION_KEY L"&")),
       "The extension's component device is '%ls'\n", Id);
}

static VOID
CheckNoBaseDriverFromExtension(HDEVINFO Set)
{
    SP_DEVINFO_DATA Data;
    SP_DRVINFO_DATA_W Driver;

    if (!CreateRootDevice(Set, L"LNTEXTONLY", ONLY_HWID, &Data))
    {
        ok(0, "Creating the extension-only device failed: %lu\n", GetLastError());
        return;
    }

    ok(SetupDiBuildDriverInfoList(Set, &Data, SPDIT_COMPATDRIVER), "SetupDiBuildDriverInfoList failed: %lu\n", GetLastError());
    Driver.cbSize = sizeof(Driver);
    ok(!SetupDiEnumDriverInfoW(Set, &Data, SPDIT_COMPATDRIVER, 0, &Driver) && GetLastError() == ERROR_NO_MORE_ITEMS,
       "An extension INF was offered as a base driver: '%ls' error %lu\n", Driver.Description, GetLastError());
    SetupDiDestroyDriverInfoList(Set, &Data, SPDIT_COMPATDRIVER);

    ok(SetupDiCallClassInstaller(DIF_REMOVE, Set, &Data), "Removing the extension-only device failed: %lu\n", GetLastError());
}

START_TEST(DeviceExtension)
{
    struct package Base = { BaseInf, L"lntextbase" };
    struct package Old = { OldExtensionInf, L"lntextold" };
    struct package New = { NewExtensionInf, L"lntextnew" };
    struct package Late = { LateExtensionInf, L"lntextlate" };
    WCHAR Root[MAX_PATH], InstanceId[MAX_DEVICE_ID_LEN], Text[64], ChildId[MAX_DEVICE_ID_LEN] = L"";
    SP_DEVINFO_DATA Data;
    HDEVINFO Set;
    BOOL Reboot = FALSE;
    DEVINST Child;
    HKEY Key;

    GetTempPathW(ARRAYSIZE(Root), Root);
    lstrcatW(Root, L"lntext");
    CreateDirectoryW(Root, NULL);
    ok(WritePackage(Root, &Base) && WritePackage(Root, &Old) && WritePackage(Root, &New) && WritePackage(Root, &Late),
       "Writing the packages failed: %lu\n", GetLastError());

    ok(FindPublished(&Old), "Importing the older extension failed: %lu\n", GetLastError());
    ok(FindPublished(&New), "Importing the newer extension failed: %lu\n", GetLastError());

    Set = SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_SYSTEM, NULL);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    if (!CreateRootDevice(Set, L"LNTEXTTEST", DEVICE_HWID, &Data))
    {
        if (GetLastError() == ERROR_ACCESS_DENIED)
            skip("Creating a device needs an administrator\n");
        else
            ok(0, "Creating the device failed: %lu\n", GetLastError());
        SetupDiDestroyDeviceInfoList(Set);
        Unpublish(&Old);
        Unpublish(&New);
        return;
    }
    ok(SetupDiGetDeviceInstanceIdW(Set, &Data, InstanceId, ARRAYSIZE(InstanceId), NULL), "No instance ID\n");

    ok(UpdateDriverForPlugAndPlayDevicesW(NULL, DEVICE_HWID, Base.Path, INSTALLFLAG_FORCE, &Reboot),
       "Installing the base driver failed: %lu\n", GetLastError());
    ok(CM_Reenumerate_DevNode(Data.DevInst, CM_REENUMERATE_SYNCHRONOUS) == CR_SUCCESS, "CM_Reenumerate_DevNode failed\n");
    ok(CMP_WaitNoPendingInstallEvents(5000) == WAIT_OBJECT_0, "An installation is still pending\n");

    CheckExtensionApplied(Set, &Data, InstanceId);
    CheckNoBaseDriverFromExtension(Set);

    /* A package installed later goes on the devices that already have their base driver */
    ok(DiInstallDriverW(NULL, Late.Path, 0, &Reboot), "DiInstallDriverW of an extension failed: %lu\n", GetLastError());
    Key = SetupDiOpenDevRegKey(Set, &Data, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
    ok(Key != INVALID_HANDLE_VALUE && ReadString(Key, L"LntLateExtValue", Text, sizeof(Text)) && !wcscmp(Text, L"late"),
       "The extension installed later did not apply: '%ls'\n", Text);
    if (Key != INVALID_HANDLE_VALUE)
        RegCloseKey(Key);
    ok(CMP_WaitNoPendingInstallEvents(5000) == WAIT_OBJECT_0, "An installation is still pending\n");

    if (CM_Get_Child(&Child, Data.DevInst, 0) == CR_SUCCESS)
        CM_Get_Device_IDW(Child, ChildId, ARRAYSIZE(ChildId), 0);
    if (ChildId[0])
    {
        SP_DEVINFO_DATA ChildData;

        ChildData.cbSize = sizeof(ChildData);
        if (SetupDiOpenDeviceInfoW(Set, ChildId, NULL, 0, &ChildData))
            SetupDiCallClassInstaller(DIF_REMOVE, Set, &ChildData);
    }
    ok(SetupDiCallClassInstaller(DIF_REMOVE, Set, &Data), "Removing the device failed: %lu\n", GetLastError());
    SetupDiDestroyDeviceInfoList(Set);

    FindPublished(&Base);
    FindPublished(&Late);
    Unpublish(&Base);
    Unpublish(&Late);
    Unpublish(&New);
    Unpublish(&Old);
    RemoveDirectoryW(Root);
}
