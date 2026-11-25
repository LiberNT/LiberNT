/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that hosts file edits reach the resolver cache while the DNS Client service runs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windns.h>
#include <stdio.h>

#define HOSTS_WAIT_MS 5000
#define HOSTS_TEST_ADDRESS "192.0.2.77"
#define HOSTS_TEST_ADDRESS_BE 0x4D0200C0

static BOOL GetHostsPath(PWSTR Path, DWORD Count)
{
    UINT Length = GetSystemDirectoryW(Path, Count);

    if (!Length || Length >= Count)
        return FALSE;
    return lstrlenW(Path) + lstrlenW(L"\\drivers\\etc\\hosts") < (int)Count &&
           lstrcatW(Path, L"\\drivers\\etc\\hosts") != NULL;
}

static PBYTE ReadWholeFile(PCWSTR Path, PDWORD Size)
{
    HANDLE File;
    PBYTE Data;
    DWORD Read;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return NULL;
    *Size = GetFileSize(File, NULL);
    Data = HeapAlloc(GetProcessHeap(), 0, *Size + 1);
    if (Data && (!ReadFile(File, Data, *Size, &Read, NULL) || Read != *Size))
    {
        HeapFree(GetProcessHeap(), 0, Data);
        Data = NULL;
    }
    CloseHandle(File);
    return Data;
}

static DWORD WriteHostsFile(PCWSTR Path, DWORD Access, const void *Data, DWORD Size, BOOL Truncate)
{
    HANDLE File;
    DWORD Written, Error = ERROR_SUCCESS;

    File = CreateFileW(Path, Access, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return GetLastError();
    if (!WriteFile(File, Data, Size, &Written, NULL) || Written != Size)
        Error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    else if (Truncate && !SetEndOfFile(File))
        Error = GetLastError();
    CloseHandle(File);
    return Error;
}

static DNS_STATUS QueryCachedA(PCWSTR Name, PDWORD Address)
{
    PDNS_RECORD Records = NULL, Record;
    DNS_STATUS Status;

    *Address = 0;
    Status = DnsQuery_W(Name, DNS_TYPE_A, DNS_QUERY_NO_WIRE_QUERY, NULL, &Records, NULL);
    if (Status == ERROR_SUCCESS)
    {
        for (Record = Records; Record; Record = Record->pNext)
        {
            if (Record->wType == DNS_TYPE_A)
                *Address = Record->Data.A.IpAddress;
        }
        DnsRecordListFree(Records, DnsFreeRecordList);
    }
    return Status;
}

static BOOL WaitForName(PCWSTR Name, BOOL Present, PDWORD Address, PDWORD Elapsed)
{
    DWORD Start = GetTickCount();
    DNS_STATUS Status;

    for (;;)
    {
        Status = QueryCachedA(Name, Address);
        *Elapsed = GetTickCount() - Start;
        if ((Status == ERROR_SUCCESS) == Present)
            return TRUE;
        if (*Elapsed >= HOSTS_WAIT_MS)
            return FALSE;
        Sleep(20);
    }
}

START_TEST(HostsFile)
{
    WCHAR Path[MAX_PATH], Name[64];
    CHAR Line[128];
    PBYTE Original;
    DWORD OriginalSize, Address, Elapsed, Error;
    BOOL Found;

    ok(GetHostsPath(Path, ARRAYSIZE(Path)), "GetHostsPath failed\n");
    Original = ReadWholeFile(Path, &OriginalSize);
    if (!Original)
    {
        skip("Cannot read %ls (%lu)\n", Path, GetLastError());
        return;
    }

    _snwprintf(Name, ARRAYSIZE(Name) - 1, L"lnthosts%08lx", GetTickCount() ^ GetCurrentProcessId());
    Name[ARRAYSIZE(Name) - 1] = L'\0';
    ok(QueryCachedA(Name, &Address) != ERROR_SUCCESS, "%ls resolved before it was added\n", Name);

    _snprintf(Line, sizeof(Line) - 1, "\r\n%s %ls\r\n", HOSTS_TEST_ADDRESS, Name);
    Line[sizeof(Line) - 1] = '\0';
    Error = WriteHostsFile(Path, FILE_APPEND_DATA, Line, (DWORD)strlen(Line), FALSE);
    if (Error == ERROR_ACCESS_DENIED)
    {
        skip("%ls is not writable; run the test elevated\n", Path);
        HeapFree(GetProcessHeap(), 0, Original);
        return;
    }
    ok(Error == ERROR_SUCCESS, "Appending to %ls failed with %lu\n", Path, Error);

    Found = WaitForName(Name, TRUE, &Address, &Elapsed);
    ok(Found, "%ls was not in the resolver cache %lu ms after it was added to hosts\n", Name, Elapsed);
    ok(Address == HOSTS_TEST_ADDRESS_BE, "%ls resolved to 0x%08lx\n", Name, Address);
    trace("Added entry visible after %lu ms\n", Elapsed);

    Error = WriteHostsFile(Path, GENERIC_WRITE, Original, OriginalSize, TRUE);
    ok(Error == ERROR_SUCCESS, "Restoring %ls failed with %lu\n", Path, Error);

    Found = WaitForName(Name, FALSE, &Address, &Elapsed);
    ok(Found, "%ls was still in the resolver cache %lu ms after it was removed from hosts\n", Name, Elapsed);
    trace("Removed entry gone after %lu ms\n", Elapsed);

    HeapFree(GetProcessHeap(), 0, Original);
}
