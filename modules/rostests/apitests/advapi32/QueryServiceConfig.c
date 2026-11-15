/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     String placement and buffer sizes of QueryServiceConfigW
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "precomp.h"

#define CONFIG_ALIGN(x) (((x) + sizeof(PVOID) - 1) & ~(ULONG)(sizeof(PVOID) - 1))

static ULONG MultiSzBytes(PCWSTR Str)
{
    PCWSTR p = Str;

    while (*p)
        p += wcslen(p) + 1;
    return (ULONG)((p - Str + 1) * sizeof(WCHAR));
}

static void CheckString(PCSTR Name, PBYTE Base, PCWSTR Actual, PCWSTR Expected, ULONG Bytes, ULONG *Offset)
{
    *Offset = CONFIG_ALIGN(*Offset);
    ok(Actual != NULL, "%s: NULL\n", Name);
    if (!Actual)
        return;
    ok((PBYTE)Actual - Base == (LONG_PTR)*Offset, "%s: offset %ld, expected %lu\n", Name, (LONG)((PBYTE)Actual - Base), *Offset);
    ok(memcmp(Actual, Expected, Bytes) == 0, "%s: '%ls', expected '%ls'\n", Name, Actual, Expected);
    *Offset += Bytes;
}

static void TestShape(SC_HANDLE hScm, PCWSTR ServiceName, PCWSTR DisplayName, PCWSTR Path, PCWSTR Group,
                      PCWSTR Dependencies, PCWSTR StartName)
{
    static DECLSPEC_ALIGN(8) BYTE Buffer[8192];
    LPQUERY_SERVICE_CONFIGW Config = (LPQUERY_SERVICE_CONFIGW)Buffer;
    SC_HANDLE hService;
    DWORD Needed, Size;
    ULONG Offset;
    BOOL ret;

    hService = CreateServiceW(hScm, ServiceName, DisplayName, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                              SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, Path, Group, NULL, Dependencies,
                              StartName, NULL);
    ok(hService != NULL, "%ls: CreateServiceW failed, error %lu\n", ServiceName, GetLastError());
    if (!hService)
        return;

    Needed = 0;
    SetLastError(0xdeadbeef);
    ret = QueryServiceConfigW(hService, NULL, 0, &Needed);
    ok(!ret, "%ls: QueryServiceConfigW(NULL, 0) succeeded\n", ServiceName);
    ok_err(ERROR_INSUFFICIENT_BUFFER);
    ok(Needed > sizeof(QUERY_SERVICE_CONFIGW) && Needed <= sizeof(Buffer), "%ls: needed %lu\n", ServiceName, Needed);
    Size = min(Needed, (DWORD)sizeof(Buffer));

    memset(Buffer, 0xcc, sizeof(Buffer));
    ret = QueryServiceConfigW(hService, Config, Size, &Needed);
    ok(ret, "%ls: QueryServiceConfigW(%lu) failed, error %lu\n", ServiceName, Size, GetLastError());
    if (ret)
    {
        Offset = sizeof(QUERY_SERVICE_CONFIGW);
        CheckString("path", Buffer, Config->lpBinaryPathName, Path, (ULONG)((wcslen(Path) + 1) * sizeof(WCHAR)), &Offset);
        CheckString("group", Buffer, Config->lpLoadOrderGroup, Group ? Group : L"",
                    (ULONG)(((Group ? wcslen(Group) : 0) + 1) * sizeof(WCHAR)), &Offset);
        CheckString("dependencies", Buffer, Config->lpDependencies, Dependencies ? Dependencies : L"\0",
                    Dependencies ? MultiSzBytes(Dependencies) : 2 * sizeof(WCHAR), &Offset);
        CheckString("start name", Buffer, Config->lpServiceStartName, StartName ? StartName : L"LocalSystem",
                    (ULONG)(((StartName ? wcslen(StartName) : wcslen(L"LocalSystem")) + 1) * sizeof(WCHAR)), &Offset);
        CheckString("display name", Buffer, Config->lpDisplayName, DisplayName,
                    (ULONG)((wcslen(DisplayName) + 1) * sizeof(WCHAR)), &Offset);
        ok(Size >= Offset, "%ls: needed %lu, layout ends at %lu\n", ServiceName, Size, Offset);
    }

    DeleteService(hService);
    CloseServiceHandle(hService);
}

START_TEST(QueryServiceConfig)
{
    SC_HANDLE hScm;

    hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    ok(hScm != NULL, "OpenSCManagerW failed, error %lu\n", GetLastError());
    if (!hScm)
    {
        skip("No service control manager\n");
        return;
    }

    TestShape(hScm, L"advapi32_apitest_QSC_A", L"LNT SCM compare",
              L"C:\\Program Files (x86)\\Common Files\\LntScmCmp\\svc.exe -u https://example.invalid/x",
              NULL, NULL, NULL);
    TestShape(hScm, L"advapi32_apitest_QSC_B", L"LNT align B", L"C:\\a.exe", L"LntGroup",
              L"Tcpip\0+NetworkProvider\0", L"NT AUTHORITY\\LocalService");
    TestShape(hScm, L"advapi32_apitest_QSC_C", L"D", L"C:\\ab.exe", L"G", L"Afd\0",
              L"NT AUTHORITY\\NetworkService");

    CloseServiceHandle(hScm);
}
