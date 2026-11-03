/*
 * PROJECT:     LiberNT Services
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Query and set the trigger parameters of a service
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "sc.h"

static const GUID ScFirstIpGuid = { 0x4f27f2de, 0x14e2, 0x430b, { 0xa5, 0x49, 0x7c, 0xd4, 0x8c, 0xbc, 0x82, 0x45 } };
static const GUID ScLastIpGuid = { 0xcc4ba62a, 0x162e, 0x4648, { 0x84, 0x7a, 0xb6, 0xbd, 0xf9, 0x93, 0xe3, 0x35 } };
static const GUID ScDomainJoinGuid = { 0x1ce20aba, 0x9851, 0x4421, { 0x94, 0x30, 0x1d, 0xde, 0xb7, 0x66, 0xe8, 0x09 } };
static const GUID ScDomainLeaveGuid = { 0xddaf516e, 0x58c2, 0x4866, { 0x95, 0x74, 0xc3, 0xb6, 0x15, 0xd4, 0x2e, 0xa1 } };
static const GUID ScPortOpenGuid = { 0xb7569e07, 0x8421, 0x4ee0, { 0xad, 0x10, 0x86, 0x91, 0x5a, 0xfd, 0xad, 0x09 } };
static const GUID ScPortCloseGuid = { 0xa144ed38, 0x8e12, 0x4de4, { 0x9d, 0x96, 0xe6, 0x47, 0x40, 0xb1, 0xa5, 0x24 } };
static const GUID ScMachinePolicyGuid = { 0x659fcae6, 0x5bdb, 0x4da9, { 0xb1, 0xff, 0xca, 0x2a, 0x17, 0x8d, 0x46, 0xe0 } };
static const GUID ScUserPolicyGuid = { 0x54fb46c8, 0xf089, 0x464c, { 0xb1, 0xfd, 0x59, 0xd1, 0xb6, 0x2c, 0x3b, 0x50 } };
static const GUID ScRpcInterfaceGuid = { 0xbc90d167, 0x9470, 0x4139, { 0xa9, 0xba, 0xbe, 0x0b, 0xbb, 0xf5, 0xb7, 0x4d } };
static const GUID ScNamedPipeGuid = { 0x1f81d131, 0x3fac, 0x4537, { 0x9e, 0x0c, 0x7e, 0x7b, 0x0c, 0x2f, 0x4b, 0x55 } };

typedef struct _SC_TRIGGER_KEYWORD
{
    LPCTSTR Name;
    DWORD Action;
    DWORD Type;
    const GUID *Subtype;     /* NULL when the option carries a UUID */
    DWORD DataType;          /* 0 when the option takes no data */
} SC_TRIGGER_KEYWORD;

static const SC_TRIGGER_KEYWORD ScTriggerKeywords[] =
{
    { _T("start/device/"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_DEVICE_INTERFACE_ARRIVAL, NULL, SERVICE_TRIGGER_DATA_TYPE_STRING },
    { _T("start/custom/"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_CUSTOM, NULL, SERVICE_TRIGGER_DATA_TYPE_BINARY },
    { _T("stop/custom/"), SERVICE_TRIGGER_ACTION_SERVICE_STOP, SERVICE_TRIGGER_TYPE_CUSTOM, NULL, SERVICE_TRIGGER_DATA_TYPE_BINARY },
    { _T("start/strcustom/"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_CUSTOM, NULL, SERVICE_TRIGGER_DATA_TYPE_STRING },
    { _T("stop/strcustom/"), SERVICE_TRIGGER_ACTION_SERVICE_STOP, SERVICE_TRIGGER_TYPE_CUSTOM, NULL, SERVICE_TRIGGER_DATA_TYPE_STRING },
    { _T("start/levelcustom/"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_CUSTOM, NULL, SERVICE_TRIGGER_DATA_TYPE_LEVEL },
    { _T("stop/levelcustom/"), SERVICE_TRIGGER_ACTION_SERVICE_STOP, SERVICE_TRIGGER_TYPE_CUSTOM, NULL, SERVICE_TRIGGER_DATA_TYPE_LEVEL },
    { _T("start/anykeywordcustom/"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_CUSTOM, NULL, SERVICE_TRIGGER_DATA_TYPE_KEYWORD_ANY },
    { _T("stop/anykeywordcustom/"), SERVICE_TRIGGER_ACTION_SERVICE_STOP, SERVICE_TRIGGER_TYPE_CUSTOM, NULL, SERVICE_TRIGGER_DATA_TYPE_KEYWORD_ANY },
    { _T("start/allkeywordcustom/"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_CUSTOM, NULL, SERVICE_TRIGGER_DATA_TYPE_KEYWORD_ALL },
    { _T("stop/allkeywordcustom/"), SERVICE_TRIGGER_ACTION_SERVICE_STOP, SERVICE_TRIGGER_TYPE_CUSTOM, NULL, SERVICE_TRIGGER_DATA_TYPE_KEYWORD_ALL },
    { _T("start/portopen/"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_FIREWALL_PORT_EVENT, &ScPortOpenGuid, SERVICE_TRIGGER_DATA_TYPE_STRING },
    { _T("stop/portclose/"), SERVICE_TRIGGER_ACTION_SERVICE_STOP, SERVICE_TRIGGER_TYPE_FIREWALL_PORT_EVENT, &ScPortCloseGuid, SERVICE_TRIGGER_DATA_TYPE_STRING },
    { _T("start/rpcinterface/"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_NETWORK_ENDPOINT, &ScRpcInterfaceGuid, SERVICE_TRIGGER_DATA_TYPE_STRING },
    { _T("start/namedpipe/"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_NETWORK_ENDPOINT, &ScNamedPipeGuid, SERVICE_TRIGGER_DATA_TYPE_STRING },
    { _T("start/networkon"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_IP_ADDRESS_AVAILABILITY, &ScFirstIpGuid, 0 },
    { _T("stop/networkoff"), SERVICE_TRIGGER_ACTION_SERVICE_STOP, SERVICE_TRIGGER_TYPE_IP_ADDRESS_AVAILABILITY, &ScLastIpGuid, 0 },
    { _T("start/domainjoin"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_DOMAIN_JOIN, &ScDomainJoinGuid, 0 },
    { _T("stop/domainleave"), SERVICE_TRIGGER_ACTION_SERVICE_STOP, SERVICE_TRIGGER_TYPE_DOMAIN_JOIN, &ScDomainLeaveGuid, 0 },
    { _T("start/machinepolicy"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_GROUP_POLICY, &ScMachinePolicyGuid, 0 },
    { _T("start/userpolicy"), SERVICE_TRIGGER_ACTION_SERVICE_START, SERVICE_TRIGGER_TYPE_GROUP_POLICY, &ScUserPolicyGuid, 0 },
};

static int
HexValue(TCHAR c)
{
    if (c >= _T('0') && c <= _T('9')) return c - _T('0');
    if (c >= _T('a') && c <= _T('f')) return c - _T('a') + 10;
    if (c >= _T('A') && c <= _T('F')) return c - _T('A') + 10;
    return -1;
}

/* Hexadecimal digits into bytes; returns the byte count or -1. */
static int
ParseHex(LPCTSTR lpText, SIZE_T cchText, PBYTE lpBytes, SIZE_T cbBytes)
{
    SIZE_T i;

    if (cchText % 2 != 0 || cchText / 2 > cbBytes)
        return -1;
    for (i = 0; i < cchText; i += 2)
    {
        int hi = HexValue(lpText[i]), lo = HexValue(lpText[i + 1]);

        if (hi < 0 || lo < 0)
            return -1;
        lpBytes[i / 2] = (BYTE)((hi << 4) | lo);
    }
    return (int)(cchText / 2);
}

static BOOL
ParseGuid(LPCTSTR lpText, SIZE_T cchText, GUID *lpGuid)
{
    BYTE Bytes[16];
    static const SIZE_T Groups[] = { 8, 4, 4, 4, 12 };
    SIZE_T i, cbDone = 0, cchDone = 0;

    if (cchText != 36)
        return FALSE;
    for (i = 0; i < ARRAYSIZE(Groups); i++)
    {
        if (ParseHex(lpText + cchDone, Groups[i], Bytes + cbDone, sizeof(Bytes) - cbDone) < 0)
            return FALSE;
        cbDone += Groups[i] / 2;
        cchDone += Groups[i];
        if (i + 1 < ARRAYSIZE(Groups) && lpText[cchDone++] != _T('-'))
            return FALSE;
    }
    lpGuid->Data1 = ((DWORD)Bytes[0] << 24) | ((DWORD)Bytes[1] << 16) | ((DWORD)Bytes[2] << 8) | Bytes[3];
    lpGuid->Data2 = (WORD)((Bytes[4] << 8) | Bytes[5]);
    lpGuid->Data3 = (WORD)((Bytes[6] << 8) | Bytes[7]);
    CopyMemory(lpGuid->Data4, Bytes + 8, 8);
    return TRUE;
}

/* One data item from the text between two slashes, in the representation of its type. */
static BOOL
ParseTriggerData(DWORD dwDataType, LPCTSTR lpText, SIZE_T cchText, PSERVICE_TRIGGER_SPECIFIC_DATA_ITEM lpItem)
{
    ULONGLONG ullValue;
    TCHAR szNumber[32];
    int cb;

    lpItem->dwDataType = dwDataType;
    switch (dwDataType)
    {
        case SERVICE_TRIGGER_DATA_TYPE_BINARY:
            lpItem->pData = HeapAlloc(GetProcessHeap(), 0, cchText / 2 + 1);
            if (lpItem->pData == NULL)
                return FALSE;
            cb = ParseHex(lpText, cchText, lpItem->pData, cchText / 2 + 1);
            lpItem->cbData = (DWORD)cb;
            return cb >= 0;

        case SERVICE_TRIGGER_DATA_TYPE_STRING:
            lpItem->cbData = (DWORD)((cchText + 1) * sizeof(WCHAR));
            lpItem->pData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, lpItem->cbData);
            if (lpItem->pData == NULL)
                return FALSE;
            CopyMemory(lpItem->pData, lpText, cchText * sizeof(WCHAR));
            return TRUE;

        default:
            if (cchText == 0 || cchText >= ARRAYSIZE(szNumber))
                return FALSE;
            CopyMemory(szNumber, lpText, cchText * sizeof(TCHAR));
            szNumber[cchText] = 0;
            ullValue = _tcstoui64(szNumber, NULL, 0);
            lpItem->cbData = dwDataType == SERVICE_TRIGGER_DATA_TYPE_LEVEL ? sizeof(BYTE) : sizeof(ULONGLONG);
            lpItem->pData = HeapAlloc(GetProcessHeap(), 0, lpItem->cbData);
            if (lpItem->pData == NULL)
                return FALSE;
            CopyMemory(lpItem->pData, &ullValue, lpItem->cbData);
            return TRUE;
    }
}

static VOID
FreeTriggers(PSERVICE_TRIGGER lpTriggers, DWORD dwCount)
{
    DWORD i, j;

    for (i = 0; i < dwCount; i++)
    {
        for (j = 0; j < lpTriggers[i].cDataItems; j++)
            HeapFree(GetProcessHeap(), 0, lpTriggers[i].pDataItems[j].pData);
        HeapFree(GetProcessHeap(), 0, lpTriggers[i].pDataItems);
        HeapFree(GetProcessHeap(), 0, lpTriggers[i].pTriggerSubtype);
    }
    HeapFree(GetProcessHeap(), 0, lpTriggers);
}

/* One "start/..." or "stop/..." option into a trigger. */
static BOOL
ParseTrigger(LPCTSTR lpOption, PSERVICE_TRIGGER lpTrigger)
{
    const SC_TRIGGER_KEYWORD *lpKeyword = NULL;
    LPCTSTR p, lpEnd;
    SIZE_T i, cchName;
    DWORD dwItems = 0;

    ZeroMemory(lpTrigger, sizeof(*lpTrigger));
    for (i = 0; i < ARRAYSIZE(ScTriggerKeywords); i++)
    {
        cchName = _tcslen(ScTriggerKeywords[i].Name);
        if (!_tcsnicmp(lpOption, ScTriggerKeywords[i].Name, cchName) &&
            (ScTriggerKeywords[i].DataType != 0 || lpOption[cchName] == 0))
        {
            lpKeyword = &ScTriggerKeywords[i];
            break;
        }
    }
    if (lpKeyword == NULL)
        return FALSE;

    lpTrigger->dwTriggerType = lpKeyword->Type;
    lpTrigger->dwAction = lpKeyword->Action;
    lpTrigger->pTriggerSubtype = HeapAlloc(GetProcessHeap(), 0, sizeof(GUID));
    if (lpTrigger->pTriggerSubtype == NULL)
        return FALSE;
    p = lpOption + cchName;
    if (lpKeyword->Subtype != NULL)
    {
        *lpTrigger->pTriggerSubtype = *lpKeyword->Subtype;
    }
    else
    {
        lpEnd = _tcschr(p, _T('/'));
        if (!ParseGuid(p, lpEnd ? (SIZE_T)(lpEnd - p) : _tcslen(p), lpTrigger->pTriggerSubtype))
            return FALSE;
        p = lpEnd ? lpEnd + 1 : p + _tcslen(p);
    }
    if (lpKeyword->DataType == 0 || *p == 0)
        return TRUE;

    lpTrigger->pDataItems = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 64 * sizeof(SERVICE_TRIGGER_SPECIFIC_DATA_ITEM));
    if (lpTrigger->pDataItems == NULL)
        return FALSE;
    while (*p != 0 && dwItems < 64)
    {
        lpEnd = _tcschr(p, _T('/'));
        if (!ParseTriggerData(lpKeyword->DataType, p, lpEnd ? (SIZE_T)(lpEnd - p) : _tcslen(p),
                              &lpTrigger->pDataItems[dwItems]))
        {
            lpTrigger->cDataItems = dwItems + 1;
            return FALSE;
        }
        dwItems++;
        p = lpEnd ? lpEnd + 1 : p + _tcslen(p);
    }
    lpTrigger->cDataItems = dwItems;
    return *p == 0;
}

static SC_HANDLE
OpenTriggerService(LPCTSTR ServiceName, DWORD dwDesiredAccess, SC_HANDLE *phManager)
{
    SC_HANDLE hService;

    *phManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (*phManager == NULL)
    {
        _tprintf(_T("[SC] OpenSCManager FAILED %lu:\n\n"), GetLastError());
        return NULL;
    }
    hService = OpenService(*phManager, ServiceName, dwDesiredAccess);
    if (hService == NULL)
    {
        _tprintf(_T("[SC] OpenService FAILED %lu:\n\n"), GetLastError());
        CloseServiceHandle(*phManager);
        *phManager = NULL;
    }
    return hService;
}

static BOOL
ChangeTriggers(LPCTSTR ServiceName, PSERVICE_TRIGGER_INFO lpInfo)
{
    SC_HANDLE hManager, hService;
    BOOL bResult;

    hService = OpenTriggerService(ServiceName, SERVICE_CHANGE_CONFIG, &hManager);
    if (hService == NULL)
    {
        ReportLastError();
        return FALSE;
    }
    bResult = ChangeServiceConfig2(hService, SERVICE_CONFIG_TRIGGER_INFO, (LPBYTE)lpInfo);
    if (bResult)
    {
        _tprintf(_T("[SC] ChangeServiceConfig2 SUCCESS\n\n"));
    }
    else
    {
        _tprintf(_T("[SC] ChangeServiceConfig2 FAILED %lu:\n\n"), GetLastError());
        ReportLastError();
    }
    CloseServiceHandle(hService);
    CloseServiceHandle(hManager);
    return bResult;
}

BOOL SetTriggerInfo(LPCTSTR *ServiceArgs, INT ArgCount)
{
    SERVICE_TRIGGER_INFO Info = { 0, NULL, NULL };
    INT i;
    BOOL bResult;

    if (ArgCount < 2)
    {
        SetTriggerInfoUsage();
        return FALSE;
    }
    if (ArgCount == 2 && !_tcsicmp(ServiceArgs[1], _T("delete")))
        return ChangeTriggers(ServiceArgs[0], &Info);
    if (ArgCount - 1 > 64)
    {
        SetTriggerInfoUsage();
        return FALSE;
    }

    Info.pTriggers = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (ArgCount - 1) * sizeof(SERVICE_TRIGGER));
    if (Info.pTriggers == NULL)
        return FALSE;
    for (i = 1; i < ArgCount; i++)
    {
        Info.cTriggers = i;
        if (!ParseTrigger(ServiceArgs[i], &Info.pTriggers[i - 1]))
        {
            FreeTriggers(Info.pTriggers, Info.cTriggers);
            SetTriggerInfoUsage();
            return FALSE;
        }
    }
    bResult = ChangeTriggers(ServiceArgs[0], &Info);
    FreeTriggers(Info.pTriggers, Info.cTriggers);
    return bResult;
}

static LPCTSTR
TriggerTypeName(const SERVICE_TRIGGER *lpTrigger)
{
    switch (lpTrigger->dwTriggerType)
    {
        case SERVICE_TRIGGER_TYPE_DEVICE_INTERFACE_ARRIVAL: return _T("DEVICE INTERFACE ARRIVAL");
        case SERVICE_TRIGGER_TYPE_IP_ADDRESS_AVAILABILITY: return _T("NETWORK EVENT");
        case SERVICE_TRIGGER_TYPE_DOMAIN_JOIN: return _T("DOMAIN JOINED STATUS");
        case SERVICE_TRIGGER_TYPE_FIREWALL_PORT_EVENT: return _T("FIREWALL PORT EVENT");
        case SERVICE_TRIGGER_TYPE_GROUP_POLICY: return _T("GROUP POLICY");
        case SERVICE_TRIGGER_TYPE_NETWORK_ENDPOINT: return _T("NETWORK EVENT");
        case SERVICE_TRIGGER_TYPE_CUSTOM_SYSTEM_STATE_CHANGE: return _T("CUSTOM SYSTEM STATE CHANGE");
        case SERVICE_TRIGGER_TYPE_CUSTOM: return _T("CUSTOM");
        case SERVICE_TRIGGER_TYPE_AGGREGATE: return _T("AGGREGATE");
        default: return _T("UNKNOWN");
    }
}

static VOID
PrintTriggerData(const SERVICE_TRIGGER_SPECIFIC_DATA_ITEM *lpItem)
{
    DWORD i;

    _tprintf(_T("            DATA                       : "));
    if (lpItem->dwDataType == SERVICE_TRIGGER_DATA_TYPE_STRING)
    {
        LPCWSTR p = (LPCWSTR)lpItem->pData;
        DWORD cch = lpItem->cbData / sizeof(WCHAR);

        for (i = 0; i < cch && p[i] != 0; i++)
            _tprintf(_T("%c"), p[i]);
    }
    else
    {
        for (i = 0; i < lpItem->cbData; i++)
            _tprintf(_T("%02x"), lpItem->pData[i]);
    }
    _tprintf(_T("\n"));
}

BOOL QueryTriggerInfo(LPCTSTR ServiceName)
{
    PSERVICE_TRIGGER_INFO lpInfo = NULL;
    SC_HANDLE hManager, hService;
    DWORD cbNeeded = 0, i, j;
    BOOL bResult;

    hService = OpenTriggerService(ServiceName, SERVICE_QUERY_CONFIG, &hManager);
    if (hService == NULL)
    {
        ReportLastError();
        return FALSE;
    }
    bResult = QueryServiceConfig2(hService, SERVICE_CONFIG_TRIGGER_INFO, NULL, 0, &cbNeeded);
    if (!bResult && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        lpInfo = HeapAlloc(GetProcessHeap(), 0, cbNeeded);
        bResult = lpInfo != NULL &&
                  QueryServiceConfig2(hService, SERVICE_CONFIG_TRIGGER_INFO, (LPBYTE)lpInfo, cbNeeded, &cbNeeded);
    }
    if (bResult)
    {
        _tprintf(_T("[SC] QueryServiceConfig2 SUCCESS\n\n"));
        _tprintf(_T("SERVICE_NAME: %s\n\n"), ServiceName);
        for (i = 0; lpInfo != NULL && i < lpInfo->cTriggers; i++)
        {
            const SERVICE_TRIGGER *lpTrigger = &lpInfo->pTriggers[i];
            const GUID *g = lpTrigger->pTriggerSubtype;

            _tprintf(lpTrigger->dwAction == SERVICE_TRIGGER_ACTION_SERVICE_START ?
                     _T("        START SERVICE\n") : _T("        STOP SERVICE\n"));
            _tprintf(_T("          %-28s : %08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\n"),
                     TriggerTypeName(lpTrigger), g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1],
                     g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
            for (j = 0; j < lpTrigger->cDataItems; j++)
                PrintTriggerData(&lpTrigger->pDataItems[j]);
        }
    }
    else
    {
        _tprintf(_T("[SC] QueryServiceConfig2 FAILED %lu:\n\n"), GetLastError());
        ReportLastError();
    }
    if (lpInfo != NULL)
        HeapFree(GetProcessHeap(), 0, lpInfo);
    CloseServiceHandle(hService);
    CloseServiceHandle(hManager);
    return bResult;
}
