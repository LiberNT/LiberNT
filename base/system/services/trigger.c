/*
 * PROJECT:     LiberNT Service Control Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Service trigger configuration (SERVICE_CONFIG_TRIGGER_INFO)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "services.h"

#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <strsafe.h>

#define NDEBUG
#include <debug.h>

#define SCM_MAX_TRIGGERS 64
#define SCM_MAX_TRIGGER_DATA_ITEMS 64
#define SCM_MAX_TRIGGER_DATA 1024

static BOOL
ScmIsValidTriggerType(DWORD dwTriggerType)
{
    switch (dwTriggerType)
    {
        case SERVICE_TRIGGER_TYPE_DEVICE_INTERFACE_ARRIVAL:
        case SERVICE_TRIGGER_TYPE_IP_ADDRESS_AVAILABILITY:
        case SERVICE_TRIGGER_TYPE_DOMAIN_JOIN:
        case SERVICE_TRIGGER_TYPE_FIREWALL_PORT_EVENT:
        case SERVICE_TRIGGER_TYPE_GROUP_POLICY:
        case SERVICE_TRIGGER_TYPE_NETWORK_ENDPOINT:
        case SERVICE_TRIGGER_TYPE_CUSTOM_SYSTEM_STATE_CHANGE:
        case SERVICE_TRIGGER_TYPE_CUSTOM:
        case SERVICE_TRIGGER_TYPE_AGGREGATE:
            return TRUE;
        default:
            return FALSE;
    }
}

static DWORD
ScmValidateTriggers(const SERVICE_TRIGGER_INFO *lpInfo)
{
    DWORD i, j;

    if (lpInfo->pReserved != NULL || lpInfo->cTriggers > SCM_MAX_TRIGGERS ||
        (lpInfo->cTriggers != 0 && lpInfo->pTriggers == NULL))
        return ERROR_INVALID_PARAMETER;

    for (i = 0; i < lpInfo->cTriggers; i++)
    {
        const SERVICE_TRIGGER *lpTrigger = &lpInfo->pTriggers[i];

        if (!ScmIsValidTriggerType(lpTrigger->dwTriggerType) ||
            (lpTrigger->dwAction != SERVICE_TRIGGER_ACTION_SERVICE_START &&
             lpTrigger->dwAction != SERVICE_TRIGGER_ACTION_SERVICE_STOP) ||
            lpTrigger->pTriggerSubtype == NULL ||
            lpTrigger->cDataItems > SCM_MAX_TRIGGER_DATA_ITEMS ||
            (lpTrigger->cDataItems != 0 && lpTrigger->pDataItems == NULL))
            return ERROR_INVALID_PARAMETER;

        for (j = 0; j < lpTrigger->cDataItems; j++)
        {
            const SERVICE_TRIGGER_SPECIFIC_DATA_ITEM *lpItem = &lpTrigger->pDataItems[j];

            if (lpItem->dwDataType < SERVICE_TRIGGER_DATA_TYPE_BINARY ||
                lpItem->dwDataType > SERVICE_TRIGGER_DATA_TYPE_KEYWORD_ALL ||
                lpItem->cbData > SCM_MAX_TRIGGER_DATA ||
                (lpItem->cbData != 0 && lpItem->pData == NULL))
                return ERROR_INVALID_PARAMETER;
        }
    }
    return ERROR_SUCCESS;
}

static DWORD
ScmWriteTrigger(HKEY hTriggersKey, DWORD dwIndex, const SERVICE_TRIGGER *lpTrigger)
{
    WCHAR szName[16];
    HKEY hTriggerKey;
    DWORD dwError, j;

    StringCchPrintfW(szName, ARRAYSIZE(szName), L"%lu", dwIndex);
    dwError = RegCreateKeyExW(hTriggersKey, szName, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL,
                              &hTriggerKey, NULL);
    if (dwError != ERROR_SUCCESS)
        return dwError;

    dwError = RegSetValueExW(hTriggerKey, L"Type", 0, REG_DWORD, (LPBYTE)&lpTrigger->dwTriggerType, sizeof(DWORD));
    if (dwError == ERROR_SUCCESS)
        dwError = RegSetValueExW(hTriggerKey, L"Action", 0, REG_DWORD, (LPBYTE)&lpTrigger->dwAction, sizeof(DWORD));
    if (dwError == ERROR_SUCCESS)
        dwError = RegSetValueExW(hTriggerKey, L"GUID", 0, REG_BINARY, (LPBYTE)lpTrigger->pTriggerSubtype, sizeof(GUID));
    for (j = 0; dwError == ERROR_SUCCESS && j < lpTrigger->cDataItems; j++)
    {
        const SERVICE_TRIGGER_SPECIFIC_DATA_ITEM *lpItem = &lpTrigger->pDataItems[j];

        StringCchPrintfW(szName, ARRAYSIZE(szName), L"Data%lu", j);
        dwError = RegSetValueExW(hTriggerKey, szName, 0, REG_BINARY, lpItem->pData, lpItem->cbData);
        if (dwError == ERROR_SUCCESS)
        {
            StringCchPrintfW(szName, ARRAYSIZE(szName), L"DataType%lu", j);
            dwError = RegSetValueExW(hTriggerKey, szName, 0, REG_DWORD, (LPBYTE)&lpItem->dwDataType, sizeof(DWORD));
        }
    }
    RegCloseKey(hTriggerKey);
    return dwError;
}

DWORD
ScmSetServiceTriggers(HKEY hServiceKey, const SERVICE_TRIGGER_INFO *lpInfo)
{
    HKEY hTriggersKey;
    DWORD dwError, i;

    if (lpInfo == NULL)
        return ERROR_SUCCESS;
    dwError = ScmValidateTriggers(lpInfo);
    if (dwError != ERROR_SUCCESS)
        return dwError;

    dwError = RegDeleteTreeW(hServiceKey, L"TriggerInfo");
    if (dwError != ERROR_SUCCESS && dwError != ERROR_FILE_NOT_FOUND)
        return dwError;
    if (lpInfo->cTriggers == 0)
        return ERROR_SUCCESS;

    dwError = RegCreateKeyExW(hServiceKey, L"TriggerInfo", 0, NULL, REG_OPTION_NON_VOLATILE,
                              KEY_CREATE_SUB_KEY | KEY_SET_VALUE, NULL, &hTriggersKey, NULL);
    if (dwError != ERROR_SUCCESS)
        return dwError;
    for (i = 0; dwError == ERROR_SUCCESS && i < lpInfo->cTriggers; i++)
        dwError = ScmWriteTrigger(hTriggersKey, i, &lpInfo->pTriggers[i]);
    RegCloseKey(hTriggersKey);
    return dwError;
}

static DWORD
ScmQueryDword(HKEY hKey, LPCWSTR lpName, LPDWORD lpValue)
{
    DWORD dwType, dwSize = sizeof(DWORD);
    DWORD dwError = RegQueryValueExW(hKey, lpName, NULL, &dwType, (LPBYTE)lpValue, &dwSize);

    if (dwError == ERROR_SUCCESS && (dwType != REG_DWORD || dwSize != sizeof(DWORD)))
        dwError = ERROR_INVALID_DATA;
    return dwError;
}

static SIZE_T
ScmAlignUp(SIZE_T cbOffset)
{
    return (cbOffset + sizeof(ULONG_PTR) - 1) & ~(SIZE_T)(sizeof(ULONG_PTR) - 1);
}

/* Number of data items of a stored trigger and the bytes their data take. */
static DWORD
ScmMeasureTrigger(HKEY hTriggerKey, LPDWORD lpItems, PSIZE_T lpDataBytes)
{
    WCHAR szName[16];
    DWORD dwSize, dwType;

    for (*lpItems = 0, *lpDataBytes = 0; *lpItems < SCM_MAX_TRIGGER_DATA_ITEMS; (*lpItems)++)
    {
        StringCchPrintfW(szName, ARRAYSIZE(szName), L"Data%lu", *lpItems);
        dwSize = 0;
        if (RegQueryValueExW(hTriggerKey, szName, NULL, &dwType, NULL, &dwSize) != ERROR_SUCCESS)
            break;
        if (dwSize > SCM_MAX_TRIGGER_DATA)
            return ERROR_INVALID_DATA;
        *lpDataBytes += dwSize;
    }
    return ERROR_SUCCESS;
}

static DWORD
ScmReadTrigger(HKEY hTriggerKey, LPBYTE lpBuffer, PSERVICE_TRIGGER lpTrigger, GUID *lpGuid,
               PSERVICE_TRIGGER_SPECIFIC_DATA_ITEM lpItems, DWORD dwItems, LPBYTE *lppData)
{
    WCHAR szName[16];
    DWORD dwSize = sizeof(GUID), dwType, dwError, j;

    dwError = ScmQueryDword(hTriggerKey, L"Type", &lpTrigger->dwTriggerType);
    if (dwError == ERROR_SUCCESS)
        dwError = ScmQueryDword(hTriggerKey, L"Action", &lpTrigger->dwAction);
    if (dwError == ERROR_SUCCESS)
        dwError = RegQueryValueExW(hTriggerKey, L"GUID", NULL, &dwType, (LPBYTE)lpGuid, &dwSize);
    if (dwError != ERROR_SUCCESS || dwSize != sizeof(GUID))
        return dwError != ERROR_SUCCESS ? dwError : ERROR_INVALID_DATA;

    lpTrigger->pTriggerSubtype = (GUID *)((LPBYTE)lpGuid - lpBuffer);
    lpTrigger->cDataItems = dwItems;
    lpTrigger->pDataItems = dwItems ? (PSERVICE_TRIGGER_SPECIFIC_DATA_ITEM)((LPBYTE)lpItems - lpBuffer) : NULL;
    for (j = 0; j < dwItems; j++)
    {
        StringCchPrintfW(szName, ARRAYSIZE(szName), L"DataType%lu", j);
        dwError = ScmQueryDword(hTriggerKey, szName, &lpItems[j].dwDataType);
        if (dwError != ERROR_SUCCESS)
            return dwError;
        StringCchPrintfW(szName, ARRAYSIZE(szName), L"Data%lu", j);
        dwSize = SCM_MAX_TRIGGER_DATA;
        dwError = RegQueryValueExW(hTriggerKey, szName, NULL, &dwType, *lppData, &dwSize);
        if (dwError != ERROR_SUCCESS)
            return dwError;
        lpItems[j].cbData = dwSize;
        lpItems[j].pData = dwSize ? (PBYTE)(*lppData - lpBuffer) : NULL;
        *lppData += dwSize;
    }
    return ERROR_SUCCESS;
}

/* Opens the stored triggers; *lpCount is 0 when the service has none. */
static DWORD
ScmOpenTriggers(HKEY hServiceKey, PHKEY lphTriggersKey, LPDWORD lpCount)
{
    DWORD dwError;

    *lphTriggersKey = NULL;
    *lpCount = 0;
    dwError = RegOpenKeyExW(hServiceKey, L"TriggerInfo", 0, KEY_READ, lphTriggersKey);
    if (dwError == ERROR_FILE_NOT_FOUND)
        return ERROR_SUCCESS;
    if (dwError != ERROR_SUCCESS)
        return dwError;
    dwError = RegQueryInfoKeyW(*lphTriggersKey, NULL, NULL, NULL, lpCount, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (dwError == ERROR_SUCCESS)
        *lpCount = min(*lpCount, SCM_MAX_TRIGGERS);
    return dwError;
}

static DWORD
ScmOpenTrigger(HKEY hTriggersKey, DWORD dwIndex, PHKEY lphTriggerKey)
{
    WCHAR szName[16];

    StringCchPrintfW(szName, ARRAYSIZE(szName), L"%lu", dwIndex);
    return RegOpenKeyExW(hTriggersKey, szName, 0, KEY_QUERY_VALUE, lphTriggerKey);
}

/* A self-relative SERVICE_TRIGGER_INFO whose pointers are offsets from lpBuffer. */
DWORD
ScmQueryServiceTriggers(HKEY hServiceKey, LPBYTE lpBuffer, DWORD cbBufSize, LPDWORD pcbBytesNeeded)
{
    DWORD dwItems[SCM_MAX_TRIGGERS], dwTotalItems = 0, dwCount, dwError, i;
    SIZE_T cbData = 0, cbItems, cbNeeded, cbOne;
    PSERVICE_TRIGGER_INFO lpInfo = (PSERVICE_TRIGGER_INFO)lpBuffer;
    PSERVICE_TRIGGER lpTriggers;
    PSERVICE_TRIGGER_SPECIFIC_DATA_ITEM lpItems;
    GUID *lpGuids;
    LPBYTE lpData;
    HKEY hTriggersKey, hTriggerKey;

    dwError = ScmOpenTriggers(hServiceKey, &hTriggersKey, &dwCount);
    if (dwError != ERROR_SUCCESS)
        return dwError;
    for (i = 0; i < dwCount && dwError == ERROR_SUCCESS; i++)
    {
        dwError = ScmOpenTrigger(hTriggersKey, i, &hTriggerKey);
        if (dwError != ERROR_SUCCESS)
            break;
        dwError = ScmMeasureTrigger(hTriggerKey, &dwItems[i], &cbOne);
        RegCloseKey(hTriggerKey);
        dwTotalItems += dwItems[i];
        cbData += cbOne;
    }
    if (dwError != ERROR_SUCCESS)
    {
        if (hTriggersKey != NULL)
            RegCloseKey(hTriggersKey);
        return dwError;
    }

    cbItems = ScmAlignUp(sizeof(SERVICE_TRIGGER_INFO)) + ScmAlignUp(dwCount * sizeof(SERVICE_TRIGGER)) +
              ScmAlignUp(dwCount * sizeof(GUID));
    cbNeeded = cbItems + ScmAlignUp(dwTotalItems * sizeof(SERVICE_TRIGGER_SPECIFIC_DATA_ITEM)) + cbData;
    *pcbBytesNeeded = (DWORD)cbNeeded;
    if (cbBufSize < cbNeeded)
    {
        if (hTriggersKey != NULL)
            RegCloseKey(hTriggersKey);
        return ERROR_INSUFFICIENT_BUFFER;
    }

    ZeroMemory(lpBuffer, cbNeeded);
    lpTriggers = (PSERVICE_TRIGGER)(lpBuffer + ScmAlignUp(sizeof(SERVICE_TRIGGER_INFO)));
    lpGuids = (GUID *)((LPBYTE)lpTriggers + ScmAlignUp(dwCount * sizeof(SERVICE_TRIGGER)));
    lpItems = (PSERVICE_TRIGGER_SPECIFIC_DATA_ITEM)(lpBuffer + cbItems);
    lpData = (LPBYTE)lpItems + ScmAlignUp(dwTotalItems * sizeof(SERVICE_TRIGGER_SPECIFIC_DATA_ITEM));
    lpInfo->cTriggers = dwCount;
    lpInfo->pTriggers = dwCount ? (PSERVICE_TRIGGER)((LPBYTE)lpTriggers - lpBuffer) : NULL;
    lpInfo->pReserved = NULL;
    for (i = 0; i < dwCount && dwError == ERROR_SUCCESS; i++)
    {
        dwError = ScmOpenTrigger(hTriggersKey, i, &hTriggerKey);
        if (dwError != ERROR_SUCCESS)
            break;
        dwError = ScmReadTrigger(hTriggerKey, lpBuffer, &lpTriggers[i], &lpGuids[i], lpItems, dwItems[i], &lpData);
        RegCloseKey(hTriggerKey);
        lpItems += dwItems[i];
    }
    if (hTriggersKey != NULL)
        RegCloseKey(hTriggersKey);
    return dwError;
}


/* ------------------------------------------------------------------------ *
 * Trigger events
 * ------------------------------------------------------------------------ */

/* TODO: SERVICE_TRIGGER_TYPE_CUSTOM needs real-time ETW delivery to the SCM; custom triggers do not fire yet */
/* TODO: network endpoint, firewall port, domain, group policy, custom system state and aggregate triggers have no event source */

typedef struct _SCM_TRIGGER_EVENT
{
    DWORD dwTriggerType;
    GUID Subtype;
    LPCWSTR lpDeviceIds;    /* hardware and compatible IDs of an arriving device interface, or NULL */
} SCM_TRIGGER_EVENT, *PSCM_TRIGGER_EVENT;

typedef struct _SCM_TRIGGER_HIT
{
    PSERVICE lpService;
    DWORD dwAction;
} SCM_TRIGGER_HIT;

static const GUID ScmFirstIpGuid = { 0x4f27f2de, 0x14e2, 0x430b, { 0xa5, 0x49, 0x7c, 0xd4, 0x8c, 0xbc, 0x82, 0x45 } };
static const GUID ScmLastIpGuid = { 0xcc4ba62a, 0x162e, 0x4648, { 0x84, 0x7a, 0xb6, 0xbd, 0xf9, 0x93, 0xe3, 0x35 } };
static CRITICAL_SECTION ScmTriggerLock;
static GUID ScmTriggerClasses[SCM_MAX_TRIGGERS];
static HCMNOTIFICATION ScmTriggerClassNotify[SCM_MAX_TRIGGERS];
static DWORD ScmTriggerClassCount;
static HANDLE ScmIpNotify;
static LONG ScmIpAvailable = -1;

static BOOL
ScmMultiStringContains(LPCWSTR lpList, LPCWSTR lpString)
{
    for (; lpList != NULL && *lpList != UNICODE_NULL; lpList += wcslen(lpList) + 1)
    {
        if (_wcsicmp(lpList, lpString) == 0)
            return TRUE;
    }
    return FALSE;
}

/* A device trigger names hardware or compatible IDs; it fires for any of them, or for any device without them. */
static BOOL
ScmDeviceIdsMatch(HKEY hTriggerKey, LPCWSTR lpDeviceIds)
{
    WCHAR Data[SCM_MAX_TRIGGER_DATA / sizeof(WCHAR) + 2];
    WCHAR szName[16];
    DWORD dwDataType, cbData, j;
    LPCWSTR p;
    BOOL bNamesIds = FALSE;

    for (j = 0; j < SCM_MAX_TRIGGER_DATA_ITEMS; j++)
    {
        StringCchPrintfW(szName, ARRAYSIZE(szName), L"DataType%lu", j);
        if (ScmQueryDword(hTriggerKey, szName, &dwDataType) != ERROR_SUCCESS)
            break;
        StringCchPrintfW(szName, ARRAYSIZE(szName), L"Data%lu", j);
        ZeroMemory(Data, sizeof(Data));
        cbData = SCM_MAX_TRIGGER_DATA;
        if (RegQueryValueExW(hTriggerKey, szName, NULL, NULL, (LPBYTE)Data, &cbData) != ERROR_SUCCESS)
            break;
        if (dwDataType != SERVICE_TRIGGER_DATA_TYPE_STRING)
            continue;
        bNamesIds = TRUE;
        for (p = Data; *p != UNICODE_NULL; p += wcslen(p) + 1)
        {
            if (ScmMultiStringContains(lpDeviceIds, p))
                return TRUE;
        }
    }
    return !bNamesIds;
}

static BOOL
ScmTriggerMatches(HKEY hTriggerKey, const SCM_TRIGGER_EVENT *lpEvent, LPDWORD lpAction)
{
    DWORD dwType, cbGuid = sizeof(GUID);
    GUID Subtype;

    if (ScmQueryDword(hTriggerKey, L"Type", &dwType) != ERROR_SUCCESS || dwType != lpEvent->dwTriggerType ||
        ScmQueryDword(hTriggerKey, L"Action", lpAction) != ERROR_SUCCESS ||
        RegQueryValueExW(hTriggerKey, L"GUID", NULL, NULL, (LPBYTE)&Subtype, &cbGuid) != ERROR_SUCCESS ||
        cbGuid != sizeof(GUID) || !IsEqualGUID(&Subtype, &lpEvent->Subtype))
        return FALSE;
    return lpEvent->lpDeviceIds == NULL || ScmDeviceIdsMatch(hTriggerKey, lpEvent->lpDeviceIds);
}

/* The action of the first trigger of the service that matches the event, or 0. */
static DWORD
ScmServiceTriggerAction(PSERVICE lpService, const SCM_TRIGGER_EVENT *lpEvent)
{
    HKEY hServiceKey, hTriggersKey, hTriggerKey;
    DWORD dwCount = 0, dwAction = 0, dwMatched = 0, i;

    if (ScmOpenServiceKey(lpService->lpServiceName, KEY_READ, &hServiceKey) != ERROR_SUCCESS)
        return 0;
    if (ScmOpenTriggers(hServiceKey, &hTriggersKey, &dwCount) == ERROR_SUCCESS)
    {
        for (i = 0; i < dwCount && dwMatched == 0; i++)
        {
            if (ScmOpenTrigger(hTriggersKey, i, &hTriggerKey) != ERROR_SUCCESS)
                continue;
            if (ScmTriggerMatches(hTriggerKey, lpEvent, &dwAction))
                dwMatched = dwAction;
            RegCloseKey(hTriggerKey);
        }
        if (hTriggersKey != NULL)
            RegCloseKey(hTriggersKey);
    }
    RegCloseKey(hServiceKey);
    return dwMatched;
}

static VOID
ScmRunTriggerAction(PSERVICE lpService, DWORD dwAction)
{
    static PCWSTR TriggerArguments[] = { SERVICE_TRIGGER_STARTED_ARGUMENT };
    DWORD dwError;

    if (dwAction == SERVICE_TRIGGER_ACTION_SERVICE_START)
    {
        if (lpService->dwStartType == SERVICE_DISABLED || lpService->Status.dwCurrentState != SERVICE_STOPPED)
            return;
        dwError = ScmStartService(lpService, ARRAYSIZE(TriggerArguments), TriggerArguments);
        if (dwError != ERROR_SUCCESS && dwError != ERROR_SERVICE_ALREADY_RUNNING)
            DPRINT1("Trigger start of %S failed (Error %lu)\n", lpService->lpServiceName, dwError);
        return;
    }

    if (lpService->Status.dwCurrentState != SERVICE_RUNNING || lpService->lpImage == NULL ||
        !(lpService->Status.dwControlsAccepted & SERVICE_ACCEPT_STOP))
        return;
    dwError = ScmControlService(lpService->lpImage->hControlPipe, lpService->lpServiceName,
                                SERVICE_CONTROL_STOP, lpService->dwResumeCount);
    if (dwError != ERROR_SUCCESS)
        DPRINT1("Trigger stop of %S failed (Error %lu)\n", lpService->lpServiceName, dwError);
}

static VOID
ScmFireTriggerEvent(const SCM_TRIGGER_EVENT *lpEvent)
{
    SCM_TRIGGER_HIT Hits[64];
    DWORD dwHits = 0, i;
    PLIST_ENTRY Entry;

    if (ScmShutdown)
        return;

    ScmLockDatabaseShared();
    for (Entry = ServiceListHead.Flink; Entry != &ServiceListHead && dwHits < ARRAYSIZE(Hits); Entry = Entry->Flink)
    {
        PSERVICE lpService = CONTAINING_RECORD(Entry, SERVICE, ServiceListEntry);
        DWORD dwAction;

        if (lpService->bDeleted)
            continue;
        dwAction = ScmServiceTriggerAction(lpService, lpEvent);
        if (dwAction == 0)
            continue;
        ScmReferenceService(lpService);
        Hits[dwHits].lpService = lpService;
        Hits[dwHits].dwAction = dwAction;
        dwHits++;
    }
    ScmUnlockDatabase();

    for (i = 0; i < dwHits; i++)
    {
        ScmRunTriggerAction(Hits[i].lpService, Hits[i].dwAction);
        ScmLockDatabaseExclusive();
        ScmDereferenceService(Hits[i].lpService);
        ScmUnlockDatabase();
    }
}

/* The hardware IDs followed by the compatible IDs of the device behind a device interface. */
static LPWSTR
ScmQueryDeviceIds(LPCWSTR lpSymbolicLink)
{
    WCHAR szInstanceId[MAX_DEVICE_ID_LEN];
    ULONG cbInstanceId = sizeof(szInstanceId), cbHardware = 0, cbCompatible = 0;
    DEVPROPTYPE PropertyType;
    DEVINST DevInst;
    LPWSTR lpIds;

    if (CM_Get_Device_Interface_PropertyW(lpSymbolicLink, &DEVPKEY_Device_InstanceId, &PropertyType,
                                          (PBYTE)szInstanceId, &cbInstanceId, 0) != CR_SUCCESS ||
        CM_Locate_DevNodeW(&DevInst, szInstanceId, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return NULL;
    CM_Get_DevNode_Registry_PropertyW(DevInst, CM_DRP_HARDWAREID, NULL, NULL, &cbHardware, 0);
    CM_Get_DevNode_Registry_PropertyW(DevInst, CM_DRP_COMPATIBLEIDS, NULL, NULL, &cbCompatible, 0);

    lpIds = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cbHardware + cbCompatible + 2 * sizeof(WCHAR));
    if (lpIds == NULL)
        return NULL;
    if (cbHardware == 0 ||
        CM_Get_DevNode_Registry_PropertyW(DevInst, CM_DRP_HARDWAREID, NULL, lpIds, &cbHardware, 0) != CR_SUCCESS)
        cbHardware = 0;
    while (cbHardware >= sizeof(WCHAR) && lpIds[cbHardware / sizeof(WCHAR) - 1] == UNICODE_NULL &&
           (cbHardware < 2 * sizeof(WCHAR) || lpIds[cbHardware / sizeof(WCHAR) - 2] == UNICODE_NULL))
        cbHardware -= sizeof(WCHAR);
    if (cbCompatible != 0 &&
        CM_Get_DevNode_Registry_PropertyW(DevInst, CM_DRP_COMPATIBLEIDS, NULL,
                                          (PBYTE)lpIds + cbHardware, &cbCompatible, 0) != CR_SUCCESS)
        ZeroMemory((PBYTE)lpIds + cbHardware, cbCompatible);
    return lpIds;
}

static VOID
ScmDeviceInterfaceArrived(const GUID *lpClass, LPCWSTR lpSymbolicLink)
{
    SCM_TRIGGER_EVENT Event;
    LPWSTR lpIds = ScmQueryDeviceIds(lpSymbolicLink);
    WCHAR szNoIds[2] = { 0, 0 };

    Event.dwTriggerType = SERVICE_TRIGGER_TYPE_DEVICE_INTERFACE_ARRIVAL;
    Event.Subtype = *lpClass;
    Event.lpDeviceIds = lpIds != NULL ? lpIds : szNoIds;
    ScmFireTriggerEvent(&Event);
    if (lpIds != NULL)
        HeapFree(GetProcessHeap(), 0, lpIds);
}

static DWORD
CALLBACK
ScmDeviceInterfaceCallback(HCMNOTIFICATION hNotify, PVOID Context, CM_NOTIFY_ACTION Action,
                           PCM_NOTIFY_EVENT_DATA EventData, DWORD EventDataSize)
{
    UNREFERENCED_PARAMETER(hNotify);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(EventDataSize);

    if (Action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL &&
        EventData->FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE)
    {
        ScmDeviceInterfaceArrived(&EventData->u.DeviceInterface.ClassGuid, EventData->u.DeviceInterface.SymbolicLink);
    }
    return ERROR_SUCCESS;
}

/* Fires the triggers for the interfaces of the class that are already present. */
static VOID
ScmPresentDeviceInterfaces(const GUID *lpClass)
{
    ULONG cchList = 0;
    LPWSTR lpList, p;

    if (CM_Get_Device_Interface_List_SizeW(&cchList, (LPGUID)lpClass, NULL,
                                           CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || cchList <= 1)
        return;
    lpList = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cchList * sizeof(WCHAR));
    if (lpList == NULL)
        return;
    if (CM_Get_Device_Interface_ListW((LPGUID)lpClass, NULL, lpList, cchList,
                                      CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS)
    {
        for (p = lpList; *p != UNICODE_NULL; p += wcslen(p) + 1)
            ScmDeviceInterfaceArrived(lpClass, p);
    }
    HeapFree(GetProcessHeap(), 0, lpList);
}

static VOID
ScmWatchDeviceClass(const GUID *lpClass, BOOL bAtBoot)
{
    CM_NOTIFY_FILTER Filter;
    DWORD i;

    for (i = 0; i < ScmTriggerClassCount; i++)
    {
        if (IsEqualGUID(&ScmTriggerClasses[i], lpClass))
            return;
    }
    if (ScmTriggerClassCount == ARRAYSIZE(ScmTriggerClasses))
        return;

    ZeroMemory(&Filter, sizeof(Filter));
    Filter.cbSize = sizeof(Filter);
    Filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    Filter.u.DeviceInterface.ClassGuid = *lpClass;
    if (CM_Register_Notification(&Filter, NULL, ScmDeviceInterfaceCallback,
                                 &ScmTriggerClassNotify[ScmTriggerClassCount]) != CR_SUCCESS)
    {
        DPRINT1("Cannot watch device interfaces for service triggers\n");
        return;
    }
    ScmTriggerClasses[ScmTriggerClassCount++] = *lpClass;
    if (bAtBoot)
        ScmPresentDeviceInterfaces(lpClass);
}

/* The interface classes named by the start triggers of a service. */
static DWORD
ScmCollectDeviceClasses(PSERVICE lpService, GUID *lpClasses, DWORD dwMax)
{
    HKEY hServiceKey, hTriggersKey, hTriggerKey;
    DWORD dwCount = 0, dwFound = 0, dwType, dwAction, cbGuid, i;

    if (ScmOpenServiceKey(lpService->lpServiceName, KEY_READ, &hServiceKey) != ERROR_SUCCESS)
        return 0;
    if (ScmOpenTriggers(hServiceKey, &hTriggersKey, &dwCount) == ERROR_SUCCESS)
    {
        for (i = 0; i < dwCount && dwFound < dwMax; i++)
        {
            if (ScmOpenTrigger(hTriggersKey, i, &hTriggerKey) != ERROR_SUCCESS)
                continue;
            cbGuid = sizeof(GUID);
            if (ScmQueryDword(hTriggerKey, L"Type", &dwType) == ERROR_SUCCESS &&
                dwType == SERVICE_TRIGGER_TYPE_DEVICE_INTERFACE_ARRIVAL &&
                ScmQueryDword(hTriggerKey, L"Action", &dwAction) == ERROR_SUCCESS &&
                dwAction == SERVICE_TRIGGER_ACTION_SERVICE_START &&
                RegQueryValueExW(hTriggerKey, L"GUID", NULL, NULL, (LPBYTE)&lpClasses[dwFound], &cbGuid) == ERROR_SUCCESS &&
                cbGuid == sizeof(GUID))
                dwFound++;
            RegCloseKey(hTriggerKey);
        }
        if (hTriggersKey != NULL)
            RegCloseKey(hTriggersKey);
    }
    RegCloseKey(hServiceKey);
    return dwFound;
}

static VOID
ScmWatchDeviceTriggers(BOOL bAtBoot)
{
    GUID Classes[SCM_MAX_TRIGGERS];
    DWORD dwClasses = 0, i;
    PLIST_ENTRY Entry;

    ScmLockDatabaseShared();
    for (Entry = ServiceListHead.Flink; Entry != &ServiceListHead && dwClasses < ARRAYSIZE(Classes); Entry = Entry->Flink)
    {
        PSERVICE lpService = CONTAINING_RECORD(Entry, SERVICE, ServiceListEntry);

        if (!lpService->bDeleted)
            dwClasses += ScmCollectDeviceClasses(lpService, &Classes[dwClasses], ARRAYSIZE(Classes) - dwClasses);
    }
    ScmUnlockDatabase();

    for (i = 0; i < dwClasses; i++)
        ScmWatchDeviceClass(&Classes[i], bAtBoot);
}

static ULONG
ScmCountIpAddresses(VOID)
{
    PMIB_UNICASTIPADDRESS_TABLE lpTable = NULL;
    ULONG ulCount = 0, i;

    if (GetUnicastIpAddressTable(AF_UNSPEC, &lpTable) != NO_ERROR)
        return 0;
    for (i = 0; i < lpTable->NumEntries; i++)
    {
        if (lpTable->Table[i].InterfaceLuid.Info.IfType != IF_TYPE_SOFTWARE_LOOPBACK)
            ulCount++;
    }
    FreeMibTable(lpTable);
    return ulCount;
}

static VOID
CALLBACK
ScmUnicastAddressChanged(PVOID Context, PMIB_UNICASTIPADDRESS_ROW Row, MIB_NOTIFICATION_TYPE NotificationType)
{
    SCM_TRIGGER_EVENT Event;
    LONG lAvailable, lPrevious;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Row);
    UNREFERENCED_PARAMETER(NotificationType);

    lAvailable = ScmCountIpAddresses() != 0;
    lPrevious = InterlockedExchange(&ScmIpAvailable, lAvailable);
    if (lPrevious == lAvailable || (lPrevious == -1 && !lAvailable))
        return;

    Event.dwTriggerType = SERVICE_TRIGGER_TYPE_IP_ADDRESS_AVAILABILITY;
    Event.Subtype = lAvailable ? ScmFirstIpGuid : ScmLastIpGuid;
    Event.lpDeviceIds = NULL;
    ScmFireTriggerEvent(&Event);
}

/* Arms the triggers of the configured services; called after the auto-start services were started. */
VOID
ScmStartTriggers(VOID)
{
    InitializeCriticalSection(&ScmTriggerLock);
    EnterCriticalSection(&ScmTriggerLock);
    ScmWatchDeviceTriggers(TRUE);
    LeaveCriticalSection(&ScmTriggerLock);
    if (NotifyUnicastIpAddressChange(AF_UNSPEC, ScmUnicastAddressChanged, NULL, TRUE, &ScmIpNotify) != NO_ERROR)
        DPRINT1("Cannot watch IP addresses for service triggers\n");
}

/* A service changed its triggers; watch the device interface classes it names. */
VOID
ScmTriggersChanged(VOID)
{
    if (ScmInitialize)
        return;
    EnterCriticalSection(&ScmTriggerLock);
    ScmWatchDeviceTriggers(FALSE);
    LeaveCriticalSection(&ScmTriggerLock);
}
