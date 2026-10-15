/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the internal app model and app compatibility data exports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef HRESULT (WINAPI *PFN_ADDDEPENDENCY)(PCWSTR, PVOID, PVOID, PVOID);
typedef HRESULT (WINAPI *PFN_APPXGETPACKAGESID)(PCWSTR, PSID *);
typedef VOID (WINAPI *PFN_APPXFREEMEMORY)(PVOID);
typedef DWORD (WINAPI *PFN_READAPPCOMPAT)(HANDLE, PVOID *, PVOID);
typedef VOID (WINAPI *PFN_FREEAPPCOMPAT)(PVOID);

static FARPROC
FindExport(PCSTR Name)
{
    static const PCWSTR Modules[] = { L"kernel.appcore.dll", L"kernelbase.dll", L"kernel32.dll" };
    FARPROC Proc;
    HMODULE Module;
    ULONG Index;

    for (Index = 0; Index < _countof(Modules); Index++)
    {
        Module = LoadLibraryW(Modules[Index]);
        if (!Module)
            continue;
        Proc = GetProcAddress(Module, Name);
        if (Proc)
            return Proc;
    }
    return NULL;
}

static VOID
TestAppModel(VOID)
{
    PFN_ADDDEPENDENCY pAddDependencyToProcessPackageGraph;
    PFN_APPXGETPACKAGESID pAppXGetPackageSid;
    PFN_APPXFREEMEMORY pAppXFreeMemory;
    PSID Sid;
    HRESULT hr;

    pAddDependencyToProcessPackageGraph = (PFN_ADDDEPENDENCY)FindExport("AddDependencyToProcessPackageGraph");
    pAppXGetPackageSid = (PFN_APPXGETPACKAGESID)FindExport("AppXGetPackageSid");
    pAppXFreeMemory = (PFN_APPXFREEMEMORY)FindExport("AppXFreeMemory");
    ok(pAddDependencyToProcessPackageGraph != NULL, "AddDependencyToProcessPackageGraph is missing\n");
    ok(pAppXGetPackageSid != NULL, "AppXGetPackageSid is missing\n");
    ok(pAppXFreeMemory != NULL, "AppXFreeMemory is missing\n");

    if (pAddDependencyToProcessPackageGraph)
    {
        hr = pAddDependencyToProcessPackageGraph(L"Nonexistent.Package_8wekyb3d8bbwe", NULL, NULL, NULL);
        ok(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "missing family: 0x%08lx\n", hr);
        hr = pAddDependencyToProcessPackageGraph(L"bogus", NULL, NULL, NULL);
        ok(hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "bogus family: 0x%08lx\n", hr);
    }

    if (pAppXGetPackageSid)
    {
        Sid = (PSID)(ULONG_PTR)7;
        hr = pAppXGetPackageSid(L"bogus", &Sid);
        ok(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "bogus name: 0x%08lx\n", hr);
        ok(Sid == (PSID)(ULONG_PTR)7, "bogus name wrote %p\n", Sid);
        hr = pAppXGetPackageSid(L"Abc_1.0.0.0_neutral__8wekyb3d8bbwe", &Sid);
        ok(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "unknown package: 0x%08lx\n", hr);
        ok(Sid == (PSID)(ULONG_PTR)7, "unknown package wrote %p\n", Sid);
    }

    if (pAppXFreeMemory)
        pAppXFreeMemory(NULL);
}

static VOID
TestAppCompatData(VOID)
{
    PFN_READAPPCOMPAT pBaseReadAppCompatDataForProcess;
    PFN_FREEAPPCOMPAT pBaseFreeAppCompatDataForProcess;
    HANDLE Limited;
    PVOID Data;
    DWORD Error;
    SIZE_T Size, Index;

    pBaseReadAppCompatDataForProcess = (PFN_READAPPCOMPAT)FindExport("BaseReadAppCompatDataForProcess");
    pBaseFreeAppCompatDataForProcess = (PFN_FREEAPPCOMPAT)FindExport("BaseFreeAppCompatDataForProcess");
    ok(pBaseReadAppCompatDataForProcess != NULL, "BaseReadAppCompatDataForProcess is missing\n");
    ok(pBaseFreeAppCompatDataForProcess != NULL, "BaseFreeAppCompatDataForProcess is missing\n");
    if (!pBaseReadAppCompatDataForProcess || !pBaseFreeAppCompatDataForProcess)
        return;

    Data = NULL;
    Error = pBaseReadAppCompatDataForProcess(GetCurrentProcess(), &Data, NULL);
    ok(Error == ERROR_SUCCESS, "self: %lu\n", Error);
    ok(Data != NULL, "self: no data\n");
    if (Error == ERROR_SUCCESS && Data)
    {
        Size = HeapSize(GetProcessHeap(), 0, Data);
        ok(Size == 4544, "self: heap size %Iu\n", Size);
        for (Index = 0; Index < 64; Index++)
        {
            if (((PUCHAR)Data)[Index])
                break;
        }
        ok(Index == 64, "self: nonzero byte at %Iu\n", Index);
        pBaseFreeAppCompatDataForProcess(Data);
    }

    Error = pBaseReadAppCompatDataForProcess(GetCurrentProcess(), NULL, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "no output: %lu\n", Error);

    Data = (PVOID)(ULONG_PTR)7;
    Error = pBaseReadAppCompatDataForProcess(NULL, &Data, NULL);
    ok(Error == ERROR_INVALID_PARAMETER, "no process: %lu\n", Error);
    ok(Data == (PVOID)(ULONG_PTR)7, "no process wrote %p\n", Data);

    Limited = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    ok(Limited != NULL, "OpenProcess failed with %lu\n", GetLastError());
    if (Limited)
    {
        Data = (PVOID)(ULONG_PTR)7;
        Error = pBaseReadAppCompatDataForProcess(Limited, &Data, NULL);
        ok(Error == ERROR_ACCESS_DENIED, "limited handle: %lu\n", Error);
        ok(Data == (PVOID)(ULONG_PTR)7, "limited handle wrote %p\n", Data);
        CloseHandle(Limited);
    }
}

START_TEST(AppModelInternal)
{
    TestAppModel();
    TestAppCompatData();
}
