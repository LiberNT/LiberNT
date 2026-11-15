/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a device's class name comes from its class key, not from its instance key
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>

static BOOL
ReadClassKeyName(HKEY InstanceKey, WCHAR *Name, DWORD NameSize)
{
    WCHAR Guid[40], Path[MAX_PATH];
    DWORD Size = sizeof(Guid), Type;
    HKEY ClassKey;
    LONG Error;

    if (RegQueryValueExW(InstanceKey, L"ClassGUID", NULL, &Type, (BYTE *)Guid, &Size) != ERROR_SUCCESS || Type != REG_SZ)
        return FALSE;
    Guid[min(Size / sizeof(WCHAR), ARRAYSIZE(Guid) - 1)] = UNICODE_NULL;
    _snwprintf(Path, ARRAYSIZE(Path) - 1, L"System\\CurrentControlSet\\Control\\Class\\%s", Guid);
    Path[ARRAYSIZE(Path) - 1] = UNICODE_NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_QUERY_VALUE, &ClassKey) != ERROR_SUCCESS)
        return FALSE;
    Size = NameSize;
    Error = RegQueryValueExW(ClassKey, L"Class", NULL, &Type, (BYTE *)Name, &Size);
    RegCloseKey(ClassKey);
    return Error == ERROR_SUCCESS && Type == REG_SZ;
}

static void
CheckDevice(HDEVINFO Set, SP_DEVINFO_DATA *Data, ULONG *Checked)
{
    WCHAR Id[MAX_DEVICE_ID_LEN], Path[MAX_PATH], Expected[MAX_CLASS_NAME_LEN], Name[MAX_CLASS_NAME_LEN];
    DEVPROPTYPE PropType;
    ULONG Size, RegType, Status, Problem;
    CONFIGRET Cr;
    HKEY InstanceKey;
    LONG Error;
    BOOL HasClass;

    if (!SetupDiGetDeviceInstanceIdW(Set, Data, Id, ARRAYSIZE(Id), NULL))
        return;
    _snwprintf(Path, ARRAYSIZE(Path) - 1, L"System\\CurrentControlSet\\Enum\\%s", Id);
    Path[ARRAYSIZE(Path) - 1] = UNICODE_NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Path, 0, KEY_QUERY_VALUE, &InstanceKey) != ERROR_SUCCESS)
        return;

    Error = RegQueryValueExW(InstanceKey, L"Class", NULL, NULL, NULL, NULL);
    ok(Error == ERROR_FILE_NOT_FOUND, "%ls: instance key holds a Class value (error %ld)\n", Id, Error);

    if (CM_Get_DevNode_Status(&Status, &Problem, Data->DevInst, 0) == CR_SUCCESS && (Status & DN_HAS_PROBLEM) &&
        Problem == CM_PROB_FAILED_INSTALL)
    {
        Error = RegQueryValueExW(InstanceKey, L"ClassGUID", NULL, NULL, NULL, NULL);
        ok(Error == ERROR_FILE_NOT_FOUND, "%ls: a device with no driver has a ClassGUID (error %ld)\n", Id, Error);
    }

    HasClass = ReadClassKeyName(InstanceKey, Expected, sizeof(Expected));
    RegCloseKey(InstanceKey);
    if (!HasClass)
        return;
    (*Checked)++;

    Name[0] = UNICODE_NULL;
    ok(SetupDiGetDeviceRegistryPropertyW(Set, Data, SPDRP_CLASS, &RegType, (BYTE *)Name, sizeof(Name), NULL),
       "%ls: SPDRP_CLASS failed with %lu\n", Id, GetLastError());
    ok(!wcscmp(Name, Expected), "%ls: SPDRP_CLASS is '%ls', class key says '%ls'\n", Id, Name, Expected);

    Name[0] = UNICODE_NULL;
    Size = sizeof(Name);
    Cr = CM_Get_DevNode_Registry_PropertyW(Data->DevInst, CM_DRP_CLASS, &RegType, Name, &Size, 0);
    ok(Cr == CR_SUCCESS, "%ls: CM_DRP_CLASS returned %lu\n", Id, Cr);
    ok(Cr != CR_SUCCESS || RegType == REG_SZ, "%ls: CM_DRP_CLASS type %lu\n", Id, RegType);
    ok(!wcscmp(Name, Expected), "%ls: CM_DRP_CLASS is '%ls', class key says '%ls'\n", Id, Name, Expected);

    Name[0] = UNICODE_NULL;
    Size = sizeof(Name);
    Cr = CM_Get_DevNode_PropertyW(Data->DevInst, &DEVPKEY_Device_Class, &PropType, (BYTE *)Name, &Size, 0);
    ok(Cr == CR_SUCCESS, "%ls: DEVPKEY_Device_Class returned %lu\n", Id, Cr);
    ok(Cr != CR_SUCCESS || PropType == DEVPROP_TYPE_STRING, "%ls: DEVPKEY_Device_Class type 0x%lx\n", Id, PropType);
    ok(!wcscmp(Name, Expected), "%ls: DEVPKEY_Device_Class is '%ls', class key says '%ls'\n", Id, Name, Expected);
}

static void
TestSetClassProperty(void)
{
    static const GUID UnknownClass = { 0x4d36e97e, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
    static const WCHAR Unknown[] = L"Unknown";
    SP_DEVINFO_DATA Data;
    HDEVINFO Set;
    HKEY InstanceKey;
    LONG Error;
    BOOL Ret;
    DWORD LastError;

    Set = SetupDiCreateDeviceInfoList(&UnknownClass, NULL);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;
    Data.cbSize = sizeof(Data);
    Ret = SetupDiCreateDeviceInfoW(Set, L"ROOT\\LNTCLASSNAME\\0000", &UnknownClass, NULL, NULL, 0, &Data);
    ok(Ret, "SetupDiCreateDeviceInfoW failed with %lu\n", GetLastError());
    if (Ret)
    {
        ok(SetupDiRegisterDeviceInfo(Set, &Data, 0, NULL, NULL, NULL), "SetupDiRegisterDeviceInfo failed with %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        Ret = SetupDiSetDeviceRegistryPropertyW(Set, &Data, SPDRP_CLASS, (const BYTE *)Unknown, sizeof(Unknown));
        LastError = GetLastError();
        ok(!Ret, "SPDRP_CLASS set succeeded\n");
        ok(LastError == ERROR_INVALID_REG_PROPERTY, "SPDRP_CLASS set failed with %lu, expected ERROR_INVALID_REG_PROPERTY\n", LastError);
        Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Enum\\ROOT\\LNTCLASSNAME\\0000", 0, KEY_QUERY_VALUE, &InstanceKey);
        if (Error == ERROR_SUCCESS)
        {
            Error = RegQueryValueExW(InstanceKey, L"Class", NULL, NULL, NULL, NULL);
            ok(Error == ERROR_FILE_NOT_FOUND, "instance key holds a Class value after the set (error %ld)\n", Error);
            RegCloseKey(InstanceKey);
        }
        ok(SetupDiRemoveDevice(Set, &Data), "SetupDiRemoveDevice failed with %lu\n", GetLastError());
    }
    SetupDiDestroyDeviceInfoList(Set);
}

START_TEST(DeviceClassName)
{
    TestSetClassProperty();
    SP_DEVINFO_DATA Data;
    HDEVINFO Set;
    ULONG Checked = 0;
    DWORD Index;

    Set = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    ok(Set != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (Set == INVALID_HANDLE_VALUE)
        return;

    Data.cbSize = sizeof(Data);
    for (Index = 0; SetupDiEnumDeviceInfo(Set, Index, &Data); Index++)
        CheckDevice(Set, &Data, &Checked);

    ok(Checked > 0, "no present device has a class key with a name\n");
    trace("%lu devices checked\n", Checked);
    SetupDiDestroyDeviceInfoList(Set);
}
