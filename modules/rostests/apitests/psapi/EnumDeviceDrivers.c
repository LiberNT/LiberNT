/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for EnumDeviceDrivers and K32EnumDeviceDrivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arifing193@gmail.com>
 */

#include <stdio.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <apitest.h>
#include <ndk/ntndk.h>
#include <psapi.h>

typedef BOOL (WINAPI *PENUM_DEVICE_DRIVERS)(LPVOID *, DWORD, LPDWORD);

typedef struct _ENUM_CALL
{
    PENUM_DEVICE_DRIVERS Function;
    LPVOID *ImageBase;
    DWORD Size;
    DWORD Needed;
    BOOL Result;
    DWORD Error;
} ENUM_CALL, *PENUM_CALL;

static
DWORD
WINAPI
EnumDeviceDriversThread(
    _In_ LPVOID Parameter)
{
    PENUM_CALL Call = Parameter;

    SetLastError(0xdeadbeef);
    Call->Result = Call->Function(Call->ImageBase, Call->Size, &Call->Needed);
    Call->Error = GetLastError();
    return 0;
}

static
BOOL
CallEnumDeviceDrivers(
    _Inout_ PENUM_CALL Call)
{
    HANDLE Thread;
    DWORD Wait;

    Call->Needed = 0xdeadbeef;
    Call->Result = FALSE;
    Call->Error = 0;
    Thread = CreateThread(NULL, 0, EnumDeviceDriversThread, Call, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        return FALSE;

    Wait = WaitForSingleObject(Thread, 10000);
    ok(Wait == WAIT_OBJECT_0, "EnumDeviceDrivers did not return: wait %lu\n", Wait);
    CloseHandle(Thread);
    return Wait == WAIT_OBJECT_0;
}

static
ULONG
QueryKernelModuleCount(VOID)
{
    PRTL_PROCESS_MODULES Modules;
    NTSTATUS Status;
    ULONG Length = 0;
    ULONG Count;

    Status = NtQuerySystemInformation(SystemModuleInformation, NULL, 0, &Length);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "Status 0x%lx\n", Status);
    if (Length < sizeof(*Modules))
        return 0;

    Modules = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!Modules)
        return 0;

    Status = NtQuerySystemInformation(SystemModuleInformation, Modules, Length, &Length);
    ok(Status == STATUS_SUCCESS, "Status 0x%lx\n", Status);
    Count = NT_SUCCESS(Status) ? Modules->NumberOfModules : 0;
    HeapFree(GetProcessHeap(), 0, Modules);
    return Count;
}

static
VOID
TestEnumDeviceDrivers(
    _In_ PENUM_DEVICE_DRIVERS Function,
    _In_ PCSTR Name,
    _In_ ULONG ModuleCount)
{
    ENUM_CALL Call;
    LPVOID One[1];
    LPVOID *All;
    DWORD Needed;

    /* A one-entry array is too small; the call still succeeds and reports the whole size */
    Call.Function = Function;
    Call.ImageBase = One;
    Call.Size = sizeof(One);
    if (!CallEnumDeviceDrivers(&Call))
    {
        skip("%s did not return\n", Name);
        return;
    }
    ok(Call.Result == TRUE, "%s: result %d, error %lu\n", Name, Call.Result, Call.Error);
    ok(Call.Needed == ModuleCount * sizeof(LPVOID),
       "%s: needed %lu, expected %lu\n", Name, Call.Needed, (ULONG)(ModuleCount * sizeof(LPVOID)));

    Needed = Call.Needed;
    if (!Call.Result || Needed == 0 || Needed == 0xdeadbeef || (Needed % sizeof(LPVOID)) != 0)
    {
        skip("%s: no usable size\n", Name);
        return;
    }

    All = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Needed);
    if (!All)
    {
        skip("%s: no memory\n", Name);
        return;
    }

    Call.ImageBase = All;
    Call.Size = Needed;
    if (CallEnumDeviceDrivers(&Call))
    {
        ok(Call.Result == TRUE, "%s: result %d, error %lu\n", Name, Call.Result, Call.Error);
        ok(Call.Needed == Needed, "%s: needed %lu, expected %lu\n", Name, Call.Needed, Needed);
    }
    HeapFree(GetProcessHeap(), 0, All);
}

static
VOID
TestExport(
    _In_ PCWSTR ModuleName,
    _In_ PCSTR Name,
    _In_ ULONG ModuleCount)
{
    PENUM_DEVICE_DRIVERS Function;
    HMODULE Module;

    Module = LoadLibraryW(ModuleName);
    ok(Module != NULL, "%ls not loaded: %lu\n", ModuleName, GetLastError());
    if (!Module)
        return;

    Function = (PENUM_DEVICE_DRIVERS)GetProcAddress(Module, Name);
    ok(Function != NULL, "%ls!%s not exported\n", ModuleName, Name);
    if (Function)
        TestEnumDeviceDrivers(Function, Name, ModuleCount);
}

START_TEST(EnumDeviceDrivers)
{
    ULONG ModuleCount;

    ModuleCount = QueryKernelModuleCount();
    ok(ModuleCount > 4, "Module count %lu\n", ModuleCount);
    if (ModuleCount == 0)
    {
        skip("No kernel module count\n");
        return;
    }

    TestExport(L"psapi.dll", "EnumDeviceDrivers", ModuleCount);
    TestExport(L"kernel32.dll", "K32EnumDeviceDrivers", ModuleCount);
}
