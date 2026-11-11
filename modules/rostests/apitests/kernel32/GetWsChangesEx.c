/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests GetWsChangesEx through kernel32, kernelbase and psapi
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <psapi.h>

typedef BOOL (WINAPI *PFN_INITIALIZE_WS_WATCH)(HANDLE);
typedef BOOL (WINAPI *PFN_GET_WS_CHANGES_EX)(HANDLE, PPSAPI_WS_WATCH_INFORMATION_EX, PDWORD);

#define WATCH_BUFFER_SIZE 0x10000

static
BOOL
FindPage(
    _In_reads_bytes_(Length) PPSAPI_WS_WATCH_INFORMATION_EX Records,
    _In_ DWORD Length,
    _In_ ULONG_PTR Page,
    _In_ ULONG_PTR PageMask,
    _Out_ PBOOL Terminated,
    _Out_ PULONG_PTR ThreadId)
{
    DWORD Count = Length / sizeof(*Records), Index;
    BOOL Found = FALSE;

    *Terminated = FALSE;
    *ThreadId = 0;
    for (Index = 0; Index < Count; Index++)
    {
        if (Records[Index].BasicInfo.FaultingPc == NULL)
        {
            *Terminated = TRUE;
            break;
        }
        if (!Found && ((ULONG_PTR)Records[Index].BasicInfo.FaultingVa & ~PageMask) == Page)
        {
            Found = TRUE;
            *ThreadId = Records[Index].FaultingThreadId;
        }
    }

    return Found;
}

static
void
TestEntryPoint(
    _In_ PFN_GET_WS_CHANGES_EX Function,
    _In_ PCSTR Name,
    _In_ PPSAPI_WS_WATCH_INFORMATION_EX Buffer,
    _In_ ULONG_PTR PageMask)
{
    volatile UCHAR *Page;
    ULONG_PTR ThreadId;
    BOOL Success, Found, Terminated;
    DWORD Length;

    Page = VirtualAlloc(NULL, PageMask + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Page != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (!Page)
        return;

    Length = WATCH_BUFFER_SIZE;
    ok(Function(GetCurrentProcess(), Buffer, &Length), "%s: draining failed with %lu\n", Name, GetLastError());

    Page[0] = 1;

    Length = sizeof(PSAPI_WS_WATCH_INFORMATION_EX);
    SetLastError(0xdeadbeef);
    Success = Function(GetCurrentProcess(), Buffer, &Length);
    ok(!Success, "%s: a buffer of one record held the fault and its terminator\n", Name);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "%s: last error %lu\n", Name, GetLastError());
    trace("%s: length after ERROR_INSUFFICIENT_BUFFER %lu\n", Name, Length);

    memset(Buffer, 0xcc, WATCH_BUFFER_SIZE);
    Length = WATCH_BUFFER_SIZE;
    Success = Function(GetCurrentProcess(), Buffer, &Length);
    ok(Success, "%s: GetWsChangesEx failed with %lu\n", Name, GetLastError());
    if (Success)
    {
        ok(Length >= sizeof(PSAPI_WS_WATCH_INFORMATION_EX) && Length <= WATCH_BUFFER_SIZE &&
           Length % sizeof(PSAPI_WS_WATCH_INFORMATION_EX) == 0,
           "%s: returned length %lu\n", Name, Length);
        Found = FindPage(Buffer, WATCH_BUFFER_SIZE, (ULONG_PTR)Page, PageMask, &Terminated, &ThreadId);
        ok(Terminated, "%s: no terminating record\n", Name);
        ok(Found, "%s: the fault on %p was not reported\n", Name, Page);
        ok(!Found || ThreadId == GetCurrentThreadId(), "%s: faulting thread %Iu, expected %lu\n",
           Name, ThreadId, GetCurrentThreadId());
    }

    memset(Buffer, 0xcc, WATCH_BUFFER_SIZE);
    Length = WATCH_BUFFER_SIZE;
    Success = Function(GetCurrentProcess(), Buffer, &Length);
    ok(Success, "%s: GetWsChangesEx failed with %lu\n", Name, GetLastError());
    if (Success)
    {
        Found = FindPage(Buffer, WATCH_BUFFER_SIZE, (ULONG_PTR)Page, PageMask, &Terminated, &ThreadId);
        ok(Terminated, "%s: no terminating record\n", Name);
        ok(!Found, "%s: a record survived the query that returned it\n", Name);
    }

    VirtualFree((PVOID)Page, 0, MEM_RELEASE);
}

START_TEST(GetWsChangesEx)
{
    PFN_GET_WS_CHANGES_EX pK32GetWsChangesEx, pKbGetWsChangesEx = NULL, pKbK32GetWsChangesEx = NULL, pPsapiGetWsChangesEx = NULL;
    PFN_INITIALIZE_WS_WATCH pInitialize;
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
    HMODULE Psapi = LoadLibraryW(L"psapi.dll");
    PPSAPI_WS_WATCH_INFORMATION_EX Buffer;
    SYSTEM_INFO SystemInfo;
    DWORD Length;
    BOOL Success;

    pK32GetWsChangesEx = (PFN_GET_WS_CHANGES_EX)GetProcAddress(Kernel32, "K32GetWsChangesEx");
    pInitialize = (PFN_INITIALIZE_WS_WATCH)GetProcAddress(Kernel32, "K32InitializeProcessForWsWatch");
    if (KernelBase)
    {
        pKbGetWsChangesEx = (PFN_GET_WS_CHANGES_EX)GetProcAddress(KernelBase, "GetWsChangesEx");
        pKbK32GetWsChangesEx = (PFN_GET_WS_CHANGES_EX)GetProcAddress(KernelBase, "K32GetWsChangesEx");
    }
    if (Psapi)
        pPsapiGetWsChangesEx = (PFN_GET_WS_CHANGES_EX)GetProcAddress(Psapi, "GetWsChangesEx");

    if (!pK32GetWsChangesEx)
        skip("kernel32!K32GetWsChangesEx is missing\n");
    if (!pInitialize)
        skip("kernel32!K32InitializeProcessForWsWatch is missing\n");
    if (!pKbGetWsChangesEx)
        skip("kernelbase!GetWsChangesEx is missing\n");
    if (!pKbK32GetWsChangesEx)
        skip("kernelbase!K32GetWsChangesEx is missing\n");
    if (!pPsapiGetWsChangesEx)
        skip("psapi!GetWsChangesEx is missing\n");
    if (!pK32GetWsChangesEx || !pInitialize)
        return;

    Buffer = HeapAlloc(GetProcessHeap(), 0, WATCH_BUFFER_SIZE);
    if (!Buffer)
    {
        skip("Out of memory\n");
        return;
    }

    Length = WATCH_BUFFER_SIZE;
    SetLastError(0xdeadbeef);
    Success = pK32GetWsChangesEx(NULL, Buffer, &Length);
    ok(!Success, "GetWsChangesEx on a NULL handle succeeded\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "Last error %lu\n", GetLastError());

    ok(pInitialize(GetCurrentProcess()), "InitializeProcessForWsWatch failed with %lu\n", GetLastError());

    GetSystemInfo(&SystemInfo);
    TestEntryPoint(pK32GetWsChangesEx, "kernel32!K32GetWsChangesEx", Buffer, SystemInfo.dwPageSize - 1);
    if (pKbGetWsChangesEx)
        TestEntryPoint(pKbGetWsChangesEx, "kernelbase!GetWsChangesEx", Buffer, SystemInfo.dwPageSize - 1);
    if (pPsapiGetWsChangesEx)
        TestEntryPoint(pPsapiGetWsChangesEx, "psapi!GetWsChangesEx", Buffer, SystemInfo.dwPageSize - 1);

    HeapFree(GetProcessHeap(), 0, Buffer);
    if (Psapi)
        FreeLibrary(Psapi);
}
