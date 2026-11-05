/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SERVICE_CONFIG_TRIGGER_INFO through ChangeServiceConfig2 and sc triggerinfo
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include "precomp.h"

static const GUID ProviderGuid = { 0xe1bdc95e, 0x0f07, 0x5469, { 0x8e, 0x64, 0x06, 0x1e, 0xa5, 0xbe, 0x6a, 0x0d } };
static const GUID FirstIpGuid = { 0x4f27f2de, 0x14e2, 0x430b, { 0xa5, 0x49, 0x7c, 0xd4, 0x8c, 0xbc, 0x82, 0x45 } };

static SC_HANDLE create_service(SC_HANDLE scm, WCHAR *name, SIZE_T size)
{
    WCHAR path[MAX_PATH];
    SC_HANDLE service;

    GetModuleFileNameW(NULL, path, ARRAYSIZE(path));
    StringCbPrintfW(name, size, L"LntTriggerTest%lu", GetTickCount());
    service = CreateServiceW(scm, name, name, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                             SERVICE_ERROR_IGNORE, path, NULL, NULL, NULL, NULL, NULL);
    ok(service != NULL, "CreateServiceW failed: %lu\n", GetLastError());
    return service;
}

static PSERVICE_TRIGGER_INFO query_triggers(SC_HANDLE service)
{
    PSERVICE_TRIGGER_INFO info;
    DWORD needed = 0;
    BOOL res;

    SetLastError(0xdeadbeef);
    res = QueryServiceConfig2W(service, SERVICE_CONFIG_TRIGGER_INFO, NULL, 0, &needed);
    ok(!res && GetLastError() == ERROR_INSUFFICIENT_BUFFER && needed >= sizeof(SERVICE_TRIGGER_INFO),
       "size query: %d error %lu needed %lu\n", res, GetLastError(), needed);
    if (res || needed < sizeof(SERVICE_TRIGGER_INFO))
        return NULL;
    info = HeapAlloc(GetProcessHeap(), 0, needed);
    if (!info)
        return NULL;
    res = QueryServiceConfig2W(service, SERVICE_CONFIG_TRIGGER_INFO, (BYTE *)info, needed, &needed);
    ok(res, "QueryServiceConfig2W failed: %lu\n", GetLastError());
    if (!res)
    {
        HeapFree(GetProcessHeap(), 0, info);
        return NULL;
    }
    return info;
}

static void test_round_trip(SC_HANDLE service)
{
    WCHAR start[] = L"StartSession";
    BYTE binary[4] = { 0xab, 0xcd, 0xab, 0xcd };
    SERVICE_TRIGGER_SPECIFIC_DATA_ITEM items[2] = {
        { SERVICE_TRIGGER_DATA_TYPE_STRING, sizeof(start), (BYTE *)start },
        { SERVICE_TRIGGER_DATA_TYPE_BINARY, sizeof(binary), binary },
    };
    SERVICE_TRIGGER triggers[2] = {
        { SERVICE_TRIGGER_TYPE_CUSTOM, SERVICE_TRIGGER_ACTION_SERVICE_START, (GUID *)&ProviderGuid, 2, items },
        { SERVICE_TRIGGER_TYPE_IP_ADDRESS_AVAILABILITY, SERVICE_TRIGGER_ACTION_SERVICE_START, (GUID *)&FirstIpGuid, 0, NULL },
    };
    SERVICE_TRIGGER_INFO info = { 2, triggers, NULL };
    PSERVICE_TRIGGER_INFO out;
    BOOL res;

    res = ChangeServiceConfig2W(service, SERVICE_CONFIG_TRIGGER_INFO, &info);
    ok(res, "ChangeServiceConfig2W(triggers) failed: %lu\n", GetLastError());
    out = query_triggers(service);
    if (!out)
        return;
    ok(out->cTriggers == 2, "triggers %lu\n", out->cTriggers);
    if (out->cTriggers == 2)
    {
        PSERVICE_TRIGGER t0 = &out->pTriggers[0], t1 = &out->pTriggers[1];

        ok(t0->dwTriggerType == SERVICE_TRIGGER_TYPE_CUSTOM && t0->dwAction == SERVICE_TRIGGER_ACTION_SERVICE_START &&
           IsEqualGUID(t0->pTriggerSubtype, &ProviderGuid) && t0->cDataItems == 2, "first trigger %lu %lu %lu\n",
           t0->dwTriggerType, t0->dwAction, t0->cDataItems);
        if (t0->cDataItems == 2)
        {
            ok(t0->pDataItems[0].dwDataType == SERVICE_TRIGGER_DATA_TYPE_STRING &&
               t0->pDataItems[0].cbData == sizeof(start) && !memcmp(t0->pDataItems[0].pData, start, sizeof(start)),
               "string data item type %lu size %lu\n", t0->pDataItems[0].dwDataType, t0->pDataItems[0].cbData);
            ok(t0->pDataItems[1].dwDataType == SERVICE_TRIGGER_DATA_TYPE_BINARY &&
               t0->pDataItems[1].cbData == sizeof(binary) && !memcmp(t0->pDataItems[1].pData, binary, sizeof(binary)),
               "binary data item type %lu size %lu\n", t0->pDataItems[1].dwDataType, t0->pDataItems[1].cbData);
        }
        ok(t1->dwTriggerType == SERVICE_TRIGGER_TYPE_IP_ADDRESS_AVAILABILITY && IsEqualGUID(t1->pTriggerSubtype, &FirstIpGuid) &&
           t1->cDataItems == 0, "second trigger %lu items %lu\n", t1->dwTriggerType, t1->cDataItems);
    }
    HeapFree(GetProcessHeap(), 0, out);

    info.cTriggers = 0;
    info.pTriggers = NULL;
    res = ChangeServiceConfig2W(service, SERVICE_CONFIG_TRIGGER_INFO, &info);
    ok(res, "ChangeServiceConfig2W(no triggers) failed: %lu\n", GetLastError());
    out = query_triggers(service);
    ok(out && out->cTriggers == 0, "triggers after delete %lu\n", out ? out->cTriggers : 0xdead);
    if (out)
        HeapFree(GetProcessHeap(), 0, out);
}

static void test_invalid(SC_HANDLE service)
{
    SERVICE_TRIGGER trigger = { SERVICE_TRIGGER_TYPE_CUSTOM, 3, (GUID *)&ProviderGuid, 0, NULL };
    SERVICE_TRIGGER_INFO info = { 1, &trigger, NULL };
    BYTE reserved = 0;
    BOOL res;

    SetLastError(0xdeadbeef);
    res = ChangeServiceConfig2W(service, SERVICE_CONFIG_TRIGGER_INFO, &info);
    ok(!res && GetLastError() == ERROR_INVALID_PARAMETER, "action 3: %d error %lu\n", res, GetLastError());
    trigger.dwAction = SERVICE_TRIGGER_ACTION_SERVICE_START;
    info.pReserved = &reserved;
    SetLastError(0xdeadbeef);
    res = ChangeServiceConfig2W(service, SERVICE_CONFIG_TRIGGER_INFO, &info);
    ok(res, "pReserved: %d error %lu\n", res, GetLastError());
}

/* sc.exe must finish: an installer waits for it. */
static DWORD run_sc(PCWSTR arguments)
{
    WCHAR system[MAX_PATH], command[1024];
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION info;
    DWORD code = 0xffffffff;

    GetSystemDirectoryW(system, ARRAYSIZE(system));
    StringCbPrintfW(command, sizeof(command), L"\"%ls\\sc.exe\" %ls", system, arguments);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = INVALID_HANDLE_VALUE;
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &info))
        return code;
    if (WaitForSingleObject(info.hProcess, 10000) == WAIT_OBJECT_0)
        GetExitCodeProcess(info.hProcess, &code);
    else
        TerminateProcess(info.hProcess, 1);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return code;
}

static void test_sc(SC_HANDLE service, PCWSTR name)
{
    WCHAR arguments[512];
    PSERVICE_TRIGGER_INFO out;
    DWORD code;

    StringCbPrintfW(arguments, sizeof(arguments),
                    L"triggerinfo %ls start/strcustom/e1bdc95e-0f07-5469-8e64-061ea5be6a0d/StartSession "
                    L"stop/strcustom/e1bdc95e-0f07-5469-8e64-061ea5be6a0d/StopSession", name);
    code = run_sc(arguments);
    ok(code == 0, "sc triggerinfo: exit code %lu\n", code);
    out = query_triggers(service);
    ok(out && out->cTriggers == 2, "triggers set by sc %lu\n", out ? out->cTriggers : 0xdead);
    if (out && out->cTriggers == 2)
    {
        PSERVICE_TRIGGER t = &out->pTriggers[1];

        ok(t->dwTriggerType == SERVICE_TRIGGER_TYPE_CUSTOM && t->dwAction == SERVICE_TRIGGER_ACTION_SERVICE_STOP &&
           IsEqualGUID(t->pTriggerSubtype, &ProviderGuid) && t->cDataItems == 1 &&
           t->pDataItems[0].dwDataType == SERVICE_TRIGGER_DATA_TYPE_STRING &&
           !wcscmp((PCWSTR)t->pDataItems[0].pData, L"StopSession"), "stop trigger %lu %lu items %lu\n",
           t->dwTriggerType, t->dwAction, t->cDataItems);
    }
    if (out)
        HeapFree(GetProcessHeap(), 0, out);

    StringCbPrintfW(arguments, sizeof(arguments), L"qtriggerinfo %ls", name);
    code = run_sc(arguments);
    ok(code == 0, "sc qtriggerinfo: exit code %lu\n", code);
    StringCbPrintfW(arguments, sizeof(arguments), L"triggerinfo %ls delete", name);
    code = run_sc(arguments);
    ok(code == 0, "sc triggerinfo delete: exit code %lu\n", code);
    out = query_triggers(service);
    ok(out && out->cTriggers == 0, "triggers after sc delete %lu\n", out ? out->cTriggers : 0xdead);
    if (out)
        HeapFree(GetProcessHeap(), 0, out);
}

START_TEST(TriggerInfo)
{
    WCHAR name[64];
    SC_HANDLE scm, service;

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!scm)
    {
        skip("OpenSCManagerW failed with error %lu\n", GetLastError());
        return;
    }
    service = create_service(scm, name, sizeof(name));
    if (service)
    {
        test_round_trip(service);
        test_invalid(service);
        test_sc(service, name);
        ok(DeleteService(service), "DeleteService failed: %lu\n", GetLastError());
        CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);
}
