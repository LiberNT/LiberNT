/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Configuration of network devices
 * FILE:            dll/win32/netcfgx/installer.c
 * PURPOSE:         Network devices installer
 *
 * PROGRAMMERS:     Hervé Poussineau (hpoussin@reactos.org)
 */

#include "precomp.h"
#include <strsafe.h>
#include <devpropdef.h>

typedef struct _NET_INF_KEYWORDS
{
    BOOL Present;
    DWORD IfType;
    DWORD MediaType;
    DWORD PhysicalMediaType;
} NET_INF_KEYWORDS, *PNET_INF_KEYWORDS;
typedef const NET_INF_KEYWORDS *PCNET_INF_KEYWORDS;

static const GUID NetSetupInterfaceFmtid = {0xa111f1f4, 0x5923, 0x47c0, {0x9a, 0x68, 0xd0, 0xba, 0xfb, 0x57, 0x79, 0x01}};
static const GUID NetSetupInfFmtid = {0xa111f200, 0x5923, 0x47c0, {0x9a, 0x68, 0xd0, 0xba, 0xfb, 0x57, 0x79, 0x01}};
static const GUID NetSetupBindFmtid = {0xa111f1f1, 0x5923, 0x47c0, {0x9a, 0x68, 0xd0, 0xba, 0xfb, 0x57, 0x79, 0x01}};
static const GUID NetSetupDriverFmtid = {0xa111f1f0, 0x5923, 0x47c0, {0x9a, 0x68, 0xd0, 0xba, 0xfb, 0x57, 0x79, 0x01}};

#define NETSETUP_INTERFACES_KEY L"SYSTEM\\CurrentControlSet\\Control\\NetworkSetup2\\Interfaces"
#define NET_LUID_INDEX_FIRST 0x8000
#define NET_LUID_INDEX_LAST  0xFFFFFF


/* Append a REG_SZ to an existing REG_MULTI_SZ string in the registry.
 * If the value doesn't exist, create it.
 * Returns ERROR_SUCCESS if success. Otherwise, returns an error code
 */
static
LONG
AppendStringToMultiSZ(
    IN HKEY hKey,
    IN PCWSTR ValueName,
    IN PCWSTR ValueToAppend)
{
    PWSTR Buffer = NULL;
    DWORD dwRegType;
    DWORD dwRequired, dwLength;
    DWORD dwTmp;
    LONG rc;

    rc = RegQueryValueExW(hKey,
                          ValueName,
                          NULL,
                          &dwRegType,
                          NULL,
                          &dwRequired);
    if (rc != ERROR_FILE_NOT_FOUND)
    {
        if (rc != ERROR_SUCCESS)
            goto cleanup;
        if (dwRegType != REG_MULTI_SZ)
        {
            rc = ERROR_GEN_FAILURE;
            goto cleanup;
        }

        dwTmp = dwLength = dwRequired + wcslen(ValueToAppend) * sizeof(WCHAR) + sizeof(UNICODE_NULL);
        Buffer = HeapAlloc(GetProcessHeap(), 0, dwLength);
        if (!Buffer)
        {
            rc = ERROR_NOT_ENOUGH_MEMORY;
            goto cleanup;
        }

        rc = RegQueryValueExW(hKey,
                              ValueName,
                              NULL,
                              NULL,
                              (BYTE*)Buffer,
                              &dwTmp);
        if (rc != ERROR_SUCCESS)
            goto cleanup;
    }
    else
    {
        dwRequired = sizeof(WCHAR);
        dwLength = wcslen(ValueToAppend) * sizeof(WCHAR) + 2 * sizeof(UNICODE_NULL);
        Buffer = HeapAlloc(GetProcessHeap(), 0, dwLength);
        if (!Buffer)
        {
            rc = ERROR_NOT_ENOUGH_MEMORY;
            goto cleanup;
        }
    }

    /* Append the value */
    wcscpy(&Buffer[dwRequired / sizeof(WCHAR) - 1], ValueToAppend);
    /* Terminate the REG_MULTI_SZ string */
    Buffer[dwLength / sizeof(WCHAR) - 1] = UNICODE_NULL;

    rc = RegSetValueExW(hKey,
                        ValueName,
                        0,
                        REG_MULTI_SZ,
                        (const BYTE*)Buffer,
                        dwLength);

cleanup:
    HeapFree(GetProcessHeap(), 0, Buffer);
    return rc;
}

static
DWORD
GetUniqueConnectionName(
    _In_ HKEY hNetworkKey,
    _Out_ PWSTR *ppszNameBuffer)
{
    int Length = 0;
    PWSTR pszDefaultName = NULL;
    PWSTR pszNameBuffer = NULL;
    DWORD dwSubKeys = 0;
    DWORD dwError;

    TRACE("GetNewConnectionName()\n");

    Length = LoadStringW(netcfgx_hInstance, IDS_NET_CONNECT, (LPWSTR)&pszDefaultName, 0);
    if (Length == 0)
    {
        pszDefaultName = L"Network Connection";
        Length = wcslen(pszDefaultName);
    }

    TRACE("Length %d\n", Length);

    dwError = RegQueryInfoKeyW(hNetworkKey,
                               NULL, NULL, NULL, &dwSubKeys,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (dwError != ERROR_SUCCESS)
    {
        ERR("RegQueryInfoKeyW: Error %lu\n", dwError);
        return dwError;
    }

    TRACE("Adapter Count: %lu\n", dwSubKeys);

    pszNameBuffer = HeapAlloc(GetProcessHeap(),
                              HEAP_ZERO_MEMORY,
                              (Length + ((dwSubKeys != 0) ? 6 : 1)) * sizeof(WCHAR));
    if (pszNameBuffer == NULL)
    {
        return ERROR_OUTOFMEMORY;
    }

    if (dwSubKeys != 0)
        StringCchPrintfW(pszNameBuffer, Length + 6, L"%.*s %lu", Length, pszDefaultName, dwSubKeys + 1);
    else
        StringCchPrintfW(pszNameBuffer, Length + 1, L"%.*s", Length, pszDefaultName);

    TRACE("Adapter Name: %S\n", pszNameBuffer);

    *ppszNameBuffer = pszNameBuffer;

    return ERROR_SUCCESS;
}

static
LONG
SetSingleMultiSzValue(
    _In_ HKEY hKey,
    _In_ PCWSTR ValueName,
    _In_ PCWSTR String)
{
    SIZE_T Length = wcslen(String);
    PWSTR Buffer;
    LONG rc;

    Buffer = HeapAlloc(GetProcessHeap(), 0, (Length + 2) * sizeof(WCHAR));
    if (Buffer == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    CopyMemory(Buffer, String, Length * sizeof(WCHAR));
    Buffer[Length] = UNICODE_NULL;
    Buffer[Length + 1] = UNICODE_NULL;
    rc = RegSetValueExW(hKey, ValueName, 0, REG_MULTI_SZ, (const BYTE*)Buffer, (DWORD)((Length + 2) * sizeof(WCHAR)));
    HeapFree(GetProcessHeap(), 0, Buffer);
    return rc;
}

static
LONG
SetNetSetupProperty(
    _In_ HKEY hPropertiesKey,
    _In_ const GUID *Fmtid,
    _In_ ULONG Pid,
    _In_ DEVPROPTYPE Type,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ DWORD Size)
{
    WCHAR SubKey[64];
    HKEY hKey;
    LONG rc;

    StringCchPrintfW(SubKey, ARRAYSIZE(SubKey),
                     L"{%08lx-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x}\\%04lx",
                     Fmtid->Data1, Fmtid->Data2, Fmtid->Data3,
                     Fmtid->Data4[0], Fmtid->Data4[1], Fmtid->Data4[2], Fmtid->Data4[3],
                     Fmtid->Data4[4], Fmtid->Data4[5], Fmtid->Data4[6], Fmtid->Data4[7],
                     Pid);

    rc = RegCreateKeyExW(hPropertiesKey, SubKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL);
    if (rc != ERROR_SUCCESS)
        return rc;

    rc = RegSetValueExW(hKey, NULL, 0, 0xFFFF0000 | Type, (const BYTE*)Data, Size);
    RegCloseKey(hKey);
    return rc;
}

static
LONG
SetNetSetupStringProperty(
    _In_ HKEY hPropertiesKey,
    _In_ const GUID *Fmtid,
    _In_ ULONG Pid,
    _In_ PCWSTR String)
{
    return SetNetSetupProperty(hPropertiesKey, Fmtid, Pid, DEVPROP_TYPE_STRING,
                               String, (wcslen(String) + 1) * sizeof(WCHAR));
}

static
LONG
SetNetSetupBooleanProperty(
    _In_ HKEY hPropertiesKey,
    _In_ const GUID *Fmtid,
    _In_ ULONG Pid)
{
    BYTE Value = 1;

    return SetNetSetupProperty(hPropertiesKey, Fmtid, Pid, DEVPROP_TYPE_BOOLEAN, &Value, sizeof(Value));
}

static
LONG
SetNetSetupRangeProperty(
    _In_ HKEY hPropertiesKey,
    _In_ HKEY hInterfacesKey,
    _In_ PCWSTR ValueName,
    _In_ ULONG Pid)
{
    WCHAR Range[256];
    DWORD dwType, dwSize = sizeof(Range) - 2 * sizeof(WCHAR);
    PWSTR p;
    LONG rc;

    ZeroMemory(Range, sizeof(Range));
    rc = RegQueryValueExW(hInterfacesKey, ValueName, NULL, &dwType, (LPBYTE)Range, &dwSize);
    if (rc != ERROR_SUCCESS || dwType != REG_SZ || Range[0] == UNICODE_NULL)
        return rc;

    for (p = Range; *p != UNICODE_NULL; p++)
    {
        if (*p == L',')
            *p = UNICODE_NULL;
    }

    return SetNetSetupProperty(hPropertiesKey, &NetSetupBindFmtid, Pid, DEVPROP_TYPE_STRING_LIST,
                               Range, (DWORD)((p - Range) + 2) * sizeof(WCHAR));
}

static
DWORD
AllocateNetLuidIndex(
    _In_ HKEY hInterfacesKey,
    _In_ PCWSTR UuidString,
    _In_ DWORD IfType)
{
    WCHAR SubKeyName[64];
    DWORD Index, Candidate = NET_LUID_INDEX_FIRST;
    BOOL Collision;

    do
    {
        Collision = FALSE;
        for (Index = 0; ; Index++)
        {
            DWORD NameLength = ARRAYSIZE(SubKeyName);
            DWORD Value, dwType, dwSize;
            DWORD OtherIfType = 0;
            HKEY hKernelKey;

            if (RegEnumKeyExW(hInterfacesKey, Index, SubKeyName, &NameLength, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;
            if (_wcsicmp(SubKeyName, UuidString) == 0)
                continue;
            if (StringCchCatW(SubKeyName, ARRAYSIZE(SubKeyName), L"\\Kernel") != S_OK)
                continue;
            if (RegOpenKeyExW(hInterfacesKey, SubKeyName, 0, KEY_QUERY_VALUE, &hKernelKey) != ERROR_SUCCESS)
                continue;

            dwSize = sizeof(Value);
            if (RegQueryValueExW(hKernelKey, L"IfType", NULL, &dwType, (LPBYTE)&Value, &dwSize) == ERROR_SUCCESS && dwType == REG_DWORD)
                OtherIfType = Value;
            dwSize = sizeof(Value);
            if (OtherIfType == IfType &&
                RegQueryValueExW(hKernelKey, L"NetLuidIndex", NULL, &dwType, (LPBYTE)&Value, &dwSize) == ERROR_SUCCESS &&
                dwType == REG_DWORD && Value == Candidate)
            {
                Collision = TRUE;
            }
            RegCloseKey(hKernelKey);

            if (Collision)
                break;
        }

        if (Collision)
            Candidate++;
    } while (Collision && Candidate <= NET_LUID_INDEX_LAST);

    return Collision ? 0 : Candidate;
}

static
DWORD
WriteNetSetupInterface(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ PCWSTR UuidString,
    _In_ PCNET_INF_KEYWORDS Keywords,
    _In_ DWORD Characteristics,
    _In_opt_ PCWSTR IfAlias)
{
    HKEY hDriverKey = INVALID_HANDLE_VALUE;
    HKEY hInterfacesKey = NULL, hInterfaceKey = NULL, hKernelKey = NULL, hPropertiesKey = NULL, hNdiKey = NULL;
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    WCHAR ComponentId[MAX_DEVICE_ID_LEN];
    WCHAR IfDescr[LINE_LEN];
    DWORD NetLuidIndex = 0, dwType, dwSize;
    ULONG64 NetLuid, InstallTimestamp;
    FILETIME Now;
    SYSTEMTIME NowSystem;
    LONG rc;

    if (!Keywords->Present)
        return ERROR_SUCCESS;

    if (!SetupDiGetDeviceInstanceIdW(DeviceInfoSet, DeviceInfoData, InstanceId, ARRAYSIZE(InstanceId), NULL))
        return GetLastError();

    hDriverKey = SetupDiOpenDevRegKey(DeviceInfoSet, DeviceInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ | KEY_WRITE);
    if (hDriverKey == INVALID_HANDLE_VALUE)
        return GetLastError();

    ComponentId[0] = UNICODE_NULL;
    dwSize = sizeof(ComponentId) - sizeof(WCHAR);
    if (RegQueryValueExW(hDriverKey, L"MatchingDeviceId", NULL, &dwType, (LPBYTE)ComponentId, &dwSize) != ERROR_SUCCESS || dwType != REG_SZ)
        ComponentId[0] = UNICODE_NULL;
    ComponentId[ARRAYSIZE(ComponentId) - 1] = UNICODE_NULL;

    IfDescr[0] = UNICODE_NULL;
    dwSize = sizeof(IfDescr) - sizeof(WCHAR);
    if (RegQueryValueExW(hDriverKey, L"DriverDesc", NULL, &dwType, (LPBYTE)IfDescr, &dwSize) != ERROR_SUCCESS || dwType != REG_SZ)
        IfDescr[0] = UNICODE_NULL;
    IfDescr[ARRAYSIZE(IfDescr) - 1] = UNICODE_NULL;

    rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, NETSETUP_INTERFACES_KEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_READ | KEY_CREATE_SUB_KEY, NULL, &hInterfacesKey, NULL);
    if (rc != ERROR_SUCCESS)
        goto cleanup;

    rc = RegCreateKeyExW(hInterfacesKey, UuidString, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_READ | KEY_CREATE_SUB_KEY, NULL, &hInterfaceKey, NULL);
    if (rc != ERROR_SUCCESS)
        goto cleanup;

    rc = RegCreateKeyExW(hInterfaceKey, L"Kernel", 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &hKernelKey, NULL);
    if (rc != ERROR_SUCCESS)
        goto cleanup;

    dwSize = sizeof(NetLuidIndex);
    if (RegQueryValueExW(hKernelKey, L"NetLuidIndex", NULL, &dwType, (LPBYTE)&NetLuidIndex, &dwSize) != ERROR_SUCCESS ||
        dwType != REG_DWORD || NetLuidIndex < NET_LUID_INDEX_FIRST || NetLuidIndex > NET_LUID_INDEX_LAST)
    {
        NetLuidIndex = AllocateNetLuidIndex(hInterfacesKey, UuidString, Keywords->IfType);
        if (NetLuidIndex == 0)
        {
            rc = ERROR_NO_MORE_ITEMS;
            goto cleanup;
        }
    }

    rc = RegCreateKeyExW(hInterfaceKey, L"Properties", 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_CREATE_SUB_KEY, NULL, &hPropertiesKey, NULL);
    if (rc != ERROR_SUCCESS)
        goto cleanup;

    NetLuid = ((ULONG64)(Keywords->IfType & 0xFFFF) << 48) | ((ULONG64)NetLuidIndex << 24);

    if ((rc = SetNetSetupProperty(hPropertiesKey, &NetSetupInterfaceFmtid, 0x0004, DEVPROP_TYPE_UINT64, &NetLuid, sizeof(NetLuid))) != ERROR_SUCCESS ||
        (rc = SetNetSetupProperty(hPropertiesKey, &NetSetupInterfaceFmtid, 0x000a, DEVPROP_TYPE_UINT32, &Keywords->IfType, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = SetNetSetupProperty(hPropertiesKey, &NetSetupInterfaceFmtid, 0x000c, DEVPROP_TYPE_UINT32, &Keywords->MediaType, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = SetNetSetupProperty(hPropertiesKey, &NetSetupInterfaceFmtid, 0x000e, DEVPROP_TYPE_UINT32, &Keywords->PhysicalMediaType, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = SetNetSetupStringProperty(hPropertiesKey, &NetSetupInterfaceFmtid, 0x0012, IfDescr)) != ERROR_SUCCESS ||
        (rc = SetNetSetupStringProperty(hPropertiesKey, &NetSetupInterfaceFmtid, 0x0032, InstanceId)) != ERROR_SUCCESS ||
        (rc = SetNetSetupProperty(hPropertiesKey, &NetSetupInfFmtid, 0x0006, DEVPROP_TYPE_UINT32, &Characteristics, sizeof(DWORD))) != ERROR_SUCCESS)
    {
        goto cleanup;
    }

    if (IfAlias && (rc = SetNetSetupStringProperty(hPropertiesKey, &NetSetupInterfaceFmtid, 0x0010, IfAlias)) != ERROR_SUCCESS)
        goto cleanup;
    if (ComponentId[0] && (rc = SetNetSetupStringProperty(hPropertiesKey, &NetSetupInterfaceFmtid, 0x0034, ComponentId)) != ERROR_SUCCESS)
        goto cleanup;
    if ((Characteristics & NCF_PHYSICAL) && (rc = SetNetSetupBooleanProperty(hPropertiesKey, &NetSetupInterfaceFmtid, 0x001e)) != ERROR_SUCCESS)
        goto cleanup;
    if ((Characteristics & NCF_VIRTUAL) && (rc = SetNetSetupBooleanProperty(hPropertiesKey, &NetSetupInterfaceFmtid, 0x0020)) != ERROR_SUCCESS)
        goto cleanup;
    if ((Characteristics & NCF_HIDDEN) && (rc = SetNetSetupBooleanProperty(hPropertiesKey, &NetSetupDriverFmtid, 0x0014)) != ERROR_SUCCESS)
        goto cleanup;

    if (RegOpenKeyExW(hDriverKey, L"Ndi\\Interfaces", 0, KEY_QUERY_VALUE, &hNdiKey) == ERROR_SUCCESS)
    {
        SetNetSetupRangeProperty(hPropertiesKey, hNdiKey, L"UpperRange", 0x0002);
        SetNetSetupRangeProperty(hPropertiesKey, hNdiKey, L"LowerRange", 0x0004);
    }

    GetSystemTime(&NowSystem);
    if ((rc = SetNetSetupProperty(hPropertiesKey, &NetSetupInfFmtid, 0x0028, DEVPROP_TYPE_BINARY, &NowSystem, sizeof(NowSystem))) != ERROR_SUCCESS)
        goto cleanup;

    if ((rc = RegSetValueExW(hKernelKey, L"IfType", 0, REG_DWORD, (const BYTE*)&Keywords->IfType, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = RegSetValueExW(hKernelKey, L"MediaType", 0, REG_DWORD, (const BYTE*)&Keywords->MediaType, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = RegSetValueExW(hKernelKey, L"PhysicalMediaType", 0, REG_DWORD, (const BYTE*)&Keywords->PhysicalMediaType, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = RegSetValueExW(hKernelKey, L"NetLuidIndex", 0, REG_DWORD, (const BYTE*)&NetLuidIndex, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = RegSetValueExW(hKernelKey, L"Characteristics", 0, REG_DWORD, (const BYTE*)&Characteristics, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = RegSetValueExW(hKernelKey, L"IfDescr", 0, REG_SZ, (const BYTE*)IfDescr, (wcslen(IfDescr) + 1) * sizeof(WCHAR))) != ERROR_SUCCESS ||
        (rc = RegSetValueExW(hKernelKey, L"ProtocolList", 0, REG_MULTI_SZ, (const BYTE*)L"Tcpip\0", (wcslen(L"Tcpip") + 2) * sizeof(WCHAR))) != ERROR_SUCCESS)
    {
        goto cleanup;
    }
    if (IfAlias && (rc = RegSetValueExW(hKernelKey, L"IfAlias", 0, REG_SZ, (const BYTE*)IfAlias, (wcslen(IfAlias) + 1) * sizeof(WCHAR))) != ERROR_SUCCESS)
        goto cleanup;

    if ((rc = RegSetValueExW(hDriverKey, L"*IfType", 0, REG_DWORD, (const BYTE*)&Keywords->IfType, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = RegSetValueExW(hDriverKey, L"*MediaType", 0, REG_DWORD, (const BYTE*)&Keywords->MediaType, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = RegSetValueExW(hDriverKey, L"*PhysicalMediaType", 0, REG_DWORD, (const BYTE*)&Keywords->PhysicalMediaType, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = RegSetValueExW(hDriverKey, L"IfTypePreStart", 0, REG_DWORD, (const BYTE*)&Keywords->IfType, sizeof(DWORD))) != ERROR_SUCCESS ||
        (rc = RegSetValueExW(hDriverKey, L"DeviceInstanceID", 0, REG_SZ, (const BYTE*)InstanceId, (wcslen(InstanceId) + 1) * sizeof(WCHAR))) != ERROR_SUCCESS ||
        (rc = RegSetValueExW(hDriverKey, L"NetLuidIndex", 0, REG_DWORD, (const BYTE*)&NetLuidIndex, sizeof(DWORD))) != ERROR_SUCCESS)
    {
        goto cleanup;
    }

    dwSize = sizeof(InstallTimestamp);
    if (RegQueryValueExW(hDriverKey, L"NetworkInterfaceInstallTimestamp", NULL, &dwType, (LPBYTE)&InstallTimestamp, &dwSize) != ERROR_SUCCESS ||
        dwType != REG_QWORD)
    {
        GetSystemTimeAsFileTime(&Now);
        InstallTimestamp = ((ULONG64)Now.dwHighDateTime << 32) | Now.dwLowDateTime;
        rc = RegSetValueExW(hDriverKey, L"NetworkInterfaceInstallTimestamp", 0, REG_QWORD, (const BYTE*)&InstallTimestamp, sizeof(InstallTimestamp));
    }

cleanup:
    if (hNdiKey != NULL)
        RegCloseKey(hNdiKey);
    if (hPropertiesKey != NULL)
        RegCloseKey(hPropertiesKey);
    if (hKernelKey != NULL)
        RegCloseKey(hKernelKey);
    if (hInterfaceKey != NULL)
        RegCloseKey(hInterfaceKey);
    if (hInterfacesKey != NULL)
        RegCloseKey(hInterfacesKey);
    RegCloseKey(hDriverKey);
    return rc;
}

static
DWORD
InstallNetDevice(
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData,
    LPCWSTR UuidString,
    DWORD Characteristics,
    LPCWSTR BusType,
    PCNET_INF_KEYWORDS Keywords)
{
    SP_DEVINSTALL_PARAMS_W DeviceInstallParams;
    LPWSTR InstanceId = NULL;
    LPWSTR ComponentId = NULL;
    LPWSTR DeviceName = NULL;
    LPWSTR ExportName = NULL;
    LONG rc;
    HKEY hInterfacesKey = NULL, hInterfaceKey = NULL;
    HKEY hKey = NULL;
    HKEY hNetworkKey = NULL;
    HKEY hLinkageKey = NULL;
    HKEY hConnectionKey = NULL;
    DWORD dwShowIcon, dwLength, dwValue;
    PWSTR pszNameBuffer = NULL;
    DWORD dwRegType;

    DeviceInstallParams.cbSize = sizeof(DeviceInstallParams);
    if (!SetupDiGetDeviceInstallParamsW(DeviceInfoSet,
                                        DeviceInfoData,
                                        &DeviceInstallParams))
    {
        rc = GetLastError();
        ERR("SetupDiGetDeviceInstallParamsW() failed (Error %lu)\n", rc);
        goto cleanup;
    }

    /* Do not start the adapter in the call to SetupDiInstallDevice */
    DeviceInstallParams.Flags |= DI_DONOTCALLCONFIGMG;

    if (!SetupDiSetDeviceInstallParamsW(DeviceInfoSet,
                                        DeviceInfoData,
                                        &DeviceInstallParams))
    {
        rc = GetLastError();
        ERR("SetupDiSetDeviceInstallParamsW() failed (Error %lu)\n", rc);
        goto cleanup;
    }

    /* Install the adapter */
    if (!SetupDiInstallDevice(DeviceInfoSet, DeviceInfoData))
    {
        rc = GetLastError();
        ERR("SetupDiInstallDevice() failed (Error %lu)\n", rc);
        goto cleanup;
    }

    /* Get Instance ID */
    if (SetupDiGetDeviceInstanceIdW(DeviceInfoSet, DeviceInfoData, NULL, 0, &dwLength))
    {
        ERR("SetupDiGetDeviceInstanceIdW() returned TRUE. FALSE expected\n");
        rc = ERROR_GEN_FAILURE;
        goto cleanup;
    }

    InstanceId = HeapAlloc(GetProcessHeap(), 0, dwLength * sizeof(WCHAR));
    if (!InstanceId)
    {
        ERR("HeapAlloc() failed\n");
        rc = ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }

    if (!SetupDiGetDeviceInstanceIdW(DeviceInfoSet, DeviceInfoData, InstanceId, dwLength, NULL))
    {
        rc = GetLastError();
        ERR("SetupDiGetDeviceInstanceIdW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    /* Create device name */
    DeviceName = HeapAlloc(GetProcessHeap(), 0, (wcslen(L"\\Device\\") + wcslen(UuidString)) * sizeof(WCHAR) + sizeof(UNICODE_NULL));
    if (!DeviceName)
    {
        ERR("HeapAlloc() failed\n");
        rc = ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }
    wcscpy(DeviceName, L"\\Device\\");
    wcscat(DeviceName, UuidString);

    /* Create export name */
    ExportName = HeapAlloc(GetProcessHeap(), 0, (wcslen(L"\\Device\\Tcpip_") + wcslen(UuidString)) * sizeof(WCHAR) + sizeof(UNICODE_NULL));
    if (!ExportName)
    {
        ERR("HeapAlloc() failed\n");
        rc = ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }
    wcscpy(ExportName, L"\\Device\\Tcpip_");
    wcscat(ExportName, UuidString);

    /* Write Tcpip parameters in new service Key */
    rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hInterfacesKey, NULL);
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegCreateKeyExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = RegCreateKeyExW(hInterfacesKey, UuidString, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hInterfaceKey, NULL);
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegCreateKeyExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = RegSetValueExW(hInterfaceKey, L"DefaultGateway", 0, REG_MULTI_SZ, (const BYTE*)L"0.0.0.0\0", (wcslen(L"0.0.0.0") + 2) * sizeof(WCHAR));
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = RegSetValueExW(hInterfaceKey, L"IPAddress", 0, REG_MULTI_SZ, (const BYTE*)L"0.0.0.0\0", (wcslen(L"0.0.0.0") + 2) * sizeof(WCHAR));
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = RegSetValueExW(hInterfaceKey, L"SubnetMask", 0, REG_MULTI_SZ, (const BYTE*)L"0.0.0.0\0", (wcslen(L"0.0.0.0") + 2) * sizeof(WCHAR));
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    dwValue = 1;
    rc = RegSetValueExW(hInterfaceKey, L"EnableDHCP", 0, REG_DWORD, (const BYTE*)&dwValue, sizeof(DWORD));
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    /* Write 'Linkage' key in hardware key */
#if _WIN32_WINNT >= 0x502
    hKey = SetupDiOpenDevRegKey(DeviceInfoSet, DeviceInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ | KEY_WRITE);
#else
    hKey = SetupDiOpenDevRegKey(DeviceInfoSet, DeviceInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_ALL_ACCESS);
#endif
    if (hKey == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND)
        hKey = SetupDiCreateDevRegKeyW(DeviceInfoSet, DeviceInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DRV, NULL, NULL);
    if (hKey == INVALID_HANDLE_VALUE)
    {
        hKey = NULL;
        rc = GetLastError();
        ERR("SetupDiCreateDevRegKeyW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = RegSetValueExW(hKey, L"NetCfgInstanceId", 0, REG_SZ, (const BYTE*)UuidString, (wcslen(UuidString) + 1) * sizeof(WCHAR));
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = RegSetValueExW(hKey, L"Characteristics", 0, REG_DWORD, (const BYTE*)&Characteristics, sizeof(DWORD));
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    /* Get ComponentId (from MatchingDeviceId value) */
    rc = RegQueryValueExW(hKey, L"MatchingDeviceId", 0, &dwRegType, NULL, &dwLength);
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegQueryValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }
    if (dwRegType != REG_SZ)
    {
        ERR("Invalid registry type for 'MatchingDeviceId'\n");
        rc = ERROR_GEN_FAILURE;
        goto cleanup;
    }
    ComponentId = HeapAlloc(GetProcessHeap(), 0, dwLength);
    if (!ComponentId)
    {
        ERR("HeapAlloc() failed\n");
        rc = ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }
    rc = RegQueryValueExW(hKey, L"MatchingDeviceId", 0, &dwRegType, (BYTE*)ComponentId, &dwLength);
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegQueryValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = RegSetValueExW(hKey, L"ComponentId", 0, REG_SZ, (const BYTE*)ComponentId, dwLength);
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    if (BusType)
    {
        rc = RegSetValueExW(hKey, L"BusType", 0, REG_SZ, (const BYTE*)BusType, (wcslen(BusType) + 1) * sizeof(WCHAR));
        if (rc != ERROR_SUCCESS)
        {
            ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
            goto cleanup;
        }
    }

    rc = RegCreateKeyExW(hKey, L"Linkage", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hLinkageKey, NULL);
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegCreateKeyExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = SetSingleMultiSzValue(hLinkageKey, L"Export", DeviceName);
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = SetSingleMultiSzValue(hLinkageKey, L"RootDevice", UuidString);
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = SetSingleMultiSzValue(hLinkageKey, L"UpperBind", L"Tcpip");
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }
    RegCloseKey(hKey);
    hKey = NULL;

    /* Write connection information in network subkey */
    rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                         L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}",
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         KEY_CREATE_SUB_KEY | KEY_QUERY_VALUE,
                         NULL,
                         &hNetworkKey,
                         NULL);
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegCreateKeyExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = GetUniqueConnectionName(hNetworkKey, &pszNameBuffer);
    if (rc != ERROR_SUCCESS)
    {
        ERR("GetUniqueConnectionName() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = RegCreateKeyExW(hNetworkKey, UuidString, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_CREATE_SUB_KEY, NULL, &hKey, NULL);
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegCreateKeyExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = RegCreateKeyExW(hKey, L"Connection", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hConnectionKey, NULL);
    RegCloseKey(hKey);
    hKey = NULL;
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegCreateKeyExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = RegSetValueExW(hConnectionKey, L"Name", 0, REG_SZ, (const BYTE*)pszNameBuffer, (wcslen(pszNameBuffer) + 1) * sizeof(WCHAR));
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = RegSetValueExW(hConnectionKey, L"PnpInstanceID", 0, REG_SZ, (const BYTE*)InstanceId, (wcslen(InstanceId) + 1) * sizeof(WCHAR));
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    dwShowIcon = 1;
    rc = RegSetValueExW(hConnectionKey, L"ShowIcon", 0, REG_DWORD, (const BYTE*)&dwShowIcon, sizeof(dwShowIcon));
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    /* Write linkage information in Tcpip service */
    rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Linkage", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &hKey, NULL);
    if (rc != ERROR_SUCCESS)
    {
        ERR("RegCreateKeyExW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }
    rc = AppendStringToMultiSZ(hKey, L"Bind", DeviceName);
    if (rc != ERROR_SUCCESS)
    {
        ERR("AppendStringToMultiSZ() failed with error 0x%lx\n", rc);
        goto cleanup;
    }
    rc = AppendStringToMultiSZ(hKey, L"Export", ExportName);
    if (rc != ERROR_SUCCESS)
    {
        ERR("AppendStringToMultiSZ() failed with error 0x%lx\n", rc);
        goto cleanup;
    }
    rc = AppendStringToMultiSZ(hKey, L"Route", UuidString);
    if (rc != ERROR_SUCCESS)
    {
        ERR("AppendStringToMultiSZ() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = WriteNetSetupInterface(DeviceInfoSet, DeviceInfoData, UuidString, Keywords, Characteristics, pszNameBuffer);
    if (rc != ERROR_SUCCESS)
    {
        ERR("WriteNetSetupInterface() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    /* Start the device */
    if (!SetupDiRestartDevices(DeviceInfoSet, DeviceInfoData))
    {
        rc = GetLastError();
        ERR("SetupDiRestartDevices() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    rc = ERROR_SUCCESS;

cleanup:
    HeapFree(GetProcessHeap(), 0, pszNameBuffer);
    HeapFree(GetProcessHeap(), 0, InstanceId);
    HeapFree(GetProcessHeap(), 0, ComponentId);
    HeapFree(GetProcessHeap(), 0, DeviceName);
    HeapFree(GetProcessHeap(), 0, ExportName);
    if (hInterfaceKey != NULL)
        RegCloseKey(hInterfaceKey);
    if (hInterfacesKey != NULL)
        RegCloseKey(hInterfacesKey);
    if (hKey != NULL)
        RegCloseKey(hKey);
    if (hNetworkKey != NULL)
        RegCloseKey(hNetworkKey);
    if (hLinkageKey != NULL)
        RegCloseKey(hLinkageKey);
    if (hConnectionKey != NULL)
        RegCloseKey(hConnectionKey);

    return rc;
}

static
DWORD
ReinstallNetDevice(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ PCWSTR UuidString,
    _In_ DWORD Characteristics,
    _In_ PCNET_INF_KEYWORDS Keywords)
{
    WCHAR ConnectionKey[160];
    WCHAR IfAlias[LINE_LEN];
    DWORD dwType, dwSize = sizeof(IfAlias) - sizeof(WCHAR);
    PCWSTR Alias = NULL;
    HKEY hKey;
    DWORD rc;

    ZeroMemory(IfAlias, sizeof(IfAlias));
    StringCchPrintfW(ConnectionKey, ARRAYSIZE(ConnectionKey),
                     L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\%s\\Connection",
                     UuidString);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ConnectionKey, 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(hKey, L"Name", NULL, &dwType, (LPBYTE)IfAlias, &dwSize) == ERROR_SUCCESS && dwType == REG_SZ)
            Alias = IfAlias;
        RegCloseKey(hKey);
    }

    rc = WriteNetSetupInterface(DeviceInfoSet, DeviceInfoData, UuidString, Keywords, Characteristics, Alias);
    if (rc != ERROR_SUCCESS)
    {
        ERR("WriteNetSetupInterface() failed with error 0x%lx\n", rc);
        return rc;
    }

    return SetupDiInstallDevice(DeviceInfoSet, DeviceInfoData) ? ERROR_SUCCESS : GetLastError();
}

static
DWORD
InstallNetClient(VOID)
{
    FIXME("Installation of network clients is not yet supported\n");
    return ERROR_GEN_FAILURE;
}

static
DWORD
InstallNetService(VOID)
{
    FIXME("Installation of network services is not yet supported\n");
    return ERROR_GEN_FAILURE;
}

static
DWORD
InstallNetTransport(VOID)
{
    FIXME("Installation of network protocols is not yet supported\n");
    return ERROR_GEN_FAILURE;
}

DWORD
WINAPI
NetClassInstaller(
    IN DI_FUNCTION InstallFunction,
    IN HDEVINFO DeviceInfoSet,
    IN PSP_DEVINFO_DATA DeviceInfoData OPTIONAL)
{
    SP_DRVINFO_DATA_W DriverInfoData;
    SP_DRVINFO_DETAIL_DATA_W DriverInfoDetail;
    WCHAR SectionName[LINE_LEN];
    HINF hInf = INVALID_HANDLE_VALUE;
    INFCONTEXT InfContext;
    UINT ErrorLine;
    INT CharacteristicsInt;
    DWORD Characteristics;
    LPWSTR BusType = NULL;
    NET_INF_KEYWORDS Keywords;
    INT KeywordValue;
    RPC_STATUS RpcStatus;
    UUID Uuid;
    LPWSTR UuidRpcString = NULL;
    LPWSTR UuidString = NULL;
    HKEY hDriverKey;
    LONG rc;
    DWORD dwLength;

    if (InstallFunction != DIF_INSTALLDEVICE)
        return ERROR_DI_DO_DEFAULT;

    TRACE("%lu %p %p\n", InstallFunction, DeviceInfoSet, DeviceInfoData);

    /* Get driver info details */
    DriverInfoData.cbSize = sizeof(SP_DRVINFO_DATA_W);
    if (!SetupDiGetSelectedDriverW(DeviceInfoSet, DeviceInfoData, &DriverInfoData))
    {
        rc = GetLastError();
        ERR("SetupDiGetSelectedDriverW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    DriverInfoDetail.cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);
    if (!SetupDiGetDriverInfoDetailW(DeviceInfoSet, DeviceInfoData, &DriverInfoData, &DriverInfoDetail, sizeof(DriverInfoDetail), NULL)
     && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        rc = GetLastError();
        ERR("SetupDiGetDriverInfoDetailW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    hInf = SetupOpenInfFileW(DriverInfoDetail.InfFileName, NULL, INF_STYLE_WIN4, &ErrorLine);
    if (hInf == INVALID_HANDLE_VALUE)
    {
        rc = GetLastError();
        ERR("SetupOpenInfFileW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    if (!SetupDiGetActualSectionToInstallW(hInf, DriverInfoDetail.SectionName, SectionName, LINE_LEN, NULL, NULL))
    {
        rc = GetLastError();
        ERR("SetupDiGetActualSectionToInstallW() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    /* Get Characteristics and BusType (optional) from .inf file */
    if (!SetupFindFirstLineW(hInf, SectionName, L"Characteristics", &InfContext))
    {
        rc = GetLastError();
        ERR("Unable to find key %S in section %S of file %S (error 0x%lx)\n",
            L"Characteristics", SectionName, DriverInfoDetail.InfFileName, rc);
        goto cleanup;
    }

    if (!SetupGetIntField(&InfContext, 1, &CharacteristicsInt))
    {
        rc = GetLastError();
        ERR("SetupGetIntField() failed with error 0x%lx\n", rc);
        goto cleanup;
    }

    Characteristics = (DWORD)CharacteristicsInt;
    ZeroMemory(&Keywords, sizeof(Keywords));
    if (IsEqualIID(&DeviceInfoData->ClassGuid, &GUID_DEVCLASS_NET))
    {
        Keywords.Present = TRUE;
        if (SetupFindFirstLineW(hInf, SectionName, L"*IfType", &InfContext) && SetupGetIntField(&InfContext, 1, &KeywordValue))
            Keywords.IfType = (DWORD)KeywordValue;
        else
            Keywords.Present = FALSE;
        if (SetupFindFirstLineW(hInf, SectionName, L"*MediaType", &InfContext) && SetupGetIntField(&InfContext, 1, &KeywordValue))
            Keywords.MediaType = (DWORD)KeywordValue;
        else
            Keywords.Present = FALSE;
        if (SetupFindFirstLineW(hInf, SectionName, L"*PhysicalMediaType", &InfContext) && SetupGetIntField(&InfContext, 1, &KeywordValue))
            Keywords.PhysicalMediaType = (DWORD)KeywordValue;
        else
            Keywords.Present = FALSE;

        if (SetupFindFirstLineW(hInf, SectionName, L"BusType", &InfContext))
        {
            if (!SetupGetStringFieldW(&InfContext, 1, NULL, 0, &dwLength))
            {
                rc = GetLastError();
                ERR("SetupGetStringFieldW() failed with error 0x%lx\n", rc);
                goto cleanup;
            }

            BusType = HeapAlloc(GetProcessHeap(), 0, dwLength * sizeof(WCHAR));
            if (!BusType)
            {
                ERR("HeapAlloc() failed\n");
                rc = ERROR_NOT_ENOUGH_MEMORY;
                goto cleanup;
            }

            if (!SetupGetStringFieldW(&InfContext, 1, BusType, dwLength, NULL))
            {
                rc = GetLastError();
                ERR("SetupGetStringFieldW() failed with error 0x%lx\n", rc);
                goto cleanup;
            }
        }
    }

    if (IsEqualIID(&DeviceInfoData->ClassGuid, &GUID_DEVCLASS_NET))
    {
        hDriverKey = SetupDiOpenDevRegKey(DeviceInfoSet, DeviceInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
        if (hDriverKey != INVALID_HANDLE_VALUE)
        {
            WCHAR InstanceGuid[40];
            DWORD dwType;

            dwLength = sizeof(InstanceGuid) - sizeof(WCHAR);
            ZeroMemory(InstanceGuid, sizeof(InstanceGuid));
            rc = RegQueryValueExW(hDriverKey, L"NetCfgInstanceId", NULL, &dwType, (LPBYTE)InstanceGuid, &dwLength);
            RegCloseKey(hDriverKey);
            if (rc == ERROR_SUCCESS && dwType == REG_SZ)
            {
                rc = ReinstallNetDevice(DeviceInfoSet, DeviceInfoData, InstanceGuid, Characteristics, &Keywords);
                goto cleanup;
            }
        }
    }

    /* Create a new UUID */
    RpcStatus = UuidCreate(&Uuid);
    if (RpcStatus != RPC_S_OK && RpcStatus != RPC_S_UUID_LOCAL_ONLY)
    {
        ERR("UuidCreate() failed with RPC status 0x%lx\n", RpcStatus);
        rc = ERROR_GEN_FAILURE;
        goto cleanup;
    }

    RpcStatus = UuidToStringW(&Uuid, &UuidRpcString);
    if (RpcStatus != RPC_S_OK)
    {
        ERR("UuidToStringW() failed with RPC status 0x%lx\n", RpcStatus);
        rc = ERROR_GEN_FAILURE;
        goto cleanup;
    }

    /* Add curly braces around Uuid */
    UuidString = HeapAlloc(GetProcessHeap(), 0, (2 + wcslen(UuidRpcString)) * sizeof(WCHAR) + sizeof(UNICODE_NULL));
    if (!UuidString)
    {
        ERR("HeapAlloc() failed\n");
        rc = ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }

    wcscpy(UuidString, L"{");
    wcscat(UuidString, UuidRpcString);
    wcscat(UuidString, L"}");

    if (IsEqualIID(&DeviceInfoData->ClassGuid, &GUID_DEVCLASS_NET))
        rc = InstallNetDevice(DeviceInfoSet, DeviceInfoData, UuidString, Characteristics, BusType, &Keywords);
    else if (IsEqualIID(&DeviceInfoData->ClassGuid, &GUID_DEVCLASS_NETCLIENT))
        rc = InstallNetClient();
    else if (IsEqualIID(&DeviceInfoData->ClassGuid, &GUID_DEVCLASS_NETSERVICE))
        rc = InstallNetService();
    else if (IsEqualIID(&DeviceInfoData->ClassGuid, &GUID_DEVCLASS_NETTRANS))
        rc = InstallNetTransport();
    else
    {
        ERR("Invalid class guid\n");
        rc = ERROR_GEN_FAILURE;
    }

cleanup:
    if (hInf != INVALID_HANDLE_VALUE)
        SetupCloseInfFile(hInf);
    if (UuidRpcString != NULL)
        RpcStringFreeW(&UuidRpcString);
    HeapFree(GetProcessHeap(), 0, BusType);
    HeapFree(GetProcessHeap(), 0, UuidString);

    TRACE("Returning 0x%lx\n", rc);
    return rc;
}
