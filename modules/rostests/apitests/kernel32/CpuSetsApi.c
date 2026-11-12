/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the process default and thread selected CPU set functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#ifndef THREAD_SET_LIMITED_INFORMATION
#define THREAD_SET_LIMITED_INFORMATION 0x0400
#endif

typedef BOOL (WINAPI *PFN_GET_SYSTEM_CPU_SET_INFORMATION)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
typedef BOOL (WINAPI *PFN_GET_CPU_SETS)(HANDLE, PULONG, ULONG, PULONG);
typedef BOOL (WINAPI *PFN_SET_CPU_SETS)(HANDLE, const ULONG *, ULONG);

typedef struct _CPU_SET_API
{
    PCSTR Module;
    PFN_GET_CPU_SETS GetProcessDefault;
    PFN_SET_CPU_SETS SetProcessDefault;
    PFN_GET_CPU_SETS GetThreadSelected;
    PFN_SET_CPU_SETS SetThreadSelected;
} CPU_SET_API, *PCPU_SET_API;

static
BOOL
LoadApi(
    _In_ PCWSTR ModuleName,
    _In_ PCSTR Module,
    _Out_ PCPU_SET_API Api)
{
    HMODULE Handle = GetModuleHandleW(ModuleName);

    ZeroMemory(Api, sizeof(*Api));
    Api->Module = Module;
    if (!Handle)
        return FALSE;

    Api->GetProcessDefault = (PFN_GET_CPU_SETS)GetProcAddress(Handle, "GetProcessDefaultCpuSets");
    Api->SetProcessDefault = (PFN_SET_CPU_SETS)GetProcAddress(Handle, "SetProcessDefaultCpuSets");
    Api->GetThreadSelected = (PFN_GET_CPU_SETS)GetProcAddress(Handle, "GetThreadSelectedCpuSets");
    Api->SetThreadSelected = (PFN_SET_CPU_SETS)GetProcAddress(Handle, "SetThreadSelectedCpuSets");
    if (!Api->GetProcessDefault)
        skip("%s!GetProcessDefaultCpuSets is missing\n", Module);
    if (!Api->SetProcessDefault)
        skip("%s!SetProcessDefaultCpuSets is missing\n", Module);
    if (!Api->GetThreadSelected)
        skip("%s!GetThreadSelectedCpuSets is missing\n", Module);
    if (!Api->SetThreadSelected)
        skip("%s!SetThreadSelectedCpuSets is missing\n", Module);

    return Api->GetProcessDefault && Api->SetProcessDefault &&
           Api->GetThreadSelected && Api->SetThreadSelected;
}

static
ULONG
GetSystemCpuSetIds(
    _Out_writes_(Capacity) PULONG Ids,
    _In_ ULONG Capacity)
{
    PFN_GET_SYSTEM_CPU_SET_INFORMATION pGetSystemCpuSetInformation;
    PSYSTEM_CPU_SET_INFORMATION Entry;
    PUCHAR Buffer, Cursor;
    ULONG Length = 0, Count = 0;
    BOOL Success;

    pGetSystemCpuSetInformation = (PFN_GET_SYSTEM_CPU_SET_INFORMATION)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetSystemCpuSetInformation");
    if (!pGetSystemCpuSetInformation)
        skip("kernel32!GetSystemCpuSetInformation is missing\n");
    if (!pGetSystemCpuSetInformation)
        return 0;

    SetLastError(0xdeadbeef);
    Success = pGetSystemCpuSetInformation(NULL, 0, &Length, GetCurrentProcess(), 0);
    ok(!Success, "GetSystemCpuSetInformation with no buffer succeeded\n");
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "Last error %lu\n", GetLastError());
    ok(Length >= sizeof(SYSTEM_CPU_SET_INFORMATION), "Required length %lu\n", Length);
    if (Length < sizeof(SYSTEM_CPU_SET_INFORMATION))
        return 0;

    Buffer = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!Buffer)
        return 0;

    Success = pGetSystemCpuSetInformation((PSYSTEM_CPU_SET_INFORMATION)Buffer, Length, &Length, GetCurrentProcess(), 0);
    ok(Success, "GetSystemCpuSetInformation failed with %lu\n", GetLastError());
    for (Cursor = Buffer; Success && Cursor + sizeof(*Entry) <= Buffer + Length && Count < Capacity; Cursor += Entry->Size)
    {
        Entry = (PSYSTEM_CPU_SET_INFORMATION)Cursor;
        if (Entry->Size < sizeof(*Entry))
            break;
        if (Entry->Type == CpuSetInformation)
            Ids[Count++] = Entry->CpuSet.Id;
    }

    HeapFree(GetProcessHeap(), 0, Buffer);
    return Count;
}

static
void
TestEmptyAndInvalid(
    _In_ PCPU_SET_API Api)
{
    ULONG Ids[4], Required;
    BOOL Success;

    Required = 0xdeadbeef;
    ok(Api->GetProcessDefault(GetCurrentProcess(), NULL, 0, &Required),
       "%s: GetProcessDefaultCpuSets failed with %lu\n", Api->Module, GetLastError());
    ok(Required == 0, "%s: a process with no default reports %lu ids\n", Api->Module, Required);

    Required = 0xdeadbeef;
    ok(Api->GetThreadSelected(GetCurrentThread(), Ids, ARRAYSIZE(Ids), &Required),
       "%s: GetThreadSelectedCpuSets failed with %lu\n", Api->Module, GetLastError());
    ok(Required == 0, "%s: a thread with no selection reports %lu ids\n", Api->Module, Required);

    ok(Api->SetProcessDefault(GetCurrentProcess(), NULL, 0),
       "%s: clearing the process default failed with %lu\n", Api->Module, GetLastError());
    ok(Api->SetThreadSelected(GetCurrentThread(), NULL, 0),
       "%s: clearing the thread selection failed with %lu\n", Api->Module, GetLastError());

    SetLastError(0xdeadbeef);
    Required = 0xdeadbeef;
    Success = Api->GetProcessDefault(NULL, NULL, 0, &Required);
    ok(!Success, "%s: GetProcessDefaultCpuSets on a NULL handle succeeded\n", Api->Module);
    ok(GetLastError() == ERROR_INVALID_HANDLE, "%s: last error %lu\n", Api->Module, GetLastError());
    ok(Required == 0, "%s: required count %lu after a failure\n", Api->Module, Required);

    SetLastError(0xdeadbeef);
    Success = Api->SetThreadSelected(NULL, NULL, 0);
    ok(!Success, "%s: SetThreadSelectedCpuSets on a NULL handle succeeded\n", Api->Module);
    ok(GetLastError() == ERROR_INVALID_HANDLE, "%s: last error %lu\n", Api->Module, GetLastError());

    SetLastError(0xdeadbeef);
    Success = Api->SetProcessDefault(GetCurrentProcess(), NULL, 1);
    ok(!Success, "%s: SetProcessDefaultCpuSets(NULL, 1) succeeded\n", Api->Module);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "%s: last error %lu\n", Api->Module, GetLastError());

    SetLastError(0xdeadbeef);
    Success = Api->GetThreadSelected(GetCurrentThread(), NULL, 1, &Required);
    ok(!Success, "%s: GetThreadSelectedCpuSets(NULL, 1) succeeded\n", Api->Module);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "%s: last error %lu\n", Api->Module, GetLastError());
}

static
void
TestLimitedAccess(
    _In_ PCPU_SET_API Api)
{
    HANDLE Handle;
    ULONG Required;

    Handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    ok(Handle != NULL, "OpenProcess failed with %lu\n", GetLastError());
    if (Handle)
    {
        ok(Api->GetProcessDefault(Handle, NULL, 0, &Required),
           "%s: query with PROCESS_QUERY_LIMITED_INFORMATION failed with %lu\n", Api->Module, GetLastError());
        CloseHandle(Handle);
    }

    Handle = OpenProcess(PROCESS_SET_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    ok(Handle != NULL, "OpenProcess failed with %lu\n", GetLastError());
    if (Handle)
    {
        ok(Api->SetProcessDefault(Handle, NULL, 0),
           "%s: set with PROCESS_SET_LIMITED_INFORMATION failed with %lu\n", Api->Module, GetLastError());
        CloseHandle(Handle);
    }

    Handle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
    ok(Handle != NULL, "OpenThread failed with %lu\n", GetLastError());
    if (Handle)
    {
        ok(Api->GetThreadSelected(Handle, NULL, 0, &Required),
           "%s: query with THREAD_QUERY_LIMITED_INFORMATION failed with %lu\n", Api->Module, GetLastError());
        CloseHandle(Handle);
    }

    Handle = OpenThread(THREAD_SET_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
    ok(Handle != NULL, "OpenThread failed with %lu\n", GetLastError());
    if (Handle)
    {
        ok(Api->SetThreadSelected(Handle, NULL, 0),
           "%s: set with THREAD_SET_LIMITED_INFORMATION failed with %lu\n", Api->Module, GetLastError());
        CloseHandle(Handle);
    }
}

static
void
TestAssignment(
    _In_ PCPU_SET_API Api,
    _In_ ULONG Id,
    _In_ ULONG UnusedId)
{
    ULONG Ids[4], Required;
    BOOL Success;

    ok(Api->SetProcessDefault(GetCurrentProcess(), &Id, 1),
       "%s: SetProcessDefaultCpuSets(0x%lx) failed with %lu\n", Api->Module, Id, GetLastError());

    SetLastError(0xdeadbeef);
    Required = 0xdeadbeef;
    Success = Api->GetProcessDefault(GetCurrentProcess(), NULL, 0, &Required);
    ok(!Success, "%s: GetProcessDefaultCpuSets with no room succeeded\n", Api->Module);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "%s: last error %lu\n", Api->Module, GetLastError());
    ok(Required == 1, "%s: required count %lu\n", Api->Module, Required);

    Ids[0] = 0xdeadbeef;
    Required = 0xdeadbeef;
    ok(Api->GetProcessDefault(GetCurrentProcess(), Ids, ARRAYSIZE(Ids), &Required),
       "%s: GetProcessDefaultCpuSets failed with %lu\n", Api->Module, GetLastError());
    ok(Required == 1, "%s: returned count %lu\n", Api->Module, Required);
    ok(Ids[0] == Id, "%s: process default 0x%lx, expected 0x%lx\n", Api->Module, Ids[0], Id);

    ok(Api->SetProcessDefault(GetCurrentProcess(), NULL, 0),
       "%s: clearing the process default failed with %lu\n", Api->Module, GetLastError());
    Required = 0xdeadbeef;
    ok(Api->GetProcessDefault(GetCurrentProcess(), Ids, ARRAYSIZE(Ids), &Required),
       "%s: GetProcessDefaultCpuSets failed with %lu\n", Api->Module, GetLastError());
    ok(Required == 0, "%s: %lu ids left after clearing\n", Api->Module, Required);

    ok(Api->SetThreadSelected(GetCurrentThread(), &Id, 1),
       "%s: SetThreadSelectedCpuSets(0x%lx) failed with %lu\n", Api->Module, Id, GetLastError());

    SetLastError(0xdeadbeef);
    Required = 0xdeadbeef;
    Success = Api->GetThreadSelected(GetCurrentThread(), NULL, 0, &Required);
    ok(!Success, "%s: GetThreadSelectedCpuSets with no room succeeded\n", Api->Module);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "%s: last error %lu\n", Api->Module, GetLastError());
    ok(Required == 1, "%s: required count %lu\n", Api->Module, Required);

    Ids[0] = 0xdeadbeef;
    Required = 0xdeadbeef;
    ok(Api->GetThreadSelected(GetCurrentThread(), Ids, ARRAYSIZE(Ids), &Required),
       "%s: GetThreadSelectedCpuSets failed with %lu\n", Api->Module, GetLastError());
    ok(Required == 1, "%s: returned count %lu\n", Api->Module, Required);
    ok(Ids[0] == Id, "%s: thread selection 0x%lx, expected 0x%lx\n", Api->Module, Ids[0], Id);

    ok(Api->SetThreadSelected(GetCurrentThread(), NULL, 0),
       "%s: clearing the thread selection failed with %lu\n", Api->Module, GetLastError());
    Required = 0xdeadbeef;
    ok(Api->GetThreadSelected(GetCurrentThread(), Ids, ARRAYSIZE(Ids), &Required),
       "%s: GetThreadSelectedCpuSets failed with %lu\n", Api->Module, GetLastError());
    ok(Required == 0, "%s: %lu ids left after clearing\n", Api->Module, Required);

    SetLastError(0xdeadbeef);
    Success = Api->SetProcessDefault(GetCurrentProcess(), &UnusedId, 1);
    ok(!Success, "%s: unknown CPU set id 0x%lx accepted\n", Api->Module, UnusedId);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "%s: last error %lu\n", Api->Module, GetLastError());
    Required = 0xdeadbeef;
    ok(Api->GetProcessDefault(GetCurrentProcess(), Ids, ARRAYSIZE(Ids), &Required) && Required == 0,
       "%s: a refused id changed the process default (%lu ids)\n", Api->Module, Required);
}

START_TEST(CpuSetsApi)
{
    CPU_SET_API Kernel32, KernelBase;
    ULONG Ids[64], Count, UnusedId = 0, Index;
    BOOL HaveKernel32, HaveKernelBase;

    HaveKernel32 = LoadApi(L"kernel32.dll", "kernel32", &Kernel32);
    HaveKernelBase = LoadApi(L"kernelbase.dll", "kernelbase", &KernelBase);

    if (HaveKernel32)
    {
        TestEmptyAndInvalid(&Kernel32);
        TestLimitedAccess(&Kernel32);
    }
    if (HaveKernelBase)
        TestEmptyAndInvalid(&KernelBase);

    Count = GetSystemCpuSetIds(Ids, ARRAYSIZE(Ids));
    ok(Count != 0, "No CPU set ids were reported\n");
    if (!Count)
        return;

    for (Index = 0; Index < Count; Index++)
    {
        if (Ids[Index] >= UnusedId)
            UnusedId = Ids[Index] + 1;
    }

    if (HaveKernel32)
        TestAssignment(&Kernel32, Ids[Count - 1], UnusedId);
    if (HaveKernelBase)
        TestAssignment(&KernelBase, Ids[0], UnusedId);
}
