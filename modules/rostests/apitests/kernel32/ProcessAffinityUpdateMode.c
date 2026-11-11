/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests QueryProcessAffinityUpdateMode and SetProcessAffinityUpdateMode
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_SET_AFFINITY_UPDATE_MODE)(HANDLE, DWORD);
typedef BOOL (WINAPI *PFN_QUERY_AFFINITY_UPDATE_MODE)(HANDLE, LPDWORD);

static PFN_SET_AFFINITY_UPDATE_MODE pSetProcessAffinityUpdateMode;
static PFN_QUERY_AFFINITY_UPDATE_MODE pQueryProcessAffinityUpdateMode;

static
DWORD
QueryMode(
    _In_ HANDLE Process,
    _Out_ PBOOL Success)
{
    DWORD Flags = 0xdeadbeef;

    *Success = pQueryProcessAffinityUpdateMode(Process, &Flags);
    return Flags;
}

static
void
TestChildDoesNotInherit(void)
{
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 32];
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    DWORD Flags;
    BOOL Success;
    UINT Length;

    Length = GetSystemDirectoryW(Path, ARRAYSIZE(Path));
    if (!Length || Length >= ARRAYSIZE(Path) - 16)
    {
        skip("No system directory (%lu)\n", GetLastError());
        return;
    }
    StringCchCatW(Path, ARRAYSIZE(Path), L"\\cmd.exe");
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" /c exit 0", Path);

    if (!CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED,
                        NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        skip("CreateProcessW failed with %lu\n", GetLastError());
        return;
    }

    Flags = QueryMode(ProcessInfo.hProcess, &Success);
    ok(Success, "Query on the child failed with %lu\n", GetLastError());
    ok(Flags == 0, "Child inherited affinity update mode 0x%lx\n", Flags);

    TerminateProcess(ProcessInfo.hProcess, 0);
    WaitForSingleObject(ProcessInfo.hProcess, 5000);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
}

static
void
TestQueryHandles(void)
{
    HANDLE Process;
    DWORD Flags;
    BOOL Success;

    SetLastError(0xdeadbeef);
    Flags = QueryMode(NULL, &Success);
    ok(!Success, "Query on a NULL handle succeeded\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "Last error %lu\n", GetLastError());

    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    ok(Process != NULL, "OpenProcess failed with %lu\n", GetLastError());
    if (Process)
    {
        Flags = QueryMode(Process, &Success);
        ok(Success, "Query with PROCESS_QUERY_LIMITED_INFORMATION failed with %lu\n", GetLastError());
        ok(Flags == 0, "Flags 0x%lx\n", Flags);
        CloseHandle(Process);
    }

    Process = OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId());
    ok(Process != NULL, "OpenProcess failed with %lu\n", GetLastError());
    if (Process)
    {
        SetLastError(0xdeadbeef);
        QueryMode(Process, &Success);
        ok(!Success, "Query without a query right succeeded\n");
        ok(GetLastError() == ERROR_ACCESS_DENIED, "Last error %lu\n", GetLastError());
        CloseHandle(Process);
    }

    ok(pQueryProcessAffinityUpdateMode(GetCurrentProcess(), NULL),
       "Query without an output failed with %lu\n", GetLastError());
}

static
void
TestSetParameters(void)
{
    HANDLE Process;
    BOOL Success;

    SetLastError(0xdeadbeef);
    Success = pSetProcessAffinityUpdateMode(GetCurrentProcess(), 0x2);
    ok(!Success, "Undefined flag 0x2 accepted\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Last error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Success = pSetProcessAffinityUpdateMode(GetCurrentProcess(),
                                            PROCESS_AFFINITY_ENABLE_AUTO_UPDATE | 0x80000000);
    ok(!Success, "Undefined flag 0x80000000 accepted\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "Last error %lu\n", GetLastError());

    Process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());
    ok(Process != NULL, "OpenProcess failed with %lu\n", GetLastError());
    if (Process)
    {
        SetLastError(0xdeadbeef);
        Success = pSetProcessAffinityUpdateMode(Process, PROCESS_AFFINITY_ENABLE_AUTO_UPDATE);
        ok(!Success, "A handle other than GetCurrentProcess() was accepted\n");
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "Last error %lu\n", GetLastError());
        CloseHandle(Process);
    }
}

START_TEST(ProcessAffinityUpdateMode)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
    PFN_SET_AFFINITY_UPDATE_MODE pKbSet = NULL;
    PFN_QUERY_AFFINITY_UPDATE_MODE pKbQuery = NULL;
    DWORD Flags;
    BOOL Success;

    pSetProcessAffinityUpdateMode = (PFN_SET_AFFINITY_UPDATE_MODE)GetProcAddress(Kernel32, "SetProcessAffinityUpdateMode");
    pQueryProcessAffinityUpdateMode = (PFN_QUERY_AFFINITY_UPDATE_MODE)GetProcAddress(Kernel32, "QueryProcessAffinityUpdateMode");
    if (KernelBase)
    {
        pKbSet = (PFN_SET_AFFINITY_UPDATE_MODE)GetProcAddress(KernelBase, "SetProcessAffinityUpdateMode");
        pKbQuery = (PFN_QUERY_AFFINITY_UPDATE_MODE)GetProcAddress(KernelBase, "QueryProcessAffinityUpdateMode");
    }
    if (!pSetProcessAffinityUpdateMode)
        skip("kernel32!SetProcessAffinityUpdateMode is missing\n");
    if (!pQueryProcessAffinityUpdateMode)
        skip("kernel32!QueryProcessAffinityUpdateMode is missing\n");
    if (!pKbSet)
        skip("kernelbase!SetProcessAffinityUpdateMode is missing\n");
    if (!pKbQuery)
        skip("kernelbase!QueryProcessAffinityUpdateMode is missing\n");
    if (!pSetProcessAffinityUpdateMode || !pQueryProcessAffinityUpdateMode)
        return;

    Flags = QueryMode(GetCurrentProcess(), &Success);
    ok(Success, "Query failed with %lu\n", GetLastError());
    ok(Flags == 0, "A process starts with affinity update mode 0x%lx\n", Flags);

    TestQueryHandles();
    TestSetParameters();

    Flags = QueryMode(GetCurrentProcess(), &Success);
    ok(Success && Flags == 0, "Rejected calls changed the mode: %d 0x%lx\n", Success, Flags);

    ok(pSetProcessAffinityUpdateMode(GetCurrentProcess(), PROCESS_AFFINITY_ENABLE_AUTO_UPDATE),
       "Enabling failed with %lu\n", GetLastError());
    Flags = QueryMode(GetCurrentProcess(), &Success);
    ok(Success, "Query failed with %lu\n", GetLastError());
    ok(Flags == PROCESS_AFFINITY_ENABLE_AUTO_UPDATE, "Flags after enabling 0x%lx\n", Flags);

    if (pKbQuery)
    {
        Flags = 0xdeadbeef;
        ok(pKbQuery(GetCurrentProcess(), &Flags), "kernelbase query failed with %lu\n", GetLastError());
        ok(Flags == PROCESS_AFFINITY_ENABLE_AUTO_UPDATE, "kernelbase flags 0x%lx\n", Flags);
    }

    TestChildDoesNotInherit();

    ok(pSetProcessAffinityUpdateMode(GetCurrentProcess(), PROCESS_AFFINITY_ENABLE_AUTO_UPDATE),
       "Enabling twice failed with %lu\n", GetLastError());

    ok(pSetProcessAffinityUpdateMode(GetCurrentProcess(), 0),
       "Disabling after enabling failed with %lu\n", GetLastError());
    Flags = QueryMode(GetCurrentProcess(), &Success);
    ok(Success, "Query failed with %lu\n", GetLastError());
    ok(Flags == 0, "Flags after disabling 0x%lx\n", Flags);

    SetLastError(0xdeadbeef);
    Success = pSetProcessAffinityUpdateMode(GetCurrentProcess(), PROCESS_AFFINITY_ENABLE_AUTO_UPDATE);
    ok(!Success, "Enabling after disabling succeeded\n");
    trace("Enabling after disabling: last error %lu\n", GetLastError());
    Flags = QueryMode(GetCurrentProcess(), &Success);
    ok(Success && Flags == 0, "Mode after the refused enable: %d 0x%lx\n", Success, Flags);

    if (pKbSet)
    {
        ok(!pKbSet(GetCurrentProcess(), PROCESS_AFFINITY_ENABLE_AUTO_UPDATE),
           "kernelbase enabled the mode after it was disabled\n");
        ok(pKbSet(GetCurrentProcess(), 0), "kernelbase disabling failed with %lu\n", GetLastError());
    }
}
